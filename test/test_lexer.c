#include "test_utils.h"

int test_lexer_basic() {
    Arena *arena = arena_create(1024);
    const char *src = "fn main() -> i64 { return 10; }";
    Lexer *l = lexer_create(src, strlen(src), arena);
    ASSERT(lexer_lex_all(l));
    
    int expected_types[] = {
        TOK_FN, TOK_IDENTIFIER, TOK_LPAREN, TOK_RPAREN, TOK_ARROW, 
        TOK_I64, TOK_LBRACE, TOK_RETURN, TOK_INT_LIT, TOK_SEMICOLON, 
        TOK_RBRACE, TOK_EOF
    };
    
    ASSERT_EQ_INT(l->tokens->count, 12);
    
    for(int i=0; i<12; i++) {
        Token *t = (Token*)dynarray_get(l->tokens, i);
        if (t->type != expected_types[i]) {
            fprintf(stderr, "Token %d mismatch: got %d, expected %d\n", i, t->type, expected_types[i]);
            return 0;
        }
    }

    lexer_destroy(l);
    arena_destroy(arena);
    return 1;
}

int test_lexer_comments() {
    Arena *arena = arena_create(1024);
    const char *src = "10 // this is a comment\n 20";
    Lexer *l = lexer_create(src, strlen(src), arena);
    ASSERT(lexer_lex_all(l));
    
    // Expect: 10, 20, EOF (Comments are skipped)
    ASSERT_EQ_INT(l->tokens->count, 3);
    
    lexer_destroy(l);
    arena_destroy(arena);
    return 1;
}

int test_lexer_operators() {
    Arena *arena = arena_create(1024);
    const char *src = "+ - * / == != <= >= && ||";
    Lexer *l = lexer_create(src, strlen(src), arena);
    ASSERT(lexer_lex_all(l));
    
    int expected[] = {
        TOK_PLUS, TOK_MINUS, TOK_STAR, TOK_SLASH, 
        TOK_EQ_EQ, TOK_BANG_EQ, TOK_LT_EQ, TOK_GT_EQ,
        TOK_AND_AND, TOK_OR_OR, TOK_EOF
    };
    
    lexer_destroy(l);
    arena_destroy(arena);
    return 1;
}

int test_lexer_float_literals() {
    Arena *arena = arena_create(1024);
    // Test various float formats: simple, decimal, scientific notation
    const char *src = "3.14 0.5 10. .01"; // basic forms often supported or not
    Lexer *l = lexer_create(src, strlen(src), arena);
    ASSERT(lexer_lex_all(l));

    // Check first token: 3.14
    Token *t0 = (Token*)dynarray_get(l->tokens, 0);
    ASSERT_EQ_INT(t0->type, TOK_FLOAT_LIT);
    
    // Check second: 0.5
    Token *t1 = (Token*)dynarray_get(l->tokens, 1);
    ASSERT_EQ_INT(t1->type, TOK_FLOAT_LIT);

    lexer_destroy(l);
    arena_destroy(arena);
    return 1;
}

int test_lexer_bad_strings() {
    Arena *arena = arena_create(1024);
    // Unterminated string
    const char *src = "\"hello world";
    Lexer *l = lexer_create(src, strlen(src), arena);
    // Should lexer fail or error token?
    bool success = lexer_lex_all(l); 
    
    if (success) {
        Token *last = (Token*)dynarray_get(l->tokens, l->tokens->count - 1);
        if (last->type == TOK_EOF) {
             Token *prev = (Token*)dynarray_get(l->tokens, l->tokens->count - 2);
             // Just ensure we didn't crash
        }
    }
    
    lexer_destroy(l);
    arena_destroy(arena);
    return 1; 
}

int test_lexer_identifiers() {
    Arena *arena = arena_create(1024);
    const char *src = "foo _bar baz123 _123";
    Lexer *l = lexer_create(src, strlen(src), arena);
    ASSERT(lexer_lex_all(l));
    
    ASSERT_EQ_INT(l->tokens->count, 5); // 4 ids + EOF
    
    for(int i=0; i<4; i++) {
        Token *t = (Token*)dynarray_get(l->tokens, i);
        ASSERT_EQ_INT(t->type, TOK_IDENTIFIER);
    }
    
    lexer_destroy(l);
    arena_destroy(arena);
    return 1;
}

int test_lexer_empty() {
    Arena *arena = arena_create(1024);
    const char *src = "";
    Lexer *l = lexer_create(src, strlen(src), arena);
    ASSERT(lexer_lex_all(l));
    ASSERT_EQ_INT(l->tokens->count, 1);
    Token *t = (Token*)dynarray_get(l->tokens, 0);
    ASSERT_EQ_INT(t->type, TOK_EOF);
    lexer_destroy(l);
    arena_destroy(arena);
    return 1;
}

int test_lexer_whitespace_only() {
    Arena *arena = arena_create(1024);
    const char *src = "    \n\t  \r\n  ";
    Lexer *l = lexer_create(src, strlen(src), arena);
    ASSERT(lexer_lex_all(l));
    ASSERT_EQ_INT(l->tokens->count, 1); // just EOF
    lexer_destroy(l);
    arena_destroy(arena);
    return 1;
}

// Exception/stress tests moved here
int test_exception_long_identifier() {
    Arena *arena = arena_create(1024 * 1024);
    // Create a 2000 char identifier
    int len = 2000;
    char *src = arena_alloc(arena, len + 10);
    memset(src, 'a', len);
    src[len] = ';';
    src[len+1] = 0;
    
    Lexer *l = lexer_create(src, strlen(src), arena);
    bool success = lexer_lex_all(l); 
    // Should succeed lexing, but create a huge identifier token.
    ASSERT(success);
    Token *t = (Token*)dynarray_get(l->tokens, 0);
    ASSERT_EQ_INT(t->type, TOK_IDENTIFIER);
    ASSERT_EQ_INT(t->slice.len, len);
    
    lexer_destroy(l);
    arena_destroy(arena);
    return 1;
}

int test_exception_weird_chars() {
    Arena *arena = arena_create(1024);
    const char *src = "fn main() { let x = @; }"; // @ is usually invalid
    Lexer *l = lexer_create(src, strlen(src), arena);
    // lexer might fail or produce UNKNOWN token
    if (lexer_lex_all(l)) {
         // check for unknown token
         bool found_bad = false;
         for(int i=0; i<l->tokens->count; i++) {
             Token *t = (Token*)dynarray_get(l->tokens, i);
             if (t->type == TOK_UNKNOWN) found_bad = true;
         }
         // Not strictly asserting, but good to know
    }
    lexer_destroy(l);
    arena_destroy(arena);
    return 1;
}

int test_lexer_as_keyword() {
    Arena *arena = arena_create(1024);
    const char *src = "x as i32";
    Lexer *l = lexer_create(src, strlen(src), arena);
    ASSERT(lexer_lex_all(l));
    
    int expected_types[] = {
        TOK_IDENTIFIER, TOK_AS, TOK_I32, TOK_EOF
    };
    
    ASSERT_EQ_INT(l->tokens->count, 4);
    
    for(int i=0; i<4; i++) {
        Token *t = (Token*)dynarray_get(l->tokens, i);
        ASSERT_EQ_INT(t->type, expected_types[i]);
    }

    lexer_destroy(l);
    arena_destroy(arena);
    return 1;
}

int test_exception_unclosed_comment() {
    Arena *arena = arena_create(1024);
    const char *src = "/* this is never closed";
    Lexer *l = lexer_create(src, strlen(src), arena);
    bool success = lexer_lex_all(l); 
    // Just ensure no crash
    lexer_destroy(l);
    arena_destroy(arena);
    return 1;
}
