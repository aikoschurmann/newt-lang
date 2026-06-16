// test/cases/codegen_abi.inc

TEST_CASE_PRIO("Codegen/ABI/LargeStructArg", 40) {
    // 3 * 8 bytes = 24 bytes > 16 bytes limit. Must be passed by pointer (byval).
    const char *src = 
        "struct Large { a: i64; b: i64; c: i64; }\n"
        "fn take_large(l: Large) -> i64 {\n"
        "    return l.a + l.b + l.c;\n"
        "}\n"
        "fn main() -> i64 {\n"
        "    l: Large = Large { a: 1, b: 2, c: 3 };\n"
        "    return take_large(l);\n"
        "}";
    ASSERT_EQ_INT(test_run_and_get_exit_code(src), 6);
    return 1;
}

TEST_CASE_PRIO("Codegen/ABI/LargeStructReturn", 40) {
    // Return a struct > 16 bytes (must use sret)
    const char *src = 
        "struct Large { a: i64; b: i64; c: i64; }\n"
        "fn mk_large() -> Large {\n"
        "    return Large { a: 10, b: 20, c: 30 };\n"
        "}\n"
        "fn main() -> i64 {\n"
        "    l: Large = mk_large();\n"
        "    return l.a + l.b + l.c;\n"
        "}";
    ASSERT_EQ_INT(test_run_and_get_exit_code(src), 60);
    return 1;
}

TEST_CASE_PRIO("Codegen/ABI/NestedLargeStruct", 40) {
    const char *src = 
        "struct Large { a: i64; b: i64; c: i64; }\n"
        "struct Nested { l1: Large; l2: Large; }\n"
        "fn sum_nested(n: Nested) -> i64 {\n"
        "    return n.l1.a + n.l1.b + n.l1.c + n.l2.a + n.l2.b + n.l2.c;\n"
        "}\n"
        "fn main() -> i64 {\n"
        "    n: Nested = Nested { \n"
        "        l1: Large { a: 1, b: 2, c: 3 }, \n"
        "        l2: Large { a: 4, b: 5, c: 6 } \n"
        "    };\n"
        "    return sum_nested(n);\n"
        "}";
    ASSERT_EQ_INT(test_run_and_get_exit_code(src), 21);
    return 1;
}

CODEGEN_EXIT("mixed_args_abi", 
    "struct Large { a: i64; b: i64; c: i64; }\n"
    "fn take_mixed(a: i32, l: Large, b: i32) -> i32 {\n"
    "    return a + (l.a + l.b + l.c) as i32 + b;\n"
    "}\n"
    "fn main() -> i32 {\n"
    "    l: Large = Large { a: 1, b: 2, c: 3 };\n"
    "    return take_mixed(10, l, 20);\n"
    "}", 36)

CODEGEN_EXIT("deep_nested_abi",
    "struct Inner { x: i64; y: i64; z: i64; }\n"
    "struct Outer { a: Inner; b: Inner; }\n"
    "fn process_outer(o: Outer) -> i64 {\n"
    "    return o.a.x + o.a.y + o.a.z + o.b.x + o.b.y + o.b.z;\n"
    "}\n"
    "fn main() -> i64 {\n"
    "    o: Outer = Outer { \n"
    "        a: Inner { x: 1, y: 2, z: 3 }, \n"
    "        b: Inner { x: 4, y: 5, z: 6 } \n"
    "    };\n"
    "    return process_outer(o);\n"
    "}", 21)

CODEGEN_EXIT("multi_large_return",
    "struct Large { a: i64; b: i64; c: i64; }\n"
    "fn get_a() -> Large { return Large { a: 1, b: 2, c: 3 }; }\n"
    "fn get_b() -> Large { return Large { a: 4, b: 5, c: 6 }; }\n"
    "fn main() -> i64 {\n"
    "    la: Large = get_a();\n"
    "    lb: Large = get_b();\n"
    "    return la.a + lb.a;\n"
    "}", 5)
