#include "parser.h"
#include <stdio.h>
#include "lexer.h"
#include "colors.h"
#include "file.h"

Parser *parser_create(DynArray *tokens, char *filename, Arena *arena) {
    if (!arena) return NULL;
    Parser *p = arena_alloc(arena, sizeof(Parser));
    if (!p) return NULL;
    p->tokens = tokens;
    p->current = 0;
    p->end = tokens->count;
    p->arena = arena;
    p->filename = filename;
    return p;
}

void parser_free(Parser *parser) { (void)parser; }

Token *current_token(Parser *p) {
    if (!p || p->current >= p->end) return NULL;
    return (Token*)dynarray_get(p->tokens, p->current);
}

Token *peek(Parser *p, size_t offset) {
    if (!p) return NULL;
    size_t index = p->current + offset;
    if (index >= p->end) return NULL;
    return (Token*)dynarray_get(p->tokens, index);
}

Token *parser_advance(Parser *p) {
    if (!p || p->current >= p->end) return NULL;
    return (Token*)dynarray_get(p->tokens, p->current++);
}

Token *consume(Parser *p, TokenKind expected) {
    Token *tok = current_token(p);
    if (!tok || tok->type != expected) return NULL;
    p->current++;
    return tok;
}

int parser_match(Parser *p, TokenKind expected) {
    Token *tok = current_token(p);
    if (!tok || tok->type != expected) return 0;
    p->current++;
    return 1;
}

void create_parse_error(ParseError *err_out, Parser *p, const char *message, Token *token) {
    if (!err_out || !p || !message) return;
    err_out->p = p;
    err_out->message = (char*)arena_alloc(p->arena, strlen(message) + 1);
    if (err_out->message) strcpy(err_out->message, message);
    err_out->token = token;
}

AstNode *new_node_or_err(Parser *p, AstNodeType kind, ParseError *err, const char *oom_msg) {
    AstNode *n = ast_create_node(kind, p->arena, p->filename);
    if (!n && err) create_parse_error(err, p, oom_msg, NULL);
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

static void print_error_header(const char *msg) {
    fprintf(stderr, RED "error:" RESET " %s\n", msg);
}

static void print_error_location(const char *filename, Token *tok) {
    fprintf(stderr, "   %s:%zu:%zu\n", filename, tok->span.start_line, tok->span.start_col);
}

static void print_error_source(const char *filename, Token *tok, bool use_prev) {
    if (use_prev) {
        print_source_excerpt(filename, tok->span.start_line, tok->span.start_col);
    } else {
        if (tok->span.start_line == tok->span.end_line && tok->span.end_col > tok->span.start_col) {
            print_source_excerpt_span(filename, tok->span.start_line, tok->span.start_col, tok->span.end_col);
        } else {
            print_source_excerpt(filename, tok->span.start_line, tok->span.start_col);
        }
    }
}

void print_parse_error(ParseError *error) {
    if (!error || !error->p || !error->message) return;

    const char *filename = error->p->filename ? error->p->filename : "<unknown>";
    print_error_header(error->message);

    if (!error->token) {
        fprintf(stderr, "   %s\n", filename);
        return;
    }

    Token display_tok = *error->token;
    if (error->use_prev_token) {
        Token *prev = peek(error->p, -1);
        if (prev) {
            display_tok = *prev;
            display_tok.span.start_col += (size_t)display_tok.slice.len;
        }
    }

    print_error_location(filename, &display_tok);
    print_error_source(filename, &display_tok, error->use_prev_token);
}
