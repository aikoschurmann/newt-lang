// main.c - driver: orchestrates lexing, parsing, type internment, reporting

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "core/file.h"
#include "lexing/lexer.h"
#include "parsing/parser.h"
#include "parsing/parse_statements.h"
#include "parsing/parse_declarations.h"
#include "parsing/ast.h"
#include "datastructures/arena.h"
#include "sema/type.h"
#include "sema/typecheck.h"
#include "sema/type_print.h"
#include "datastructures/scope.h"
#include "cli/cli.h"
#include "cli/metrics.h"
#include "core/utils.h"
#include "codegen/codegen.h"
#include "core/module_loader.h"
#include "core/exit_codes.h"

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
    (void)peak_rss_before_kb;
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
    lexer_populate_default_keywords(keywords);

    ModuleLoader *loader = module_loader_create(arena, &opts, keywords, identifiers, strings);

    // Recursive Load
    exit_code = load_module_recursive(loader, path, NULL, NULL, 0);
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
        store, 
        identifiers, 
        keywords, 
        path,
        loader
    );

    // Run Semantic Passes
    typecheck_program(&type_ctx);
    
    double t4 = now_seconds();
    double t_sema = t4 - t3;
    size_t mem_sema = arena_total_allocated(arena) - mem_before_sema;
    (void)mem_sema;

    // Reporting
    if (opts.print_ast) {
        printf("--- AST ---\n");
        for (size_t i = 0; i < loader->units_ordered->count; i++) {
            CompilationUnit *u = *(CompilationUnit**)dynarray_get(loader->units_ordered, i);
            printf("Module: %s\n", u->absolute_path);
            print_ast(u->ast_root, 0, keywords, identifiers, strings);
        }
    }

    if (opts.print_types) {
        // Pass the unit's GLOBAL SCOPE instead of the AST
        CompilationUnit *main_unit = *(CompilationUnit**)dynarray_get(loader->units_ordered, loader->units_ordered->count - 1);
        type_print_store_dump(store, main_unit->global_scope); 
    }

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
    CodegenContext *cg_ctx = codegen_context_create(store, "main_module", opts.opt_level, loader);
    if (codegen_program(cg_ctx) == 0) {
        if (opts.print_ir) codegen_dump_module(cg_ctx);

#ifdef _WIN32
        const char *obj_ext = ".obj";
#else
        const char *obj_ext = ".o";
#endif

        size_t obj_path_len = strlen(opts.output_name) + strlen(obj_ext) + 1;
        char *obj_path = arena_alloc(arena, obj_path_len);
        snprintf(obj_path, obj_path_len, "%s%s", opts.output_name, obj_ext);
        codegen_emit_object(cg_ctx, obj_path);
        
        if (opts.verbose) printf("Linking...\n");

        char *runtime_path = get_runtime_path();
        
        // Use "cc" as a generic name, or "clang" if on Windows
#ifdef _WIN32
        const char *linker = "clang";
#else
        const char *linker = "cc";
#endif

        char *link_args[] = { (char*)linker, obj_path, runtime_path, "-o", (char*)opts.output_name, NULL };
        if (run_command(linker, link_args) == 0) {
            if (opts.verbose) printf("Successfully compiled to '%s' executable.\n", opts.output_name);
            free(runtime_path);

            if (opts.run_executable) {
                char *run_cmd;
#ifdef _WIN32
                size_t run_cmd_len = strlen(opts.output_name) + 1;
                run_cmd = arena_alloc(arena, run_cmd_len);
                snprintf(run_cmd, run_cmd_len, "%s", opts.output_name);
#else
                size_t run_cmd_len = strlen(opts.output_name) + 3;
                run_cmd = arena_alloc(arena, run_cmd_len);
                snprintf(run_cmd, run_cmd_len, "./%s", opts.output_name);
#endif
                char *run_args[] = { run_cmd, NULL };
                run_command(run_cmd, run_args);
            }
        } else {
            fprintf(stderr, "Error: Linking failed\n");
            free(runtime_path);
            exit_code = EXIT_IO;
        }
    } else {
        fprintf(stderr, "Error: Code generation failed\n");
        exit_code = EXIT_TYPE;
    }
    codegen_context_destroy(cg_ctx);
    double t_codegen = now_seconds() - t5;

    if (opts.print_time) {
        printf("\n--- Metrics ---\n");
        printf("Time Parse/Load: %.3fms\n", t_parse * 1000);
        printf("Time Sema:       %.3fms\n", t_sema * 1000);
        printf("Time Codegen:    %.3fms\n", t_codegen * 1000);
        printf("Peak RSS:        %zu KB\n", get_peak_rss_kb());
    }

cleanup:
    if (arena) arena_destroy(arena);
    return exit_code;
}
