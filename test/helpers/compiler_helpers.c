#include "compiler_helpers.h"
#include "../harness/test_harness.h"
#include "core/module_loader.h"
#include <string.h>
#include <stdio.h>

TestCompileResult test_compile_source(const char *src) {
    TestCompileResult res = {0};
    res.arena = arena_create(4 * 1024 * 1024); // 4MB
    
    // 1. Interners
    DenseArenaInterner *keywords = intern_table_create(hashmap_create(res.arena, 32), res.arena, string_copy_func, slice_hash, slice_cmp);
    DenseArenaInterner *identifiers = intern_table_create(hashmap_create(res.arena, 128), res.arena, string_copy_func, slice_hash, slice_cmp);
    DenseArenaInterner *strings = intern_table_create(hashmap_create(res.arena, 64), res.arena, string_copy_func, slice_hash, slice_cmp);
    
    lexer_populate_default_keywords(keywords);

    // 2. Lex
    res.lexer = lexer_create_ex(src, strlen(src), res.arena, keywords, identifiers, strings);
    if (!lexer_lex_all(res.lexer)) {
        res.failed = true;
        return res;
    }

    // 3. Parse
    res.parser = parser_create(res.lexer->tokens, "<test>", res.arena);
    ParseError p_err = {0};
    res.program = parse_program(res.parser, &p_err);
    if (p_err.message) {
        res.failed = true;
        return res;
    }

    // 4. Module Loader
    Options opts = { .stdlib_path = "lib", .verbose = false };
    ModuleLoader *loader = module_loader_create(res.arena, &opts, keywords, identifiers, strings);
    
    CompilationUnit *unit = arena_alloc(res.arena, sizeof(CompilationUnit));
    unit->absolute_path = (char*)"<test>";
    unit->logical_path = NULL;
    unit->ast_root = res.program;
    unit->global_scope = NULL;
    unit->signatures_resolved = false;
    unit->imports_resolved = false;
    unit->generic_templates = hashmap_create(res.arena, 16);
    unit->mono_instances = arena_alloc(res.arena, sizeof(DynArray));
    if (unit->mono_instances) {
        dynarray_init_in_arena(unit->mono_instances, res.arena, sizeof(AstNode*), 8);
    }
    
    hashmap_put(loader->units, unit->absolute_path, unit, str_hash, str_cmp);
    dynarray_push_value(loader->units_ordered, &unit);

    // Manually trigger import resolution for the virtual <test> unit
    if (unit->ast_root && unit->ast_root->node_type == AST_PROGRAM) {
        AstProgram *prog = &unit->ast_root->data.program;
        if (prog->decls) {
            for (size_t i = 0; i < prog->decls->count; i++) {
                AstNode *decl = *(AstNode**)dynarray_get(prog->decls, i);
                if (decl->node_type == AST_IMPORT_DECLARATION) {
                    AstImportDeclaration *imp = &decl->data.import_declaration;
                    
                    char cl_buf[512] = {0};
                    char cp_buf[512] = {0};
                    
                    for (size_t j = 0; j < imp->module_path->count; j++) {
                        InternResult *part = *(InternResult**)dynarray_get(imp->module_path, j);
                        Slice *s = (Slice*)part->key;
                        if (j > 0) {
                            strcat(cl_buf, ".");
                            strcat(cp_buf, "/");
                        }
                        strncat(cl_buf, s->ptr, s->len);
                        strncat(cp_buf, s->ptr, s->len);
                    }

                    char target_file[1024];
                    snprintf(target_file, sizeof(target_file), "%s/%s.nt", opts.stdlib_path, cp_buf);
                    
                    FILE *f = fopen(target_file, "r");
                    if (f) {
                        fclose(f);
                    } else {
                        snprintf(target_file, sizeof(target_file), "%s/%s/module.nt", opts.stdlib_path, cp_buf);
                    }
                    
                    char *target_logical = arena_alloc(loader->arena, strlen(cl_buf) + 1);
                    strcpy(target_logical, cl_buf);
                    
                    int load_res = load_module_recursive(loader, target_file, target_logical, unit->absolute_path, 1);
                    if (load_res == 0) { // EXIT_OK
                        imp->resolved_logical_path = target_logical;
                    }
                }
            }
        }
    }

    // 5. Sema
    res.store = typestore_create(res.arena, identifiers, keywords);
    res.sema_ctx = typecheck_context_create(res.arena, res.store, identifiers, keywords, "<test>", loader);
    typecheck_program(&res.sema_ctx);
    
    if (res.sema_ctx.errors->count > 0) {
        res.failed = true;
    }

    return res;
}

void test_cleanup_compilation(TestCompileResult *res) {
    if (res->arena) arena_destroy(res->arena);
}

bool test_is_lex_valid(const char *src) {
    Arena *arena = arena_create(64 * 1024);
    Lexer *l = lexer_create(src, strlen(src), arena);
    bool ok = lexer_lex_all(l);
    arena_destroy(arena);
    return ok;
}

bool test_is_parse_valid(const char *src) {
    Arena *arena = arena_create(1024 * 1024);
    Lexer *l = lexer_create(src, strlen(src), arena);
    lexer_lex_all(l);
    Parser *p = parser_create(l->tokens, "<test>", arena);
    ParseError p_err = {0};
    AstNode *prog = parse_program(p, &p_err);
    bool ok = (p_err.message == NULL && prog != NULL);
    arena_destroy(arena);
    return ok;
}

bool test_is_sema_valid(const char *src) {
    TestCompileResult res = test_compile_source(src);
    bool ok = !res.failed;
    if (!ok) {
        test_log("\n      %s[Sema Validation Failed]%s\n", COL_RED, COL_RESET);
        test_log("      Source: %s\n", src);
        if (res.sema_ctx.errors) {
            for (size_t i = 0; i < res.sema_ctx.errors->count; i++) {
                TypeError *err = (TypeError*)dynarray_get(res.sema_ctx.errors, i);
                test_log("      Error %zu: Type Error Kind %d\n", i + 1, err->kind);
            }
        }
    }
    test_cleanup_compilation(&res);
    return ok;
}

bool test_check_sema_error(const char *src, TypeErrorKind kind) {
    TestCompileResult res = test_compile_source(src);
    bool found = false;
    if (res.sema_ctx.errors) {
        for (size_t i = 0; i < res.sema_ctx.errors->count; i++) {
            TypeError *err = (TypeError*)dynarray_get(res.sema_ctx.errors, i);
            if (err->kind == kind) {
                found = true;
                break;
            }
        }
    }
    test_cleanup_compilation(&res);
    return found;
}

int test_run_and_get_exit_code(const char *src) {
    TestCompileResult res = test_compile_source(src);
    if (res.failed) {
        if (res.sema_ctx.errors) {
            for (size_t i = 0; i < res.sema_ctx.errors->count; i++) {
                TypeError *err = (TypeError*)dynarray_get(res.sema_ctx.errors, i);
                fprintf(stderr, "      Sema Error %zu: Kind %d at span %u:%u to %u:%u in %s\n", i + 1, err->kind, err->span.start_line, err->span.start_col, err->span.end_line, err->span.end_col, err->filename);
            }
        }
        test_cleanup_compilation(&res);
        return -100;
    }
    
    CodegenContext *cg_ctx = codegen_context_create(res.store, "test_jit", 0, res.sema_ctx.loader);
    if (codegen_program(cg_ctx) != 0) {
        codegen_context_destroy(cg_ctx);
        test_cleanup_compilation(&res);
        return -101;
    }

    int exit_code = codegen_run_jit(cg_ctx);

    codegen_context_destroy(cg_ctx);
    test_cleanup_compilation(&res);
    return exit_code;
}
