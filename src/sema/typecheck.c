#include "sema/typecheck.h"
#include "sema/type_coerce.h"
#include "sema/type_utils.h"
#include "sema/symbol_utils.h"
#include "sema/typecheck_expr.h"
#include "core/utils.h"
#include "core/error.h"
#include "datastructures/dynamic_array.h"
#include "codegen/codegen_utils.h"
#include <string.h>
#include <stdlib.h>
#include <stdio.h>

// Forward declarations
static void check_statement(TypeCheckContext *ctx, Scope *scope, AstNode *stmt, Type *return_type);
static void check_block(TypeCheckContext *ctx, Scope *parent, AstNode *block_node, Type *return_type, bool create_new_scope);
static void check_function(TypeCheckContext *ctx, Scope *parent_scope, AstNode *func_node);
static Type *instantiate_generic_struct(TypeCheckContext *ctx, Scope *scope, Symbol *sym, Type **arg_types, size_t count, Span error_span);
static size_t type_mangled_len(Type *t);
static void type_to_mangled_str_append(Type *t, char **buf);
static void drain_mono_queue(TypeCheckContext *ctx);

TypeCheckContext typecheck_context_create(Arena *arena, TypeStore *store, DenseArenaInterner *identifiers, DenseArenaInterner *keywords, const char *filename, ModuleLoader *loader) {
    DynArray *errors = arena_alloc(arena, sizeof(DynArray));
    dynarray_init_in_arena(errors, arena, sizeof(TypeError), 8);
    DynArray *mono_queue = arena_alloc(arena, sizeof(DynArray));
    dynarray_init_in_arena(mono_queue, arena, sizeof(void*), 16);
    return (TypeCheckContext) {
        .program = NULL,
        .store = store,
        .identifiers = identifiers,
        .keywords = keywords,
        .filename = filename,
        .errors = errors,
        .loader = loader,
        .current_pass = 0,
        .mono_queue = mono_queue
    };
}

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
            if (sym->kind == SYMBOL_VALUE_TYPE) {
                if (sym->type) return sym->type;
                if (sym->decl_node && sym->decl_node->type) return sym->decl_node->type;
            }
            if (sym->kind == SYMBOL_GENERIC_STRUCT || sym->kind == SYMBOL_GENERIC_FUNCTION) {
                TypeError err = { .kind = TE_MISSING_TYPE_ARGS, .span = node->span, .filename = ctx->filename };
                dynarray_push_value(ctx->errors, &err);
                return NULL;
            }
            if (sym->kind == SYMBOL_VALUE_ALIAS) {
                Symbol *target = sym->target_symbol;
                while (target && target->kind == SYMBOL_VALUE_ALIAS) target = target->target_symbol;
                if (target && target->kind == SYMBOL_VALUE_TYPE) {
                    if (target->type) return target->type;
                    if (target->decl_node && target->decl_node->type) return target->decl_node->type;
                }
                if (!target && sym->type) return sym->type;
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
            if (curr && curr->kind == SYMBOL_VALUE_TYPE) {
                if (curr->type) return curr->type;
                if (curr->decl_node && curr->decl_node->type) return curr->decl_node->type;
            }
            if (curr && (curr->kind == SYMBOL_GENERIC_STRUCT || curr->kind == SYMBOL_GENERIC_FUNCTION)) {
                TypeError err = { .kind = TE_MISSING_TYPE_ARGS, .span = node->span, .filename = ctx->filename };
                dynarray_push_value(ctx->errors, &err);
                return NULL;
            }
            if (!curr && sym && sym->type) return sym->type;
            if (!curr && sym && (sym->kind == SYMBOL_GENERIC_STRUCT || sym->kind == SYMBOL_GENERIC_FUNCTION)) {
                TypeError err = { .kind = TE_MISSING_TYPE_ARGS, .span = node->span, .filename = ctx->filename };
                dynarray_push_value(ctx->errors, &err);
                return NULL;
            }
        }
        return NULL;
    }

    if (node->node_type == AST_GENERIC_INST_EXPR) {
        AstGenericInstExpr *inst = &node->data.generic_inst_expr;
        size_t count = inst->type_args ? inst->type_args->count : 0;
        Type **arg_types = count > 0 ? arena_alloc(ctx->store->arena, sizeof(Type*) * count) : NULL;
        for (size_t i = 0; i < count; i++) {
            AstNode *arg_node = *(AstNode**)dynarray_get(inst->type_args, i);
            Type *arg_t = resolve_ast_type(ctx, scope, arg_node);
            if (!arg_t) { return NULL; }
            arg_types[i] = arg_t;
        }

        Symbol *sym = NULL;
        if (inst->base->node_type == AST_IDENTIFIER) {
            sym = scope_lookup_symbol(scope, inst->base->data.identifier.intern_result, ctx->filename);
        } else if (inst->base->node_type == AST_MEMBER_EXPR) {
            check_expression(ctx, scope, inst->base, NULL);
            if (inst->base->node_type == AST_MEMBER_EXPR) {
                sym = inst->base->data.member_expr.symbol;
            } else if (inst->base->node_type == AST_IDENTIFIER) {
                sym = inst->base->data.identifier.symbol;
            }
        }

        if (sym && sym->kind == SYMBOL_GENERIC_STRUCT) {
            Type *inst_type = instantiate_generic_struct(ctx, scope, sym, arg_types, count, node->span);
            if (ctx->current_pass > 0) {
                drain_mono_queue(ctx);
            }
            return inst_type;
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
                    if (!curr && sym && sym->type) return sym->type;
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
                        if (sym->kind == SYMBOL_GENERIC_STRUCT) {
                            TypeError err = { .kind = TE_MISSING_TYPE_ARGS, .span = node->span, .filename = ctx->filename };
                            dynarray_push_value(ctx->errors, &err);
                            return NULL;
                        }
                        if (sym->kind == SYMBOL_VALUE_ALIAS) {
                             Symbol *target = sym->target_symbol;
                             while (target && target->kind == SYMBOL_VALUE_ALIAS) target = target->target_symbol;
                             if (target && target->kind == SYMBOL_VALUE_TYPE) return target->type;
                             if (target && target->kind == SYMBOL_GENERIC_STRUCT) {
                                 TypeError err = { .kind = TE_MISSING_TYPE_ARGS, .span = node->span, .filename = ctx->filename };
                                 dynarray_push_value(ctx->errors, &err);
                                 return NULL;
                             }
                             if (!target && sym->type) return sym->type;
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
                 param_types = arena_alloc(ctx->store->arena, sizeof(Type*) * count);
             }
             for (size_t i = 0; i < count; i++) {
                 AstNode *p_node = FAST_GET(params, i);
                 Type *pt = resolve_ast_type(ctx, scope, p_node);
                 if (!pt) {
                     return NULL; 
                 }
                 param_types[i] = pt;
             }
             Type proto = { .kind = TYPE_FUNCTION, .as.func.return_type = ret, .as.func.param_count = count, .as.func.params = param_types };
             InternResult *res = intern_type(store, &proto);
             return res ? (Type*)((Slice*)res->key)->ptr : NULL;
        }
        case AST_TYPE_APPLICATION: {
             DynArray *args = ast_ty->u.application.args;
             size_t count = args ? args->count : 0;
             Type **arg_types = count > 0 ? arena_alloc(ctx->store->arena, sizeof(Type*) * count) : NULL;
             for (size_t i = 0; i < count; i++) {
                 AstNode *arg_node = FAST_GET(args, i);
                 Type *arg_t = resolve_ast_type(ctx, scope, arg_node);
                 if (!arg_t) {
                     return NULL;
                 }
                 arg_types[i] = arg_t;
             }

             AstNode *base_node = ast_ty->u.application.base;
             Symbol *sym = NULL;
             if (base_node && base_node->node_type == AST_TYPE) {
                 AstType *base_ty = &base_node->data.ast_type;
                 if (base_ty->kind == AST_TYPE_PRIMITIVE) {
                     if (base_ty->u.base.path) {
                         check_expression(ctx, scope, base_ty->u.base.path, NULL);
                         if (base_ty->u.base.path->node_type == AST_MEMBER_EXPR) {
                             sym = base_ty->u.base.path->data.member_expr.symbol;
                         } else if (base_ty->u.base.path->node_type == AST_IDENTIFIER) {
                             sym = base_ty->u.base.path->data.identifier.symbol;
                         }
                     } else if (base_ty->u.base.intern_result) {
                         sym = scope_lookup_symbol(scope, base_ty->u.base.intern_result, ctx->filename);
                     }
                 }
             } else if (base_node && base_node->node_type == AST_IDENTIFIER) {
                 sym = scope_lookup_symbol(scope, base_node->data.identifier.intern_result, ctx->filename);
             }

             if (sym && sym->kind == SYMBOL_GENERIC_STRUCT) {
                 Type *inst_type = instantiate_generic_struct(ctx, scope, sym, arg_types, count, node->span);
                 if (ctx->current_pass > 0) {
                     drain_mono_queue(ctx);
                 }
                 return inst_type;
             }

             if (!sym) {
                 // Error handled elsewhere
             } else {
                 TypeError err = { .kind = TE_NOT_GENERIC, .span = node->span, .filename = ctx->filename };
                 err.as.name.name = ((Slice*)sym->name_rec->key)->ptr;
                 dynarray_push_value(ctx->errors, &err);
             }

             return NULL;
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
    if (param_count > 0) param_types = arena_alloc(ctx->store->arena, sizeof(Type*) * param_count);

    for (size_t i = 0; i < param_count; i++) {
        AstNode *param_node = *(AstNode**)dynarray_get(decl->params, i);
        Type *pt = resolve_ast_type(ctx, scope, param_node->data.param.type);
        if (!pt) pt = ctx->store->t_void; 
        
        if (type_is_void(pt)) {
            TypeError err = { .kind = TE_VOID_PARAMETER, .span = param_node->span, .filename = ctx->filename };
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

        if (struct_decl->type_params && struct_decl->type_params->count > 0) {
            Type *struct_type = arena_calloc(ctx->store->arena, sizeof(Type));
            struct_type->kind = TYPE_STRUCT;
            struct_type->as.struct_type.name = struct_decl->intern_result;
            struct_type->as.struct_type.decl_node = decl;
            struct_type->as.struct_type.field_count = 0;
            struct_type->as.struct_type.fields = NULL;
            struct_type->as.struct_type.field_map = NULL;
            struct_type->as.struct_type.methods = NULL;
            decl->type = struct_type;

            define_symbol_or_error(ctx, global_scope, struct_decl->intern_result, decl->type, SYMBOL_GENERIC_STRUCT, decl->span, struct_decl->is_pub, decl->filename, decl);
            
            CompilationUnit *unit = module_loader_get_unit(ctx->loader, decl->filename);
            if (unit && unit->generic_templates) {
                hashmap_put(unit->generic_templates, struct_decl->intern_result->key, decl, ptr_hash, ptr_cmp);
            }
            continue;
        }

        Type *struct_type = arena_calloc(ctx->store->arena, sizeof(Type));
        struct_type->kind = TYPE_STRUCT;
        struct_type->as.struct_type.name = struct_decl->intern_result;
        struct_type->as.struct_type.decl_node = decl;
        struct_type->as.struct_type.field_count = struct_decl->fields ? struct_decl->fields->count : 0;
        struct_type->as.struct_type.fields = NULL; 
        struct_type->as.struct_type.field_map = NULL;
        struct_type->as.struct_type.methods = NULL; 

        decl->type = struct_type;
        define_symbol_or_error(ctx, global_scope, struct_decl->intern_result, decl->type, SYMBOL_VALUE_TYPE, decl->span, struct_decl->is_pub, decl->filename, decl);
    }
}

static void register_program_enums(TypeCheckContext *ctx, Scope *global_scope) {
    if (!ctx || !ctx->program) return;
    AstProgram *program = &ctx->program->data.program;
    if (!program->decls) return;

    for (size_t i = 0; i < program->decls->count; i++) {
        AstNode *decl = *(AstNode**)dynarray_get(program->decls, i);
        if (!decl || decl->node_type != AST_ENUM_DECLARATION) continue;

        ctx->filename = decl->filename;
        AstEnumDeclaration *enum_decl = &decl->data.enum_declaration;
        if (!enum_decl->intern_result) continue;

        Type *enum_type = arena_calloc(ctx->store->arena, sizeof(Type));
        enum_type->kind = TYPE_ENUM;
        enum_type->as.enum_type.name = enum_decl->intern_result;
        enum_type->as.enum_type.decl_node = decl;
        enum_type->as.enum_type.variant_count = enum_decl->variants ? enum_decl->variants->count : 0;
        enum_type->as.enum_type.variants = NULL;
        enum_type->as.enum_type.variant_map = NULL;
        decl->type = enum_type;

        define_symbol_or_error(ctx, global_scope, enum_decl->intern_result, decl->type, SYMBOL_VALUE_TYPE, decl->span, enum_decl->is_pub, decl->filename, decl);
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

        // Methods are registered later, in a separate pass
        if (func->target_type_node) continue;

        if (func->intern_result) {
            if (func->type_params && func->type_params->count > 0) {
                define_symbol_or_error(ctx, global_scope, func->intern_result, NULL, SYMBOL_GENERIC_FUNCTION, decl->span, func->is_pub, decl->filename, decl);
                CompilationUnit *unit = module_loader_get_unit(ctx->loader, decl->filename);
                if (unit && unit->generic_templates) {
                    hashmap_put(unit->generic_templates, func->intern_result->key, decl, ptr_hash, ptr_cmp);
                }
                continue;
            }
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

static void group_program_methods(TypeCheckContext *ctx, Scope *global_scope) {
    if (!ctx || !ctx->program) return;
    AstProgram *program = &ctx->program->data.program;
    if (!program->decls) return;

    for (size_t i = 0; i < program->decls->count; i++) {
        AstNode *decl = *(AstNode**)dynarray_get(program->decls, i);
        if (!decl || decl->node_type != AST_FUNCTION_DECLARATION) continue;

        AstFunctionDeclaration *func = &decl->data.function_declaration;
        if (!func->target_type_node) continue;
        
        Symbol *target_sym = NULL;
        if (func->target_type_node->node_type == AST_IDENTIFIER) {
            target_sym = scope_lookup_symbol(global_scope, func->target_type_node->data.identifier.intern_result, decl->filename);
        }
        
        if (target_sym && (target_sym->kind == SYMBOL_GENERIC_STRUCT || target_sym->kind == SYMBOL_VALUE_TYPE)) {
            AstNode *struct_node = target_sym->decl_node;
            if (struct_node && struct_node->node_type == AST_STRUCT_DECLARATION) {
                AstStructDeclaration *struct_decl = &struct_node->data.struct_declaration;
                if (!struct_decl->methods) {
                    struct_decl->methods = arena_calloc(ctx->store->arena, sizeof(DynArray));
                    dynarray_init_in_arena(struct_decl->methods, ctx->store->arena, sizeof(AstNode*), 4);
                }
                dynarray_push_value(struct_decl->methods, &decl);
            }
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
        
        // Initialize field_map and methods map
        struct_type->as.struct_type.field_map = hashmap_create(ctx->store->arena, struct_type->as.struct_type.field_count);
        struct_type->as.struct_type.methods   = hashmap_create(ctx->store->arena, 4); 

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

        AstStructDeclaration *struct_decl = &decl->data.struct_declaration;
        if (struct_decl->type_params && struct_decl->type_params->count > 0) {
            continue; // Skip templates
        }

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

static void resolve_program_enums(TypeCheckContext *ctx, Scope *global_scope) {
    if (!ctx || !ctx->program) return;
    AstProgram *program = &ctx->program->data.program;
    if (!program->decls) return;

    for (size_t i = 0; i < program->decls->count; i++) {
        AstNode *decl = *(AstNode**)dynarray_get(program->decls, i);
        if (!decl || decl->node_type != AST_ENUM_DECLARATION) continue;

        ctx->filename = decl->filename;
        Type *enum_type = decl->type;
        if (!enum_type || enum_type->kind != TYPE_ENUM) continue;

        AstEnumDeclaration *enum_decl = &decl->data.enum_declaration;
        size_t count = enum_decl->variants ? enum_decl->variants->count : 0;
        enum_type->as.enum_type.variant_count = count;

        if (count > 0) {
            enum_type->as.enum_type.variants = arena_alloc(ctx->store->arena, sizeof(EnumVariant) * count);
            enum_type->as.enum_type.variant_map = hashmap_create(ctx->store->arena, (count * 4) / 3 + 1);

            int64_t current_val = 0;
            for (size_t v = 0; v < count; v++) {
                AstEnumVariant *variant_node = dynarray_get(enum_decl->variants, v);
                EnumVariant *variant = &enum_type->as.enum_type.variants[v];
                variant->name = variant_node->name;

                if (variant_node->value) {
                    // Evaluate constant expression
                    check_expression(ctx, global_scope, variant_node->value, NULL);
                    // Const evaluation. For now assume it's AST_LITERAL
                    if (variant_node->value->node_type == AST_LITERAL && variant_node->value->data.literal.type == INT_LITERAL) {
                        current_val = variant_node->value->data.literal.value.int_val;
                    } else {
                        TypeError err = { .kind = TE_TYPE_MISMATCH, .span = variant_node->value->span, .filename = ctx->filename };
                        dynarray_push_value(ctx->errors, &err);
                    }
                }

                variant->value = current_val;

                // Check for duplicate variant
                if (hashmap_get(enum_type->as.enum_type.variant_map, variant->name->key, str_hash, str_cmp)) {
                    TypeError err = { .kind = TE_REDECLARATION, .span = decl->span, .filename = ctx->filename, .as.name.name = variant->name->key };
                    dynarray_push_value(ctx->errors, &err);
                } else {
                    hashmap_put(enum_type->as.enum_type.variant_map, variant->name->key, variant, str_hash, str_cmp);
                }

                current_val++;
            }
        }
        
        // Finalize intern
        InternResult *interned = intern_type(ctx->store, enum_type);
        decl->type = (Type*)((Slice*)interned->key)->ptr;
    }
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

        // Skip methods, they are resolved separately
        if (func->target_type_node) continue;

        // Skip generic function templates
        if (func->type_params && func->type_params->count > 0) continue;

        resolve_function_decl(ctx, global_scope, decl);
        
        // Targeted Lookup: Find the specific Symbol associated with this AstNode
        Symbol *top = scope_lookup_symbol_local(global_scope, func->intern_result);
        Symbol *target = NULL;

        if (top) {
            if (top->kind == SYMBOL_OVERLOAD_SET) {
                // Find the candidate whose decl_node matches this AST node.
                for (size_t j = 0; j < top->overloads->count; j++) {
                    Symbol *c = *(Symbol**)dynarray_get(top->overloads, j);
                    if (c->decl_node == decl) { 
                        target = c; 
                        break; 
                    }
                }
            } else {
                target = top;
            }
        }

        if (target) {
            target->type = decl->type;
        }
    }
}

static void resolve_program_methods(TypeCheckContext *ctx, Scope *global_scope) {
    if (!ctx || !ctx->program) return;
    AstProgram *program = &ctx->program->data.program;
    if (!program->decls) return;

    for (size_t i = 0; i < program->decls->count; i++) {
        AstNode *decl = *(AstNode**)dynarray_get(program->decls, i);
        if (!decl) continue;

        if (decl->node_type == AST_IMPL_DECLARATION) {
            AstImplDeclaration *impl = &decl->data.impl_declaration;
            ctx->filename = decl->filename;
            
            Symbol *target_sym = NULL;
            AstNode *lookup_node = impl->target_type_node;
            
            while (lookup_node) {
                if (lookup_node->node_type == AST_TYPE) {
                    if (lookup_node->data.ast_type.kind == AST_TYPE_APPLICATION) {
                        lookup_node = lookup_node->data.ast_type.u.application.base;
                        continue;
                    } else if (lookup_node->data.ast_type.kind == AST_TYPE_PRIMITIVE) {
                        if (lookup_node->data.ast_type.u.base.path) {
                            lookup_node = lookup_node->data.ast_type.u.base.path;
                            continue;
                        } else {
                            target_sym = scope_lookup_symbol(global_scope, lookup_node->data.ast_type.u.base.intern_result, ctx->filename);
                            break;
                        }
                    } else {
                        break;
                    }
                } else if (lookup_node->node_type == AST_GENERIC_INST_EXPR) {
                    lookup_node = lookup_node->data.generic_inst_expr.base;
                } else if (lookup_node->node_type == AST_IDENTIFIER) {
                    target_sym = scope_lookup_symbol(global_scope, lookup_node->data.identifier.intern_result, ctx->filename);
                    break;
                } else if (lookup_node->node_type == AST_MEMBER_EXPR) {
                    target_sym = lookup_node->data.member_expr.symbol;
                    if (!target_sym) {
                        check_expression(ctx, global_scope, lookup_node, NULL);
                        target_sym = lookup_node->data.member_expr.symbol;
                    }
                    break;
                } else {
                    break;
                }
            }
            
            if ((impl->type_params && impl->type_params->count > 0) || (target_sym && target_sym->kind == SYMBOL_GENERIC_STRUCT)) {
                
                /* Validate that the number of type parameters declared in the impl block matches
                 * the number of generic arguments expected by the target struct. */
                if (impl->target_type_node && target_sym && target_sym->decl_node && target_sym->decl_node->node_type == AST_STRUCT_DECLARATION) {
                    size_t expected_args = 0;
                    AstStructDeclaration *str_decl = &target_sym->decl_node->data.struct_declaration;
                    if (str_decl->type_params) expected_args = str_decl->type_params->count;
                    
                    size_t applied_args = 0;
                    AstNode *tgt = impl->target_type_node;
                    if (tgt->node_type == AST_TYPE && tgt->data.ast_type.kind == AST_TYPE_APPLICATION) {
                        applied_args = tgt->data.ast_type.u.application.args ? 
                                       tgt->data.ast_type.u.application.args->count : 0;
                    } else if (tgt->node_type == AST_GENERIC_INST_EXPR) {
                        applied_args = tgt->data.generic_inst_expr.type_args ? 
                                       tgt->data.generic_inst_expr.type_args->count : 0;
                    }
                    
                    if (expected_args > 0 && applied_args != expected_args) {
                        const char *target_name = "Target struct";
                        if (target_sym->name_rec && target_sym->name_rec->key) {
                            target_name = ((Slice*)target_sym->name_rec->key)->ptr;
                        }
                    
                        TypeError err = { 
                            .kind = TE_GENERIC_ARG_MISMATCH, 
                            .span = impl->target_type_node->span, 
                            .filename = ctx->filename 
                        };
                        err.as.generic_mismatch.name = target_name;
                        err.as.generic_mismatch.expected = expected_args;
                        err.as.generic_mismatch.provided = applied_args;
                        
                        dynarray_push_value(ctx->errors, &err);
                        continue; /* Skip registering invalid template */
                    }
                }

                if (target_sym && target_sym->decl_node && target_sym->decl_node->node_type == AST_STRUCT_DECLARATION) {
                    Type *base_type = target_sym->type;
                    if (base_type) {
                        DynArray *impls = (DynArray*)hashmap_get(ctx->store->impl_registry, (void*)base_type, ptr_hash, ptr_cmp);
                        if (!impls) {
                            impls = arena_calloc(ctx->store->arena, sizeof(DynArray));
                            dynarray_init_in_arena(impls, ctx->store->arena, sizeof(AstNode*), 4);
                            hashmap_put(ctx->store->impl_registry, (void*)base_type, impls, ptr_hash, ptr_cmp);
                        }
                        dynarray_push_value(impls, &decl);
                    }
                }
                continue;
            }
            
            Type *target_type = resolve_ast_type(ctx, global_scope, impl->target_type_node);
            if (!target_type || target_type->kind != TYPE_STRUCT) continue;
            
            if (impl->methods) {
                for (size_t m = 0; m < impl->methods->count; m++) {
                    AstNode *method_decl = *(AstNode**)dynarray_get(impl->methods, m);
                    if (method_decl->node_type != AST_FUNCTION_DECLARATION) continue;
                    
                    AstFunctionDeclaration *func = &method_decl->data.function_declaration;
                    func->target_type_node = impl->target_type_node;
                    
                    if (func->type_params && func->type_params->count > 0) {
                        Symbol *sym = arena_calloc(ctx->store->arena, sizeof(Symbol));
                        sym->name_rec = func->intern_result;
                        sym->type = NULL;
                        sym->kind = SYMBOL_GENERIC_FUNCTION;
                        sym->decl_node = method_decl;
                        sym->is_pub = func->is_pub;
                        sym->filename = method_decl->filename;
                        sym->overloads = arena_calloc(ctx->store->arena, sizeof(DynArray));
            dynarray_init_in_arena(sym->overloads, ctx->store->arena, sizeof(Symbol*), 4);

                        Symbol *existing_method = (Symbol*)hashmap_get(target_type->as.struct_type.methods, func->intern_result->key, ptr_hash, ptr_cmp);
                        if (existing_method) {
                            if (existing_method->kind == SYMBOL_VALUE_FUNCTION || existing_method->kind == SYMBOL_GENERIC_FUNCTION) {
                                Symbol *set = scope_make_overload_set(ctx->store->arena, existing_method, sym);
                                hashmap_put(target_type->as.struct_type.methods, func->intern_result->key, set, ptr_hash, ptr_cmp);
                            } else if (existing_method->kind == SYMBOL_OVERLOAD_SET) {
                                scope_overload_set_add(existing_method, sym, ctx->store->arena);
                            }
                        } else {
                            hashmap_put(target_type->as.struct_type.methods, func->intern_result->key, sym, ptr_hash, ptr_cmp);
                        }
                        
                        CompilationUnit *unit = module_loader_get_unit(ctx->loader, decl->filename);
                        if (unit && unit->generic_templates) {
                            hashmap_put(unit->generic_templates, func->intern_result->key, method_decl, ptr_hash, ptr_cmp);
                        }
                        continue;
                    }

                    resolve_function_decl(ctx, global_scope, method_decl);
                    
                    Symbol *sym = arena_calloc(ctx->store->arena, sizeof(Symbol));
                    sym->name_rec = func->intern_result;
                    sym->type = method_decl->type;
                    sym->kind = SYMBOL_VALUE_FUNCTION;
                    sym->decl_node = method_decl;
                    sym->is_pub = func->is_pub;
                    sym->filename = method_decl->filename;
                    
                    Symbol *existing_method = (Symbol*)hashmap_get(target_type->as.struct_type.methods, func->intern_result->key, ptr_hash, ptr_cmp);
                    if (existing_method) {
                        if (existing_method->kind == SYMBOL_VALUE_FUNCTION || existing_method->kind == SYMBOL_GENERIC_FUNCTION) {
                            Symbol *set = scope_make_overload_set(ctx->store->arena, existing_method, sym);
                            hashmap_put(target_type->as.struct_type.methods, func->intern_result->key, set, ptr_hash, ptr_cmp);
                        } else if (existing_method->kind == SYMBOL_OVERLOAD_SET) {
                            scope_overload_set_add(existing_method, sym, ctx->store->arena);
                        }
                    } else {
                        hashmap_put(target_type->as.struct_type.methods, func->intern_result->key, sym, ptr_hash, ptr_cmp);
                    }
                }
            }
            continue;
        }

        if (decl->node_type != AST_FUNCTION_DECLARATION) continue;

        AstFunctionDeclaration *func = &decl->data.function_declaration;

        if (!func->target_type_node) continue;

        ctx->filename = decl->filename;

        // Skip methods on generic template structs
        Symbol *target_sym = NULL;
        if (func->target_type_node->node_type == AST_IDENTIFIER) {
            target_sym = scope_lookup_symbol(global_scope, func->target_type_node->data.identifier.intern_result, ctx->filename);
        }
        if (target_sym && target_sym->kind == SYMBOL_GENERIC_STRUCT) {
            continue;
        }

        // 1. Resolve target struct type
        Type *target_type = resolve_ast_type(ctx, global_scope, func->target_type_node);
        if (!target_type || target_type->kind != TYPE_STRUCT) {
            TypeError err = { .kind = TE_UNDECLARED, .span = func->target_type_node->span, .filename = ctx->filename };
            err.as.name.name = "Method must be bound to a struct type";
            dynarray_push_value(ctx->errors, &err);
            continue;
        }

        if (func->type_params && func->type_params->count > 0) {
            Symbol *sym = arena_calloc(ctx->store->arena, sizeof(Symbol));
            sym->name_rec = func->intern_result;
            sym->type = NULL;
            sym->kind = SYMBOL_GENERIC_FUNCTION;
            sym->decl_node = decl;
            sym->is_pub = func->is_pub;
            sym->filename = decl->filename;
            sym->overloads = arena_calloc(ctx->store->arena, sizeof(DynArray));
            dynarray_init_in_arena(sym->overloads, ctx->store->arena, sizeof(Symbol*), 4);

            Symbol *existing_method = (Symbol*)hashmap_get(target_type->as.struct_type.methods, func->intern_result->key, ptr_hash, ptr_cmp);
            if (existing_method) {
                if (existing_method->kind == SYMBOL_VALUE_FUNCTION || existing_method->kind == SYMBOL_GENERIC_FUNCTION) {
                    Symbol *set = scope_make_overload_set(ctx->store->arena, existing_method, sym);
                    hashmap_put(target_type->as.struct_type.methods, func->intern_result->key, set, ptr_hash, ptr_cmp);
                } else if (existing_method->kind == SYMBOL_OVERLOAD_SET) {
                    scope_overload_set_add(existing_method, sym, ctx->store->arena);
                }
            } else {
                hashmap_put(target_type->as.struct_type.methods, func->intern_result->key, sym, ptr_hash, ptr_cmp);
            }
            
            CompilationUnit *unit = module_loader_get_unit(ctx->loader, decl->filename);
            if (unit && unit->generic_templates) {
                hashmap_put(unit->generic_templates, func->intern_result->key, decl, ptr_hash, ptr_cmp);
            }
            continue;
        }

        // 2. Resolve method signature
        resolve_function_decl(ctx, global_scope, decl);

        // 3. Register in struct's method map
        Symbol *sym = arena_calloc(ctx->store->arena, sizeof(Symbol));
        sym->name_rec = func->intern_result;
        sym->type = decl->type;
        sym->kind = SYMBOL_VALUE_FUNCTION;
        sym->decl_node = decl;
        sym->is_pub = func->is_pub;
        sym->filename = decl->filename;

        Symbol *existing_method = (Symbol*)hashmap_get(target_type->as.struct_type.methods, func->intern_result->key, ptr_hash, ptr_cmp);
        if (existing_method) {
            if (existing_method->kind == SYMBOL_VALUE_FUNCTION) {
                Symbol *set = scope_make_overload_set(ctx->store->arena, existing_method, sym);
                hashmap_put(target_type->as.struct_type.methods, func->intern_result->key, set, ptr_hash, ptr_cmp);
            } else if (existing_method->kind == SYMBOL_OVERLOAD_SET) {
                if (!scope_overload_set_add(existing_method, sym, ctx->store->arena)) {
                    TypeError err = { .kind = TE_REDECLARATION, .span = decl->span, .filename = ctx->filename };
                    err.as.name.name = ((Slice*)func->intern_result->key)->ptr;
                    dynarray_push_value(ctx->errors, &err);
                }
            }
        } else {
            hashmap_put(target_type->as.struct_type.methods, func->intern_result->key, sym, ptr_hash, ptr_cmp);
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
            if (!target_sym) {
                Type *prim = (Type*)hashmap_get(ctx->store->primitive_registry, alias->target->data.identifier.intern_result->key, ptr_hash, ptr_cmp);
                if (prim) {
                    my_alias_sym->type = prim;
                    continue;
                }
            }
        } else if (alias->target->node_type == AST_MEMBER_EXPR) {
            check_expression(ctx, global_scope, alias->target, NULL);
            if (alias->target->node_type == AST_MEMBER_EXPR) {
                target_sym = alias->target->data.member_expr.symbol;
            } else if (alias->target->node_type == AST_IDENTIFIER) {
                target_sym = alias->target->data.identifier.symbol;
            }
        }
        
        if (target_sym) {
            my_alias_sym->target_symbol = target_sym;
            my_alias_sym->type = target_sym->type;
            continue;
        }

        if (alias->target->node_type == AST_GENERIC_INST_EXPR || alias->target->node_type == AST_TYPE) {
            Type *inst_type = resolve_ast_type(ctx, global_scope, alias->target);
            if (inst_type) {
                my_alias_sym->type = inst_type;
                my_alias_sym->target_symbol = NULL; 
            } else {
                TypeError err = { .kind = TE_UNDECLARED, .span = alias->target->span, .filename = ctx->filename };
                err.as.name.name = "Could not resolve alias target type";
                dynarray_push_value(ctx->errors, &err);
            }
            continue;
        } else {
             TypeError err = { .kind = TE_TYPE_MISMATCH, .span = alias->target->span, .filename = ctx->filename };
             err.as.name.name = "Alias target must be a symbol, path, or generic instantiation";
             dynarray_push_value(ctx->errors, &err);
             continue;
        }
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
            .kind = TE_VOID_VARIABLE, 
            .span = var_node->span, 
            .filename = ctx->filename
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
            case STRING_LITERAL:
            case NULL_LITERAL:
                // Not supported as simple numeric constants in symbols for now
                break;
            default:
                ICE("Unsupported LiteralType for constant symbol value: %d", cv->type);
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

    // Skip if this is a template (has type params or is a method on a generic struct template)
    if (decl->type_params && decl->type_params->count > 0) {
        ctx->filename = old_filename;
        return;
    }
    if (decl->target_type_node) {
        Symbol *target_sym = NULL;
        if (decl->target_type_node->node_type == AST_IDENTIFIER) {
            target_sym = scope_lookup_symbol(parent_scope, decl->target_type_node->data.identifier.intern_result, func_node->filename);
        }
        if (target_sym && target_sym->kind == SYMBOL_GENERIC_STRUCT) {
            ctx->filename = old_filename;
            return;
        }
    }

    Type *func_type = func_node->type;
    if (!func_type) return;

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
             // Fallback to rebuilding from module_path if needed
             size_t total_len = 0;
             for (size_t j = 0; j < import->module_path->count; j++) {
                InternResult *part = *(InternResult**)dynarray_get(import->module_path, j);
                Slice *s = (Slice*)part->key;
                total_len += s->len + (j > 0 ? 1 : 0);
             }

             char *rebuilt = arena_alloc(ctx->store->arena, total_len + 1);
             size_t r_len = 0;
             for (size_t j = 0; j < import->module_path->count; j++) {
                InternResult *part = *(InternResult**)dynarray_get(import->module_path, j);
                Slice *s = (Slice*)part->key;
                if (j > 0) rebuilt[r_len++] = '.';
                memcpy(rebuilt + r_len, s->ptr, s->len);
                r_len += s->len;
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
    register_primitives_to_scope(ctx->store, universe_scope, ctx->keywords);

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
        register_program_enums(ctx, unit->global_scope);
        register_program_globals(ctx, unit->global_scope);
        register_program_functions(ctx, unit->global_scope);
        register_program_aliases(ctx, unit->global_scope);
        group_program_methods(ctx, unit->global_scope);

        // Step B: Imports (Now that all dependency names are registered due to post-order)
        resolve_imports(ctx, unit);
        unit->imports_resolved = true;

        // Step C: Full Signatures (Types are now available via imports)
        resolve_program_aliases(ctx, unit->global_scope);
        resolve_program_structs(ctx, unit->global_scope);
        resolve_program_enums(ctx, unit->global_scope);
        resolve_program_globals(ctx, unit->global_scope);
        resolve_program_functions(ctx, unit->global_scope);
        resolve_program_methods(ctx, unit->global_scope);
        unit->signatures_resolved = true;
    }

    drain_mono_queue(ctx);

    // 3. Pass 2: Bodies (Global)
    // Keep a constant assignment of 1 so the `last_checked_pass` logic works internally for duplicate prevention
    ctx->current_pass = 1; 
    for (size_t i = 0; i < ctx->loader->units_ordered->count; i++) {
        CompilationUnit *unit = *(CompilationUnit**)dynarray_get(ctx->loader->units_ordered, i);
        ctx->filename = unit->absolute_path;
        ctx->program = unit->ast_root;

        AstProgram *program = &unit->ast_root->data.program;
        if (!program->decls) continue;

        for (size_t j = 0; j < program->decls->count; j++) {
            AstNode *decl = *(AstNode**)dynarray_get(program->decls, j);
            switch (decl->node_type) {
                case AST_VARIABLE_DECLARATION: check_variable_declaration(ctx, unit->global_scope, decl); break;
                case AST_FUNCTION_DECLARATION: check_function(ctx, unit->global_scope, decl); break;
                case AST_IMPL_DECLARATION: {
                    AstImplDeclaration *impl = &decl->data.impl_declaration;
                    if (impl->type_params && impl->type_params->count > 0) break; // Skip generic templates
                    
                    if (impl->methods) {
                        for (size_t k = 0; k < impl->methods->count; k++) {
                            AstNode *method = *(AstNode**)dynarray_get(impl->methods, k);
                            check_function(ctx, unit->global_scope, method);
                        }
                    }
                    break;
                }
                default: break;
            }
        }
    }
}

// --- Generic Monomorphization Helpers ---

static size_t type_mangled_len(Type *t) {
    if (!t) return 7; // unknown
    switch (t->kind) {
        case TYPE_VOID: return 4;
        case TYPE_PRIMITIVE:
            switch (t->as.primitive) {
                case PRIM_I8: case PRIM_U8: return 2;
                case PRIM_I16: case PRIM_U16: return 3;
                case PRIM_I32: case PRIM_U32: case PRIM_F32: return 3;
                case PRIM_I64: case PRIM_U64: case PRIM_F64: return 3;
                case PRIM_USIZE: case PRIM_ISIZE: return 5;
                case PRIM_BOOL: case PRIM_CHAR: return 4;
            }
            return 0;
        case TYPE_POINTER: return 4 + type_mangled_len(t->as.ptr.base);
        case TYPE_ARRAY: return 4 + type_mangled_len(t->as.array.base);
        case TYPE_SLICE: return 6 + type_mangled_len(t->as.slice.base);
        case TYPE_STRUCT:
            if (t->as.struct_type.name && t->as.struct_type.name->key) return ((Slice*)t->as.struct_type.name->key)->len;
            return 6; // struct
        case TYPE_GENERIC_INST: {
            size_t len = type_mangled_len(t->as.generic_inst.base);
            for (size_t i = 0; i < t->as.generic_inst.arg_count; i++) {
                len += 1 + type_mangled_len(t->as.generic_inst.args[i]);
            }
            return len;
        }
        case TYPE_TYPEVAR:
            if (t->as.typevar.name && t->as.typevar.name->key) return ((Slice*)t->as.typevar.name->key)->len;
            return 7; // typevar
        default: return 4; // type
    }
}

static void type_to_mangled_str_append(Type *t, char **buf) {
    if (!t) {
        memcpy(*buf, "unknown", 7); *buf += 7; return;
    }
    switch (t->kind) {
        case TYPE_VOID: memcpy(*buf, "void", 4); *buf += 4; break;
        case TYPE_PRIMITIVE:
            switch (t->as.primitive) {
                case PRIM_I8: memcpy(*buf, "i8", 2); *buf += 2; break;
                case PRIM_I16: memcpy(*buf, "i16", 3); *buf += 3; break;
                case PRIM_I32: memcpy(*buf, "i32", 3); *buf += 3; break;
                case PRIM_I64: memcpy(*buf, "i64", 3); *buf += 3; break;
                case PRIM_U8: memcpy(*buf, "u8", 2); *buf += 2; break;
                case PRIM_U16: memcpy(*buf, "u16", 3); *buf += 3; break;
                case PRIM_U32: memcpy(*buf, "u32", 3); *buf += 3; break;
                case PRIM_U64: memcpy(*buf, "u64", 3); *buf += 3; break;
                case PRIM_F32: memcpy(*buf, "f32", 3); *buf += 3; break;
                case PRIM_F64: memcpy(*buf, "f64", 3); *buf += 3; break;
                case PRIM_USIZE: memcpy(*buf, "usize", 5); *buf += 5; break;
                case PRIM_ISIZE: memcpy(*buf, "isize", 5); *buf += 5; break;
                case PRIM_BOOL: memcpy(*buf, "bool", 4); *buf += 4; break;
                case PRIM_CHAR: memcpy(*buf, "char", 4); *buf += 4; break;
            }
            break;
        case TYPE_POINTER:
            memcpy(*buf, "ptr_", 4); *buf += 4;
            type_to_mangled_str_append(t->as.ptr.base, buf);
            break;
        case TYPE_ARRAY:
            memcpy(*buf, "arr_", 4); *buf += 4;
            type_to_mangled_str_append(t->as.array.base, buf);
            break;
        case TYPE_SLICE:
            memcpy(*buf, "slice_", 6); *buf += 6;
            type_to_mangled_str_append(t->as.slice.base, buf);
            break;
        case TYPE_STRUCT:
            if (t->as.struct_type.name && t->as.struct_type.name->key) {
                Slice *s = (Slice*)t->as.struct_type.name->key;
                memcpy(*buf, s->ptr, s->len);
                *buf += s->len;
            } else {
                memcpy(*buf, "struct", 6); *buf += 6;
            }
            break;
        case TYPE_GENERIC_INST:
            type_to_mangled_str_append(t->as.generic_inst.base, buf);
            for (size_t i = 0; i < t->as.generic_inst.arg_count; i++) {
                **buf = '_'; (*buf)++;
                type_to_mangled_str_append(t->as.generic_inst.args[i], buf);
            }
            break;
        case TYPE_TYPEVAR:
            if (t->as.typevar.name && t->as.typevar.name->key) {
                Slice *s = (Slice*)t->as.typevar.name->key;
                memcpy(*buf, s->ptr, s->len);
                *buf += s->len;
            } else {
                memcpy(*buf, "typevar", 7); *buf += 7;
            }
            break;
        default:
            memcpy(*buf, "type", 4); *buf += 4;
            break;
    }
}

typedef struct {
    Symbol *sym;
    Type *inst_type;
    Scope *scope;
    Type **arg_types;
    size_t count;
    int depth;
} MonoJob;

static Type *instantiate_generic_struct(TypeCheckContext *ctx, Scope *scope, Symbol *sym, Type **arg_types, size_t count, Span error_span) {
    if (!ctx || !sym || !sym->decl_node) return NULL;
    AstNode *decl_node = sym->decl_node;
    if (decl_node->node_type != AST_STRUCT_DECLARATION) return NULL;
    AstStructDeclaration *struct_decl = &decl_node->data.struct_declaration;

    size_t expected_count = struct_decl->type_params ? struct_decl->type_params->count : 0;
    if (expected_count != count) {
        TypeError err = { .kind = TE_GENERIC_ARG_MISMATCH, .span = error_span, .filename = ctx->filename };
        err.as.generic_mismatch.name = ((Slice*)sym->name_rec->key)->ptr;
        err.as.generic_mismatch.expected = expected_count;
        err.as.generic_mismatch.provided = count;
        dynarray_push_value(ctx->errors, &err);
        return NULL;
    }

    if (ctx->current_mono_depth > 64) {
        TypeError err = { .kind = TE_INSTANTIATION_DEPTH, .span = error_span, .filename = ctx->filename };
        err.as.name.name = ((Slice*)sym->name_rec->key)->ptr;
        dynarray_push_value(ctx->errors, &err);
        return NULL;
    }

    Type *base_type = sym->type;
    if (!base_type) return NULL;

    Type *inst_type = make_generic_inst_type(ctx->store, base_type, arg_types, count);
    if (!inst_type) return NULL;

    if (inst_type->as.generic_inst.concrete_type) {
        return inst_type;
    }

    // Check if it's already in the queue to avoid duplicates
    for (size_t i = 0; i < ctx->mono_queue->count; i++) {
        MonoJob *job = *(MonoJob**)dynarray_get(ctx->mono_queue, i);
        if (job->inst_type == inst_type) {
            return inst_type; // Already queued
        }
    }

    MonoJob *job = arena_calloc(ctx->store->arena, sizeof(MonoJob));
    job->sym = sym;
    job->inst_type = inst_type;
    job->scope = scope;
    job->count = count;
    job->depth = ctx->current_mono_depth + 1;
    
    // Copy type arguments so they survive
    job->arg_types = arena_alloc(ctx->store->arena, sizeof(Type*) * count);
    memcpy(job->arg_types, arg_types, sizeof(Type*) * count);

    dynarray_push_value(ctx->mono_queue, &job);

    return inst_type;
}

static void drain_mono_queue(TypeCheckContext *ctx) {
    if (ctx->is_draining) return;
    ctx->is_draining = true;

    for (size_t q = 0; q < ctx->mono_queue->count; q++) {
        MonoJob *job = *(MonoJob**)dynarray_get(ctx->mono_queue, q);
        Type *inst_type = job->inst_type;
        if (inst_type->as.generic_inst.concrete_type) continue;

        ctx->current_mono_depth = job->depth;

        Symbol *sym = job->sym;
        Scope *scope = job->scope;
        size_t count = job->count;
        Type **arg_types = job->arg_types;
        
        AstNode *decl_node = sym->decl_node;
        AstStructDeclaration *struct_decl = &decl_node->data.struct_declaration;
        Type *base_type = sym->type;

        // Check for recursion AFTER popping from queue if needed, but true structural 
        // infinite recursion will just hang here. We should use a depth counter if we wanted to prevent hangs.
        if (sym->flags & SYMBOL_FLAG_COMPUTING) {
            TypeError err = { .kind = TE_INCOMPLETE_TYPE, .span = decl_node->span, .filename = ctx->filename };
            err.as.name.name = "Recursive instantiation of generic struct";
            dynarray_push_value(ctx->errors, &err);
            continue;
        }
        sym->flags |= SYMBOL_FLAG_COMPUTING;

    Slice *base_slice = (Slice*)base_type->as.struct_type.name->key;
    size_t base_len = base_slice->len;
    
    size_t total_len = base_len;
    for (size_t i = 0; i < count; i++) {
        total_len += 2 + type_mangled_len(arg_types[i]); // "__" + arg
    }
    
    char *name_buf = arena_alloc(ctx->store->arena, total_len + 1);
    char *ptr = name_buf;
    memcpy(ptr, base_slice->ptr, base_len);
    ptr += base_len;
    
    for (size_t i = 0; i < count; i++) {
        memcpy(ptr, "__", 2);
        ptr += 2;
        type_to_mangled_str_append(arg_types[i], &ptr);
    }
    *ptr = '\0';

    Slice mangled_slice = { .ptr = name_buf, .len = total_len };
    InternResult *mangled_res = intern(ctx->identifiers, &mangled_slice, NULL);

    CompilationUnit *unit = module_loader_get_unit(ctx->loader, sym->filename);
    Scope *parent_global = unit ? unit->global_scope : scope;
    Scope *inst_scope = scope_create(ctx->store->arena, parent_global, count, SCOPE_IDENTIFIERS);

    for (size_t i = 0; i < count; i++) {
        InternResult *tp_name = *(InternResult**)dynarray_get(struct_decl->type_params, i);
        define_symbol_or_error(ctx, inst_scope, tp_name, arg_types[i], SYMBOL_VALUE_TYPE, sym->span, false, sym->filename, NULL);
    }

    Type *concrete_struct = arena_calloc(ctx->store->arena, sizeof(Type));
    concrete_struct->kind = TYPE_STRUCT;
    concrete_struct->as.struct_type.name = mangled_res;
    concrete_struct->as.struct_type.decl_node = sym->decl_node;
    concrete_struct->as.struct_type.field_count = struct_decl->fields ? struct_decl->fields->count : 0;
    concrete_struct->as.struct_type.fields = concrete_struct->as.struct_type.field_count > 0 ? arena_alloc(ctx->store->arena, sizeof(StructField) * concrete_struct->as.struct_type.field_count) : NULL;
    concrete_struct->as.struct_type.field_map = hashmap_create(ctx->store->arena, concrete_struct->as.struct_type.field_count);
    concrete_struct->as.struct_type.methods = hashmap_create(ctx->store->arena, 4);

    inst_type->as.generic_inst.concrete_type = concrete_struct;

    const char *saved_filename = ctx->filename;
    ctx->filename = sym->filename;
    for (size_t j = 0; j < concrete_struct->as.struct_type.field_count; j++) {
        AstFieldDecl *fdecl = (AstFieldDecl*)dynarray_get(struct_decl->fields, j);
        concrete_struct->as.struct_type.fields[j].name = fdecl->name;
        concrete_struct->as.struct_type.fields[j].type = resolve_ast_type(ctx, inst_scope, fdecl->type);
        hashmap_put(concrete_struct->as.struct_type.field_map, fdecl->name->key, (void*)(uintptr_t)(j + 1), ptr_hash, ptr_cmp);
    }
    ctx->filename = saved_filename;

    sym->flags &= ~SYMBOL_FLAG_COMPUTING;
    }
    ctx->is_draining = false;
}

Symbol *instantiate_generic_method(TypeCheckContext *ctx, Scope *scope, Type *inst_type, AstNode *method_node) {
    if (!ctx || !scope || !inst_type || inst_type->kind != TYPE_GENERIC_INST || !method_node) return NULL;
    
    Type *base_type = inst_type->as.generic_inst.base;
    Type *concrete_struct = inst_type->as.generic_inst.concrete_type;
    if (!base_type || !concrete_struct) return NULL;
    
    AstFunctionDeclaration *orig_func = &method_node->data.function_declaration;
    InternResult *orig_name = orig_func->intern_result;
    
    // Check if already monomorphized
    Symbol *existing = (Symbol*)hashmap_get(concrete_struct->as.struct_type.methods, orig_name->key, ptr_hash, ptr_cmp);
    if (existing) return existing;
    
    // Get the decl_node directly from the struct type!
    AstNode *decl_node = base_type->as.struct_type.decl_node;
    if (!decl_node || decl_node->node_type != AST_STRUCT_DECLARATION) {
        return NULL;
    }
    
    // The method's parent scope should be the global scope where the struct was DEFINED!
    CompilationUnit *unit = module_loader_get_unit(ctx->loader, decl_node->filename);
    Scope *parent_global = unit ? unit->global_scope : scope;
    
    AstNode *mono_method = ast_clone_node(method_node, ctx->store->arena);
    if (!mono_method) return NULL;
    
    AstFunctionDeclaration *mono_func = &mono_method->data.function_declaration;
    
    AstStructDeclaration *struct_decl = &decl_node->data.struct_declaration;
    size_t count = inst_type->as.generic_inst.arg_count;
    
    Scope *inst_scope = scope_create(ctx->store->arena, parent_global, count, SCOPE_IDENTIFIERS);
    for (size_t i = 0; i < count; i++) {
        InternResult *tp_name = *(InternResult**)dynarray_get(struct_decl->type_params, i);
        define_symbol_or_error(ctx, inst_scope, tp_name, inst_type->as.generic_inst.args[i], SYMBOL_VALUE_TYPE, decl_node->span, false, decl_node->filename, NULL);
    }
    
    // Generate LLVM mangled name: Vec__i32_push
    Slice *base_slice = (Slice*)base_type->as.struct_type.name->key;
    size_t base_len = base_slice->len;
    
    size_t total_len = base_len;
    for (size_t i = 0; i < count; i++) {
        total_len += 2 + type_mangled_len(inst_type->as.generic_inst.args[i]);
    }
    
    Slice *orig_slice = (Slice*)orig_name->key;
    size_t m_len = total_len + 1 + orig_slice->len; // name_buf + '_' + orig_name
    
    char *m_name = arena_alloc(ctx->store->arena, m_len + 1);
    char *ptr = m_name;
    memcpy(ptr, base_slice->ptr, base_len);
    ptr += base_len;
    
    for (size_t i = 0; i < count; i++) {
        memcpy(ptr, "__", 2);
        ptr += 2;
        type_to_mangled_str_append(inst_type->as.generic_inst.args[i], &ptr);
    }
    
    *ptr = '_';
    ptr++;
    memcpy(ptr, orig_slice->ptr, orig_slice->len);
    ptr += orig_slice->len;
    *ptr = '\0';
    
    Slice m_slice = { .ptr = m_name, .len = m_len };
    InternResult *mono_m_res = intern(ctx->identifiers, &m_slice, NULL);
    
    mono_func->intern_result = orig_name; 
    
    if (orig_func->type_params && orig_func->type_params->count > 0) {
        mono_func->type_params = orig_func->type_params; 
        if (mono_func->target_type_node && mono_func->target_type_node->node_type == AST_IDENTIFIER) {
            mono_func->target_type_node->data.identifier.intern_result = concrete_struct->as.struct_type.name;
        }
        
        Symbol *method_sym = arena_calloc(ctx->store->arena, sizeof(Symbol));
        method_sym->name_rec = mono_func->intern_result;
        method_sym->kind = SYMBOL_GENERIC_FUNCTION;
        method_sym->decl_node = mono_method;
        method_sym->is_pub = mono_func->is_pub;
        method_sym->filename = mono_method->filename;
        method_sym->module_scope = inst_scope; // Save struct's generic bindings
        method_sym->overloads = arena_calloc(ctx->store->arena, sizeof(DynArray));
        dynarray_init_in_arena(method_sym->overloads, ctx->store->arena, sizeof(Symbol*), 4);
        
        hashmap_put(concrete_struct->as.struct_type.methods, orig_name->key, method_sym, ptr_hash, ptr_cmp);
        return method_sym;
    }

    mono_func->type_params = NULL; 
    if (mono_func->target_type_node && mono_func->target_type_node->node_type == AST_IDENTIFIER) {
        mono_func->target_type_node->data.identifier.intern_result = concrete_struct->as.struct_type.name;
    }
    
    Symbol *method_sym = arena_calloc(ctx->store->arena, sizeof(Symbol));
    method_sym->name_rec = mono_func->intern_result;
    method_sym->kind = SYMBOL_VALUE_FUNCTION;
    method_sym->decl_node = mono_method;
    method_sym->is_pub = mono_func->is_pub;
    method_sym->filename = mono_method->filename;

    const char *saved_filename = ctx->filename;
    ctx->filename = method_sym->filename;

    resolve_function_decl(ctx, inst_scope, mono_method);
    
    method_sym->type = mono_method->type;
    hashmap_put(concrete_struct->as.struct_type.methods, orig_name->key, method_sym, ptr_hash, ptr_cmp);
    
    define_symbol_or_error(ctx, parent_global, mono_m_res, mono_method->type, SYMBOL_VALUE_FUNCTION, mono_method->span, mono_func->is_pub, mono_method->filename, mono_method);
    
    unit = module_loader_get_unit(ctx->loader, method_sym->filename);
    if (unit && unit->mono_instances) {
        dynarray_push_value(unit->mono_instances, &mono_method);
    }
    
    check_function(ctx, inst_scope, mono_method);
    ctx->filename = saved_filename;
    
    return method_sym;
}

Symbol *instantiate_generic_function(TypeCheckContext *ctx, Scope *scope, Symbol *sym, Type **arg_types, size_t count, Span error_span) {
    if (!ctx || !scope || !sym || !sym->decl_node) return NULL;
    
    AstNode *decl_node = sym->decl_node;
    if (decl_node->node_type != AST_FUNCTION_DECLARATION) return NULL;
    AstFunctionDeclaration *func_decl = &decl_node->data.function_declaration;
    
    size_t expected_count = func_decl->type_params ? func_decl->type_params->count : 0;
    if (expected_count != count) {
        TypeError err = { .kind = TE_GENERIC_ARG_MISMATCH, .span = error_span, .filename = ctx->filename };
        err.as.generic_mismatch.name = ((Slice*)sym->name_rec->key)->ptr;
        err.as.generic_mismatch.expected = expected_count;
        err.as.generic_mismatch.provided = count;
        dynarray_push_value(ctx->errors, &err);
        return NULL;
    }

    if (ctx->current_mono_depth > 64) {
        TypeError err = { .kind = TE_INSTANTIATION_DEPTH, .span = error_span, .filename = ctx->filename };
        err.as.name.name = ((Slice*)sym->name_rec->key)->ptr;
        dynarray_push_value(ctx->errors, &err);
        return NULL;
    }
    
    if (!sym->overloads) {
        sym->overloads = arena_calloc(ctx->store->arena, sizeof(DynArray));
        dynarray_init_in_arena(sym->overloads, ctx->store->arena, sizeof(Symbol*), 4);
    }
    
    // Generate LLVM mangled name: abs__i32
    Slice *base_slice = (Slice*)sym->name_rec->key;
    size_t base_len = base_slice->len;
    
    size_t total_len = base_len;
    for (size_t i = 0; i < count; i++) {
        total_len += 2 + type_mangled_len(arg_types[i]);
    }
    
    char *name_buf = arena_alloc(ctx->store->arena, total_len + 1);
    char *ptr = name_buf;
    memcpy(ptr, base_slice->ptr, base_len);
    ptr += base_len;
    
    for (size_t i = 0; i < count; i++) {
        memcpy(ptr, "__", 2);
        ptr += 2;
        type_to_mangled_str_append(arg_types[i], &ptr);
    }
    *ptr = '\0';
    
    Slice mangled_slice = { .ptr = name_buf, .len = total_len };
    InternResult *mangled_res = intern(ctx->identifiers, &mangled_slice, NULL);
    
    // Check cache by mangled name
    for (size_t i = 0; i < sym->overloads->count; i++) {
        Symbol *inst = *(Symbol**)dynarray_get(sym->overloads, i);
        if (inst->name_rec == mangled_res) return inst;
    }
    
    // The function's parent scope should be the global scope where it was DEFINED!
    // If it's a generic method on a generic struct, sym->module_scope holds the struct's instantiation scope.
    CompilationUnit *unit = module_loader_get_unit(ctx->loader, sym->filename);
    Scope *parent_global = sym->module_scope ? sym->module_scope : (unit ? unit->global_scope : scope);
    
    AstNode *mono_node = ast_clone_node(decl_node, ctx->store->arena);
    if (!mono_node) return NULL;
    AstFunctionDeclaration *mono_func = &mono_node->data.function_declaration;
    
    Scope *inst_scope = scope_create(ctx->store->arena, parent_global, count, SCOPE_IDENTIFIERS);
    for (size_t i = 0; i < count; i++) {
        InternResult *tp_name = *(InternResult**)dynarray_get(func_decl->type_params, i);
        define_symbol_or_error(ctx, inst_scope, tp_name, arg_types[i], SYMBOL_VALUE_TYPE, decl_node->span, false, decl_node->filename, NULL);
    }
    
    mono_func->intern_result = mangled_res;
    mono_func->type_params = NULL; // No longer generic
    
    Symbol *inst_sym = arena_calloc(ctx->store->arena, sizeof(Symbol));
    inst_sym->name_rec = mangled_res;
    inst_sym->kind = SYMBOL_VALUE_FUNCTION;
    inst_sym->decl_node = mono_node;
    inst_sym->is_pub = mono_func->is_pub;
    inst_sym->filename = mono_node->filename;

    const char *saved_filename = ctx->filename;
    ctx->filename = inst_sym->filename;

    ctx->current_mono_depth++;

    resolve_function_decl(ctx, inst_scope, mono_node);
    
    inst_sym->type = mono_node->type;
    
    dynarray_push_value(sym->overloads, &inst_sym);
    
    // Register it in the parent module so it gets exported or found
    define_symbol_or_error(ctx, parent_global, mangled_res, mono_node->type, SYMBOL_VALUE_FUNCTION, mono_node->span, mono_func->is_pub, mono_node->filename, mono_node);
    
    if (unit && unit->mono_instances) {
        dynarray_push_value(unit->mono_instances, &mono_node);
    }
    
    check_function(ctx, inst_scope, mono_node);

    ctx->current_mono_depth--;
    ctx->filename = saved_filename;
    
    return inst_sym;
}