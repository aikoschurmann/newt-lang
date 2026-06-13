#include "parse_statements.h"
#include "parser.h"
#include "dynamic_array.h"
#include "lexer.h"
#include "ast.h"
#include <limits.h>
#include <errno.h>
#include <math.h>
#include <stdio.h>


/* Helper: create AST node or set OOM parse error */
static AstNode *new_node_or_err(Parser *p, AstNodeType kind, ParseError *err, const char *oom_msg) {
    AstNode *n = ast_create_node(kind, p->arena, p->filename);
    if (!n) {
        if (err) create_parse_error(err, p, oom_msg, NULL);
    }
    return n;
}




static inline bool parse_int_lit(const char *s, size_t len, long long *out) {
    if (len == 0) return false;
    
    int base = 10;
    size_t start = 0;

    if (len > 2 && s[0] == '0') {
        if (s[1] == 'x' || s[1] == 'X') {
            base = 16;
            start = 2;
        } else if (s[1] == 'b' || s[1] == 'B') {
            base = 2;
            start = 2;
        }
    }

    uint64_t val = 0;
    bool any_digits = false;

    for (size_t i = start; i < len; i++) {
        if (s[i] == '_') continue; // Support underscores

        unsigned int d = 0;
        char c = s[i];
        if (c >= '0' && c <= '9') d = (unsigned int)(c - '0');
        else if (c >= 'a' && c <= 'f') d = (unsigned int)(10 + (c - 'a'));
        else if (c >= 'A' && c <= 'F') d = (unsigned int)(10 + (c - 'A'));
        else return false; // invalid char

        if (d >= (unsigned int)base) return false;

        // Check overflow
        if (val > (ULLONG_MAX - d) / (unsigned int)base) return false;

        val = val * (unsigned int)base + d;
        any_digits = true;
    }

    if (!any_digits) return false;
    if (base == 10 && val > LLONG_MAX) return false;

    *out = (long long)val;
    return true;
}




static inline bool parse_float_lit(const char *s, size_t len, double *out) {
    double val = 0.0;
    size_t i = 0;

    // integer part
    for (; i < len && s[i] >= '0' && s[i] <= '9'; i++) {
        val = val * 10.0 + (s[i] - '0');
    }

    // fractional part
    if (i < len && s[i] == '.') {
        i++;
        double frac = 0.0, base = 0.1;
        while (i < len && s[i] >= '0' && s[i] <= '9') {
            frac += (s[i] - '0') * base;
            base *= 0.1;
            i++;
        }
        val += frac;
    }

    // exponent part
    if (i < len && (s[i] == 'e' || s[i] == 'E')) {
        i++;
        int exp_sign = 1;
        if (i < len && (s[i] == '+' || s[i] == '-')) {
            if (s[i] == '-') exp_sign = -1;
            i++;
        }

        int exp_val = 0;
        while (i < len && s[i] >= '0' && s[i] <= '9') {
            exp_val = exp_val * 10 + (s[i] - '0');
            i++;
        }

        val *= pow(10.0, exp_sign * exp_val);
    }

    if (i != len) return false; // invalid chars remain

    *out = val;
    return true;
}





/* Helper: allocate + init a DynArray in arena, on error sets parse error and returns NULL */
static DynArray *alloc_dynarray(Parser *p, ParseError *err, size_t elem_size, int initial_capacity, const char *oom_msg) {
    DynArray *arr = arena_alloc(p->arena, sizeof(DynArray));
    if (!arr) {
        if (err) create_parse_error(err, p, oom_msg, NULL);
        return NULL;
    }
    dynarray_init_in_arena(arr, p->arena, elem_size, initial_capacity);
    return arr;
}

/* <Program> ::= { <Declaration> } */
AstNode *parse_program(Parser *p, ParseError *err) {
    if (!p) return NULL;

    AstNode *program = new_node_or_err(p, AST_PROGRAM, err, "out of memory creating program node");
    if (!program) return NULL;

    program->data.program.decls = alloc_dynarray(p, err, sizeof(AstNode*), 8, "out of memory creating program decls");
    if (!program->data.program.decls) return NULL;

    /* Parse a sequence of declarations. Keep track of overall span. */
    Span first_span = {0}, last_span = {0};
    bool have_any = false;

    for (;;) {
        /* If there's an error already recorded, stop parsing.
           parse_declaration sets err on failure. */
        if (err && err->message) return NULL;

        AstNode *decl = parse_declaration(p, err);
        if (!decl) break; /* no (more) declarations or parse error produced */

        if (dynarray_push_value(program->data.program.decls, &decl) != 0) {
            if (err) create_parse_error(err, p, "out of memory adding declaration to program", NULL);
            return NULL;
        }

        if (!have_any) {
            first_span = decl->span;
            have_any = true;
        }
        last_span = decl->span;
    }

    /* If parse_declaration produced an error earlier, we've already returned. Now check for trailing tokens. */
    if (p->current < p->end) {
        if (err && !err->message) { /* Only create error if there isn't one already */
            Token *t = current_token(p);
            create_parse_error(err, p, "trailing tokens after program end", t);
        }
        return NULL;
    }

    if (have_any) program->span = span_join(&first_span, &last_span);
    else if (p->tokens && p->tokens->count > 0) {
        Token *first = (Token*)dynarray_get(p->tokens, 0);
        program->span = first->span;
    } else {
        program->span = (Span){0,0,0,0};
    }

    return program;
}

AstNode *parse_declaration(Parser *p, ParseError *err) {
    if (!p) return NULL;

    InternResult *link_name = NULL;
    if (current_token(p) && current_token(p)->type == TOK_AT) {
        consume(p, TOK_AT);
        Token *attr_name = consume(p, TOK_IDENTIFIER);
        if (attr_name) {
            if (attr_name->slice.len == 4 && memcmp(attr_name->slice.ptr, "link", 4) == 0) {
                if (!consume(p, TOK_LPAREN)) {
                    if (err) create_parse_error(err, p, "expected '(' after @link", current_token(p));
                    return NULL;
                }
                Token *name_lit = consume(p, TOK_STRING_LIT);
                if (!name_lit) {
                    if (err) create_parse_error(err, p, "expected string literal in @link", current_token(p));
                    return NULL;
                }
                link_name = name_lit->record;
                if (!consume(p, TOK_RPAREN)) {
                    if (err) create_parse_error(err, p, "expected ')' after @link name", current_token(p));
                    return NULL;
                }
            } else {
                if (err) create_parse_error(err, p, "unknown attribute", attr_name);
                return NULL;
            }
        }
    }

    bool is_pub = (parser_match(p, TOK_PUB) != 0);

    Token *current = current_token(p);
    if (!current) {
        if (err) create_parse_error(err, p, "Unexpected end of input", NULL);
        return NULL;
    }

    if (current->type == TOK_EOF) {
        /* consume EOF to leave parser in a consistent state */
        consume(p, TOK_EOF);
        return NULL;
    }

    AstNode *decl = NULL;
    switch (current->type) {
        case TOK_IMPORT:
            if (is_pub) {
                if (err) create_parse_error(err, p, "public imports are not supported", current);
                return NULL;
            }
            decl = parse_import_declaration(p, err);
            return decl;
        case TOK_FN: 
            decl = parse_function_declaration(p, err); 
            if (decl) {
                decl->data.function_declaration.is_pub = is_pub;
                decl->data.function_declaration.link_name = link_name;
            }
            return decl;
        case TOK_STRUCT: 
            decl = parse_struct_declaration(p, err); 
            if (decl) {
                decl->data.struct_declaration.is_pub = is_pub;
                if (link_name) {
                    if (err) create_parse_error(err, p, "@link attribute not supported for structs", current);
                }
            }
            return decl;
        case TOK_CONST:
        case TOK_IDENTIFIER: 
            decl = parse_declaration_stmt(p, err); 
            if (decl) {
                decl->data.variable_declaration.is_pub = is_pub;
                if (link_name) {
                    if (err) create_parse_error(err, p, "@link attribute not supported for variables", current);
                }
            }
            return decl;
        default:
            if (err) create_parse_error(err, p, "expected function, struct or variable declaration", current);
            return NULL;
    }
}

/* operand_parser: parse one operand/sublevel and return AST node (or NULL on error). */
typedef AstNode *(*operand_parser_fn)(Parser *p, ParseError *err);

typedef OpKind (*map_token_to_op_fn)(Token *tok);

AstNode *parse_import_declaration(Parser *p, ParseError *err) {
    if (!p) return NULL;
    Token *import_tok = consume(p, TOK_IMPORT);
    if (!import_tok) return NULL;

    AstNode *decl = new_node_or_err(p, AST_IMPORT_DECLARATION, err, "out of memory creating import declaration node");
    if (!decl) return NULL;

    Token *name_tok = consume(p, TOK_IDENTIFIER);
    if (!name_tok) {
        if (err) create_parse_error(err, p, "expected module name after 'import'", current_token(p));
        return NULL;
    }
    decl->data.import_declaration.module_name = name_tok->record;

    Token *semi = consume(p, TOK_SEMICOLON);
    if (!semi) {
        if (err) create_parse_error(err, p, "expected ';' after import declaration", current_token(p));
        return NULL;
    }

    decl->span = span_join(&import_tok->span, &semi->span);
    return decl;
}

AstNode *parse_struct_declaration(Parser *p, ParseError *err) {
    if (!p) return NULL;
    Token *struct_kw = consume(p, TOK_STRUCT);
    if (!struct_kw) return NULL; // Should not happen if called from parse_declaration
    
    AstNode *decl = new_node_or_err(p, AST_STRUCT_DECLARATION, err, "out of memory creating struct declaration node");
    if (!decl) return NULL;

    Token *name_tok = consume(p, TOK_IDENTIFIER);
    if (!name_tok) {
        if (err) create_parse_error(err, p, "expected identifier after 'struct'", current_token(p));
        return NULL;
    }
    decl->data.struct_declaration.intern_result = name_tok->record;
    
    decl->data.struct_declaration.fields = arena_alloc(p->arena, sizeof(DynArray));
    if (!decl->data.struct_declaration.fields) {
        if (err) create_parse_error(err, p, "out of memory allocating struct fields array", current_token(p));
        return NULL;
    }
    dynarray_init_in_arena(decl->data.struct_declaration.fields, p->arena, sizeof(AstFieldDecl), 8);

    if (!consume(p, TOK_LBRACE)) {
        if (err) create_parse_error(err, p, "expected '{' starting struct body", current_token(p));
        return NULL;
    }

    Token *current = current_token(p);
    while (current && current->type != TOK_RBRACE && current->type != TOK_EOF) {
        AstFieldDecl field = {0};
        Token *field_name = consume(p, TOK_IDENTIFIER);
        if (!field_name) {
            if (err) create_parse_error(err, p, "expected field name identifier", current_token(p));
            return NULL;
        }
        field.name = field_name->record;

        if (!consume(p, TOK_COLON)) {
            if (err) create_parse_error(err, p, "expected ':' after field name", current_token(p));
            return NULL;
        }

        AstNode *type = parse_type(p, err);
        if (!type) return NULL;
        field.type = type;

        if (!consume(p, TOK_SEMICOLON)) {
            if (err) create_parse_error(err, p, "expected ';' after field type", current_token(p));
            return NULL;
        }

        dynarray_push_value(decl->data.struct_declaration.fields, &field);
        current = current_token(p);
    }

    Token *rbrace = consume(p, TOK_RBRACE);
    if (!rbrace) {
        if (err) create_parse_error(err, p, "expected '}' closing struct body", current_token(p));
        return NULL;
    }

    decl->span = span_join(&struct_kw->span, &rbrace->span);
    return decl;
}

AstNode *parse_declaration_stmt(Parser *p, ParseError *err) {
    if (!p) return NULL;

    AstNode *declaration = parse_variable_declaration(p, err);
    if (!declaration) return NULL;

    Token *semi = consume(p, TOK_SEMICOLON);
    if (!semi) {
        if (err) {err->use_prev_token = true; create_parse_error(err, p, "expected ';' after variable declaration", current_token(p));}
        return NULL;
    }

    declaration->span = span_join(&declaration->span, &semi->span);
    return declaration;
}

AstNode *parse_variable_declaration(Parser *p, ParseError *err) {
    if (!p) return NULL;

    AstNode *declaration = new_node_or_err(p, AST_VARIABLE_DECLARATION, err, "out of memory creating variable declaration node");
    if (!declaration) return NULL;

    Token *tok = current_token(p);
    if (!tok) {
        if (err) create_parse_error(err, p, "unexpected end of input in variable declaration", NULL);
        return NULL;
    }

    /* optional 'const' */
    if (tok->type == TOK_CONST) {
        Token *const_tok = consume(p, TOK_CONST);
        declaration->data.variable_declaration.is_const = 1;
        declaration->span = const_tok->span;
    } else {
        declaration->span = (Span){0,0,0,0};
    }

    Token *name_tok = consume(p, TOK_IDENTIFIER);
    if (!name_tok) {
        if (err) create_parse_error(err, p, "expected identifier in variable declaration", current_token(p));
        return NULL;
    }
    declaration->data.variable_declaration.intern_result = name_tok->record;

    /* ensure span covers the identifier (and 'const' if present) */
    if (declaration->span.start_line == 0) declaration->span = name_tok->span;
    else declaration->span = span_join(&declaration->span, &name_tok->span);

    if (!consume(p, TOK_COLON)) {
        if (err) create_parse_error(err, p, "expected ':' after variable name", current_token(p));
        return NULL;
    }

    AstNode *type = parse_type(p, err);
    if (!type) return NULL; /* parse_type produced an error */
    declaration->data.variable_declaration.type = type;

    /* optional initializer */
    AstNode *initializer = NULL;
    
    // parser_match consumes the token, so we should NOT call consume() again
    if (parser_match(p, TOK_ASSIGN)) {
        // Token *assign_tok = consume(p, TOK_ASSIGN); // REMOVED (Double consume bug)

        Token *token = current_token(p);
        if (token && token->type == TOK_LBRACE) {
            initializer = parse_initializer_list(p, err);
        } else {
            initializer = parse_expression(p, err);
        }
        if (!initializer) return NULL;
        declaration->span = span_join(&declaration->span, &initializer->span);
    } else {
        declaration->span = span_join(&declaration->span, &type->span);
    }

    declaration->data.variable_declaration.initializer = initializer;
    return declaration;
}

// <FunctionDeclaration> ::= FN IDENTIFIER LPAREN [ <ParamList> ] RPAREN [ ARROW <Type> ] <Block>
AstNode *parse_function_declaration(Parser *p, ParseError *err) {
    AstNode *func_decl = ast_create_node(AST_FUNCTION_DECLARATION, p->arena, p->filename);
    if (!func_decl) {
        if (err) create_parse_error(err, p, "out of memory creating function declaration node", NULL);  
        return NULL;
    }
    func_decl->data.function_declaration.params = arena_alloc(p->arena, sizeof(DynArray));
    if (!func_decl->data.function_declaration.params) {
        if (err) create_parse_error(err, p, "out of memory creating function params", NULL);
        return NULL;
    }
    dynarray_init_in_arena(func_decl->data.function_declaration.params, p->arena, sizeof(AstNode*), 4);

    /* fn */
    Token *fn_tok = consume(p, TOK_FN);
    if (!fn_tok) { create_parse_error(err, p, "expected 'fn' keyword at start of function declaration", current_token(p)); return NULL; }
    
    /* Initialize span with the fn token */
    func_decl->span = fn_tok->span;

    /* name */
    Token *name_tok = consume(p, TOK_IDENTIFIER);
    if (!name_tok) { create_parse_error(err, p, "expected function name", current_token(p)); return NULL; }
    func_decl->data.function_declaration.intern_result = name_tok->record;

    /* parameters */
    if (!consume(p, TOK_LPAREN)) { create_parse_error(err, p, "expected '(' after function name", current_token(p)); return NULL; }
    
    // parse parameters
    if (!parse_parameter_list(p, func_decl, err)) {
        return NULL; // parse_parameter_list already freed func_decl via fail_with
    }

    if (!consume(p, TOK_RPAREN)) { create_parse_error(err, p, "expected ')' after function parameters", current_token(p)); return NULL; }

    /* optional return type */
    Token *arrow_tok = current_token(p);
    if (arrow_tok && arrow_tok->type == TOK_ARROW) {
        consume(p, TOK_ARROW);
        AstNode *return_type = parse_type(p, err);
        if (!return_type) { return NULL; }
        func_decl->data.function_declaration.return_type = return_type;
    } else {
        func_decl->data.function_declaration.return_type = NULL; // no return type
    }

    /* body or semicolon */
    Token *tok = current_token(p);
    if (tok && tok->type == TOK_SEMICOLON) {
        consume(p, TOK_SEMICOLON);
        func_decl->data.function_declaration.body = NULL;
        func_decl->span = span_join(&func_decl->span, &tok->span);
    } else {
        func_decl->data.function_declaration.body = parse_block(p, err);
        if (!func_decl->data.function_declaration.body) return NULL; /* parse_block produced an error */
        
        /* Update span to include the entire function from fn token to end of body */
        func_decl->span = span_join(&func_decl->span, &func_decl->data.function_declaration.body->span);
    }
    
    return func_decl;
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
        Token *star = (Token*)dynarray_get(prefix_ptrs, i - 1);
        AstNode *ptr_type = new_node_or_err(p, AST_TYPE, err, "out of memory creating pointer type node");
        if (!ptr_type) return NULL;

        ptr_type->data.ast_type.kind = AST_TYPE_PTR;
        ptr_type->data.ast_type.u.ptr.target = base;
        ptr_type->data.ast_type.span = span_join(&star->span, &base->span);

        base = ptr_type;
    }

    token = current_token(p);

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
        AstNode *dim_node = *(AstNode**)dynarray_get(dims, i - 1);
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

/* <TypeAtom> ::= <BaseType> | LPAREN <Type> RPAREN | <FunctionType> */
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

    InternResult *base_type = get_base_type(p, err);
    if (!base_type) return NULL;

    AstNode *type_node = new_node_or_err(p, AST_TYPE, err, "out of memory creating type node");
    if (!type_node) return NULL;

    type_node->data.ast_type.kind = AST_TYPE_PRIMITIVE;
    type_node->data.ast_type.span = tok->span;
    type_node->data.ast_type.u.base.intern_result = base_type;
    return type_node;
}


InternResult *get_base_type(Parser *p, ParseError *err) {
    if (!p) return NULL;
    Token *tok = current_token(p);
    if (!tok) { if (err) create_parse_error(err, p, "unexpected end of input while looking for base type", NULL); return NULL; }

    /* base types are contiguous in token enum between TOK_I32 and TOK_VOID, plus identifiers for structs */
    if ((tok->type >= TOK_I32 && tok->type <= TOK_VOID) || tok->type == TOK_IDENTIFIER) {
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

/* Generic left-associative binary parser */
static AstNode *parse_left_assoc_binary(Parser *p, ParseError *err,
                        operand_parser_fn parse_operand,
                        map_token_to_op_fn map_op,
                        const char *oom_msg)
{
    AstNode *lhs = parse_operand(p, err);
    if (!lhs) return NULL;

    Token *token = current_token(p);
    int op;
    while (token && (op = map_op(token)) != OP_NULL) {
        /* copy operator token for span/error reporting */
        Token op_token = *token;
        consume(p, token->type);

        AstNode *rhs = parse_operand(p, err);
        if (!rhs) return NULL;

        AstNode *bin = new_node_or_err(p, AST_BINARY_EXPR, err, oom_msg);
        if (!bin) {
            if (err) create_parse_error(err, p, oom_msg, &op_token);
            return NULL;
        }

        bin->data.binary_expr.left  = lhs;
        bin->data.binary_expr.right = rhs;
        bin->data.binary_expr.op    = (OpKind)op;
        bin->span = span_join(&lhs->span, &rhs->span);

        lhs = bin;
        token = current_token(p);
    }

    return lhs;
}

AstNode *parse_expression(Parser *p, ParseError *err) {
    Token *tok = current_token(p);
    AstNode *lhs = parse_logical_or(p, err);
    if (!lhs) return NULL;

    Token *token = current_token(p);
    if (token && is_assignment_op(token->type)) {
        AstNode *assignment = parse_assignment(p, lhs, err);
        if (!assignment) return NULL;
        return assignment;
    }
    return lhs;
}



OpKind map_assignment_op(Token *tok) {
    if (!tok) return OP_NULL;
    switch (tok->type) {
        case TOK_ASSIGN: return OP_ASSIGN;
        case TOK_PLUS_EQ: return OP_PLUS_EQ;
        case TOK_MINUS_EQ: return OP_MINUS_EQ;
        case TOK_STAR_EQ: return OP_MUL_EQ;
        case TOK_SLASH_EQ: return OP_DIV_EQ;
        case TOK_PERCENT_EQ: return OP_MOD_EQ;
        default: return OP_NULL;
    }
}

AstNode* parse_assignment(Parser *p, AstNode *lhs, ParseError *err) {
    if (!p || !lhs) return NULL;

    if (!is_lvalue_node(lhs)) {
        if (err) create_parse_error(err, p, "left-hand side of assignment must be an lvalue", NULL);
        return NULL;
    }

    Token *op_tok = current_token(p);
    if (!op_tok || !is_assignment_op(op_tok->type)) {
        if (err) create_parse_error(err, p, "expected assignment operator", current_token(p));
        return NULL;
    }
    consume(p, op_tok->type);

    AstNode *rhs = parse_expression(p, err);
    if (!rhs) return NULL;

    AstNode *assign = new_node_or_err(p, AST_ASSIGNMENT_EXPR, err, "out of memory creating assignment node");
    if (!assign) return NULL;

    assign->data.assignment_expr.lvalue = lhs;
    assign->data.assignment_expr.rvalue = rhs;
    assign->data.assignment_expr.op = map_assignment_op(op_tok);
    assign->span = span_join(&lhs->span, &rhs->span);
    return assign;
}

OpKind map_logical_or_op(Token *tok) {
    if (!tok) return OP_NULL;
    if (tok->type == TOK_OR_OR) return OP_OR;
    return OP_NULL;
}

AstNode *parse_logical_or(Parser *p, ParseError *err) {
    return parse_left_assoc_binary(p, err, parse_logical_and, map_logical_or_op, "out of memory creating logical-or node");
}

OpKind map_logical_and_op(Token *tok) {
    if (!tok) return OP_NULL;
    if (tok->type == TOK_AND_AND) return OP_AND;
    return OP_NULL;
}
AstNode *parse_logical_and(Parser *p, ParseError *err) {
    return parse_left_assoc_binary(p, err, parse_equality, map_logical_and_op, "out of memory creating logical-and node");
}

OpKind map_equality_op(Token *tok) {
    if (!tok) return OP_NULL;
    switch (tok->type) {
        case TOK_EQ_EQ: return OP_EQ;
        case TOK_BANG_EQ: return OP_NEQ;
        default: return OP_NULL;
    }
}
AstNode *parse_equality(Parser *p, ParseError *err) {
    return parse_left_assoc_binary(p, err, parse_relational, map_equality_op, "out of memory creating equality node");
}

OpKind map_relational_op(Token *tok) {
    if (!tok) return OP_NULL;
    switch (tok->type) {
        case TOK_LT: return OP_LT;
        case TOK_GT: return OP_GT;
        case TOK_LT_EQ: return OP_LE;
        case TOK_GT_EQ: return OP_GE;
        default: return OP_NULL;
    }
}
AstNode *parse_relational(Parser *p, ParseError *err) {
    return parse_left_assoc_binary(p, err, parse_additive, map_relational_op, "out of memory creating relational node");
}

OpKind map_additive_op(Token *tok) {
    if (!tok) return OP_NULL;
    switch (tok->type) {
        case TOK_PLUS: return OP_ADD;
        case TOK_MINUS: return OP_SUB;
        default: return OP_NULL;
    }
}
AstNode *parse_additive(Parser *p, ParseError *err) {
    return parse_left_assoc_binary(p, err, parse_multiplicative, map_additive_op, "out of memory creating additive node");
}

OpKind map_multiplicative_op(Token *tok) {
    if (!tok) return OP_NULL;
    switch (tok->type) {
        case TOK_STAR: return OP_MUL;
        case TOK_SLASH: return OP_DIV;
        case TOK_PERCENT: return OP_MOD;
        default: return OP_NULL;
    }
}
AstNode *parse_multiplicative(Parser *p, ParseError *err) {
    return parse_left_assoc_binary(p, err, parse_cast, map_multiplicative_op, "out of memory creating multiplicative node");
}

AstNode *parse_cast(Parser *p, ParseError *err) {
    AstNode *expr = parse_unary(p, err);
    if (!expr) return NULL;

    while (current_token(p) && current_token(p)->type == TOK_AS) {
        consume(p, TOK_AS);
        AstNode *target_type_node = parse_type(p, err);
        if (!target_type_node) return NULL;

        AstNode *cast = new_node_or_err(p, AST_CAST, err, "out of memory creating cast node");
        if (!cast) return NULL;

        cast->data.cast_expr.expr = expr;
        cast->data.cast_expr.target_type = NULL;
        cast->data.cast_expr.target_type_node = target_type_node;
        cast->span = span_join(&expr->span, &target_type_node->span);
        expr = cast;
    }
    return expr;
}

OpKind map_unary_op(Token *tok) {
    if (!tok) return OP_NULL;
    switch (tok->type) {
        case TOK_PLUS: return OP_ADD;
        case TOK_MINUS: return OP_SUB;
        case TOK_BANG: return OP_NOT;
        case TOK_STAR: return OP_DEREF;
        case TOK_AMP: return OP_ADRESS;
        case TOK_PLUSPLUS: return OP_PRE_INC;
        case TOK_MINUSMINUS: return OP_PRE_DEC;
        default: return OP_NULL;
    }
}

AstNode *parse_unary(Parser *p, ParseError *err) {
    Token *token = current_token(p);
    if (token && (token->type == TOK_PLUS || token->type == TOK_MINUS || token->type == TOK_BANG || token->type == TOK_STAR || token->type == TOK_AMP || token->type == TOK_PLUSPLUS || token->type == TOK_MINUSMINUS)) {
        Token *op_token = consume(p, token->type);
        if (!op_token) { if (err) create_parse_error(err, p, "failed to consume prefix operator", token); return NULL; }
        AstNode *operand = parse_unary(p, err);
        if (!operand) return NULL;

        AstNode *unary = new_node_or_err(p, AST_UNARY_EXPR, err, "out of memory creating unary node");
        if (!unary) return NULL;

        unary->data.unary_expr.expr = operand;
        unary->data.unary_expr.op = map_unary_op(op_token);
        unary->span = span_join(&op_token->span, &operand->span);
        return unary;
    }
    return parse_postfix(p, err);
}

AstNode *parse_postfix(Parser *p, ParseError *err) {
    AstNode *primary = parse_primary(p, err);
    if (!primary) return NULL;

    Token *token = current_token(p);
    while (token && (token->type == TOK_PLUSPLUS || token->type == TOK_MINUSMINUS || token->type == TOK_LBRACKET || token->type == TOK_LPAREN || token->type == TOK_DOT)) {
        if (token->type == TOK_PLUSPLUS || token->type == TOK_MINUSMINUS) {
            Token *op_tok = token;
            AstNode *postfix = new_node_or_err(p, AST_UNARY_EXPR, err, "out of memory creating postfix node");
            if (!postfix) return NULL;

            postfix->data.unary_expr.expr = primary;
            postfix->data.unary_expr.op = (op_tok->type == TOK_PLUSPLUS) ? OP_POST_INC : OP_POST_DEC;
            postfix->span = span_join(&primary->span, &op_tok->span);

            consume(p, op_tok->type);
            primary = postfix;

        } else if (token->type == TOK_LBRACKET) {
            Token *lbr = consume(p, TOK_LBRACKET);
            AstNode *index = parse_expression(p, err);
            if (!index) return NULL;
            Token *rbr = consume(p, TOK_RBRACKET);
            if (!rbr) { if (err) create_parse_error(err, p, "expected ']' after array index expression", token); return NULL; }

            AstNode *array_access = new_node_or_err(p, AST_SUBSCRIPT_EXPR, err, "out of memory creating subscript node");
            if (!array_access) return NULL;

            array_access->data.subscript_expr.target = primary;
            array_access->data.subscript_expr.index = index;
            array_access->span = span_join(&primary->span, &rbr->span);
            primary = array_access;

        } else if (token->type == TOK_LPAREN) {
            Token *lparen = consume(p, TOK_LPAREN);

            AstNode *func_call = new_node_or_err(p, AST_CALL_EXPR, err, "out of memory creating function call node");
            if (!func_call) return NULL;

            func_call->data.call_expr.args = alloc_dynarray(p, err, sizeof(AstNode*), 4, "out of memory creating function call args array");
            if (!func_call->data.call_expr.args) return NULL;

            if (!parse_argument_list(p, func_call, err)) return NULL;

            Token *rparen = consume(p, TOK_RPAREN);
            if (!rparen) { if (err) create_parse_error(err, p, "expected ')' after function call arguments", token); return NULL; }

            func_call->data.call_expr.callee = primary;
            func_call->span = span_join(&primary->span, &rparen->span);
            primary = func_call;
        } else if (token->type == TOK_DOT) {
            consume(p, TOK_DOT);
            Token *name_tok = consume(p, TOK_IDENTIFIER);
            if (!name_tok) {
                if (err) create_parse_error(err, p, "expected identifier after '.'", current_token(p));
                return NULL;
            }

            AstNode *member_access = new_node_or_err(p, AST_MEMBER_EXPR, err, "out of memory creating member access node");
            if (!member_access) return NULL;

            member_access->data.member_expr.target = primary;
            member_access->data.member_expr.member = name_tok->record;
            member_access->span = span_join(&primary->span, &name_tok->span);
            primary = member_access;
        }

        token = current_token(p);
    }

    return primary;
}

LiteralType get_literal_type(TokenType type) {
    switch (type) {
        case TOK_INT_LIT: return INT_LITERAL;
        case TOK_FLOAT_LIT: return FLOAT_LITERAL;
        case TOK_TRUE: case TOK_FALSE: return BOOL_LITERAL;
        case TOK_STRING_LIT: return STRING_LITERAL;
        case TOK_CHAR_LIT: return CHAR_LITERAL;
        default: return LIT_UNKNOWN;
    }
}

int parse_argument_list(Parser *p, AstNode* call, ParseError *err) {
    Token *tok = current_token(p);
    if (!tok) { if (err) create_parse_error(err, p, "unexpected end of input in argument list", NULL); return 0; }
    if (tok->type == TOK_RPAREN) return 1; /* empty arglist */

    while (1) {
        tok = current_token(p);
        if (!tok) { if (err) create_parse_error(err, p, "unexpected end of input in argument list", NULL); return 0; }

        AstNode *argument = NULL;
        if (tok->type == TOK_LBRACE) {
            argument = parse_initializer_list(p, err);
            if (!argument) return 0;
        } else {
            argument = parse_expression(p, err);
            if (!argument) return 0;
        }

        if (dynarray_push_value(call->data.call_expr.args, &argument) != 0) { if (err) create_parse_error(err, p, "out of memory adding argument to call", NULL); return 0; }

        tok = current_token(p);
        if (!tok) { if (err) create_parse_error(err, p, "unexpected end of input in argument list", NULL); return 0; }
        if (tok->type == TOK_RPAREN) break;
        
        if (!consume(p, TOK_COMMA)) { if (err) create_parse_error(err, p, "expected a ',' or ')'", tok); return 0; }
        
        // Support trailing comma
        tok = current_token(p);
        if (tok && tok->type == TOK_RPAREN) break;
    }

    return 1;
}

LiteralType get_literal_value_kind(TokenType type) {
    return get_literal_type(type); /* identical mapping */
}

AstNode *parse_primary(Parser *p, ParseError *err) {
    Token *token = current_token(p);
    if (!token) { if (err) create_parse_error(err, p, "unexpected end of input, expected primary expression", NULL); return NULL; }

    switch (token->type) {
        
        case TOK_AT: {
            consume(p, TOK_AT);
            Token *name_tok = consume(p, TOK_IDENTIFIER);
            if (!name_tok) {
                if (err) create_parse_error(err, p, "expected intrinsic name after '@'", current_token(p));
                return NULL;
            }
            
            AstNode *intrinsic = new_node_or_err(p, AST_INTRINSIC, err, "out of memory creating intrinsic node");
            if (!intrinsic) return NULL;
            
            if (name_tok->slice.len == 5 && memcmp(name_tok->slice.ptr, "alloc", 5) == 0) {
                intrinsic->data.intrinsic.kind = INTRINSIC_ALLOC;
            } else if (name_tok->slice.len == 4 && memcmp(name_tok->slice.ptr, "free", 4) == 0) {
                intrinsic->data.intrinsic.kind = INTRINSIC_FREE;
            } else {
                if (err) create_parse_error(err, p, "unknown compiler intrinsic", name_tok);
                return NULL;
            }
            
            intrinsic->data.intrinsic.args = alloc_dynarray(p, err, sizeof(AstNode*), 4, "out of memory");
            if (!consume(p, TOK_LPAREN)) {
                if (err) create_parse_error(err, p, "expected '(' after intrinsic name", current_token(p));
                return NULL;
            }
            
            if (current_token(p)->type != TOK_RPAREN) {
                while (1) {
                    // Peek to see if this argument is a type
                    Token *peek_tok = current_token(p);
                    AstNode *arg = NULL;
                    
                    // Special case: @alloc's first argument MUST be a type
                    if (intrinsic->data.intrinsic.kind == INTRINSIC_ALLOC && intrinsic->data.intrinsic.args->count == 0) {
                        arg = parse_type(p, err);
                    } 
                    else if (peek_tok && (peek_tok->type >= TOK_I32 && peek_tok->type <= TOK_VOID)) {
                         arg = parse_type(p, err);
                    } else {
                         arg = parse_expression(p, err);
                    }
                    
                    if (!arg) return NULL;
                    dynarray_push_value(intrinsic->data.intrinsic.args, &arg);
                    
                    if (current_token(p)->type == TOK_RPAREN) break;
                    if (!consume(p, TOK_COMMA)) {
                        if (err) create_parse_error(err, p, "expected ',' or ')'", current_token(p));
                        return NULL;
                    }
                }
            }
            
            Token *rparen = consume(p, TOK_RPAREN);
            intrinsic->span = span_join(&name_tok->span, &rparen->span);
            return intrinsic;
        }

        case TOK_INT_LIT: case TOK_FLOAT_LIT: case TOK_TRUE: case TOK_FALSE: case TOK_CHAR_LIT: case TOK_STRING_LIT: {
            AstNode *literal = new_node_or_err(p, AST_LITERAL, err, "out of memory creating literal node");
            if (!literal) return NULL;

            literal->data.literal.type = get_literal_type(token->type);
            
            
            switch (token->type) {
                case TOK_INT_LIT: {
                    long long v;
                    if (parse_int_lit(token->slice.ptr, token->slice.len, &v)) {
                        literal->data.literal.value.int_val = v;
                    } else {
                        create_parse_error(err, p, "invalid integer literal or overflow", token);
                        return NULL;
                    }
                    break;
                }
                case TOK_FLOAT_LIT: {
                    double v;
                    if (parse_float_lit(token->slice.ptr, token->slice.len, &v)) {
                        literal->data.literal.value.float_val = v;
                    } else {
                        create_parse_error(err, p, "invalid float literal or overflow", token);
                        return NULL;
                    }
                    break;
                }
                case TOK_TRUE:
                    literal->data.literal.value.bool_val = 1;
                    break;
                case TOK_FALSE:
                    literal->data.literal.value.bool_val = 0;
                    break;
                case TOK_CHAR_LIT:
                    // Use the unescaped character from the token's record field
                    literal->data.literal.value.char_val = (char)(uintptr_t)token->record;
                    break;
                case TOK_STRING_LIT:
                    // Use the interned string from the token
                    literal->data.literal.value.string_val = token->record;
                    break;
                default:
                    /* For other types, set default values */
                    literal->data.literal.value.int_val = 0;
                    break;
            }   

            
            
            literal->is_const_expr = 0;
            literal->span = token->span;
            consume(p, token->type);
            return literal;
        }

        case TOK_IDENTIFIER: {
            Token *peek_tok = peek(p, 1);
            if (peek_tok && peek_tok->type == TOK_LBRACE) {
                
                // Ensure this is ACTUALLY a struct literal, not just an 'if' condition block!
                Token *peek_2 = peek(p, 2);
                Token *peek_3 = peek(p, 3);
                bool is_struct_lit = false;
                
                if (peek_2 && peek_2->type == TOK_RBRACE) {
                    is_struct_lit = true; // Name {}
                } else if (peek_2 && peek_2->type == TOK_IDENTIFIER && peek_3 && peek_3->type == TOK_COLON) {
                    is_struct_lit = true; // Name { field: ... }
                }

                if (is_struct_lit) {
                    /* Struct literal: IDENTIFIER { field: expr, ... } */
                    AstNode *struct_lit = new_node_or_err(p, AST_STRUCT_LITERAL, err, "out of memory creating struct literal node");
                    if (!struct_lit) return NULL;
                    struct_lit->data.struct_literal.intern_result = token->record;
                    Span start_span = token->span;
                    consume(p, TOK_IDENTIFIER); /* consume name */
                    consume(p, TOK_LBRACE);     /* consume '{' */
                    
                    struct_lit->data.struct_literal.fields = arena_alloc(p->arena, sizeof(DynArray));
                    if (!struct_lit->data.struct_literal.fields) {
                        if (err) create_parse_error(err, p, "out of memory allocating struct literal fields", current_token(p));
                        return NULL;
                    }
                    dynarray_init_in_arena(struct_lit->data.struct_literal.fields, p->arena, sizeof(AstFieldInit), 8);
                    
                    Token *current = current_token(p);
                    while (current && current->type != TOK_RBRACE && current->type != TOK_EOF) {
                        AstFieldInit init = {0};
                        Token *field_name = consume(p, TOK_IDENTIFIER);
                        if (!field_name) {
                            if (err) create_parse_error(err, p, "expected field name in struct literal", current_token(p));
                            return NULL;
                        }
                        init.name = field_name->record;
                        
                        if (!consume(p, TOK_COLON)) {
                            if (err) create_parse_error(err, p, "expected ':' after field name", current_token(p));
                            return NULL;
                        }
                        
                        init.expr = parse_expression(p, err);
                        if (!init.expr) return NULL;
                        
                        dynarray_push_value(struct_lit->data.struct_literal.fields, &init);
                        
                        current = current_token(p);
                        if (current && current->type == TOK_COMMA) {
                            consume(p, TOK_COMMA);
                            current = current_token(p);
                        } else {
                            break;
                        }
                    }
                    
                    Token *rbrace = consume(p, TOK_RBRACE);
                    if (!rbrace) {
                        if (err) create_parse_error(err, p, "expected '}' or ',' in struct literal", current_token(p));
                        return NULL;
                    }
                    
                    struct_lit->span = span_join(&start_span, &rbrace->span);
                    return struct_lit;
                }
            }

            AstNode *identifier = new_node_or_err(p, AST_IDENTIFIER, err, "out of memory creating identifier node");
            if (!identifier) return NULL;
            identifier->data.identifier.intern_result = token->record;
            identifier->span = token->span;
            consume(p, TOK_IDENTIFIER);
            return identifier;
        }

        case TOK_LPAREN: {
            Token *lpar = consume(p, TOK_LPAREN);
            AstNode *expr = parse_expression(p, err);
            if (!expr) return NULL;
            Token *r = consume(p, TOK_RPAREN);
            if (!r) { if (err) err->use_prev_token = true; create_parse_error(err, p, "expected ')' after expression", current_token(p)); return NULL; }
            expr->span = span_join(&lpar->span, &r->span);
            return expr;
        }

        case TOK_LBRACE:
            return parse_initializer_list(p, err);

        default:

            if (err) {err->use_prev_token = true; create_parse_error(err, p, "expected primary expression (literal, identifier, or parenthesized expression)", current_token(p));}
            return NULL;
    }
}

AstNode *parse_initializer_list(Parser *p, ParseError *err) {
    if (!p) return NULL;

    /* consume the opening '{' (and record its span for later) */
    Token *start_tok = consume(p, TOK_LBRACE);
    if (!start_tok) {
        if (err) create_parse_error(err, p, "expected '{' to start initializer list", current_token(p));
        return NULL;
    }
    Span start_span = start_tok->span;

    AstNode *init = ast_create_node(AST_INITIALIZER_LIST, p->arena, p->filename);
    if (!init) {
        if (err) create_parse_error(err, p, "out of memory creating initializer node", NULL);
        return NULL;
    }
    init->data.initializer_list.elements = arena_alloc(p->arena, sizeof(DynArray));
    if (!init->data.initializer_list.elements) {
        if (err) create_parse_error(err, p, "out of memory creating initializer elements", NULL);
        return NULL;
    }
    dynarray_init_in_arena(init->data.initializer_list.elements, p->arena, sizeof(AstNode*), 4);

    Token *tok = current_token(p);
    if (!tok) {
        /* unexpected EOF */
        create_parse_error(err, p, "unexpected end of input in initializer list", NULL);
        return NULL;
    }

    /* empty list: {} */
    if (tok->type == TOK_RBRACE) {
        Token *rbrace = consume(p, TOK_RBRACE);
        /* span covers from '{' to '}' */
        init->span = span_join(&start_span, &rbrace->span);
        return init;
    }

    /* parse elements: either nested initializer or expression, separated by commas.
       Trailing comma is NOT allowed: a ',' must be followed by another element. */
    while (1) {
        tok = current_token(p);
        if (!tok) {
            create_parse_error(err, p, "unexpected end of input in initializer list", NULL);
            return NULL;
        }

        AstNode *element = NULL;
        if (tok->type == TOK_LBRACE) {
            /* nested initializer — recursive call consumes the '{' */
            element = parse_initializer_list(p, err);
            if (!element) {
                /* err already set by recursive call; clean up */
                return NULL;
            }
        } else {
            /* normal expression */
            element = parse_expression(p, err);
            if (!element) {
                return NULL; /* parse_expression set err */
            }
        }

        /* push element into the initializer array */
        if (dynarray_push_value(init->data.initializer_list.elements, &element) != 0) {
            create_parse_error(err, p, "out of memory adding element to initializer list", NULL);
            return NULL;
        }

        /* after element, expect comma or '}' */
        tok = current_token(p);
        if (!tok) {
            create_parse_error(err, p, "unexpected end of input in initializer list", NULL);
            return NULL;
        }

        if (tok->type == TOK_COMMA) {
            /* consume comma */
            consume(p, TOK_COMMA);

            /* check for trailing comma */
            tok = current_token(p);
            if (tok && tok->type == TOK_RBRACE) {
                /* trailing comma: just consume '}' and finish */
                Token *rbrace = consume(p, TOK_RBRACE);
                init->span = span_join(&start_span, &rbrace->span);
                return init;
            }

            /* otherwise continue to parse the next element */
            continue;
        }
 else if (tok->type == TOK_RBRACE) {
            Token *rbrace = consume(p, TOK_RBRACE);
            /* final span covers from the original '{' to this '}' */
            init->span = span_join(&start_span, &rbrace->span);
            return init;
        } else {
            /* invalid token */
            err->use_prev_token = true;
            create_parse_error(err, p, "expected ',' or '}' in initializer list", tok);
            return NULL;
        }
    }

    /* unreachable */
    return init;
}


//<ParamList> ::= <Param> { COMMA <Param> }
//<Param>     ::= IDENTIFIER COLON <Type>
int parse_parameter_list(Parser *p, AstNode *func_decl, ParseError *err) {
    if (!func_decl || func_decl->node_type != AST_FUNCTION_DECLARATION) {
        if (err) create_parse_error(err, p, "internal error: parse_parameter_list called with non-function-declaration node", current_token(p));
        return 0;
    }

    Token *tok = current_token(p);
    if (!tok) {
        if (err) create_parse_error(err, p, "unexpected end of input in parameter list", NULL);
        return 0;
    }

    /* empty parameter list: caller consumes the ')' */
    if (tok->type == TOK_RPAREN) {
        return 1; // nothing to parse
    }

    /* parse parameters */
    while (1) {
        /* refresh token at loop start */
        tok = current_token(p);
        if (!tok) {
            if (err) create_parse_error(err, p, "unexpected end of input in parameter list", NULL);
            return 0;
        }

        if (tok->type != TOK_IDENTIFIER) {
            if (err) create_parse_error(err, p, "expected identifier for parameter name", NULL);
            return 0;
        }

        /* remember span of the identifier (start of param) */
        Span start_span = tok->span;

        /* create and fill a new parameter node */
        AstNode *param = ast_create_node(AST_PARAM, p->arena, p->filename);
        if (!param) {
            if (err) create_parse_error(err, p, "out of memory creating parameter node", NULL);
            return 0;
        }

        param->data.param.name_idx = tok->record ? tok->record->entry->dense_index : -1;
        consume(p, TOK_IDENTIFIER);

        if (!consume(p, TOK_COLON)) {
            if (err) create_parse_error(err, p, "expected ':' after parameter name", current_token(p));
            return 0;
        }

        param->data.param.type = parse_type(p, err);
        if (!param->data.param.type) {
            return 0; /* parse_type set err */
        }

        /* compute param span from identifier to end of type */
        param->span = span_join(&start_span, &param->data.param.type->data.ast_type.span);

        /* push the parameter into the function declaration */
        if (dynarray_push_value(func_decl->data.function_declaration.params, &param) != 0) {
            if (err) create_parse_error(err, p, "out of memory pushing parameter", NULL);
            return 0;
        }

        /* look ahead: either ')' (done) or ',' (another param) */
        tok = current_token(p);
        if (!tok) {
            if (err) create_parse_error(err, p, "unexpected end of input in parameter list", NULL);
            return 0;
        }

        if (tok->type == TOK_RPAREN) {
            /* done, caller will consume ')' */
            break;
        }

        if (!consume(p, TOK_COMMA)) {
            if (err) create_parse_error(err, p, "expected ',' or ')' after parameter", tok);
            return 0;
        }

        // Support trailing comma: if next is ')', we are done
        tok = current_token(p);
        if (tok && tok->type == TOK_RPAREN) break;

        /* loop to parse next parameter (tok will be refreshed at top) */
    }

    return 1; // successfully parsed parameter list
}


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
    // parenthesis are optional around the condition; parse_expression already handles that
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

    // parenthesis are optional around the condition parse_expression already handles that
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
    // Initialize for_node->data.for_statement fields to NULL...

    if (!consume(p, TOK_LPAREN)) {
        if (err) create_parse_error(err, p, "expected '(' after 'for'", current_token(p));
        return NULL;
    }

    // 1. Init Clause (Declaration or Expression)
    if (current_token(p)->type != TOK_SEMICOLON) {
        // Check if it's a variable declaration (starts with Identifier then Colon)
        // Or just parse as statement which handles both Decl and ExprStmt
        AstNode *init = parse_statement(p, err); 
        if (!init) return NULL;
        for_node->data.for_statement.init = init;
        // Note: parse_statement usually consumes the semicolon. 
        // If your BNF expects a semicolon *separator*, ensure parse_statement didn't already eat it
        // or assumes the init clause includes the semicolon (which <VariableDeclarationStmt> does).
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

    // Join spans...
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
    return expr;
}