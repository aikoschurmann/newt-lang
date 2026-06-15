#pragma once

#include "parsing/ast.h"
#include "arena.h"
#include "cli.h"
#include "hash_map.h"
#include "datastructures/scope.h"

typedef struct CompilationUnit {
    char *absolute_path;
    char *logical_path;     // e.g., "std.io"
    AstNode *ast_root;
    Scope *global_scope;
    bool signatures_resolved;
    bool imports_resolved;
} CompilationUnit;

typedef struct {
    Arena *arena;
    Options *opts;
    DenseArenaInterner *keywords;
    DenseArenaInterner *identifiers;
    DenseArenaInterner *strings;

    HashMap *units; // char* (abs_path) -> CompilationUnit*
    HashMap *units_by_logical_path; // char* (logical) -> CompilationUnit*
    DynArray *units_ordered; // DynArray<CompilationUnit*> (post-order)

    char *project_root; // Absolute path to entry point directory
} ModuleLoader;


ModuleLoader* module_loader_create(Arena *arena, Options *opts, 
                                   DenseArenaInterner *keywords, 
                                   DenseArenaInterner *identifiers, 
                                   DenseArenaInterner *strings);

int load_module_recursive(ModuleLoader *loader, const char *path, const char *logical_path, const char *importer_path, int depth);
CompilationUnit* module_loader_get_unit(ModuleLoader *loader, const char *path);
