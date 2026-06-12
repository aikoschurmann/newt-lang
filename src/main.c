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

int main(int argc, char **argv) {
    codegen_initialize();
    // 1. Parse CLI Options
    Options opts;
    const char *path = NULL;
    if (!parse_options(argc, argv, &opts, &path)) {
        return EXIT_USAGE;
    }

    int exit_code = EXIT_OK;
    char *src = NULL;
    Arena *arena = NULL;
    Lexer *lexer = NULL;
    Parser *parser = NULL;
    
    // Metric tracking
    long peak_rss_before_kb = get_peak_rss_kb();
    double t_start = now_seconds();
    double t_lex = 0, t_parse = 0, t_sema = 0;
    size_t mem_lex = 0, mem_parse = 0, mem_sema = 0;

    // 2. Initialize Core Resources
    // Pre-allocate 4MB to avoid OS page fault jitter during initialization
    arena = arena_create(4 * 1024 * 1024);
    if (!arena) {
        fprintf(stderr, "Error: Failed to create memory arena\n");
        return EXIT_IO;
    }

    // Copy filename to arena for safety/lifetime management
    char *filename_interned = arena_alloc(arena, strlen(path) + 1);
    strcpy(filename_interned, path);

    // 3. Read Source File
    src = read_file(path);
    if (!src) {
        fprintf(stderr, "Error: Failed to read file: %s\n", path);
        exit_code = EXIT_IO;
        goto cleanup;
    }
    size_t src_len = strlen(src);

    // Lexical Analysis
    // ---------------------------------------------------------
    if (opts.verbose) printf("Lexing...\n");
    lexer = lexer_create(src, src_len, arena);
    if (!lexer) {
        fprintf(stderr, "Error: Failed to initialize lexer\n");
        exit_code = EXIT_LEX;
        goto cleanup;
    }

    double t0 = now_seconds();
    if (!lexer_lex_all(lexer)) {
        fprintf(stderr, "Error: Lexing failed\n");
        exit_code = EXIT_LEX;
        goto cleanup;
    }
    t_lex = now_seconds() - t0;
    mem_lex = arena_total_allocated(arena);

    // Optional: Print Tokens
    if (opts.print_tokens && lexer->tokens) {
        printf("--- Tokens ---\n");
        for (size_t i = 0; i < lexer->tokens->count; i++) {
            Token *tok = (Token*)dynarray_get(lexer->tokens, i);
            print_token(tok);
        }
        printf("\n");
    }

    // ---------------------------------------------------------
    // Parsing
    // ---------------------------------------------------------
    if (opts.verbose) printf("Parsing...\n");
    parser = parser_create(lexer->tokens, filename_interned, arena);
    if (!parser) {
        fprintf(stderr, "Error: Failed to initialize parser\n");
        exit_code = EXIT_PARSE;
        goto cleanup;
    }

    ParseError parse_err = {0};
    double t1 = now_seconds();
    AstNode *program = parse_program(parser, &parse_err);
    double t2 = now_seconds();
    t_parse = t2 - t1;
    mem_parse = arena_total_allocated(arena) - mem_lex;

    if (parse_err.message) {
        print_parse_error(&parse_err);
        exit_code = EXIT_PARSE;
        goto cleanup;
    }

    if (!program) {
        // Empty program or non-fatal parse failure
        goto cleanup;
    }

    // ---------------------------------------------------------
    // Semantic Analysis (Type Checking)
    // ---------------------------------------------------------
    if (opts.verbose) printf("Semantic Analysis...\n");
    size_t mem_before_sema = arena_total_allocated(arena);
    double t3 = now_seconds();

    // Create Type System
    TypeStore *store = typestore_create(arena, lexer->identifiers, lexer->keywords);
    TypeCheckContext type_ctx = typecheck_context_create(
        arena, 
        program, 
        store, 
        lexer->identifiers, 
        lexer->keywords, 
        filename_interned
    );

    // Run Semantic Passes
    typecheck_program(&type_ctx);
    
    double t4 = now_seconds();
    t_sema = t4 - t3;
    mem_sema = arena_total_allocated(arena) - mem_before_sema;

    // Check for Semantic Errors
    if (type_ctx.errors->count > 0) {
        for (size_t i = 0; i < type_ctx.errors->count; i++) {
            TypeError *e = (TypeError*)dynarray_get(type_ctx.errors, i);
            print_type_error(e); // Filename is already in context
        }
        exit_code = EXIT_TYPE;
        goto cleanup;
    }

    // Code Generation
    // ---------------------------------------------------------
    if (opts.verbose) printf("Code Generation...\n");
    double t5 = now_seconds();
    double t_run = 0;
    int program_exit_code = 0;
    CodegenContext *cg_ctx = codegen_context_create(program, store, "main_module", opts.opt_level);
    if (codegen_program(cg_ctx) == 0) {
        // Successfully generated IR
        if (opts.print_ir) {
             codegen_dump_module(cg_ctx);
        }

        char obj_path[256];
        snprintf(obj_path, sizeof(obj_path), "%s.o", opts.output_name);
        codegen_emit_object(cg_ctx, obj_path);
        
        if (opts.verbose) printf("Linking...\n");
        char link_cmd[512];
        snprintf(link_cmd, sizeof(link_cmd), "cc %s src/core/runtime.c -o %s 2>/dev/null || cc %s -o %s", 
                 obj_path, opts.output_name, obj_path, opts.output_name);
        
        int ret = system(link_cmd);
        if (ret == 0) {
            if (opts.verbose) printf("Successfully compiled to '%s' executable.\n", opts.output_name);
            
            if (opts.run_executable) {
                if (opts.verbose) printf("\n--- Running Output ---\n");
                double t_run_start = now_seconds();
                char run_cmd[258];
                snprintf(run_cmd, sizeof(run_cmd), "./%s", opts.output_name);
                program_exit_code = system(run_cmd);
                t_run = now_seconds() - t_run_start;
                
                // system() returns termination status (exit code is high 8 bits)
                if (WIFEXITED(program_exit_code)) {
                    program_exit_code = WEXITSTATUS(program_exit_code);
                    if (opts.verbose) printf("--- Process Exited with Code: %d ---\n", program_exit_code);
                } else if (WIFSIGNALED(program_exit_code)) {
                    if (opts.verbose) printf("--- Process Terminated by Signal: %d ---\n", WTERMSIG(program_exit_code));
                }
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

    // ---------------------------------------------------------
    // Reporting & Output
    // ---------------------------------------------------------
    
    // 1. Print AST (Now populated with Types)
    if (opts.print_ast) {
        printf("--- AST ---\n");
        print_ast(program, 0, lexer->keywords, lexer->identifiers, lexer->strings);
        printf("\n");
    }

    // 2. Print Type Store Dump
    if (opts.print_types) {
        printf("--- Type Store ---\n");
        type_print_store_dump(store, program);
        printf("\n");
    }

    // 3. Print Metrics
    if (opts.print_time) {
        long peak_rss_after_kb = get_peak_rss_kb();
        size_t token_count = lexer->tokens ? lexer->tokens->count : 0;
        
        CompilationStats stats = {
            .time_tokenize_ms = t_lex * 1000,
            .time_parse_ms = t_parse * 1000,
            .time_sema_ms = t_sema * 1000,
            .time_codegen_ms = t_codegen * 1000,
            .mem_lex_bytes = mem_lex,
            .mem_parse_bytes = mem_parse,
            .mem_sema_bytes = mem_sema,
            .rss_delta_bytes = (peak_rss_after_kb - peak_rss_before_kb) * 1024,
            .token_count = token_count,
            .file_size_bytes = src_len,
            .filename = filename_interned
        };
        print_compilation_report(&stats, program);
    }

cleanup:
    // Single cleanup point for resources
    if (parser) parser_free(parser);
    if (lexer) lexer_destroy(lexer);
    if (arena) arena_destroy(arena);
    // if (src) free(src); // Intentionally commented out to cause a leak

    return exit_code;
}