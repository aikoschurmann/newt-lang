#include <string.h>
#include "type.h"
#include "sema/intrinsics.h"
#include "datastructures/scope.h"

// FNV-1a constants for 64-bit systems
#define FNV_OFFSET 0xcbf29ce484222325ULL
#define FNV_PRIME  0x100000001b3ULL


// Combine two hash values using FNV-1a mixing
static inline size_t hash_combine(size_t seed, size_t value) {
    seed ^= value;
    seed *= FNV_PRIME;
    return seed;
}

/*
* HashMap expects void pointer input for hashing.
* However, the interner stores keys as Slice* objects, so we need to
* extract the actual Type* from the Slice.
*/
static size_t type_hasher(void *ptr) {

    const Slice *slice = (const Slice*)ptr;
    const Type *type = (const Type*)slice->ptr;
    
    // Start with a basis
    size_t h = FNV_OFFSET;

    // Mix the Kind
    h = hash_combine(h, (size_t)type->kind);

    switch (type->kind) {
        case TYPE_PRIMITIVE:
            h = hash_combine(h, (size_t)type->as.primitive);
            break;

        case TYPE_POINTER:
            h = hash_combine(h, type->as.ptr.base->cached_hash);
            break;

        case TYPE_ARRAY:
            // Mix Child's Hash + Size
            h = hash_combine(h, type->as.array.base->cached_hash);
            h = hash_combine(h, (size_t)type->as.array.size);
            break;

        case TYPE_SLICE:
            // Mix Child's Hash
            h = hash_combine(h, type->as.slice.base->cached_hash);
            break;

        case TYPE_FUNCTION:
            // Mix Return Type Hash
            h = hash_combine(h, type->as.func.return_type->cached_hash);

            // Mix Param Count
            h = hash_combine(h, (size_t)type->as.func.param_count);

            // Mix Hash of every parameter
            for (size_t i = 0; i < type->as.func.param_count; i++) {
                h = hash_combine(h, type->as.func.params[i]->cached_hash);
            }
            break;

        case TYPE_TYPEVAR:
            h = hash_combine(h, (size_t)(uintptr_t)type->as.typevar.name);
            h = hash_combine(h, (size_t)type->as.typevar.index);
            break;

        case TYPE_GENERIC_INST:
            h = hash_combine(h, type->as.generic_inst.base->cached_hash);
            h = hash_combine(h, (size_t)type->as.generic_inst.arg_count);
            for (size_t i = 0; i < type->as.generic_inst.arg_count; i++) {
                h = hash_combine(h, type->as.generic_inst.args[i]->cached_hash);
            }
            break;

        default:
            break;
    }

    return h;
}


/*
 * Type Comparator for DenseArenaInterner
 * - Returns 0 if types are equal, 1 (or non-zero) otherwise.
 * - Assumes 'data' is a Type struct (or compatible layout).
 * - Note: The interner passes keys as Slice* (pointer to wrapper), so we access ->ptr.
 *   This wrapper is used because the Interner is generic and stores keys + lengths.
 */
static int type_comparator(void *a, void *b) {
    const Slice *slice_a = (const Slice*)a;
    const Slice *slice_b = (const Slice*)b;
    const Type *ta = (const Type*)slice_a->ptr;
    const Type *tb = (const Type*)slice_b->ptr;

    // 1. Trivial Check (Same memory location)
    if (ta == tb) return 0;

    // 2. Kind Check
    if (ta->kind != tb->kind) return 1;

    // 3. Structural Check
    switch (ta->kind) {
        case TYPE_PRIMITIVE:
            return (ta->as.primitive == tb->as.primitive) ? 0 : 1;

        case TYPE_POINTER:
            // We only compare the 'target' pointer addresses.
            // Since 'target' is already interned, ta->target == tb->target
            // implies they are the exact same type. We do NOT need to recurse.
            return (ta->as.ptr.base == tb->as.ptr.base) ? 0 : 1;

        case TYPE_ARRAY:
            // Compare size and element type pointer
            if (ta->as.array.size != tb->as.array.size) return 1;
            return (ta->as.array.base == tb->as.array.base) ? 0 : 1;

        case TYPE_SLICE:
            // Compare element type pointer
            return (ta->as.slice.base == tb->as.slice.base) ? 0 : 1;

        case TYPE_FUNCTION:
            // Compare Return Type (pointer check)
            if (ta->as.func.return_type != tb->as.func.return_type) return 1;
            // Compare Param Count
            if (ta->as.func.param_count != tb->as.func.param_count) return 1;

            // Compare Param Array
            // Since params is an array of pointers (Type**), and those pointers
            // are canonical, we can just memcmp the array of pointers.
            // This compares [ptr1, ptr2, ptr3] vs [ptr1, ptr2, ptr3].
            if (ta->as.func.param_count > 0) {
                return memcmp(ta->as.func.params, tb->as.func.params, 
                              ta->as.func.param_count * sizeof(Type*));
            }
            return 0;
        
        // Future: Structs
        // case TYPE_STRUCT: return (ta->as.structure.name == tb->as.structure.name) ? 0 : 1;

        case TYPE_TYPEVAR:
            if (ta->as.typevar.name != tb->as.typevar.name) return 1;
            return (ta->as.typevar.index == tb->as.typevar.index) ? 0 : 1;

        case TYPE_GENERIC_INST:
            if (ta->as.generic_inst.base != tb->as.generic_inst.base) return 1;
            if (ta->as.generic_inst.arg_count != tb->as.generic_inst.arg_count) return 1;
            if (ta->as.generic_inst.arg_count > 0) {
                return memcmp(ta->as.generic_inst.args, tb->as.generic_inst.args,
                              ta->as.generic_inst.arg_count * sizeof(Type*));
            }
            return 0;

        default:
            return 1; // Not equal
    }
}


static void *type_copy_func(Arena *arena, const void *data, size_t len) {
    if (!arena || !data) return NULL;
    
    // The input 'data' is the pointer we passed to `intern` as `slice->ptr`
    // In `intern_type`, we pass `slice.ptr = (const char*)prototype`.
    // So `data` here IS `Type *prototype`.
    const Type *src = (const Type*)data;
    
    Type *copy = arena_alloc(arena, sizeof(Type));
    if (!copy) return NULL;
    
    // Copy the basic structure
    memcpy(copy, src, sizeof(Type));

    // Deep copy extra data (like parameter arrays) into the arena.
    // The prototype's pointers often point to stack/temporary memory, so the
    // interned instance must allocate its own persistent copy of that data.
    // We do this here (on intern) rather than during resolution to avoid 
    // allocating memory for types that already exist in the interner.
    if (src->kind == TYPE_FUNCTION && src->as.func.param_count > 0) {
        size_t params_size = sizeof(Type*) * src->as.func.param_count;
        Type **new_params = arena_alloc(arena, params_size);
        if (new_params) {
            memcpy(new_params, src->as.func.params, params_size);
            copy->as.func.params = new_params;
        }
    }

    if (src->kind == TYPE_GENERIC_INST && src->as.generic_inst.arg_count > 0) {
        size_t args_size = sizeof(Type*) * src->as.generic_inst.arg_count;
        Type **new_args = arena_alloc(arena, args_size);
        if (new_args) {
            memcpy(new_args, src->as.generic_inst.args, args_size);
            copy->as.generic_inst.args = new_args;
        }
    }
    
    // The interner expects us to return the pointer to the canonical data.
    // When we use `intern_type`, we set `slice.ptr` to the Type*.
    // The interner then allocates a persistent Slice, calls this function to get
    // the canonical data pointer, and sets the persistent Slice's ptr to it.
    //
    // However, our `type_hasher` and `type_comparator` behave as if the `key`
    // passed to them is a `Slice*`, and they dereference `Slice->ptr` to get `Type*`.
    //
    // The `DenseArenaInterner` implementation does:
    //    key_slice->ptr = interner->copy_func(...)
    //
    // So here we should return `Type*` (the canonical copy of the type struct).
    // The interner will put that `Type*` into `key_slice->ptr`.
    // Then `key_slice` (which is `Slice*`) is passed to `type_hasher(void *key)`.
    // Inside `type_hasher`, it casts `key` to `Slice*`, then reads `->ptr` to get `Type*`.
    //
    // This matches:
    //   type_copy_func returns Type*
    //   interner stores Type* in slice->ptr
    //   hasher receives Slice*, reads ptr -> Type*
    
    return copy;
}

InternResult *intern_type(TypeStore *ts, Type *prototype) {
    if (!ts || !prototype) return NULL;

    // Pre-calculate hash for the prototype (required by interner)
    prototype->cached_hash = type_hasher(&(Slice){.ptr = (char*)prototype, .len = sizeof(Type)});

    Slice slice = { .ptr = (const char*)prototype, .len = sizeof(Type) };
    InternResult *result = intern(ts->type_interner, &slice, NULL);
    if (!result) return NULL;

    return result;
}

// Helper to register a primitive
static void register_prim(TypeStore *ts, DenseArenaInterner *ids, const char *name, Type *t) {
    // 1. Intern the string to get the canonical Key pointer
    Slice s = { .ptr = (char*)name, .len = strlen(name) };
    InternResult *res = intern(ids, &s, NULL);
    
    // 2. Map Key -> Type*
    if (res && res->key) {
        // cast res->key via uintptr_t to uint64_t for hashmap key
        hashmap_put(ts->primitive_registry, res->key, t, ptr_hash, ptr_cmp);
    }
}

static Type *create_primitive(TypeStore *ts, PrimitiveKind kind) {
    Type proto = {0};
    proto.kind = TYPE_PRIMITIVE;
    proto.as.primitive = kind;
    // Pre-calculate hash for the prototype (required by interner)
    proto.cached_hash = type_hasher(&(Slice){.ptr = (char*)&proto, .len = sizeof(Type)});
    
    InternResult *res = intern(ts->type_interner, &(Slice){.ptr = (char*)&proto, .len = sizeof(Type)}, NULL);
    if (!res) return NULL;
    return (Type*)((Slice*)res->key)->ptr;
}

TypeStore *typestore_create(Arena *arena, DenseArenaInterner *identifiers, DenseArenaInterner *keywords) {
    if (!arena) return NULL;

    TypeStore *ts = arena_alloc(arena, sizeof(TypeStore));
    if (!ts) return NULL;
    
    ts->arena = arena;
    
    // Create Hashmap for the interner
    // 64 buckets is fine for initial set of types. It will grow.
    HashMap *tm = hashmap_create(arena, 64);
    if (!tm) return NULL;

    // Create Interner
    ts->type_interner = intern_table_create(tm, arena, type_copy_func, type_hasher, type_comparator);
    if (!ts->type_interner) return NULL;

    ts->primitive_registry = hashmap_create(arena, 64);
    ts->generic_inst_cache = hashmap_create(arena, 32);
    ts->impl_registry = hashmap_create(arena, 32);

    // Create canonical primitives
    ts->t_i8  = create_primitive(ts, PRIM_I8);
    ts->t_i16 = create_primitive(ts, PRIM_I16);
    ts->t_i32 = create_primitive(ts, PRIM_I32);
    ts->t_i64 = create_primitive(ts, PRIM_I64);

    ts->t_u8  = create_primitive(ts, PRIM_U8);
    ts->t_u16 = create_primitive(ts, PRIM_U16);
    ts->t_u32 = create_primitive(ts, PRIM_U32);
    ts->t_u64 = create_primitive(ts, PRIM_U64);

    ts->t_f32 = create_primitive(ts, PRIM_F32);
    ts->t_f64 = create_primitive(ts, PRIM_F64);

    ts->t_bool = create_primitive(ts, PRIM_BOOL);
    ts->t_char = create_primitive(ts, PRIM_CHAR);
    
    // Create canonical str type (*char)
    Type str_proto = { .kind = TYPE_POINTER, .as.ptr.base = ts->t_char };
    InternResult *str_res = intern_type(ts, &str_proto);
    ts->t_str = (Type*)((Slice*)str_res->key)->ptr;
    
    // Create canonical void type
    Type void_proto = { .kind = TYPE_VOID };
    InternResult *void_res = intern_type(ts, &void_proto);
    ts->t_void = (Type*)((Slice*)void_res->key)->ptr;


    // 1. Manually create the void* type prototype
    Type void_ptr_proto = { .kind = TYPE_POINTER, .as.ptr.base = ts->t_void };

    // 2. Intern it to get the canonical Type*
    InternResult *vp_res = intern_type(ts, &void_ptr_proto);
    ts->t_void_ptr = (Type*)((Slice*)vp_res->key)->ptr;

   

    // Register KEYWORDS (lexed as TOK_I32 etc.)
    register_prim(ts, keywords, "i8",  ts->t_i8);
    register_prim(ts, keywords, "i16", ts->t_i16);
    register_prim(ts, keywords, "i32", ts->t_i32);
    register_prim(ts, keywords, "i64", ts->t_i64);
    register_prim(ts, keywords, "u8",  ts->t_u8);
    register_prim(ts, keywords, "u16", ts->t_u16);
    register_prim(ts, keywords, "u32", ts->t_u32);
    register_prim(ts, keywords, "u64", ts->t_u64);
    register_prim(ts, keywords, "f32", ts->t_f32);
    register_prim(ts, keywords, "f64", ts->t_f64);
    register_prim(ts, keywords, "bool", ts->t_bool);
    register_prim(ts, keywords, "char", ts->t_char);
    register_prim(ts, keywords, "str", ts->t_str);
    register_prim(ts, keywords, "void", ts->t_void);

    // Pre-intern "len" for O(1) field lookup on arrays/slices
    Slice len_slice = { .ptr = "len", .len = 3 };
    ts->kw_len = intern(identifiers, &len_slice, NULL);
    
    return ts;
}

void register_intrinsics(TypeStore *ts, Scope *global_scope, DenseArenaInterner *identifiers) {
    // 1. print(...)
    Slice print_slice = { .ptr = "print", .len = 5 };
    InternResult *print_res = intern(identifiers, &print_slice, NULL);
    Symbol *print_sym = scope_define_symbol(global_scope, print_res, ts->t_void, SYMBOL_VALUE_INTRINSIC, "<builtin>", true, NULL);
    if (print_sym) {
        print_sym->intrinsic_kind = INTRINSIC_PRINT;
    }

    // 2. println(...)
    Slice println_slice = { .ptr = "println", .len = 7 };
    InternResult *println_res = intern(identifiers, &println_slice, NULL);
    Symbol *println_sym = scope_define_symbol(global_scope, println_res, ts->t_void, SYMBOL_VALUE_INTRINSIC, "<builtin>", true, NULL);
    if (println_sym) {
        println_sym->intrinsic_kind = INTRINSIC_PRINT_NEWLINE;
    }
}

void register_primitives_to_scope(TypeStore *ts, Scope *universe_scope, DenseArenaInterner *keywords) {
    // 3. Register primitive types as symbols so they can be aliased and looked up
    const char* prims[] = {"i8", "i16", "i32", "i64", "u8", "u16", "u32", "u64", "f32", "f64", "bool", "char", "str", "void"};
    Type* ptypes[] = {ts->t_i8, ts->t_i16, ts->t_i32, ts->t_i64, ts->t_u8, ts->t_u16, ts->t_u32, ts->t_u64, ts->t_f32, ts->t_f64, ts->t_bool, ts->t_char, ts->t_str, ts->t_void};
    for (int i = 0; i < 14; i++) {
        Slice s = { .ptr = (char*)prims[i], .len = strlen(prims[i]) };
        InternResult *res = intern(keywords, &s, NULL);
        scope_define_symbol(universe_scope, res, ptypes[i], SYMBOL_VALUE_TYPE, "<builtin>", true, NULL);
    }
}

// --- Type construction helpers ---

Type *make_typevar_type(TypeStore *ts, InternResult *name, int index) {
    if (!ts || !name) return NULL;
    Type proto = { .kind = TYPE_TYPEVAR, .as.typevar = { .name = name, .index = index } };
    InternResult *res = intern_type(ts, &proto);
    if (!res) return NULL;
    return (Type*)((Slice*)res->key)->ptr;
}

Type *make_pointer_type(TypeStore *ts, Type *base) {
    if (!ts || !base) return NULL;
    Type proto = { .kind = TYPE_POINTER, .as.ptr.base = base };
    InternResult *res = intern_type(ts, &proto);
    if (!res) return NULL;
    return (Type*)((Slice*)res->key)->ptr;
}

Type *make_array_type(TypeStore *ts, Type *base, int64_t size) {
    if (!ts || !base) return NULL;
    Type proto = { .kind = TYPE_ARRAY, .as.array = { .base = base, .size = size } };
    InternResult *res = intern_type(ts, &proto);
    if (!res) return NULL;
    return (Type*)((Slice*)res->key)->ptr;
}

Type *make_slice_type(TypeStore *ts, Type *base) {
    if (!ts || !base) return NULL;
    Type proto = { .kind = TYPE_SLICE, .as.slice.base = base };
    InternResult *res = intern_type(ts, &proto);
    if (!res) return NULL;
    return (Type*)((Slice*)res->key)->ptr;
}

Type *make_function_type(TypeStore *ts, Type *return_type, Type **params, size_t param_count) {
    if (!ts || !return_type) return NULL;
    Type proto = { .kind = TYPE_FUNCTION, .as.func = { .return_type = return_type, .params = params, .param_count = param_count } };
    InternResult *res = intern_type(ts, &proto);
    if (!res) return NULL;
    return (Type*)((Slice*)res->key)->ptr;
}

// --- Generic caching infrastructure ---

typedef struct {
    Type *base;
    size_t arg_count;
    Type **args;
} MonoKey;

static size_t mono_key_hash(void *ptr) {
    MonoKey *key = (MonoKey*)ptr;
    size_t h = hash_combine(0, (size_t)(uintptr_t)key->base);
    h = hash_combine(h, key->arg_count);
    for (size_t i = 0; i < key->arg_count; i++) {
        h = hash_combine(h, (size_t)(uintptr_t)key->args[i]);
    }
    return h;
}

static int mono_key_cmp(void *a, void *b) {
    MonoKey *ka = (MonoKey*)a;
    MonoKey *kb = (MonoKey*)b;
    if (ka->base != kb->base) return 1;
    if (ka->arg_count != kb->arg_count) return 1;
    for (size_t i = 0; i < ka->arg_count; i++) {
        if (ka->args[i] != kb->args[i]) return 1;
    }
    return 0; // equal
}

Type *make_generic_inst_type(TypeStore *ts, Type *base, Type **args, size_t arg_count) {
    if (!ts || !base) return NULL;

    // Check the cache first
    MonoKey lookup_key = { .base = base, .args = args, .arg_count = arg_count };
    Type *cached = (Type*)hashmap_get(ts->generic_inst_cache, &lookup_key, mono_key_hash, mono_key_cmp);
    if (cached) return cached;

    // Build the TYPE_GENERIC_INST proto type
    Type proto = {0};
    proto.kind = TYPE_GENERIC_INST;
    proto.as.generic_inst.base = base;
    proto.as.generic_inst.args = args;
    proto.as.generic_inst.arg_count = arg_count;
    proto.as.generic_inst.concrete_type = NULL;

    InternResult *res = intern_type(ts, &proto);
    if (!res) return NULL;

    Type *inst_type = (Type*)((Slice*)res->key)->ptr;

    // Insert into cache
    MonoKey *persistent_key = arena_alloc(ts->arena, sizeof(MonoKey));
    persistent_key->base = base;
    persistent_key->arg_count = arg_count;
    persistent_key->args = arena_alloc(ts->arena, sizeof(Type*) * arg_count);
    memcpy(persistent_key->args, args, sizeof(Type*) * arg_count);

    inst_type->as.generic_inst.args = persistent_key->args;

    hashmap_put(ts->generic_inst_cache, persistent_key, inst_type, mono_key_hash, mono_key_cmp);

    return inst_type;
}

// --- Generic type substitution ---

Type *type_substitute(TypeStore *ts, Type *t, HashMap *bindings) {
    if (!t || !bindings) return t;

    switch (t->kind) {
        case TYPE_TYPEVAR: {
            Type *bound = hashmap_get(bindings, t->as.typevar.name->key, ptr_hash, ptr_cmp);
            return bound ? bound : t;
        }

        case TYPE_POINTER: {
            Type *sub_base = type_substitute(ts, t->as.ptr.base, bindings);
            if (sub_base == t->as.ptr.base) return t;
            return make_pointer_type(ts, sub_base);
        }

        case TYPE_ARRAY: {
            Type *sub_base = type_substitute(ts, t->as.array.base, bindings);
            if (sub_base == t->as.array.base) return t;
            return make_array_type(ts, sub_base, t->as.array.size);
        }

        case TYPE_SLICE: {
            Type *sub_base = type_substitute(ts, t->as.slice.base, bindings);
            if (sub_base == t->as.slice.base) return t;
            return make_slice_type(ts, sub_base);
        }

        case TYPE_FUNCTION: {
            Type *sub_ret = type_substitute(ts, t->as.func.return_type, bindings);
            bool changed = (sub_ret != t->as.func.return_type);

            Type **sub_params = NULL;
            if (t->as.func.param_count > 0) {
                for (size_t i = 0; i < t->as.func.param_count; i++) {
                    Type *subbed = type_substitute(ts, t->as.func.params[i], bindings);
                    if (subbed != t->as.func.params[i]) {
                        if (!changed && !sub_params) {
                            sub_params = arena_alloc(ts->arena, sizeof(Type*) * t->as.func.param_count);
                            for (size_t j = 0; j < i; j++) sub_params[j] = t->as.func.params[j];
                        }
                        changed = true;
                    }
                    if (sub_params) sub_params[i] = subbed;
                }
            }

            if (!changed) return t;
            return make_function_type(ts, sub_ret, sub_params ? sub_params : t->as.func.params, t->as.func.param_count);
        }

        case TYPE_GENERIC_INST: {
            bool changed = false;
            Type **sub_args = NULL;
            if (t->as.generic_inst.arg_count > 0) {
                for (size_t i = 0; i < t->as.generic_inst.arg_count; i++) {
                    Type *subbed = type_substitute(ts, t->as.generic_inst.args[i], bindings);
                    if (subbed != t->as.generic_inst.args[i]) {
                        if (!changed && !sub_args) {
                            sub_args = arena_alloc(ts->arena, sizeof(Type*) * t->as.generic_inst.arg_count);
                            for (size_t j = 0; j < i; j++) sub_args[j] = t->as.generic_inst.args[j];
                        }
                        changed = true;
                    }
                    if (sub_args) sub_args[i] = subbed;
                }
            }

            Type *sub_concrete = t->as.generic_inst.concrete_type ? type_substitute(ts, t->as.generic_inst.concrete_type, bindings) : NULL;
            if (sub_concrete != t->as.generic_inst.concrete_type) changed = true;

            if (!changed) return t;

            Type *new_inst = make_generic_inst_type(ts, t->as.generic_inst.base, sub_args ? sub_args : t->as.generic_inst.args, t->as.generic_inst.arg_count);
            if (new_inst) {
                new_inst->as.generic_inst.concrete_type = sub_concrete;
            }
            return new_inst;
        }

        default:
            return t;
    }
}

