// main.c - driver: orchestrates lexing, parsing, type internment, reporting

/**
 * DOC: Compiler Driver for Newt-lang
 *
 * Orchestrates the compilation pipeline: command line options parsing, arena
 * allocator initialization, lexical analysis, parsing, semantic verification,
 * AST/Type store printing, and LLVM/Linking backend code generation.
 */

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

/**
 * DEFINE: COMPILER_INIT_ARENA_SIZE
 *
 * The initial size (in bytes) requested for the compiler's linear memory arena.
 * Set to 8 Megabytes. The arena will dynamically grow if exceeded, but this
 * default size prevents frequent heap reallocations during typical compilation runs.
 */
#define COMPILER_INIT_ARENA_SIZE (8 * 1024 * 1024)

/**
 * struct CompilerState - Complete tracking state for a compilation transaction.
 * @opts: User-supplied command-line driver configuration options.
 * @arena: Pointer to the central arena allocator managing AST and symbol lifetimes.
 * @keywords: Unique string pool interner containing keyword identifiers.
 * @identifiers: Unique string pool interner for user-defined symbols.
 * @strings: Unique string pool interner for string literals.
 * @loader: Coordinates file loading, parsing, and resolving the module dependency graph.
 * @store: Serves as the type registry and interner cache for type definitions.
 * @t_start: Timestamp indicating when the compilation process was initiated.
 *
 * Encapsulates the configuration, allocator tables, and symbol pools required to
 * perform lexing, parsing, semantic check, and code generation passes.
 */
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

/**
 * compiler_init() - Prepares compiler subsystem states and string pools.
 * @state: Destination memory chunk to initialize. Must not be NULL.
 * @opts: Parsed options to assign to this compiler session. Must not be NULL.
 *
 * Initializes the central memory arena, constructs keyword, identifier, and string
 * pools, registers default lexer keywords, and creates the module loader subsystem.
 * 
 * Return: EXIT_OK on success, or EXIT_IO on allocation failures.
 */
static int compiler_init(CompilerState *state, Options *opts) {
    if (!state || !opts) {
        fprintf(stderr, "Error: Invalid null state passed to compiler_init\n");
        return EXIT_IO;
    }

    state->opts = opts;
    state->t_start = now_seconds();
    
    /* Allocate primary linear allocator arena */
    state->arena = arena_create(COMPILER_INIT_ARENA_SIZE);
    if (!state->arena) {
        fprintf(stderr, "Error: Failed to allocate central compiler arena (size = %d)\n", COMPILER_INIT_ARENA_SIZE);
        return EXIT_IO;
    }

    /* Allocate underlying hash map buffers for interning tables */
    HashMap *kw_map = hashmap_create(state->arena, 32);
    HashMap *id_map = hashmap_create(state->arena, 256);
    HashMap *str_map = hashmap_create(state->arena, 128);

    if (!kw_map || !id_map || !str_map) {
        fprintf(stderr, "Error: Failed to construct hashmap buffers for string interning tables\n");
        return EXIT_IO;
    }

    /* Initialize interner tables inside the arena */
    state->keywords = intern_table_create(kw_map, state->arena, string_copy_func, slice_hash, slice_cmp);
    state->identifiers = intern_table_create(id_map, state->arena, string_copy_func, slice_hash, slice_cmp);
    state->strings = intern_table_create(str_map, state->arena, string_copy_func, slice_hash, slice_cmp);

    if (!state->keywords || !state->identifiers || !state->strings) {
        fprintf(stderr, "Error: Failed to initialize string interner tables\n");
        return EXIT_IO;
    }

    /* Pre-populate keyword table with built-in language words */
    lexer_populate_default_keywords(state->keywords);

    /* Construct module loader subsystem */
    state->loader = module_loader_create(state->arena, opts, state->keywords, state->identifiers, state->strings);
    if (!state->loader) {
        fprintf(stderr, "Error: Failed to initialize module loader subsystem\n");
        return EXIT_IO;
    }

    state->store = NULL;
    return EXIT_OK;
}

/**
 * compiler_load_modules() - Recursively loads and parses all input files.
 * @state: The current active compiler state transaction. Must not be NULL.
 * @path: Absolute or relative system path to the root module/file.
 *
 * Dispatches parsing of the entry path, resolving internal imports and compiling
 * dependency targets recursively.
 *
 * Return: EXIT_OK on success, or an exit code representation of parser failure.
 */
static int compiler_load_modules(CompilerState *state, const char *path) {
    if (!state || !state->loader) {
        fprintf(stderr, "Error: Invalid state or loader in compiler_load_modules\n");
        return EXIT_IO;
    }
    return load_module_recursive(state->loader, path, NULL, NULL, 0);
}

/**
 * compiler_run_sema() - Orchestrates the semantic type verification pass.
 * @state: Active compiler state transaction. Must not be NULL.
 *
 * Creates the type system store, establishes a global TypeCheckContext pointing
 * to the primary entry compilation unit, and recursively checks types. Generates
 * descriptive diagnostic errors to stderr if any violations are found.
 *
 * Return: EXIT_OK on success, EXIT_TYPE if type check fails, or EXIT_IO on OOM.
 */
static int compiler_run_sema(CompilerState *state) {
    if (!state || !state->loader) {
        fprintf(stderr, "Error: Invalid state or loader in compiler_run_sema\n");
        return EXIT_IO;
    }

    /* Validate compilation units collection boundary */
    if (!state->loader->units_ordered || state->loader->units_ordered->count == 0) {
        fprintf(stderr, "Error: No compilation units were loaded. Verification aborted.\n");
        return EXIT_IO;
    }

    if (state->opts->verbose) {
        printf("Semantic Analysis...\n");
    }

    /* Allocate and initialize the central TypeStore */
    state->store = typestore_create(state->arena, state->identifiers, state->keywords);
    if (!state->store) {
        fprintf(stderr, "Error: Failed to initialize TypeStore (Out of Memory).\n");
        return EXIT_IO;
    }
    
    /* Locate the root compilation unit (always at position 0 in ordered units) */
    CompilationUnit *first_unit = *(CompilationUnit**)dynarray_get(state->loader->units_ordered, 0);
    if (!first_unit) {
        fprintf(stderr, "Error: Primary compilation unit is NULL.\n");
        return EXIT_IO;
    }

    /* Construct verification context */
    TypeCheckContext type_ctx = typecheck_context_create(
        state->arena, state->store, state->identifiers, state->keywords, first_unit->absolute_path, state->loader
    );

    /* Perform the semantic validation pass */
    typecheck_program(&type_ctx);

    /* Check error accumulation and print diagnostics to standard error */
    if (type_ctx.errors && type_ctx.errors->count > 0) {
        for (size_t i = 0; i < type_ctx.errors->count; i++) {
            TypeError *err = (TypeError*)dynarray_get(type_ctx.errors, i);
            if (err) {
                print_type_error(err);
            }
        }
        return EXIT_TYPE;
    }

    return EXIT_OK;
}

/**
 * compiler_dump_info() - Outputs AST and type structures if print flags are set.
 * @state: Active compiler state transaction. Must not be NULL.
 *
 * Safe-guards AST traversal and TypeStore dumps by confirming that compilation
 * units are non-empty before looking up roots and scopes.
 */
static void compiler_dump_info(CompilerState *state) {
    if (!state || !state->loader || !state->loader->units_ordered) {
        return;
    }

    size_t unit_count = state->loader->units_ordered->count;
    if (unit_count == 0) {
        return;
    }

    /* Dump Abstract Syntax Tree (AST) for each unit */
    if (state->opts->print_ast) {
        printf("--- AST ---\n");
        for (size_t i = 0; i < unit_count; i++) {
            CompilationUnit *u = *(CompilationUnit**)dynarray_get(state->loader->units_ordered, i);
            if (u) {
                printf("Module: %s\n", u->absolute_path ? u->absolute_path : "<unknown>");
                print_ast(u->ast_root, 0, state->keywords, state->identifiers, state->strings);
            }
        }
    }

    /* Dump the global type store registers mapping to the main module */
    if (state->opts->print_types && state->store) {
        CompilationUnit *main_unit = *(CompilationUnit**)dynarray_get(state->loader->units_ordered, unit_count - 1);
        if (main_unit) {
            type_print_store_dump(state->store, main_unit->global_scope);
        }
    }
}

/**
 * compiler_run_backend() - Generates LLVM IR, object outputs, links, and runs execution.
 * @state: Active compiler state transaction. Must not be NULL.
 *
 * Coordinates creation of LLVM context, optimization, generation of machine object files,
 * platform-specific linking (via 'cc' / 'clang'), and optional test run behavior. Ensures
 * resource cleanup on error paths, specifically for raw heap buffers like linker outputs.
 *
 * Return: EXIT_OK on success, EXIT_TYPE on codegen errors, or EXIT_IO on IO/Link failure.
 */
static int compiler_run_backend(CompilerState *state) {
    if (!state || !state->opts) {
        fprintf(stderr, "Error: Invalid compiler state passed to backend\n");
        return EXIT_IO;
    }

    if (state->opts->verbose) {
        printf("Code Generation...\n");
    }

    /* Construct LLVM CodeGen runtime context */
    CodegenContext *cg_ctx = codegen_context_create(state->store, "main_module", state->opts->opt_level, state->loader);
    if (!cg_ctx) {
        fprintf(stderr, "Error: Failed to create LLVM Codegen Context.\n");
        return EXIT_TYPE;
    }
    
    /* Translate AST node semantics into LLVM intermediate representation */
    if (codegen_program(cg_ctx) != 0) {
        fprintf(stderr, "Error: LLVM Code generation pass failed\n");
        codegen_context_destroy(cg_ctx);
        return EXIT_TYPE;
    }

    /* Print LLVM IR if explicitly requested */
    if (state->opts->print_ir) {
        codegen_dump_module(cg_ctx);
    }

    /* Determine target platform object file format suffix */
    const char *obj_ext = 
#ifdef _WIN32
        ".obj";
#else
        ".o";
#endif

    if (!state->opts->output_name) {
        fprintf(stderr, "Error: Missing output file path configuration.\n");
        codegen_context_destroy(cg_ctx);
        return EXIT_IO;
    }

    /* Allocate buffer for object output file path */
    size_t obj_path_len = strlen(state->opts->output_name) + strlen(obj_ext) + 1;
    char *obj_path = arena_alloc(state->arena, obj_path_len);
    if (!obj_path) {
        fprintf(stderr, "Error: Out of memory during object path allocation.\n");
        codegen_context_destroy(cg_ctx);
        return EXIT_IO;
    }

    snprintf(obj_path, obj_path_len, "%s%s", state->opts->output_name, obj_ext);
    
    /* Write machine instruction object file to disk */
    codegen_emit_object(cg_ctx, obj_path);
    
    if (state->opts->verbose) {
        printf("Linking...\n");
    }

    /* Locate the compiler's support runtime library */
    char *runtime_path = get_runtime_path();
    const char *linker = 
#ifdef _WIN32
        "clang";
#else
        "cc";
#endif

    /* Formulate linking arguments array (clang/cc <obj> <runtime> -o <output>) */
    char *link_args[] = { (char*)linker, obj_path, runtime_path, "-o", (char*)state->opts->output_name, NULL };
    int link_res = run_command(linker, link_args);
    
    /* runtime_path is allocated outside of arena, must free manually */
    free(runtime_path);

    if (link_res != 0) {
        fprintf(stderr, "Error: Linker execution failed (code: %d)\n", link_res);
        codegen_context_destroy(cg_ctx);
        return EXIT_IO;
    }

    if (state->opts->verbose) {
        printf("Successfully compiled to '%s' executable.\n", state->opts->output_name);
    }

    /* Optionally execute compiled target directly */
    if (state->opts->run_executable) {
        char *run_cmd = NULL;
#ifdef _WIN32
        run_cmd = (char*)state->opts->output_name;
#else
        size_t len = strlen(state->opts->output_name) + 3;
        run_cmd = arena_alloc(state->arena, len);
        if (!run_cmd) {
            fprintf(stderr, "Error: Out of memory preparing run path.\n");
            codegen_context_destroy(cg_ctx);
            return EXIT_IO;
        }
        snprintf(run_cmd, len, "./%s", state->opts->output_name);
#endif
        char *run_args[] = { run_cmd, NULL };
        run_command(run_cmd, run_args);
    }

    codegen_context_destroy(cg_ctx);
    return EXIT_OK;
}

/**
 * main() - Compiler CLI Application Entry Point.
 * @argc: Command line argument count.
 * @argv: Command line argument string values.
 *
 * Performs static initialization of the codegen target architecture registry,
 * parses CLI options, sets up compilation contexts, walks compilation stages,
 * and prints execution diagnostics. Releases all allocated resources through the
 * central arena interface before returning.
 *
 * Return: Exit code (EXIT_OK, EXIT_USAGE, EXIT_IO, or EXIT_TYPE).
 */
int main(int argc, char **argv) {
    /* Initialize targets globally before option checking */
    codegen_initialize();
    
    Options opts;
    const char *path = NULL;
    
    /* Parse user driver switches */
    if (!parse_options(argc, argv, &opts, &path)) {
        return EXIT_USAGE;
    }

    CompilerState state;
    
    /* Set up string tables, loaders, and allocations */
    int exit_code = compiler_init(&state, &opts);
    if (exit_code != EXIT_OK) {
        return exit_code;
    }

    /* Recursive module loading & syntax parsing */
    exit_code = compiler_load_modules(&state, path);
    if (exit_code != EXIT_OK) {
        goto cleanup;
    }

    double t_load = now_seconds() - state.t_start;

    /* Semantic type verification */
    double t_sema_start = now_seconds();
    exit_code = compiler_run_sema(&state);
    double t_sema = now_seconds() - t_sema_start;

    /* AST & Type system structure dumps */
    compiler_dump_info(&state);

    if (exit_code != EXIT_OK) {
        goto cleanup;
    }

    /* Codegen and link steps */
    double t_cg_start = now_seconds();
    exit_code = compiler_run_backend(&state);
    double t_cg = now_seconds() - t_cg_start;

    /* Report performance metrics if explicitly configured */
    if (opts.print_time) {
        printf("\n--- Metrics ---\n");
        printf("Time Parse/Load: %.3fms\n", t_load * 1000);
        printf("Time Sema:       %.3fms\n", t_sema * 1000);
        printf("Time Codegen:    %.3fms\n", t_cg * 1000);
        printf("Peak RSS:        %zu KB\n", get_peak_rss_kb());
    }

cleanup:
    /* Safely release all system memory allocated during session */
    if (state.arena) {
        arena_destroy(state.arena);
    }
    return exit_code;
}
