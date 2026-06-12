#pragma once

#include "parsing/ast.h"
#include "arena.h"
#include "cli.h"
#include "hash_map.h"

typedef struct {
    Arena *arena;
    Options *opts;
    DenseArenaInterner *keywords;
    DenseArenaInterner *identifiers;
    DenseArenaInterner *strings;
    HashMap *visited_modules; // char* -> bool
    AstNode *merged_program;
} ModuleLoader;

ModuleLoader* module_loader_create(Arena *arena, Options *opts, 
                                   DenseArenaInterner *keywords, 
                                   DenseArenaInterner *identifiers, 
                                   DenseArenaInterner *strings);

int load_module_recursive(ModuleLoader *loader, const char *path);
