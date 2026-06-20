// test/cases/sema_overload.inc

// --- VALID OVERLOADING ---

SEMA_VALID("overload_arg_count", "fn f() -> i32 { return 1; } fn f(a: i64) -> i32 { return 2; } fn main() { x: i32 = f(); y: i32 = f(10); }")

SEMA_VALID("overload_arg_type", "fn f(a: i64) -> i32 { return 1; } fn f(a: f64) -> i32 { return 2; } fn main() { x: i32 = f(10); y: i32 = f(3.14); }")

SEMA_VALID("overload_ptr_void", "fn f(p: *void) -> i32 { return 1; } fn f(p: *i32) -> i32 { return 2; } fn main() { x: i32 = 0; f(&x); }")

SEMA_VALID("overload_array_slice_match", "fn f(s: i32[]) -> i32 { return 1; } fn f(i: i32) -> i32 { return 2; } fn main() { a: i32[3] = {1,2,3}; f(a); }")

SEMA_VALID("method_overload", "struct S { x: i32; } impl S { fn foo(self: *S) -> i32 { return 1; } fn foo(self: *S, a: i64) -> i32 { return 2; } } fn main() { s: S; s.foo(); s.foo(10); }")

SEMA_VALID("method_overload_static", "struct S { x: i32; } impl S { fn bar() -> i32 { return 1; } fn bar(a: i64) -> i32 { return 2; } } fn main() { S.bar(); S.bar(10); }")

// --- DOMINATION ---

SEMA_VALID("domination_exact_vs_cast", "fn f(a: i32) -> i32 { return 1; } fn f(a: i64) -> i32 { return 2; } fn main() { x: i32 = f(10 as i32); }")

SEMA_VALID("domination_multi_param", "fn f(a: i32, b: i64) -> i32 { return 1; } fn f(a: i64, b: i64) -> i32 { return 2; } fn main() { x: i32 = f(10 as i32, 20); }")

SEMA_VALID("domination_mixed", "fn f(a: i32, b: i32) -> i32 { return 1; } fn f(a: i64, b: i32) -> i32 { return 2; } fn main() { x: i32 = f(1 as i32, 2 as i32); }")

// --- AMBIGUITY ---

SEMA_ERROR("ambiguity_cross_cast", "fn f(a: i32, b: i64) -> i32 { return 1; } fn f(a: i64, b: i32) -> i32 { return 2; } fn main() { x: i32 = f(1 as i32, 2 as i32); }", TE_AMBIGUOUS_OVERLOAD)

SEMA_ERROR("ambiguity_identical_scores", "fn f(a: i64) -> i32 { return 1; } fn f(a: f64) -> i32 { return 2; } fn main() { x: i32 = f(10 as i32); }", TE_AMBIGUOUS_OVERLOAD)

SEMA_ERROR("ambiguity_auto_ref", "struct S { x: i32; } impl S { fn foo(self: S) {} fn foo(self: *S) {} } fn main() { s: S; s.foo(); }", TE_AMBIGUOUS_OVERLOAD)

SEMA_ERROR("ambiguity_return_type", "fn f() -> i32 { return 1; } fn f() -> f64 { return 1.0; } fn main() { x: i32 = f(); }", TE_AMBIGUOUS_OVERLOAD)

// --- NO MATCHING OVERLOAD ---

SEMA_ERROR("no_match_null_pointers", "fn f(p: *i32) {} fn f(p: *f64) {} fn main() { f(null); }", TE_NO_MATCHING_OVERLOAD)

SEMA_ERROR("no_match_multiple", "fn f(a: i32) {} fn f(a: f32) {} fn main() { f(10 as i64); }", TE_NO_MATCHING_OVERLOAD)

SEMA_ERROR("no_matching_overload_count", "fn f(a: i32) {} fn f(a: i32, b: i32) {} fn main() { f(1, 2, 3); }", TE_NO_MATCHING_OVERLOAD)

SEMA_ERROR("no_matching_overload_type", "fn f(a: i32) {} fn f(a: f32) {} fn main() { f(\"hi\"); }", TE_NO_MATCHING_OVERLOAD)

// --- SIMPLE ERRORS ---

SEMA_ERROR("mismatch_single_fn", "fn f(a: i32) {} fn main() { f(10 as i64); }", TE_TYPE_MISMATCH)
