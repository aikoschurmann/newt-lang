#include "compiler_helpers.h"
#include "../harness/test_harness.h"
#include "module_loader.h"
#include <sys/stat.h>
#include <string.h>
#include <stdlib.h>

#ifdef _WIN32
    #include <windows.h>
    // MSYS2 handles stat and S_ISDIR natively. 
    // We only redefine them if they are strictly missing (e.g., pure MSVC)
    #ifndef S_ISDIR
        #define stat _stat
        #define S_ISDIR(m) (((m) & _S_IFMT) == _S_IFDIR)
    #endif
#else
    #include <dirent.h>
#endif

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
// 5. Sema
TypeStore *store = typestore_create(arena, identifiers, keywords);
TypeCheckContext sema_ctx = typecheck_context_create(arena, store, identifiers, keywords, main_path, loader);
typecheck_program(&sema_ctx);


    char expect_path[512];
    snprintf(expect_path, sizeof(expect_path), "%s/expect.txt", dir_path);
    FILE *f = fopen(expect_path, "r");
    if (!f) {
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
    char line[256];
    while (fgets(line, sizeof(line), f)) {
        line[strcspn(line, "\n")] = 0;
        if (strncmp(line, "error:", 6) == 0) {
            const char *count_str = line + 6;
            while (*count_str == ' ') count_str++;

            if (*count_str >= '0' && *count_str <= '9') {
                int expected_errors = atoi(count_str);
                if ((int)sema_ctx.errors->count != expected_errors) {
                    test_log("      %s✗%s %-30s (Error count mismatch: %zu != %d)\n", COL_RED, COL_RESET, name, sema_ctx.errors->count, expected_errors);
                    success = 0;
                }
            } else {
                if (sema_ctx.errors->count == 0) {
                    test_log("      %s✗%s %-30s (Expected error, but passed)\n", COL_RED, COL_RESET, name);
                    success = 0;
                }
            }
        }
        else if (strncmp(line, "exit:", 5) == 0) {
            if (sema_ctx.errors->count > 0) {
                test_log("      %s✗%s %-30s (Sema errors occurred, skipping codegen)\n", COL_RED, COL_RESET, name);
                success = 0;
            } else {
                int expected_exit = atoi(line + 5);
                CodegenContext *cg_ctx = codegen_context_create(store, "jit_module", 0, loader);
                if (codegen_program(cg_ctx) == 0) {
                    int actual_exit = codegen_run_jit(cg_ctx);
                    if (actual_exit != expected_exit) {
                        test_log("      %s✗%s %-30s (Exit code: %d != %d)\n", COL_RED, COL_RESET, name, actual_exit, expected_exit);
                        success = 0;
                    }
                } else {
                    test_log("      %s✗%s %-30s (Codegen failed)\n", COL_RED, COL_RESET, name);
                    success = 0;
                }
                codegen_context_destroy(cg_ctx);
            }
        }
    }

    if (success) {
        test_log("      %s✓%s %-30s\n", COL_GREEN, COL_RESET, name);
    }

    fclose(f);
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
