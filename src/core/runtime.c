#include <stdio.h>
#include <stdint.h>

void print_i32(int32_t val) {
    printf("%d", val);
}

void print_i64(int64_t val) {
    printf("%lld", (long long)val);
}

void print_f32(float val) {
    printf("%f", val);
}

void print_f64(double val) {
    printf("%f", val);
}

void print_bool(int val) {
    printf("%s", val ? "true" : "false");
}

void print_str(const char *s) {
    printf("%s", s);
}

void print_char(char c) {
    printf("%c", c);
}

void print_newline() {
    printf("\n");
}
