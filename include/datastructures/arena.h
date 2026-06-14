#pragma once
#include <stddef.h>

/*
 * A memory arena is a pre-allocated block of memory used for fast, efficient
 * allocation of many small objects or strings. Instead of calling malloc() 
 * repeatedly (which incurs overhead and fragmentation), the arena allocates 
 * memory linearly from its internal buffer. This is particularly useful 
 * in compilers or lexers where many short-lived objects (like tokens or 
 * identifiers) need to be allocated quickly. 
 *
 * Advantages:
 *   - Very fast allocations (simple pointer bumping).
 *   - Low overhead compared to repeated malloc/free.
 *   - Good cache locality for sequential allocations.
 *
 * Limitations:
 *   - Individual objects cannot be freed; the entire arena is freed at once.
 *   - Memory usage may be higher if the arena is over-allocated.
 */

typedef struct ArenaBlock {
    struct ArenaBlock *next; // Pointer to the next block
    size_t capacity;         // Total capacity of this block
    size_t used;             // Amount of memory used in this block
    char data[];             // Flexible array member for actual data
} ArenaBlock;

typedef struct Arena {
    ArenaBlock *blocks;      // Linked list of memory blocks
    size_t block_size;       // Size of each block (initial capacity)
} Arena;

Arena *arena_create(size_t initial_capacity);
void arena_destroy(Arena *arena);
void arena_reset(Arena *arena);
void *arena_alloc(Arena *arena, size_t size);
void *arena_calloc(Arena *arena, size_t size);

/* Debug / Metrics helpers */
size_t arena_bytes_used(const Arena *arena);
size_t arena_bytes_capacity(const Arena *arena);
size_t arena_block_count(const Arena *arena);
size_t arena_total_allocated(const Arena *arena);
