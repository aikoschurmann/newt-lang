#include "scope.h"
#include <stdio.h>
#include <string.h>

Scope *scope_create(Arena *arena, Scope *parent, int identifier_count, int kind) {
    Scope *scope = arena_calloc(arena, sizeof(Scope));
    if (!scope) return NULL;

    scope->symbols = arena_calloc(arena, sizeof(Symbol *) * identifier_count);
    if (!scope->symbols) {
        return NULL;
    }

    scope->depth = parent ? parent->depth + 1 : 0;
    scope->parent = parent;
    scope->arena = arena;  // Set the arena field
    scope->capacity = identifier_count; 
    scope->kind = kind;

    return scope;
}

Symbol *scope_define_symbol(Scope *scope, InternResult *rec, Type *type, SymbolValue kind, const char *filename, bool is_pub, AstNode *decl_node) {
    if (!scope || !rec || !type) {
        return NULL;
    }

    if (rec->entry->dense_index >= scope->capacity) {
        // Grow the scope's symbol array
        // Strategy: Double capacity until it fits the new index
        size_t new_cap = scope->capacity ? scope->capacity * 2 : 16;
        while (new_cap <= rec->entry->dense_index) {
            new_cap *= 2;
        }
        
        // Allocate new array (zeroed)
        Symbol **new_symbols = arena_calloc(scope->arena, sizeof(Symbol *) * new_cap);
        if (!new_symbols) return NULL;
        
        // Copy existing symbols
        if (scope->symbols) {
            memcpy(new_symbols, scope->symbols, sizeof(Symbol *) * scope->capacity);
        }
        
        scope->symbols = new_symbols;
        scope->capacity = new_cap;
    }

    // Check for existing symbol in current scope
    if (scope_lookup_symbol_local(scope, rec)) {
        return NULL; // Symbol already defined in this scope
    }

    Symbol *symbol = arena_calloc(scope->arena, sizeof(Symbol));
    if (!symbol) {
        return NULL;
    }

    symbol->name_rec = rec;
    symbol->type = type;
    symbol->kind = kind;
    symbol->flags = SYMBOL_FLAG_NONE;
    symbol->filename = filename;
    symbol->is_pub = is_pub;
    symbol->decl_node = decl_node;

    scope->symbols[rec->entry->dense_index] = symbol;
    scope->symbol_count++;
    
    return symbol;
}

Symbol *scope_lookup_symbol_local(Scope *scope, InternResult *rec) {
    if (!scope || !rec) return NULL;
    if (rec->entry->dense_index >= scope->capacity) return NULL;
    return scope->symbols[rec->entry->dense_index];
}

Symbol *scope_lookup_symbol(Scope *scope, InternResult *rec, const char *caller_filename) {
    if (!scope || !rec) return NULL;
    
    // Detect if the key is a Keyword (meta != 0) or Identifier (meta == 0)
    bool is_keyword_key = (rec->entry->meta != NULL);

    Scope *current = scope;
    while (current) {
        bool is_keyword_scope = (current->kind == SCOPE_KEYWORDS);
        
        if (is_keyword_key == is_keyword_scope) {
             Symbol *symbol = scope_lookup_symbol_local(current, rec);
             if (symbol) {
                 // Visibility check:
                 // 1. Local symbols (depth > 0) are always visible in their children
                 // 2. Global symbols (depth == 0) must be 'pub' OR from the same file
                 if (current->depth == 0 && !symbol->is_pub && symbol->filename && caller_filename) {
                     if (strcmp(symbol->filename, caller_filename) != 0) {
                         // Symbol is private and from another module
                         // Continue searching (to allow shadowing or just fail later)
                         current = current->parent;
                         continue;
                     }
                 }
                 return symbol;
             }
        }
        
        current = current->parent;
    }
    return NULL;
}

Symbol *symbol_set_value_int(Symbol *symbol, int value){
    if (!symbol) return NULL;
    symbol->kind = SYMBOL_VALUE_INT;
    symbol->value.int_val = (int64_t)value;
    return symbol;
}
Symbol *symbol_set_value_float(Symbol *symbol, float value){
    if (!symbol) return NULL;
    symbol->kind = SYMBOL_VALUE_FLOAT;
    symbol->value.float_val = (double)value;
    return symbol;
}
Symbol *symbol_set_value_bool(Symbol *symbol, bool value){
    if (!symbol) return NULL;
    symbol->kind = SYMBOL_VALUE_BOOL;
    symbol->value.bool_val = value;
    return symbol;
}

size_t scope_get_symbol_count(Scope *scope){
    if (!scope) return 0;
    return scope->symbol_count;
}

void scope_set_flags(Scope *scope, InternResult *rec, int flags){
    if (!scope || !rec) return;

    Symbol *symbol = scope_lookup_symbol(scope, rec, NULL);
    if (symbol) {
        symbol->flags |= flags;
    }
}

void scope_check_unused_symbols(Scope *scope){
    if (!scope) return;

    for (size_t i = 0; i < scope->symbol_count; i++) {
        Symbol *symbol = scope->symbols[i];
        if (symbol && !(symbol->flags & SYMBOL_FLAG_USED)) {
            printf("Warning: Unused symbol '%s'\n", symbol->name_rec->key ? (char*)symbol->name_rec->key : "(unknown)");
        }
    }
}

void scope_print_symbols(Scope *scope, int indent) {
    if (!scope) return;

    for (size_t i = 0; i < scope->symbol_count; ++i) {
        Symbol *s = scope->symbols[i];
        if (!s) continue;
        const char *name = s->name_rec && s->name_rec->key ? (char*)s->name_rec->key : "(unknown)";
        // For now just print "(type present)" because we don't include type.h
        const char *type_name = s->type ? "(type present)" : "(none)";
        printf("%*s- Symbol: '%s', type: %s, flags: 0x%02x\n", indent, "", name, type_name, (unsigned)s->flags);
    }
}

void scope_print_hierarchy(Scope *scope) {
    if (!scope) return;

    int indent = 0;
    Scope *cur = scope;
    while (cur) {
        printf("%*s- Scope (depth %zu):\n", indent, "", cur->depth);
        scope_print_symbols(cur, indent + 2);
        cur = cur->parent;
        indent += 2;
    }
}