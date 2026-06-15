#pragma once

#include <stddef.h>
#include "token.h"
#include "dynamic_array.h"
#include "arena.h"

#include "ast.h"

/* Parser structure */
typedef struct {
    DynArray   *tokens;   /* borrowed: DynArray of Token (Tokens stored in tokens.data) */
    size_t      current;  /* next token index to consume (0..tokens.count) */
    size_t      end;      /* tokens.count (one-past-last) */
    char       *filename; /* pointer to filename string in arena */
    Arena      *arena;    /* arena that owns parser/filename/messages */
} Parser;


typedef struct {
    char     *message;  /* pointer to error in arena */
    Token    *token;    /* token that caused the error (NULL if not applicable) */
    Parser   *p;        /* parser where error occurred */
    int      use_prev_token; /* if set, use token before 'token' for error location */
} ParseError;



/* Create parser allocated inside the given arena; the arena owns the parser and filename. */
Parser *parser_create(DynArray *tokens, char *filename, Arena *arena); /* filename is already in arena */

void parser_free(Parser *parser); /* free parser resources */

/* Basic token access */
Token   *current_token(Parser *p);            /* token at p->current or NULL if at end */
Token   *peek(Parser *p, size_t offset);     /* token at current+offset or NULL if OOB */

/* Advance and consume helpers */
Token   *parser_advance(Parser *p);                       /* consume and return token previously current, or NULL */
Token   *consume(Parser *p, TokenType expected);          /* consume when exact type expected, else NULL */
int      parser_match(Parser *p, TokenType expected);    /* if current==expected, advance and return 1 */

/* utility: create parse error message. This will allocate message in parser->arena */
void     create_parse_error(ParseError *err_out, Parser *p, const char *message, Token *token);

/* Pretty-print a parse error (to stderr). */
void     print_parse_error(ParseError *error);

/* Shared helpers for creating nodes and dynamic arrays with error handling */
AstNode *new_node_or_err(Parser *p, AstNodeType kind, ParseError *err, const char *oom_msg);
DynArray *alloc_dynarray(Parser *p, ParseError *err, size_t elem_size, int initial_capacity, const char *oom_msg);
