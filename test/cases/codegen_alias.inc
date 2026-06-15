// test/cases/codegen_alias.inc

CODEGEN_EXIT("alias_chain",
    "x: i32 = 42;\n"
    "alias a = x;\n"
    "alias b = a;\n"
    "fn main() -> i32 {\n"
    "    b = 100;\n"
    "    return x;\n"
    "}", 100)

CODEGEN_EXIT("alias_resolution_stress",
    "struct Target { val: i32; }\n"
    "alias a = t;\n"
    "t: Target = Target { val: 100 };\n"
    "fn main() -> i32 {\n"
    "    a.val = 200;\n"
    "    return t.val;\n"
    "}", 200)

CODEGEN_EXIT("alias_to_function",
    "fn my_add(x: i32, y: i32) -> i32 { return x + y; }\n"
    "alias add = my_add;\n"
    "fn main() -> i32 {\n"
    "    return add(10, 5);\n"
    "}", 15)

CODEGEN_EXIT("alias_to_struct",
    "struct MyStruct { id: i32; }\n"
    "alias S = MyStruct;\n"
    "fn main() -> i32 {\n"
    "    val: S = S { id: 77 };\n"
    "    return val.id;\n"
    "}", 77)
