#include "../harness/test_harness.h"
#include "../helpers/compiler_helpers.h"

TEST_CASE_PRIO("Codegen: Basic Arithmetic", 40) {
    ASSERT_EQ_INT(test_run_and_get_exit_code("fn main() -> i32 { return 10 + 20; }"), 30);
    return 1;
}

TEST_CASE_PRIO("Codegen: If-Else", 40) {
    const char *src = "fn main() -> i32 { if (1 < 2) { return 1; } else { return 2; } }";
    ASSERT_EQ_INT(test_run_and_get_exit_code(src), 1);
    return 1;
}

TEST_CASE_PRIO("Codegen: Loops", 40) {
    const char *src = 
        "fn main() -> i32 {\n"
        "    sum: i32 = 0;\n"
        "    for (i: i32 = 0; i < 10; i = i + 1) {\n"
        "        sum = sum + i;\n"
        "    }\n"
        "    return sum;\n"
        "}";
    ASSERT_EQ_INT(test_run_and_get_exit_code(src), 45);
    return 1;
}

#define CODEGEN_EXIT(name, src, expected) \
    TEST_CASE_PRIO("Codegen/" name, 40) { ASSERT_EQ_INT(test_run_and_get_exit_code(src), expected); return 1; }

#include "codegen_abi.inc"
#include "codegen_alias.inc"
#include "codegen_struct.inc"
#include "codegen_pointers.inc"
#include "codegen_loops.inc"
#include "codegen_logic.inc"
#include "codegen_casts.inc"
#include "codegen_arrays.inc"
#include "codegen_intrinsics.inc"
#include "codegen_defer.inc"
#include "codegen_generics.inc"

#undef CODEGEN_EXIT
