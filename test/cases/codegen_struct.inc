// test/cases/codegen_struct.inc

CODEGEN_EXIT("struct_out_of_order", 
    "struct S { a: i32; b: i32; }\n"
    "fn main() -> i32 {\n"
    "    s: S = S{b: 2, a: 1};\n"
    "    return s.a * 10 + s.b;\n"
    "}", 12)

CODEGEN_EXIT("linked_list_stress", 
    "struct Node { val: i32; next: *Node; }\n"
    "fn main() -> i32 {\n"
    "    n2: Node = Node{val: 20, next: 0 as *Node};\n"
    "    n1: Node = Node{val: 10, next: &n2};\n"
    "    return n1.next.val;\n"
    "}", 20)

CODEGEN_EXIT("slice_from_struct", 
    "struct S { a: i32[3]; }\n"
    "fn main() -> i32 {\n"
    "    s: S = S{ a: {1, 2, 3} };\n"
    "    sl: i32[] = s.a;\n"
    "    return sl[1];\n"
    "}", 2)

CODEGEN_EXIT("struct_pointer_member_assign",
    "struct S { x: i32; }\n"
    "fn main() -> i32 {\n"
    "    s: S = S { x: 10 };\n"
    "    p: *S = &s;\n"
    "    p.x = 20;\n"
    "    return s.x;\n"
    "}", 20)

CODEGEN_EXIT("alignment_stress", 
    "struct S { a: char; b: i64; c: bool; }\n"
    "fn main() -> i32 {\n"
    "    s: S;\n"
    "    s.b = 123456789;\n"
    "    return s.b as i32;\n"
    "}", 123456789)

CODEGEN_EXIT("nested_struct_array", 
    "struct Point { x: i32; y: i32; }\n"
    "struct Polygon { points: Point[2]; }\n"
    "fn main() -> i32 {\n"
    "    poly: Polygon = Polygon { points: { Point{x: 1, y: 2}, Point{x: 3, y: 4} } };\n"
    "    return poly.points[1].y;\n"
    "}", 4)

CODEGEN_EXIT("deep_struct", 
    "struct A { val: i32; }\n"
    "struct B { a: A; }\n"
    "struct C { b: B; }\n"
    "fn main() -> i32 {\n"
    "    c: C = C { b: B { a: A { val: 42 } } };\n"
    "    return c.b.a.val;\n"
    "}", 42)

CODEGEN_EXIT("global_struct_init",
    "struct Point { x: i32; y: i32; }\n"
    "p: Point = Point{x: 10, y: 20};\n"
    "fn main() -> i32 {\n"
    "    return p.x + p.y;\n"
    "}", 30)

CODEGEN_EXIT("struct_literal_address",
    "struct Point { x: i32; y: i32; }\n"
    "fn get_x(p: *Point) -> i32 { return p.x; }\n"
    "fn main() -> i32 {\n"
    "    return get_x(&Point{x: 15, y: 25});\n"
    "}", 15)
