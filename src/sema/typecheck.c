#include "sema/typecheck.h"
#include "sema/type_utils.h"
#include "sema/symbol_utils.h"
#include "sema/typecheck_expr.h"
#include "core/utils.h"
#include "datastructures/dynamic_array.h"
#include <string.h>
#include <stdlib.h>
#include <stdio.h>

// Forward declarations
static void check_statement(TypeCheckContext *ctx, Scope *scope, AstNode *stmt, Type *return_type);
static void check_block(TypeCheckContext *ctx, Scope *parent, AstNode *block_node, Type *return_type, bool create_new_scope);

TypeCheckContext typecheck_context_create(Arena *arena, AstNode *program, TypeStore *store, DenseArenaInterner *identifiers, DenseArenaInterner *keywords, const char *filename) {
    DynArray *errors = arena_alloc(arena, sizeof(DynArray));
    dynarray_init_in_arena(errors, arena, sizeof(TypeError), 8);

    TypeCheckContext ctx = {
        .program = program,
        .store = store,
        .identifiers = identifiers,
        .keywords = keywords,
        .filename = filename,
        .errors = errors
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

    if (type_ast->data.ast_type.kind == AST_TYPE_ARRAY && concrete_type->kind == TYPE_ARRAY) {
        patch_inferred_array_sizes(ctx, 
            type_ast->data.ast_type.u.array.elem, 
            concrete_type->as.array.base
        );

        if (!type_ast->data.ast_type.u.array.size_expr && concrete_type->as.array.size_known) {
            AstNode *size_lit = ast_create_node(AST_LITERAL, ctx->store->arena);
            if (size_lit) {
                size_lit->node_type = AST_LITERAL;
                size_lit->span = type_ast->span;
                size_lit->type = ctx->store->t_i64;
                size_lit->is_const_expr = 1;
                size_lit->data.literal.type = INT_LITERAL;
                size_lit->data.literal.value.int_val = (int64_t)concrete_type->as.array.size;
                size_lit->const_value.type = INT_LITERAL;
                size_lit->const_value.value.int_val = (int64_t)concrete_type->as.array.size;
                type_ast->data.ast_type.u.array.size_expr = size_lit;
            }
        }
    }
}

Type *resolve_ast_type(TypeCheckContext *ctx, Scope *scope, AstNode *node) {
    if (!ctx || !node) return NULL;
    TypeStore *store = ctx->store;
    if (node->node_type != AST_TYPE) return NULL;

    AstType *ast_ty = &node->data.ast_type;

    switch (ast_ty->kind) {
        case AST_TYPE_PRIMITIVE: {
            InternResult *name_res = ast_ty->u.base.intern_result;
            if (name_res && name_res->key) {
                Type *prim = (Type*)hashmap_get(store->primitive_registry, name_res->key, ptr_hash, ptr_cmp);
                if (prim) return prim;
                
                if (scope) {
                    Symbol *sym = scope_lookup_symbol(scope, name_res);
                    if (sym) {
                        if (sym->kind == SYMBOL_VALUE_TYPE) return sym->type;
                        else printf("DEBUG: Found symbol '%s' but kind is %d, expected %d\n", ((Slice*)name_res->key)->ptr, sym->kind, SYMBOL_VALUE_TYPE);
                    } else {
                        printf("DEBUG: scope_lookup_symbol returned NULL for '%s'\n", ((Slice*)name_res->key)->ptr);
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
                    if (!sz->is_const_expr) {
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

            Type proto = { .kind = TYPE_ARRAY, .as.array.base = elem, .as.array.size = size, .as.array.size_known = size_known };
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
                 else param_types = malloc(sizeof(Type*) * count);
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
    if (param_count > 0) param_types = malloc(sizeof(Type*) * param_count);

    for (size_t i = 0; i < param_count; i++) {
        AstNode *param_node = *(AstNode**)dynarray_get(decl->params, i);
        Type *pt = resolve_ast_type(ctx, scope, param_node->data.param.type);
        if (!pt) pt = ctx->store->t_void; 
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

void resolve_program_structs(TypeCheckContext *ctx, Scope *global_scope) {
    if (!ctx || !ctx->program) return;
    AstProgram *program = &ctx->program->data.program;
    if (!program->decls) return;

    // Pass 1: Register incomplete struct types
    for (size_t i = 0; i < program->decls->count; i++) {
        AstNode *decl = *(AstNode**)dynarray_get(program->decls, i);
        if (!decl || decl->node_type != AST_STRUCT_DECLARATION) continue;
        
        AstStructDeclaration *struct_decl = &decl->data.struct_declaration;
        if (!struct_decl->intern_result) continue;

        Type *struct_type = arena_alloc(ctx->store->arena, sizeof(Type));
        struct_type->kind = TYPE_STRUCT;
        struct_type->as.struct_type.name = struct_decl->intern_result;
        struct_type->as.struct_type.field_count = struct_decl->fields ? struct_decl->fields->count : 0;
        struct_type->as.struct_type.fields = NULL; // Fill in Pass 2
        
        // Don't intern incomplete structs yet, we need to mutate them
        decl->type = struct_type;

        define_symbol_or_error(ctx, global_scope, struct_decl->intern_result, decl->type, SYMBOL_VALUE_TYPE, decl->span);
    }

    // Pass 2: Resolve struct fields
    for (size_t i = 0; i < program->decls->count; i++) {
        AstNode *decl = *(AstNode**)dynarray_get(program->decls, i);
        if (!decl || decl->node_type != AST_STRUCT_DECLARATION) continue;
        
        AstStructDeclaration *struct_decl = &decl->data.struct_declaration;
        if (!struct_decl->fields || struct_decl->fields->count == 0) continue;

        Type *struct_type = decl->type; 
        
        struct_type->as.struct_type.fields = arena_alloc(ctx->store->arena, sizeof(StructField) * struct_type->as.struct_type.field_count);
        
        for (size_t j = 0; j < struct_type->as.struct_type.field_count; j++) {
            AstFieldDecl *fdecl = (AstFieldDecl*)dynarray_get(struct_decl->fields, j);
            struct_type->as.struct_type.fields[j].name = fdecl->name;
            struct_type->as.struct_type.fields[j].type = resolve_ast_type(ctx, global_scope, fdecl->type);
        }
    }
}

void resolve_program_functions(TypeCheckContext *ctx, Scope *global_scope) {
    if (!ctx || !ctx->program) return;
    AstProgram *program = &ctx->program->data.program;
    if (!program->decls) return;

    for (size_t i = 0; i < program->decls->count; i++) {
        AstNode *decl = *(AstNode**)dynarray_get(program->decls, i);
        if (!decl) continue;
        if (decl->node_type == AST_FUNCTION_DECLARATION) {
             resolve_function_decl(ctx, global_scope, decl);
             AstFunctionDeclaration *func = &decl->data.function_declaration;
             if (func->intern_result && decl->type) {
                 define_symbol_or_error(ctx, global_scope, func->intern_result, decl->type, SYMBOL_VALUE_FUNCTION, decl->span);
             }
        }
    }
}

// -----------------------------------------------------------------------------
// Type Checking Logic
// -----------------------------------------------------------------------------

static void check_initializer(TypeCheckContext *ctx, Scope *scope, AstNode *var_node, AstNode *initializer, Type *var_type, Symbol *sym) {
    if (!initializer) return;
    Type *actual_type = check_expression(ctx, scope, initializer, var_type);

    if (!actual_type) return; 

    if (actual_type && var_type && actual_type != var_type) {
        
        bool can_cast = type_can_implicit_cast(var_type, actual_type);

        // INFERENCE LOGIC: If variable is T[] (unknown size) and initializer is T[N] (known), update it.
        // We do this if 'can_cast' is true OR if it's purely an array size refinement.
        bool is_inference = (var_type->kind == TYPE_ARRAY && !var_type->as.array.size_known && actual_type->kind == TYPE_ARRAY);

        if (is_inference) {
            var_node->type = actual_type;
            if (sym) sym->type = actual_type;
            if (var_node->node_type == AST_VARIABLE_DECLARATION) {
                patch_inferred_array_sizes(ctx, var_node->data.variable_declaration.type, actual_type);
            }
            return;
        }

        if (can_cast) {
            insert_cast(ctx, initializer, var_type);
        } else {
            TypeError err = {
                .kind = TE_TYPE_MISMATCH,
                .span = initializer->span,
                .filename = ctx->filename,
                .as.mismatch = { .expected = var_type, .actual = actual_type }
            };
            dynarray_push_value(ctx->errors, &err);
        }
    }
}

static bool is_type_complete(Type *t) {
    if (!t) return false;
    if (t->kind == TYPE_ARRAY) {
        if (!t->as.array.size_known) return false;
        return is_type_complete(t->as.array.base);
    }
    return true; 
}

void check_variable_declaration(TypeCheckContext *ctx, Scope *scope, AstNode *var_node) {
    if (var_node->node_type != AST_VARIABLE_DECLARATION) return;
    AstVariableDeclaration *var_decl = &var_node->data.variable_declaration;

    Type *var_type = resolve_ast_type(ctx, scope, var_decl->type);
    if (!var_type) return; 

    // FIX: Check for incomplete types (missing size without initializer)
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
    
    Symbol *sym = NULL;
    if (var_decl->intern_result) {
        define_symbol_or_error(ctx, scope, var_decl->intern_result, var_type, SYMBOL_VARIABLE, var_node->span);
        sym = scope_lookup_symbol(scope, var_decl->intern_result);
    }

    check_initializer(ctx, scope, var_node, var_decl->initializer, var_type, sym);

    if (sym && var_decl->is_const && var_decl->initializer && var_decl->initializer->is_const_expr) {
        sym->flags |= SYMBOL_FLAG_CONST | SYMBOL_FLAG_COMPUTED_VALUE;
        ConstValue *cv = &var_decl->initializer->const_value;
        switch (cv->type) {
            case INT_LITERAL:   sym->value.int_val = cv->value.int_val; break;
            case FLOAT_LITERAL: sym->value.float_val = cv->value.float_val; break;
            case BOOL_LITERAL:  sym->value.bool_val = (bool)cv->value.bool_val; break;
            case CHAR_LITERAL:  sym->value.int_val = (int64_t)cv->value.char_val; break;
            default: break;
        }
    }
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
                 if (type_can_implicit_cast(return_type, actual)) {
                    insert_cast(ctx, ret->expression, return_type);
                 } else {
                    TypeError err = { .kind = TE_TYPE_MISMATCH, .span = stmt->span, .filename = ctx->filename, .as.mismatch = { .expected = return_type, .actual = actual } };
                    dynarray_push_value(ctx->errors, &err);
                 }
            }
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
            check_expression(ctx, scope, stmt, NULL);
            break;
        // -----------------------
        default: 
            break; 
    }
}

static void check_function(TypeCheckContext *ctx, Scope *parent_scope, AstNode *func_node) {
    AstFunctionDeclaration *decl = &func_node->data.function_declaration;
    Type *func_type = func_node->type;

    Scope *fn_scope = scope_create(ctx->store->arena, parent_scope, 32, SCOPE_IDENTIFIERS);
    if (decl->params) {
        for (size_t i = 0; i < decl->params->count; i++) {
            AstNode *param = *(AstNode**)dynarray_get(decl->params, i);
            if (param->data.param.name_idx != -1) {
                InternResult *name_rec = interner_get_result(ctx->identifiers, param->data.param.name_idx);
                define_symbol_or_error(ctx, fn_scope, name_rec, param->type, SYMBOL_VARIABLE, param->span);
            }
        }
    }
    if (decl->body) {
        check_block(ctx, fn_scope, decl->body, func_type->as.func.return_type, false);
    }
}

void typecheck_program(TypeCheckContext *ctx) {
    if (!ctx || !ctx->program) return;
    Arena *scope_arena = ctx->store->arena;
    int global_count = (ctx->identifiers ? ctx->identifiers->dense_index_count : 0) + 64;
    Scope *global_scope = scope_create(scope_arena, NULL, global_count, SCOPE_IDENTIFIERS);  
    
    register_intrinsics(ctx->store, global_scope, ctx->identifiers);
    resolve_program_structs(ctx, global_scope);
    resolve_program_functions(ctx, global_scope);

    AstProgram *program = &ctx->program->data.program;
    if (!program->decls) return;

    for (size_t i = 0; i < program->decls->count; i++) {
        AstNode *decl = *(AstNode**)dynarray_get(program->decls, i);
        switch (decl->node_type) {
            case AST_VARIABLE_DECLARATION: check_variable_declaration(ctx, global_scope, decl); break;
            case AST_FUNCTION_DECLARATION: check_function(ctx, global_scope, decl); break;
            case AST_STRUCT_DECLARATION: /* Handled in resolve_program_structs */ break;
            default: break;
        }
    }
}