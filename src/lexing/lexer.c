// lexer.c
// Lexer implementation using DynArray for token storage and the DenseArenaInterner
//
// Optimized: pointer-based scanning (cur/end), single-peek usage in branches,
// and usage of intern_peek for keywords (no accidental insertion).

#include "lexer.h"
#include "dense_arena_interner.h"
#include "token.h"
#include "dynamic_array.h"
#include "arena.h"


#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <stdint.h>
#include <stdio.h>  // for debug prints

/* Initial token array capacity (used by dynarray as growth hint) */
#define INITIAL_TOKEN_CAPACITY 256

/* Keyword table for initial population (used only at startup) */
static const struct {
    const char *word;
    TokenType type;
} KEYWORDS[] = {
    {"fn", TOK_FN},
    {"if", TOK_IF},
    {"else", TOK_ELSE},
    {"while", TOK_WHILE},
    {"for", TOK_FOR},
    {"return", TOK_RETURN},
    {"break", TOK_BREAK},
    {"continue", TOK_CONTINUE},
    {"const", TOK_CONST},
    {"pub", TOK_PUB},
    {"import", TOK_IMPORT},
    {"struct", TOK_STRUCT},
    {"as", TOK_AS},
    {"i32", TOK_I32},
    {"i64", TOK_I64},
    {"bool", TOK_BOOL},
    {"f32", TOK_F32},
    {"f64", TOK_F64},
    {"str", TOK_STRING},
    {"char", TOK_CHAR},
    {"void", TOK_VOID},
    {"true", TOK_TRUE},
    {"false", TOK_FALSE},
    {NULL, TOK_UNKNOWN}  /* sentinel */
};


static inline char lexer_peek(const Lexer *lexer) {
    return lexer->cur < lexer->end ? *lexer->cur : '\0';
}

static inline char lexer_peek_next(const Lexer *lexer) {
    return (lexer->cur + 1 < lexer->end) ? *(lexer->cur + 1) : '\0';
}

/* Advance returns the char consumed (like before) and updates pos/cur/line/col */
static inline char lexer_advance(Lexer *lexer) {
    if (lexer->cur >= lexer->end) return '\0';
    char c = *lexer->cur++;
    lexer->pos++;
    if (c == '\n') {
        lexer->line++;
        lexer->col = 1;
    } else {
        lexer->col++;
    }
    return c;
}

/* Portable classification: treat '_' as alpha, use ctype for letters/digits */
static inline bool is_alpha(char c) {
    return c == '_' || isalpha((unsigned char)c);
}
static inline bool is_digit(char c) {
    return isdigit((unsigned char)c);
}
static inline bool is_alnum(char c) {
    return is_alpha(c) || is_digit(c);
}

/* Create slice from current lexer position using pointer arithmetic */
static Slice lexer_make_slice_from_ptrs(const char *start_ptr, const char *end_ptr) {
    return (Slice) { .ptr = (char*)start_ptr, .len = (size_t)(end_ptr - start_ptr) };
}

/* Forward declarations */
static bool lexer_add_token(Lexer *lexer, const Token *tok);

/* Skip whitespace and comments (pointer-based) */
static void lexer_skip_whitespace(Lexer *lexer) {
    while (lexer->cur < lexer->end) {
        char c = *lexer->cur;

        if (c == '\n') {
            lexer_advance(lexer);
            continue;
        }

        /* line comment // */
        if (c == '/' && (lexer->cur + 1) < lexer->end && *(lexer->cur + 1) == '/') {
            /* consume '//' */
            lexer_advance(lexer);
            lexer_advance(lexer);
            while (lexer->cur < lexer->end && *lexer->cur != '\n') lexer_advance(lexer);
            continue;
        }

        /* block comment ... */
        if (c == '/' && (lexer->cur + 1) < lexer->end && *(lexer->cur + 1) == '*') {
            lexer_advance(lexer); /* '/' */
            lexer_advance(lexer); /* '*' */
            while (lexer->cur < lexer->end) {
                if (*lexer->cur == '*' && (lexer->cur + 1) < lexer->end && *(lexer->cur + 1) == '/') {
                    lexer_advance(lexer); /* '*' */
                    lexer_advance(lexer); /* '/' */
                    break;
                }
                lexer_advance(lexer);
            }
            continue;
        }

        if (isspace((unsigned char)c)) {
            lexer_advance(lexer);
            continue;
        }

        break;
    }
}

/* Identifier or keyword (uses intern_peek to avoid insertion on keyword check) */
static void *lexer_lex_identifier(Lexer *lexer, const char *start_ptr, const char *end_ptr, TokenType *out_type) {
    Slice slice = lexer_make_slice_from_ptrs(start_ptr, end_ptr);

    /* Lookup keyword WITHOUT inserting. intern_peek must be available. */
    InternResult *kwres = intern_peek(lexer->keywords, &slice);
    if (kwres) {
        *out_type = (TokenType)(uintptr_t)kwres->entry->meta;
        return kwres;
    }

    /* Not a keyword: insert/return identifier record */
    InternResult *idres = intern(lexer->identifiers, &slice, NULL);
    if (!idres) {
        *out_type = TOK_UNKNOWN;
        return NULL;
    }
    *out_type = TOK_IDENTIFIER;
    return idres;
}

/* Number literal (pointer-based scan). Handles basic integer/float forms. */
static TokenType lexer_lex_number(Lexer *lexer, const char *start_ptr, const char **out_end_ptr) {
    const char *p = start_ptr;

    if (*p == '0') {
        p++;
        if (p < lexer->end && (*p == 'x' || *p == 'X')) {
            p++;
            while (p < lexer->end && (isxdigit((unsigned char)*p) || *p == '_')) p++;
            *out_end_ptr = p;
            return TOK_INT_LIT;
        } else if (p < lexer->end && (*p == 'b' || *p == 'B')) {
            p++;
            while (p < lexer->end && (*p == '0' || *p == '1' || *p == '_')) p++;
            *out_end_ptr = p;
            return TOK_INT_LIT;
        }
        // Fallthrough for decimal starting with 0
    }

    while (p < lexer->end && (isdigit((unsigned char)*p) || *p == '_')) p++;

    /* fractional part */
    if (p < lexer->end && *p == '.') {
        const char *next = p + 1;
        if (next < lexer->end && isdigit((unsigned char)*next)) {
             p++; /* consume '.' */
             while (p < lexer->end && (isdigit((unsigned char)*p) || *p == '_')) p++;
        } else {
             // 10. (might be a member access on an integer literal if supported, or just 10.0)
             // For now, if '.' is followed by non-digit, we stop.
             *out_end_ptr = p;
             return TOK_INT_LIT;
        }
        
        /* exponent part */
        if (p < lexer->end && (*p == 'e' || *p == 'E')) {
            p++;
            if (p < lexer->end && (*p == '+' || *p == '-')) p++;
            while (p < lexer->end && (isdigit((unsigned char)*p) || *p == '_')) p++;
        }
        *out_end_ptr = p;
        return TOK_FLOAT_LIT;
    }

    *out_end_ptr = p;
    return TOK_INT_LIT;
}

/* String literal (handles escapes). Returns end pointer after closing quote. */
static TokenType lexer_lex_string(const char **curptr, const char *endptr) {
    /* curptr points at the opening quote ('"') */
    const char *p = *curptr;
    p++; /* skip opening quote */

    while (p < endptr && *p != '"') {
        if (*p == '\\') {
            p++;
            if (p < endptr) p++; /* skip escaped char */
        } else {
            p++;
        }
    }

    if (p >= endptr) {
        *curptr = p;
        return TOK_UNKNOWN; /* unterminated */
    }

    p++; /* skip closing quote */
    *curptr = p;
    return TOK_STRING_LIT;
}

/* Char literal */
static TokenType lexer_lex_char(const char **curptr, const char *endptr, uint32_t *out_codepoint) {
    const char *p = *curptr;
    p++; /* skip opening '\'' */

    if (p >= endptr) {
        *curptr = p;
        return TOK_UNKNOWN;
    }

    uint32_t cp = 0;
    if (*p == '\\') {
        p++;
        if (p >= endptr) { *curptr = p; return TOK_UNKNOWN; }
        switch (*p) {
            case 'n': cp = '\n'; break;
            case 't': cp = '\t'; break;
            case 'r': cp = '\r'; break;
            case '\\': cp = '\\'; break;
            case '\'': cp = '\''; break;
            case '0': cp = '\0'; break;
            default:
                /* simple fallback: take the escaped char literally */
                cp = (unsigned char)*p;
                break;
        }
        p++;
    } else {
        cp = (unsigned char)*p;
        p++;
    }

    if (p >= endptr || *(p) != '\'') {
        *curptr = p;
        return TOK_UNKNOWN;
    }

    p++; /* skip closing '\'' */
    *curptr = p;
    *out_codepoint = cp;
    return TOK_CHAR_LIT;
}

/* Unescape string content (raw slice excludes surrounding quotes) into arena and return slice
   pointing into arena. The arena allocation size is conservatively raw.len (since unescaped <= raw.len)
   plus one for NUL. */
static Slice unescape_string_into_arena(const Slice raw, Arena *arena) {
    if (raw.len == 0) return (Slice){ .ptr = NULL, .len = 0 };
    char *out = arena_alloc(arena, raw.len + 1);
    if (!out) return (Slice){ .ptr = NULL, .len = 0 };
    char *w = out;
    const char *r = raw.ptr;
    const char *end = raw.ptr + raw.len;

    while (r < end) {
        if (*r == '\\') {
            r++;
            if (r >= end) break;
            switch (*r) {
                case 'n': *w++ = '\n'; break;
                case 't': *w++ = '\t'; break;
                case 'r': *w++ = '\r'; break;
                case '\\': *w++ = '\\'; break;
                case '"': *w++ = '"'; break;
                case '\'': *w++ = '\''; break;
                case '0': *w++ = '\0'; break;
                default: *w++ = *r; break;
            }
            r++;
        } else {
            *w++ = *r++;
        }
    }

    size_t n = (size_t)(w - out);
    out[n] = '\0';
    return (Slice){ .ptr = out, .len = n };
}

/* Create and initialize a new Lexer */
void lexer_populate_default_keywords(DenseArenaInterner *keywords) {
    for (size_t i = 0; KEYWORDS[i].word; i++) {
        Slice slice = {
            .ptr = (char*)KEYWORDS[i].word,
            .len = strlen(KEYWORDS[i].word)
        };
        intern(keywords, &slice, (void*)(uintptr_t)KEYWORDS[i].type);
    }
}

Lexer* lexer_create(const char *source, size_t source_len, Arena *arena) {
    if (!source || !arena) return NULL;

    DenseArenaInterner *keywords = intern_table_create(hashmap_create(32), arena, string_copy_func, slice_hash, slice_cmp);
    DenseArenaInterner *identifiers = intern_table_create(hashmap_create(128), arena, string_copy_func, slice_hash, slice_cmp);
    DenseArenaInterner *strings = intern_table_create(hashmap_create(64), arena, string_copy_func, slice_hash, slice_cmp);
    
    if (!keywords || !identifiers || !strings) return NULL;

    /* Pre-intern keywords with TokenType as metadata */
    lexer_populate_default_keywords(keywords);

    return lexer_create_ex(source, source_len, arena, keywords, identifiers, strings);
}

Lexer* lexer_create_ex(const char *source, size_t source_len, Arena *arena,
                      DenseArenaInterner *keywords,
                      DenseArenaInterner *identifiers,
                      DenseArenaInterner *strings) {
    if (!source || !arena) return NULL;

    Lexer *lexer = arena_alloc(arena, sizeof(Lexer));
    if (!lexer) return NULL;

    lexer->source = source;
    lexer->source_len = source_len;
    lexer->pos = 0;
    lexer->line = 1;
    lexer->col = 1;
    lexer->arena = arena;

    /* pointer-based scanning state */
    lexer->cur = source;
    lexer->end = source + source_len;

    lexer->keywords = keywords;
    lexer->identifiers = identifiers;
    lexer->strings = strings;

    lexer->tokens = arena_alloc(arena, sizeof(DynArray));
    if (!lexer->tokens) return NULL;
    // Initialize the DynArray for tokens using arena-backed storage
    dynarray_init_in_arena(lexer->tokens, arena, sizeof(Token), INITIAL_TOKEN_CAPACITY);

    return lexer;
}

/* Destroy lexer */
void lexer_destroy(Lexer *lexer) {
    if (!lexer) return;
    // Note: We don't destroy interners here as they might be shared or arena-owned.
    // They will be cleaned up when the arena is destroyed.
    if (lexer->tokens) {
        dynarray_free(lexer->tokens);
        lexer->tokens = NULL;
    }
}

/* Check EOF */
bool lexer_at_end(const Lexer *lexer) {
    return lexer->cur >= lexer->end;
}

/* Produce next token (pointer-based) */
Token lexer_next_token(Lexer *lexer) {
    lexer_skip_whitespace(lexer);

    if (lexer_at_end(lexer)) {
        return (Token){
            .type = TOK_EOF,
            .slice = {.ptr = (char*)lexer->cur, .len = 0},
            .span = {lexer->line, lexer->col, lexer->line, lexer->col},
            .record = NULL
        };
    }

    const char *start_ptr = lexer->cur;
    size_t start_line = lexer->line;
    size_t start_col = lexer->col;

    char c = lexer_advance(lexer);

    TokenType token_type = TOK_UNKNOWN;
    void *rec = NULL;
    Slice slice = { NULL, 0 };

    if (is_alpha(c)) {
        /* identifier or keyword: consume rest using pointer p */
        const char *p = lexer->cur;
        while (p < lexer->end && (is_alpha(*p) || is_digit(*p))) p++;
        /* update lexer->cur/pos/line/col to p by advancing remaining chars quickly */
        while (lexer->cur < p) lexer_advance(lexer);

        slice = lexer_make_slice_from_ptrs(start_ptr, lexer->cur);

        rec = lexer_lex_identifier(lexer, start_ptr, lexer->cur, &token_type);

    } else if (is_digit(c)) {
        /* number — use pointer-based scan */
        const char *endptr = NULL;
        token_type = lexer_lex_number(lexer, start_ptr, &endptr);
        /* advance lexer->cur to endptr */
        while (lexer->cur < endptr) lexer_advance(lexer);
        slice = lexer_make_slice_from_ptrs(start_ptr, lexer->cur);

    } else if (c == '"') {
        /* lex string using a helper that advances a local pointer, then sync */
        const char *tmpcur = lexer->cur - 1; /* points to opening quote */
        token_type = lexer_lex_string(&tmpcur, lexer->end);
        /* advance lexer->cur to tmpcur */
        while (lexer->cur < tmpcur) lexer_advance(lexer);
        /* raw slice includes quotes; create content slice without quotes for unescaping */
        const char *raw_start = start_ptr + 1; /* after opening quote */
        const char *raw_end = lexer->cur - 1;   /* before closing quote */
        slice = lexer_make_slice_from_ptrs(start_ptr, lexer->cur);

        if (token_type == TOK_STRING_LIT) {
            Slice raw_content = lexer_make_slice_from_ptrs(raw_start, raw_end);
            Slice unescaped = unescape_string_into_arena(raw_content, lexer->arena);
            if (unescaped.ptr && unescaped.len > 0) {
                /* Intern the unescaped string into the dedicated strings interner */
                InternResult *ires = intern(lexer->strings, &unescaped, NULL);
                rec = ires;
            } else if (unescaped.ptr) {
                /* empty string: still intern an empty slice */
                InternResult *ires = intern(lexer->strings, &unescaped, NULL);
                rec = ires;
            } else {
                /* allocation failed or other error: leave rec NULL */
                rec = NULL;
            }
        }

    } else if (c == '\'') {
        const char *tmpcur = lexer->cur - 1;
        uint32_t cp = 0;
        token_type = lexer_lex_char(&tmpcur, lexer->end, &cp);
        while (lexer->cur < tmpcur) lexer_advance(lexer);
        slice = lexer_make_slice_from_ptrs(start_ptr, lexer->cur);
        if (token_type == TOK_CHAR_LIT) {
            /* store codepoint in record as integer-cast pointer */
            rec = (void*)(uintptr_t)cp;
        }

    } else {
        /* Operators & punctuation. read next char once where needed */
        char next = lexer_peek(lexer);
        switch (c) {
            case '+':
                if (next == '+') { lexer_advance(lexer); token_type = TOK_PLUSPLUS; }
                else if (next == '=') { lexer_advance(lexer); token_type = TOK_PLUS_EQ; }
                else token_type = TOK_PLUS;
                break;
            case '-':
                if (next == '-') { lexer_advance(lexer); token_type = TOK_MINUSMINUS; }
                else if (next == '=') { lexer_advance(lexer); token_type = TOK_MINUS_EQ; }
                else if (next == '>') { lexer_advance(lexer); token_type = TOK_ARROW; }
                else token_type = TOK_MINUS;
                break;
            case '=':
                if (next == '=') { lexer_advance(lexer); token_type = TOK_EQ_EQ; }
                else token_type = TOK_ASSIGN;
                break;
            case '!':
                if (next == '=') { lexer_advance(lexer); token_type = TOK_BANG_EQ; }
                else token_type = TOK_BANG;
                break;
            case '<':
                if (next == '=') { lexer_advance(lexer); token_type = TOK_LT_EQ; }
                else token_type = TOK_LT;
                break;
            case '>':
                if (next == '=') { lexer_advance(lexer); token_type = TOK_GT_EQ; }
                else token_type = TOK_GT;
                break;
            case '&':
                if (next == '&') { lexer_advance(lexer); token_type = TOK_AND_AND; }
                else token_type = TOK_AMP;
                break;
            case '|':
                if (next == '|') { lexer_advance(lexer); token_type = TOK_OR_OR; }
                else token_type = TOK_PIPE;
                break;
            case '(' : token_type = TOK_LPAREN; break;
            case ')' : token_type = TOK_RPAREN; break;
            case '{' : token_type = TOK_LBRACE; break;
            case '}' : token_type = TOK_RBRACE; break;
            case '[' : token_type = TOK_LBRACKET; break;
            case ']' : token_type = TOK_RBRACKET; break;
            case ',' : token_type = TOK_COMMA; break;
            case ';': token_type = TOK_SEMICOLON; break;
            case ':': token_type = TOK_COLON; break;
            case '@': token_type = TOK_AT; break;
            case '*':
                if (next == '=') { lexer_advance(lexer); token_type = TOK_STAR_EQ; }
                else token_type = TOK_STAR;
                break;
            case '/':
                if (next == '=') { lexer_advance(lexer); token_type = TOK_SLASH_EQ; }
                else token_type = TOK_SLASH;
                break;
            case '%':
                if (next == '=') { lexer_advance(lexer); token_type = TOK_PERCENT_EQ; }
                else token_type = TOK_PERCENT;
                break;
            case '.': token_type = TOK_DOT; break;
            default:  token_type = TOK_UNKNOWN; break;
        }

        slice = lexer_make_slice_from_ptrs(start_ptr, lexer->cur);
    }

    Span span = { start_line, start_col, lexer->line, lexer->col };
    return (Token){ .type = token_type, .slice = slice, .span = span, .record = rec };
}

/* Add token to lexer's token array (direct push; no extra intern lookups) */
static bool lexer_add_token(Lexer *lexer, const Token *tok) {
    if (!lexer || !tok) return false;
    return dynarray_push_value(lexer->tokens, tok) == 0;
}

/* Lex all tokens into the lexer's token array */
bool lexer_lex_all(Lexer *lexer) {
    if (!lexer) return false;

    for (;;) {
        Token token = lexer_next_token(lexer);

        if (!lexer_add_token(lexer, &token)) {
            return false;
        }

        if (token.type == TOK_EOF) break;
    }

    return true;
}


/* Return pointer to token array and count */
Token* lexer_get_tokens(Lexer *lexer, size_t *count) {
    if (!lexer || !count) return NULL;
    *count = lexer->tokens->count;
    return (Token*)lexer->tokens->data;
}

/* Human-readable token type (unchanged) */
const char* token_type_to_string(TokenType type) {
    switch (type) {
        case TOK_FN: return "FN";
        case TOK_IF: return "IF";
        case TOK_ELSE: return "ELSE";
        case TOK_WHILE: return "WHILE";
        case TOK_FOR: return "FOR";
        case TOK_RETURN: return "RETURN";
        case TOK_BREAK: return "BREAK";
        case TOK_CONTINUE: return "CONTINUE";
        case TOK_CONST: return "CONST";
        case TOK_PUB: return "PUB";
        case TOK_IMPORT: return "IMPORT";
        case TOK_STRUCT: return "STRUCT";
        case TOK_I32: return "I32";
        case TOK_I64: return "I64";
        case TOK_BOOL: return "BOOL";
        case TOK_F32: return "F32";
        case TOK_F64: return "F64";
        case TOK_CHAR: return "CHAR";
        case TOK_TRUE: return "TRUE";
        case TOK_FALSE: return "FALSE";
        case TOK_IDENTIFIER: return "IDENTIFIER";
        case TOK_INT_LIT: return "INT_LIT";
        case TOK_FLOAT_LIT: return "FLOAT_LIT";
        case TOK_STRING_LIT: return "STRING_LIT";
        case TOK_CHAR_LIT: return "CHAR_LIT";
        case TOK_EQ_EQ: return "EQUALSEQUALS";
        case TOK_BANG_EQ: return "BANGEQUALS";
        case TOK_LT_EQ: return "LESSEREQUALS";
        case TOK_GT_EQ: return "GREATEREQUALS";
        case TOK_PLUSPLUS: return "PLUSPLUS";
        case TOK_MINUSMINUS: return "MINUSMINUS";
        case TOK_PLUS_EQ: return "PLUSEQUALS";
        case TOK_MINUS_EQ: return "MINUSEQUALS";
        case TOK_STAR_EQ: return "STAREQUALS";
        case TOK_SLASH_EQ: return "SLAHSEQUALS";
        case TOK_PERCENT_EQ: return "PERCEQUALS";
        case TOK_AND_AND: return "ANDAND";
        case TOK_OR_OR: return "OROR";
        case TOK_ARROW: return "ARROW";
        case TOK_ASSIGN: return "ASSIGN";
        case TOK_PLUS: return "PLUS";
        case TOK_MINUS: return "MINUS";
        case TOK_STAR: return "STAR";
        case TOK_SLASH: return "SLASH";
        case TOK_PERCENT: return "PERCENT";
        case TOK_BANG: return "BANG";
        case TOK_AMP: return "AMP";
        case TOK_LT: return "LESS";
        case TOK_GT: return "GREATER";
        case TOK_DOT: return "DOT";
        case TOK_LPAREN: return "LPAREN";
        case TOK_RPAREN: return "RPAREN";
        case TOK_LBRACE: return "LBRACE";
        case TOK_RBRACE: return "RBRACE";
        case TOK_LBRACKET: return "LBRACKET";
        case TOK_RBRACKET: return "RBRACKET";
        case TOK_COMMA: return "COMMA";
        case TOK_SEMICOLON: return "SEMICOLON";
        case TOK_COLON: return "COLON";
        case TOK_EOF: return "EOF";
        case TOK_AS: return "AS";
        case TOK_VOID: return "VOID";
        default: return "UNKNOWN";
    }
}

void lexer_reset(Lexer *lexer) {
    if (!lexer) return;

    lexer->pos = 0;
    lexer->line = 1;
    lexer->col = 1;

    /* reset pointer-based scanning state */
    lexer->cur = lexer->source;
    lexer->end = lexer->source + lexer->source_len;

    /* clear collected tokens but keep allocated buffer for reuse */
    if (lexer->tokens) {
        lexer->tokens->count = 0;
    }

    /* Note: we intentionally do NOT clear interners/arena here so that
       keywords and previously interned identifiers remain available. */
}


/* Pretty token printing (table-like) */
void print_token(const Token *tok) {
    const char *type_str = token_type_to_string(tok->type);
    printf("│ %3zu:%-3zu │ %-13s │ ",
           tok->span.start_line,
           tok->span.start_col,
           type_str);
    if (tok->slice.ptr && tok->slice.len > 0) {
        printf("'%.*s'", (int)tok->slice.len, tok->slice.ptr);
    } else {
        printf("(no-lexeme)");
    }

    /* If token.record holds a codepoint for char literal, print it */
    if (tok->type == TOK_CHAR_LIT && tok->record) {
        uint32_t cp = (uint32_t)(uintptr_t)tok->record;
        printf("  (char: U+%04X)", cp);
    }

    printf("\n");
}