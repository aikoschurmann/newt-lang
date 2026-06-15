#pragma once
#include "arena.h"
#include "lexing/lexer.h"
#include "parsing/parser.h"
#include "parsing/parse_statements.h"
#include "parsing/parse_expressions.h"
#include "parsing/parse_types.h"
#include "parsing/parse_declarations.h"
#include "sema/typecheck.h"
#include "codegen/codegen.h"

typedef struct {
    Arena *arena;
    Lexer *lexer;
    Parser *parser;
    AstNode *program;
    TypeStore *store;
    TypeCheckContext sema_ctx;
    bool failed;
} TestCompileResult;

TestCompileResult test_compile_source(const char *src);
void test_cleanup_compilation(TestCompileResult *res);

// Specific helpers for different stages
bool test_is_lex_valid(const char *src);
bool test_is_parse_valid(const char *src);
bool test_is_sema_valid(const char *src);
bool test_check_sema_error(const char *src, TypeErrorKind kind);
int test_run_and_get_exit_code(const char *src);
