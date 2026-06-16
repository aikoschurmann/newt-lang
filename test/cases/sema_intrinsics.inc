// test/cases/sema_intrinsics.inc

SEMA_ERROR("alloc_bad_fields", "struct Allocator { ctx: *void; } fn main() { a: Allocator; p: *i32 = @alloc(i32, a); }", TE_INCOMPLETE_TYPE)
SEMA_ERROR("alloc_bad_names", "struct Allocator { c: *void; a: fn(*void, i64) -> *void; f: fn(*void, *void) -> void; } fn main() { a: Allocator; p: *i32 = @alloc(i32, a); }", TE_INCOMPLETE_TYPE)
SEMA_ERROR("alloc_bad_sig", "struct Allocator { ctx: *void; _alloc: fn(i32) -> *void; _free: fn(*void, *void) -> void; } fn main() { a: Allocator; p: *i32 = @alloc(i32, a); }", TE_TYPE_MISMATCH)
SEMA_ERROR("alloc_bad_args", "fn main() { p: *i32 = @alloc(i32); }", TE_ARG_COUNT_MISMATCH)
SEMA_ERROR("free_bad_args", "fn main() { @free(); }", TE_ARG_COUNT_MISMATCH)

SEMA_ERROR("alloc_bad_type", "import std; fn main() { p: *i32 = @alloc(10, std.heap.allocator, 1); }", TE_TYPE_MISMATCH)
SEMA_ERROR("alloc_bad_allocator", "fn main() { p: *i32 = @alloc(i32, 10, 1); }", TE_TYPE_MISMATCH)
SEMA_ERROR("alloc_bad_count", "import std; fn main() { p: *i32 = @alloc(i32, std.heap.allocator, \"hi\"); }", TE_TYPE_MISMATCH)
SEMA_ERROR("free_bad_allocator", "fn main() { p: *i32 = 0 as *i32; @free(10, p); }", TE_TYPE_MISMATCH)
SEMA_ERROR("free_bad_ptr", "import std; fn main() { @free(std.heap.allocator, 10); }", TE_TYPE_MISMATCH)
