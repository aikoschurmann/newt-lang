#include "sema/type_coerce.h"
#include "sema/type_utils.h"
#include "sema/type.h"
#include "parsing/ast.h"
#include "datastructures/dynamic_array.h"
#include "datastructures/arena.h"
#include <string.h>

/**
 * Checks if 'source' can be implicitly promoted/decayed to 'target'
 * based on the language's coercion rules (e.g. widening, array-to-slice).
 */
bool type_can_implicit_cast(Type *target, Type *source) {
    if (!target || !source) return false;
    if (target == source) return true;

    // 1. Lossless Widening ONLY (Rust-strict style)
    if (source->kind == TYPE_PRIMITIVE && target->kind == TYPE_PRIMITIVE) {
        PrimitiveKind s = source->as.primitive;
        PrimitiveKind t = target->as.primitive;

        // i32 -> i64 (Lossless)
        if (s == PRIM_I32 && t == PRIM_I64) return true;
        // f32 -> f64 (Lossless)
        if (s == PRIM_F32 && t == PRIM_F64) return true;
        // i32 -> f64 (Exact in 53-bit mantissa)
        if (s == PRIM_I32 && t == PRIM_F64) return true;

        // NOTE: i64 -> f64 is NOT implicit (precision loss for large values)
        // NOTE: i32 -> f32 is NOT implicit (precision loss for large values)
        // NOTE: No narrowing (i64 -> i32) is implicit.
    }

    // 2. Array Decay (T[N] -> T[])
    // Tightened: base types must be IDENTICAL, no recursive implicit casting.
    if (target->kind == TYPE_SLICE && source->kind == TYPE_ARRAY) {
        return target->as.slice.base == source->as.array.base;
    }

    // 3. Pointer Relaxation
    if (target->kind == TYPE_POINTER && source->kind == TYPE_POINTER) {
        // Safe: T* -> *void (Implicitly discarding type info)
        if (type_is_void(target->as.ptr.base)) return true;
        
        // Tightened: T[N]* -> T[]* only if T is identical
        Type *sb = source->as.ptr.base;
        Type *tb = target->as.ptr.base;
        if (sb->kind == TYPE_ARRAY && tb->kind == TYPE_SLICE) {
             return tb->as.slice.base == sb->as.array.base;
        }
    }

    // 4. Array to Pointer Decay (T[N] -> *T)
    if (target->kind == TYPE_POINTER && source->kind == TYPE_ARRAY) {
        return target->as.ptr.base == source->as.array.base;
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
    // 4. Pointer <-> i64 (Bit reinterpretation)
    if ((source->kind == TYPE_POINTER && type_is_integer(target)) ||
        (type_is_integer(source) && target->kind == TYPE_POINTER)) {

        Type *int_type = (source->kind == TYPE_POINTER) ? target : source;
        if (int_type->kind == TYPE_PRIMITIVE && int_type->as.primitive == PRIM_I64) {
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
