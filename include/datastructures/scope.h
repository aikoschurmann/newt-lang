#pragma once

#include <stddef.h>
#include <stdbool.h>
#include "arena.h"
#include "dynamic_array.h"
#include "dense_arena_interner.h"
#include "hash_map.h"

// Forward declarations
typedef struct Scope Scope;
typedef struct Symbol Symbol;
typedef struct AstNode AstNode;
typedef struct CompilationUnit CompilationUnit;

// Symbol kinds for better type safety and debugging
typedef enum {
    SYMBOL_VALUE_INT,
    SYMBOL_VALUE_FLOAT,
    SYMBOL_VALUE_BOOL,
    SYMBOL_VALUE_FUNCTION,
    SYMBOL_VALUE_TYPE,   // Added for type names (i32, f64, structs)
    SYMBOL_VALUE_INTRINSIC, // Added for compiler built-ins
    SYMBOL_VARIABLE,      // General runtime variable
    SYMBOL_VALUE_MODULE,   // Added for imported modules
    SYMBOL_VALUE_NAMESPACE, // Added for synthetic namespace path components
    SYMBOL_VALUE_ALIAS,    // Added for local aliases
    SYMBOL_OVERLOAD_SET    // Added for function overloading
} SymbolValue;



typedef struct Type Type; // forward declaration


typedef enum {
    SYMBOL_FLAG_NONE = 0,
    SYMBOL_FLAG_CONST = 1 << 0,          // Was 'int is_const'
    SYMBOL_FLAG_COMPUTED_VALUE = 1 << 1, // Was 'int has_const_value'
    SYMBOL_FLAG_USED = 1 << 2,
    SYMBOL_FLAG_INITIALIZED = 1 << 3,
    SYMBOL_FLAG_COMPUTING = 1 << 4       // Recursion guard
} SymbolFlags;

typedef struct Symbol {
    InternResult *name_rec; 
    Type *type;
    Span span;
    const char *filename; // Which module defined this
    bool is_pub;         // Visibility

    AstNode *decl_node; // AST node that defined this symbol
    Scope *module_scope; // Pointer to the global scope of an imported module
    struct Symbol *target_symbol; // For aliases: pointer to the actual symbol

    SymbolValue kind;   // 4 bytes
    SymbolFlags flags;  // 4 bytes (Merged booleans here!)
    int intrinsic_kind; // Added for intrinsics

    // Raw 64-bit storage (8 bytes).
    // We implicitly know the type from 'this->type'.
    union {
        int64_t int_val;
        double float_val;
        bool bool_val;
        // void *ptr_val; // For strings if you implement them
    } value; 

    // For function overloading
    DynArray *overloads; // DynArray<Symbol*>

} Symbol;

typedef struct Scope {
    HashMap *symbols; // Open-addressed hash map for symbols
    DynArray symbols_list; // For efficient iteration

    Scope *parent;
    Arena *arena;                // Arena for memory allocation
    
    // Scope metadata
    size_t depth;                // Nesting depth (0 = global)
    
    // Scope kind to distinguish symbol namespaces
    int kind; // ScopeKind

    CompilationUnit *unit;

} Scope;

typedef enum {
    SCOPE_IDENTIFIERS = 0,
    SCOPE_KEYWORDS = 1
} ScopeKind;

// Scope management functions
// kind: 0 for identifiers (default), 1 for keywords (universe primitive types)
Scope *scope_create(Arena *arena, Scope *parent, int identifier_count, int kind);

// Symbol management
Symbol *scope_define_symbol(Scope *scope, InternResult *name, Type *type, SymbolValue kind, const char *filename, bool is_pub, AstNode *decl_node);
Symbol *scope_lookup_symbol(Scope *scope, InternResult *name, const char *caller_filename);
Symbol *scope_lookup_symbol_local(Scope *scope, InternResult *name);

// Overload set helpers
Symbol *scope_make_overload_set(Arena *arena, Symbol *first, Symbol *second);
bool    scope_overload_set_add(Symbol *set, Symbol *fn_sym, Arena *arena);

// Symbol modification
Symbol *symbol_set_value_int(Symbol *symbol, int value);
Symbol *symbol_set_value_float(Symbol *symbol, float value);
Symbol *symbol_set_value_bool(Symbol *symbol, bool value);


// Scope utilities
size_t scope_get_symbol_count(Scope *scope);
void scope_set_flags(Scope *scope, InternResult *rec, int flags);
void scope_check_unused_symbols(Scope *scope);

// Debugging and introspection
void scope_print_symbols(Scope *scope, int indent);
void scope_print_hierarchy(Scope *scope);
