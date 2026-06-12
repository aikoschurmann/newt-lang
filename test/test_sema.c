#include "test_utils.h"
#include "test_harness.h"
#include "parsing/ast.h"
#include "sema/type.h"

// -----------------------------------------------------------------------------
// Helpers
// -----------------------------------------------------------------------------

static bool check_sema_error(const char *src, TypeErrorKind kind) {
    CompileResult res = compile_source((char*)src);
    bool found = false;
    if (res.ctx.errors) {
        for (size_t i = 0; i < res.ctx.errors->count; i++) {
            TypeError *err = (TypeError*)dynarray_get(res.ctx.errors, i);
            if (err->kind == kind) {
                found = true;
                break;
            }
        }
    }
    cleanup_compilation(&res);
    return found;
}

static bool check_sema_valid(const char *src) {
    CompileResult res = compile_source((char*)src);
    bool valid = (res.ctx.errors->count == 0);
    if (!valid) {
        TypeError *err = (TypeError*)dynarray_get(res.ctx.errors, 0);
        fprintf(stderr, "\n[Sema Valid Check Failed] Unexpected error: %d at src: %s\n", err->kind, src);
    }
    cleanup_compilation(&res);
    return valid;
}

// -----------------------------------------------------------------------------
// 1. Implicit Conversions (Lossless Widening Only)
// -----------------------------------------------------------------------------

int test_sema_implicit_widening() {
    // Valid: i32 -> i64
    ASSERT(check_sema_valid("fn main() { a: i32 = 10; b: i64 = a; }"));
    // Valid: f32 -> f64
    ASSERT(check_sema_valid("fn main() { a: f32 = 1.0; b: f64 = a; }"));
    // Valid: i32 -> f64 (lossless)
    ASSERT(check_sema_valid("fn main() { a: i32 = 10; b: f64 = a; }"));

    // Invalid: i64 -> f64 (potential loss)
    ASSERT(check_sema_error("fn main() { a: i64 = 10; b: f64 = a; }", TE_TYPE_MISMATCH));
    // Invalid: i32 -> f32 (potential loss)
    ASSERT(check_sema_error("fn main() { a: i32 = 10; b: f32 = a; }", TE_TYPE_MISMATCH));
    // Invalid: narrowing i64 -> i32
    ASSERT(check_sema_error("fn main() { a: i64 = 10; b: i32 = a; }", TE_TYPE_MISMATCH));
    
    return 1;
}

// -----------------------------------------------------------------------------
// 2. Literal Inference
// -----------------------------------------------------------------------------

int test_sema_literal_inference() {
    // Untyped literal '5' adapts to i64
    ASSERT(check_sema_valid("fn main() { x: i64 = 5; }"));
    // Untyped literal '1.0' adapts to f32
    ASSERT(check_sema_valid("fn main() { x: f32 = 1.0; }"));
    // Untyped integer literal '1' adapts to f32
    ASSERT(check_sema_valid("fn main() { x: f32 = 1; }"));
    
    // Mixed arithmetic with literal (1 adapts to f32)
    ASSERT(check_sema_valid("fn main() { a: f32 = 1.0; b: f32 = a + 1; }"));
    
    return 1;
}

// -----------------------------------------------------------------------------
// 3. Strict Binary Operators
// -----------------------------------------------------------------------------

int test_sema_strict_binary_ops() {
    // Valid: Same types
    ASSERT(check_sema_valid("fn main() { a: i32 = 1; b: i32 = 2; c: i32 = a + b; }"));
    
    // Invalid: i32 + i64 (No implicit promotion in binary ops)
    ASSERT(check_sema_error("fn main() { a: i32 = 1; b: i64 = 2; c: i64 = a + b; }", TE_BINOP_MISMATCH));
    
    // Invalid: f32 + f64
    ASSERT(check_sema_error("fn main() { a: f32 = 1.0; b: f64 = 2.0; c: f64 = a + b; }", TE_BINOP_MISMATCH));
    
    // Invalid: Comparison between mixed types
    ASSERT(check_sema_error("fn main() { a: i32 = 1; b: i64 = 1; if (a == b) {} }", TE_BINOP_MISMATCH));

    return 1;
}

// -----------------------------------------------------------------------------
// 4. 'as' Casting Rules
// -----------------------------------------------------------------------------

int test_sema_as_casting() {
    // Valid: Numeric conversions
    ASSERT(check_sema_valid("fn main() { a: i64 = 10; b: i32 = a as i32; }"));
    ASSERT(check_sema_valid("fn main() { a: f64 = 1.0; b: f32 = a as f32; }"));
    ASSERT(check_sema_valid("fn main() { a: i32 = 65; b: char = a as char; }"));
    
    // Valid: Bool -> Numeric
    ASSERT(check_sema_valid("fn main() { a: bool = true; b: i32 = a as i32; }"));
    
    // Invalid: Numeric -> Bool (Use explicit comparison)
    ASSERT(check_sema_error("fn main() { a: i32 = 1; b: bool = a as bool; }", TE_TYPE_MISMATCH));
    
    // Invalid: 'str' in 'as'
    ASSERT(check_sema_error("fn main() { a: i32 = 1; b: str = a as str; }", TE_TYPE_MISMATCH));

    return 1;
}

// -----------------------------------------------------------------------------
// 5. Pointer Rules
// -----------------------------------------------------------------------------

int test_sema_pointer_rules() {
    // Valid: T* -> *void (Implicit safe promotion)
    ASSERT(check_sema_valid("fn main() { x: i32 = 0; p: *void = &x; }"));
    
    // Invalid: *void -> T* (Must be explicit)
    ASSERT(check_sema_error("fn main() { x: i32 = 0; p: *void = &x; y: *i32 = p; }", TE_TYPE_MISMATCH));
    
    // Valid: *void -> T* (Explicit as)
    ASSERT(check_sema_valid("fn main() { x: i32 = 0; p: *void = &x; y: *i32 = p as *i32; }"));
    
    // Valid: Pointer <-> Integer (Reinterpretation)
    ASSERT(check_sema_valid("fn main() { x: i32 = 0; p: *i32 = &x; val: i64 = p as i64; }"));
    ASSERT(check_sema_valid("fn main() { val: i64 = 12345; p: *void = val as *void; }"));

    return 1;
}

// -----------------------------------------------------------------------------
// 6. Array & Slice Rules
// -----------------------------------------------------------------------------

int test_sema_array_slice_rules() {
    // Valid: T[N] -> T[] (Decay with identical base)
    ASSERT(check_sema_valid("fn main() { arr: i32[5]; slice: i32[] = arr; }"));
    
    // Invalid: T[N] -> U[] (Even if T casts to U)
    ASSERT(check_sema_error("fn main() { arr: i32[5]; slice: i64[] = arr; }", TE_TYPE_MISMATCH));
    
    // Invalid: T[] -> T[N] (Cannot prove size)
    ASSERT(check_sema_error("fn take(s: i32[]) { arr: i32[5] = s; }", TE_TYPE_MISMATCH));

    return 1;
}

// -----------------------------------------------------------------------------
// Miscellaneous / Legacy Tests
// -----------------------------------------------------------------------------

// -----------------------------------------------------------------------------
// 7. Scoping & Shadowing
// -----------------------------------------------------------------------------

int test_sema_shadowing() {
    // 1. Shadowing a type name
    const char *src1 = 
        "struct S { x: i32; }\n"
        "fn main() {\n"
        "    S: i32 = 10;\n"
        "    val: S = S{x: 1};\n" // Error: S is now a variable, not a type
        "}";
    ASSERT(check_sema_error(src1, TE_UNKNOWN_TYPE));

    // 2. Shadowing a function name
    const char *src2 = 
        "fn f() {}\n"
        "fn main() {\n"
        "    f: i32 = 10;\n"
        "    f();\n" // Error: f is now an i32, not callable
        "}";
    ASSERT(check_sema_error(src2, TE_NOT_CALLABLE));

    return 1;
}

int test_sema_arg_mismatch_regression() {
    const char *src = "fn f(a: i32) {} fn main() { f(); }";
    ASSERT(check_sema_error(src, TE_ARG_COUNT_MISMATCH));
    return 1;
}

int test_sema_undeclared() {
    const char *src = "fn main() { x = 1; }";
    ASSERT(check_sema_error(src, TE_UNDECLARED));
    return 1;
}

int test_sema_struct_basic() {
    const char *src = "struct S { x: i32; } fn main() { s: S = S{x: 10}; val: i32 = s.x; }";
    ASSERT(check_sema_valid(src));
    return 1;
}

int test_sema_const_folding() {
    const char *src = "const x: i32 = 1 + 2 * 3;";
    CompileResult res = compile_source(src);
    ASSERT(res.ctx.errors->count == 0);
    AstNode *decl = *(AstNode**)dynarray_get(res.program->data.program.decls, 0);
    ASSERT(decl->data.variable_declaration.initializer->is_const_expr);
    ASSERT_EQ_INT(decl->data.variable_declaration.initializer->const_value.value.int_val, 7);
    cleanup_compilation(&res);
    return 1;
}
