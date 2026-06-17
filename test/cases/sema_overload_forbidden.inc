SEMA_ERROR("overload_main_forbidden", "fn main() -> i32 { return 0; } fn main(x: i32) -> i32 { return x; }", TE_REDECLARATION)
SEMA_ERROR("overload_link_forbidden", "@link(\"malloc\") fn malloc(size: i64) -> *void; @link(\"malloc\") fn malloc(size: i64, extra: i32) -> *void; fn main() -> i32 { return 0; }", TE_REDECLARATION)
