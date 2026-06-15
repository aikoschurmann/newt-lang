#pragma once
#include <stdint.h>
#include <string.h>


typedef struct {
    size_t start_line;
    size_t start_col;
    size_t end_line;
    size_t end_col;
} Span;

// ------------------------------
// A slice: view into the source buffer
// ------------------------------
typedef struct {
    const char* ptr;  // pointer into source buffer
    uint32_t len;     // length of the slice
} Slice;

static inline size_t slice_hash(void *p) {
    Slice *s = (Slice*)p;
    size_t h = (size_t)1469598103934665603ULL; /* FNV-1a 64-bit */
    for (size_t i = 0; i < s->len; ++i) {
        h ^= (unsigned char)s->ptr[i];
        h *= (size_t)1099511628211ULL;
    }
    return h;
}

static inline int slice_cmp(void *a, void *b) {
    Slice *sa = (Slice*)a;
    Slice *sb = (Slice*)b;
    if (sa->len != sb->len) return (sa->len < sb->len) ? -1 : 1;
    return memcmp(sa->ptr, sb->ptr, sa->len);
}

static inline Span span_join(const Span *a, const Span *b) {
    if (!a || !b) return (Span){0,0,0,0};
    return (Span){a->start_line, a->start_col, b->end_line, b->end_col};
}

static inline size_t ptr_hash(void *key) {
    // Shift right to remove alignment zeros (usually 3 or 4 bits)
    // and mix slightly to avoid collisions in low buckets
    size_t k = (size_t)key;
    return (k >> 4) ^ (k >> 9);
}

static inline int ptr_cmp(void *a, void *b) {
    // Compare pointer identity: Three-way comparison (Q-3)
    return (a > b) - (a < b);
}

static inline size_t str_hash(void *key) {
    const char *s = (const char *)key;
    size_t h = (size_t)1469598103934665603ULL;
    while (*s) {
        h ^= (unsigned char)*s++;
        h *= (size_t)1099511628211ULL;
    }
    return h;
}

static inline int str_cmp(void *a, void *b) {
    return strcmp((const char *)a, (const char *)b);
}

double now_seconds(void);
size_t get_peak_rss_kb(void);

void *xmalloc(size_t size);
void *xcalloc(size_t nmemb, size_t size);
char *xstrdup(const char *s);

char *get_runtime_path(void);