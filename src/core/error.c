#include "core/error.h"
#include <stdio.h>
#include <stdlib.h>
#include <execinfo.h>

void ice_impl(const char *file, int line, const char *fmt, ...) {
    fprintf(stderr, "\033[1;31mINTERNAL COMPILER ERROR\033[0m at %s:%d\n", file, line);
    
    va_list args;
    va_start(args, fmt);
    fprintf(stderr, "Message: ");
    vfprintf(stderr, fmt, args);
    fprintf(stderr, "\n\n");
    va_end(args);

    // Attempt stack trace (Unix-like systems)
    void *buffer[100];
    int nptrs = backtrace(buffer, 100);
    fprintf(stderr, "Stack Trace:\n");
    backtrace_symbols_fd(buffer, nptrs, fileno(stderr));
    
    fprintf(stderr, "\nThis is a bug in the compiler. Please report it.\n");
    abort();
}