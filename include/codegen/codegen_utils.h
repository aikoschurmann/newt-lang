#pragma once
#include "sema/type.h"
#include <stddef.h>

// Returns the index of a field by name in a struct type.
// Returns (size_t)-1 if not found.
size_t struct_field_index(Type *struct_type, const char *field_name);
size_t get_struct_field_index(Type *struct_type, InternResult *field_name);
