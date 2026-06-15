// test/cases/codegen_defer.inc

CODEGEN_EXIT("defer_while_continue",
    "fn main() -> i32 {\n"
    "    res: i32 = 0;\n"
    "    i: i32 = 0;\n"
    "    while (i < 5) {\n"
    "        i = i + 1;\n"
    "        defer { res = res + 1; }\n"
    "        if (i < 3) { continue; }\n"
    "    }\n"
    "    return res;\n"
    "}", 5)

CODEGEN_EXIT("defer_nested_early_return",
    "fn test_defer(x: i32) -> i32 {\n"
    "    defer { return 10; }\n"
    "    if (x > 0) {\n"
    "        defer { return 20; }\n"
    "        return 1;\n"
    "    }\n"
    "    return 0;\n"
    "}\n"
    "fn main() -> i32 {\n"
    "   return test_defer(1);\n"
    "}", 10)

CODEGEN_EXIT("defer_nested_scopes",
    "fn main() -> i32 {\n"
    "    res: i32 = 0;\n"
    "    {\n"
    "        defer { res = res + 100; }\n"
    "        if (res == 0) {\n"
    "            defer { res = res + 10; }\n"
    "            return res + 1; // Should execute both defers\n"
    "        }\n"
    "    }\n"
    "    return res;\n"
    "}", 1) 

CODEGEN_EXIT("defer_in_for_loop",
    "fn main() -> i32 {\n"
    "    sum: i32 = 0;\n"
    "    for(i: i32 = 0; i < 3; i = i + 1) {\n"
    "        defer { sum = sum + 10; }\n"
    "        sum = sum + 1;\n"
    "    }\n"
    "    return sum;\n"
    "}", 33)

CODEGEN_EXIT("defer_shadowing_var",
    "fn main() -> i32 {\n"
    "    val: i32 = 5;\n"
    "    {\n"
    "        val: i32 = 10;\n"
    "        defer { val = val + 1; }\n"
    "    }\n"
    "    return val;\n"
    "}", 5)
