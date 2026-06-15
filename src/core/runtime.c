#include <stdio.h>
#include <stdint.h>

void print_i32(int32_t val);
void print_i64(int64_t val);
void print_f32(float val);
void print_f64(double val);
void print_bool(int val);
void print_str(const char *s);
void print_char(char c);
void print_ptr(void *p);
void print_newline(void);

void print_i32(int32_t val) {
    printf("%d", val);
}

void print_i64(int64_t val) {
    printf("%lld", (long long)val);
}

void print_f32(float val) {
    printf("%g", (double)val);
}

void print_f64(double val) {
    printf("%g", val);
}

void print_bool(int val) {
    printf("%s", val ? "true" : "false");
}

void print_str(const char *s) {
    if (s) printf("%s", s);
    else printf("(null)");
}

void print_char(char c) {
    printf("%c", c);
}

void print_ptr(void *p) {
    if (p) printf("%p", p);
    else printf("null");
}

void print_newline(void) {
    printf("\n");
}
