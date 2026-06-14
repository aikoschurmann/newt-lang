// arena.c - improved, safer arena with alignment + stats
#include "arena.h"
#include <stdlib.h>
#include <string.h>
#include <stddef.h>
#include <stdint.h>
#include <stdalign.h>
#include <limits.h>

static size_t align_up(size_t v, size_t a) {
    return (v + a - 1) & ~(a - 1);
}

Arena *arena_create(size_t initial_capacity) {
    if (initial_capacity == 0) initial_capacity = 1024;
    /* sanity cap to avoid absurd allocations */
    if (initial_capacity > (SIZE_MAX / 2)) initial_capacity = 1024;

    Arena *arena = malloc(sizeof(Arena));
    if (!arena) return NULL;

    /* ensure initial allocation checked for overflow */
    size_t header = sizeof(ArenaBlock);
    size_t alloc_req;
    if (initial_capacity > SIZE_MAX - header) { free(arena); return NULL; }
    alloc_req = header + initial_capacity;

    ArenaBlock *block = malloc(alloc_req);
    if (!block) { free(arena); return NULL; }

    block->next = NULL;
    block->capacity = initial_capacity;
    block->used = 0;

    arena->blocks = block;
    arena->block_size = initial_capacity;
    return arena;
}

void arena_destroy(Arena *arena) {
    if (!arena) return;
    ArenaBlock *b = arena->blocks;
    while (b) {
        ArenaBlock *n = b->next;
        free(b);
        b = n;
    }
    free(arena);
}

/* Reset arena: keep only the first block and reuse it. */
void arena_reset(Arena *arena) {
    if (!arena) return;
    /* free all but first block */
    ArenaBlock *b = arena->blocks->next;
    while (b) {
        ArenaBlock *n = b->next;
        free(b);
        b = n;
    }
    arena->blocks->next = NULL;
    arena->blocks->used = 0;
}

/* Allocate aligned size from the arena. Returns NULL on OOM. */
void *arena_alloc(Arena *arena, size_t size) {
    if (!arena) return NULL;
    if (size == 0) return NULL; /* semantic choice */

    const size_t align = alignof(max_align_t);

    ArenaBlock *block = arena->blocks;
    if (!block) return NULL;

    /* align the *offset*, not just the size */
    size_t offset = align_up(block->used, align);

    /* If not enough room in current block, allocate a new one */
    if (offset + size > block->capacity) {
        /* Determine new capacity: at least arena->block_size, but grow until it fits */
        size_t new_capacity = arena->block_size;
        while (new_capacity < size) {
            if (new_capacity > SIZE_MAX / 2) { new_capacity = size; break; }
            new_capacity *= 2;
        }

        /* allocate block (check overflow) */
        size_t header = sizeof(ArenaBlock);
        if (new_capacity > SIZE_MAX - header) return NULL;
        ArenaBlock *new_block = malloc(header + new_capacity);
        if (!new_block) return NULL;

        new_block->next = arena->blocks;
        new_block->capacity = new_capacity;
        new_block->used = 0;
        arena->blocks = new_block;
        block = new_block;
        offset = 0;
    }

    void *ptr = (void*)(block->data + offset);
    block->used = offset + align_up(size, align); /* bump after alignment */
    return ptr;
}

void *arena_calloc(Arena *arena, size_t size) {
    void *p = arena_alloc(arena, size);
    if (!p) return NULL;
    memset(p, 0, size);
    return p;
}

/* Debug helpers */
size_t arena_bytes_used(const Arena *arena) {
    if (!arena) return 0;
    size_t total = 0;
    for (const ArenaBlock *b = arena->blocks; b; b = b->next)
        total += b->used;
    return total;
}

size_t arena_bytes_capacity(const Arena *arena) {
    if (!arena) return 0;
    size_t total = 0;
    for (const ArenaBlock *b = arena->blocks; b; b = b->next)
        total += b->capacity;
    return total;
}
size_t arena_block_count(const Arena *arena) {
    if (!arena) return 0;
    size_t count = 0;
    for (const ArenaBlock *b = arena->blocks; b; b = b->next)
        count++;
    return count;
}

size_t arena_total_allocated(const Arena *arena) {
    if (!arena) return 0;
    size_t total = 0;
    for (const ArenaBlock *b = arena->blocks; b; b = b->next)
        total += b->used;
    return total;
}