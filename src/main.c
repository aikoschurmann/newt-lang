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

typedef struct {
    Options *opts;
    Arena *arena;
    DenseArenaInterner *keywords;
    DenseArenaInterner *identifiers;
    DenseArenaInterner *strings;
    ModuleLoader *loader;
    TypeStore *store;
    double t_start;
} CompilerState;

static int compiler_init(CompilerState *state, Options *opts) {
    state->opts = opts;
    state->t_start = now_seconds();
    state->arena = arena_create(8 * 1024 * 1024);
    if (!state->arena) return EXIT_IO;

    state->keywords = intern_table_create(hashmap_create(state->arena, 32), state->arena, string_copy_func, slice_hash, slice_cmp);
    state->identifiers = intern_table_create(hashmap_create(state->arena, 256), state->arena, string_copy_func, slice_hash, slice_cmp);
    state->strings = intern_table_create(hashmap_create(state->arena, 128), state->arena, string_copy_func, slice_hash, slice_cmp);

    lexer_populate_default_keywords(state->keywords);
    state->loader = module_loader_create(state->arena, opts, state->keywords, state->identifiers, state->strings);
    state->store = NULL;
    return EXIT_OK;
}

static int compiler_load_modules(CompilerState *state, const char *path) {
    return load_module_recursive(state->loader, path, NULL, NULL, 0);
}

static int compiler_run_sema(CompilerState *state) {
    if (state->opts->verbose) printf("Semantic Analysis...\n");
    state->store = typestore_create(state->arena, state->identifiers, state->keywords);
    
    CompilationUnit *first_unit = *(CompilationUnit**)dynarray_get(state->loader->units_ordered, 0);
    TypeCheckContext type_ctx = typecheck_context_create(
        state->arena, state->store, state->identifiers, state->keywords, first_unit->absolute_path, state->loader
    );

    typecheck_program(&type_ctx);

    if (type_ctx.errors->count > 0) {
        for (size_t i = 0; i < type_ctx.errors->count; i++) {
            print_type_error((TypeError*)dynarray_get(type_ctx.errors, i));
        }
        return EXIT_TYPE;
    }
    return EXIT_OK;
}

static void compiler_dump_info(CompilerState *state) {
    if (state->opts->print_ast) {
        printf("--- AST ---\n");
        for (size_t i = 0; i < state->loader->units_ordered->count; i++) {
            CompilationUnit *u = *(CompilationUnit**)dynarray_get(state->loader->units_ordered, i);
            printf("Module: %s\n", u->absolute_path);
            print_ast(u->ast_root, 0, state->keywords, state->identifiers, state->strings);
        }
    }
    if (state->opts->print_types) {
        CompilationUnit *main_unit = *(CompilationUnit**)dynarray_get(state->loader->units_ordered, state->loader->units_ordered->count - 1);
        type_print_store_dump(state->store, main_unit->global_scope);
    }
}

static int compiler_run_backend(CompilerState *state) {
    if (state->opts->verbose) printf("Code Generation...\n");
    CodegenContext *cg_ctx = codegen_context_create(state->store, "main_module", state->opts->opt_level, state->loader);
    
    if (codegen_program(cg_ctx) != 0) {
        fprintf(stderr, "Error: Code generation failed\n");
        codegen_context_destroy(cg_ctx);
        return EXIT_TYPE;
    }

    if (state->opts->print_ir) codegen_dump_module(cg_ctx);

    char *obj_ext = 
#ifdef _WIN32
        ".obj";
#else
        ".o";
#endif

    size_t obj_path_len = strlen(state->opts->output_name) + strlen(obj_ext) + 1;
    char *obj_path = arena_alloc(state->arena, obj_path_len);
    snprintf(obj_path, obj_path_len, "%s%s", state->opts->output_name, obj_ext);
    codegen_emit_object(cg_ctx, obj_path);
    
    if (state->opts->verbose) printf("Linking...\n");
    char *runtime_path = get_runtime_path();
    char *linker = 
#ifdef _WIN32
        "clang";
#else
        "cc";
#endif

    char *link_args[] = { linker, obj_path, runtime_path, "-o", (char*)state->opts->output_name, NULL };
    int link_res = run_command(linker, link_args);
    free(runtime_path);

    if (link_res != 0) {
        fprintf(stderr, "Error: Linking failed\n");
        codegen_context_destroy(cg_ctx);
        return EXIT_IO;
    }

    if (state->opts->verbose) printf("Successfully compiled to '%s' executable.\n", state->opts->output_name);

    if (state->opts->run_executable) {
        char *run_cmd;
#ifdef _WIN32
        run_cmd = (char*)state->opts->output_name;
#else
        size_t len = strlen(state->opts->output_name) + 3;
        run_cmd = arena_alloc(state->arena, len);
        snprintf(run_cmd, len, "./%s", state->opts->output_name);
#endif
        char *run_args[] = { run_cmd, NULL };
        run_command(run_cmd, run_args);
    }

    codegen_context_destroy(cg_ctx);
    return EXIT_OK;
}

int main(int argc, char **argv) {
    codegen_initialize();
    
    Options opts;
    const char *path = NULL;
    if (!parse_options(argc, argv, &opts, &path)) return EXIT_USAGE;

    CompilerState state;
    int exit_code = compiler_init(&state, &opts);
    if (exit_code != EXIT_OK) return exit_code;

    exit_code = compiler_load_modules(&state, path);
    if (exit_code != EXIT_OK) goto cleanup;

    double t_load = now_seconds() - state.t_start;

    double t_sema_start = now_seconds();
    exit_code = compiler_run_sema(&state);
    double t_sema = now_seconds() - t_sema_start;
    if (exit_code != EXIT_OK) goto cleanup;

    compiler_dump_info(&state);

    double t_cg_start = now_seconds();
    exit_code = compiler_run_backend(&state);
    double t_cg = now_seconds() - t_cg_start;

    if (opts.print_time) {
        printf("\n--- Metrics ---\n");
        printf("Time Parse/Load: %.3fms\n", t_load * 1000);
        printf("Time Sema:       %.3fms\n", t_sema * 1000);
        printf("Time Codegen:    %.3fms\n", t_cg * 1000);
        printf("Peak RSS:        %zu KB\n", get_peak_rss_kb());
    }

cleanup:
    arena_destroy(state.arena);
    return exit_code;
}
