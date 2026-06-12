#include "module_loader.h"
#include "file.h"
#include "lexing/lexer.h"
#include "parsing/parser.h"
#include "parsing/parse_statements.h"
#include <stdio.h>
#include <string.h>

#define EXIT_OK    0
#define EXIT_USAGE 1
#define EXIT_IO    2
#define EXIT_LEX   3
#define EXIT_PARSE 4
#define EXIT_TYPE  5

ModuleLoader* module_loader_create(Arena *arena, Options *opts, 
                                   DenseArenaInterner *keywords, 
                                   DenseArenaInterner *identifiers, 
                                   DenseArenaInterner *strings) {
    ModuleLoader *loader = arena_alloc(arena, sizeof(ModuleLoader));
    loader->arena = arena;
    loader->opts = opts;
    loader->keywords = keywords;
    loader->identifiers = identifiers;
    loader->strings = strings;
    loader->visited_modules = hashmap_create(16);
    loader->merged_program = ast_create_node(AST_PROGRAM, arena);
    loader->merged_program->data.program.decls = arena_alloc(arena, sizeof(DynArray));
    dynarray_init_in_arena(loader->merged_program->data.program.decls, arena, sizeof(AstNode*), 32);
    return loader;
}

int load_module_recursive(ModuleLoader *loader, const char *path) {
    // 1. Avoid circularity
    if (hashmap_get(loader->visited_modules, (void*)path, str_hash, str_cmp)) {
        return EXIT_OK;
    }
    hashmap_put(loader->visited_modules, (void*)path, (void*)1, str_hash, str_cmp);

    if (loader->opts->verbose) printf("Loading module: %s\n", path);

    // 2. Read Source
    char *src = read_file(path);
    if (!src) {
        fprintf(stderr, "Error: Failed to read file: %s\n", path);
        return EXIT_IO;
    }
    size_t src_len = strlen(src);

    // 3. Lexing (Shared Interners)
    Lexer *lexer = lexer_create_ex(src, src_len, loader->arena, loader->keywords, loader->identifiers, loader->strings);
    if (!lexer_lex_all(lexer)) {
        fprintf(stderr, "Error: Lexing failed for %s\n", path);
        free(src);
        return EXIT_LEX;
    }

    // 4. Parsing
    Parser *parser = parser_create(lexer->tokens, (char*)path, loader->arena);
    ParseError parse_err = {0};
    AstNode *module_ast = parse_program(parser, &parse_err);
    if (parse_err.message) {
        print_parse_error(&parse_err);
        free(src);
        return EXIT_PARSE;
    }

    if (!module_ast) {
        free(src);
        return EXIT_OK;
    }

    // 5. Recursive Loading and Merging
    AstProgram *module_prog = &module_ast->data.program;
    for (size_t i = 0; i < module_prog->decls->count; i++) {
        AstNode *decl = *(AstNode**)dynarray_get(module_prog->decls, i);
        
        if (decl->node_type == AST_IMPORT_DECLARATION) {
            // Simple path resolution: input/<module_name>.tn
            Slice *mod_name = (Slice*)decl->data.import_declaration.module_name->key;
            char mod_path[256];
            snprintf(mod_path, sizeof(mod_path), "input/%.*s.tn", (int)mod_name->len, mod_name->ptr);
            
            // Intern the path string so it survives recursion and can be used as hash key
            char *interned_path = arena_alloc(loader->arena, strlen(mod_path) + 1);
            strcpy(interned_path, mod_path);

            int res = load_module_recursive(loader, interned_path);
            if (res != EXIT_OK) {
                free(src);
                return res;
            }
        } else {
            // Add non-import declarations to the global pool
            dynarray_push_value(loader->merged_program->data.program.decls, &decl);
        }
    }

    return EXIT_OK;
}
