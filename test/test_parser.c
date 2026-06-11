#include "test_utils.h"
#include <string.h>

// Helper to check for specific parse error
static bool check_parse_error(const char *src, const char *expected_msg_part) {
    Arena *arena = arena_create(1024);
    Lexer *l = lexer_create(src, strlen(src), arena);
    if (!lexer_lex_all(l)) {
        arena_destroy(arena);
        return false;
    }
    
    char *fname = arena_alloc(arena, 8);
    strcpy(fname, "test");
    Parser *p = parser_create(l->tokens, fname, arena);
    ParseError err = {0};
    AstNode *prog = parse_program(p, &err);
    
    bool result = false;
    if (err.message) {
        if (strstr(err.message, expected_msg_part)) {
            result = true;
        } else {
            fprintf(stderr, "Expected error containing '%s', got '%s'\n", expected_msg_part, err.message);
        }
    } else {
        fprintf(stderr, "Expected parse error '%s', but got success.\n", expected_msg_part);
    }
    
    arena_destroy(arena);
    return result;
}

int test_parser_expression() {
    // 1 + 2 * 3
    Arena *arena = arena_create(1024*1024);
    const char *src = "1 + 2 * 3";
    Lexer *lexer = lexer_create(src, strlen(src), arena);
    lexer_lex_all(lexer);
    Parser *p = parser_create(lexer->tokens, "test", arena);
    
    ParseError err = {0};
    AstNode *expr = parse_expression(p, &err);
    ASSERT_NOT_NULL(expr);
    ASSERT_EQ_INT(expr->node_type, AST_BINARY_EXPR);
    ASSERT_EQ_INT(expr->data.binary_expr.op, OP_ADD);
    
    // Check Precedence: (1 + (2 * 3))
    AstNode *rhs = expr->data.binary_expr.right;
    ASSERT_EQ_INT(rhs->node_type, AST_BINARY_EXPR);
    ASSERT_EQ_INT(rhs->data.binary_expr.op, OP_MUL);

    lexer_destroy(lexer);
    arena_destroy(arena);
    return 1;
}

int test_parser_unary_precedence() {
    // -a * b  should be (-a) * b
    Arena *arena = arena_create(1024*1024);
    const char *src = "-a * b";
    Lexer *lexer = lexer_create(src, strlen(src), arena);
    lexer_lex_all(lexer);
    Parser *p = parser_create(lexer->tokens, "test", arena);
    
    ParseError err = {0};
    AstNode *expr = parse_expression(p, &err);
    ASSERT_NOT_NULL(expr);
    ASSERT_EQ_INT(expr->node_type, AST_BINARY_EXPR); // *
    ASSERT_EQ_INT(expr->data.binary_expr.op, OP_MUL);
    
    AstNode *lhs = expr->data.binary_expr.left;
    ASSERT_EQ_INT(lhs->node_type, AST_UNARY_EXPR); // -a
    ASSERT_EQ_INT(lhs->data.unary_expr.op, OP_SUB);
    
    lexer_destroy(lexer);
    arena_destroy(arena);
    return 1;
}

int test_parser_comparison_precedence() {
    // a < b && c > d -> (a < b) && (c > d)
    // Precedence: logical AND (&&) is lower than relational (<, >)
    Arena *arena = arena_create(1024*1024);
    const char *src = "a < b && c > d";
    Lexer *lexer = lexer_create(src, strlen(src), arena);
    lexer_lex_all(lexer);
    Parser *p = parser_create(lexer->tokens, "test", arena);
    
    ParseError err = {0};
    AstNode *expr = parse_expression(p, &err);
    ASSERT_NOT_NULL(expr);
    ASSERT_EQ_INT(expr->node_type, AST_BINARY_EXPR);
    ASSERT_EQ_INT(expr->data.binary_expr.op, OP_AND); // && is top level
    
    ASSERT_EQ_INT(expr->data.binary_expr.left->node_type, AST_BINARY_EXPR);
    ASSERT_EQ_INT(expr->data.binary_expr.left->data.binary_expr.op, OP_LT);
    
    ASSERT_EQ_INT(expr->data.binary_expr.right->node_type, AST_BINARY_EXPR);
    ASSERT_EQ_INT(expr->data.binary_expr.right->data.binary_expr.op, OP_GT);

    lexer_destroy(lexer);
    arena_destroy(arena);
    return 1;
}

int test_parser_paren_expression() {
    // (1 + 2) * 3
    Arena *arena = arena_create(1024*1024);
    const char *src = "(1 + 2) * 3";
    Lexer *lexer = lexer_create(src, strlen(src), arena);
    lexer_lex_all(lexer);
    Parser *p = parser_create(lexer->tokens, "test", arena);
    
    ParseError err = {0};
    AstNode *expr = parse_expression(p, &err);
    ASSERT_NOT_NULL(expr);
    ASSERT_EQ_INT(expr->node_type, AST_BINARY_EXPR);
    ASSERT_EQ_INT(expr->data.binary_expr.op, OP_MUL);
    
    // Check Left side is Add
    AstNode *lhs = expr->data.binary_expr.left;
    ASSERT_EQ_INT(lhs->node_type, AST_BINARY_EXPR);
    ASSERT_EQ_INT(lhs->data.binary_expr.op, OP_ADD);

    lexer_destroy(lexer);
    arena_destroy(arena);
    return 1;
}

int test_parser_if_statement() {
    char *src = "fn test() { if (true) { return 1; } else { return 0; } }";
    CompileResult res = compile_source(src);
    ASSERT(!res.parse_failed);
    
    // Traverse AST to find IfStatement
    // Program -> Decl -> Function -> Block -> If
    AstNode *prog = res.program;
    AstNode *func = *(AstNode**)dynarray_get(prog->data.program.decls, 0);
    AstBlock *body = &func->data.function_declaration.body->data.block;
    AstNode *stmt = *(AstNode**)dynarray_get(body->statements, 0);
    
    ASSERT_EQ_INT(stmt->node_type, AST_IF_STATEMENT);
    ASSERT_NOT_NULL(stmt->data.if_statement.else_branch);

    cleanup_compilation(&res);
    return 1;
}


int test_parser_unclosed_paren() {
    const char *src = "fn main() { x = (1 + 2; }"; // missing closing paren
    ASSERT(check_parse_error(src, "expected ')'"));
    return 1;
}

int test_parser_missing_semicolon() {
    const char *src = "fn main() { let x = 10 }"; // missing semicolon
    ASSERT(check_parse_error(src, "expected ';'")); // or similar message
    return 1;
}

int test_parser_bad_stmt_start() {
    const char *src2 = "fn main() { ) 5; }"; 
    ASSERT(check_parse_error(src2, "expected")); 
    return 1;
}

int test_parser_nested_parens() {
    // Deeply nested
    char buf[1024];
    strcpy(buf, "fn main() { x = ");
    for(int i=0; i<50; i++) strcat(buf, "(");
    strcat(buf, "1");
    for(int i=0; i<50; i++) strcat(buf, ")");
    strcat(buf, "; }");
    
    CompileResult res = compile_source(buf);
    ASSERT(!res.parse_failed);
    
    cleanup_compilation(&res);
    return 1;
}

int test_parser_extra_tokens() {
    const char *src = "fn main() {} 123"; // trailing tokens
    // parse_program loops until EOF mostly. If top level only allows declarations, 123 is invalid.
    ASSERT(check_parse_error(src, "expected function")); // or similar
    return 1;
}

int test_parser_empty_file() {
    const char *src = "";
    CompileResult res = compile_source(src);
    ASSERT(!res.parse_failed); 
    // Usually empty file is valid (empty translation unit)
    cleanup_compilation(&res);
    return 1;
}

int test_exception_deep_blocks() {
    // 500 levels of braces
    int levels = 200;
    char *buf = malloc(levels * 2 + 50);
    char *p = buf;
    p += sprintf(p, "fn main() { ");
    for(int i=0; i<levels; i++) *p++ = '{';
    for(int i=0; i<levels; i++) *p++ = '}';
    *p++ = '}';
    *p = 0;
    
    CompileResult res = compile_source(buf);
    
    cleanup_compilation(&res);
    free(buf);
    return 1;
}

int test_parser_for_statement() {
    // 1. Test Standard Loop Structure: for (i: i32 = 0; i < 10; i++) {}
    const char *src = "fn main() { for (i: i32 = 0; i < 10; i++) { return; } }";
    CompileResult res = compile_source((char*)src);
    ASSERT(!res.parse_failed);
    
    // Traverse AST: Program -> Decl -> Function -> Block -> ForStatement
    AstNode *prog = res.program;
    AstNode *func = *(AstNode**)dynarray_get(prog->data.program.decls, 0);
    AstBlock *func_body = &func->data.function_declaration.body->data.block;
    AstNode *stmt = *(AstNode**)dynarray_get(func_body->statements, 0);
    
    ASSERT_EQ_INT(stmt->node_type, AST_FOR_STATEMENT);
    
    // Check Init (VariableDeclaration)
    AstNode *init = stmt->data.for_statement.init;
    ASSERT_NOT_NULL(init);
    ASSERT_EQ_INT(init->node_type, AST_VARIABLE_DECLARATION);
    
    // Check Condition (BinaryExpr)
    AstNode *cond = stmt->data.for_statement.condition;
    ASSERT_NOT_NULL(cond);
    ASSERT_EQ_INT(cond->node_type, AST_BINARY_EXPR);
    ASSERT_EQ_INT(cond->data.binary_expr.op, OP_LT);
    
    // Check Post (UnaryExpr i++)
    AstNode *post = stmt->data.for_statement.post;
    ASSERT_NOT_NULL(post);
    ASSERT_EQ_INT(post->node_type, AST_UNARY_EXPR); // Postfix is often stored as Unary or Postfix depending on impl
    // Based on your parser, ++ is mapped to OP_POST_INC/DEC
    
    cleanup_compilation(&res);

    // 2. Test Variations (ensure they don't crash or fail parse)
    
    // Missing Init: for (; i < 10; i++)
    const char *src_no_init = "fn main() { i: i32 = 0; for (; i < 10; i++) {} }";
    res = compile_source((char*)src_no_init);
    ASSERT(!res.parse_failed);
    cleanup_compilation(&res);

    // Missing Condition (Infinite): for (i=0; ; i++)
    const char *src_no_cond = "fn main() { for (i: i32=0; ; i++) { break; } }";
    res = compile_source((char*)src_no_cond);
    ASSERT(!res.parse_failed);
    cleanup_compilation(&res);

    // Missing Post: for (i=0; i<10; )
    const char *src_no_post = "fn main() { for (i: i32=0; i<10; ) { i++; } }";
    res = compile_source((char*)src_no_post);
    ASSERT(!res.parse_failed);
    cleanup_compilation(&res);

    // Forever Loop: for (;;)
    const char *src_forever = "fn main() { for (;;) { break; } }";
    res = compile_source((char*)src_forever);
    ASSERT(!res.parse_failed);
    cleanup_compilation(&res);

    // Expression Init (assignment instead of decl): for (x = 0; x < 10; x++)
    const char *src_expr_init = "fn main() { x: i32; for (x = 0; x < 10; x++) {} }";
    res = compile_source((char*)src_expr_init);
    ASSERT(!res.parse_failed);
    cleanup_compilation(&res);

    return 1;
}

int test_parser_while_statement() {
    // 1. Standard While: while (x < 10) { ... }
    const char *src = "fn main() { while (x < 10) { x++; } }";
    CompileResult res = compile_source((char*)src);
    ASSERT(!res.parse_failed);

    AstNode *prog = res.program;
    AstNode *func = *(AstNode**)dynarray_get(prog->data.program.decls, 0);
    AstBlock *func_body = &func->data.function_declaration.body->data.block;
    AstNode *stmt = *(AstNode**)dynarray_get(func_body->statements, 0);

    ASSERT_EQ_INT(stmt->node_type, AST_WHILE_STATEMENT);
    ASSERT_NOT_NULL(stmt->data.while_statement.condition);
    ASSERT_NOT_NULL(stmt->data.while_statement.body);
    
    cleanup_compilation(&res);

    // 2. Optional Parentheses: while x < 10 { ... }
    // Your grammar: <WhileStmt> ::= WHILE [ LPAREN ] <Expression> [ RPAREN ] <Block>
    const char *src_no_paren = "fn main() { while x < 10 { x--; } }";
    res = compile_source((char*)src_no_paren);
    ASSERT(!res.parse_failed);
    cleanup_compilation(&res);

    return 1;
}

int test_parser_arrays_and_pointers() {
    // 1. Array Indexing: a[10]
    const char *src_arr = "fn main() { val = a[10]; }";
    CompileResult res = compile_source((char*)src_arr);
    ASSERT(!res.parse_failed);
    
    AstNode *func = *(AstNode**)dynarray_get(res.program->data.program.decls, 0);
    AstNode *stmt = *(AstNode**)dynarray_get(func->data.function_declaration.body->data.block.statements, 0);
    // stmt is Assignment -> rvalue is Subscript
    AstNode *rvalue = stmt->data.assignment_expr.rvalue;
    
    ASSERT_EQ_INT(rvalue->node_type, AST_SUBSCRIPT_EXPR);
    ASSERT_NOT_NULL(rvalue->data.subscript_expr.target); // 'a'
    ASSERT_NOT_NULL(rvalue->data.subscript_expr.index);  // '10'
    
    cleanup_compilation(&res);

    // 2. Multi-dimensional: a[1][2]
    // Should parse as (a[1])[2]
    const char *src_multi = "fn main() { val = a[1][2]; }";
    res = compile_source((char*)src_multi);
    ASSERT(!res.parse_failed);
    cleanup_compilation(&res);

    // 3. Pointer Dereference: *x
    const char *src_ptr = "fn main() { val = *x; }";
    res = compile_source((char*)src_ptr);
    ASSERT(!res.parse_failed);
    
    func = *(AstNode**)dynarray_get(res.program->data.program.decls, 0);
    stmt = *(AstNode**)dynarray_get(func->data.function_declaration.body->data.block.statements, 0);
    rvalue = stmt->data.assignment_expr.rvalue;

    ASSERT_EQ_INT(rvalue->node_type, AST_UNARY_EXPR);
    ASSERT_EQ_INT(rvalue->data.unary_expr.op, OP_DEREF);
    
    cleanup_compilation(&res);

    return 1;
}

int test_parser_member_access() {
    // a.b
    Arena *arena = arena_create(1024*1024);
    const char *src = "a.b";
    Lexer *lexer = lexer_create(src, strlen(src), arena);
    lexer_lex_all(lexer);
    Parser *p = parser_create(lexer->tokens, "test", arena);
    
    ParseError err = {0};
    AstNode *expr = parse_expression(p, &err);
    ASSERT_NOT_NULL(expr);
    ASSERT_EQ_INT(expr->node_type, AST_MEMBER_EXPR);
    ASSERT_NOT_NULL(expr->data.member_expr.target);
    ASSERT_NOT_NULL(expr->data.member_expr.member);

    lexer_destroy(lexer);
    arena_destroy(arena);
    return 1;
}

int test_parser_flow_control() {
    // 1. Return with value
    const char *src_ret = "fn main() { return 0; }";
    CompileResult res = compile_source((char*)src_ret);
    ASSERT(!res.parse_failed);
    
    AstNode *func = *(AstNode**)dynarray_get(res.program->data.program.decls, 0);
    AstNode *stmt = *(AstNode**)dynarray_get(func->data.function_declaration.body->data.block.statements, 0);
    
    ASSERT_EQ_INT(stmt->node_type, AST_RETURN_STATEMENT);
    ASSERT_NOT_NULL(stmt->data.return_statement.expression);
    cleanup_compilation(&res);

    // 2. Return void (no expression)
    const char *src_ret_void = "fn main() { return; }";
    res = compile_source((char*)src_ret_void);
    ASSERT(!res.parse_failed);
    func = *(AstNode**)dynarray_get(res.program->data.program.decls, 0);
    stmt = *(AstNode**)dynarray_get(func->data.function_declaration.body->data.block.statements, 0);
    ASSERT_EQ_INT(stmt->node_type, AST_RETURN_STATEMENT);
    ASSERT(stmt->data.return_statement.expression == NULL);
    cleanup_compilation(&res);

    return 1;
}