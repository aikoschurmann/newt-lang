// main.c - driver: orchestrates lexing, parsing, type internment, reporting

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "file.h"
#include "lexer.h"
#include "parser.h"
#include "parse_statements.h"
#include "ast.h"
#include "arena.h"
#include "type.h"
#include "typecheck.h"
#include "type_print.h"
#include "scope.h"
#include "cli.h"
#include "metrics.h"
#include "utils.h"
#include "codegen.h"



// Exit codes
#define EXIT_OK    0
#define EXIT_USAGE 1
#define EXIT_IO    2
#define EXIT_LEX   3
#define EXIT_PARSE 4
#define EXIT_TYPE  5

typedef struct {
    Arena *arena;
    Options *opts;
    DenseArenaInterner *keywords;
    DenseArenaInterner *identifiers;
    DenseArenaInterner *strings;
    HashMap *visited_modules; // char* -> bool
    AstNode *merged_program;
} ModuleLoader;

static int load_module_recursive(ModuleLoader *loader, const char *path) {
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

    // Note: We don't free 'src' yet because tokens/slices might point into it
    // In a production compiler, we'd probably copy lexemes to the arena
    return EXIT_OK;
}

int main(int argc, char **argv) {
    codegen_initialize();
    // 1. Parse CLI Options
    Options opts;
    const char *path = NULL;
    if (!parse_options(argc, argv, &opts, &path)) {
        return EXIT_USAGE;
    }

    int exit_code = EXIT_OK;
    Arena *arena = NULL;
    
    // Metric tracking
    long peak_rss_before_kb = get_peak_rss_kb();
    double t_start = now_seconds();

    // 2. Initialize Core Resources
    arena = arena_create(8 * 1024 * 1024); // 8MB for multi-module
    if (!arena) {
        fprintf(stderr, "Error: Failed to create memory arena\n");
        return EXIT_IO;
    }

    // Shared Interners
    DenseArenaInterner *keywords = intern_table_create(hashmap_create(32), arena, string_copy_func, slice_hash, slice_cmp);
    DenseArenaInterner *identifiers = intern_table_create(hashmap_create(256), arena, string_copy_func, slice_hash, slice_cmp);
    DenseArenaInterner *strings = intern_table_create(hashmap_create(128), arena, string_copy_func, slice_hash, slice_cmp);

    /* Pre-intern keywords */
    extern TokenType get_keyword_type(const char *word); // Use a helper if needed or just copy the list
    // Actually, lexer_create usually does this. I'll manually seed them or let the first lexer do it.
    // For simplicity, I'll just use lexer_create for the first one or just seed here.
    const char *kw_list[] = {"fn", "if", "else", "while", "for", "return", "break", "continue", "const", "pub", "import", "struct", "as", "i32", "i64", "bool", "f32", "f64", "str", "char", "void", "true", "false", NULL};
    const TokenType kw_types[] = {TOK_FN, TOK_IF, TOK_ELSE, TOK_WHILE, TOK_FOR, TOK_RETURN, TOK_BREAK, TOK_CONTINUE, TOK_CONST, TOK_PUB, TOK_IMPORT, TOK_STRUCT, TOK_AS, TOK_I32, TOK_I64, TOK_BOOL, TOK_F32, TOK_F64, TOK_STRING, TOK_CHAR, TOK_VOID, TOK_TRUE, TOK_FALSE};
    for(int i=0; kw_list[i]; i++) {
        Slice s = {(char*)kw_list[i], strlen(kw_list[i])};
        intern(keywords, &s, (void*)(uintptr_t)kw_types[i]);
    }

    AstNode *merged_program = ast_create_node(AST_PROGRAM, arena);
    merged_program->data.program.decls = arena_alloc(arena, sizeof(DynArray));
    dynarray_init_in_arena(merged_program->data.program.decls, arena, sizeof(AstNode*), 32);

    ModuleLoader loader = {
        .arena = arena,
        .opts = &opts,
        .keywords = keywords,
        .identifiers = identifiers,
        .strings = strings,
        .visited_modules = hashmap_create(16),
        .merged_program = merged_program
    };

    // Recursive Load
    exit_code = load_module_recursive(&loader, path);
    if (exit_code != EXIT_OK) goto cleanup;

    double t_load_end = now_seconds();
    double t_parse = t_load_end - t_start;

    // ---------------------------------------------------------
    // Semantic Analysis (Type Checking)
    // ---------------------------------------------------------
    if (opts.verbose) printf("Semantic Analysis...\n");
    size_t mem_before_sema = arena_total_allocated(arena);
    double t3 = now_seconds();

    // Create Type System
    TypeStore *store = typestore_create(arena, identifiers, keywords);
    TypeCheckContext type_ctx = typecheck_context_create(
        arena, 
        merged_program, 
        store, 
        identifiers, 
        keywords, 
        path
    );

    // Run Semantic Passes
    typecheck_program(&type_ctx);
    
    double t4 = now_seconds();
    double t_sema = t4 - t3;
    size_t mem_sema = arena_total_allocated(arena) - mem_before_sema;

    // Check for Semantic Errors
    if (type_ctx.errors->count > 0) {
        for (size_t i = 0; i < type_ctx.errors->count; i++) {
            TypeError *e = (TypeError*)dynarray_get(type_ctx.errors, i);
            print_type_error(e);
        }
        exit_code = EXIT_TYPE;
        goto cleanup;
    }

    // Code Generation
    // ---------------------------------------------------------
    if (opts.verbose) printf("Code Generation...\n");
    double t5 = now_seconds();
    CodegenContext *cg_ctx = codegen_context_create(merged_program, store, "main_module", opts.opt_level);
    if (codegen_program(cg_ctx) == 0) {
        if (opts.print_ir) codegen_dump_module(cg_ctx);

        char obj_path[256];
        snprintf(obj_path, sizeof(obj_path), "%s.o", opts.output_name);
        codegen_emit_object(cg_ctx, obj_path);
        
        char link_cmd[512];
        snprintf(link_cmd, sizeof(link_cmd), "cc %s src/core/runtime.c -o %s 2>/dev/null || cc %s -o %s", 
                 obj_path, opts.output_name, obj_path, opts.output_name);
        
        if (system(link_cmd) == 0) {
            if (opts.verbose) printf("Successfully compiled to '%s' executable.\n", opts.output_name);
            if (opts.run_executable) {
                char run_cmd[258];
                snprintf(run_cmd, sizeof(run_cmd), "./%s", opts.output_name);
                system(run_cmd);
            }
        } else {
            fprintf(stderr, "Error: Linking failed\n");
            exit_code = EXIT_IO;
        }
    } else {
        fprintf(stderr, "Error: Code generation failed\n");
        exit_code = EXIT_TYPE;
    }
    codegen_context_destroy(cg_ctx);
    double t_codegen = now_seconds() - t5;

    // Reporting
    if (opts.print_ast) {
        printf("--- AST ---\n");
        print_ast(merged_program, 0, keywords, identifiers, strings);
    }
    if (opts.print_time) {
        printf("\n--- Metrics ---\n");
        printf("Time Parse/Load: %.3fms\n", t_parse * 1000);
        printf("Time Sema:       %.3fms\n", t_sema * 1000);
        printf("Time Codegen:    %.3fms\n", t_codegen * 1000);
        printf("Peak RSS:        %ld KB\n", get_peak_rss_kb());
    }

cleanup:
    if (arena) arena_destroy(arena);
    return exit_code;
}