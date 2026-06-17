#include "parse_types.h"
#include "parse_expressions.h"
#include "parser.h"
#include "dynamic_array.h"
#include "lexer.h"
#include "ast.h"
#include <string.h>

/* Helper for parsing dotted identifiers like std.mem.Allocator as a type path or expression */
static AstNode *parse_path(Parser *p, ParseError *err) {
    Token *tok = current_token(p);
    if (!tok || tok->type != TOK_IDENTIFIER) {
        if (err) create_parse_error(err, p, "expected identifier", tok);
        return NULL;
    }

    AstNode *primary = new_node_or_err(p, AST_IDENTIFIER, err, "out of memory");
    if (!primary) return NULL;
    primary->data.identifier.intern_result = tok->record;
    primary->span = tok->span;
    consume(p, TOK_IDENTIFIER);

    while (current_token(p) && current_token(p)->type == TOK_DOT) {
        consume(p, TOK_DOT);
        Token *name_tok = consume(p, TOK_IDENTIFIER);
        if (!name_tok) {
            if (err) create_parse_error(err, p, "expected identifier after '.'", current_token(p));
            return NULL;
        }

        AstNode *member_access = new_node_or_err(p, AST_MEMBER_EXPR, err, "out of memory");
        if (!member_access) return NULL;

        member_access->data.member_expr.target = primary;
        member_access->data.member_expr.member = name_tok->record;
        member_access->span = span_join(&primary->span, &name_tok->span);
        primary = member_access;
    }
    return primary;
}

/* <Type> ::= { <PointerPrefix> } <TypeAtom> { <ArraySuffix> } { <PointerSuffix> } */
AstNode *parse_type(Parser *p, ParseError *err) {
    if (!p) return NULL;

    /* pointer prefixes (e.g. *i32) */
    DynArray *prefix_ptrs = alloc_dynarray(p, err, sizeof(Token), 4, "out of memory for pointer prefixes");
    if (!prefix_ptrs) return NULL;

    Token *token = current_token(p);
    while (token && token->type == TOK_STAR) {
        Token *star = consume(p, TOK_STAR);
        dynarray_push_value(prefix_ptrs, star);
        token = current_token(p);
    }

    AstNode *base = parse_type_atom(p, err);
    if (!base) return NULL;

    /* Apply prefix pointers (Right-to-Left) */
    for (size_t i = prefix_ptrs->count; i > 0; i--) {
        Token *star = (Token*)dynarray_get(prefix_ptrs, (int)i - 1);
        AstNode *ptr_type = new_node_or_err(p, AST_TYPE, err, "out of memory creating pointer type node");
        if (!ptr_type) return NULL;

        ptr_type->data.ast_type.kind = AST_TYPE_PTR;
        ptr_type->data.ast_type.u.ptr.target = base;
        ptr_type->data.ast_type.span = span_join(&star->span, &base->span);

        base = ptr_type;
    }

    token = current_token(p);

    /* Handle generic type arguments: Vec<i32, bool> */
    if (token && token->type == TOK_LT) {
        consume(p, TOK_LT);
        DynArray *type_args = alloc_dynarray(p, err, sizeof(AstNode*), 4, "out of memory for type args");
        if (!type_args) return NULL;

        do {
            AstNode *arg = parse_type(p, err);
            if (!arg) return NULL;
            dynarray_push_value(type_args, &arg);
            if (current_token(p) && current_token(p)->type == TOK_GT) break;
        } while (parser_match(p, TOK_COMMA));

        Token *rgt = consume(p, TOK_GT);
        if (!rgt) { if (err) create_parse_error(err, p, "expected '>'", current_token(p)); return NULL; }

        AstNode *app_type = new_node_or_err(p, AST_TYPE, err, "out of memory");
        if (!app_type) return NULL;
        app_type->data.ast_type.kind = AST_TYPE_APPLICATION;
        app_type->data.ast_type.u.application.base = base;
        app_type->data.ast_type.u.application.args = type_args;
        app_type->span = span_join(&base->span, &rgt->span);

        base = app_type;
        token = current_token(p);
    }

    /* arrays: [ <const-expr>? ] */
    /* Parse dimensions into a list, then wrap REVERSELY (Right-to-Left) */
    DynArray *dims = alloc_dynarray(p, err, sizeof(AstNode*), 4, "out of memory for array dimensions");
    if (!dims) return NULL;


    while (token && token->type == TOK_LBRACKET) {
        Token *lbr = consume(p, TOK_LBRACKET);
        if (!lbr) { if (err) create_parse_error(err, p, "expected '['", current_token(p)); return NULL; }

        AstNode *size_expr = NULL;
        Token *peek_tok = current_token(p);
        if (peek_tok && peek_tok->type != TOK_RBRACKET) {
            size_expr = parse_expression(p, err);
            if (!size_expr && err && err->message) return NULL;
        }

        Token *rbr = consume(p, TOK_RBRACKET);
        if (!rbr) { if (err) err->use_prev_token = true; create_parse_error(err, p, "expected ']'", current_token(p)); return NULL; }

        // We create the array node here to capture the span and size, but don't set 'elem' yet
        AstNode *array_type = new_node_or_err(p, AST_TYPE, err, "out of memory");
        if (!array_type) return NULL;

        array_type->data.ast_type.kind = AST_TYPE_ARRAY;
        array_type->data.ast_type.u.array.size_expr = size_expr;
        array_type->data.ast_type.u.array.elem = NULL; // Set later
        // Temporary span covering just the brackets/size
        array_type->span = span_join(&lbr->span, &rbr->span); 

        if (dynarray_push_value(dims, &array_type) != 0) {
             if (err) create_parse_error(err, p, "out of memory pushing array dim", NULL);
             return NULL;
        }
        token = current_token(p);
    }

    /* Apply dimensions Backwards (Right-to-Left) */
    for (size_t i = dims->count; i > 0; i--) {
        AstNode *dim_node = *(AstNode**)dynarray_get(dims, (int)i - 1);
        dim_node->data.ast_type.u.array.elem = base;
        // Extend span to cover the base it wraps
        dim_node->span = span_join(&base->span, &dim_node->span);
        base = dim_node;
    }

    token = current_token(p);

    /* pointer suffixes again (if any) */
    while (token && token->type == TOK_STAR) {
        Token *star = consume(p, TOK_STAR);
        if (!star) { if (err) create_parse_error(err, p, "expected '*'", current_token(p)); return NULL; }

        AstNode *ptr_type = new_node_or_err(p, AST_TYPE, err, "out of memory creating pointer type node");
        if (!ptr_type) return NULL;
        ptr_type->data.ast_type.kind = AST_TYPE_PTR;
        ptr_type->data.ast_type.u.ptr.target = base;
        ptr_type->data.ast_type.span = span_join(&base->span, &star->span);
        base = ptr_type;
        token = current_token(p);
    }

    return base;
}

/* <TypeAtom> ::= <Path> | LPAREN <Type> RPAREN | <FunctionType> */
AstNode *parse_type_atom(Parser *p, ParseError *err) {
    if (!p) return NULL;

    Token *tok = current_token(p);
    if (!tok) { if (err) create_parse_error(err, p, "unexpected end of input in type", NULL); return NULL; }

    if (tok->type == TOK_LPAREN) {
        Token *lpar = consume(p, TOK_LPAREN);
        AstNode *inner = parse_type(p, err);
        if (!inner) return NULL;
        Token *rparen = consume(p, TOK_RPAREN);
        if (!rparen) { if (err) create_parse_error(err, p, "expected ')' after type", current_token(p)); return NULL; }
        inner->span = span_join(&lpar->span, &rparen->span);
        return inner;
    }

    if (tok->type == TOK_FN) {
        AstNode *func_type = parse_function_type(p, err);
        if (!func_type) return NULL;
        return func_type;
    }

    // Handle paths: std.mem.Allocator or just i32
    if ((tok->type >= TOK_I8 && tok->type <= TOK_VOID) || tok->type == TOK_IDENTIFIER) {
        AstNode *type_node = new_node_or_err(p, AST_TYPE, err, "out of memory creating type node");
        if (!type_node) return NULL;
        type_node->data.ast_type.kind = AST_TYPE_PRIMITIVE;

        if (tok->type >= TOK_I8 && tok->type <= TOK_VOID) {
            type_node->data.ast_type.u.base.intern_result = tok->record;
            type_node->data.ast_type.u.base.path = NULL;
            type_node->data.ast_type.span = tok->span;
            consume(p, tok->type);
        } else {
            AstNode *primary = parse_path(p, err);
            if (!primary) return NULL;
            type_node->data.ast_type.span = primary->span;
            
            if (primary->node_type == AST_IDENTIFIER) {
                type_node->data.ast_type.u.base.intern_result = primary->data.identifier.intern_result;
                type_node->data.ast_type.u.base.path = NULL;
            } else if (primary->node_type == AST_MEMBER_EXPR) {
                type_node->data.ast_type.u.base.intern_result = NULL; 
                type_node->data.ast_type.u.base.path = primary; 
            } else {
                 type_node->data.ast_type.u.base.intern_result = NULL;
                 type_node->data.ast_type.u.base.path = primary;
            }
        }
        return type_node;
    }

    if (err) create_parse_error(err, p, "expected type name, path, or (type)", tok);
    return NULL;
}

InternResult *get_base_type(Parser *p, ParseError *err) {
    if (!p) return NULL;
    Token *tok = current_token(p);
    if (!tok) { if (err) create_parse_error(err, p, "unexpected end of input while looking for base type", NULL); return NULL; }

    /* base types are contiguous in token enum between TOK_I32 and TOK_VOID, plus identifiers for structs */
    /* Exclude TOK_STRUCT as it is not a base type by itself */
    if (((tok->type >= TOK_I32 && tok->type <= TOK_VOID) && tok->type != TOK_STRUCT) || tok->type == TOK_IDENTIFIER) {
        consume(p, tok->type);
        return tok->record;
    }

    if (err) create_parse_error(err, p, "expected base type", tok);
    return NULL;
}

/* <FunctionType> ::= FN LPAREN [ <TypeList> ] RPAREN [ ARROW <Type> ] */
AstNode *parse_function_type(Parser *p, ParseError *err) {
    if (!p) return NULL;

    Token *fn_tok = consume(p, TOK_FN);
    if (!fn_tok) { if (err) create_parse_error(err, p, "expected 'fn' keyword", current_token(p)); return NULL; }

    AstNode *type_node = new_node_or_err(p, AST_TYPE, err, "out of memory creating function type node");
    if (!type_node) return NULL;

    type_node->data.ast_type.kind = AST_TYPE_FUNC;
    type_node->data.ast_type.span = fn_tok->span;

    Token *lparen = consume(p, TOK_LPAREN);
    if (!lparen) { if (err) create_parse_error(err, p, "expected '(' after 'fn type'", current_token(p)); return NULL; }

    Token *maybe = current_token(p);
    if (maybe && maybe->type == TOK_RPAREN) {
        Token *r = consume(p, TOK_RPAREN);
        type_node->data.ast_type.u.func.param_types = NULL;
        type_node->data.ast_type.span = span_join(&type_node->data.ast_type.span, &r->span);
    } else {
        DynArray *params = alloc_dynarray(p, err, sizeof(AstNode*), 4, "out of memory creating param types array");
        if (!params) return NULL;

        AstNode *param = parse_type(p, err);
        if (!param) return NULL;
        if (dynarray_push_value(params, &param) != 0) { if (err) create_parse_error(err, p, "out of memory adding param type", NULL); return NULL; }

        Token *sep = current_token(p);
        while (sep && sep->type == TOK_COMMA) {
            consume(p, TOK_COMMA);
            param = parse_type(p, err);
            if (!param) return NULL;
            if (dynarray_push_value(params, &param) != 0) { if (err) create_parse_error(err, p, "out of memory adding param type", NULL); return NULL; }
            sep = current_token(p);
        }

        Token *rparen = consume(p, TOK_RPAREN);
        if (!rparen) { if (err) create_parse_error(err, p, "expected ')' after function parameter types", current_token(p)); return NULL; }

        type_node->data.ast_type.u.func.param_types = params;
        type_node->data.ast_type.span = span_join(&type_node->data.ast_type.span, &rparen->span);
    }

    /* optional return type */
    Token *arrow = current_token(p);
    if (arrow && arrow->type == TOK_ARROW) {
        consume(p, TOK_ARROW);
        AstNode *ret = parse_type(p, err);
        if (!ret) return NULL;
        type_node->data.ast_type.u.func.return_type = ret;
        type_node->data.ast_type.span = span_join(&type_node->data.ast_type.span, &ret->span);
    } else {
        /* already ensured closing paren covered span above */
        type_node->data.ast_type.u.func.return_type = NULL;
    }

    return type_node;
}
