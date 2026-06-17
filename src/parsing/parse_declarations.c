#include "parse_declarations.h"
#include "parse_expressions.h"
#include "parse_types.h"
#include "parse_statements.h"
#include "parser.h"
#include "dynamic_array.h"
#include "lexer.h"
#include "ast.h"
#include <string.h>

static bool parse_program_decls(Parser *p, AstNode *program, ParseError *err, Span *first_span, Span *last_span, bool *have_any) {
    for (;;) {
        /* If there's an error already recorded, stop parsing.
           parse_declaration sets err on failure. */
        if (err && err->message) return false;

        AstNode *decl = parse_declaration(p, err);
        if (!decl) break; /* no (more) declarations or parse error produced */

        if (dynarray_push_value(program->data.program.decls, &decl) != 0) {
            if (err) create_parse_error(err, p, "out of memory adding declaration to program", NULL);
            return false;
        }

        if (!*have_any) {
            *first_span = decl->span;
            *have_any = true;
        }
        *last_span = decl->span;
    }
    return true;
}

static void set_program_span(Parser *p, AstNode *program, bool have_any, Span first_span, Span last_span) {
    if (have_any) {
        program->span = span_join(&first_span, &last_span);
    } else if (p->tokens && p->tokens->count > 0) {
        Token *first = (Token*)dynarray_get(p->tokens, 0);
        program->span = first->span;
    } else {
        program->span = (Span){0,0,0,0};
    }
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

    if (!parse_program_decls(p, program, err, &first_span, &last_span, &have_any)) {
        return NULL;
    }

    /* If parse_declaration produced an error earlier, we've already returned. Now check for trailing tokens. */
    if (p->current < p->end) {
        if (err && !err->message) { /* Only create error if there isn't one already */
            Token *t = current_token(p);
            create_parse_error(err, p, "trailing tokens after program end", t);
        }
        return NULL;
    }

    set_program_span(p, program, have_any, first_span, last_span);
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
            decl = parse_import_declaration(p, err);
            if (decl) {
                decl->data.import_declaration.is_pub = is_pub;
            }
            return decl;
        case TOK_ALIAS:

             if (is_pub) {
                if (err) create_parse_error(err, p, "public aliases are not supported", current);
                return NULL;
            }
            decl = parse_alias_declaration(p, err);
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

AstNode *parse_alias_declaration(Parser *p, ParseError *err) {
    Token *alias_tok = consume(p, TOK_ALIAS);
    if (!alias_tok) return NULL;

    AstNode *decl = new_node_or_err(p, AST_ALIAS_DECLARATION, err, "out of memory creating alias node");
    if (!decl) return NULL;

    Token *name_tok = consume(p, TOK_IDENTIFIER);
    if (!name_tok) {
        if (err) create_parse_error(err, p, "expected alias name identifier", current_token(p));
        return NULL;
    }
    decl->data.alias_declaration.alias_name = name_tok->record;

    if (!consume(p, TOK_ASSIGN)) {
        if (err) create_parse_error(err, p, "expected '=' after alias name", current_token(p));
        return NULL;
    }

    // Parse the target (e.g. std.io.println)
    AstNode *target = parse_postfix(p, err);
    if (!target) return NULL;

    decl->data.alias_declaration.target = target;

    Token *semi = consume(p, TOK_SEMICOLON);
    if (!semi) {
        if (err) create_parse_error(err, p, "expected ';' after alias declaration", current_token(p));
        return NULL;
    }

    decl->span = span_join(&alias_tok->span, &semi->span);
    return decl;
}

AstNode *parse_import_declaration(Parser *p, ParseError *err) {
    if (!p) return NULL;
    Token *import_tok = consume(p, TOK_IMPORT);
    if (!import_tok) return NULL;

    AstNode *decl = new_node_or_err(p, AST_IMPORT_DECLARATION, err, "out of memory creating import declaration node");
    if (!decl) return NULL;

    // Initialize fields
    decl->data.import_declaration.module_path = alloc_dynarray(p, err, sizeof(InternResult*), 4, "out of memory creating module path array");
    if (!decl->data.import_declaration.module_path) return NULL;
    
    decl->data.import_declaration.module_alias = NULL;
    decl->data.import_declaration.specific_symbols = NULL;
    decl->data.import_declaration.leading_dots = 0;
    decl->data.import_declaration.is_root_relative = false;

    // 1. Prefix: leading dots or '@'
    while (current_token(p) && current_token(p)->type == TOK_DOT) {
        consume(p, TOK_DOT);
        decl->data.import_declaration.leading_dots++;
    }

    if (decl->data.import_declaration.leading_dots == 0) {
        if (parser_match(p, TOK_AT)) {
            decl->data.import_declaration.is_root_relative = true;
        }
    }

    // 2. Module path (dotted identifiers: std.io)
    do {
        Token *part = consume(p, TOK_IDENTIFIER);
        if (!part) {
            if (err) create_parse_error(err, p, "expected identifier in module path", current_token(p));
            return NULL;
        }
        dynarray_push_value(decl->data.import_declaration.module_path, &part->record);
    } while (parser_match(p, TOK_DOT));

    // 3. Specific symbols: import std.io { println };
    if (parser_match(p, TOK_LBRACE)) {
        decl->data.import_declaration.specific_symbols = alloc_dynarray(p, err, sizeof(ImportSymbol*), 4, "out of memory creating specific symbols array");
        if (!decl->data.import_declaration.specific_symbols) return NULL;

        while (current_token(p) && current_token(p)->type != TOK_RBRACE && current_token(p)->type != TOK_EOF) {
            Token *sym_tok = consume(p, TOK_IDENTIFIER);
            if (!sym_tok) {
                if (err) create_parse_error(err, p, "expected identifier in import list", current_token(p));
                return NULL;
            }

            ImportSymbol *sym = arena_alloc(p->arena, sizeof(ImportSymbol));
            sym->original_name = sym_tok->record;
            sym->alias_name = NULL;

            if (parser_match(p, TOK_ALIAS)) {
                Token *alias_tok = consume(p, TOK_IDENTIFIER);
                if (!alias_tok) {
                    if (err) create_parse_error(err, p, "expected identifier after 'alias'", current_token(p));
                    return NULL;
                }
                sym->alias_name = alias_tok->record;
            }

            dynarray_push_value(decl->data.import_declaration.specific_symbols, &sym);

            if (!parser_match(p, TOK_COMMA)) break;
        }

        if (!consume(p, TOK_RBRACE)) {
            if (err) create_parse_error(err, p, "expected '}' after import list", current_token(p));
            return NULL;
        }
    } 
    // 3. Optional module alias: import std.io alias io; 
    // (Only if NOT importing specific symbols)
    else if (parser_match(p, TOK_ALIAS)) {
        Token *alias_tok = consume(p, TOK_IDENTIFIER);
        if (!alias_tok) {
            if (err) create_parse_error(err, p, "expected identifier after 'alias'", current_token(p));
            return NULL;
        }
        decl->data.import_declaration.module_alias = alias_tok->record;
    }

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
    if (!struct_kw) return NULL; 
    
    AstNode *decl = new_node_or_err(p, AST_STRUCT_DECLARATION, err, "out of memory creating struct declaration node");
    if (!decl) return NULL;

    Token *name_tok = consume(p, TOK_IDENTIFIER);
    if (!name_tok) {
        if (err) create_parse_error(err, p, "expected identifier after 'struct'", current_token(p));
        return NULL;
    }
    decl->data.struct_declaration.intern_result = name_tok->record;

    DynArray *type_params = NULL;
    if (current_token(p) && current_token(p)->type == TOK_LT) {
        consume(p, TOK_LT);
        type_params = arena_alloc(p->arena, sizeof(DynArray));
        if (!type_params) {
            if (err) create_parse_error(err, p, "out of memory for type params", NULL);
            return NULL;
        }
        dynarray_init_in_arena(type_params, p->arena, sizeof(InternResult*), 2);

        do {
            Token *tp = consume(p, TOK_IDENTIFIER);
            if (!tp) { create_parse_error(err, p, "expected type parameter name", current_token(p)); return NULL; }
            dynarray_push_value(type_params, &tp->record);
        } while (parser_match(p, TOK_COMMA));

        if (!consume(p, TOK_GT)) { create_parse_error(err, p, "expected '>' after type parameters", current_token(p)); return NULL; }
    }
    decl->data.struct_declaration.type_params = type_params;

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
    
    if (parser_match(p, TOK_ASSIGN)) {
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

    AstNode *target_type = NULL;
    Token *dot_tok = current_token(p);

    if (dot_tok && dot_tok->type == TOK_DOT) {
        consume(p, TOK_DOT);
        
        // The first token was actually the target struct name
        target_type = new_node_or_err(p, AST_IDENTIFIER, err, "out of memory creating target type identifier node");
        if (!target_type) return NULL;

        target_type->data.identifier.intern_result = name_tok->record;
        target_type->span = name_tok->span;
        
        // Now consume the actual method name
        name_tok = consume(p, TOK_IDENTIFIER);
        if (!name_tok) { create_parse_error(err, p, "expected method name after '.'", current_token(p)); return NULL; }
    }

    func_decl->data.function_declaration.intern_result = name_tok->record;
    func_decl->data.function_declaration.target_type_node = target_type;

    DynArray *type_params = NULL;
    if (current_token(p) && current_token(p)->type == TOK_LT) {
        consume(p, TOK_LT);
        type_params = arena_alloc(p->arena, sizeof(DynArray));
        if (!type_params) {
            if (err) create_parse_error(err, p, "out of memory for type params", NULL);
            return NULL;
        }
        dynarray_init_in_arena(type_params, p->arena, sizeof(InternResult*), 2);

        do {
            Token *tp = consume(p, TOK_IDENTIFIER);
            if (!tp) { create_parse_error(err, p, "expected type parameter name", current_token(p)); return NULL; }
            dynarray_push_value(type_params, &tp->record);
        } while (parser_match(p, TOK_COMMA));

        if (!consume(p, TOK_GT)) { create_parse_error(err, p, "expected '>' after type parameters", current_token(p)); return NULL; }
    }
    func_decl->data.function_declaration.type_params = type_params;

    /* parameters */
    if (!consume(p, TOK_LPAREN)) { create_parse_error(err, p, "expected '(' after function name", current_token(p)); return NULL; }
    
    // parse parameters
    if (!parse_parameter_list(p, func_decl, err)) {
        return NULL; 
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
    }

    return 1; // successfully parsed parameter list
}
