// test/cases/codegen_arrays.inc

CODEGEN_EXIT("string_subscript",
    "fn main() -> i32 {\n"
    "    s: str = \"hello\";\n"
    "    if (s[0] == 'h' && s[1] == 'e') {\n"
    "        return 1;\n"
    "    }\n"
    "    return 0;\n"
    "}", 1)

CODEGEN_EXIT("large_stack_array", 
    "fn main() -> i32 {\n"
    "    arr: i32[5000];\n"
    "    arr[4999] = 1337;\n"
    "    return arr[4999];\n"
    "}", 1337)

CODEGEN_EXIT("multi_dim_array",
    "fn main() -> i32 {\n"
    "    arr: i32[2][2];\n"
    "    arr[1][1] = 42;\n"
    "    return arr[1][1];\n"
    "}", 42)

CODEGEN_EXIT("slice_pass_by_value",
    "fn get_first(s: i32[]) -> i32 {\n"
    "    return s[0];\n"
    "}\n"
    "fn main() -> i32 {\n"
    "    arr: i32[3] = {10, 20, 30};\n"
    "    return get_first(arr);\n"
    "}", 10)

CODEGEN_EXIT("array_literal_subscript",
    "fn main() -> i32 {\n"
    "    return ({10, 20, 30})[1];\n"
    "}", 20)
