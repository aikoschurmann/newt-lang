/**
 * @file type_print.c
 * @brief Utilities for formatting and printing the compiler's type system.
 * * This module is responsible for serializing internal compiler types into 
 * human-readable strings for error messages (`type_report.c`) and for the 
 * `--types` CLI debugging flag.
 */

#include "sema/type_print.h"
#include "sema/type.h"
#include "sema/symbol_utils.h" // Needed for Symbol and Scope definitions
#include "parsing/ast.h"
#include <stdio.h>
#include <string.h>
#include "core/colors.h"


#define COL_KIND_PRIMITIVE CYAN
#define COL_KIND_POINTER   YELLOW
#define COL_KIND_ARRAY     GREEN
#define COL_KIND_SLICE     BLUE
#define COL_KIND_STRUCT    MAGENTA
#define COL_KIND_FUNCTION  RED
#define COL_KIND_OTHER     DIM
#define COL_INDEX          DIM
#define COL_PTR            MAGENTA
#define COL_NUM            YELLOW

static void print_primitive_kind(FILE *out, PrimitiveKind kind) {
    switch (kind) {
        case PRIM_I8:   fprintf(out, "i8"); break;
        case PRIM_I16:  fprintf(out, "i16"); break;
        case PRIM_I32:  fprintf(out, "i32"); break;
        case PRIM_I64:  fprintf(out, "i64"); break;
        case PRIM_U8:   fprintf(out, "u8"); break;
        case PRIM_U16:  fprintf(out, "u16"); break;
        case PRIM_U32:  fprintf(out, "u32"); break;
        case PRIM_U64:  fprintf(out, "u64"); break;
        case PRIM_F32:  fprintf(out, "f32"); break;
        case PRIM_F64:  fprintf(out, "f64"); break;
        case PRIM_BOOL: fprintf(out, "bool"); break;
        case PRIM_CHAR: fprintf(out, "char"); break;
    }
}

static void type_print_internal(FILE *out, const Type *type) {
    if (!type) { fprintf(out, "null"); return; }

    switch (type->kind) {
        case TYPE_VOID: fprintf(out, "void"); break;
        case TYPE_PRIMITIVE: print_primitive_kind(out, type->as.primitive); break;
        case TYPE_POINTER:
            fprintf(out, "*");
            type_print_internal(out, type->as.ptr.base);
            break;
        case TYPE_ARRAY:
            type_print_internal(out, type->as.array.base);
            fprintf(out, "[%lld]", (long long)type->as.array.size);
            break;
        case TYPE_SLICE:
            type_print_internal(out, type->as.slice.base);
            fprintf(out, "[]");
            break;
        case TYPE_STRUCT:
            if (type->as.struct_type.name && type->as.struct_type.name->key) {
                Slice *s = (Slice*)type->as.struct_type.name->key;
                fprintf(out, "%.*s", (int)s->len, s->ptr);
            } else fprintf(out, "struct");
            break;
        case TYPE_FUNCTION:
            fprintf(out, "fn(");
            for (size_t i = 0; i < type->as.func.param_count; i++) {
                if (i > 0) fprintf(out, ", ");
                type_print_internal(out, type->as.func.params[i]);
            }
            fprintf(out, ") -> ");
            type_print_internal(out, type->as.func.return_type);
            break;
        default: break;
    }
}

/**
 * Public API wrapper for printing types.
 */
void type_print(FILE *out, const Type *type) {
    type_print_internal(out, type);
}

// =============================================================================
// CLI DUMP FORMATTING HELPERS
// =============================================================================

static const char* get_kind_name(const Type *type) {
    if (!type) return "NULL";
    switch (type->kind) {
        case TYPE_VOID:      return "Void";
        case TYPE_PRIMITIVE: return "Primitive";
        case TYPE_POINTER:   return "Pointer";
        case TYPE_ARRAY:     return "Array";
        case TYPE_SLICE:     return "Slice";
        case TYPE_STRUCT:    return "Struct";
        case TYPE_FUNCTION:  return "Function";
        case TYPE_ENUM:      return "Enum";
        default:             return "Unknown";
    }
}

static const char* get_kind_color(const Type *type) {
    if (!type) return RESET;
    switch (type->kind) {
        case TYPE_VOID:      return COL_KIND_OTHER;
        case TYPE_PRIMITIVE: return COL_KIND_PRIMITIVE;
        case TYPE_POINTER:   return COL_KIND_POINTER;
        case TYPE_ARRAY:     return COL_KIND_ARRAY;
        case TYPE_SLICE:     return COL_KIND_SLICE;
        case TYPE_STRUCT:    return COL_KIND_STRUCT;
        case TYPE_FUNCTION:  return COL_KIND_FUNCTION;
        case TYPE_ENUM:      return COL_KIND_OTHER;
        default:             return COL_KIND_OTHER;
    }
}

static const char* symbol_kind_to_str(SymbolValue kind) {
    switch (kind) {
        case SYMBOL_VARIABLE:       return "Variable";
        case SYMBOL_VALUE_FUNCTION: return "Function";
        case SYMBOL_VALUE_TYPE:     return "Type/Struct";
        case SYMBOL_VALUE_ALIAS:    return "Alias";
        case SYMBOL_VALUE_MODULE:   return "Module";
        case SYMBOL_VALUE_NAMESPACE:return "Namespace";
        case SYMBOL_VALUE_INTRINSIC:return "Intrinsic";
        default:                    return "Unknown";
    }
}

static void print_header(void) {
    printf("\n" BOLD "═══════════════════════════════════════════════════════════════════" RESET "\n");
    printf(BOLD "                    COMPILER STATE ANALYSIS" RESET "\n");
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

static void print_interned_type_line(int index, int index_width, const Type *type) {
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

// =============================================================================
// MAIN COMPILER STATE DUMPER (--types)
// =============================================================================

/**
 * Dumps the entire state of the TypeStore (Interner) and the main Module's 
 * Symbol Table (Global Scope). Activated via the `--types` CLI flag.
 */
void type_print_store_dump(TypeStore *store, Scope *global_scope) {
    if (!store || !store->type_interner) return; 

    print_header();
    printf("Total unique types interned: " COL_NUM "%d" RESET "\n", store->type_interner->dense_index_count);

    // -------------------------------------------------------------------------
    // 1. DUMP INTERNED TYPES
    // -------------------------------------------------------------------------
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

    if (!global_scope) return;

    // -------------------------------------------------------------------------
    // 2. DUMP GLOBAL SYMBOL TABLE
    // -------------------------------------------------------------------------
    printf("\n" BOLD "Global Symbol Table:" RESET "\n");
    printf("--------------------\n");

    if (global_scope->symbols_list.count == 0) {
         printf(" " DIM "(no symbols in global scope)" RESET "\n");
    }

    for (size_t i = 0; i < global_scope->symbols_list.count; i++) {
        Symbol *sym = *(Symbol**)dynarray_get(&global_scope->symbols_list, i);
        if (!sym) continue;

        const char *name = safe_symbol_name(sym->name_rec);
        
        // Header: Symbol Name and Kind
        printf("  " BOLD "%s" RESET " " DIM "[%s]" RESET, name, symbol_kind_to_str(sym->kind));

        // Modifiers
        if (sym->is_pub) printf(GREEN " pub" RESET);
        if (sym->flags & SYMBOL_FLAG_CONST) printf(YELLOW " const" RESET);
        printf("\n");

        // Internal Pointer
        printf("    symbol ptr: " DIM "%p" RESET "\n", (void *)sym->name_rec);

        // Type Information
        if (sym->type) {
            printf("    type:   ");
            type_print_internal(stdout, sym->type);
            printf("\n");
            
            // Nested Fields (For Structs)
            if (sym->type->kind == TYPE_STRUCT && sym->kind == SYMBOL_VALUE_TYPE) {
                printf("    fields:\n");
                for (size_t f = 0; f < sym->type->as.struct_type.field_count; f++) {
                    StructField *field = &sym->type->as.struct_type.fields[f];
                    printf("      - %s: ", safe_symbol_name(field->name));
                    type_print_internal(stdout, field->type);
                    printf(" %s(%s)" RESET "\n", get_kind_color(field->type), get_kind_name(field->type));
                }
            }
            
            // Detailed Function Breakdown
            if (sym->type->kind == TYPE_FUNCTION) {
                size_t param_count = sym->type->as.func.param_count;
                printf("    params (%zu):\n", param_count);

                for (size_t p = 0; p < param_count; p++) {
                    Type *param_type = sym->type->as.func.params[p];
                    printf("      param[%zu]: ", p);
                    type_print_internal(stdout, param_type);
                    printf(" %s(%s)" RESET "\n", get_kind_color(param_type), get_kind_name(param_type));
                }

                Type *return_type = sym->type->as.func.return_type;
                if (return_type) {
                    printf("    return: ");
                    type_print_internal(stdout, return_type);
                    printf(" %s(%s)" RESET "\n", get_kind_color(return_type), get_kind_name(return_type));
                }
            }
        } else if (sym->kind != SYMBOL_VALUE_MODULE && sym->kind != SYMBOL_VALUE_ALIAS) {
            printf("    type:   " COL_PTR "none" RESET "\n");
        }

        // Aliasing Information
        if (sym->kind == SYMBOL_VALUE_ALIAS && sym->target_symbol) {
             printf("    target: " CYAN "%s" RESET "\n", safe_symbol_name(sym->target_symbol->name_rec));
        }

        // Computed Constant Values
        if (sym->flags & SYMBOL_FLAG_COMPUTED_VALUE) {
            if (type_is_integer(sym->type)) {
                printf("    value:  " YELLOW "%lld" RESET "\n", (long long)sym->value.int_val);
            } else if (type_is_float(sym->type)) {
                printf("    value:  " YELLOW "%f" RESET "\n", sym->value.float_val);
            }
        }

        // Module Path Info
        if (sym->kind == SYMBOL_VALUE_MODULE && sym->filename) {
            printf("    path:   " DIM "%s" RESET "\n", sym->filename);
        }

        printf("\n");
    }
}
