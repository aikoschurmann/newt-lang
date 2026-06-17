#include "compiler_helpers.h"
#include "../harness/test_harness.h"
#include "module_loader.h"
#include <sys/stat.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>

#ifdef _WIN32
    #include <windows.h>
    #include <io.h>
    #include <fcntl.h>
    #ifndef S_ISDIR
        #define stat _stat
        #define S_ISDIR(m) (((m) & _S_IFMT) == _S_IFDIR)
    #endif
    #define dup _dup
    #define dup2 _dup2
    #define pipe(fds) _pipe(fds, 4096, _O_BINARY)
    #define read _read
    #define close _close
#else
    #include <dirent.h>
    #include <unistd.h>
#endif

static char* read_entire_file(const char* path) {
    FILE* f = fopen(path, "rb");
    if (!f) return NULL;
    fseek(f, 0, SEEK_END);
    long size = ftell(f);
    fseek(f, 0, SEEK_SET);
    char* buf = malloc(size + 1);
    fread(buf, 1, size, f);
    buf[size] = '\0';
    fclose(f);
    return buf;
}

static int run_single_fixture(const char *dir_path, const char *name) {
    Arena *arena = arena_create(4 * 1024 * 1024);
    DenseArenaInterner *keywords = intern_table_create(hashmap_create(arena, 32), arena, string_copy_func, slice_hash, slice_cmp);
    DenseArenaInterner *identifiers = intern_table_create(hashmap_create(arena, 256), arena, string_copy_func, slice_hash, slice_cmp);
    DenseArenaInterner *strings = intern_table_create(hashmap_create(arena, 128), arena, string_copy_func, slice_hash, slice_cmp);
    lexer_populate_default_keywords(keywords);

    Options opts = { .verbose = false, .stdlib_path = (char*)"lib" };
    ModuleLoader *loader = module_loader_create(arena, &opts, keywords, identifiers, strings);
    
    char main_path[512];
    snprintf(main_path, sizeof(main_path), "%s/main.tn", dir_path);
    
    int load_res = load_module_recursive(loader, main_path, NULL, NULL, 0);
    if (load_res != 0) {
        test_log("      %s✗%s %-30s (Load failed: %d)\n", COL_RED, COL_RESET, name, load_res);
        arena_destroy(arena);
        return 0;
    }

    TypeStore *store = typestore_create(arena, identifiers, keywords);
    TypeCheckContext sema_ctx = typecheck_context_create(arena, store, identifiers, keywords, main_path, loader);
    typecheck_program(&sema_ctx);

    char expect_path[512];
    snprintf(expect_path, sizeof(expect_path), "%s/expect.txt", dir_path);
    char *expect_content = read_entire_file(expect_path);
    if (!expect_content) {
        if (sema_ctx.errors->count > 0) {
            test_log("      %s✗%s %-30s (Unexpected sema errors)\n", COL_RED, COL_RESET, name);
            arena_destroy(arena);
            return 0;
        }
        test_log("      %s✓%s %-30s\n", COL_GREEN, COL_RESET, name);
        arena_destroy(arena);
        return 1;
    }

    int success = 1;
    int expected_errors = -1;
    int expected_exit = -1;
    char *expected_output = malloc(strlen(expect_content) + 1);
    expected_output[0] = '\0';
    size_t out_pos = 0;

    char *saveptr;
    char *content_copy = strdup(expect_content);
    char *line = strtok_r(content_copy, "\n", &saveptr);
    while (line) {
        if (strncmp(line, "error:", 6) == 0) {
            expected_errors = atoi(line + 6);
        } else if (strncmp(line, "exit:", 5) == 0) {
            expected_exit = atoi(line + 5);
        } else {
            size_t len = strlen(line);
            memcpy(expected_output + out_pos, line, len);
            out_pos += len;
            expected_output[out_pos++] = '\n';
            expected_output[out_pos] = '\0';
        }
        line = strtok_r(NULL, "\n", &saveptr);
    }
    free(content_copy);

    if (expected_errors != -1) {
        if ((int)sema_ctx.errors->count != expected_errors) {
            test_log("      %s✗%s %-30s (Error count mismatch: %zu != %d)\n", COL_RED, COL_RESET, name, sema_ctx.errors->count, expected_errors);
            success = 0;
        }
    } else if (sema_ctx.errors->count > 0) {
        test_log("      %s✗%s %-30s (Unexpected sema errors)\n", COL_RED, COL_RESET, name);
        success = 0;
    }

    if (success && (expected_exit != -1 || out_pos > 0)) {
        CodegenContext *cg_ctx = codegen_context_create(store, "jit_module", 0, loader);
        if (codegen_program(cg_ctx) == 0) {
            int pipe_fds[2];
            int stdout_save = -1;
            
            if (out_pos > 0) {
                fflush(stdout);
                stdout_save = dup(STDOUT_FILENO);
                if (pipe(pipe_fds) != 0) {
                    test_log("      %s✗%s %-30s (Failed to create pipe)\n", COL_RED, COL_RESET, name);
                    success = 0;
                } else {
                    dup2(pipe_fds[1], STDOUT_FILENO);
                    close(pipe_fds[1]);
                }
            }

            int actual_exit = codegen_run_jit(cg_ctx);

            if (out_pos > 0 && success) {
                fflush(stdout);
                dup2(stdout_save, STDOUT_FILENO);
                close(stdout_save);

                char actual_output[8192]; // Adjust buffer size as needed
                ssize_t n = read(pipe_fds[0], actual_output, sizeof(actual_output) - 1);
                if (n < 0) n = 0;
                actual_output[n] = '\0';
                close(pipe_fds[0]);

                if (strcmp(actual_output, expected_output) != 0) {
                    test_log("      %s✗%s %-30s (Output mismatch)\n", COL_RED, COL_RESET, name);
                    // Optional: diff print
                    success = 0;
                }
            }

            if (success && expected_exit != -1 && actual_exit != expected_exit) {
                test_log("      %s✗%s %-30s (Exit code: %d != %d)\n", COL_RED, COL_RESET, name, actual_exit, expected_exit);
                success = 0;
            }
        } else {
            test_log("      %s✗%s %-30s (Codegen failed)\n", COL_RED, COL_RESET, name);
            success = 0;
        }
        codegen_context_destroy(cg_ctx);
    }

    if (success) {
        test_log("      %s✓%s %-30s\n", COL_GREEN, COL_RESET, name);
    }

    free(expected_output);
    free(expect_content);
    arena_destroy(arena);
    return success;
}

TEST_CASE_PRIO("Fixtures: Module Loader", 50) {
    const char *base_path = "test/fixtures/modules";
    int total_success = 1;

#ifdef _WIN32
    WIN32_FIND_DATAA find_data;
    char search_path[512];
    snprintf(search_path, sizeof(search_path), "%s/*", base_path);
    HANDLE hFind = FindFirstFileA(search_path, &find_data);
    
    if (hFind == INVALID_HANDLE_VALUE) return 1;

    do {
        if (find_data.cFileName[0] == '.') continue;
        
        char full_path[512];
        snprintf(full_path, sizeof(full_path), "%s/%s", base_path, find_data.cFileName);
        
        struct stat st;
        if (stat(full_path, &st) == 0 && S_ISDIR(st.st_mode)) {
            if (!run_single_fixture(full_path, find_data.cFileName)) {
                total_success = 0;
            }
        }
    } while (FindNextFileA(hFind, &find_data) != 0);
    FindClose(hFind);
#else
    DIR *dir = opendir(base_path);
    if (!dir) return 1;

    struct dirent *entry;
    while ((entry = readdir(dir)) != NULL) {
        if (entry->d_name[0] == '.') continue;
        
        char full_path[512];
        snprintf(full_path, sizeof(full_path), "%s/%s", base_path, entry->d_name);
        
        struct stat st;
        if (stat(full_path, &st) == 0 && S_ISDIR(st.st_mode)) {
            if (!run_single_fixture(full_path, entry->d_name)) {
                total_success = 0;
            }
        }
    }
    closedir(dir);
#endif

    return total_success;
}
