#include "test_harness.h"
#include <time.h>

static int g_tests_run = 0;
static int g_tests_passed = 0;
static int g_tests_failed = 0;

void run_test(const char *name, TestFunc func) {
    g_tests_run++;
    
    // Print "running" state
    fprintf(stderr, " ... %-40s", name);
    fflush(stderr);
    
    struct timespec start, end;
    clock_gettime(CLOCK_MONOTONIC, &start);
    int result = func();
    clock_gettime(CLOCK_MONOTONIC, &end);
    
    double diff = (double)(end.tv_sec - start.tv_sec) + 
                  (double)(end.tv_nsec - start.tv_nsec) / 1e9;

    if (result) {
        g_tests_passed++;
        // \r moves to start of line, ANSI clear line (optional), then print checked
        fprintf(stderr, "\r %s✓%s %-40s %s%8.3fms%s\n", 
            COL_GREEN, COL_RESET, 
            name, 
            COL_CYAN, diff * 1000.0, COL_RESET);
    } else {
        g_tests_failed++;
        // If ASSERT failed, it printed an error message with newline.
        // We just print the fail marker
        fprintf(stderr, "\n %s✗%s %-40s\n", COL_RED, COL_RESET, name);
    }
}

int print_test_summary() {
    fprintf(stderr, "\n%s=== Test Summary ===%s\n", COL_BOLD, COL_RESET);
    fprintf(stderr, "  Total:  %d\n", g_tests_run);
    fprintf(stderr, "  Passed: %s%d%s\n", COL_GREEN, g_tests_passed, COL_RESET);
    fprintf(stderr, "  Failed: %s%d%s\n", g_tests_failed ? COL_RED : COL_GREEN, g_tests_failed, COL_RESET);
    fprintf(stderr, "%s====================%s\n", COL_BOLD, COL_RESET);
    
    return g_tests_failed > 0 ? 1 : 0;
}
