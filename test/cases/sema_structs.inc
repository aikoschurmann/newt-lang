// test/cases/sema_structs.inc

SEMA_VALID("hoist_struct", "fn main() { s: S; } struct S { x: i32; }")
SEMA_ERROR("struct_cycle", "struct A { b: B; } struct B { a: A; }", TE_INCOMPLETE_TYPE)
SEMA_VALID("nested_init", "struct Inner { x: i32; } struct Outer { i: Inner; } fn main() { o: Outer = Outer{ i: Inner{x: 1} }; }")
SEMA_ERROR("init_missing_field", "struct S { x: i32; y: i32; } fn main() { s: S = S{x: 1}; }", TE_ARG_COUNT_MISMATCH)
SEMA_ERROR("init_extra_field", "struct S { x: i32; } fn main() { s: S = S{x: 1, y: 2}; }", TE_ARG_COUNT_MISMATCH)
SEMA_ERROR("field_mismatch", "struct S { x: i32; } fn main() { s: S = S{x: \"hi\"}; }", TE_TYPE_MISMATCH)
SEMA_VALID("empty_struct_sema", "struct E {} fn main() { e: E = E{}; }")
SEMA_VALID("struct_ptr_member", "struct Node { next: *Node; } fn main() { n: Node = Node{next: 0 as *Node}; }")

// Type-Bound Methods
SEMA_VALID("method_static_call", "struct S { x: i32; } impl S { fn new() -> S { return S{x: 10}; } } fn main() { s: S = S.new(); }")
SEMA_VALID("method_instance_ptr_call", "struct S { x: i32; } impl S { fn inc(self: *S) { self.x = self.x + 1; } } fn main() { s: S = S{x: 0}; s.inc(); }")
SEMA_VALID("method_instance_val_call", "struct S { x: i32; } impl S { fn get(self: S) -> i32 { return self.x; } } fn main() { s: S = S{x: 10}; p: *S = &s; val: i32 = p.get(); }")
SEMA_ERROR("method_not_found", "struct S { x: i32; } fn main() { s: S = S{x: 0}; s.missing(); }", TE_FIELD_ACCESS)
SEMA_ERROR("method_on_primitive", "fn main() { x: i32 = 0; x.missing(); }", TE_FIELD_ACCESS)
