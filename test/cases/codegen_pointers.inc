// test/cases/codegen_pointers.inc

CODEGEN_EXIT("array_ptr_math", 
    "fn main() -> i32 {\n"
    "    arr: i32[2] = {10, 20};\n"
    "    p: *i32 = &arr[0];\n"
    "    if (*p != 10) { return 1; }\n"
    "    return 0;\n"
    "}", 0)

CODEGEN_EXIT("recursive_fn_ptr", 
    "fn f(n: i32) -> i32 {\n"
    "    if (n == 0) { return 42; }\n"
    "    return f(n - 1);\n"
    "}\n"
    "fn main() -> i32 {\n"
    "    return f(5);\n"
    "}", 42)

CODEGEN_EXIT("pointer_to_pointer_deref",
    "fn main() -> i32 {\n"
    "    x: i32 = 42;\n"
    "    p: *i32 = &x;\n"
    "    pp: **i32 = &p;\n"
    "    return **pp;\n"
    "}", 42)

CODEGEN_EXIT("null_pointer_init",
    "fn main() -> i32 {\n"
    "    p: *i32 = 0 as *i32;\n"
    "    if (p == 0 as *i32) { return 1; }\n"
    "    return 0;\n"
    "}", 1)

CODEGEN_EXIT("null_keyword_init",
    "fn main() -> i32 {\n"
    "    p: *i32 = null;\n"
    "    if (p == null) { return 1; }\n"
    "    return 0;\n"
    "}", 1)
