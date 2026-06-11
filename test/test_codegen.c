#include "test_utils.h"
#include "test_harness.h"
#include "codegen/codegen.h"
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <sys/wait.h>

// Helper to compile and run a source string, returning the exit code of the generated program
static int run_compiled_code(const char *src) {
    CompileResult res = compile_source(src);
    if (res.parse_failed || (res.ctx.errors && res.ctx.errors->count > 0)) {
        cleanup_compilation(&res);
        return -100; // Compilation failed
    }

    CodegenContext *cg_ctx = codegen_context_create(res.program, res.store, "test_module");
    if (codegen_program(cg_ctx) != 0) {
        codegen_context_destroy(cg_ctx);
        cleanup_compilation(&res);
        return -101; // Codegen failed
    }

    codegen_emit_object(cg_ctx, "test_output.o");
    
    // Link with runtime
    int link_ret = system("cc test_output.o src/core/runtime.c -o test_output 2>/dev/null");
    if (link_ret != 0) {
        codegen_context_destroy(cg_ctx);
        cleanup_compilation(&res);
        return -102; // Linking failed
    }

    // Run the executable
    int run_ret = system("./test_output");
    int exit_code = WEXITSTATUS(run_ret);

    // Cleanup artifacts
    unlink("test_output.o");
    unlink("test_output");
    unlink("output.ll");

    codegen_context_destroy(cg_ctx);
    cleanup_compilation(&res);

    return exit_code;
}

int test_codegen_basic_arithmetic() {
    const char *src = "fn main() -> i32 { return (10 + 20) * 2 / 5; }";
    int code = run_compiled_code(src);
    ASSERT_EQ_INT(code, 12);
    return 1;
}

int test_codegen_recursion() {
    const char *src = 
        "fn factorial(n: i32) -> i32 {\n"
        "  if (n <= 1) { return 1; }\n"
        "  return n * factorial(n - 1);\n"
        "}\n"
        "fn main() -> i32 { return factorial(5); }";
    int code = run_compiled_code(src);
    ASSERT_EQ_INT(code, 120);
    return 1;
}

int test_codegen_array_sort() {
    const char *src = 
        "fn swap(a: i32[]*, i: i32, j: i32) {\n"
        "    temp: i32 = (*a)[i];\n"
        "    (*a)[i] = (*a)[j];\n"
        "    (*a)[j] = temp;\n"
        "}\n"
        "fn main() -> i32 {\n"
        "    arr: i32[3] = { 3, 1, 2 };\n"
        "    if (arr[0] > arr[1]) { swap(&arr, 0, 1); }\n"
        "    if (arr[1] > arr[2]) { swap(&arr, 1, 2); }\n"
        "    if (arr[0] > arr[1]) { swap(&arr, 0, 1); }\n"
        "    return arr[0] * 100 + arr[1] * 10 + arr[2];\n" // 123
        "}\n";
    int code = run_compiled_code(src);
    ASSERT_EQ_INT(code, 123);
    return 1;
}

int test_codegen_logic() {
    const char *src = 
        "fn main() -> i32 {\n"
        "  a: bool = true;\n"
        "  b: bool = false;\n"
        "  if (a && !b) { return 1; }\n"
        "  return 0;\n"
        "}";
    int code = run_compiled_code(src);
    ASSERT_EQ_INT(code, 1);
    return 1;
}

int test_codegen_complex_loop() {
    const char *src = 
        "fn main() -> i32 {\n"
        "  sum: i32 = 0;\n"
        "  for(i: i32 = 1; i <= 10; i++) {\n"
        "    if (i % 2 == 0) { sum += i; }\n"
        "  }\n"
        "  return sum;\n" // 2 + 4 + 6 + 8 + 10 = 30
        "}";
    int code = run_compiled_code(src);
    ASSERT_EQ_INT(code, 30);
    return 1;
}

int test_codegen_fib_recursive() {
    const char *src = 
        "fn fib(n: i32) -> i32 {\n"
        "  if (n <= 1) { return n; }\n"
        "  return fib(n-1) + fib(n-2);\n"
        "}\n"
        "fn main() -> i32 { return fib(10); }"; // fib(10) = 55
    int code = run_compiled_code(src);
    ASSERT_EQ_INT(code, 55);
    return 1;
}

int test_codegen_nested_loops() {
    const char *src = 
        "fn main() -> i32 {\n"
        "  count: i32 = 0;\n"
        "  for(i: i32 = 0; i < 5; i++) {\n"
        "    for(j: i32 = 0; j < 5; j++) {\n"
        "      count++;\n"
        "    }\n"
        "  }\n"
        "  return count;\n" // 25
        "}";
    int code = run_compiled_code(src);
    ASSERT_EQ_INT(code, 25);
    return 1;
}

int test_codegen_matrix_multiplication() {
    const char *src = 
        "fn main() -> i32 {\n"
        "  a: i32[2][2] = {{1, 2}, {3, 4}};\n"
        "  b: i32[2][2] = {{5, 6}, {7, 8}};\n"
        "  res: i32[2][2] = {{0, 0}, {0, 0}};\n"
        "  for(i: i32 = 0; i < 2; i++) {\n"
        "    for(j: i32 = 0; j < 2; j++) {\n"
        "      for(k: i32 = 0; k < 2; k++) {\n"
        "        res[i][j] += a[i][k] * b[k][j];\n"
        "      }\n"
        "    }\n"
        "  }\n"
        "  return res[0][0] + res[0][1] + res[1][0] + res[1][1];\n" // 19 + 22 + 43 + 50 = 134
        "}";
    int code = run_compiled_code(src);
    ASSERT_EQ_INT(code, 134);
    return 1;
}

int test_codegen_break_continue() {
    const char *src = 
        "fn main() -> i32 {\n"
        "  sum: i32 = 0;\n"
        "  for(i: i32 = 0; i < 100; i++) {\n"
        "    if (i == 11) { break; }\n"
        "    if (i % 2 != 0) { continue; }\n"
        "    sum += i;\n"
        "  }\n"
        "  return sum;\n" // 0 + 2 + 4 + 6 + 8 + 10 = 30
        "}";
    int code = run_compiled_code(src);
    ASSERT_EQ_INT(code, 30);
    return 1;
}

int test_codegen_shadowing() {
    const char *src = 
        "fn main() -> i32 {\n"
        "  x: i32 = 10;\n"
        "  {\n"
        "    x: i32 = 20;\n"
        "    if (x != 20) { return 0; }\n"
        "  }\n"
        "  return x;\n" // 10
        "}";
    int code = run_compiled_code(src);
    ASSERT_EQ_INT(code, 10);
    return 1;
}

int test_codegen_type_promotion() {
    const char *src = 
        "fn main() -> i32 {\n"
        "  a: i32 = 100;\n"
        "  b: i64 = 200;\n"
        "  c: i64 = a + b;\n" // i32 promoted to i64
        "  if (c == 300) { return 1; }\n"
        "  return 0;\n"
        "}";
    int code = run_compiled_code(src);
    ASSERT_EQ_INT(code, 1);
    return 1;
}

int test_codegen_large_array() {
    const char *src = 
        "fn main() -> i32 {\n"
        "  arr: i32[100];\n"
        "  for(i: i32 = 0; i < 100; i++) { arr[i] = i; }\n"
        "  sum: i32 = 0;\n"
        "  for(i: i32 = 0; i < 10; i++) { sum += arr[i]; }\n"
        "  return sum;\n" // 0+1+2+3+4+5+6+7+8+9 = 45
        "}";
    int code = run_compiled_code(src);
    ASSERT_EQ_INT(code, 45);
    return 1;
}

int test_codegen_strings() {
    const char *src = 
        "fn print_str(s: str);\n"
        "fn main() -> i32 {\n"
        "  s: str = \"Hello\";\n"
        "  print_str(s);\n"
        "  return 42;\n"
        "}";
    int code = run_compiled_code(src);
    ASSERT_EQ_INT(code, 42);
    return 1;
}

int test_codegen_slice_len_write() {
    const char *src = 
        "fn main() -> i32 {\n"
        "  arr: i32[10];\n"
        "  slice: i32[] = arr;\n"
        "  return slice.len;\n"
        "}";
    int code = run_compiled_code(src);
    ASSERT_EQ_INT(code, 10);
    return 1;
}

int test_codegen_fixed_array_len() {
    const char *src = 
        "fn main() -> i32 {\n"
        "  arr: i32[15];\n"
        "  return arr.len;\n"
        "}";
    int code = run_compiled_code(src);
    ASSERT_EQ_INT(code, 15);
    return 1;
}

int test_codegen_multi_array_len() {
    const char *src = 
        "fn main() -> i32 {\n"
        "  arr: i32[3][4];\n"
        "  // arr.len should be 3\n"
        "  // arr[0].len should be 4\n"
        "  return arr.len + arr[0].len;\n"
        "}";
    int code = run_compiled_code(src);
    ASSERT_EQ_INT(code, 7);
    return 1;
}

int test_codegen_multi_array_slice_func() {
    const char *src = 
        "fn sum_slice(s: i32[][]) -> i32 {\n"
        "  sum: i32 = 0;\n"
        "  for(i: i32 = 0; i < s.len; i++) {\n"
        "    for(j: i32 = 0; j < s[i].len; j++) {\n"
        "      sum += s[i][j];\n"
        "    }\n"
        "  }\n"
        "  return sum;\n"
        "}\n"
        "fn main() -> i32 {\n"
        "  arr: i32[2][2] = {{1, 2}, {3, 4}};\n"
        "  return sum_slice(arr);\n"
        "}";
    int code = run_compiled_code(src);
    ASSERT_EQ_INT(code, 10);
    return 1;
}

int test_codegen_structs() {
    const char *src =
        "struct Point { x: i32; y: i32; }\n"
        "fn main() -> i32 {\n"
        "    p: Point = Point { x: 10, y: 20 };\n"
        "    return p.x + p.y;\n"
        "}\n";
    int res = run_compiled_code(src);
    ASSERT_EQ_INT(res, 30);
    return 1;
}

int test_codegen_struct_edge_cases() {
    // Test 1: Nested structs, arrays in structs, and pointers to structs
    const char *src1 =
        "struct Inner {\n"
        "    val: i32;\n"
        "}\n"
        "struct Outer {\n"
        "    arr: i32[3];\n"
        "    inner: Inner;\n"
        "    ptr: *Inner;\n"
        "}\n"
        "fn main() -> i32 {\n"
        "    i: Inner = Inner { val: 42 };\n"
        "    o: Outer = Outer {\n"
        "        arr:  {1, 2, 3},\n"
        "        inner: Inner { val: 100 },\n"
        "        ptr: &i\n"
        "    };\n"
        "    // Modify through pointer\n"
        "    (*o.ptr).val = 50;\n"
        "    return o.arr[1] + o.inner.val + i.val;\n"
        "}\n";
    int res1 = run_compiled_code(src1);
    ASSERT_EQ_INT(res1, 152); // 2 + 100 + 50 = 152
    
    // Test 2: Implicit casting in struct initialization and assignment
    const char *src2 =
        "struct Mixed {\n"
        "    f: f64;\n"
        "    i: i64;\n"
        "}\n"
        "fn main() -> i32 {\n"
        "    // Initialize with i32, should promote to f64 and i64\n"
        "    x: i32 = 10;\n"
        "    m: Mixed = Mixed { f: 5, i: x };\n"
        "    m.f = m.f + 1.5;\n"
        "    m.i = m.i * 2;\n"
        "    if (m.f > 6.0) {\n"
        "        return m.i; // Should return 20\n"
        "    }\n"
        "    return 0;\n"
        "}\n";
    int res2 = run_compiled_code(src2);
    ASSERT_EQ_INT(res2, 20);
    
    // Test 3: Arrays of structs
    const char *src3 = 
        "struct Pair { a: i32; b: i32; }\n"
        "fn main() -> i32 {\n"
        "    arr: Pair[2] = { Pair{a: 1, b: 2}, Pair{a: 3, b: 4} };\n"
        "    arr[1].a = 10;\n"
        "    return arr[0].b + arr[1].a;\n"
        "}\n";
    int res3 = run_compiled_code(src3);
    ASSERT_EQ_INT(res3, 12); // 2 + 10 = 12

    return 1;
}
