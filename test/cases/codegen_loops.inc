// test/cases/codegen_loops.inc

CODEGEN_EXIT("for_loop_empty_clauses",
    "fn main() -> i32 {\n"
    "    i: i32 = 0;\n"
    "    for (; i < 5; ) {\n"
    "        i = i + 1;\n"
    "    }\n"
    "    return i;\n"
    "}", 5)

CODEGEN_EXIT("while_loop_break",
    "fn main() -> i32 {\n"
    "    i: i32 = 0;\n"
    "    while (i < 10) {\n"
    "        if (i == 5) { break; }\n"
    "        i = i + 1;\n"
    "    }\n"
    "    return i;\n"
    "}", 5)

CODEGEN_EXIT("for_loop_continue",
    "fn main() -> i32 {\n"
    "    sum: i32 = 0;\n"
    "    for (i: i32 = 0; i < 5; i = i + 1) {\n"
    "        if (i == 2) { continue; }\n"
    "        sum = sum + i;\n"
    "    }\n"
    "    return sum;\n"
    "}", 8) // 0 + 1 + 3 + 4 = 8

CODEGEN_EXIT("nested_loops",
    "fn main() -> i32 {\n"
    "    count: i32 = 0;\n"
    "    for (i: i32 = 0; i < 3; i = i + 1) {\n"
    "        j: i32 = 0;\n"
    "        while (j < 3) {\n"
    "            count = count + 1;\n"
    "            j = j + 1;\n"
    "        }\n"
    "    }\n"
    "    return count;\n"
    "}", 9)
