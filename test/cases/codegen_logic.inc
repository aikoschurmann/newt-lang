// test/cases/codegen_logic.inc

CODEGEN_EXIT("short_circuit_and", 
    "fn fail() -> bool { return 1 / 0 == 0; }\n"
    "fn main() -> i32 {\n"
    "    if (false && fail()) { return 1; }\n"
    "    return 0;\n"
    "}", 0)

CODEGEN_EXIT("short_circuit_or", 
    "fn fail() -> bool { return 1 / 0 == 0; }\n"
    "fn main() -> i32 {\n"
    "    if (true || fail()) { return 1; }\n"
    "    return 0;\n"
    "}", 1)

CODEGEN_EXIT("complex_bool_logic",
    "fn main() -> i32 {\n"
    "    a: bool = true;\n"
    "    b: bool = false;\n"
    "    if (a && !b || b) {\n"
    "        return 1;\n"
    "    }\n"
    "    return 0;\n"
    "}", 1)

CODEGEN_EXIT("const_chain", 
    "const A: i32 = B + 1;\n"
    "const B: i32 = 10;\n"
    "fn main() -> i32 { return A; }", 11)
