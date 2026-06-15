// test/cases/sema_casts.inc

// Implicit Valid
SEMA_VALID("implicit_i32_i64", "fn main() { a: i32 = 10; b: i64 = a; }")
SEMA_VALID("implicit_f32_f64", "fn main() { a: f32 = 1.0; b: f64 = a; }")
SEMA_VALID("implicit_i32_f64", "fn main() { a: i32 = 10; b: f64 = a; }")
SEMA_VALID("implicit_array_decay", "fn f(s: i32[]) {} fn main() { arr: i32[3] = {1,2,3}; f(arr); }")
SEMA_VALID("implicit_ptr_void", "fn f(p: *void) {} fn main() { x: i32 = 10; f(&x); }")

// Implicit Error (Rust-strict)
SEMA_ERROR("err_i64_i32", "fn main() { a: i64 = 10; b: i32 = a; }", TE_TYPE_MISMATCH)
SEMA_ERROR("err_f64_f32", "fn main() { a: f64 = 1.0; b: f32 = a; }", TE_TYPE_MISMATCH)
SEMA_ERROR("err_i64_f64", "fn main() { a: i64 = 10; b: f64 = a; }", TE_TYPE_MISMATCH)
SEMA_ERROR("err_i32_f32", "fn main() { a: i32 = 10; b: f32 = a; }", TE_TYPE_MISMATCH)
SEMA_ERROR("err_void_ptr_T", "fn main() { x: i32 = 0; p: *void = &x; p2: *i32 = p; }", TE_TYPE_MISMATCH)
SEMA_ERROR("err_int_ptr", "fn main() { p: *i32 = 123; }", TE_TYPE_MISMATCH)
SEMA_ERROR("err_i32_ptr_cast", "fn main() { a: i32 = 0; p: *i32 = a as *i32; }", TE_TYPE_MISMATCH)

// Explicit Valid
SEMA_VALID("explicit_narrow", "fn main() { a: i64 = 10; b: i32 = a as i32; }")
SEMA_VALID("explicit_f_narrow", "fn main() { a: f64 = 1.0; b: f32 = a as f32; }")
SEMA_VALID("explicit_ptr_int", "fn main() { x: i32 = 0; addr: i64 = &x as i64; }")
SEMA_VALID("explicit_int_ptr", "fn main() { p: *i32 = 0 as *i32; }")
SEMA_VALID("explicit_ptr_ptr", "fn main() { x: i32 = 0; p: *f32 = &x as *f32; }")
SEMA_VALID("explicit_float_int", "fn main() { a: f64 = 3.14; b: i32 = a as i32; }")
