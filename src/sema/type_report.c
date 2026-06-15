#include "sema/type_report.h"
#include "sema/type_print.h"
#include "core/file.h"
#include <stdio.h>
#include <stdlib.h>

#define COL_RESET   "\033[0m"
#define COL_RED     "\033[1;31m"
#define COL_YELLOW  "\033[1;33m"
#define COL_MAGENTA "\033[1;35m"
#define COL_BOLD    "\033[1m"
#define COL_DIM     "\033[2m"

static const char* op_kind_to_str(OpKind op) {
    switch (op) {
        case OP_ADD: return "+";
        case OP_SUB: return "-";
        case OP_MUL: return "*";
        case OP_DIV: return "/";
        case OP_MOD: return "%";
        case OP_EQ:  return "==";
        case OP_NEQ: return "!=";
        case OP_LT:  return "<";
        case OP_GT:  return ">";
        case OP_LE:  return "<=";
        case OP_GE:  return ">=";
        case OP_AND: return "&&";
        case OP_OR:  return "||";
        case OP_NOT: return "!";
        case OP_ASSIGN: return "=";
        case OP_PLUS_EQ: return "+=";
        case OP_MINUS_EQ: return "-=";
        case OP_MUL_EQ: return "*=";
        case OP_DIV_EQ: return "/=";
        case OP_MOD_EQ: return "%=";
        default: return "?";
    }
}

static void print_type_quoted(FILE *f, const Type *type) {
    fprintf(f, "'%s", COL_YELLOW);
    type_print(f, type);
    fprintf(f, "%s'", COL_RESET);
}

void print_type_error(TypeError *err) {
    if (!err) return;

    fprintf(stderr, "%s%s:%zu:%zu: %serror:%s ", 
        COL_BOLD, 
        err->filename ? err->filename : "<input>", 
        err->span.start_line, err->span.start_col, 
        COL_RED, COL_RESET);

    switch (err->kind) {
        case TE_UNKNOWN_TYPE:
            fprintf(stderr, "Unknown type '%s%s%s'.\n", COL_YELLOW, err->as.name.name, COL_RESET);
            break;
        case TE_INCOMPLETE_TYPE:
             fprintf(stderr, "Incomplete type: '%s%s%s'.\n", COL_YELLOW, err->as.name.name, COL_RESET);
             break;
        case TE_EXPECTED_TYPE_ARG:
            fprintf(stderr, "Expected a type as argument, but found expression of type ");
            print_type_quoted(stderr, err->as.mismatch.actual);
            fprintf(stderr, ".\n");
            break;
        case TE_REDECLARATION:
            fprintf(stderr, "Redefinition of symbol '%s%s%s'.\n", COL_YELLOW, err->as.name.name, COL_RESET);
            break;
        case TE_UNDECLARED:
            fprintf(stderr, "Use of undeclared identifier '%s%s%s'.\n", COL_YELLOW, err->as.name.name, COL_RESET);
            break;
        case TE_TYPE_MISMATCH:
            fprintf(stderr, "Type mismatch: expected ");
            print_type_quoted(stderr, err->as.mismatch.expected);
            fprintf(stderr, " but found ");
            if (err->as.mismatch.actual) print_type_quoted(stderr, err->as.mismatch.actual);
            else fprintf(stderr, "unknown/invalid");
            fprintf(stderr, ".\n");
            break;

        case TE_DIMENSION_MISMATCH:
            fprintf(stderr, "Dimension mismatch: expected %d dimensions, but got %d.\n", 
                err->as.dims.expected_ndim, err->as.dims.actual_ndim);
            break;
        case TE_ARRAY_SIZE_MISMATCH:
            fprintf(stderr, "Array size mismatch: dimension has size %zu, but initializer has size %zu.\n", 
                err->as.size.expected_size, err->as.size.actual_size);
            break;
        case TE_INDEX_OUT_OF_BOUNDS:
            fprintf(stderr, "Array index out of bounds: index %zu is >= array size %zu.\n", 
                 err->as.size.actual_size, err->as.size.expected_size);
            break;
        case TE_EXPECTED_ARRAY:
            fprintf(stderr, "Type mismatch: expected array type ");
            print_type_quoted(stderr, err->as.mismatch.expected);
            fprintf(stderr, ", but found scalar expression of type ");
            if (err->as.mismatch.actual) print_type_quoted(stderr, err->as.mismatch.actual);
            else fprintf(stderr, "unknown");
            fprintf(stderr, ".\n");
            break;
        case TE_UNEXPECTED_LIST:
            fprintf(stderr, "Type mismatch: expected scalar type ");
            print_type_quoted(stderr, err->as.mismatch.expected);
            fprintf(stderr, ", but found an initializer list.\n");
            break;

        case TE_RETURN_MISMATCH:
            fprintf(stderr, "Function return type mismatch: expected ");
            print_type_quoted(stderr, err->as.mismatch.expected);
            fprintf(stderr, " but found ");
            print_type_quoted(stderr, err->as.mismatch.actual);
            fprintf(stderr, ".\n");
            break;
        case TE_VARIABLE_TYPE_RESOLUTION_FAILED:
            fprintf(stderr, "Failed to resolve type for variable '%s%s%s'.\n", COL_YELLOW, err->as.name.name, COL_RESET);
            break;
        case TE_BINOP_MISMATCH:
            fprintf(stderr, "Invalid operands for binary operator '%s%s%s'. Left: ", COL_MAGENTA, op_kind_to_str(err->as.binop.op), COL_RESET);
            print_type_quoted(stderr, err->as.binop.left);
            fprintf(stderr, " Right: ");
            print_type_quoted(stderr, err->as.binop.right);
            fprintf(stderr, ".\n");
            break;
        case TE_UNOP_MISMATCH:
            fprintf(stderr, "Invalid operand for unary operator '%s%s%s'. Operand: ", COL_MAGENTA, op_kind_to_str(err->as.unop.op), COL_RESET);
            print_type_quoted(stderr, err->as.unop.operand);
            fprintf(stderr, ".\n");
            break;
        case TE_NOT_CALLABLE:
            fprintf(stderr, "Expression of type ");
            print_type_quoted(stderr, err->as.bad_usage.actual);
            fprintf(stderr, " is not callable.\n");
            break;
        case TE_NOT_INDEXABLE:
            fprintf(stderr, "Expression of type ");
            print_type_quoted(stderr, err->as.bad_usage.actual);
            fprintf(stderr, " is not indexable.\n");
            break;
        case TE_FIELD_ACCESS:
            fprintf(stderr, "Type ");
            if (err->as.field.type) print_type_quoted(stderr, err->as.field.type);
            else fprintf(stderr, "of expression");
            fprintf(stderr, " has no field named '%s%s%s'.\n", COL_YELLOW, err->as.field.name ? err->as.field.name : "unknown", COL_RESET);
            break;
        case TE_NOT_MEMBER_ACCESSIBLE:
            fprintf(stderr, "Expression of type ");
            print_type_quoted(stderr, err->as.bad_usage.actual);
            fprintf(stderr, " does not support member access.\n");
            break;
        case TE_CONST_ASSIGN:
             fprintf(stderr, "Cannot assign to immutable variable/parameter.\n");
             break;
        case TE_ARG_COUNT_MISMATCH:
             fprintf(stderr, "Argument count mismatch. Expected %zu, found %zu.\n", err->as.arg_count.expected, err->as.arg_count.actual);
             break;
        case TE_NOT_CONST:
            fprintf(stderr, "Expression must be a constant expression.\n");
            break;
        case TE_NOT_LVALUE:
            fprintf(stderr, "Expression is not an lvalue.\n");
            break;
        case TE_RECURSIVE_CONST:
            fprintf(stderr, "Recursive constant definition detected (cycle).\n");
            break;
        default:
            fprintf(stderr, "Unknown Semantic Error.\n");
            break;
    }

    if (err->filename && err->span.start_line > 0) {
        if (err->span.start_line == err->span.end_line && err->span.end_col > err->span.start_col) {
            print_source_excerpt_span(err->filename, err->span.start_line, err->span.start_col, err->span.end_col);
        } else {
            print_source_excerpt(err->filename, err->span.start_line, err->span.start_col);
        }
    }
}