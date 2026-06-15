#include "scope.h"
#include "core/module_loader.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

Scope *scope_create(Arena *arena, Scope *parent, int identifier_count, int kind) {
    Scope *scope = arena_calloc(arena, sizeof(Scope));
    if (!scope) return NULL;

    scope->symbols = arena_calloc(arena, sizeof(Symbol *) * identifier_count);
    if (!scope->symbols) {
        return NULL;
    }

    dynarray_init_in_arena(&scope->symbols_list, arena, sizeof(Symbol *), identifier_count > 0 ? identifier_count / 4 : 8);

    scope->depth = parent ? parent->depth + 1 : 0;
    scope->parent = parent;
    scope->arena = arena;
    scope->capacity = identifier_count; 
    scope->kind = kind;
    scope->unit = NULL;

    return scope;
}

Symbol *scope_define_symbol(Scope *scope, InternResult *rec, Type *type, SymbolValue kind, const char *filename, bool is_pub, AstNode *decl_node) {
    if (!scope || !rec) {
        return NULL;
    }
    
    // Type is optional (e.g. during Pass 1 name registration)
    // We used to restrict this to modules, but now allow it for all kinds.

    if ((size_t)rec->entry->dense_index >= scope->capacity) {
        size_t new_cap = scope->capacity ? scope->capacity * 2 : 16;
        while (new_cap <= (size_t)rec->entry->dense_index) {
            new_cap *= 2;
        }
        
        Symbol **new_symbols = arena_calloc(scope->arena, sizeof(Symbol *) * new_cap);
        if (!new_symbols) return NULL;
        
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
    symbol->module_scope = NULL;

    scope->symbols[rec->entry->dense_index] = symbol;
    scope->symbol_count++;
    
    dynarray_push_value(&scope->symbols_list, &symbol);

    return symbol;
}

Symbol *scope_lookup_symbol_local(Scope *scope, InternResult *rec) {
    if (!scope || !rec) return NULL;
    if ((size_t)rec->entry->dense_index >= scope->capacity) return NULL;
    return scope->symbols[rec->entry->dense_index];
}

Symbol *scope_lookup_symbol(Scope *scope, InternResult *rec, const char *caller_filename) {
    if (!scope || !rec) return NULL;
    
    bool is_keyword_key = (rec->entry->meta != NULL);

    Scope *current = scope;
    while (current) {
        bool is_keyword_scope = (current->kind == SCOPE_KEYWORDS);
        
        if (is_keyword_key == is_keyword_scope) {
             Symbol *symbol = scope_lookup_symbol_local(current, rec);
             if (symbol) {
                 // 0. Transparent Alias Resolution
                 while (symbol && symbol->kind == SYMBOL_VALUE_ALIAS) {
                     symbol = symbol->target_symbol;
                 }
                 if (!symbol) {
                      current = current->parent;
                      continue;
                 }

                 // Visibility check:
                 // 1. If this is the caller's own scope (e.g. its module global scope or a local block), everything is visible.
                 if (current->unit && caller_filename && strcmp(current->unit->absolute_path, caller_filename) == 0) {
                     return symbol;
                 }
                 
                 // 2. If it's a local scope (depth > 0) AND it doesn't belong to a specific unit (like a function block), it's visible.
                 if (current->depth > 0 && !current->unit) {
                     return symbol;
                 }

                 // 3. Otherwise, it must be 'pub'.
                 if (symbol->is_pub) {
                     return symbol;
                 }

                 // Special case: if no unit is attached (like Universe), it's public.
                 if (!current->unit) return symbol;

                 // Not visible.
                 current = current->parent;
                 continue;
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

    for (size_t i = 0; i < scope->symbols_list.count; i++) {
        Symbol *symbol = *(Symbol**)dynarray_get(&scope->symbols_list, i);
        if (symbol && !(symbol->flags & SYMBOL_FLAG_USED)) {
            printf("Warning: Unused symbol '%s'\n", symbol->name_rec->key ? (char*)symbol->name_rec->key : "(unknown)");
        }
    }
}

void scope_print_symbols(Scope *scope, int indent) {
    if (!scope) return;

    for (size_t i = 0; i < scope->symbols_list.count; ++i) {
        Symbol *s = *(Symbol**)dynarray_get(&scope->symbols_list, i);
        if (!s) continue;
        const char *name = s->name_rec && s->name_rec->key ? (char*)s->name_rec->key : "(unknown)";
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
