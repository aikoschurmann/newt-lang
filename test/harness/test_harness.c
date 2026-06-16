#include "test_harness.h"
#include "core/utils.h"
#include <time.h>
#include <stdarg.h>

#ifdef _WIN32
    #include <io.h>
    #define dup _dup
    #define dup2 _dup2
    #define fileno _fileno
#else
    #include <unistd.h>
#endif

#define MAX_TESTS 2048
#define LOG_BUFFER_SIZE 16384

typedef struct {
    const char *name;
    TestFunc func;
    int priority;
} TestEntry;

static TestEntry g_registry[MAX_TESTS];
static int g_registry_count = 0;

static int g_tests_run = 0;
static int g_tests_passed = 0;
static int g_tests_failed = 0;

static char g_log_buffer[LOG_BUFFER_SIZE];
static size_t g_log_len = 0;

void test_clear_log(void) {
    g_log_len = 0;
    g_log_buffer[0] = '\0';
}

void test_log(const char *fmt, ...) {
    if (g_log_len >= LOG_BUFFER_SIZE - 1) return;
    
    va_list args;
    va_start(args, fmt);
    int written = vsnprintf(g_log_buffer + g_log_len, LOG_BUFFER_SIZE - g_log_len, fmt, args);
    va_end(args);
    
    if (written > 0) {
        g_log_len += (size_t)written;
    }
}

void test_registry_add(const char *name, TestFunc func, int priority) {
    if (g_registry_count < MAX_TESTS) {
        g_registry[g_registry_count++] = (TestEntry){name, func, priority};
    } else {
        fprintf(stderr, "Error: Test registry overflow\n");
    }
}

static void run_single_test(const char *name, TestFunc func) {
    g_tests_run++;
    test_clear_log();

    const char *sub = name;
    const char *slash = strrchr(name, '/');
    if (slash) sub = slash + 1;

    fprintf(stdout, "      %-50s", sub);
    fflush(stdout);
    
    // Redirect stdout and stderr to a temporary file
    int stdout_save = dup(STDOUT_FILENO);
    int stderr_save = dup(STDERR_FILENO);
    FILE *tmp = tmpfile();
    if (tmp) {
        int tmp_fd = fileno(tmp);
        fflush(stdout);
        fflush(stderr);
        dup2(tmp_fd, STDOUT_FILENO);
        dup2(tmp_fd, STDERR_FILENO);
    }

    double start = now_seconds();
    int result = func();
    double end = now_seconds();
    
    // Restore stdout and stderr
    if (tmp) {
        fflush(stdout);
        fflush(stderr);
        dup2(stdout_save, STDOUT_FILENO);
        dup2(stderr_save, STDERR_FILENO);
    }
    close(stdout_save);
    close(stderr_save);

    double diff_ms = (end - start) * 1000.0;

    if (result) {
        g_tests_passed++;
        fprintf(stdout, "\r\033[K    %s✓%s %-50s %s%8.3fms%s\n", 
            COL_GREEN, COL_RESET, sub, COL_CYAN, diff_ms, COL_RESET);
            
        if (g_log_len > 0) {
            fprintf(stdout, "%s", g_log_buffer);
            if (g_log_buffer[g_log_len - 1] != '\n') fprintf(stdout, "\n");
        }
    } else {
        g_tests_failed++;
        fprintf(stdout, "\r\033[K    %s✗%s %-50s\n", COL_RED, COL_RESET, sub);
        
        if (tmp) {
            rewind(tmp);
            char buf[1024];
            bool has_output = false;
            while (fgets(buf, sizeof(buf), tmp)) {
                if (!has_output) {
                    fprintf(stdout, "      %s--- Captured Output ---%s\n", COL_YELLOW, COL_RESET);
                    has_output = true;
                }
                fprintf(stdout, "      %s", buf);
            }
            if (has_output) {
                fprintf(stdout, "      %s-----------------------%s\n", COL_YELLOW, COL_RESET);
            }
        }

        if (g_log_len > 0) {
            fprintf(stdout, "%s", g_log_buffer);
            if (g_log_buffer[g_log_len - 1] != '\n') fprintf(stdout, "\n");
        }
    }
    if (tmp) fclose(tmp);
}

int run_all_registered_tests(int argc, char **argv) {
    const char *filter = NULL;
    bool fail_fast = false;

    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--filter") == 0 && i + 1 < argc) {
            filter = argv[++i];
        } else if (strcmp(argv[i], "--fail-fast") == 0) {
            fail_fast = true;
        }
    }

    // Sort by priority
    for (int i = 0; i < g_registry_count - 1; i++) {
        for (int j = 0; j < g_registry_count - i - 1; j++) {
            if (g_registry[j].priority > g_registry[j+1].priority) {
                TestEntry tmp = g_registry[j];
                g_registry[j] = g_registry[j+1];
                g_registry[j+1] = tmp;
            }
        }
    }

    fprintf(stdout, "\n%s=== Compiler Test Suite ===%s\n", COL_BOLD, COL_RESET);
    
    const char *current_cat = NULL;
    for (int i = 0; i < g_registry_count; i++) {
        if (filter && strstr(g_registry[i].name, filter) == NULL) continue;

        char cat[64] = {0};
        const char *sep = strpbrk(g_registry[i].name, "/:");
        if (sep) {
            size_t len = (size_t)(sep - g_registry[i].name);
            if (len < sizeof(cat)) memcpy(cat, g_registry[i].name, len);
        } else {
            strcpy(cat, "General");
        }

        if (current_cat == NULL || strcmp(cat, current_cat) != 0) {
            fprintf(stdout, "\n %s[%s]%s\n", COL_BOLD, cat, COL_RESET);
            if (current_cat) free((void*)current_cat);
            current_cat = strdup(cat);
        }
        
        run_single_test(g_registry[i].name, g_registry[i].func);
        
        if (fail_fast && g_tests_failed > 0) {
            fprintf(stdout, "\n%sStopping early due to failure%s\n", COL_RED, COL_RESET);
            break;
        }
    }

    if (current_cat) free((void*)current_cat);

    fprintf(stdout, "\n%s=== Summary ===%s\n", COL_BOLD, COL_RESET);
    fprintf(stdout, "  Total:  %d\n", g_tests_run);
    fprintf(stdout, "  Passed: %s%d%s\n", COL_GREEN, g_tests_passed, COL_RESET);
    fprintf(stdout, "  Failed: %s%d%s\n", g_tests_failed ? COL_RED : COL_GREEN, g_tests_failed, COL_RESET);
    fprintf(stdout, "%s===============%s\n", COL_BOLD, COL_RESET);
    
    return g_tests_failed > 0 ? 1 : 0;
}
