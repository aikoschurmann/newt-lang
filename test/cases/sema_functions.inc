// test/cases/sema_functions.inc

SEMA_ERROR("not_callable", "fn main() { x: i32 = 10; x(); }", TE_NOT_CALLABLE)
SEMA_ERROR("arg_mismatch", "fn f(a: i32) {} fn main() { f(); }", TE_ARG_COUNT_MISMATCH)
SEMA_VALID("hoist_fn", "fn main() { f(); } fn f() {}")
SEMA_VALID("fn_ptr_var", "fn f(x: i32) {} fn main() { ptr: fn(i32) = f; }")
SEMA_VALID("fn_ptr_call", "fn f() -> i32 { return 42; } fn main() { ptr: fn()->i32 = f; x: i32 = ptr(); }")
SEMA_ERROR("fn_ptr_mismatch", "fn f(a: i32) {} fn main() { ptr: fn(i64) = f; }", TE_TYPE_MISMATCH)
SEMA_VALID("fn_ptr_param", "fn exec(p: fn()) { p(); } fn my_fn() {} fn main() { exec(my_fn); }")
SEMA_ERROR("return_mismatch", "fn f() -> i32 { return \"hi\"; } fn main() {}", TE_TYPE_MISMATCH)
