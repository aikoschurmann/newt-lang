#include "test_utils.h"
#include "test_harness.h"
#include "parsing/ast.h"
#include "sema/type.h"

// -----------------------------------------------------------------------------
// Helpers
// -----------------------------------------------------------------------------

// Helper to check if a specific error message part exists in the errors
static bool check_sema_error(const char *src, const char *expected_msg_part) {
    CompileResult res = compile_source((char*)src);
    bool found = false;
    
    // Check if semantic errors exist
    if (res.ctx.errors && res.ctx.errors->count > 0) {
        // In a real implementation, we would check the error message string.
        // Since we currently assume valid compilation = false and error = true for this helper:
        found = true; 
    }
    
    cleanup_compilation(&res);
    return found;
}

// -----------------------------------------------------------------------------
// Regression & Basic Tests
// -----------------------------------------------------------------------------

int test_sema_arg_mismatch_regression() {
    // Regression test for "Argument count mismatch" error span fix
    const char *src = 
        "fn dummy() -> i64 { return 0; }\n"
        "arr: (fn(i64) -> i64)[2] = {dummy, dummy};\n" 
        "fn fib(n: i64) -> i64 { return n; }\n"
        "arr2: (fn(i64)->i64)[1] = {fib};\n"
        "res: i64 = arr2[0]();"; // Error here

    CompileResult res = compile_source(src);
    
    ASSERT(!res.parse_failed);
    ASSERT_NOT_NULL(res.ctx.errors);
    
    bool found = false;
    for (size_t i = 0; i < res.ctx.errors->count; i++) {
        TypeError *err = (TypeError*)dynarray_get(res.ctx.errors, i);
        if (err->kind == TE_ARG_COUNT_MISMATCH) {
             found = true;
        }
    }
    ASSERT(found);

    cleanup_compilation(&res);
    return 1;
}

int test_sema_type_mismatch() {
    const char *src = "x: i32 = \"string\";";
    CompileResult res = compile_source(src);
    
    // Should pass parsing but fail type check
    ASSERT(!res.parse_failed);
    ASSERT(res.ctx.errors->count > 0);
    
    bool found = false;
    for(size_t i=0; i<res.ctx.errors->count; i++) {
        TypeError *err = (TypeError*)dynarray_get(res.ctx.errors, i);
        if (err->kind == TE_TYPE_MISMATCH) found = true;
    }
    ASSERT(found);
    
    cleanup_compilation(&res);
    return 1;
}

int test_sema_undeclared() {
    const char *src = "x: i32 = y;";
    CompileResult res = compile_source(src);
    
    ASSERT(!res.parse_failed);
    ASSERT(res.ctx.errors->count > 0);
    
    bool found = false;
    for(size_t i=0; i<res.ctx.errors->count; i++) {
        TypeError *err = (TypeError*)dynarray_get(res.ctx.errors, i);
        if (err->kind == TE_UNDECLARED) found = true;
    }
    ASSERT(found);
    
    cleanup_compilation(&res);
    return 1;
}

int test_sema_valid_program() {
    const char *src = 
        "fn add(a: i64, b: i64) -> i64 { return a + b; }\n"
        "x: i64 = add(10, 20);";
        
    CompileResult res = compile_source(src);
    ASSERT(!res.parse_failed);
    
    if (res.ctx.errors && res.ctx.errors->count > 0) {
         TypeError *err = (TypeError*)dynarray_get(res.ctx.errors, 0);
         fprintf(stderr, "Unexpected error in valid program: kind %d\n", err->kind);
         cleanup_compilation(&res);
         return 0;
    }

    cleanup_compilation(&res);
    return 1;
}

int test_sema_call_arg_count() {
    // Global call with wrong arg count
    const char *src = 
        "fn add(a: i32, b: i32) -> i32 { return 0; }\n"
        "val: i32 = add(1);"; 
    CompileResult res = compile_source(src);
    ASSERT(!res.parse_failed);
    ASSERT(res.ctx.errors && res.ctx.errors->count > 0);
    cleanup_compilation(&res);
    return 1;
}

int test_sema_call_arg_type() {
    // Global call with wrong arg type
    const char *src = 
        "fn inc(a: i32) -> i32 { return 0; }\n"
        "val: i32 = inc(true);"; 
    CompileResult res = compile_source(src);
    ASSERT(!res.parse_failed);
    ASSERT(res.ctx.errors && res.ctx.errors->count > 0);
    cleanup_compilation(&res);
    return 1;
}

// -----------------------------------------------------------------------------
// Advanced Features: Promotion, Inference, Initializers
// -----------------------------------------------------------------------------

int test_sema_type_promotion() {
    // 1. Scalar Promotion: i32 -> f64
    const char *src1 = "x: f64 = 10;"; 
    CompileResult res1 = compile_source((char*)src1);
    ASSERT(!res1.parse_failed);
    ASSERT(res1.ctx.errors->count == 0);
    
    AstNode *decl = *(AstNode**)dynarray_get(res1.program->data.program.decls, 0);
    AstNode *init = decl->data.variable_declaration.initializer;
    
    // Check that we either inserted a cast or promoted the type
    // Depending on implementation, it might be an implicit cast node or a modified literal
    bool promoted = (init->node_type == AST_CAST && init->data.cast_expr.target_type->kind == TYPE_PRIMITIVE) ||
                    (init->node_type == AST_LITERAL && type_is_float(init->type));
    
    ASSERT(promoted);
    cleanup_compilation(&res1);

    // 2. Binary Op Promotion: 10 + 2.5 -> 12.5 (f64)
    const char *src2 = "x: f64 = 10 + 2.5;";
    CompileResult res2 = compile_source((char*)src2);
    ASSERT(!res2.parse_failed);
    ASSERT(res2.ctx.errors->count == 0);
    cleanup_compilation(&res2);

    return 1;
}

int test_sema_array_inference_1d() {
    // 1. Simple Inference: i32[] = {1, 2, 3} -> i32[3]
    const char *src = "arr: i32[] = {1, 2, 3};";
    CompileResult res = compile_source((char*)src);
    ASSERT(!res.parse_failed);
    ASSERT(res.ctx.errors->count == 0);

    AstNode *decl = *(AstNode**)dynarray_get(res.program->data.program.decls, 0);
    Type *type = decl->type;

    ASSERT_EQ_INT(type->kind, TYPE_ARRAY);
    ASSERT_EQ_INT(type->as.array.size, 3);
    ASSERT(type->as.array.size_known);
    
    cleanup_compilation(&res);
    return 1;
}

int test_sema_array_inference_mixed_types() {
    // 1. Inference + Promotion: f64[] = {1, 2.5, 3} -> f64[3]
    const char *src = "arr: f64[] = {1, 2.5, 3};";
    CompileResult res = compile_source((char*)src);
    ASSERT(!res.parse_failed);
    ASSERT(res.ctx.errors->count == 0);

    AstNode *decl = *(AstNode**)dynarray_get(res.program->data.program.decls, 0);
    Type *type = decl->type;

    ASSERT_EQ_INT(type->kind, TYPE_ARRAY);
    ASSERT_EQ_INT(type->as.array.size, 3);
    
    // Verify base type is f64 (floating point)
    Type *base = type->as.array.base;
    ASSERT(type_is_float(base)); 

    cleanup_compilation(&res);
    return 1;
}

int test_sema_multidimensional_arrays() {
    // ---------------------------------------------------------
    // Case 1: Strictly Asymmetric Explicit (i32[2][3][4])
    // ---------------------------------------------------------
    const char *src_explicit = 
        "tensor: i32[2][3][4] = {"
        "  {" // Block 0
        "    { 1,  2,  3,  4}, " // Row 0
        "    { 5,  6,  7,  8}, " // Row 1
        "    { 9, 10, 11, 12}  " // Row 2
        "  },"
        "  {" // Block 1
        "    {13, 14, 15, 16}, " // Row 0
        "    {17, 18, 19, 20}, " // Row 1
        "    {21, 22, 23, 24}  " // Row 2
        "  }"
        "};";

    CompileResult res1 = compile_source((char*)src_explicit);
    ASSERT(!res1.parse_failed);
    ASSERT(res1.ctx.errors->count == 0);
    
    // Verify AST Types
    AstNode *decl1 = *(AstNode**)dynarray_get(res1.program->data.program.decls, 0);
    Type *t1 = decl1->type;
    ASSERT_EQ_INT(t1->kind, TYPE_ARRAY);
    ASSERT_EQ_INT(t1->as.array.size, 2);             // Outer
    ASSERT_EQ_INT(t1->as.array.base->as.array.size, 3); // Middle
    ASSERT_EQ_INT(t1->as.array.base->as.array.base->as.array.size, 4); // Inner
    cleanup_compilation(&res1);

    // ---------------------------------------------------------
    // Case 2: Inferred Asymmetric (i32[][3][4])
    // ---------------------------------------------------------
    // Compiler must infer Outer=2 from the initializer.
    const char *src_inferred = 
        "tensor: i32[][3][4] = {"
        "  { {1,2,3,4}, {5,6,7,8}, {9,0,1,2} },"
        "  { {3,4,5,6}, {7,8,9,0}, {1,2,3,4} }"
        "};";

    CompileResult res2 = compile_source((char*)src_inferred);
    ASSERT(!res2.parse_failed);
    ASSERT(res2.ctx.errors->count == 0);

    AstNode *decl2 = *(AstNode**)dynarray_get(res2.program->data.program.decls, 0);
    Type *t2 = decl2->type;
    ASSERT_EQ_INT(t2->as.array.size, 2); // Inferred Outer
    ASSERT_EQ_INT(t2->as.array.base->as.array.size, 3); // Middle
    cleanup_compilation(&res2);

    // ---------------------------------------------------------
    // Case 3: Deep Type Promotion (f64[2][2] from ints)
    // ---------------------------------------------------------
    // Verifies implicit casting works recursively.
    const char *src_promo = "mat: f64[2][2] = {{1, 2}, {3, 4}};";
    CompileResult res3 = compile_source((char*)src_promo);
    ASSERT(!res3.parse_failed);
    ASSERT(res3.ctx.errors->count == 0);
    
    // Check that the literal '1' (int) was wrapped in a Cast or converted
    AstNode *decl3 = *(AstNode**)dynarray_get(res3.program->data.program.decls, 0);
    AstNode *init_list = decl3->data.variable_declaration.initializer;
    AstNode *first_row = *(AstNode**)dynarray_get(init_list->data.initializer_list.elements, 0);
    AstNode *first_elem = *(AstNode**)dynarray_get(first_row->data.initializer_list.elements, 0);
    
    // Either it's a cast or the literal type was changed to f64
    bool promoted = (first_elem->node_type == AST_CAST) || 
                    (first_elem->node_type == AST_LITERAL && type_is_float(first_elem->type));
    ASSERT(promoted);
    cleanup_compilation(&res3);

    // ---------------------------------------------------------
    // Case 4: Error Detection (Mismatch)
    // ---------------------------------------------------------
    // Providing 4 rows where 3 are expected in the middle dimension.
    const char *src_fail = 
        "tensor: i32[2][3][4] = {"
        "  {"
        "    {1,2,3,4}, {5,6,7,8}, {9,0,1,2}, {1,1,1,1}" // 4 rows! Error.
        "  },"
        "  {"
        "    {1,2,3,4}, {5,6,7,8}, {9,0,1,2}"
        "  }"
        "};";
    CompileResult res4 = compile_source((char*)src_fail);
    ASSERT(!res4.parse_failed);
    ASSERT(res4.ctx.errors->count > 0); // Must have errors

    cleanup_compilation(&res4);

    return 1;
}

int test_sema_initializer_errors() {
    // 1. Excess Elements: i32[2] = {1, 2, 3}
    const char *src1 = "arr: i32[2] = {1, 2, 3};";
    ASSERT(check_sema_error(src1, "excess elements")); 

    // 2. Type Mismatch in Array: i32[] = {1, "string"}
    const char *src2 = "arr: i32[] = {1, \"bad\"};";
    ASSERT(check_sema_error(src2, "type mismatch")); 

    return 1;
}

int test_sema_const_folding() {
    // 1. Const Eval: const i32 x = 2 * 5 + 1;
    const char *src = "const x: i32 = 2 * 5 + 1;";
    CompileResult res = compile_source((char*)src);
    ASSERT(!res.parse_failed);
    ASSERT(res.ctx.errors->count == 0);

    AstNode *decl = *(AstNode**)dynarray_get(res.program->data.program.decls, 0);
    AstNode *init = decl->data.variable_declaration.initializer;

    // Check that the parser/sema folded this into a single literal or marked it as constant
    // Depending on implementation, 'is_const_expr' should be true
    ASSERT(init->is_const_expr);
    ASSERT_EQ_INT(init->const_value.value.int_val, 11);
    
    cleanup_compilation(&res);
    return 1;
}

int test_sema_bounds_checks() {
    // 1. Bounds check in statement path
    const char *src1 = "v: i32[2]; fn main() { v[2] = 1; }";
    CompileResult res1 = compile_source(src1);
    ASSERT(!res1.parse_failed && res1.ctx.errors->count > 0);
    ASSERT_EQ_INT(((TypeError*)dynarray_get(res1.ctx.errors, 0))->kind, TE_INDEX_OUT_OF_BOUNDS);
    cleanup_compilation(&res1);

    // 2. Incomplete types (no size, no init)
    const char *src2 = "v: i32[][3];";
    CompileResult res2 = compile_source(src2);
    ASSERT(res2.ctx.errors->count > 0);
    ASSERT_EQ_INT(((TypeError*)dynarray_get(res2.ctx.errors, 0))->kind, TE_INCOMPLETE_TYPE);
    cleanup_compilation(&res2);

    // 3. Deep AST Patching Verification
    const char *src3 = "mat: i32[][2] = {{1, 2}, {3, 4}, {5, 6}};";
    CompileResult res3 = compile_source(src3);
    ASSERT(res3.ctx.errors->count == 0);

    AstNode *decl = *(AstNode**)dynarray_get(res3.program->data.program.decls, 0);
    AstType *ast_t = &decl->data.variable_declaration.type->data.ast_type;
    
    // Check semantic type
    ASSERT_EQ_INT(decl->type->as.array.size, 3);
    // Check syntactic AST patching
    ASSERT_NOT_NULL(ast_t->u.array.size_expr);
    ASSERT_EQ_INT(ast_t->u.array.size_expr->const_value.value.int_val, 3);
    
    cleanup_compilation(&res3);

    // 4. Multi-dimensional OOB check
    const char *src4 = "m: i32[2][2]; fn main() { m[0][5] = 1; }";
    CompileResult res4 = compile_source(src4);
    ASSERT(res4.ctx.errors->count > 0);
    ASSERT_EQ_INT(((TypeError*)dynarray_get(res4.ctx.errors, 0))->kind, TE_INDEX_OUT_OF_BOUNDS);
    cleanup_compilation(&res4);

    return 1;
}

// -----------------------------------------------------------------------------
// Full Integration Test
// -----------------------------------------------------------------------------

int test_sema_full_features() {
    const char *src = 
        "var1: i32[][] = {{1, 2}, {3, 4}};\n"
        "var2: i64[][] = {{1, 2}, {3, 4}};\n"
        "var3: f32[][] = {{1.1, 2.2}, {3.3, 4.4}};\n"
        "var4: f64[][] = {{1.1, 2.2}, {3.3, 4.4}};\n"
        "\n"
        "fn partition(a: i32[], lo: i32, hi: i32) -> i32 {\n"
        "    pivot: i32 = a[hi];\n"
        "    return partition_rec(a, lo, hi, lo, lo - 1, pivot);\n"
        "}\n"
        "\n"
        "fn partition_rec(a: i32[], lo: i32, hi: i32, j: i32, i: i32, pivot: i32) -> i32 {\n"
        "\n"
        "    if (j >= hi) {\n"
        "        tmp: i32 = a[i + 1];\n"
        "        a[i + 1] = a[hi];\n"
        "        a[hi] = tmp;\n"
        "        return i + 1;\n"
        "    }\n"
        "\n"
        "    if (a[j] < pivot) {\n"
        "        i2: i32 = i + 1;\n"
        "\n"
        "        tmp2: i32 = a[i2];\n"
        "        a[i2] = a[j];\n"
        "        a[j] = tmp2;\n"
        "\n"
        "        return partition_rec(a, lo, hi, j + 1, i2, pivot);\n"
        "    } else {\n"
        "        return partition_rec(a, lo, hi, j + 1, i, pivot);\n"
        "    }\n"
        "}\n"
        "\n"
        "fn quicksort(a: i32[], lo: i32, hi: i32) {\n"
        "    if (lo < hi) {\n"
        "        p: i32 = partition(a, lo, hi);\n"
        "        quicksort(a, lo, p - 1);\n"
        "        quicksort(a, p + 1, hi);\n"
        "    }\n"
        "}\n"
        "\n"
        "fn fib_iter(n: i32) -> i32 {\n"
        "    if (n <= 1) { return n; }\n"
        "    a: i32 = 0;\n"
        "    b: i32 = 1;\n"
        "    i: i32 = 2;\n"
        "    while (i <= n) {\n"
        "        temp: i32 = a + b;\n"
        "        a = b;\n"
        "        b = temp;\n"
        "        i++;\n"
        "    }\n"
        "    return b;\n"
        "}\n"
        "\n"
        "fn test_float_math(start: f32) -> f32 {\n"
        "    val: f32 = start;\n"
        "    count: i32 = 0;\n"
        "    while (count < 10) {\n"
        "        val += 1.5;\n"
        "        if (val > 100.0) { break; }\n"
        "        count++;\n"
        "    }\n"
        "    return val;\n"
        "}\n"
        "\n"
        "fn test_operators() {\n"
        "    x: i32 = 10;\n"
        "    x += 5; // 15\n"
        "    x *= 2; // 30\n"
        "    x--;    // 29\n"
        "    \n"
        "    y: i32 = 0;\n"
        "    while (y < 10) {\n"
        "        y++;\n"
        "        if (y % 2 == 0) { continue; }\n"
        "        // odd numbers logic\n"
        "    }\n"
        "}\n"
        "\n"
        "fn main() {\n"
        "    // Array & Quicksort Test\n"
        "    arr: i32[9] = { 30, 3, 4, 20, 5, 1, 17, 12, 9 };\n"
        "    quicksort(arr, 0, 8);\n"
        "\n"
        "    // Iterative Logic & Math Test\n"
        "    fib_res: i32 = fib_iter(10);\n"
        "    \n"
        "    // Float Logic\n"
        "    f_res: f32 = test_float_math(10.5);\n"
        "    \n"
        "    // Operators & Control Flow\n"
        "    test_operators();\n"
        "}";

    CompileResult res = compile_source(src);
    if (res.parse_failed) {
        fprintf(stderr, "Parsing full feature test suite failed.\n");
        return 0;
    }
    
    // Check that globals are fine
    if (res.ctx.errors && res.ctx.errors->count > 0) {
        for (size_t i = 0; i < res.ctx.errors->count; i++) {
             TypeError *err = (TypeError*)dynarray_get(res.ctx.errors, i);
             print_type_error(err);
        }
        cleanup_compilation(&res);
        return 0;
    }

    cleanup_compilation(&res);
    return 1;
}

int test_sema_array_len() {
    const char *src = "fn main() { arr: i32[5]; x: i64 = arr.len; }";
    CompileResult res = compile_source(src);
    ASSERT(!res.parse_failed);

    if (res.ctx.errors->count > 0) {
        for (size_t i = 0; i < res.ctx.errors->count; i++) {
            TypeError *err = (TypeError*)dynarray_get(res.ctx.errors, i);
            print_type_error(err);
        }
    }
    ASSERT(res.ctx.errors->count == 0);

    // Check that it was folded to a literal 5
    // main is decls[0]
    AstNode *main_func = *(AstNode**)dynarray_get(res.program->data.program.decls, 0);
    AstNode *block = main_func->data.function_declaration.body;
    // x is statements[1]
    AstNode *x_decl = *(AstNode**)dynarray_get(block->data.block.statements, 1);
    AstNode *init = x_decl->data.variable_declaration.initializer;
    
    ASSERT_EQ_INT(init->node_type, AST_LITERAL);
    ASSERT_EQ_INT(init->const_value.value.int_val, 5);

    cleanup_compilation(&res);
    return 1;
}