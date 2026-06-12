#pragma once
#include "type.h"
#include <stdbool.h>
bool type_is_integer(Type *t);
bool type_is_float(Type *t);
bool type_is_bool(Type *t);
bool type_is_char(Type *t);
bool type_is_void(Type *t);
bool type_is_pointer_to_void(Type *t);
bool type_is_pointer_like(Type *t);
bool type_can_implicit_cast(Type *target, Type *source);
bool type_is_numeric(Type *t);
