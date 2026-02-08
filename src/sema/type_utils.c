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

bool type_is_numeric(Type *t) {
    return type_is_integer(t) || type_is_float(t);
}

bool type_can_implicit_cast(Type *target, Type *source) {
    if (!target || !source) return false;
    if (target == source) return true;

    // 1. Numeric Promotions
    // Int -> Int (Promotion)
    if (type_is_integer(source) && type_is_integer(target)) {
        if (source->as.primitive == PRIM_I32 && target->as.primitive == PRIM_I64) return true;
    }
    // Float -> Float (Promotion)
    if (type_is_float(source) && type_is_float(target)) {
        if (source->as.primitive == PRIM_F32 && target->as.primitive == PRIM_F64) return true;
    }
    // Int -> Float (Conversion)
    if (type_is_integer(source) && type_is_float(target)) {
        return true; 
    }

    // 2. Array Relaxation / Compatibility
    // Allows T[N] -> T[] and T[N][M] -> T[][]
    if (target->kind == TYPE_ARRAY && source->kind == TYPE_ARRAY) {
        // Target must be generic (unsized) OR sizes must match exactly
        if (!target->as.array.size_known || 
            (source->as.array.size_known && target->as.array.size == source->as.array.size)) {
            
            // Recursively check base types (e.g. check f32[] vs f32[2])
            return type_can_implicit_cast(target->as.array.base, source->as.array.base);
        }
    }

    // 3. Pointer Relaxation (T[N]* -> T[]*)
    if (target->kind == TYPE_POINTER && source->kind == TYPE_POINTER) {
        // A pointer is compatible if its base types are compatible
        // This allows i64[6]* to cast to i64[]*
        return type_can_implicit_cast(target->as.ptr.base, source->as.ptr.base);
    }

    return false;
}