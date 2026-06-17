// test/cases/sema_slices.inc

// --- VALID SLICE USAGE ---

SEMA_VALID("slice_assignment", "fn main() { arr: i32[3] = {1,2,3}; s: i32[] = arr; }")

SEMA_VALID("slice_pass_to_fn", "fn f(s: i32[]) {} fn main() { a: i32[2] = {1,2}; f(a); }")

SEMA_VALID("slice_of_slices", "fn main() { s: i32[][]; }")

SEMA_VALID("alloc_slice_context", "import std; fn main() { s: i32[] = @alloc(i32, std.heap.allocator, 10); }")

SEMA_VALID("free_slice", "import std; fn main() { s: i32[] = @alloc(i32, std.heap.allocator, 10); @free(std.heap.allocator, s); }")

// --- SLICE ERRORS ---

SEMA_ERROR("slice_ptr_mismatch", "fn main() { x: i32 = 10; s: i32[] = &x; }", TE_TYPE_MISMATCH)

SEMA_ERROR("slice_init_scalar", "fn main() { s: i32[] = 10; }", TE_TYPE_MISMATCH)

SEMA_ERROR("slice_element_mismatch", "fn main() { a: f32[2]; s: i32[] = a; }", TE_TYPE_MISMATCH)

SEMA_ERROR("slice_rank_mismatch", "fn main() { a: i32[2][2]; s: i32[] = a; }", TE_TYPE_MISMATCH)

SEMA_ERROR("len_on_ptr", "fn main() { x: i32 = 0; p: *i32 = &x; l: i64 = p.len; }", TE_FIELD_ACCESS)
