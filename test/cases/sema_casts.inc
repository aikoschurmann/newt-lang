// test/cases/sema_casts.inc

// --- Implicit Valid (Lossless Widening) ---

SEMA_VALID("implicit_i8_i16", "fn main() { a: i8 = 10; b: i16 = a; }")
SEMA_VALID("implicit_u8_i16", "fn main() { a: u8 = 10; b: i16 = a; }")
SEMA_VALID("implicit_u16_i32", "fn main() { a: u16 = 10; b: i32 = a; }")
SEMA_VALID("implicit_u32_i64", "fn main() { a: u32 = 10; b: i64 = a; }")
SEMA_VALID("implicit_i32_i64", "fn main() { a: i32 = 10; b: i64 = a; }")

SEMA_VALID("implicit_u8_u64", "fn main() { a: u8 = 10; b: u64 = a; }")
SEMA_VALID("implicit_i16_f32", "fn main() { a: i16 = 10; b: f32 = a; }")
SEMA_VALID("implicit_u32_f64", "fn main() { a: u32 = 10; b: f64 = a; }")

SEMA_VALID("implicit_f32_f64", "fn main() { a: f32 = 1.0; b: f64 = a; }")
SEMA_VALID("implicit_array_decay", "fn f(s: i32[]) {} fn main() { arr: i32[3] = {1,2,3}; f(arr); }")
SEMA_VALID("implicit_ptr_void", "fn f(p: *void) {} fn main() { x: i32 = 10; f(&x); }")

// --- Implicit Error (Narrowing or Unsafe Signedness) ---

SEMA_ERROR("err_i16_i8", "fn main() { a: i16 = 10; b: i8 = a; }", TE_TYPE_MISMATCH)
SEMA_ERROR("err_u8_i8", "fn main() { a: u8 = 10; b: i8 = a; }", TE_TYPE_MISMATCH)
SEMA_ERROR("err_i8_u8", "fn main() { a: i8 = 10; b: u8 = a; }", TE_TYPE_MISMATCH)
SEMA_ERROR("err_u32_i32", "fn main() { a: u32 = 10; b: i32 = a; }", TE_TYPE_MISMATCH)

SEMA_ERROR("err_i64_f64", "fn main() { a: i64 = 10; b: f64 = a; }", TE_TYPE_MISMATCH)
SEMA_ERROR("err_i32_f32", "fn main() { a: i32 = 10; b: f32 = a; }", TE_TYPE_MISMATCH)

// --- Explicit Valid (Casting) ---

SEMA_VALID("explicit_u8_i8", "fn main() { a: u8 = 255; b: i8 = a as i8; }")
SEMA_VALID("explicit_i32_u8", "fn main() { a: i32 = 258; b: u8 = a as u8; }")
SEMA_VALID("explicit_f64_i32", "fn main() { a: f64 = 3.14; b: i32 = a as i32; }")
SEMA_VALID("explicit_ptr_u64", "fn main() { x: i32 = 0; addr: u64 = &x as u64; }")
