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

    CodegenContext *cg_ctx = codegen_context_create(res.program, res.store, "test_module", 0);
    if (codegen_program(cg_ctx) != 0) {
        codegen_context_destroy(cg_ctx);
        cleanup_compilation(&res);
        return -101; // Codegen failed
    }

    int exit_code = codegen_run_jit(cg_ctx);

    codegen_context_destroy(cg_ctx);
    cleanup_compilation(&res);

    return exit_code;
}

// -----------------------------------------------------------------------------
// 1. Core Arithmetic & Strict Typing
// -----------------------------------------------------------------------------

int test_codegen_basic_arithmetic() {
    ASSERT_EQ_INT(run_compiled_code("fn main() -> i32 { return (10 + 20) * 2 / 5; }"), 12);
    return 1;
}

int test_codegen_float_math() {
    const char *src = 
        "fn main() -> i32 {\n"
        "    f: f64 = 10.5;\n"
        "    g: f64 = 2.0;\n"
        "    res: f64 = f * g + 0.5;\n" 
        "    return res as i32;\n"
        "}";
    ASSERT_EQ_INT(run_compiled_code(src), 21);
    return 1;
}

// -----------------------------------------------------------------------------
// 2. Control Flow & Edge Cases
// -----------------------------------------------------------------------------

int test_codegen_complex_loop() {
    const char *src = 
        "fn main() -> i32 {\n"
        "    sum: i32 = 0;\n"
        "    for (i: i32 = 0; i < 20; i = i + 1) {\n"
        "        if (i % 2 != 0) { continue; }\n"
        "        if (i > 10) { break; }\n"
        "        sum = sum + i;\n"
    "    }\n"
    "    return sum;\n"
    "}";
    ASSERT_EQ_INT(run_compiled_code(src), 30);
    return 1;
}

int test_codegen_nested_control_flow() {
    const char *src = 
        "fn main() -> i32 {\n"
        "    res: i32 = 0;\n"
        "    for (i: i32 = 0; i < 5; i = i + 1) {\n"
        "        for (j: i32 = 0; j < 5; j = j + 1) {\n"
        "            if (i == j) { res = res + 1; }\n"
        "            else if (i > j) { res = res + 2; }\n"
        "        }\n"
    "    }\n"
    "    return res;\n" 
    "}";
    ASSERT_EQ_INT(run_compiled_code(src), 25);
    return 1;
}

int test_codegen_recursion_edge() {
    const char *src = 
        "fn fact(n: i32) -> i32 {\n"
        "    if (n <= 1) { return 1; }\n"
        "    return n * fact(n - 1);\n"
        "}\n"
        "fn main() -> i32 { return fact(5); }"; 
    ASSERT_EQ_INT(run_compiled_code(src), 120);
    return 1;
}

// -----------------------------------------------------------------------------
// 3. Array & Slice Exhaustion
// -----------------------------------------------------------------------------

int test_codegen_multi_dim_arrays() {
    const char *src = 
        "fn main() -> i32 {\n"
        "    arr: i32[2][3] = {{1, 2, 3}, {4, 5, 6}};\n"
        "    return arr[0][0] + arr[1][2];\n"
        "}";
    ASSERT_EQ_INT(run_compiled_code(src), 7);
    return 1;
}

int test_codegen_array_len_casting() {
    const char *src = 
        "fn main() -> i32 {\n"
        "    arr: f64[42];\n"
        "    return arr.len as i32;\n"
        "}";
    ASSERT_EQ_INT(run_compiled_code(src), 42);
    return 1;
}

int test_codegen_slice_decay_func() {
    const char *src = 
        "fn sum_slice(s: i32[]) -> i32 {\n"
        "    sum: i32 = 0;\n"
        "    for (i: i32 = 0; (i as i64) < s.len; i = i + 1) {\n"
        "        sum = sum + s[i];\n"
        "    }\n"
        "    return sum;\n"
        "}\n"
        "fn main() -> i32 {\n"
        "    arr: i32[3] = {10, 20, 30};\n"
        "    return sum_slice(arr);\n"
        "}";
    ASSERT_EQ_INT(run_compiled_code(src), 60);
    return 1;
}

// -----------------------------------------------------------------------------
// 4. Struct & Memory Layout
// -----------------------------------------------------------------------------

int test_codegen_nested_structs() {
    const char *src = 
        "struct Vec2 { x: f64; y: f64; }\n"
        "struct Rect { pos: Vec2; size: Vec2; }\n"
        "fn main() -> i32 {\n"
        "    r: Rect = Rect { \n"
        "        pos: Vec2{x: 10.0, y: 20.0}, \n"
        "        size: Vec2{x: 100.0, y: 200.0} \n"
        "    };\n"
        "    return (r.pos.x + r.size.y) as i32;\n"
        "}";
    ASSERT_EQ_INT(run_compiled_code(src), 210);
    return 1;
}

int test_codegen_struct_pointers() {
    const char *src = 
        "struct Data { a: i32; b: i32; }\n"
        "fn swap_fields(d: *Data) {\n"
        "    tmp: i32 = (*d).a;\n"
        "    (*d).a = (*d).b;\n"
        "    (*d).b = tmp;\n"
        "}\n"
        "fn main() -> i32 {\n"
        "    d: Data = Data { a: 1, b: 2 };\n"
        "    swap_fields(&d);\n"
        "    return d.a * 10 + d.b;\n"
        "}";
    ASSERT_EQ_INT(run_compiled_code(src), 21);
    return 1;
}

// -----------------------------------------------------------------------------
// 5. Casting & Pointer Bitcasting (FFI Escapes)
// -----------------------------------------------------------------------------

int test_codegen_void_ptr_roundtrip() {
    const char *src = 
        "fn main() -> i32 {\n"
        "    val: i32 = 42;\n"
        "    pv: *void = &val;\n"
        "    pi: *i32 = pv as *i32;\n"
        "    return *pi;\n"
        "}";
    ASSERT_EQ_INT(run_compiled_code(src), 42);
    return 1;
}

int test_codegen_ptr_to_int_cast() {
    const char *src = 
        "fn main() -> i32 {\n"
        "    x: i32 = 0;\n"
        "    ptr: *i32 = &x;\n"
        "    addr: i64 = ptr as i64;\n"
        "    if (addr != (0 as i64)) { return 1; }\n"
        "    return 0;\n"
        "}";
    ASSERT_EQ_INT(run_compiled_code(src), 1);
    return 1;
}

int test_codegen_char_math() {
    const char *src = 
        "fn main() -> i32 {\n"
        "    c: char = 'A';\n"
        "    next: char = ((c as i32) + 1) as char;\n"
        "    return next as i32;\n"
        "}";
    ASSERT_EQ_INT(run_compiled_code(src), 66);
    return 1;
}

// -----------------------------------------------------------------------------
// 6. Boolean & Logic
// -----------------------------------------------------------------------------

int test_codegen_boolean_logic() {
    // Test !true
    ASSERT_EQ_INT(run_compiled_code("fn main() -> i32 { if (!true) { return 10; } return 20; }"), 20);
    // Test !false
    ASSERT_EQ_INT(run_compiled_code("fn main() -> i32 { if (!false) { return 30; } return 40; }"), 30);
    
    // Complex logic
    const char *src = 
        "fn main() -> i32 {\n"
        "    a: bool = true;\n"
        "    b: bool = false;\n"
        "    if (!a) { return 10; }\n"
        "    if (a && b) { return 20; }\n"
        "    if (a || b) { return 30; }\n"
        "    return 0;\n"
        "}";
    ASSERT_EQ_INT(run_compiled_code(src), 30);
    return 1;
}

// -----------------------------------------------------------------------------
// 7. Exhaustive Edge Cases
// -----------------------------------------------------------------------------

int test_codegen_huge_stack_array() {
    const char *src = 
        "fn main() -> i32 {\n"
        "    arr: i32[1000];\n"
        "    arr[999] = 123;\n"
        "    return arr[999];\n"
        "}";
    ASSERT_EQ_INT(run_compiled_code(src), 123);
    return 1;
}

int test_codegen_shadowing_scoping() {
    const char *src = 
        "fn main() -> i32 {\n"
        "    x: i32 = 1;\n"
        "    {\n"
        "        x: i32 = 2;\n"
        "        {\n"
            "            x: i32 = 3;\n"
            "            if (x != 3) { return 0; }\n"
            "        }\n"
            "        if (x != 2) { return 0; }\n"
            "    }\n"
        "    return x;\n"
        "}";
    ASSERT_EQ_INT(run_compiled_code(src), 1);
    return 1;
}

int test_codegen_bool_cast_branch() {
    const char *src = 
        "fn main() -> i32 {\n"
        "    t: bool = true;\n"
        "    f: bool = false;\n"
        "    return (t as i32) * 10 + (f as i32) * 5;\n"
        "}";
    ASSERT_EQ_INT(run_compiled_code(src), 10);
    return 1;
}

int test_codegen_struct_value_passing() {
    const char *src = 
        "struct Pair { a: i32; b: i32; }\n"
        "fn sum_pair(p: Pair) -> i32 {\n"
        "    return p.a + p.b;\n"
        "}\n"
        "fn main() -> i32 {\n"
        "    p: Pair = Pair { a: 10, b: 20 };\n"
        "    return sum_pair(p);\n"
        "}";
    ASSERT_EQ_INT(run_compiled_code(src), 30);

    const char *src2 = 
        "struct Triple { x: i64; y: i64; z: i64; }\n"
        "fn make_triple(v: i64) -> Triple {\n"
        "    return Triple { x: v, y: v*2, z: v*3 };\n"
        "}\n"
        "fn main() -> i32 {\n"
        "    t: Triple = make_triple(10);\n"
        "    return (t.x + t.y + t.z) as i32;\n"
        "}";
    ASSERT_EQ_INT(run_compiled_code(src2), 60);
    return 1;
}

int test_codegen_string_indexing() {
    // Return s[0]
    ASSERT_EQ_INT(run_compiled_code("fn main() -> i32 { s: str = \"ABC\"; return s[0] as i32; }"), 65);
    // Return s[1]
    ASSERT_EQ_INT(run_compiled_code("fn main() -> i32 { s: str = \"ABC\"; return s[1] as i32; }"), 66);
    
    const char *src = 
        "fn main() -> i32 {\n"
        "    s: str = \"ABC\";\n"
        "    v1: i32 = s[0] as i32;\n"
        "    v2: i32 = s[1] as i32;\n"
        "    return v1 + v2;\n"
        "}";
    ASSERT_EQ_INT(run_compiled_code(src), 131);
    return 1;
}
