// test/cases/sema_basics.inc

SEMA_VALID("i32_init", "fn main() { x: i32 = 10; }")
SEMA_VALID("i64_init", "fn main() { x: i64 = 10000000000; }")
SEMA_VALID("bool_init", "fn main() { x: bool = true; }")
SEMA_VALID("f32_init", "fn main() { x: f32 = 3.14; }")
SEMA_VALID("f64_init", "fn main() { x: f64 = 3.14159265; }")
SEMA_VALID("ptr_void", "fn main() { x: i32 = 0; p: *void = &x; }")
SEMA_VALID("global_const_order", "const A: i32 = B + 1; const B: i32 = 10; fn main() { x: i32 = A; }")

SEMA_ERROR("mismatch_init", "fn main() { x: i32 = \"hi\"; }", TE_TYPE_MISMATCH)
SEMA_ERROR("undeclared", "fn main() { x = 10; }", TE_UNDECLARED)
SEMA_ERROR("redeclared", "fn main() { x: i32 = 1; x: i32 = 2; }", TE_REDECLARATION)
SEMA_ERROR("binop_mismatch", "fn main() { a: i32 = 1; b: i64 = 2; c: i64 = a + b; }", TE_BINOP_MISMATCH)
SEMA_ERROR("const_cycle", "const A: i32 = B; const B: i32 = A; fn main() { x: i32 = A; }", TE_RECURSIVE_CONST)
SEMA_ERROR("self_init", "fn main() { x: i32 = x; }", TE_UNDECLARED)
SEMA_ERROR("ptr_arith", "fn main() { x: i32 = 0; p: *i32 = &x; p2: *i32 = p + 1; }", TE_BINOP_MISMATCH)
