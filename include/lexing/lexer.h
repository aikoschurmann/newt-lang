#ifndef LEXER_H
#define LEXER_H

#include <stddef.h>
#include <stdbool.h>

#include "arena.h"
#include "dense_arena_interner.h"
#include "dynamic_array.h"
#include "token.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Lexer state exposed here so callers can reuse/reset the lexer if desired. */
typedef struct Lexer {
    /* Source buffer (not owned) */
    const char *source;
    size_t source_len;

    /* byte offset into source (0..source_len) */
    size_t pos;

    /* human-facing position for errors (1-based) */
    size_t line;
    size_t col;

    /* Arena used for interner allocations and lexer-local allocations */
    Arena *arena;

    /* Pointer-based scanning state (optimized path) */
    const char *cur;   /* pointer to source + pos */
    const char *end;   /* pointer to source + source_len */

    /* Interners: keywords (preseeded), identifiers, and string literals */
    DenseArenaInterner *keywords;
    DenseArenaInterner *identifiers;
    DenseArenaInterner *strings;

    /* Tokens collected during lexing (DynArray of Token) */
    DynArray *tokens;
} Lexer;

/* Create a lexer that will operate on `source` (not copied). `arena` is used
 * for lexer allocations and must remain valid for the lifetime of the lexer.
 * Returns NULL on allocation failure. */
Lexer* lexer_create(const char *source, size_t source_len, Arena *arena);

/* Create a lexer with existing interners for module systems. */
Lexer* lexer_create_ex(const char *source, size_t source_len, Arena *arena,
                      DenseArenaInterner *keywords,
                      DenseArenaInterner *identifiers,
                      DenseArenaInterner *strings);

/* Destroy lexer and free any heap allocations owned by the lexer.
 * Note: arena_destroy must be called separately if caller created the arena. */
void lexer_destroy(Lexer *lexer);

/* Return true if the lexer has reached EOF. */
bool lexer_at_end(const Lexer *lexer);

/* Produce a single token from the lexer (advances internal position). */
Token lexer_next_token(Lexer *lexer);

/* Lex the entire source and append tokens into lexer->tokens.
 * Returns true on success, false on allocation/error. */
bool lexer_lex_all(Lexer *lexer);

/* Return pointer to contiguous token array and set count. The returned pointer
 * is owned by the lexer; do not free it. */
Token* lexer_get_tokens(Lexer *lexer, size_t *count);

/* Human-readable token type helper (convenience). */
const char* token_type_to_string(TokenType type);

void lexer_reset(Lexer *lexer);

void print_token(const Token *tok);

#ifdef __cplusplus
}
#endif

#endif /* LEXER_H */
