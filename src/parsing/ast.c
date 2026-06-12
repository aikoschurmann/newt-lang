#include "ast.h"
#include "type_print.h"
#include <stdio.h>

/* Helper functions to convert enums to strings */
static const char *node_type_to_string(AstNodeType type) {
    switch (type) {
        case AST_PROGRAM: return "Program";
        case AST_VARIABLE_DECLARATION: return "VariableDeclaration";
        case AST_FUNCTION_DECLARATION: return "FunctionDeclaration";
        case AST_PARAM: return "Parameter";
        case AST_STRUCT_DECLARATION: return "StructDeclaration";
        case AST_IMPORT_DECLARATION: return "ImportDeclaration";
        case AST_INTRINSIC: return "IntrinsicCall";
        case AST_BLOCK: return "Block";
        case AST_IF_STATEMENT: return "IfStatement";
        case AST_WHILE_STATEMENT: return "WhileStatement";
        case AST_FOR_STATEMENT: return "ForStatement";
        case AST_RETURN_STATEMENT: return "ReturnStatement";
        case AST_BREAK_STATEMENT: return "BreakStatement";
        case AST_CONTINUE_STATEMENT: return "ContinueStatement";
        case AST_EXPR_STATEMENT: return "ExpressionStatement";
        case AST_LITERAL: return "Literal";
        case AST_IDENTIFIER: return "Identifier";
        case AST_BINARY_EXPR: return "BinaryExpression";
        case AST_UNARY_EXPR: return "UnaryExpression";
        case AST_POSTFIX_EXPR: return "PostfixExpression";
        case AST_ASSIGNMENT_EXPR: return "AssignmentExpression";
        case AST_CALL_EXPR: return "CallExpression";
        case AST_SUBSCRIPT_EXPR: return "SubscriptExpression";
        case AST_MEMBER_EXPR: return "MemberExpression";
        case AST_STRUCT_LITERAL: return "StructLiteral";
        case AST_CAST: return "CastExpression";
        case AST_TYPE: return "Type";
        case AST_INITIALIZER_LIST: return "InitializerList";
        default: return "Unknown";
    }
}

static const char *type_kind_to_string(AstTypeKind kind) {
    switch (kind) {
        case AST_TYPE_PRIMITIVE: return "PrimitiveType";
        case AST_TYPE_PTR: return "PointerType";
        case AST_TYPE_ARRAY: return "ArrayType";
        case AST_TYPE_FUNC: return "FunctionType";
        default: return "UnknownType";
    }
}

static const char *literal_type_to_string(LiteralType type) {
    switch (type) {
        case INT_LITERAL: return "Integer";
        case FLOAT_LITERAL: return "Float";
        case BOOL_LITERAL: return "Boolean";
        case STRING_LITERAL: return "String";
        case CHAR_LITERAL: return "Character";
        default: return "UnknownLiteral";
    }
}

/* Helper function to print a string with escape sequences visible */
static void print_escaped_string(const char *str) {
    if (!str) {
        printf("\"\"");
        return;
    }
    
    printf("\"");
    for (const char *p = str; *p; p++) {
        switch (*p) {
            case '\n': printf("\\n"); break;
            case '\t': printf("\\t"); break;
            case '\r': printf("\\r"); break;
            case '\\': printf("\\\\"); break;
            case '"': printf("\\\""); break;
            case '\0': printf("\\0"); break;
            default:
                if (*p >= 32 && *p <= 126) {
                    printf("%c", *p);  // Printable ASCII
                } else {
                    printf("\\x%02x", (unsigned char)*p);  // Non-printable as hex
                }
                break;
        }
    }
    printf("\"");
}

/* Helper function to print a character with escape sequences visible */
static void print_escaped_char(char c) {
    printf("'");
    switch (c) {
        case '\n': printf("\\n"); break;
        case '\t': printf("\\t"); break;
        case '\r': printf("\\r"); break;
        case '\\': printf("\\\\"); break;
        case '\'': printf("\\'"); break;
        case '\0': printf("\\0"); break;
        default:
            if (c >= 32 && c <= 126) {
                printf("%c", c);  // Printable ASCII
            } else {
                printf("\\x%02x", (unsigned char)c);  // Non-printable as hex
            }
            break;
    }
    printf("'");
}

AstNode *ast_create_node(AstNodeType type, Arena *arena) {
    if (!arena) return NULL;

    AstNode *node = (AstNode*)arena_calloc(arena, sizeof(AstNode));
    if (!node) return NULL;

    node->node_type = type;
    /* arena_calloc zeroed the rest (span fields = 0, union memory = 0,
       is_const_expr = 0, const_value = zero). */
    return node;
}

/* Return 1 if node is a syntactic lvalue (can appear on left side of =),
 * 0 otherwise.
 */
int is_lvalue_node(AstNode *node) {
    if (!node) return 0;

    switch (node->node_type) {
        case AST_IDENTIFIER:
            /* Simple variable name is an lvalue. */
            return 1;

        case AST_SUBSCRIPT_EXPR:
            /* a[b] yields an lvalue (array element). */
            return 1;

        case AST_MEMBER_EXPR:
            /* a.b yields an lvalue (member access). */
            return 1;

        case AST_UNARY_EXPR:
            /* Unary '*' (dereference) produces an lvalue: *(p) = ... */
            if (node->data.unary_expr.op == OP_DEREF) return 1;
            /* '&' (address-of), '+', '-', '!' are not lvalues. */
            return 0;

        case AST_POSTFIX_EXPR: {
            /* Postfix expressions like 'a++' or 'a--' are rvalues */
            OpKind k = node->data.postfix_expr.op;
            if(k == OP_POST_INC || k == OP_POST_DEC) {
                return 0; // postfix increment/decrement is not an lvalue
            }
            return 0; // not implemented yet
        }

        default:
            return 0;
    }
}

int is_assignment_op(TokenType type) {
    switch (type) {
        case TOK_ASSIGN:
        case TOK_PLUS_EQ:
        case TOK_MINUS_EQ:
        case TOK_STAR_EQ:
        case TOK_SLASH_EQ:
        case TOK_PERCENT_EQ:
            return 1;
        default:
            return 0;
    }
}


// Track which ancestor levels are "last children" for proper tree rendering
#define MAX_TREE_DEPTH 64
static int ancestor_is_last[MAX_TREE_DEPTH];

static void print_tree_prefix_tracked(int depth, int is_last) {
    if (depth == 0) return;
    
    for (int i = 0; i < depth - 1; ++i) {
        if (ancestor_is_last[i]) {
            printf("    ");  // 4 spaces for levels where ancestor was last
        } else {
            printf("│   ");  // tree continuation
        }
    }
    
    if (is_last) {
        printf("└── ");
    } else {
        printf("├── ");
    }
}

static void print_tree_prefix_with_parent_info(int depth, int is_last, int *parent_is_last, int parent_depth) {
    if (depth == 0) return;
    
    for (int i = 0; i < depth - 1; ++i) {
        if (i < parent_depth && parent_is_last && parent_is_last[i]) {
            printf("    ");  // 4 spaces for levels where parent was last
        } else {
            printf("│   ");  // tree continuation
        }
    }
    
    if (is_last) {
        printf("└── ");
    } else {
        printf("├── ");
    }
}

static void print_tree_prefix(int depth, int is_last) {
    print_tree_prefix_tracked(depth, is_last);
}

static void print_tree_simple(int depth) {
    for (int i = 0; i < depth; ++i) {
        printf("│   ");
    }
}

/* Print a string representation of an operator */
static const char *op_to_string(OpKind op) {
    switch (op) {
        case OP_ADD: return "+";
        case OP_SUB: return "-";
        case OP_MUL: return "*";
        case OP_DIV: return "/";
        case OP_MOD: return "%";
        case OP_EQ: return "==";
        case OP_NEQ: return "!=";
        case OP_LT: return "<";
        case OP_GT: return ">";
        case OP_LE: return "<=";
        case OP_GE: return ">=";
        case OP_AND: return "&&";
        case OP_OR: return "||";
        case OP_NOT: return "!";
        case OP_ASSIGN: return "=";
        case OP_PLUS_EQ: return "+=";
        case OP_MINUS_EQ: return "-=";
        case OP_DEREF: return "*";
        case OP_ADRESS: return "&";
        case OP_POST_INC: return "++";
        case OP_POST_DEC: return "--";
        case OP_PRE_INC: return "++";
        case OP_PRE_DEC: return "--";
        default: return "?";
    }
}

void print_ast(AstNode *node, int depth, DenseArenaInterner *keywords, DenseArenaInterner *identifiers, DenseArenaInterner *strings) {
    print_ast_with_prefix(node, depth, 1, keywords, identifiers, strings);
}

void print_ast_with_prefix(AstNode *node, int depth, int is_last, DenseArenaInterner *keywords, DenseArenaInterner *identifiers, DenseArenaInterner *strings) {
    if (!node) {
        print_tree_prefix(depth, is_last);
        printf("(null)\n");
        return;
    }

    print_tree_prefix(depth, is_last);
    printf("%s", node_type_to_string(node->node_type));
    // Add span info inline
    if (node->span.start_line > 0) {
        printf(" [%zu:%zu-%zu:%zu]", 
               node->span.start_line, node->span.start_col, 
               node->span.end_line, node->span.end_col);
    }
    // Add type info if available
    if (node->type) {
        printf(" \033[36m<");
        type_print(stdout, node->type);
        printf(">\033[0m");
    }
    // Add const info if relevant
    if (node->is_const_expr) {
        printf(" \033[33m(const:");
        switch (node->const_value.type) {
            case INT_LITERAL:
                printf("%lld", node->const_value.value.int_val);
                break;
            case FLOAT_LITERAL:
                printf("%f", node->const_value.value.float_val);
                break;
            case BOOL_LITERAL:
                printf("%s", node->const_value.value.bool_val ? "true" : "false");
                break;
            default:
                printf("...");
                break;
        }
        printf(")\033[0m");
    }
    printf("\n");
    
    // Print node-specific data as child nodes
    switch (node->node_type) {
        case AST_PROGRAM:
            if (node->data.program.decls && node->data.program.decls->count > 0) {
                for (size_t i = 0; i < node->data.program.decls->count; ++i) {
                    AstNode *decl = *(AstNode**)dynarray_get(node->data.program.decls, i);
                    print_ast_with_prefix(decl, depth + 1, i == node->data.program.decls->count - 1, keywords, identifiers, strings);
                }
            }
            break;

        case AST_IMPORT_DECLARATION:
            print_tree_prefix(depth + 1, 1);
            printf("module: ");
            if (node->data.import_declaration.module_name && identifiers) {
                const char *name = interner_get_cstr(identifiers, node->data.import_declaration.module_name->entry->dense_index);
                printf("'%s'", name ? name : "?");
            } else {
                printf("(unknown)");
            }
            printf("\n");
            break;
            
        case AST_INTRINSIC:
            print_tree_prefix(depth + 1, 0);
            printf("intrinsic: ");
            if (node->data.intrinsic.kind == INTRINSIC_ALLOC) printf("@alloc\n");
            else if (node->data.intrinsic.kind == INTRINSIC_FREE) printf("@free\n");
            else printf("unknown\n");
            
            if (node->data.intrinsic.args && node->data.intrinsic.args->count > 0) {
                print_tree_prefix(depth + 1, 1);
                printf("arguments:\n");
                for (size_t i = 0; i < node->data.intrinsic.args->count; ++i) {
                    AstNode *arg = *(AstNode**)dynarray_get(node->data.intrinsic.args, i);
                    print_ast_with_prefix(arg, depth + 2, i == node->data.intrinsic.args->count - 1, keywords, identifiers, strings);
                }
            } else {
                print_tree_prefix(depth + 1, 1);
                printf("arguments: (none)\n");
            }
            break;

        case AST_STRUCT_DECLARATION: {
            print_tree_prefix(depth + 1, 0);
            printf("name: ");
            if (node->data.struct_declaration.intern_result &&
                node->data.struct_declaration.intern_result->entry && identifiers) {
                const char *name = interner_get_cstr(identifiers, node->data.struct_declaration.intern_result->entry->dense_index);
                printf("'%s'", name ? name : "?");
            } else {
                printf("(none)");
            }
            printf("\n");

            if (node->data.struct_declaration.fields && node->data.struct_declaration.fields->count > 0) {
                print_tree_prefix(depth + 1, 1);
                printf("fields:\n");
                for (size_t i = 0; i < node->data.struct_declaration.fields->count; ++i) {
                    AstFieldDecl *field = (AstFieldDecl*)dynarray_get(node->data.struct_declaration.fields, i);
                    
                    print_tree_prefix(depth + 2, i == node->data.struct_declaration.fields->count - 1);
                    printf("Field '");
                    if (field->name && field->name->entry && identifiers) {
                        const char *fname = interner_get_cstr(identifiers, field->name->entry->dense_index);
                        printf("%s", fname ? fname : "?");
                    } else {
                        printf("?");
                    }
                    printf("':\n");
                    print_ast_with_prefix(field->type, depth + 3, 1, keywords, identifiers, strings);
                }
            }
            break;
        }

        case AST_STRUCT_LITERAL: {
            print_tree_prefix(depth + 1, 0);
            printf("name: ");
            if (node->data.struct_literal.intern_result &&
                node->data.struct_literal.intern_result->entry && identifiers) {
                const char *name = interner_get_cstr(identifiers, node->data.struct_literal.intern_result->entry->dense_index);
                printf("'%s'", name ? name : "?");
            } else {
                printf("(none)");
            }
            printf("\n");

            if (node->data.struct_literal.fields && node->data.struct_literal.fields->count > 0) {
                print_tree_prefix(depth + 1, 1);
                printf("fields:\n");
                for (size_t i = 0; i < node->data.struct_literal.fields->count; ++i) {
                    AstFieldInit *init = (AstFieldInit*)dynarray_get(node->data.struct_literal.fields, i);
                    
                    print_tree_prefix(depth + 2, i == node->data.struct_literal.fields->count - 1);
                    printf("Field '");
                    if (init->name && init->name->entry && identifiers) {
                        const char *fname = interner_get_cstr(identifiers, init->name->entry->dense_index);
                        printf("%s", fname ? fname : "?");
                    } else {
                        printf("?");
                    }
                    printf("':\n");
                    print_ast_with_prefix(init->expr, depth + 3, 1, keywords, identifiers, strings);
                }
            }
            break;
        }

        case AST_VARIABLE_DECLARATION: {
            int has_type = node->data.variable_declaration.type != NULL;
            int has_init = node->data.variable_declaration.initializer != NULL;
            int child_count = 0;
            if (has_type) child_count++;
            if (has_init) child_count++;
            
            // Print name info
            print_tree_prefix(depth + 1, child_count == 0);
            printf("name: ");
            if (node->data.variable_declaration.intern_result && 
                node->data.variable_declaration.intern_result->entry && identifiers) {
                const char *name = interner_get_cstr(identifiers, node->data.variable_declaration.intern_result->entry->dense_index);
                printf("'%s'", name ? name : "?");
            } else {
                printf("(none)");
            }
            if (node->data.variable_declaration.is_const) {
                printf(" [const]");
            }
            printf("\n");
            
            int current_child = 0;
            
            // Print type
            if (has_type) {
                current_child++;
                print_tree_prefix(depth + 1, current_child == child_count);
                printf("type:\n");
                print_ast_with_prefix(node->data.variable_declaration.type, depth + 2, 1, keywords, identifiers, strings);
            }
            
            // Print initializer
            if (has_init) {
                current_child++;
                print_tree_prefix(depth + 1, current_child == child_count);
                printf("initializer:\n");
                print_ast_with_prefix(node->data.variable_declaration.initializer, depth + 2, 1, keywords, identifiers, strings);
            }
            break;
        }

        case AST_FUNCTION_DECLARATION: {
            // Print name
            print_tree_prefix(depth + 1, 0);
            printf("name: ");
            if (node->data.function_declaration.intern_result && 
                node->data.function_declaration.intern_result->entry && identifiers) {
                const char *name = interner_get_cstr(identifiers, node->data.function_declaration.intern_result->entry->dense_index);
                printf("'%s'", name ? name : "?");
            } else {
                printf("(none)");
            }
            printf("\n");
            
            int has_body = node->data.function_declaration.body != NULL;
            int has_params = node->data.function_declaration.params && node->data.function_declaration.params->count > 0;
            
            // Print return type
            if (node->data.function_declaration.return_type) {
                print_tree_prefix(depth + 1, !(has_params || has_body));
                printf("return_type:\n");
                print_ast_with_prefix(node->data.function_declaration.return_type, depth + 2, !(has_params || has_body), keywords, identifiers, strings);
            }
            
            // Print parameters
            if (has_params) {
                print_tree_prefix(depth + 1, !has_body);
                printf("parameters:\n");
                for (size_t i = 0; i < node->data.function_declaration.params->count; ++i) {
                    AstNode *param = *(AstNode**)dynarray_get(node->data.function_declaration.params, i);
                    print_ast_with_prefix(param, depth + 2, i == node->data.function_declaration.params->count - 1, keywords, identifiers, strings);
                }
            }
            
            // Print body
            if (has_body) {
                print_tree_prefix(depth + 1, 1);
                printf("body:\n");
                print_ast_with_prefix(node->data.function_declaration.body, depth + 2, 1, keywords, identifiers, strings);
            }
            break;
        }

        case AST_PARAM: {
            print_tree_prefix(depth + 1, 0);
            printf("name: ");
            if (node->data.param.name_idx >= 0 && identifiers) {
                const char *name = interner_get_cstr(identifiers, node->data.param.name_idx);
                printf("'%s'", name ? name : "?");
            } else {
                printf("(anonymous)");
            }
            printf("\n");
            
            if (node->data.param.type) {
                print_tree_prefix(depth + 1, 1);
                printf("type:\n");
                print_ast_with_prefix(node->data.param.type, depth + 2, 1, keywords, identifiers, strings);
            }
            break;
        }

        case AST_BLOCK:
            if (node->data.block.statements && node->data.block.statements->count > 0) {
                for (size_t i = 0; i < node->data.block.statements->count; ++i) {
                    AstNode *stmt = *(AstNode**)dynarray_get(node->data.block.statements, i);
                    print_ast_with_prefix(stmt, depth + 1, i == node->data.block.statements->count - 1, keywords, identifiers, strings);
                }
            }
            break;

        case AST_IF_STATEMENT: {
            int has_else = node->data.if_statement.else_branch != NULL;
            
            if (node->data.if_statement.condition) {
                print_tree_prefix(depth + 1, 0);
                printf("condition:\n");
                print_ast_with_prefix(node->data.if_statement.condition, depth + 2, 0, keywords, identifiers, strings);
            }
            
            if (node->data.if_statement.then_branch) {
                print_tree_prefix(depth + 1, !has_else);
                printf("then:\n");
                print_ast_with_prefix(node->data.if_statement.then_branch, depth + 2, !has_else, keywords, identifiers, strings);
            }
            
            if (has_else) {
                print_tree_prefix(depth + 1, 1);
                printf("else:\n");
                print_ast_with_prefix(node->data.if_statement.else_branch, depth + 2, 1, keywords, identifiers, strings);
            }
            break;
        }

        case AST_WHILE_STATEMENT:
            if (node->data.while_statement.condition) {
                print_tree_prefix(depth + 1, 0);
                printf("condition:\n");
                print_ast_with_prefix(node->data.while_statement.condition, depth + 2, 0, keywords, identifiers, strings);
            }
            if (node->data.while_statement.body) {
                print_tree_prefix(depth + 1, 1);
                printf("body:\n");
                print_ast_with_prefix(node->data.while_statement.body, depth + 2, 1, keywords, identifiers, strings);
            }
            break;

        case AST_FOR_STATEMENT: {
            int parts = 0;
            if (node->data.for_statement.init) parts++;
            if (node->data.for_statement.condition) parts++;
            if (node->data.for_statement.post) parts++;
            if (node->data.for_statement.body) parts++;
            
            int current = 0;
            
            if (node->data.for_statement.init) {
                current++;
                print_tree_prefix(depth + 1, current == parts);
                printf("init:\n");
                print_ast_with_prefix(node->data.for_statement.init, depth + 2, current == parts, keywords, identifiers, strings);
            }
            if (node->data.for_statement.condition) {
                current++;
                print_tree_prefix(depth + 1, current == parts);
                printf("condition:\n");
                print_ast_with_prefix(node->data.for_statement.condition, depth + 2, current == parts, keywords, identifiers, strings);
            }
            if (node->data.for_statement.post) {
                current++;
                print_tree_prefix(depth + 1, current == parts);
                printf("post:\n");
                print_ast_with_prefix(node->data.for_statement.post, depth + 2, current == parts, keywords, identifiers, strings);
            }
            if (node->data.for_statement.body) {
                current++;
                print_tree_prefix(depth + 1, 1);
                printf("body:\n");
                print_ast_with_prefix(node->data.for_statement.body, depth + 2, 1, keywords, identifiers, strings);
            }
            break;
        }

        case AST_RETURN_STATEMENT:
            if (node->data.return_statement.expression) {
                print_tree_prefix(depth + 1, 1);
                printf("expression:\n");
                print_ast_with_prefix(node->data.return_statement.expression, depth + 2, 1, keywords, identifiers, strings);
            }
            break;

        case AST_EXPR_STATEMENT:
            if (node->data.expr_statement.expression) {
                print_tree_prefix(depth + 1, 1);
                printf("expression:\n");
                print_ast_with_prefix(node->data.expr_statement.expression, depth + 2, 1, keywords, identifiers, strings);
            }
            break;

        case AST_LITERAL:
            print_tree_prefix(depth + 1, 1);
            printf("value: ");
            switch (node->data.literal.type) {
                case INT_LITERAL:
                    printf("%lld", node->data.literal.value.int_val);
                    break;
                case FLOAT_LITERAL:
                    printf("%f", node->data.literal.value.float_val);
                    break;
                case BOOL_LITERAL:
                    printf("%s", node->data.literal.value.bool_val ? "true" : "false");
                    break;
                case STRING_LITERAL: {
                    if (node->data.literal.value.string_val && 
                        node->data.literal.value.string_val->entry && strings) {
                        const char *str = interner_get_cstr(strings, node->data.literal.value.string_val->entry->dense_index);
                        print_escaped_string(str);
                    } else {
                        printf("\"\"");
                    }
                    break;
                }
                case CHAR_LITERAL:
                    print_escaped_char(node->data.literal.value.char_val);
                    break;
                default:
                    printf("?");
                    break;
            }
            printf(" (%s)\n", literal_type_to_string(node->data.literal.type));
            break;

        case AST_IDENTIFIER:
            print_tree_prefix(depth + 1, 1);
            printf("name: ");
            if (node->data.identifier.intern_result && 
                node->data.identifier.intern_result->entry && identifiers) {
                const char *name = interner_get_cstr(identifiers, node->data.identifier.intern_result->entry->dense_index);
                printf("'%s'", name ? name : "?");
            } else {
                printf("(unknown)");
            }
            printf("\n");
            break;

        case AST_BINARY_EXPR:
            print_tree_prefix(depth + 1, 0);
            printf("operator: '%s'\n", op_to_string(node->data.binary_expr.op));
            
            if (node->data.binary_expr.left) {
                print_tree_prefix(depth + 1, !node->data.binary_expr.right);
                printf("left:\n");
                print_ast_with_prefix(node->data.binary_expr.left, depth + 2, !node->data.binary_expr.right, keywords, identifiers, strings);
            }
            if (node->data.binary_expr.right) {
                print_tree_prefix(depth + 1, 1);
                printf("right:\n");
                print_ast_with_prefix(node->data.binary_expr.right, depth + 2, 1, keywords, identifiers, strings);
            }
            break;

        case AST_UNARY_EXPR:
            print_tree_prefix(depth + 1, 0);
            printf("operator: '%s'\n", op_to_string(node->data.unary_expr.op));
            
            if (node->data.unary_expr.expr) {
                print_tree_prefix(depth + 1, 1);
                printf("expression:\n");
                print_ast_with_prefix(node->data.unary_expr.expr, depth + 2, 1, keywords, identifiers, strings);
            }
            break;

        case AST_POSTFIX_EXPR:
            print_tree_prefix(depth + 1, 0);
            printf("operator: '%s'\n", op_to_string(node->data.postfix_expr.op));
            
            if (node->data.postfix_expr.expr) {
                print_tree_prefix(depth + 1, 1);
                printf("expression:\n");
                print_ast_with_prefix(node->data.postfix_expr.expr, depth + 2, 1, keywords, identifiers, strings);
            }
            break;

        case AST_ASSIGNMENT_EXPR:
            print_tree_prefix(depth + 1, 0);
            printf("operator: '%s'\n", op_to_string(node->data.assignment_expr.op));
            
            if (node->data.assignment_expr.lvalue) {
                print_tree_prefix(depth + 1, 0);
                printf("lvalue:\n");
                print_ast_with_prefix(node->data.assignment_expr.lvalue, depth + 2, 0, keywords, identifiers, strings);
            }
            if (node->data.assignment_expr.rvalue) {
                print_tree_prefix(depth + 1, 1);
                printf("rvalue:\n");
                print_ast_with_prefix(node->data.assignment_expr.rvalue, depth + 2, 1, keywords, identifiers, strings);
            }
            break;

        case AST_CALL_EXPR:
            if (node->data.call_expr.callee) {
                int has_args = node->data.call_expr.args && node->data.call_expr.args->count > 0;
                print_tree_prefix(depth + 1, !has_args);
                printf("callee:\n");
                print_ast_with_prefix(node->data.call_expr.callee, depth + 2, 0, keywords, identifiers, strings);
            }
            
            if (node->data.call_expr.args && node->data.call_expr.args->count > 0) {
                print_tree_prefix(depth + 1, 1);
                printf("arguments:\n");
                for (size_t i = 0; i < node->data.call_expr.args->count; ++i) {
                    AstNode *arg = *(AstNode**)dynarray_get(node->data.call_expr.args, i);
                    print_ast_with_prefix(arg, depth + 2, i == node->data.call_expr.args->count - 1, keywords, identifiers, strings);
                }
            }
            break;

        case AST_SUBSCRIPT_EXPR:
            if (node->data.subscript_expr.target) {
                print_tree_prefix(depth + 1, 0);
                printf("target:\n");
                print_ast_with_prefix(node->data.subscript_expr.target, depth + 2, 0, keywords, identifiers, strings);
            }
            if (node->data.subscript_expr.index) {
                print_tree_prefix(depth + 1, 1);
                printf("index:\n");
                print_ast_with_prefix(node->data.subscript_expr.index, depth + 2, 1, keywords, identifiers, strings);
            }
            break;

        case AST_MEMBER_EXPR:
            if (node->data.member_expr.target) {
                print_tree_prefix(depth + 1, 0);
                printf("target:\n");
                print_ast_with_prefix(node->data.member_expr.target, depth + 2, 0, keywords, identifiers, strings);
            }
            print_tree_prefix(depth + 1, 1);
            printf("member: ");
            if (node->data.member_expr.member && 
                node->data.member_expr.member->entry && identifiers) {
                const char *name = interner_get_cstr(identifiers, node->data.member_expr.member->entry->dense_index);
                printf("'%s'", name ? name : "?");
            } else {
                printf("(unknown)");
            }
            printf("\n");
            break;

        case AST_CAST: {
            if (node->data.cast_expr.expr) {
                print_tree_prefix(depth + 1, 1);
                printf("expression:\n");
                print_ast_with_prefix(node->data.cast_expr.expr, depth + 2, 1, keywords, identifiers, strings);
            }
            break;
        }

        case AST_TYPE: {
            print_tree_prefix(depth + 1, 0);
            printf("kind: %s", type_kind_to_string(node->data.ast_type.kind));
            if (node->data.ast_type.span.start_line > 0) {
                printf(" [%zu:%zu-%zu:%zu]", 
                       node->data.ast_type.span.start_line, node->data.ast_type.span.start_col,
                       node->data.ast_type.span.end_line, node->data.ast_type.span.end_col);
            }
            printf("\n");
            
            switch (node->data.ast_type.kind) {
                case AST_TYPE_PRIMITIVE:
                    print_tree_prefix(depth + 1, 1);
                    printf("type_name: ");
                    if (node->data.ast_type.u.base.intern_result && node->data.ast_type.u.base.intern_result->entry) {
                        int dense_index = node->data.ast_type.u.base.intern_result->entry->dense_index;
                        const char *type_name = "?";
                        if (keywords) {
                            type_name = interner_get_cstr(keywords, dense_index);
                        }
                        if (!type_name && identifiers) {
                            type_name = interner_get_cstr(identifiers, dense_index);
                        }
                        printf("'%s'", type_name ? type_name : "?");
                    } else {
                        printf("(unknown)");
                    }
                    printf("\n");
                    break;
                    
                case AST_TYPE_PTR:
                    if (node->data.ast_type.u.ptr.target) {
                        print_tree_prefix(depth + 1, 1);
                        printf("target_type:\n");
                        print_ast_with_prefix(node->data.ast_type.u.ptr.target, depth + 2, 1, keywords, identifiers, strings);
                    }
                    break;
                    
                case AST_TYPE_ARRAY:
                    if (node->data.ast_type.u.array.elem) {
                        print_tree_prefix(depth + 1, !node->data.ast_type.u.array.size_expr);
                        printf("element_type:\n");
                        print_ast_with_prefix(node->data.ast_type.u.array.elem, depth + 2, !node->data.ast_type.u.array.size_expr, keywords, identifiers, strings);
                    }
                    if (node->data.ast_type.u.array.size_expr) {
                        print_tree_prefix(depth + 1, 1);
                        printf("size:\n");
                        print_ast_with_prefix(node->data.ast_type.u.array.size_expr, depth + 2, 1, keywords, identifiers, strings);
                    } else {
                        print_tree_prefix(depth + 1, 1);
                        printf("size: (unspecified)\n");
                    }
                    break;
                    
                case AST_TYPE_FUNC: {
                    int has_return = node->data.ast_type.u.func.return_type != NULL;
                    int has_params = node->data.ast_type.u.func.param_types && node->data.ast_type.u.func.param_types->count > 0;
                    
                    if (has_params) {
                        print_tree_prefix(depth + 1, !has_return);
                        printf("parameters:\n");
                        for (size_t i = 0; i < node->data.ast_type.u.func.param_types->count; ++i) {
                            AstNode *param_type = *(AstNode**)dynarray_get(node->data.ast_type.u.func.param_types, i);
                            print_ast_with_prefix(param_type, depth + 2, i == node->data.ast_type.u.func.param_types->count - 1, keywords, identifiers, strings);
                        }
                    }
                    if (has_return) {
                        print_tree_prefix(depth + 1, 1);
                        printf("return_type:\n");
                        print_ast_with_prefix(node->data.ast_type.u.func.return_type, depth + 2, 1, keywords, identifiers, strings);
                    }
                    break;
                }
            }
            break;
        }

        case AST_INITIALIZER_LIST:
            if (node->data.initializer_list.elements && node->data.initializer_list.elements->count > 0) {
                print_tree_prefix(depth + 1, 1);
                printf("elements:\n");
                for (size_t i = 0; i < node->data.initializer_list.elements->count; ++i) {
                    AstNode *elem = *(AstNode**)dynarray_get(node->data.initializer_list.elements, i);
                    print_ast_with_prefix(elem, depth + 2, i == node->data.initializer_list.elements->count - 1, keywords, identifiers, strings);
                }
            }
            break;

        case AST_BREAK_STATEMENT:
        case AST_CONTINUE_STATEMENT:
            break;

        default:
            print_tree_prefix(depth + 1, 1);
            printf("(unhandled node type: %d)\n", node->node_type);
            break;
    }
}