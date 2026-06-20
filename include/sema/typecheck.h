#pragma once

#include "parsing/ast.h"
#include "datastructures/scope.h"
#include "datastructures/arena.h"
#include "datastructures/dense_arena_interner.h"
#include "sema/type.h"
#include "sema/type_report.h" 
#include "core/module_loader.h"

typedef struct {
    AstNode *program;
    TypeStore *store;
    DenseArenaInterner *identifiers;
    DenseArenaInterner *keywords;
    const char *filename;
    DynArray *errors; // DynArray<TypeError>
    ModuleLoader *loader;
    int current_pass;
    DynArray *mono_queue; // Queue of MonoJob*
    bool is_draining;
    int current_mono_depth;
} TypeCheckContext;

// Context creation
TypeCheckContext typecheck_context_create(Arena *arena, TypeStore *store, DenseArenaInterner *identifiers, DenseArenaInterner *keywords, const char *filename, ModuleLoader *loader);

// Main Entry Point
void typecheck_program(TypeCheckContext *ctx);

// AST -> Type resolution (Updated to take Context)
Type *resolve_ast_type(TypeCheckContext *ctx, Scope *scope, AstNode *node);

void check_variable_declaration(TypeCheckContext *ctx, Scope *scope, AstNode *var_node);

// Lazy monomorphization for methods
Symbol *instantiate_generic_method(TypeCheckContext *ctx, Scope *scope, Type *inst_type, AstNode *method_node);

// Instantiation for generic functions
Symbol *instantiate_generic_function(TypeCheckContext *ctx, Scope *scope, Symbol *sym, Type **arg_types, size_t count, Span error_span);