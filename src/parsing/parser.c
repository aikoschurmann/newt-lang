#include "parser.h"
#include <stdio.h>
#include "lexer.h"

Parser *parser_create(DynArray *tokens, char *filename, Arena *arena) {
    if (!arena) return NULL;

    Parser *p = arena_alloc(arena, sizeof(Parser));
    if (!p) return NULL;

    p->tokens = tokens;
    p->current = 0;
    p->end = tokens->count;
    p->arena = arena;
    p->filename = filename; /* assume already in arena */
   
    return p;
}

void parser_free(Parser *parser) {
    /* parser itself is owned by arena if created via parser_create */
    (void)parser;
}

Token *current_token(Parser *p) {
    if (!p) return NULL;
    if (p->current >= p->end) return NULL;
    return (Token*)dynarray_get(p->tokens, p->current);
}

Token *peek(Parser *p, size_t offset) {
    if (!p) return NULL;
    size_t index = p->current + offset;
    if (index >= p->end) return NULL;
    return (Token*)dynarray_get(p->tokens, index);
}

Token *parser_advance(Parser *p) {
    if (!p) return NULL;
    if (p->current >= p->end) return NULL;
    Token *tok = (Token*)dynarray_get(p->tokens, p->current);
    p->current++;
    return tok;
}

Token *consume(Parser *p, TokenType expected) {
    if (!p) return NULL;
    Token *tok = current_token(p);
    if (!tok || tok->type != expected) return NULL;
    p->current++;
    return tok;
}

int parser_match(Parser *p, TokenType expected) {
    if (!p) return 0;
    Token *tok = current_token(p);
    if (!tok || tok->type != expected) return 0;
    p->current++;
    return 1;
}

void create_parse_error(ParseError *err_out, Parser *p, const char *message, Token *token) {
    if (!err_out || !p || !message) return;
    err_out->p = p;
    err_out->message = (char*)arena_alloc(p->arena, strlen(message) + 1);
    if (err_out->message) {
        strcpy(err_out->message, message);
    }
    err_out->token = token;
}

AstNode *new_node_or_err(Parser *p, AstNodeType kind, ParseError *err, const char *oom_msg) {
    AstNode *n = ast_create_node(kind, p->arena, p->filename);
    if (!n) {
        if (err) create_parse_error(err, p, oom_msg, NULL);
    }
    return n;
}

DynArray *alloc_dynarray(Parser *p, ParseError *err, size_t elem_size, int initial_capacity, const char *oom_msg) {
    DynArray *arr = arena_alloc(p->arena, sizeof(DynArray));
    if (!arr) {
        if (err) create_parse_error(err, p, oom_msg, NULL);
        return NULL;
    }
    dynarray_init_in_arena(arr, p->arena, elem_size, initial_capacity);
    return arr;
}


// === ANSI colors for nicer output ===
#define RED     "\x1b[31m"
#define CYAN    "\x1b[36m"
#define BOLD    "\x1b[1m"
#define RESET   "\x1b[0m"
#define YELLOW  "\x1b[33m"

// Use shared source excerpt helpers
#include "file.h"

// === Main error printing ===
void print_parse_error(ParseError *error) {
    if (!error || !error->p || !error->message) return;

    const char *filename = error->p->filename ? error->p->filename : "<unknown>";
    Token *orig_tok = error->token;

    /* Print the main error header */
    fprintf(stderr, RED "error:" RESET " %s\n", error->message);

    if (!orig_tok) {
        /* No token associated: print a simple location line and return */
        fprintf(stderr, "   %s\n", filename);
        return;
    }

    /* Decide which token to display. Do NOT mutate the parser/token state:
       copy the token to a local and adjust the copy if use_prev_token is set. */
    Token display_tok = *orig_tok;

    if (error->use_prev_token) {
        Token *prev = peek(error->p, -1);
        if (prev) {
            display_tok = *prev;
            /* caret should point after the previous token, so offset column by its lexeme length */
            display_tok.span.start_col += (size_t)display_tok.slice.len;
        }
    }

    /* Print file:line:col using the (possibly adjusted) display token */
    fprintf(stderr, "   %s:%zu:%zu\n",
            filename,
            display_tok.span.start_line,
            display_tok.span.start_col);

    /* Print the source excerpt and caret underline. We pass the local copy so the
       excerpt function sees the adjusted column when use_prev_token was requested. */
    if (error->use_prev_token) {
        /* When pointing after a token, just use a single caret at the new start position */
        print_source_excerpt(filename, display_tok.span.start_line, display_tok.span.start_col);
    } else {
        /* Otherwise, underline the whole token if it's on a single line */
        if (display_tok.span.start_line == display_tok.span.end_line && 
            display_tok.span.end_col > display_tok.span.start_col) {
            print_source_excerpt_span(filename, 
                                    display_tok.span.start_line, 
                                    display_tok.span.start_col, 
                                    display_tok.span.end_col);
        } else {
             /* Fallback for multi-line tokens or zero-width */
            print_source_excerpt(filename, display_tok.span.start_line, display_tok.span.start_col);
        }
    }
}
