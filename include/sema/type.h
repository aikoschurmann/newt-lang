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
    TYPE_STRUCT,      // User defined
    TYPE_ENUM,        // User defined (not yet implemented)
    TYPE_TYPEVAR,     // Abstract type variable: T (used in generic templates)
    TYPE_GENERIC_INST // Concrete generic instantiation: Vec[i32]
} TypeKind;

typedef enum {
    PRIM_I8,  PRIM_I16,  PRIM_I32,  PRIM_I64,
    PRIM_U8,  PRIM_U16,  PRIM_U32,  PRIM_U64,
    PRIM_F32, PRIM_F64,
    PRIM_BOOL,
    PRIM_CHAR,
    PRIM_USIZE,
    PRIM_ISIZE
} PrimitiveKind;

typedef struct {
    InternResult *name;
    Type *type; 
    int32_t offset;
} StructField;

typedef struct {
    InternResult *name;
    int64_t value;
} EnumVariant;

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
            struct AstNode *decl_node; // Link back to AstStructDeclaration
            StructField *fields;
            size_t field_count;
            HashMap *field_map;
            HashMap *methods; // Maps InternResult* (method name) -> Symbol* (the method)
        } struct_type;

        // TYPE_ENUM
        struct {
            InternResult *name;
            struct AstNode *decl_node;
            EnumVariant *variants;
            size_t variant_count;
            HashMap *variant_map;
        } enum_type;

        // TYPE_TYPEVAR
        struct {
            InternResult *name;  // e.g. "T"
            int index;           // Position within the template's type_params list
        } typevar;

        // TYPE_GENERIC_INST
        struct {
            Type *base;          // The original (uninstantiated) struct/function type
            Type **args;         // Concrete type arguments (e.g. [i32, bool])
            size_t arg_count;    // Number of type arguments
            Type *concrete_type; // The monomorphized TYPE_STRUCT or TYPE_FUNCTION type
        } generic_inst;
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
    Type *t_usize, *t_isize;
    Type *t_str;

    // Pre-interned common property names
    InternResult *kw_len;

    // Cache for monomorphized generic instances
    // Key: MonoKey* (template + type args), Value: Type*
    HashMap *generic_inst_cache;
    
    // Registry for generic impl blocks
    // Key: base Type* (generic struct type), Value: DynArray* of AstImplDeclaration*
    HashMap *impl_registry;
} TypeStore;

TypeStore *typestore_create(Arena *arena, DenseArenaInterner *identifiers, DenseArenaInterner *keywords);
InternResult *intern_type(TypeStore *ts, Type *prototype);

void register_primitives_to_scope(TypeStore *ts, Scope *universe_scope, DenseArenaInterner *keywords);
void register_intrinsics(TypeStore *ts, Scope *global_scope, DenseArenaInterner *identifiers);

// Returns true for i8, u8, i16, i32, i64, etc.
bool type_is_integer(Type *t);
bool type_is_unsigned(Type *t);
// Returns true for f32, f64
bool type_is_float(Type *t);
// Returns true if the type is a boolean
bool type_is_bool(Type *t);
// Returns true if the type is a char
bool type_is_char(Type *t);

// --- Type construction helpers (intern + return canonical Type*) ---
Type *make_pointer_type(TypeStore *ts, Type *base);
Type *make_array_type(TypeStore *ts, Type *base, int64_t size);
Type *make_slice_type(TypeStore *ts, Type *base);
Type *make_function_type(TypeStore *ts, Type *return_type, Type **params, size_t param_count);
Type *make_generic_inst_type(TypeStore *ts, Type *base, Type **args, size_t arg_count);

// --- Generic type substitution ---
// Recursively substitutes TYPE_TYPEVAR nodes with their concrete bindings.
// bindings: HashMap mapping InternResult* (typevar name key) -> Type* (concrete type)
Type *make_typevar_type(TypeStore *ts, InternResult *name, int index);
Type *type_substitute(TypeStore *ts, Type *t, HashMap *bindings);
