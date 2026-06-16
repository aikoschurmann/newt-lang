#include "core/error.h"
#include <stdio.h>
#include <stdlib.h>

#ifndef _WIN32
#include <execinfo.h>
#include <unistd.h>
#endif

void ice_impl(const char *file, int line, const char *fmt, ...) {
    fprintf(stderr, "\033[1;31mINTERNAL COMPILER ERROR\033[0m at %s:%d\n", file, line);

    va_list args;
    va_start(args, fmt);
    fprintf(stderr, "Message: ");
    vfprintf(stderr, fmt, args);
    fprintf(stderr, "\n\n");
    va_end(args);

#ifndef _WIN32
    // Attempt stack trace (Unix-like systems)
    void *buffer[100];
    int nptrs = backtrace(buffer, 100);
    fprintf(stderr, "Stack Trace:\n");
    backtrace_symbols_fd(buffer, nptrs, fileno(stderr));
#else
    fprintf(stderr, "Stack trace not available on Windows.\n");
#endif

    fprintf(stderr, "\nThis is a bug in the compiler. Please report it.\n");
    abort();
}

void ice_impl_at(const char *file, int line, const char *src_file, Span span, const char *fmt, ...) {
    fprintf(stderr, "\033[1;31mINTERNAL COMPILER ERROR\033[0m at %s:%d\n", file, line);
    fprintf(stderr, "  Source: %s:%zu:%zu\n", src_file ? src_file : "?", span.start_line, span.start_col);

    va_list args;
    va_start(args, fmt);
    fprintf(stderr, "Message: ");
    vfprintf(stderr, fmt, args);
    fprintf(stderr, "\n\n");
    va_end(args);

#ifndef _WIN32
    // Attempt stack trace (Unix-like systems)
    void *buffer[100];
    int nptrs = backtrace(buffer, 100);
    fprintf(stderr, "Stack Trace:\n");
    backtrace_symbols_fd(buffer, nptrs, fileno(stderr));
#else
    fprintf(stderr, "Stack trace not available on Windows.\n");
#endif

    fprintf(stderr, "\nThis is a bug in the compiler. Please report it.\n");
    abort();
}