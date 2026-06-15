#include "parse_statements.h"
#include "parse_expressions.h"
#include "parse_declarations.h"
#include "parser.h"
#include "dynamic_array.h"
#include "lexer.h"
#include "ast.h"
#include <limits.h>
#include <errno.h>
#include <math.h>
#include <stdio.h>

// <Block> ::= '{' { <Statement> } '}'
AstNode *parse_block(Parser *p, ParseError *err) {
    AstNode *block = ast_create_node(AST_BLOCK, p->arena, p->filename);

    if (!block) {
        if (err) create_parse_error(err, p, "out of memory creating block node", NULL);
        return NULL;
    }

    block->data.block.statements = arena_alloc(p->arena, sizeof(DynArray));
    if (!block->data.block.statements) {
        if (err) create_parse_error(err, p, "out of memory creating block statements array", NULL);
        return NULL;
    }
    dynarray_init_in_arena(block->data.block.statements, p->arena, sizeof(AstNode*), 4);

    /* consume '{' and remember its span as block start */
    Token *lbrace = consume(p, TOK_LBRACE);
    if (!lbrace) { create_parse_error(err, p, "expected '{' at start of block", current_token(p)); return NULL; }
    Span start_span = lbrace->span;

    /* parse statements until '}' */
    while (1) {
        Token *current = current_token(p);
        if (!current) {
            err->use_prev_token = true;
            if (err) create_parse_error(err, p, "unexpected end of input in block, expected '}'", current);
            return NULL;
        }

        if (current->type == TOK_EOF) {
            if (err) create_parse_error(err, p, "unexpected end of input in block, expected '}'", current);
            return NULL;
        }

        if (current->type == TOK_RBRACE) {
            Token *rbrace = consume(p, TOK_RBRACE);
            /* set block span from '{' to '}' */
            block->span = span_join(&start_span, &rbrace->span);
            break; // end of block
        }

        AstNode *stmt = parse_statement(p, err);
        if (!stmt) return NULL; // parse_statement already set err

        if (dynarray_push_value(block->data.block.statements, &stmt) != 0) {
            if (err) create_parse_error(err, p, "out of memory adding statement to block", NULL);
            return NULL;
        }
    }

    return block;
}

AstNode *parse_defer_statement(Parser *p, ParseError *err) {
    Token *defer_tok = consume(p, TOK_DEFER);
    if (!defer_tok) {
        if (err) create_parse_error(err, p, "expected 'defer' keyword", current_token(p));
        return NULL;
    }
    Span start_span = defer_tok->span;

    AstNode *defer_stmt = new_node_or_err(p, AST_DEFER_STATEMENT, err, "out of memory creating defer statement node");
    if (!defer_stmt) return NULL;

    Token *current = current_token(p);
    if (!current) {
        if (err) create_parse_error(err, p, "expected block or expression after 'defer'", NULL);
        return NULL;
    }

    AstNode *body = NULL;
    if (current->type == TOK_LBRACE) {
        body = parse_block(p, err);
    } else {
        body = parse_statement(p, err);
    }

    if (!body) return NULL;
    defer_stmt->data.defer_statement.body = body;
    defer_stmt->span = span_join(&start_span, &body->span);

    return defer_stmt;
}

AstNode *parse_statement(Parser *p, ParseError *err) {
    Token *tok = current_token(p);
    if (!tok) {
        if (err) create_parse_error(err, p, "unexpected end of input in statement", NULL);
        return NULL;
    }

    AstNode *stmt = NULL;
    switch (tok->type) {
        case TOK_IF:
            stmt = parse_if_statement(p, err);
            break;
        case TOK_WHILE:
            stmt = parse_while_statement(p, err);
            break;
        case TOK_FOR:
            stmt = parse_for_statement(p, err);
            break;
        case TOK_RETURN:
            stmt = parse_return_statement(p, err);
            break;
        case TOK_BREAK:
            stmt = parse_break_statement(p, err);
            break;
        case TOK_CONTINUE:
            stmt = parse_continue_statement(p, err);
            break;
        case TOK_DEFER:
            stmt = parse_defer_statement(p, err);
            break;
        case TOK_LBRACE:
            stmt = parse_block(p, err);
            break;
        case TOK_FN:
            if (err) create_parse_error(err, p, "function declarations are not allowed inside statements or blocks", tok);
            return NULL;
        case TOK_CONST:
            /* Allow const declarations as statements */
            stmt = parse_declaration_stmt(p, err);
            break;
        case TOK_IDENTIFIER: {
            Token *next = peek(p, 1);
            if (!next) {
                if (err) create_parse_error(err, p, "unexpected end of input after identifier", tok);
                return NULL;
            }
            if (next->type == TOK_COLON) {
                /* variable declaration statement */
                stmt = parse_declaration_stmt(p, err);
            } else {
                /* expression statement (covers function calls, assignments, etc.) */
                stmt = parse_expression_statement(p, err);
            }
            break;
        }            
        default:
            /* treat as expression statement */
            stmt = parse_expression_statement(p, err);
            break;
    }

    return stmt;
}

AstNode *parse_if_statement(Parser *p, ParseError *err) {
    Token *if_tok = consume(p, TOK_IF);
    if (!if_tok) {
        if (err) create_parse_error(err, p, "expected 'if' keyword", current_token(p));
        return NULL;
    }
    Span start_span = if_tok->span;

    AstNode *if_stmt = ast_create_node(AST_IF_STATEMENT, p->arena, p->filename);
    if (!if_stmt) {
        if (err) create_parse_error(err, p, "out of memory creating if statement node", current_token(p));
        return NULL;
    }


    /* parse the condition expression */
    if_stmt->data.if_statement.condition = parse_expression(p, err);
    if (!if_stmt->data.if_statement.condition) {
        return NULL;
    }

    /* parse the then block */
    if_stmt->data.if_statement.then_branch = parse_block(p, err);
    if (!if_stmt->data.if_statement.then_branch) {
        return NULL;
    }

    /* by default, the end span is the end of the then-branch */
    Span end_span = if_stmt->data.if_statement.then_branch->span;

    /* parse the optional else (else if or else block) */
    Token *else_tok = consume(p, TOK_ELSE);
    if (else_tok) {
        Token *next = current_token(p);
        if (!next) {
            if (err) create_parse_error(err, p, "unexpected end after 'else'", current_token(p));
            return NULL;
        }

        if (next->type == TOK_IF) {
            /* else-if: delegate to parse_if_statement (it will consume 'if') */
            AstNode *else_if = parse_if_statement(p, err);
            if (!else_if) {
                return NULL;
            }
            if_stmt->data.if_statement.else_branch = else_if;
            end_span = else_if->span;
        } else {
            /* else block: parse_block consumes the '{' */
            AstNode *else_block = parse_block(p, err);
            if (!else_block) {
                return NULL;
            }
            if_stmt->data.if_statement.else_branch = else_block;
            end_span = else_block->span;
        }
    }

    /* compute span for the whole if-statement: from 'if' token to end of then/else */
    if_stmt->span = span_join(&start_span, &end_span);

    return if_stmt;
}


AstNode *parse_while_statement(Parser *p, ParseError *err) {
    if (!p) return NULL;

    Token *while_tok = consume(p, TOK_WHILE);
    if (!while_tok) { if (err) create_parse_error(err, p, "expected 'while' keyword", current_token(p)); return NULL; }

    AstNode *while_stmt = new_node_or_err(p, AST_WHILE_STATEMENT, err, "out of memory creating while statement node");
    if (!while_stmt) return NULL;

    while_stmt->data.while_statement.condition = parse_expression(p, err);
    if (!while_stmt->data.while_statement.condition) return NULL;

    while_stmt->data.while_statement.body = parse_block(p, err);
    if (!while_stmt->data.while_statement.body) return NULL;

    while_stmt->span = span_join(&while_tok->span, &while_stmt->data.while_statement.body->span);
    return while_stmt;
}

AstNode *parse_for_statement(Parser *p, ParseError *err) {
    if (!p) return NULL;

    Token *for_tok = consume(p, TOK_FOR);
    if (!for_tok) { 
        if (err) create_parse_error(err, p, "expected 'for' keyword", current_token(p)); 
        return NULL; 
    }

    AstNode *for_node = ast_create_node(AST_FOR_STATEMENT, p->arena, p->filename);
    if (!for_node) return NULL;

    if (!consume(p, TOK_LPAREN)) {
        if (err) create_parse_error(err, p, "expected '(' after 'for'", current_token(p));
        return NULL;
    }

    // 1. Init Clause (Declaration or Expression)
    if (current_token(p)->type != TOK_SEMICOLON) {
        AstNode *init = parse_statement(p, err); 
        if (!init) return NULL;
        for_node->data.for_statement.init = init;
    } else {
        consume(p, TOK_SEMICOLON); // Empty init
    }

    // 2. Condition Clause
    if (current_token(p)->type != TOK_SEMICOLON) {
        for_node->data.for_statement.condition = parse_expression(p, err);
        if (!for_node->data.for_statement.condition) return NULL;
    }
    if (!consume(p, TOK_SEMICOLON)) {
         if (err) create_parse_error(err, p, "expected ';' after condition", current_token(p));
         return NULL;
    }

    // 3. Post Clause
    if (current_token(p)->type != TOK_RPAREN) {
        for_node->data.for_statement.post = parse_expression(p, err);
        if (!for_node->data.for_statement.post) return NULL;
    }
    
    Token *rparen = consume(p, TOK_RPAREN);
    if (!rparen) {
        if (err) create_parse_error(err, p, "expected ')' after for clauses", current_token(p));
        return NULL;
    }

    // 4. Body
    for_node->data.for_statement.body = parse_block(p, err);
    if (!for_node->data.for_statement.body) return NULL;

    for_node->span = span_join(&for_tok->span, &for_node->data.for_statement.body->span);
    return for_node;
}

AstNode *parse_return_statement(Parser *p, ParseError *err) {
    if (!p) return NULL;

    Token *return_tok = consume(p, TOK_RETURN);
    if (!return_tok) { if (err) create_parse_error(err, p, "expected 'return' keyword", current_token(p)); return NULL; }

    AstNode *return_stmt = new_node_or_err(p, AST_RETURN_STATEMENT, err, "out of memory creating return statement node");
    if (!return_stmt) return NULL;

    Token *semi = current_token(p);
    if (semi && semi->type == TOK_SEMICOLON) {
        /* void return */
        return_stmt->data.return_statement.expression = NULL;
        return_stmt->span = return_tok->span;
    } else {
        /* return with expression */
        return_stmt->data.return_statement.expression = parse_expression(p, err);
        if (!return_stmt->data.return_statement.expression) return NULL;
        return_stmt->span = span_join(&return_tok->span, &return_stmt->data.return_statement.expression->span);
    }

    /* expect semicolon at end */
    semi = consume(p, TOK_SEMICOLON);
    if (!semi) { if (err) create_parse_error(err, p, "expected ';' after return statement", current_token(p)); return NULL; }
    return_stmt->span = span_join(&return_stmt->span, &semi->span);

    return return_stmt;
}

AstNode *parse_break_statement(Parser *p, ParseError *err) {
    if (!p) return NULL;

    Token *break_tok = consume(p, TOK_BREAK);
    if (!break_tok) { if (err) create_parse_error(err, p, "expected 'break' keyword", current_token(p)); return NULL; }

    AstNode *break_stmt = new_node_or_err(p, AST_BREAK_STATEMENT, err, "out of memory creating break statement node");
    if (!break_stmt) return NULL;

    break_stmt->span = break_tok->span;

    /* expect semicolon at end */
    Token *semi = consume(p, TOK_SEMICOLON);
    if (!semi) { if (err) create_parse_error(err, p, "expected ';' after break statement", current_token(p)); return NULL; }
    break_stmt->span = span_join(&break_stmt->span, &semi->span);

    return break_stmt;
}

AstNode *parse_continue_statement(Parser *p, ParseError *err) {
    if (!p) return NULL;

    Token *cont_tok = consume(p, TOK_CONTINUE);
    if (!cont_tok) { if (err) create_parse_error(err, p, "expected 'continue' keyword", current_token(p)); return NULL; }

    AstNode *cont_stmt = new_node_or_err(p, AST_CONTINUE_STATEMENT, err, "out of memory creating continue statement node");
    if (!cont_stmt) return NULL;

    cont_stmt->span = cont_tok->span;

    /* expect semicolon at end */
    Token *semi = consume(p, TOK_SEMICOLON);
    if (!semi) { if (err) create_parse_error(err, p, "expected ';' after continue statement", current_token(p)); return NULL; }
    cont_stmt->span = span_join(&cont_stmt->span, &semi->span);

    return cont_stmt;
}

AstNode *parse_expression_statement(Parser *p, ParseError *err) {
    AstNode *expr = parse_expression(p, err);

    if (!expr) return NULL;

    /* consume the semicolon */
    Token *semi = consume(p, TOK_SEMICOLON);
    if (!semi) {
        err->use_prev_token = true;
        create_parse_error(err, p, "expected ';' at end of expression statement", current_token(p));
        return NULL;
    }
    
    // Create the wrapper node!
    AstNode *stmt = ast_create_node(AST_EXPR_STATEMENT, p->arena, p->filename);
    if (!stmt) {
        if (err) create_parse_error(err, p, "out of memory creating expression statement node", NULL);
        return NULL;
    }
    
    stmt->data.expr_statement.expression = expr;
    stmt->span = span_join(&expr->span, &semi->span);
    
    return stmt;
}
