#include "core/utils.h"
#include <time.h>
#include <stdio.h>
#include <stdlib.h>

#ifdef _WIN32
    #include <windows.h>
    #include <psapi.h>
#else
    #include <sys/resource.h>
    #include <sys/time.h>
#endif

double now_seconds(void) {
#ifdef _WIN32
    LARGE_INTEGER frequency;
    LARGE_INTEGER now;
    QueryPerformanceFrequency(&frequency);
    QueryPerformanceCounter(&now);
    return (double)now.QuadPart / (double)frequency.QuadPart;
#else
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (double)ts.tv_sec + (double)ts.tv_nsec / 1e9;
#endif
}

size_t get_peak_rss_kb(void) {
#ifdef _WIN32
    PROCESS_MEMORY_COUNTERS pmc;
    if (GetProcessMemoryInfo(GetCurrentProcess(), &pmc, sizeof(pmc))) {
        return pmc.PeakWorkingSetSize / 1024;
    }
    return 0;
#else
    struct rusage r;
    getrusage(RUSAGE_SELF, &r);
    return (size_t)r.ru_maxrss / 1024;
#endif
}

void *xmalloc(size_t size) {
    void *p = malloc(size);
    if (!p && size > 0) {
        fprintf(stderr, "fatal: out of memory (malloc %zu bytes)\n", size);
        abort();
    }
    return p;
}

void *xcalloc(size_t nmemb, size_t size) {
    void *p = calloc(nmemb, size);
    if (!p && nmemb > 0 && size > 0) {
        fprintf(stderr, "fatal: out of memory (calloc %zu * %zu bytes)\n", nmemb, size);
        abort();
    }
    return p;
}

char *xstrdup(const char *s) {
    if (!s) return NULL;
    char *p = strdup(s);
    if (!p) {
        fprintf(stderr, "fatal: out of memory (strdup)\n");
        abort();
    }
    return p;
}