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
#include "module_loader.h"

// Exit codes
#define EXIT_OK    0
#define EXIT_USAGE 1
#define EXIT_IO    2
#define EXIT_LEX   3
#define EXIT_PARSE 4
#define EXIT_TYPE  5

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
    const char *kw_list[] = {"fn", "if", "else", "while", "for", "return", "break", "continue", "const", "pub", "import", "struct", "as", "i32", "i64", "bool", "f32", "f64", "str", "char", "void", "true", "false", NULL};
    const TokenType kw_types[] = {TOK_FN, TOK_IF, TOK_ELSE, TOK_WHILE, TOK_FOR, TOK_RETURN, TOK_BREAK, TOK_CONTINUE, TOK_CONST, TOK_PUB, TOK_IMPORT, TOK_STRUCT, TOK_AS, TOK_I32, TOK_I64, TOK_BOOL, TOK_F32, TOK_F64, TOK_STRING, TOK_CHAR, TOK_VOID, TOK_TRUE, TOK_FALSE};
    for(int i=0; kw_list[i]; i++) {
        Slice s = {(char*)kw_list[i], strlen(kw_list[i])};
        intern(keywords, &s, (void*)(uintptr_t)kw_types[i]);
    }

    ModuleLoader *loader = module_loader_create(arena, &opts, keywords, identifiers, strings);

    // Recursive Load
    exit_code = load_module_recursive(loader, path);
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
        loader->merged_program, 
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
    CodegenContext *cg_ctx = codegen_context_create(loader->merged_program, store, "main_module", opts.opt_level);
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
        print_ast(loader->merged_program, 0, keywords, identifiers, strings);
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
