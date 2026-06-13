#pragma once

#pragma once

#include <stdarg.h>
#include <stdbool.h>

/**
 * @brief Internal Compiler Error (ICE). 
 * Prints a fatal error message with file/line information and aborts.
 * Used for asserting structural invariants that SHOULD have been caught by Sema.
 */
#define ICE(msg, ...) ice_impl(__FILE__, __LINE__, msg, ##__VA_ARGS__)

void ice_impl(const char *file, int line, const char *fmt, ...) __attribute__((noreturn));
