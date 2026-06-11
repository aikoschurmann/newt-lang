#include "type_print.h"
#include "type.h"
#include "ast.h"         // for AstNode
#include "dense_arena_interner.h" 
#include "dynamic_array.h"
#include <string.h>

#define RESET   "\033[0m"
#define BOLD    "\033[1m"
#define DIM     "\033[2m"

// Detailed Syntax Highlighting
#define COL_PRIM    RESET        // White
#define COL_PTR     "\033[31m"   // Red
#define COL_ARR     "\033[33m"   // Yellow
#define COL_NUM     RESET        // White
#define COL_FUNC    RESET        // White/Default for punctuators
#define COL_STRUCT  RESET        // White
#define COL_KEYWORD RESET        // White
#define COL_INDEX   "\033[33m"   // Yellow for [0], [1]
#define COL_KIND_PRIM "\033[34m" // Blue for (primitive)
#define COL_KIND_PTR  "\033[31m" // Red for (pointer)
#define COL_KIND_ARR  "\033[33m" // Yellow for (array)
#define COL_KIND_FUNC "\033[35m" // Magenta for (function)
#define COL_KIND_OTHER "\033[2m" // Dim for others


static const char* get_primitive_name(PrimitiveKind kind) {
    switch (kind) {
        case PRIM_I32:  return "i32";
        case PRIM_I64:  return "i64";
        case PRIM_F32:  return "f32";
        case PRIM_F64:  return "f64";
        case PRIM_BOOL: return "bool";
        case PRIM_CHAR: return "char";
        case PRIM_STR:  return "str";
        case PRIM_VOID: return "void";
        default:        return "unknown_prim";
    }
}

// Recursive implementation
static void type_print_internal(FILE *f, const Type *type) {
    if (!type) { fprintf(f, "null"); return; }

    switch (type->kind) {
        case TYPE_PRIMITIVE:
            fprintf(f, "%s", get_primitive_name(type->as.primitive));
            break;

        case TYPE_POINTER: {
            int needs_parens = (type->as.ptr.base->kind == TYPE_FUNCTION);
            
            if (needs_parens) fprintf(f, "(");
            type_print_internal(f, type->as.ptr.base);
            if (needs_parens) fprintf(f, ")");
            
            fprintf(f, "*");
            break;
        }

        case TYPE_ARRAY: { 
            int needs_parens = (type->as.array.base->kind == TYPE_FUNCTION);

            if (needs_parens) fprintf(f, "(");
            type_print_internal(f, type->as.array.base);
            if (needs_parens) fprintf(f, ")");

            fprintf(f, "[");
            if (type->as.array.size_known) {
                fprintf(f, "%lld", (long long)type->as.array.size);
            }
            fprintf(f, "]");
            break;
        }
            
        case TYPE_FUNCTION:
            fprintf(f, "(");
            for (size_t i = 0; i < type->as.func.param_count; i++) {
                if (i > 0) fprintf(f, ", ");
                type_print_internal(f, type->as.func.params[i]);
            }

            fprintf(f, ") -> ");
            if (type->as.func.return_type) {
                type_print_internal(f, type->as.func.return_type);
            } else {
                fprintf(f, "err");
            }
            break;

        case TYPE_STRUCT: {
             const char *name = "anonymous";
             if (type->as.struct_type.name && type->as.struct_type.name->key) {
                 name = ((Slice*)type->as.struct_type.name->key)->ptr;
             }
             fprintf(f, "struct %s", name);
             break;
        }
        
        default:
            fprintf(f, "unknown");
            break;
    }
}

void type_print(FILE *f, const Type *type) {
    type_print_internal(f, type);
}

void type_print_signature(const Type *type) {
    type_print_internal(stdout, type);
}

// ----------------------------------------------------------------------------
// Dump Routines
// ----------------------------------------------------------------------------

// Internal helper for usage of "Interned Types" listing
static const char* get_kind_name(const Type *type) {
    if (!type) return "unknown";
    switch (type->kind) {
        case TYPE_PRIMITIVE: return "primitive";
        case TYPE_POINTER:   return "pointer";
        case TYPE_ARRAY:     return "array";
        case TYPE_FUNCTION:  return "function";
        case TYPE_STRUCT:    return "struct";
        default:             return "unknown";
    }
}

// Internal helper for usage of "Interned Types" listing
static const char* get_kind_color(const Type *type) {
    if (!type) return RESET;
    switch (type->kind) {
        case TYPE_PRIMITIVE: return COL_KIND_PRIM;
        case TYPE_POINTER:   return COL_KIND_PTR;
        case TYPE_ARRAY:     return COL_KIND_ARR;
        case TYPE_FUNCTION:  return COL_KIND_FUNC;
        case TYPE_STRUCT:    return COL_STRUCT;
        default:             return COL_KIND_OTHER;
    }
}

static void print_header(void) {
    printf("\n" BOLD "═══════════════════════════════════════════════════════════════════" RESET "\n");
    printf(BOLD "                    TYPE INTERNMENT ANALYSIS" RESET "\n");
    printf(BOLD "═══════════════════════════════════════════════════════════════════" RESET "\n");
}

static int digits_for_count(int count) {
    int digits = 1;
    int value = count > 0 ? count - 1 : 0;
    while (value >= 10) {
        value /= 10;
        digits++;
    }
    return digits;
}

static void print_interned_type_header(int index_width) {
    int index_col_width = index_width + 2;
    if (index_col_width < 5) index_col_width = 5;
    printf("  " BOLD "%-*s  %-9s  %s" RESET "\n",
        index_col_width, "Index", "Kind", "Type");
}

static void print_interned_type_line(int index, int index_width, Type *type) {
    int index_col_width = index_width + 2;
    if (index_col_width < 5) index_col_width = 5;
    
    printf("  " COL_INDEX "[%*d]" RESET "%*s", index_width, index,
        (int)(index_col_width - (index_width + 2) + 2), "");
    printf("%s%-9s" RESET "  ", get_kind_color(type), get_kind_name(type));
    type_print_internal(stdout, type);
    printf("\n");
}

static const char *safe_symbol_name(InternResult *name_rec) {
    if (!name_rec || !name_rec->key) return "(unknown)";
    return (const char *)((Slice *)name_rec->key)->ptr;
}

static void print_symbol_info(AstNode *func_decl) {
    InternResult *intern_result = func_decl->data.function_declaration.intern_result;
    const char *func_name = safe_symbol_name(intern_result);

    printf("  " BOLD "%s" RESET "\n", func_name);

    if (intern_result) {
        printf("    symbol ptr: " DIM "%p" RESET "\n", (void *)intern_result);
    } else {
        printf("    symbol ptr: " COL_PTR "none" RESET "\n");
    }
}

static void print_function_type_info(AstNode *func_decl) {
    if (!func_decl->type) {
        printf("    type: " COL_PTR "none" RESET "\n");
        return;
    }

    printf("    type:   ");
    type_print_internal(stdout, func_decl->type);
    printf(" %s(%s)" RESET "\n", get_kind_color(func_decl->type), get_kind_name(func_decl->type));

    if (func_decl->type->kind != TYPE_FUNCTION) return;

    size_t param_count = func_decl->type->as.func.param_count;
    printf("    params (%zu):\n", param_count);

    for (size_t p = 0; p < param_count; p++) {
        Type *param_type = func_decl->type->as.func.params[p];

        printf("      param[%zu]: ", p);
        type_print_internal(stdout, param_type);
        printf(" %s(%s)" RESET "\n", get_kind_color(param_type), get_kind_name(param_type));
    }

    Type *return_type = func_decl->type->as.func.return_type;
    if (return_type) {
        printf("    return: ");
        type_print_internal(stdout, return_type);
        printf(" %s(%s)" RESET "\n", get_kind_color(return_type), get_kind_name(return_type));
    } else {
        printf("    return: " COL_PTR "none" RESET "\n");
    }
}

void type_print_store_dump(TypeStore *store, AstNode *program) {
    if (!store || !store->type_interner) return; /* nothing to show */

    print_header();

    printf("Total types interned: " COL_NUM "%d" RESET "\n",
           store->type_interner->dense_index_count);;

    /* Interned types list */
    printf("\n" BOLD "Interned Types:" RESET "\n");
    printf("--------------\n");

    int count = store->type_interner->dense_index_count;
    if (count <= 0) {
        printf(" " DIM "(none)" RESET "\n");
    } else {
        int index_width = digits_for_count(count);
        print_interned_type_header(index_width);
        for (int i = 0; i < count; i++) {
            InternResult **result_ptr = (InternResult **)dynarray_get(store->type_interner->dense_array, i);
            if (!result_ptr || !*result_ptr) continue;
            InternResult *result = *result_ptr;
            Slice *key_slice = (Slice *)result->key;
            if (!key_slice || !key_slice->ptr) continue;
            Type *type = (Type *)key_slice->ptr;
            print_interned_type_line(i, index_width, type);
        }
    }

    /* Function symbol mapping */
    if (!program || !program->data.program.decls || program->data.program.decls->count == 0) {
        printf("\n" DIM "No function declarations found." RESET "\n");
        return;
    }

    printf("\n" BOLD "Function Symbol Mapping:" RESET "\n");

    size_t func_count = program->data.program.decls->count;
    for (size_t i = 0; i < func_count; i++) {
        AstNode *func_decl = *(AstNode **)dynarray_get(program->data.program.decls, i);
        if (!func_decl || func_decl->node_type != AST_FUNCTION_DECLARATION) continue;

        print_symbol_info(func_decl);
        print_function_type_info(func_decl);
        printf("\n");
    }

    printf("\n");
}
