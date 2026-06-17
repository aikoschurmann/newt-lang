#pragma once

#include "dense_arena_interner.h"
#include <stdint.h>
#include <stdbool.h>

typedef struct Type Type;
typedef struct Symbol Symbol; // Forward declaration for structs/typedefs
typedef struct Scope Scope;   // Forward declaration for intrinsics

typedef enum {
    TYPE_VOID,
    TYPE_PRIMITIVE, // i32, f64, bool, etc.
    TYPE_POINTER,
    TYPE_ARRAY,
    TYPE_SLICE,
    TYPE_FUNCTION,
    TYPE_STRUCT,    // User defined
    TYPE_ENUM       // User defined (not yet implemented)
} TypeKind;

typedef enum {
    PRIM_I8,  PRIM_I16,  PRIM_I32,  PRIM_I64,
    PRIM_U8,  PRIM_U16,  PRIM_U32,  PRIM_U64,
    PRIM_F32, PRIM_F64,
    PRIM_BOOL,
    PRIM_CHAR
} PrimitiveKind;

typedef struct StructField {
    InternResult *name;
    Type *type;
} StructField;

struct Type {
    TypeKind kind;
    uint64_t cached_hash; // Stored hash for memoization

    union {
        // TYPE_PRIMITIVE
        PrimitiveKind primitive;

        // TYPE_POINTER
        struct {
            Type *base;
        } ptr;

        // TYPE_ARRAY
        struct {
            Type *base;
            int64_t size;     // Count of elements
        } array;

        // TYPE_SLICE
        struct {
            Type *base;
        } slice;

        // TYPE_FUNCTION
        struct {
            Type *return_type;
            Type **params;
            size_t param_count;

        } func;

        // TYPE_STRUCT
        struct {
            InternResult *name;
            StructField *fields;
            size_t field_count;
            HashMap *field_map;
            HashMap *methods; // Maps InternResult* (method name) -> Symbol* (the method)
        } struct_type;

        // TYPE_ENUM
        struct {
            char *name;       // Debug name
            Symbol *decl_node; // Link back to the AST/Symbol table for fields
        } user;
    } as;
};

// The Container
typedef struct TypeStore {
    Arena *arena;

    DenseArenaInterner *type_interner; // For interning complex types
    
    // The "Registry" for primitives
    // Key:   void* (The interned key from your identifiers interner)
    // Value: Type*
    HashMap *primitive_registry; 

    // Fast access to common primitives so we don't need to look them up constantly
    Type *t_void;
    Type *t_void_ptr;
    Type *t_i8,  *t_i16, *t_i32, *t_i64;
    Type *t_u8,  *t_u16, *t_u32, *t_u64;
    Type *t_f32, *t_f64;
    Type *t_bool;
    Type *t_char;
    Type *t_str;

    // Pre-interned common property names
    InternResult *kw_len; 
} TypeStore;

TypeStore *typestore_create(Arena *arena, DenseArenaInterner *identifiers, DenseArenaInterner *keywords);
InternResult *intern_type(TypeStore *ts, Type *prototype);

void register_intrinsics(TypeStore *ts, Scope *global_scope, DenseArenaInterner *ids);

// Returns true for i8, u8, i16, i32, i64, etc.
bool type_is_integer(Type *t);
bool type_is_unsigned(Type *t);
// Returns true for f32, f64
bool type_is_float(Type *t);
// Returns true if the type is a boolean
bool type_is_bool(Type *t);
// Returns true if the type is a char
bool type_is_char(Type *t);
