// test/cases/sema_ops.inc

// Logical
SEMA_VALID("logic_valid", "fn main() { a: bool = true && false; b: bool = true || false; c: bool = !true; }")
SEMA_ERROR("logic_int", "fn main() { a: bool = 1 && 0; }", TE_BINOP_MISMATCH)

// Comparisons
SEMA_VALID("comp_valid", "fn main() { a: bool = 1 < 2; b: bool = 1.0 > 0.5; c: bool = 'a' == 'b'; d: bool = 10 <= 20; e: bool = 100 >= 50; f: bool = 1 != 2; }")
SEMA_ERROR("comp_mismatch", "fn main() { x: i32 = 1; y: f32 = 1.0; a: bool = x < y; }", TE_BINOP_MISMATCH)
SEMA_ERROR("comp_struct", "struct S { x: i32; } fn main() { s1: S; s2: S; a: bool = s1 == s2; }", TE_BINOP_MISMATCH)

// Array/Slice Length
SEMA_VALID("array_len", "fn main() { arr: i32[10]; x: usize = arr.len; }")
SEMA_VALID("slice_len", "fn f(s: i32[]) { x: usize = s.len; } fn main() {}")
SEMA_ERROR("len_on_int", "fn main() { x: i32 = 10; y: usize = x.len; }", TE_FIELD_ACCESS)
