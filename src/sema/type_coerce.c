#include "sema/type_coerce.h"
#include "sema/type_utils.h"
#include "sema/type.h"
#include "parsing/ast.h"
#include "datastructures/dynamic_array.h"
#include "datastructures/arena.h"
#include <string.h>

/* Matrix for implicit primitive coercion (widening and lossless promotion).
   Rows: Source type, Columns: Target type.
   Indices correspond to PrimitiveKind enum in include/sema/type.h:
   0:I8, 1:I16, 2:I32, 3:I64, 4:U8, 5:U16, 6:U32, 7:U64, 8:F32, 9:F64, 10:BOOL, 11:CHAR */
static const bool primitive_coercion_matrix[14][14] = {
    /* Target: I8  I16 I32 I64 U8  U16 U32 U64 F32 F64 BOL CHR USZ ISZ */
    /* I8  */ {0,  1,  1,  1,  0,  0,  0,  0,  1,  1,  0,  0,  0,  1},
    /* I16 */ {0,  0,  1,  1,  0,  0,  0,  0,  1,  1,  0,  0,  0,  1},
    /* I32 */ {0,  0,  0,  1,  0,  0,  0,  0,  0,  1,  0,  0,  0,  1},
    /* I64 */ {0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0},
    /* U8  */ {0,  1,  1,  1,  0,  1,  1,  1,  1,  1,  0,  0,  1,  1},
    /* U16 */ {0,  0,  1,  1,  0,  0,  1,  1,  1,  1,  0,  0,  1,  1},
    /* U32 */ {0,  0,  0,  1,  0,  0,  0,  1,  0,  1,  0,  0,  1,  1},
    /* U64 */ {0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0},
    /* F32 */ {0,  0,  0,  0,  0,  0,  0,  0,  0,  1,  0,  0,  0,  0},
    /* F64 */ {0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0},
    /* BOL */ {0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0},
    /* CHR */ {0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0},
    /* USZ */ {0,  0,  0,  0,  0,  0,  0,  1,  0,  0,  0,  0,  0,  0},
    /* ISZ */ {0,  0,  0,  1,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0},
};

static bool can_implicit_cast_primitive(Type *target, Type *source) {
    PrimitiveKind s = source->as.primitive;
    PrimitiveKind t = target->as.primitive;

    if (s < 0 || s >= 14 || t < 0 || t >= 14) return false;

    return primitive_coercion_matrix[s][t];
}

static bool can_implicit_cast_array(Type *target, Type *source) {
    // 2. Array Decay (T[N] -> T[])
    if (target->kind == TYPE_SLICE) {
        return target->as.slice.base == source->as.array.base;
    }
    // 4. Array to Pointer Decay (T[N] -> *T)
    if (target->kind == TYPE_POINTER) {
        return target->as.ptr.base == source->as.array.base;
    }
    return false;
}

static bool can_implicit_cast_pointer(Type *target, Type *source) {
    // Safe: T* -> *void (Implicitly discarding type info)
    if (type_is_void(target->as.ptr.base)) return true;
    
    // Tightened: T[N]* -> T[]* only if T is identical
    Type *sb = source->as.ptr.base;
    Type *tb = target->as.ptr.base;
    if (sb->kind == TYPE_ARRAY && tb->kind == TYPE_SLICE) {
            return tb->as.slice.base == sb->as.array.base;
    }
    return false;
}

/**
 * Checks if 'source' can be implicitly promoted/decayed to 'target'
 * based on the language's coercion rules (e.g. widening, array-to-slice).
 */
bool type_can_implicit_cast(Type *target, Type *source) {
    if (!target || !source) return false;
    if (target == source) return true;

    if (source->kind == TYPE_PRIMITIVE && target->kind == TYPE_PRIMITIVE) {
        return can_implicit_cast_primitive(target, source);
    }

    if (source->kind == TYPE_ARRAY) {
        return can_implicit_cast_array(target, source);
    }

    if (source->kind == TYPE_POINTER && target->kind == TYPE_POINTER) {
        return can_implicit_cast_pointer(target, source);
    }

    if (source->kind == TYPE_ENUM && target->kind == TYPE_PRIMITIVE) {
        return type_is_integer(target);
    }

    return false;
}

/**
 * Checks if 'source' can be explicitly cast to 'target' via 'as' operator.
 * This is a superset of implicit casts, allowing narrowing and bitcasts.
 */
bool type_can_explicit_cast(Type *target, Type *source) {
    if (!target || !source) return false;
    if (type_can_implicit_cast(target, source)) return true;

    // 1. Numeric <-> Numeric (iN, fN, char) - including narrowing
    if ((type_is_numeric(source) || type_is_char(source)) && 
        (type_is_numeric(target) || type_is_char(target))) {
        return true;
    } 
    // 2. Bool -> Numeric (0 or 1)
    if (type_is_bool(source) && (type_is_numeric(target) || type_is_char(target))) {
        return true;
    }
    // 3. Pointer -> Pointer (Unrestricted bitcast for explicit)
    if (source->kind == TYPE_POINTER && target->kind == TYPE_POINTER) {
        return true;
    }
    // 4. Pointer <-> 64-bit Integer (Bit reinterpretation)
    if ((source->kind == TYPE_POINTER && type_is_integer(target)) ||
        (type_is_integer(source) && target->kind == TYPE_POINTER)) {

        Type *int_type = (source->kind == TYPE_POINTER) ? target : source;
        if (int_type->kind == TYPE_PRIMITIVE && 
            (int_type->as.primitive == PRIM_I64 || int_type->as.primitive == PRIM_U64 ||
             int_type->as.primitive == PRIM_USIZE || int_type->as.primitive == PRIM_ISIZE)) {
            return true;
        }
    }
    // 5. Array -> Slice (Decay) - already covered by implicit but for completeness
    if (source->kind == TYPE_ARRAY && target->kind == TYPE_SLICE) {
        if (source->as.array.base == target->as.slice.base) {
            return true;
        }
    }

    return false;
}

/**
 * Performs constant-folding for a cast node.
 * Should be called whenever a cast (implicit or explicit) is created or modified.
 */
void fold_cast_node(AstNode *node) {
    if (node->node_type != AST_CAST) return;
    AstCastExpr *cast = &node->data.cast_expr;
    AstNode *expr = cast->expr;
    Type *to_type = cast->target_type;

    if (!expr->is_foldable_const) return;

    node->is_foldable_const = 1;
    node->is_llvm_const_safe = 1;
    node->const_value = expr->const_value;

    if (type_is_bool(expr->type)) {
        if (type_is_integer(to_type) || type_is_char(to_type)) {
             node->const_value.type = type_is_char(to_type) ? CHAR_LITERAL : INT_LITERAL;
             node->const_value.value.int_val = expr->const_value.value.bool_val ? 1 : 0;
        }
    }
    else if (type_is_float(to_type)) {
        node->const_value.type = FLOAT_LITERAL;
        if (type_is_integer(expr->type)) {
            node->const_value.value.float_val = (double)expr->const_value.value.int_val;
        } else if (type_is_char(expr->type)) {
            node->const_value.value.float_val = (double)expr->const_value.value.char_val;
        }
    } 
    else if (type_is_integer(to_type) || type_is_char(to_type)) {
        node->const_value.type = type_is_char(to_type) ? CHAR_LITERAL : INT_LITERAL;
        if (type_is_float(expr->type)) {
            node->const_value.value.int_val = (int64_t)expr->const_value.value.float_val;
        }
    }
}

/**
 * Inserts an AST_CAST node into the tree, wrapping 'node'.
 */
void insert_cast(TypeCheckContext *ctx, AstNode *node, Type *to_type) {
    if (!node || !to_type) return;
    if (node->type == to_type) return;

    // We clone the current node into a new memory location so the original
    // node can be transformed into the cast container.
    AstNode *original = arena_alloc(ctx->store->arena, sizeof(AstNode));
    memcpy(original, node, sizeof(AstNode));

    node->node_type = AST_CAST;
    node->type = to_type;
    node->span = original->span; 
    
    node->data.cast_expr.expr = original;
    node->data.cast_expr.target_type = to_type;
    node->data.cast_expr.target_type_node = NULL;

    // Propagate constant-folding metadata
    fold_cast_node(node);
}

/**
 * Attempts to coerce 'node' to 'expected' type.
 */
Type* coerce_or_error(TypeCheckContext *ctx, AstNode *node, Type *expected) {
    if (!node || !expected) return node ? node->type : NULL;
    Type *actual = node->type;
    if (actual == expected) return actual;

    // Special case for NULL literal: allow void* -> T*
    bool is_null_literal = (node->node_type == AST_LITERAL && node->data.literal.type == NULL_LITERAL);
    if (is_null_literal && expected->kind == TYPE_POINTER) {
        insert_cast(ctx, node, expected);
        return expected;
    }

    // Check bidirectional compatibility rules (e.g. array-to-slice decay)
    if (type_can_implicit_cast(expected, actual)) {
        insert_cast(ctx, node, expected);
        return expected;
    }

    TypeError err = {
        .kind = TE_TYPE_MISMATCH,
        .span = node->span,
        .filename = ctx->filename,
        .as.mismatch = { .expected = expected, .actual = actual }
    };
    dynarray_push_value(ctx->errors, &err);
    return NULL;
}
