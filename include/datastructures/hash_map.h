#pragma once

#include <stddef.h>
#include <stdbool.h>
#include "dynamic_array.h"
#include "core/utils.h"

/* -----------------------------
   HashMap API (separate chaining)
   ----------------------------- */

typedef struct {
    void *key;
    void *value;
} KeyValue;

typedef struct {
    DynArray *buckets;     /* array of buckets, each bucket is a DynArray of KeyValue */
    size_t bucket_count;   /* number of buckets */
    size_t size;           /* total number of key-value pairs */
} HashMap;

/* Constructor / Destructor */
HashMap* hashmap_create(size_t bucket_count);

void hashmap_destroy(
    HashMap* map,
    void (*free_key)(void*),
    void (*free_value)(void*)
);

/* Insert or update */
bool hashmap_put(
    HashMap* map,
    void* key,
    void* value,
    size_t (*hash)(void*),
    int (*cmp)(void*, void*)
);

/* Lookup */
void* hashmap_get(
    HashMap* map,
    void* key,
    size_t (*hash)(void*),
    int (*cmp)(void*, void*)
);

/* Remove */
bool hashmap_remove(
    HashMap* map,
    void* key,
    size_t (*hash)(void*),
    int (*cmp)(void*, void*),
    void (*free_key)(void*),
    void (*free_value)(void*)
);

/* Resize / Rehash */
bool hashmap_rehash(
    HashMap* map,
    size_t new_bucket_count,
    size_t (*hash)(void*),
    int (*cmp)(void*, void*)
);

/* Utility */
size_t hashmap_size(HashMap* map);

void hashmap_foreach(
    HashMap* map,
    void (*func)(void* key, void* value)
);

