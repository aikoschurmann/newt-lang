#include <stdio.h>
#include <stdint.h>

#ifdef _WIN32
  #define RUNTIME_EXPORT __declspec(dllexport)
#else
  #define RUNTIME_EXPORT
#endif

RUNTIME_EXPORT void print_i32(int32_t val);
RUNTIME_EXPORT void print_i64(int64_t val);
RUNTIME_EXPORT void print_f32(float val);
RUNTIME_EXPORT void print_f64(double val);
RUNTIME_EXPORT void print_bool(int val);
RUNTIME_EXPORT void print_str(const char *s);
RUNTIME_EXPORT void print_char(char c);
RUNTIME_EXPORT void print_ptr(void *p);
RUNTIME_EXPORT void print_newline(void);

RUNTIME_EXPORT void print_i32(int32_t val) {
    printf("%d", val);
}

RUNTIME_EXPORT void print_i64(int64_t val) {
    printf("%lld", (long long)val);
}

RUNTIME_EXPORT void print_f32(float val) {
    printf("%g", (double)val);
}

RUNTIME_EXPORT void print_f64(double val) {
    printf("%g", val);
}

RUNTIME_EXPORT void print_bool(int val) {
    printf("%s", val ? "true" : "false");
}

RUNTIME_EXPORT void print_str(const char *s) {
    if (s) printf("%s", s);
    else printf("(null)");
}

RUNTIME_EXPORT void print_char(char c) {
    printf("%c", c);
}

RUNTIME_EXPORT void print_ptr(void *p) {
    if (p) printf("%p", p);
    else printf("null");
}

RUNTIME_EXPORT void print_newline(void) {
    printf("\n");
}