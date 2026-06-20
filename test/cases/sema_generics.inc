// test/cases/sema_generics.inc

SEMA_VALID("generic_fn_basic", "fn id<T>(x: T) -> T { return x; } fn main() { a: i32 = id<i32>(42); }")
SEMA_VALID("generic_fn_multiple", "fn pair<T, U>(x: T, y: U) -> T { return x; } fn main() { a: f64 = pair<f64, i32>(3.14, 42); }")
SEMA_VALID("generic_fn_infer", "fn id<T>(x: T) -> T { return x; } fn main() { a: i32 = id(42); }")
SEMA_ERROR("generic_fn_arg_mismatch", "fn id<T>(x: T) -> T { return x; } fn main() { a: i32 = id<f64>(42); }", TE_TYPE_MISMATCH)
SEMA_ERROR("generic_fn_return_mismatch", "fn id<T>(x: T) -> T { return x; } fn main() { a: *i32 = id<i32>(42); }", TE_TYPE_MISMATCH)
SEMA_VALID("generic_fn_nested_call", "fn id<T>(x: T) -> T { return x; } fn main() { a: i32 = id<i32>(id<i32>(42)); }")

SEMA_VALID("generic_struct_basic", "struct Box<T> { val: T; } fn main() { b: Box<i32>; b.val = 10; }")
SEMA_ERROR("generic_struct_missing_args", "struct Box<T> { val: T; } fn main() { b: Box; }", TE_MISSING_TYPE_ARGS)
SEMA_ERROR("generic_struct_field_mismatch", "struct Box<T> { val: T; } fn main() { b: Box<i32>; b.val = 3.14; }", TE_TYPE_MISMATCH)

SEMA_VALID("generic_alias_basic", "struct Box<T> { val: T; } alias IntBox = Box<i32>; fn main() { b: IntBox; b.val = 10; }")
SEMA_ERROR("generic_alias_mismatch", "struct Box<T> { val: T; } alias IntBox = Box<i32>; fn main() { b: IntBox; b.val = 3.14; }", TE_TYPE_MISMATCH)

SEMA_ERROR("generic_fn_too_many_args", "fn id<T>(x: T) -> T { return x; } fn main() { a: i32 = id<i32, f64>(42); }", TE_GENERIC_ARG_MISMATCH)
SEMA_ERROR("generic_fn_too_few_args", "fn pair<T, U>(x: T, y: U) -> T { return x; } fn main() { a: f64 = pair<f64>(3.14, 42); }", TE_GENERIC_ARG_MISMATCH)
SEMA_ERROR("generic_struct_too_many_args", "struct Box<T> { val: T; } fn main() { b: Box<i32, f64>; }", TE_GENERIC_ARG_MISMATCH)
SEMA_ERROR("generic_struct_too_few_args", "struct Pair<T, U> { x: T; y: U; } fn main() { p: Pair<i32>; }", TE_GENERIC_ARG_MISMATCH)
SEMA_ERROR("generic_fn_no_args_expected", "fn plain(x: i32) -> i32 { return x; } fn main() { a: i32 = plain<i32>(42); }", TE_NOT_GENERIC)
SEMA_ERROR("generic_struct_no_args_expected", "struct Plain { x: i32; } fn main() { p: Plain<i32>; }", TE_NOT_GENERIC)

SEMA_VALID("generic_struct_recursive", "struct Node<T> { val: T; next: *Node<T>; } fn main() { n: Node<i32>; }")
SEMA_ERROR("generic_struct_recursive_mismatch", "struct Node<T> { val: T; next: *Node<T, f64>; } fn main() { n: Node<i32>; }", TE_GENERIC_ARG_MISMATCH)

SEMA_VALID("generic_impl_basic", "struct Box<T> { val: T; } impl<T> Box<T> { fn get(self: *Box<T>) -> T { return self.val; } } fn main() { b: Box<i32>; b.val = 10; a: i32 = b.get(); }")
SEMA_ERROR("generic_impl_missing_args", "struct Box<T> { val: T; } impl Box { fn get(self: *Box<T>) -> T { return self.val; } } fn main() {}", TE_GENERIC_ARG_MISMATCH)
SEMA_ERROR("generic_impl_too_many_args", "struct Box<T> { val: T; } impl<T> Box<T, i32> { fn get(self: *Box<T>) -> T { return self.val; } } fn main() {}", TE_GENERIC_ARG_MISMATCH)
SEMA_ERROR("generic_impl_method_arg_mismatch", "struct Box<T> { val: T; } impl<T> Box<T> { fn set(self: *Box<T>, v: T) -> void { self.val = v; } } fn main() { b: Box<i32>; b.set(3.14); }", TE_TYPE_MISMATCH)

SEMA_VALID("generic_fn_infer_complex", "fn id<T>(x: T[]) -> T[] { return x; } fn main() { a: i32[] = id({1, 2, 3}); }")
SEMA_VALID("generic_fn_infer_args", "fn first<T>(a: T, b: T) -> T { return a; } fn main() { a: i32 = first(42, 10); }")
SEMA_ERROR("generic_fn_infer_conflict", "fn first<T>(a: T, b: T) -> T { return a; } fn main() { a: i32 = first(42, 3.14); }", TE_MISSING_TYPE_ARGS)
SEMA_ERROR("generic_fn_infer_conflict_no_return", "fn first<T>(a: T, b: T) -> T { return a; } fn main() { first(42, 3.14); }", TE_MISSING_TYPE_ARGS)
SEMA_VALID("generic_fn_infer_pointer", "fn id_ptr<T>(x: *T) -> *T { return x; } fn main() { v: i32 = 42; p: *i32 = id_ptr(&v); }")
SEMA_VALID("generic_method_infer", "struct Vec<T> { data: T[]; } impl<T> Vec<T> { fn push(self: *Vec<T>, val: T) {} } fn main() { v: Vec<i32>; v.push(42); }")
