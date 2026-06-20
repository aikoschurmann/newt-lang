// test/cases/sema_intrinsics.inc

SEMA_ERROR("alloc_bad_fields", "struct Allocator { ctx: *void; } fn main() { a: Allocator; p: *i32 = @alloc(i32, a); }", TE_ALLOCATOR_SHAPE_INVALID)
SEMA_ERROR("alloc_bad_names", "struct Allocator { c: *void; a: fn(*void, i64) -> *void; f: fn(*void, *void) -> void; } fn main() { a: Allocator; p: *i32 = @alloc(i32, a); }", TE_ALLOCATOR_SHAPE_INVALID)
SEMA_ERROR("alloc_bad_sig", "struct Allocator { ctx: *void; _alloc: fn(i32) -> *void; _free: fn(*void, *void) -> void; } fn main() { a: Allocator; p: *i32 = @alloc(i32, a); }", TE_INVALID_ALLOCATOR)
SEMA_ERROR("alloc_bad_args", "fn main() { p: *i32 = @alloc(i32); }", TE_ARG_COUNT_MISMATCH)
SEMA_ERROR("free_bad_args", "fn main() { @free(); }", TE_ARG_COUNT_MISMATCH)

SEMA_VALID("alloc_with_alias", "import std; alias MyInt = i32; fn main() { p: *MyInt = @alloc(MyInt, std.heap.allocator, 1); }")
SEMA_ERROR("alloc_bad_allocator", "fn main() { p: *i32 = @alloc(i32, 10, 1); }", TE_INVALID_ALLOCATOR)
SEMA_ERROR("alloc_bad_count", "import std; fn main() { p: *i32 = @alloc(i32, std.heap.allocator, \"hi\"); }", TE_TYPE_MISMATCH)
SEMA_ERROR("free_bad_allocator", "fn main() { p: *i32 = 0 as *i32; @free(10, p); }", TE_INVALID_ALLOCATOR)
SEMA_ERROR("free_bad_ptr", "import std; fn main() { @free(std.heap.allocator, 10); }", TE_TYPE_MISMATCH)

SEMA_VALID("print_basic", "fn main() { print(\"Hello\"); }")
SEMA_VALID("print_multiple", "fn main() { print(\"Hello\", 42, 3.14); }")
SEMA_VALID("print_empty", "fn main() { print(); }")
SEMA_VALID("println_basic", "fn main() { println(\"World\"); }")
SEMA_VALID("println_multiple", "fn main() { println(\"Hello\", 42, 3.14); }")
SEMA_VALID("println_empty", "fn main() { println(); }")
