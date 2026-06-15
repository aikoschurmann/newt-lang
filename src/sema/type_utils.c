#include "sema/type_utils.h"
#include "sema/type.h"
#include <stdbool.h>

bool type_is_integer(Type *t) {
    if (!t) return false;
    if (t->kind != TYPE_PRIMITIVE) return false;
    return t->as.primitive == PRIM_I32 || t->as.primitive == PRIM_I64;
}

bool type_is_float(Type *t) {
    if (!t) return false;
    if (t->kind != TYPE_PRIMITIVE) return false;
    return t->as.primitive == PRIM_F32 || t->as.primitive == PRIM_F64;
}

bool type_is_bool(Type *t) {
    if (!t) return false;
    if (t->kind != TYPE_PRIMITIVE) return false;
    return t->as.primitive == PRIM_BOOL;
}

bool type_is_char(Type *t) {
    if (!t) return false;
    if (t->kind != TYPE_PRIMITIVE) return false;
    return t->as.primitive == PRIM_CHAR;
}

bool type_is_void(Type *t) {
    return t && t->kind == TYPE_VOID;
}

bool type_is_pointer_to_void(Type *t) {
    if (!t || t->kind != TYPE_POINTER) return false;
    return type_is_void(t->as.ptr.base);
}

bool type_is_pointer_like(Type *t) {
    if (!t) return false;
    return (t->kind == TYPE_POINTER);
}

bool type_is_numeric(Type *t) {
    return type_is_integer(t) || type_is_float(t);
}

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