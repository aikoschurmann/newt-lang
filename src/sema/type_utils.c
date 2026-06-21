#include "sema/type_utils.h"
#include "sema/type.h"
#include <stdbool.h>

bool type_is_integer(Type *t) {
    if (!t) return false;
    if (t->kind != TYPE_PRIMITIVE) return false;
    switch (t->as.primitive) {
        case PRIM_I8: case PRIM_I16: case PRIM_I32: case PRIM_I64:
        case PRIM_U8: case PRIM_U16: case PRIM_U32: case PRIM_U64:
        case PRIM_USIZE: case PRIM_ISIZE:
            return true;
        default:
            return false;
    }
}

bool type_is_unsigned(Type *t) {
    if (!t) return false;
    if (t->kind != TYPE_PRIMITIVE) return false;
    switch (t->as.primitive) {
        case PRIM_U8: case PRIM_U16: case PRIM_U32: case PRIM_U64:
            return true;
        default:
            return false;
    }
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