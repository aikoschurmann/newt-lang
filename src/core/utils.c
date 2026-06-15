#include "core/utils.h"
#include <time.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

#ifdef _WIN32
    #include <windows.h>
    #include <psapi.h>
#else
    #include <sys/resource.h>
    #include <sys/time.h>
#endif

#ifdef __APPLE__
    #include <mach-o/dyld.h>
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

char *get_runtime_path(void) {
    // 1. Try environment variable
    char *env = getenv("COMPILER_RUNTIME_PATH");
    if (env) return xstrdup(env);

    // 2. Try relative to CWD
    if (access("src/core/runtime.c", F_OK) == 0) {
        return xstrdup("src/core/runtime.c");
    }

    // 3. Try relative to executable
    char exe_path[1024];
    uint32_t size = sizeof(exe_path);
#ifdef __APPLE__
    if (_NSGetExecutablePath(exe_path, &size) == 0) {
        char *last_slash = strrchr(exe_path, '/');
        if (last_slash) {
            *last_slash = '\0';
            char runtime_path[1024];
            // Assuming executable is in out/ and runtime is in src/core/
            // out/.. -> project root
            snprintf(runtime_path, sizeof(runtime_path), "%s/../src/core/runtime.c", exe_path);
            if (access(runtime_path, F_OK) == 0) {
                return xstrdup(runtime_path);
            }
        }
    }
#elif defined(__linux__)
    ssize_t len = readlink("/proc/self/exe", exe_path, sizeof(exe_path)-1);
    if (len != -1) {
        exe_path[len] = '\0';
        char *last_slash = strrchr(exe_path, '/');
        if (last_slash) {
            *last_slash = '\0';
            char runtime_path[1024];
            snprintf(runtime_path, sizeof(runtime_path), "%s/../src/core/runtime.c", exe_path);
            if (access(runtime_path, F_OK) == 0) {
                return xstrdup(runtime_path);
            }
        }
    }
#endif

    // Fallback to hardcoded default
    return xstrdup("src/core/runtime.c");
}