#include "../harness/test_harness.h"
#include "../helpers/compiler_helpers.h"

#define LEX_VALID(name, src) \
    TEST_CASE_PRIO("Lexer/Valid/" name, 10) { ASSERT(test_is_lex_valid(src)); return 1; }
#include "lexer_cases.inc"
#undef LEX_VALID

TEST_CASE_PRIO("Lexer: Long Identifier Stress", 10) {
    Arena *arena = arena_create(1024 * 1024);
    int len = 2000;
    char *src = arena_alloc(arena, len + 10);
    memset(src, 'a', len);
    src[len] = ';';
    src[len+1] = 0;
    
    ASSERT(test_is_lex_valid(src));
    arena_destroy(arena);
    return 1;
}
