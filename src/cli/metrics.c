#include "cli/metrics.h"
#include "datastructures/dynamic_array.h"
#include <stdio.h>
#include <string.h>

#define COL_RESET   "\033[0m"
#define COL_GRAY    "\033[90m"
#define COL_BOLD    "\033[1m"
#define COL_GREEN   "\033[32m"
#define COL_CYAN    "\033[36m"

// -----------------------------------------------------------------------------
// AST Node Counter
// -----------------------------------------------------------------------------

static void count_nodes_recursive(AstNode *node, size_t *count) {
    if (!node) return;
    (*count)++;

    switch (node->node_type) {
        case AST_PROGRAM: {
            AstProgram *p = &node->data.program;
            if (p->decls) {
                for (size_t i = 0; i < p->decls->count; i++) {
                    count_nodes_recursive(*(AstNode**)dynarray_get(p->decls, i), count);
                }
            }
            break;
        }
        case AST_FUNCTION_DECLARATION: {
            AstFunctionDeclaration *f = &node->data.function_declaration;
            if (f->params) {
                for (size_t i = 0; i < f->params->count; i++) {
                    count_nodes_recursive(*(AstNode**)dynarray_get(f->params, i), count);
                }
            }
            count_nodes_recursive(f->return_type, count);
            count_nodes_recursive(f->body, count);
            break;
        }
        case AST_VARIABLE_DECLARATION: {
            AstVariableDeclaration *v = &node->data.variable_declaration;
            count_nodes_recursive(v->type, count);
            count_nodes_recursive(v->initializer, count);
            break;
        }
        case AST_BLOCK: {
            AstBlock *b = &node->data.block;
            if (b->statements) {
                for (size_t i = 0; i < b->statements->count; i++) {
                    count_nodes_recursive(*(AstNode**)dynarray_get(b->statements, i), count);
                }
            }
            break;
        }
        case AST_RETURN_STATEMENT:
            count_nodes_recursive(node->data.return_statement.expression, count);
            break;
        case AST_IF_STATEMENT: {
            AstIfStatement *i = &node->data.if_statement;
            count_nodes_recursive(i->condition, count);
            count_nodes_recursive(i->then_branch, count);
            count_nodes_recursive(i->else_branch, count);
            break;
        }
        case AST_WHILE_STATEMENT: {
            AstWhileStatement *w = &node->data.while_statement;
            count_nodes_recursive(w->condition, count);
            count_nodes_recursive(w->body, count);
            break;
        }
        case AST_BINARY_EXPR:
            count_nodes_recursive(node->data.binary_expr.left, count);
            count_nodes_recursive(node->data.binary_expr.right, count);
            break;
        case AST_UNARY_EXPR:
            count_nodes_recursive(node->data.unary_expr.expr, count);
            break;
        case AST_ASSIGNMENT_EXPR:
            count_nodes_recursive(node->data.assignment_expr.lvalue, count);
            count_nodes_recursive(node->data.assignment_expr.rvalue, count);
            break;
        case AST_CALL_EXPR: {
            AstCallExpr *c = &node->data.call_expr;
            count_nodes_recursive(c->callee, count);
            if (c->args) {
                for (size_t i = 0; i < c->args->count; i++) {
                    count_nodes_recursive(*(AstNode**)dynarray_get(c->args, i), count);
                }
            }
            break;
        }
        case AST_SUBSCRIPT_EXPR:
            count_nodes_recursive(node->data.subscript_expr.target, count);
            count_nodes_recursive(node->data.subscript_expr.index, count);
            break;
        case AST_INITIALIZER_LIST: {
            AstInitializeList *l = &node->data.initializer_list;
            if (l->elements) {
                for (size_t i = 0; i < l->elements->count; i++) {
                    count_nodes_recursive(*(AstNode**)dynarray_get(l->elements, i), count);
                }
            }
            break;
        }
        case AST_CAST:
            count_nodes_recursive(node->data.cast_expr.expr, count);
            break;
        case AST_EXPR_STATEMENT:
            count_nodes_recursive(node->data.expr_statement.expression, count);
            break;
        case AST_TYPE:
            if (node->data.ast_type.kind == AST_TYPE_ARRAY) {
                count_nodes_recursive(node->data.ast_type.u.array.elem, count);
                count_nodes_recursive(node->data.ast_type.u.array.size_expr, count);
            } else if (node->data.ast_type.kind == AST_TYPE_PTR) {
                count_nodes_recursive(node->data.ast_type.u.ptr.target, count);
            } else if (node->data.ast_type.kind == AST_TYPE_FUNC) {
                count_nodes_recursive(node->data.ast_type.u.func.return_type, count);
            }
            break;
        default: break; // Leaf nodes (Identifier, Literal, etc.)
    }
}

size_t count_ast_nodes(AstNode *program) {
    size_t count = 0;
    count_nodes_recursive(program, &count);
    return count;
}

// -----------------------------------------------------------------------------
// Report Printer
// -----------------------------------------------------------------------------

static void print_mem_unit(size_t bytes) {
    if (bytes < 1024) {
        printf("  %7zu B   ", bytes);
    } else if (bytes < 1024 * 1024) {
        printf("  %7.2f KB  ", bytes / 1024.0);
    } else {
        printf("  %7.2f MB  ", bytes / (1024.0 * 1024.0));
    }
}

void print_compilation_report(CompilationStats *stats, AstNode *program) {
    double total_time = stats->time_tokenize_ms + stats->time_parse_ms + stats->time_sema_ms + stats->time_codegen_ms;
    if (total_time <= 0) total_time = 0.001;

    size_t ast_nodes = count_ast_nodes(program);
    size_t total_mem = stats->mem_lex_bytes + stats->mem_parse_bytes + stats->mem_sema_bytes;
    double throughput = (stats->file_size_bytes / (1024.0 * 1024.0)) / (total_time / 1000.0);

    printf("\n%s[%sCompiling %s%s%s]%s\n", COL_GRAY, COL_RESET, COL_CYAN, stats->filename ? stats->filename : "<stdin>", COL_RESET, COL_RESET);

    printf("%s┌─────────────────┬─────────────┬──────────────┐%s\n", COL_GRAY, COL_RESET);
    printf("%s│%s Phase           %s│%s  Time       %s│%s Memory       %s│%s\n", COL_GRAY, COL_BOLD, COL_GRAY, COL_BOLD, COL_GRAY, COL_BOLD, COL_GRAY, COL_RESET);
    printf("%s├─────────────────┼─────────────┼──────────────┤%s\n", COL_GRAY, COL_RESET);

    printf("%s│%s Lexical         %s│%s %8.2f ms %s│%s", COL_GRAY, COL_RESET, COL_GRAY, COL_RESET, stats->time_tokenize_ms, COL_GRAY, COL_RESET);
    print_mem_unit(stats->mem_lex_bytes);
    printf("%s│%s\n", COL_GRAY, COL_RESET);

    printf("%s│%s Parsing         %s│%s %8.2f ms %s│%s", COL_GRAY, COL_RESET, COL_GRAY, COL_RESET, stats->time_parse_ms, COL_GRAY, COL_RESET);
    print_mem_unit(stats->mem_parse_bytes);
    printf("%s│%s\n", COL_GRAY, COL_RESET);

    printf("%s│%s Semantic        %s│%s %8.2f ms %s│%s", COL_GRAY, COL_RESET, COL_GRAY, COL_RESET, stats->time_sema_ms, COL_GRAY, COL_RESET);
    print_mem_unit(stats->mem_sema_bytes);
    printf("%s│%s\n", COL_GRAY, COL_RESET);

    printf("%s│%s Codegen         %s│%s %8.2f ms %s│%s      -       %s│%s\n", COL_GRAY, COL_RESET, COL_GRAY, COL_RESET, stats->time_codegen_ms, COL_GRAY, COL_RESET, COL_GRAY, COL_RESET);

    printf("%s├─────────────────┼─────────────┼──────────────┤%s\n", COL_GRAY, COL_RESET);
    printf("%s│%s TOTAL           %s│%s %8.2f ms %s│%s", COL_GRAY, COL_BOLD, COL_GRAY, COL_BOLD, total_time, COL_GRAY, COL_BOLD);
    print_mem_unit(total_mem);
    printf("%s│%s\n", COL_GRAY, COL_RESET);
    printf("%s└─────────────────┴─────────────┴──────────────┘%s\n", COL_GRAY, COL_RESET);

    printf("  %sNodes:%s %zu  %sThroughput:%s %.2f MB/s\n\n", COL_GRAY, COL_RESET, ast_nodes, COL_GRAY, COL_RESET, throughput);
}
