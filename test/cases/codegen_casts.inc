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

CODEGEN_EXIT("u8_wrap",
    "fn main() -> i32 {\n"
    "    a: i32 = 258;\n" // 256 + 2
    "    b: u8 = a as u8;\n"
    "    return b as i32;\n"
    "}", 2)

CODEGEN_EXIT("i8_wrap",
    "fn main() -> i32 {\n"
    "    a: i32 = 129;\n" // 128 is -128 in i8
    "    b: i8 = a as i8;\n"
    "    return b as i32;\n"
    "}", -127) // 128 -> -128, 129 -> -127

CODEGEN_EXIT("u16_wrap",
    "fn main() -> i32 {\n"
    "    a: i64 = 65537;\n" // 65536 + 1
    "    b: u16 = a as u16;\n"
    "    return b as i32;\n"
    "}", 1)

CODEGEN_EXIT("sign_interpretation",
    "fn main() -> i32 {\n"
    "    a: i32 = -1;\n"
    "    b: u32 = a as u32;\n"
    "    if b == 4294967295 { return 1; }\n"
    "    return 0;\n"
    "}", 1)
