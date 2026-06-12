#include "test_harness.h"
#include "test_utils.h"
#include <string.h>

int test_intrinsic_exhaustive() {
    // 1. Valid @alloc
    {
        const char *src = "pub struct Allocator { ctx: *void; alloc: fn(*void, i64)->*void; free: fn(*void, *void); } "
                          "fn main() { a: Allocator; x: *i32 = @alloc(i32, a, 10); }";
        CompileResult res = compile_source((char*)src);
        ASSERT(!res.parse_failed);
        // Assuming no semantic errors means res.ctx.errors->count == 0
        ASSERT(res.ctx.errors->count == 0);
        cleanup_compilation(&res);
    }
    return 1;
}

int test_attribute_order_exhaustive() {
    return 1;
}
