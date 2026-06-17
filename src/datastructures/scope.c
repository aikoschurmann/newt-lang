#include "datastructures/scope.h"
#include "sema/type.h"
#include "core/module_loader.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

Scope *scope_create(Arena *arena, Scope *parent, int identifier_count, int kind) {
    Scope *scope = arena_calloc(arena, sizeof(Scope));
    if (!scope) return NULL;

    // A small initial capacity is fine since hashmap handles resizing and 
    // we no longer rely on dense indices to determine scope boundaries.
    scope->symbols = hashmap_create(arena, identifier_count > 0 ? identifier_count : 8);
    if (!scope->symbols) {
        return NULL;
    }

    dynarray_init_in_arena(&scope->symbols_list, arena, sizeof(Symbol *), identifier_count > 0 ? identifier_count / 4 : 8);

    scope->depth = parent ? parent->depth + 1 : 0;
    scope->parent = parent;
    scope->arena = arena;
    scope->kind = kind;
    scope->unit = NULL;

    return scope;
}

Symbol *scope_make_overload_set(Arena *arena, Symbol *first, Symbol *second) {
    Symbol *set = arena_calloc(arena, sizeof(Symbol));
    set->name_rec = first->name_rec;
    set->kind     = SYMBOL_OVERLOAD_SET;
    set->is_pub   = first->is_pub || second->is_pub;
    set->overloads = arena_alloc(arena, sizeof(DynArray));
    dynarray_init_in_arena(set->overloads, arena, sizeof(Symbol*), 4);
    
    dynarray_push_value(set->overloads, &first);
    dynarray_push_value(set->overloads, &second);
    return set;
}

bool scope_overload_set_add(Symbol *set, Symbol *fn_sym, Arena *arena) {
    if (!set || set->kind != SYMBOL_OVERLOAD_SET || !fn_sym) return false;

    // Reject identical signatures (same param types).
    for (size_t i = 0; i < set->overloads->count; i++) {
        Symbol *existing = *(Symbol**)dynarray_get(set->overloads, i);
        Type *et = existing->type;
        Type *nt = fn_sym->type;
        
        // If types are not resolved yet (Pass 1), we compare decl_nodes to avoid duplicates of the exact same node
        if (!et || !nt) {
            if (existing->decl_node == fn_sym->decl_node) return false;
            continue;
        }

        if (et->kind != TYPE_FUNCTION || nt->kind != TYPE_FUNCTION) continue;
        if (et->as.func.param_count != nt->as.func.param_count) continue;
        
        bool same = true;
        for (size_t p = 0; p < et->as.func.param_count; p++) {
            if (et->as.func.params[p] != nt->as.func.params[p]) { 
                same = false; 
                break; 
            }
        }
        if (same) return false; // duplicate signature
    }
    
    dynarray_push_value(set->overloads, &fn_sym);
    if (fn_sym->is_pub) set->is_pub = true;
    return true;
}

Symbol *scope_define_symbol(Scope *scope, InternResult *rec, Type *type, SymbolValue kind, const char *filename, bool is_pub, AstNode *decl_node) {
    if (!scope || !rec) {
        return NULL;
    }
    
    Symbol *existing = scope_lookup_symbol_local(scope, rec);
    if (existing) {
        // Handle function overloading
        if (kind == SYMBOL_VALUE_FUNCTION && (existing->kind == SYMBOL_VALUE_FUNCTION || existing->kind == SYMBOL_OVERLOAD_SET)) {
            
            // GUARDS: Forbid overloading main or @link functions
            bool is_main = false;
            Slice *name = (Slice*)rec->key;
            if (name->len == 4 && strncmp(name->ptr, "main", 4) == 0) is_main = true;

            bool is_link = false;
            if (decl_node && decl_node->node_type == AST_FUNCTION_DECLARATION) {
                if (decl_node->data.function_declaration.link_name) is_link = true;
            }

            // Also check existing symbol if it's a single function
            if (existing->kind == SYMBOL_VALUE_FUNCTION && existing->decl_node && 
                existing->decl_node->node_type == AST_FUNCTION_DECLARATION) {
                if (existing->decl_node->data.function_declaration.link_name) is_link = true;
            }

            if (is_main || is_link) {
                return NULL; 
            }

            Symbol *candidate = arena_calloc(scope->arena, sizeof(Symbol));
            if (!candidate) return NULL;

            candidate->name_rec  = rec;
            candidate->type      = type;
            candidate->kind      = SYMBOL_VALUE_FUNCTION;
            candidate->filename  = filename;
            candidate->is_pub    = is_pub;
            candidate->decl_node = decl_node;
            candidate->flags     = SYMBOL_FLAG_NONE;

            if (existing->kind == SYMBOL_VALUE_FUNCTION) {
                // First collision -> upgrade to set
                Symbol *set = scope_make_overload_set(scope->arena, existing, candidate);
                hashmap_put(scope->symbols, rec->key, set, ptr_hash, ptr_cmp);
                
                // Replace in symbols_list
                for (size_t i = 0; i < scope->symbols_list.count; i++) {
                    Symbol **slot = (Symbol**)dynarray_get(&scope->symbols_list, i);
                    if (*slot == existing) { 
                        *slot = set; 
                        break; 
                    }
                }
                return candidate;
            } else {
                // Already a set -> just append
                if (!scope_overload_set_add(existing, candidate, scope->arena)) {
                    return NULL; // duplicate signature
                }
                return candidate;
            }
        }
        return NULL; // Symbol already defined in this scope and not an overloadable function
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

    // Use ptr_hash and ptr_cmp since rec->key is guaranteed to be a unique, interned Slice*
    hashmap_put(scope->symbols, rec->key, symbol, ptr_hash, ptr_cmp);
    
    dynarray_push_value(&scope->symbols_list, &symbol);

    return symbol;
}

Symbol *scope_lookup_symbol_local(Scope *scope, InternResult *rec) {
    if (!scope || !rec || !rec->key) return NULL;
    return (Symbol*)hashmap_get(scope->symbols, rec->key, ptr_hash, ptr_cmp);
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
    return scope->symbols_list.count;
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
            // printf("Warning: Unused symbol '%s'\n", symbol->name_rec->key ? (char*)symbol->name_rec->key : "(unknown)");
        }
    }
}

void scope_print_symbols(Scope *scope, int indent) {
    if (!scope) return;

    for (size_t i = 0; i < scope->symbols_list.count; ++i) {
        Symbol *s = *(Symbol**)dynarray_get(&scope->symbols_list, i);
        if (!s) continue;
        const char *name = s->name_rec && s->name_rec->key ? (char*)s->name_rec->key : "(unknown)";
        
        if (s->kind == SYMBOL_OVERLOAD_SET) {
            printf("%*s- Symbol: '%s' (OVERLOAD SET, count: %zu)\n", indent, "", name, s->overloads->count);
            for (size_t j = 0; j < s->overloads->count; j++) {
                Symbol *cand = *(Symbol**)dynarray_get(s->overloads, j);
                printf("%*s  [%zu] type: %s, flags: 0x%02x\n", indent, "", j, cand->type ? "(present)" : "(none)", (unsigned)cand->flags);
            }
        } else {
            const char *type_name = s->type ? "(type present)" : "(none)";
            printf("%*s- Symbol: '%s', type: %s, flags: 0x%02x\n", indent, "", name, type_name, (unsigned)s->flags);
        }
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
