#pragma once
#include <stdint.h>  // for uint32_t
#include <stddef.h>  // for size_t
#include "utils.h"   // for Slice
#include "dense_arena_interner.h" // for InternResult



// ------------------------------
// Token type enum
// ------------------------------
typedef enum {
    // keywords
    TOK_FN,
    TOK_IF,
    TOK_ELSE,
    TOK_WHILE,
    TOK_FOR,
    TOK_RETURN,
    TOK_BREAK,
    TOK_CONTINUE,
    TOK_CONST,

    // types
    TOK_I32,
    TOK_I64,
    TOK_BOOL,
    TOK_F32,
    TOK_F64,
    TOK_STRING,
    TOK_CHAR,
    TOK_VOID,

    // operators
    TOK_PLUSPLUS,
    TOK_MINUSMINUS,
    TOK_PLUS_EQ,
    TOK_MINUS_EQ,
    TOK_STAR_EQ,
    TOK_SLASH_EQ,
    TOK_PERCENT_EQ,
    TOK_EQ_EQ,
    TOK_BANG_EQ,
    TOK_LT_EQ,
    TOK_GT_EQ,
    TOK_AND_AND,
    TOK_OR_OR,
    TOK_ARROW,
    TOK_ASSIGN,
    TOK_PLUS,
    TOK_MINUS,
    TOK_STAR,
    TOK_SLASH,
    TOK_PERCENT,
    TOK_BANG,
    TOK_AMP,
    TOK_LT,
    TOK_GT,

    // punctuation
    TOK_DOT,
    TOK_LPAREN,
    TOK_RPAREN,
    TOK_LBRACE,
    TOK_RBRACE,
    TOK_LBRACKET,
    TOK_RBRACKET,
    TOK_COMMA,
    TOK_SEMICOLON,
    TOK_COLON,
    TOK_PIPE,

    // literals
    TOK_FLOAT_LIT,
    TOK_INT_LIT,
    TOK_STRING_LIT,
    TOK_CHAR_LIT,     // 'a'
    TOK_TRUE,
    TOK_FALSE,

    // other
    TOK_IDENTIFIER,
    TOK_COMMENT,
    TOK_EOF,
    TOK_STRUCT,
    TOK_AS,
    TOK_UNKNOWN

} TokenType;


// ------------------------------
// Token struct
// ------------------------------
typedef struct {
    TokenType type;   // what kind of token
    Slice slice;      // points into source buffer
    Span span;        // position in source for error reporting
    InternResult *record;
} Token;
