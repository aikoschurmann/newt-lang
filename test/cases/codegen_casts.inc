// test/cases/codegen_casts.inc

CODEGEN_EXIT("explicit_trunc", 
    "fn main() -> i32 {\n"
    "    a: i64 = 0xFFFFFFFF0000000A;\n"
    "    b: i32 = a as i32;\n"
    "    return b;\n"
    "}", 10)

CODEGEN_EXIT("explicit_float_to_int", 
    "fn main() -> i32 {\n"
    "    a: f64 = 42.99;\n"
    "    return a as i32;\n"
    "}", 42)

CODEGEN_EXIT("array_ptr_cast", 
    "fn main() -> i32 {\n"
    "    arr: i32[2] = {10, 20};\n"
    "    p: *i32 = &arr[0];\n"
    "    p2: *i64 = p as *i64;\n"
    "    return 0;\n"
    "}", 0)

CODEGEN_EXIT("char_to_int",
    "fn main() -> i32 {\n"
    "    c: char = 'A';\n"
    "    return c as i32;\n"
    "}", 65)
