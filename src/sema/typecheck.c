#include "sema/typecheck.h"
#include "sema/type_coerce.h"
#include "sema/type_utils.h"
#include "sema/symbol_utils.h"
#include "sema/typecheck_expr.h"
#include "core/utils.h"
#include "datastructures/dynamic_array.h"
#include "codegen/codegen_utils.h"
#include <string.h>
#include <stdlib.h>
#include <stdio.h>

// Forward declarations
static void check_statement(TypeCheckContext *ctx, Scope *scope, AstNode *stmt, Type *return_type);
static void check_block(TypeCheckContext *ctx, Scope *parent, AstNode *block_node, Type *return_type, bool create_new_scope);

static void validate_allocator_struct(TypeCheckContext *ctx, Scope *global_scope) {
    // 1. Look up 'Allocator' type in global scope
    Slice name = { .ptr = "Allocator", .len = 9 };
    InternResult *res = intern_peek(ctx->identifiers, &name);
    if (!res) return; // Allocator not defined, user code error elsewhere

    Symbol *sym = scope_lookup_symbol_local(global_scope, res);
    if (!sym || sym->kind != SYMBOL_VALUE_TYPE) return; 

    // Only validate the allocator in the file it was declared
    if (sym->decl_node && sym->decl_node->filename && ctx->filename) {
        if (strcmp(sym->decl_node->filename, ctx->filename) != 0) return;
    }

    Type *t = sym->type;
    if (!t || t->kind != TYPE_STRUCT) return;

    // 2. Validate field count and names
    if (t->as.struct_type.field_count != 3) {
        TypeError err = { .kind = TE_INCOMPLETE_TYPE, .span = sym->decl_node->span, .filename = ctx->filename, 
                          .as.name.name = "Allocator struct must have exactly 3 fields: ctx, alloc, free" };
        dynarray_push_value(ctx->errors, &err);
        return;
    }

    const char *expected_fields[] = {"ctx", "alloc", "free"};
    for (int i = 0; i < 3; i++) {
        StructField *field = &t->as.struct_type.fields[i];
        if (!field->name || !field->name->key) {
             TypeError err = { .kind = TE_INCOMPLETE_TYPE, .span = sym->decl_node->span, .filename = ctx->filename, 
                              .as.name.name = "Allocator struct fields must have names" };
            dynarray_push_value(ctx->errors, &err);
            return;
        }
        Slice *field_name = (Slice*)field->name->key;
        if (strncmp(field_name->ptr, expected_fields[i], field_name->len) != 0 || expected_fields[i][field_name->len] != '\0') {
            TypeError err = { .kind = TE_INCOMPLETE_TYPE, .span = sym->decl_node->span, .filename = ctx->filename, 
                              .as.name.name = "Allocator struct fields must be named: ctx, alloc, free" };
            dynarray_push_value(ctx->errors, &err);
            return;
        }
    }
    
    // 3. Signature checks
    StructField *ctx_field = &t->as.struct_type.fields[0];
    StructField *alloc_field = &t->as.struct_type.fields[1];
    StructField *free_field = &t->as.struct_type.fields[2];

    if (!ctx_field->type || ctx_field->type->kind != TYPE_POINTER) {
        TypeError err = { .kind = TE_TYPE_MISMATCH, .span = sym->decl_node->span, .filename = ctx->filename, 
                          .as.name.name = "Allocator field 'ctx' must be a pointer" };
        dynarray_push_value(ctx->errors, &err);
    }
    
    // Validate alloc: fn(ptr, i64) -> ptr
    if (!alloc_field->type || alloc_field->type->kind != TYPE_FUNCTION) {
        TypeError err = { .kind = TE_TYPE_MISMATCH, .span = sym->decl_node->span, .filename = ctx->filename, 
                          .as.name.name = "Allocator field 'alloc' must be a function" };
        dynarray_push_value(ctx->errors, &err);
    } else {
        Type *alloc_ty = alloc_field->type;
        if (alloc_ty->as.func.param_count != 2 || 
            alloc_ty->as.func.params[0]->kind != TYPE_POINTER ||
            (alloc_ty->as.func.params[1]->kind != TYPE_PRIMITIVE || alloc_ty->as.func.params[1]->as.primitive != PRIM_I64) ||
            alloc_ty->as.func.return_type->kind != TYPE_POINTER) {
            TypeError err = { .kind = TE_TYPE_MISMATCH, .span = sym->decl_node->span, .filename = ctx->filename, 
                              .as.name.name = "Allocator field 'alloc' must have signature: fn(ptr, i64) -> ptr" };
            dynarray_push_value(ctx->errors, &err);
        }
    }

    // Validate free: fn(ptr, ptr) -> void
    if (!free_field->type || free_field->type->kind != TYPE_FUNCTION) {
        TypeError err = { .kind = TE_TYPE_MISMATCH, .span = sym->decl_node->span, .filename = ctx->filename, 
                          .as.name.name = "Allocator field 'free' must be a function" };
        dynarray_push_value(ctx->errors, &err);
    } else {
        Type *free_ty = free_field->type;
        if (free_ty->as.func.param_count != 2 || 
            free_ty->as.func.params[0]->kind != TYPE_POINTER ||
            free_ty->as.func.params[1]->kind != TYPE_POINTER ||
            !type_is_void(free_ty->as.func.return_type)) {
            TypeError err = { .kind = TE_TYPE_MISMATCH, .span = sym->decl_node->span, .filename = ctx->filename, 
                              .as.name.name = "Allocator field 'free' must have signature: fn(ptr, ptr) -> void" };
            dynarray_push_value(ctx->errors, &err);
        }
    }
}


TypeCheckContext typecheck_context_create(Arena *arena, TypeStore *store, DenseArenaInterner *identifiers, DenseArenaInterner *keywords, const char *filename, ModuleLoader *loader) {
    DynArray *errors = arena_alloc(arena, sizeof(DynArray));
    dynarray_init_in_arena(errors, arena, sizeof(TypeError), 8);

    TypeCheckContext ctx = {
        .program = NULL,
        .store = store,
        .identifiers = identifiers,
        .keywords = keywords,
        .filename = filename,
        .errors = errors,
        .loader = loader
    };
    return ctx;
}

#ifdef _MSC_VER
#include <malloc.h>
#define ALLOCA _alloca
#else
#include <alloca.h>
#define ALLOCA alloca
#endif

#define FAST_GET(arr, i) (((AstNode**)(arr)->data)[i])

// -----------------------------------------------------------------------------
// AST Patching & Resolution Helpers
// -----------------------------------------------------------------------------

static void patch_inferred_array_sizes(TypeCheckContext *ctx, AstNode *type_ast, Type *concrete_type) {
    if (!type_ast || !concrete_type) return;
    if (type_ast->node_type != AST_TYPE) return;

    if (type_ast->data.ast_type.kind == AST_TYPE_ARRAY) {
        if (concrete_type->kind == TYPE_ARRAY) {
            patch_inferred_array_sizes(ctx, 
                type_ast->data.ast_type.u.array.elem, 
                concrete_type->as.array.base
            );

            if (!type_ast->data.ast_type.u.array.size_expr) {
                AstNode *size_lit = ast_create_node(AST_LITERAL, ctx->store->arena, ctx->filename);
                if (size_lit) {
                    size_lit->node_type = AST_LITERAL;
                    size_lit->span = type_ast->span;
                    size_lit->type = ctx->store->t_i64;
                    size_lit->is_foldable_const = 1;
                    size_lit->is_llvm_const_safe = 1;
                    size_lit->data.literal.type = INT_LITERAL;
                    size_lit->data.literal.value.int_val = (int64_t)concrete_type->as.array.size;
                    size_lit->const_value.type = INT_LITERAL;
                    size_lit->const_value.value.int_val = (int64_t)concrete_type->as.array.size;
                    type_ast->data.ast_type.u.array.size_expr = size_lit;
                }
            }
        } else if (concrete_type->kind == TYPE_SLICE) {
            patch_inferred_array_sizes(ctx, 
                type_ast->data.ast_type.u.array.elem, 
                concrete_type->as.slice.base
            );
        }
    }
}

Type *resolve_ast_type(TypeCheckContext *ctx, Scope *scope, AstNode *node) {
    if (!ctx || !node) return NULL;
    TypeStore *store = ctx->store;

    if (node->node_type == AST_IDENTIFIER) {
        Symbol *sym = scope_lookup_symbol(scope, node->data.identifier.intern_result, ctx->filename);
        if (sym) {
            if (sym->kind == SYMBOL_VALUE_TYPE) return sym->type;
            if (sym->kind == SYMBOL_VALUE_ALIAS) {
                Symbol *target = sym->target_symbol;
                while (target && target->kind == SYMBOL_VALUE_ALIAS) target = target->target_symbol;
                if (target && target->kind == SYMBOL_VALUE_TYPE) return target->type;
            }
        }
        // Also check primitives
        Type *prim = (Type*)hashmap_get(store->primitive_registry, node->data.identifier.intern_result->key, ptr_hash, ptr_cmp);
        if (prim) return prim;

        return NULL;
    }

    if (node->node_type == AST_MEMBER_EXPR) {
        // Resolve member expression as a type path
        check_expression(ctx, scope, node, NULL);
        Symbol *sym = node->data.member_expr.symbol;
        if (sym) {
            Symbol *curr = sym;
            while (curr && curr->kind == SYMBOL_VALUE_ALIAS) curr = curr->target_symbol;
            if (curr && curr->kind == SYMBOL_VALUE_TYPE) return curr->type;
        }
        return NULL;
    }

    if (node->node_type != AST_TYPE) return NULL;

    AstType *ast_ty = &node->data.ast_type;

    switch (ast_ty->kind) {
        case AST_TYPE_PRIMITIVE: {
            if (ast_ty->u.base.path) {
                // Resolved complex path (e.g. std.mem.Allocator)
                AstNode *path = ast_ty->u.base.path;
                check_expression(ctx, scope, path, NULL);
                
                Symbol *sym = NULL;
                if (path->node_type == AST_MEMBER_EXPR) sym = path->data.member_expr.symbol;
                else if (path->node_type == AST_IDENTIFIER) sym = path->data.identifier.symbol;

                if (sym) {
                    Symbol *curr = sym;
                    while (curr && curr->kind == SYMBOL_VALUE_ALIAS) curr = curr->target_symbol;
                    if (curr && curr->kind == SYMBOL_VALUE_TYPE) return curr->type;
                }

                TypeError err = { .kind = TE_UNKNOWN_TYPE, .span = node->span, .filename = ctx->filename, .as.name.name = "Path does not resolve to a type" };
                dynarray_push_value(ctx->errors, &err);
                return NULL;
            }

            InternResult *name_res = ast_ty->u.base.intern_result;
            if (name_res && name_res->key) {
                Type *prim = (Type*)hashmap_get(store->primitive_registry, name_res->key, ptr_hash, ptr_cmp);
                if (prim) return prim;
                
                if (scope) {
                    Symbol *sym = scope_lookup_symbol(scope, name_res, ctx->filename);
                    if (sym) {
                        if (sym->kind == SYMBOL_VALUE_TYPE) return sym->type;
                        if (sym->kind == SYMBOL_VALUE_ALIAS) {
                             Symbol *target = sym->target_symbol;
                             while (target && target->kind == SYMBOL_VALUE_ALIAS) target = target->target_symbol;
                             if (target && target->kind == SYMBOL_VALUE_TYPE) return target->type;
                        }
                    }
                }
                const char *name_str = ((Slice*)name_res->key)->ptr;
                TypeError err = { .kind = TE_UNKNOWN_TYPE, .span = node->span, .filename = ctx->filename, .as.name.name = name_str };
                dynarray_push_value(ctx->errors, &err);
            }
            return NULL; 
        }

        case AST_TYPE_PTR: {
            Type *target = resolve_ast_type(ctx, scope, ast_ty->u.ptr.target);
            if (!target) return NULL; 
            Type proto = { .kind = TYPE_POINTER, .as.ptr.base = target };
            InternResult *res = intern_type(store, &proto);
            return res ? (Type*)((Slice*)res->key)->ptr : NULL;
        }

        case AST_TYPE_ARRAY: {
            Type *elem = resolve_ast_type(ctx, scope, ast_ty->u.array.elem);
            if (!elem) return NULL; 

            int64_t size = 0;
            bool size_known = false;
            AstNode *sz = ast_ty->u.array.size_expr;
            
            if (sz) {
                Type *sz_type = check_expression(ctx, scope, sz, store->t_i64);
                if (sz_type) {
                    if (!type_is_integer(sz_type)) {
                        TypeError err = { .kind = TE_TYPE_MISMATCH, .span = sz->span, .filename = ctx->filename, .as.mismatch = { .expected = store->t_i64, .actual = sz_type } };
                        dynarray_push_value(ctx->errors, &err);
                        return NULL;
                    }
                    if (!sz->is_foldable_const) {
                        TypeError err = { .kind = TE_NOT_CONST, .span = sz->span, .filename = ctx->filename };
                        dynarray_push_value(ctx->errors, &err);
                        return NULL;
                    }
                    size = sz->const_value.value.int_val;
                    size_known = true;
                } else {
                    return NULL;
                }
            }

            Type proto = {0};
            if (size_known) {
                proto.kind = TYPE_ARRAY;
                proto.as.array.base = elem;
                proto.as.array.size = size;
            } else {
                proto.kind = TYPE_SLICE;
                proto.as.slice.base = elem;
            }

            InternResult *res = intern_type(store, &proto);
            return res ? (Type*)((Slice*)res->key)->ptr : NULL;
        }

        case AST_TYPE_FUNC: {
             Type *ret = resolve_ast_type(ctx, scope, ast_ty->u.func.return_type);
             if (!ret) ret = store->t_void; 
             DynArray *params = ast_ty->u.func.param_types;
             size_t count = params ? params->count : 0;
             Type **param_types = NULL;
             if (count > 0) {
                 if (count <= 64) param_types = ALLOCA(sizeof(Type*) * count);
                 else param_types = xmalloc(sizeof(Type*) * count);
             }
             for (size_t i = 0; i < count; i++) {
                 AstNode *p_node = FAST_GET(params, i);
                 Type *pt = resolve_ast_type(ctx, scope, p_node);
                 if (!pt) {
                     if (count > 64) free(param_types);
                     return NULL; 
                 }
                 param_types[i] = pt;
             }
             Type proto = { .kind = TYPE_FUNCTION, .as.func.return_type = ret, .as.func.param_count = count, .as.func.params = param_types };
             InternResult *res = intern_type(store, &proto);
             if (count > 64) free(param_types);
             return res ? (Type*)((Slice*)res->key)->ptr : NULL;
        }
    }
    return NULL;
}

static void resolve_function_decl(TypeCheckContext *ctx, Scope *scope, AstNode *func_node) {
    if (func_node->node_type != AST_FUNCTION_DECLARATION) return;
    AstFunctionDeclaration *decl = &func_node->data.function_declaration;

    Type *ret_type = resolve_ast_type(ctx, scope, decl->return_type);
    if (!ret_type) ret_type = ctx->store->t_void; 

    size_t param_count = decl->params ? decl->params->count : 0;
    Type **param_types = NULL;
    if (param_count > 0) param_types = xmalloc(sizeof(Type*) * param_count);

    for (size_t i = 0; i < param_count; i++) {
        AstNode *param_node = *(AstNode**)dynarray_get(decl->params, i);
        Type *pt = resolve_ast_type(ctx, scope, param_node->data.param.type);
        if (!pt) pt = ctx->store->t_void; 
        
        if (type_is_void(pt)) {
            TypeError err = { .kind = TE_TYPE_MISMATCH, .span = param_node->span, .filename = ctx->filename, .as.name.name = "Parameter cannot have type 'void'" };
            dynarray_push_value(ctx->errors, &err);
        }

        param_types[i] = pt;
        param_node->type = pt;
    }

    Type proto = {0};
    proto.kind = TYPE_FUNCTION;
    proto.as.func.return_type = ret_type;
    proto.as.func.param_count = param_count;
    proto.as.func.params = param_types;
    
    InternResult *res = intern_type(ctx->store, &proto);
    if (res) func_node->type = (Type*)((Slice*)res->key)->ptr;
    free(param_types);
}

static bool check_struct_cycle(TypeCheckContext *ctx, Type *t, DynArray *path) {
    if (!t || t->kind != TYPE_STRUCT) return false;

    // Check if current type is already in the recursion path
    for (size_t i = 0; i < path->count; i++) {
        if (*(Type**)dynarray_get(path, i) == t) return true;
    }

    dynarray_push_value(path, &t);

    for (size_t i = 0; i < t->as.struct_type.field_count; i++) {
        Type *ft = t->as.struct_type.fields[i].type;
        if (!ft) continue;

        // Only value-type structs can cause cycles that affect layout
        if (ft->kind == TYPE_STRUCT) {
            if (check_struct_cycle(ctx, ft, path)) return true;
        } else if (ft->kind == TYPE_ARRAY) {
            // Arrays of structs also count as value-contained
            Type *base = ft->as.array.base;
            while (base && base->kind == TYPE_ARRAY) base = base->as.array.base;
            if (base && base->kind == TYPE_STRUCT) {
                if (check_struct_cycle(ctx, base, path)) return true;
            }
        }
    }

    dynarray_pop(path);
    return false;
}

// -----------------------------------------------------------------------------
// SECTION 1: GLOBAL SYMBOL REGISTRATION (Phase 1)
// -----------------------------------------------------------------------------

static void register_program_structs(TypeCheckContext *ctx, Scope *global_scope) {
    if (!ctx || !ctx->program) return;
    AstProgram *program = &ctx->program->data.program;
    if (!program->decls) return;

    for (size_t i = 0; i < program->decls->count; i++) {
        AstNode *decl = *(AstNode**)dynarray_get(program->decls, i);
        if (!decl || decl->node_type != AST_STRUCT_DECLARATION) continue;

        ctx->filename = decl->filename;
        AstStructDeclaration *struct_decl = &decl->data.struct_declaration;
        if (!struct_decl->intern_result) continue;

        if (scope_lookup_symbol_local(global_scope, struct_decl->intern_result)) continue;

        Type *struct_type = arena_alloc(ctx->store->arena, sizeof(Type));
        struct_type->kind = TYPE_STRUCT;
        struct_type->as.struct_type.name = struct_decl->intern_result;
        struct_type->as.struct_type.field_count = struct_decl->fields ? struct_decl->fields->count : 0;
        struct_type->as.struct_type.fields = NULL; 

        decl->type = struct_type;
        define_symbol_or_error(ctx, global_scope, struct_decl->intern_result, decl->type, SYMBOL_VALUE_TYPE, decl->span, struct_decl->is_pub, decl->filename, decl);
    }
}

static void register_program_globals(TypeCheckContext *ctx, Scope *global_scope) {
    if (!ctx || !ctx->program) return;
    AstProgram *program = &ctx->program->data.program;
    if (!program->decls) return;

    for (size_t i = 0; i < program->decls->count; i++) {
        AstNode *decl = *(AstNode**)dynarray_get(program->decls, i);
        if (!decl || decl->node_type != AST_VARIABLE_DECLARATION) continue;

        ctx->filename = decl->filename;
        AstVariableDeclaration *var_decl = &decl->data.variable_declaration;
        
        if (var_decl->intern_result && !scope_lookup_symbol_local(global_scope, var_decl->intern_result)) {
            define_symbol_or_error(ctx, global_scope, var_decl->intern_result, NULL, SYMBOL_VARIABLE, decl->span, var_decl->is_pub, decl->filename, decl);
        }
    }
}

static void register_program_functions(TypeCheckContext *ctx, Scope *global_scope) {
    if (!ctx || !ctx->program) return;
    AstProgram *program = &ctx->program->data.program;
    if (!program->decls) return;

    for (size_t i = 0; i < program->decls->count; i++) {
        AstNode *decl = *(AstNode**)dynarray_get(program->decls, i);
        if (!decl || decl->node_type != AST_FUNCTION_DECLARATION) continue;

        ctx->filename = decl->filename;
        AstFunctionDeclaration *func = &decl->data.function_declaration;

        if (func->intern_result && !scope_lookup_symbol_local(global_scope, func->intern_result)) {
            define_symbol_or_error(ctx, global_scope, func->intern_result, NULL, SYMBOL_VALUE_FUNCTION, decl->span, func->is_pub, decl->filename, decl);
        }
    }
}

static void register_program_aliases(TypeCheckContext *ctx, Scope *global_scope) {
    if (!ctx || !ctx->program) return;
    AstProgram *program = &ctx->program->data.program;
    if (!program->decls) return;

    for (size_t i = 0; i < program->decls->count; i++) {
        AstNode *decl = *(AstNode**)dynarray_get(program->decls, i);
        if (!decl || decl->node_type != AST_ALIAS_DECLARATION) continue;

        ctx->filename = decl->filename;
        AstAliasDeclaration *alias = &decl->data.alias_declaration;

        if (alias->alias_name && !scope_lookup_symbol_local(global_scope, alias->alias_name)) {
            scope_define_symbol(global_scope, alias->alias_name, NULL, SYMBOL_VALUE_ALIAS, decl->filename, false, decl);
        }
    }
}

// -----------------------------------------------------------------------------
// SECTION 2: GLOBAL SYMBOL RESOLUTION (Phase 2)
// -----------------------------------------------------------------------------

static void resolve_program_structs(TypeCheckContext *ctx, Scope *global_scope) {
    if (!ctx || !ctx->program) return;
    AstProgram *program = &ctx->program->data.program;
    if (!program->decls) return;

    for (size_t i = 0; i < program->decls->count; i++) {
        AstNode *decl = *(AstNode**)dynarray_get(program->decls, i);
        if (!decl || decl->node_type != AST_STRUCT_DECLARATION) continue;

        ctx->filename = decl->filename;
        AstStructDeclaration *struct_decl = &decl->data.struct_declaration;
        Type *struct_type = decl->type;
        if (!struct_type || !struct_decl->fields) continue;

        if (struct_type->as.struct_type.fields) continue;

        struct_type->as.struct_type.fields = arena_alloc(ctx->store->arena, sizeof(StructField) * struct_type->as.struct_type.field_count);
        
        // Initialize field_map
        struct_type->as.struct_type.field_map = hashmap_create(struct_type->as.struct_type.field_count);

        for (size_t j = 0; j < struct_type->as.struct_type.field_count; j++) {
            AstFieldDecl *fdecl = (AstFieldDecl*)dynarray_get(struct_decl->fields, j);
            struct_type->as.struct_type.fields[j].name = fdecl->name;
            struct_type->as.struct_type.fields[j].type = resolve_ast_type(ctx, global_scope, fdecl->type);
            
            // Populate field_map with 1-based index to avoid NULL (0) collisions
            hashmap_put(struct_type->as.struct_type.field_map, fdecl->name->key, (void*)(uintptr_t)(j + 1), ptr_hash, ptr_cmp);
        }
    }

    // Cycle Detection
    DynArray path;
    dynarray_init(&path, sizeof(Type*));
    for (size_t i = 0; i < program->decls->count; i++) {
        AstNode *decl = *(AstNode**)dynarray_get(program->decls, i);
        if (!decl || decl->node_type != AST_STRUCT_DECLARATION) continue;

        ctx->filename = decl->filename;
        path.count = 0;
        if (check_struct_cycle(ctx, decl->type, &path)) {
            TypeError err = { .kind = TE_INCOMPLETE_TYPE, .span = decl->span, .filename = ctx->filename };
            err.as.name.name = "Recursive struct definition (infinite size)";
            dynarray_push_value(ctx->errors, &err);
        }
    }
    dynarray_free(&path);
}

static void resolve_program_globals(TypeCheckContext *ctx, Scope *global_scope) {
    if (!ctx || !ctx->program) return;
    AstProgram *program = &ctx->program->data.program;
    if (!program->decls) return;

    for (size_t i = 0; i < program->decls->count; i++) {
        AstNode *decl = *(AstNode**)dynarray_get(program->decls, i);
        if (!decl || decl->node_type != AST_VARIABLE_DECLARATION) continue;

        ctx->filename = decl->filename;
        AstVariableDeclaration *var_decl = &decl->data.variable_declaration;
        
        Type *var_type = resolve_ast_type(ctx, global_scope, var_decl->type);
        if (!var_type) continue;
        
        decl->type = var_type;
        Symbol *sym = scope_lookup_symbol_local(global_scope, var_decl->intern_result);
        if (sym) {
            sym->type = var_type;
            if (var_decl->is_const) sym->flags |= SYMBOL_FLAG_CONST;
        }
    }
}

static void resolve_program_functions(TypeCheckContext *ctx, Scope *global_scope) {
    if (!ctx || !ctx->program) return;
    AstProgram *program = &ctx->program->data.program;
    if (!program->decls) return;

    for (size_t i = 0; i < program->decls->count; i++) {
        AstNode *decl = *(AstNode**)dynarray_get(program->decls, i);
        if (!decl || decl->node_type != AST_FUNCTION_DECLARATION) continue;

        ctx->filename = decl->filename;
        AstFunctionDeclaration *func = &decl->data.function_declaration;

        resolve_function_decl(ctx, global_scope, decl);
        Symbol *sym = scope_lookup_symbol_local(global_scope, func->intern_result);
        if (sym) {
            sym->type = decl->type;
        }
    }
}

static void resolve_program_aliases(TypeCheckContext *ctx, Scope *global_scope) {
    if (!ctx || !ctx->program) return;
    AstProgram *program = &ctx->program->data.program;
    if (!program->decls) return;

    for (size_t i = 0; i < program->decls->count; i++) {
        AstNode *decl = *(AstNode**)dynarray_get(program->decls, i);
        if (!decl || decl->node_type != AST_ALIAS_DECLARATION) continue;

        ctx->filename = decl->filename;
        AstAliasDeclaration *alias = &decl->data.alias_declaration;

        Symbol *my_alias_sym = scope_lookup_symbol_local(global_scope, alias->alias_name);
        if (!my_alias_sym) continue;

        Symbol *target_sym = NULL;
        if (alias->target->node_type == AST_IDENTIFIER) {
            target_sym = scope_lookup_symbol(global_scope, alias->target->data.identifier.intern_result, ctx->filename);
        } else if (alias->target->node_type == AST_MEMBER_EXPR) {
            check_expression(ctx, global_scope, alias->target, NULL);
            if (alias->target->node_type == AST_MEMBER_EXPR) {
                target_sym = alias->target->data.member_expr.symbol;
            } else if (alias->target->node_type == AST_IDENTIFIER) {
                target_sym = alias->target->data.identifier.symbol;
            }
        } else {
             TypeError err = { .kind = TE_TYPE_MISMATCH, .span = alias->target->span, .filename = ctx->filename };
             err.as.name.name = "Alias target must be a symbol or path";
             dynarray_push_value(ctx->errors, &err);
             continue;
        }

        if (!target_sym) {
             TypeError err = { .kind = TE_UNDECLARED, .span = alias->target->span, .filename = ctx->filename };
             err.as.name.name = "Could not resolve alias target";
             dynarray_push_value(ctx->errors, &err);
             continue;
        }

        my_alias_sym->target_symbol = target_sym;
        my_alias_sym->type = target_sym->type;
    }
}

// -----------------------------------------------------------------------------
// SECTION 3: Type Checking Logic
// -----------------------------------------------------------------------------

static void check_initializer(TypeCheckContext *ctx, Scope *scope, AstNode *var_node, AstNode *initializer, Type *var_type, Symbol *sym) {
    if (!initializer) return;
    Type *actual_type = check_expression(ctx, scope, initializer, var_type);

    if (!actual_type) return; 

    if (actual_type && var_type && actual_type != var_type) {
        // INFERENCE LOGIC: If variable is T[] (Slice) and initializer is an initializer list {...},
        // upgrade it to a fixed-size array T[N].
        bool is_inference = (var_type->kind == TYPE_SLICE && actual_type->kind == TYPE_ARRAY && 
                             var_type->as.slice.base == actual_type->as.array.base &&
                             initializer->node_type == AST_INITIALIZER_LIST);

        if (is_inference) {
            var_node->type = actual_type;
            if (sym) sym->type = actual_type;
            if (var_node->node_type == AST_VARIABLE_DECLARATION) {
                patch_inferred_array_sizes(ctx, var_node->data.variable_declaration.type, actual_type);
            }
            return;
        }

        coerce_or_error(ctx, initializer, var_type);
    }
}

static bool is_type_complete(Type *t) {
    if (!t) return false;
    if (t->kind == TYPE_ARRAY) {
        return is_type_complete(t->as.array.base);
    }
    return true; 
}

void check_variable_declaration(TypeCheckContext *ctx, Scope *scope, AstNode *var_node) {
    if (var_node->node_type != AST_VARIABLE_DECLARATION) return;
    
    // Skip if already checked in this pass (duplicate prevention during recursive resolutions)
    if (var_node->last_checked_pass == ctx->current_pass) return;

    const char *old_filename = ctx->filename;
    ctx->filename = var_node->filename;

    AstVariableDeclaration *var_decl = &var_node->data.variable_declaration;

    Symbol *existing = scope_lookup_symbol_local(scope, var_decl->intern_result);
    
    // Check if the symbol we found is actually us
    bool is_us = (existing && existing->decl_node == var_node);

    // If already initialized (from redundant pass call or demand-driven), skip
    if (is_us && (existing->flags & SYMBOL_FLAG_INITIALIZED)) {
        var_node->last_checked_pass = ctx->current_pass;
        ctx->filename = old_filename;
        return; 
    }
    
    // Cycle detection for constants
    if (is_us && (existing->flags & SYMBOL_FLAG_COMPUTING)) {
        TypeError err = { .kind = TE_RECURSIVE_CONST, .span = var_node->span, .filename = ctx->filename };
        dynarray_push_value(ctx->errors, &err);
        ctx->filename = old_filename;
        return;
    }

    Symbol *my_sym = is_us ? existing : NULL;
    Type *var_type = my_sym ? my_sym->type : resolve_ast_type(ctx, scope, var_decl->type);

    if (!var_type) {
        ctx->filename = old_filename;
        return;
    }

    // Forbid 'void' type for variables
    if (type_is_void(var_type)) {
        TypeError err = { 
            .kind = TE_TYPE_MISMATCH, 
            .span = var_node->span, 
            .filename = ctx->filename,
            .as.name.name = "Variable cannot have type 'void'" 
        };
        dynarray_push_value(ctx->errors, &err);
        ctx->filename = old_filename;
        return;
    }

    // Check for incomplete types (missing size without initializer)
    if (!var_decl->initializer) {
        if (!is_type_complete(var_type)) {
            TypeError err = { 
                .kind = TE_INCOMPLETE_TYPE, 
                .span = var_node->span, 
                .filename = ctx->filename,
                .as.name.name = "Variable declared with incomplete type (missing array size)" 
            };
            dynarray_push_value(ctx->errors, &err);
        }
    }

    var_node->type = var_type;

    bool is_global = (scope->depth <= 1);

    if (is_global) {
        // Global: Symbol MUST be defined before checking initializer to allow recursion
        if (!my_sym) {
            define_symbol_or_error(ctx, scope, var_decl->intern_result, var_type, SYMBOL_VARIABLE, var_node->span, var_decl->is_pub, var_node->filename, var_node);
            my_sym = scope_lookup_symbol_local(scope, var_decl->intern_result);
            if (my_sym && my_sym->decl_node != var_node) my_sym = NULL;
        }

        if (my_sym) {
            my_sym->flags |= SYMBOL_FLAG_COMPUTING;
            check_initializer(ctx, scope, var_node, var_decl->initializer, var_type, my_sym);
            my_sym->flags &= ~SYMBOL_FLAG_COMPUTING;
            my_sym->flags |= SYMBOL_FLAG_INITIALIZED;
        } else {
            check_initializer(ctx, scope, var_node, var_decl->initializer, var_type, NULL);
        }
    } else {
        // Local: Check initializer FIRST to catch self-initialization (x = x) as TE_UNDECLARED
        check_initializer(ctx, scope, var_node, var_decl->initializer, var_type, NULL);
        define_symbol_or_error(ctx, scope, var_decl->intern_result, var_type, SYMBOL_VARIABLE, var_node->span, var_decl->is_pub, var_node->filename, var_node);
        my_sym = scope_lookup_symbol_local(scope, var_decl->intern_result);
        if (my_sym && my_sym->decl_node != var_node) my_sym = NULL;
        if (my_sym) my_sym->flags |= SYMBOL_FLAG_INITIALIZED;
    }

    if (my_sym && var_decl->is_const && var_decl->initializer && var_decl->initializer->is_foldable_const) {
        my_sym->flags |= SYMBOL_FLAG_CONST | SYMBOL_FLAG_COMPUTED_VALUE;
        ConstValue *cv = &var_decl->initializer->const_value;
        switch (cv->type) {
            case INT_LITERAL:   my_sym->value.int_val = cv->value.int_val; break;
            case FLOAT_LITERAL: my_sym->value.float_val = cv->value.float_val; break;
            case BOOL_LITERAL:  my_sym->value.bool_val = (bool)cv->value.bool_val; break;
            case CHAR_LITERAL:  my_sym->value.int_val = (int64_t)cv->value.char_val; break;
            default: break;
        }
    }
    
    var_node->last_checked_pass = ctx->current_pass;
    ctx->filename = old_filename;
}

static void check_block(TypeCheckContext *ctx, Scope *parent, AstNode *block_node, Type *return_type, bool create_new_scope) {
    if (block_node->node_type != AST_BLOCK) return;
    Scope *scope = parent;
    if (create_new_scope) scope = scope_create(ctx->store->arena, parent, 16, SCOPE_IDENTIFIERS);

    AstBlock *b = &block_node->data.block;
    if (!b->statements) return;

    for (size_t i = 0; i < b->statements->count; i++) {
        AstNode *stmt = FAST_GET(b->statements, i);
        check_statement(ctx, scope, stmt, return_type);
    }
}

static void check_statement(TypeCheckContext *ctx, Scope *scope, AstNode *stmt, Type *return_type) {
    if (!stmt) return;
    
    switch (stmt->node_type) {
        case AST_RETURN_STATEMENT: {
            AstReturnStatement *ret = &stmt->data.return_statement;
            Type *actual = ctx->store->t_void;
            if (ret->expression) actual = check_expression(ctx, scope, ret->expression, return_type);
            
            if (actual && return_type && actual != return_type) {
                 coerce_or_error(ctx, ret->expression, return_type);
            }
            break;
        }
        case AST_DEFER_STATEMENT: {
            check_statement(ctx, scope, stmt->data.defer_statement.body, return_type);
            break;
        }
        case AST_BLOCK: 
            check_block(ctx, scope, stmt, return_type, true); 
            break;
        case AST_VARIABLE_DECLARATION: 
            check_variable_declaration(ctx, scope, stmt); 
            break;
        case AST_IF_STATEMENT: {
            AstIfStatement *ifs = &stmt->data.if_statement;
            check_expression(ctx, scope, ifs->condition, ctx->store->t_bool);
            check_statement(ctx, scope, ifs->then_branch, return_type);
            if (ifs->else_branch) check_statement(ctx, scope, ifs->else_branch, return_type);
            break;
        }
        case AST_WHILE_STATEMENT: {
            AstWhileStatement *ws = &stmt->data.while_statement;
            check_expression(ctx, scope, ws->condition, ctx->store->t_bool);
            check_statement(ctx, scope, ws->body, return_type);
            break;
        }
        case AST_FOR_STATEMENT: {
            AstForStatement *fs = &stmt->data.for_statement;
            // 1. Create a scope for the loop
            Scope *for_scope = scope_create(ctx->store->arena, scope, 8, SCOPE_IDENTIFIERS);
                
            // 2. Check the parts
            if (fs->init) check_statement(ctx, for_scope, fs->init, return_type);
            if (fs->condition) check_expression(ctx, for_scope, fs->condition, ctx->store->t_bool);
            if (fs->post) check_expression(ctx, for_scope, fs->post, NULL);
                
            // 3. Check the body
            check_statement(ctx, for_scope, fs->body, return_type);
            break;
        }

        case AST_EXPR_STATEMENT: 
            check_expression(ctx, scope, stmt->data.expr_statement.expression, NULL); 
            break;
        case AST_ASSIGNMENT_EXPR:
        case AST_CALL_EXPR:
        case AST_UNARY_EXPR: // Handles things like x++;
        case AST_INTRINSIC: // Handles standalone intrinsics like @free(...)
            check_expression(ctx, scope, stmt, NULL);
            break;
        default: 
            break; 
    }
}

static void check_function(TypeCheckContext *ctx, Scope *parent_scope, AstNode *func_node) {
    const char *old_filename = ctx->filename;
    ctx->filename = func_node->filename;

    AstFunctionDeclaration *decl = &func_node->data.function_declaration;
    Type *func_type = func_node->type;

    Scope *fn_scope = scope_create(ctx->store->arena, parent_scope, 32, SCOPE_IDENTIFIERS);
    if (decl->params) {
        for (size_t i = 0; i < decl->params->count; i++) {
            AstNode *param = *(AstNode**)dynarray_get(decl->params, i);
            if (param->data.param.name_idx != -1) {
                InternResult *name_rec = interner_get_result(ctx->identifiers, param->data.param.name_idx);
                define_symbol_or_error(ctx, fn_scope, name_rec, param->type, SYMBOL_VARIABLE, param->span, false, param->filename, param);
            }
        }
    }
    if (decl->body) {
        check_block(ctx, fn_scope, decl->body, func_type->as.func.return_type, false);
    }

    func_node->last_checked_pass = ctx->current_pass;
    ctx->filename = old_filename;
}

static void resolve_imports(TypeCheckContext *ctx, CompilationUnit *unit) {
    if (!unit->ast_root) return;
    AstProgram *program = &unit->ast_root->data.program;
    if (!program->decls) return;

    for (size_t i = 0; i < program->decls->count; i++) {
        AstNode *decl = *(AstNode**)dynarray_get(program->decls, i);
        if (decl->node_type != AST_IMPORT_DECLARATION) continue;

        AstImportDeclaration *import = &decl->data.import_declaration;

        // Use the resolved logical path from the loading phase
        const char *logical_path_str = import->resolved_logical_path;
        if (!logical_path_str) {
             // Fallback to rebuilding from module_path if needed (mostly for main module or simple cases)
             char rebuilt[512] = {0};
             size_t r_len = 0;
             for (size_t j = 0; j < import->module_path->count; j++) {
                InternResult *part = *(InternResult**)dynarray_get(import->module_path, j);
                Slice *s = (Slice*)part->key;
                if (r_len + s->len + 1 < sizeof(rebuilt)) {
                    if (j > 0) rebuilt[r_len++] = '.';
                    memcpy(rebuilt + r_len, s->ptr, s->len);
                    r_len += s->len;
                }
             }
             rebuilt[r_len] = '\0';
             logical_path_str = rebuilt;
        }

        // Find the unit by logical path
        CompilationUnit *target = (CompilationUnit*)hashmap_get(ctx->loader->units_by_logical_path, (void*)logical_path_str, str_hash, str_cmp);

        if (!target) {
            TypeError err = { .kind = TE_UNDECLARED, .span = decl->span, .filename = unit->absolute_path };
            err.as.name.name = "Module not found";
            dynarray_push_value(ctx->errors, &err);
            continue;
        }

        // 1. Nested Module Binding (Full Path Access)
        // Bind components like 'std' -> 'libc' in the scope hierarchy
        Scope *current_bind_scope = unit->global_scope;
        for (size_t j = 0; j < import->module_path->count; j++) {
            InternResult *part = *(InternResult**)dynarray_get(import->module_path, j);
            
            // Is this the final part?
            bool is_last = (j == import->module_path->count - 1);
            
            if (is_last) {
                // Bind the actual target module. 
                // It's private in the importing module's scope, but 'pub' relative to its parent dummy module.
                Symbol *mod_sym = scope_define_symbol(current_bind_scope, part, NULL, SYMBOL_VALUE_MODULE, target->absolute_path, true, NULL);
                if (mod_sym) {
                    mod_sym->module_scope = target->global_scope;
                    // Note: the very first component (e.g. 'std') bound to unit->global_scope 
                    // should be private to the module unless it was already there and pub.
                    if (current_bind_scope == unit->global_scope) {
                        mod_sym->is_pub = import->is_pub; 
                    }
                }
            } else {
                // Intermediate component (e.g., 'std' in 'std.libc')
                Symbol *existing = scope_lookup_symbol_local(current_bind_scope, part);
                if (existing) {
                    if (existing->kind != SYMBOL_VALUE_MODULE && existing->kind != SYMBOL_VALUE_NAMESPACE) {
                         TypeError err = { .kind = TE_REDECLARATION, .span = decl->span, .filename = unit->absolute_path };
                         err.as.name.name = "Import path component conflicts with existing symbol";
                         dynarray_push_value(ctx->errors, &err);
                         break;
                    }
                    current_bind_scope = existing->module_scope;
                } else {
                    // Create a namespace symbol for the namespace. 
                    // This namespace is 'pub' so we can traverse it.
                    Symbol *ns_sym = scope_define_symbol(current_bind_scope, part, NULL, SYMBOL_VALUE_NAMESPACE, NULL, true, NULL);
                    ns_sym->module_scope = scope_create(ctx->store->arena, NULL, 16, SCOPE_IDENTIFIERS);
                    
                    // But if it's in the root global scope, it should be private by default.
                    if (current_bind_scope == unit->global_scope) {
                        ns_sym->is_pub = import->is_pub;
                    }
                    
                    current_bind_scope = ns_sym->module_scope;
                }
            }
        }

        // 2. Handle specific symbols: import math { sin };
        if (import->specific_symbols) {
            for (size_t j = 0; j < import->specific_symbols->count; j++) {
                ImportSymbol *sym_imp = *(ImportSymbol**)dynarray_get(import->specific_symbols, j);
                Symbol *target_sym = scope_lookup_symbol_local(target->global_scope, sym_imp->original_name);

                if (!target_sym || !target_sym->is_pub) {
                    const char *missing_name = "<unknown>";
                    if (sym_imp->original_name && sym_imp->original_name->key) {
                        missing_name = ((Slice*)sym_imp->original_name->key)->ptr;
                    }
                    TypeError err = { .kind = TE_UNDECLARED, .span = decl->span, .filename = unit->absolute_path };
                    err.as.name.name = missing_name;
                    dynarray_push_value(ctx->errors, &err);
                    continue;
                }

                // Register in local scope
                InternResult *local_name = sym_imp->alias_name ? sym_imp->alias_name : sym_imp->original_name;
                scope_define_symbol(unit->global_scope, local_name, target_sym->type, target_sym->kind, target->absolute_path, import->is_pub, target_sym->decl_node);
            }
        } else if (import->is_star) {
            // "import *": Bring everything into local scope
            for (size_t j = 0; j < target->global_scope->symbols_list.count; j++) {
                Symbol *sym = *(Symbol**)dynarray_get(&target->global_scope->symbols_list, j);
                if (sym && sym->is_pub && sym->kind != SYMBOL_VALUE_MODULE) {
                     scope_define_symbol(unit->global_scope, sym->name_rec, sym->type, sym->kind, target->absolute_path, import->is_pub, sym->decl_node);
                }
            }
        } else if (import->module_alias) {
            // Handle module alias: import math alias m;
            Symbol *mod_sym = scope_define_symbol(unit->global_scope, import->module_alias, NULL, SYMBOL_VALUE_MODULE, target->absolute_path, import->is_pub, NULL);
            if (mod_sym) {
                mod_sym->module_scope = target->global_scope;
            }
        } else {
            // Bare import: qualified access only (std.alloc.Symbol).
            // The namespace binding in section 1 above already handles this.
            // Use "import std.alloc { Symbol }" to bring names directly into scope.
        }
    }
}

// -----------------------------------------------------------------------------
// MAIN ENTRY POINT
// -----------------------------------------------------------------------------

void typecheck_program(TypeCheckContext *ctx) {
    if (!ctx || !ctx->loader) return;
    Arena *scope_arena = ctx->store->arena;

    // Create a shared "Universe" scope for primitives and keywords
    int universe_count = (ctx->keywords ? ctx->keywords->dense_index_count : 0) + 32;
    Scope *universe_scope = scope_create(scope_arena, NULL, universe_count, SCOPE_KEYWORDS);

    // 1. Initialize Global Scopes for all units
    for (size_t i = 0; i < ctx->loader->units_ordered->count; i++) {
        CompilationUnit *unit = *(CompilationUnit**)dynarray_get(ctx->loader->units_ordered, i);
        int id_count = (ctx->identifiers ? ctx->identifiers->dense_index_count : 0) + 64;
        unit->global_scope = scope_create(scope_arena, universe_scope, id_count, SCOPE_IDENTIFIERS);
        unit->global_scope->unit = unit; // Set the unit pointer
        register_intrinsics(ctx->store, unit->global_scope, ctx->identifiers);
    }

    // 2. Pass 1: Signatures (Interleaved loop: Names -> Imports -> Full Signatures)
    for (size_t i = 0; i < ctx->loader->units_ordered->count; i++) {
        CompilationUnit *unit = *(CompilationUnit**)dynarray_get(ctx->loader->units_ordered, i);
        ctx->filename = unit->absolute_path;
        ctx->program = unit->ast_root;

        // Step A: Names (Register Struct/Global/Function/Alias names)
        register_program_structs(ctx, unit->global_scope);
        register_program_globals(ctx, unit->global_scope);
        register_program_functions(ctx, unit->global_scope);
        register_program_aliases(ctx, unit->global_scope);

        // Step B: Imports (Now that all dependency names are registered due to post-order)
        resolve_imports(ctx, unit);
        unit->imports_resolved = true;

        // Step C: Full Signatures (Types are now available via imports)
        resolve_program_aliases(ctx, unit->global_scope);
        resolve_program_structs(ctx, unit->global_scope);
        resolve_program_globals(ctx, unit->global_scope);
        resolve_program_functions(ctx, unit->global_scope);
        unit->signatures_resolved = true;
    }

    // 3. Pass 2: Bodies (Global)
    // Keep a constant assignment of 1 so the `last_checked_pass` logic works internally for duplicate prevention
    ctx->current_pass = 1; 
    for (size_t i = 0; i < ctx->loader->units_ordered->count; i++) {
        CompilationUnit *unit = *(CompilationUnit**)dynarray_get(ctx->loader->units_ordered, i);
        ctx->filename = unit->absolute_path;
        ctx->program = unit->ast_root;

        validate_allocator_struct(ctx, unit->global_scope);

        AstProgram *program = &unit->ast_root->data.program;
        if (!program->decls) continue;

        for (size_t j = 0; j < program->decls->count; j++) {
            AstNode *decl = *(AstNode**)dynarray_get(program->decls, j);
            switch (decl->node_type) {
                case AST_VARIABLE_DECLARATION: check_variable_declaration(ctx, unit->global_scope, decl); break;
                case AST_FUNCTION_DECLARATION: check_function(ctx, unit->global_scope, decl); break;
                default: break;
            }
        }
    }
}