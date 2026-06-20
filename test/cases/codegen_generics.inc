// test/cases/codegen_generics.inc

CODEGEN_EXIT("generics_basic",
    "struct Box<T> {\n"
    "    val: T;\n"
    "}\n"
    "fn main() -> i32 {\n"
    "    b: Box<i32>;\n"
    "    b.val = 42;\n"
    "    return b.val;\n"
    "}\n",
    42
)

/*
CODEGEN_EXIT("generics_methods",
    "struct Box<T> {\n"
    "    val: T;\n"
    "}\n"
    "impl<T> Box<T> {\n"
    "    fn get(self: *Box<T>) -> T {\n"
    "        return self.val;\n"
    "    }\n"
    "    fn set(self: *Box<T>, v: T) {\n"
    "        self.val = v;\n"
    "    }\n"
    "}\n"
    "fn main() -> i32 {\n"
    "    b: Box<i32>;\n"
    "    b.set(100);\n"
    "    return b.get();\n"
    "}\n",
    100
)
*/

/*CODEGEN_EXIT("generics_multiple_params",
    "struct Pair<T, U> {\n"
    "    first: T;\n"
    "    second: U;\n"
    "}\n"
    "impl<T, U> Pair<T, U> {\n"
    "    fn get_first(self: *Pair<T, U>) -> T {\n"
    "        return self.first;\n"
    "    }\n"
    "    fn get_second(self: *Pair<T, U>) -> U {\n"
    "        return self.second;\n"
    "    }\n"
    "}\n"
    "fn main() -> i32 {\n"
    "    p: Pair<i32, i32>;\n"
    "    p.first = 120;\n"
    "    p.second = 240;\n"
    "    return p.get_first() + p.get_second();\n"
    "}\n",
    360
)*/

CODEGEN_EXIT("generics_nested",
    "struct Wrapper<T> {\n"
    "    val: T;\n"
    "}\n"
    "struct Container<T> {\n"
    "    wrap: Wrapper<T>;\n"
    "}\n"
    "fn main() -> i32 {\n"
    "    c: Container<i32>;\n"
    "    c.wrap.val = 99;\n"
    "    return c.wrap.val;\n"
    "}\n",
    99
)

/*CODEGEN_EXIT("generics_multiple_instantiations",
    "struct Value<T> {\n"
    "    val: T;\n"
    "}\n"
    "impl<T> Value<T> {\n"
    "    fn get(self: *Value<T>) -> T {\n"
    "        return self.val;\n"
    "    }\n"
    "}\n"
    "fn main() -> i32 {\n"
    "    v1: Value<i32>;\n"
    "    v2: Value<i32>;\n"
    "    v1.val = 10;\n"
    "    v2.val = 20;\n"
    "    return v1.get() + v2.get();\n"
    "}\n",
    30
)

CODEGEN_EXIT("generics_different_instantiations",
    "struct Value<T> {\n"
    "    val: T;\n"
    "}\n"
    "impl<T> Value<T> {\n"
    "    fn get(self: *Value<T>) -> T {\n"
    "        return self.val;\n"
    "    }\n"
    "}\n"
    "fn main() -> i32 {\n"
    "    v1: Value<i32>;\n"
    "    v2: Value<i8>;\n"
    "    v1.val = 150;\n"
    "    v2.val = 50;\n"
    "    return v1.get() + (v2.get() as i32);\n"
    "}\n",
    200
)*/

CODEGEN_EXIT("generics_pointer",
    "struct Box<T> {\n"
    "    val: T;\n"
    "}\n"
    "fn main() -> i32 {\n"
    "    b: Box<i32>;\n"
    "    b.val = 88;\n"
    "    p: *Box<i32> = &b;\n"
    "    return p.val;\n"
    "}\n",
    88
)

CODEGEN_EXIT("generics_recursive",
    "struct Node<T> {\n"
    "    val: T;\n"
    "    next: *Node<T>;\n"
    "}\n"
    "fn main() -> i32 {\n"
    "    n1: Node<i32>;\n"
    "    n2: Node<i32>;\n"
    "    n1.val = 11;\n"
    "    n1.next = &n2;\n"
    "    n2.val = 22;\n"
    "    n2.next = null;\n"
    "    return n1.next.val;\n"
    "}\n",
    22
)

CODEGEN_EXIT("generics_alias_instantiation",
    "struct Box<T> { val: T; }\n"
    "alias IntBox = Box<i32>;\n"
    "fn main() -> i32 {\n"
    "    b: IntBox;\n"
    "    b.val = 123;\n"
    "    return b.val;\n"
    "}\n",
    123
)

CODEGEN_EXIT("generics_nested_instantiation",
    "struct Outer<T> { inner: T; }\n"
    "struct Inner<T> { val: T; }\n"
    "fn main() -> i32 {\n"
    "    o: Outer<Inner<i32> >;\n"
    "    o.inner.val = 456;\n"
    "    return o.inner.val;\n"
    "}\n",
    456
)

/*CODEGEN_EXIT("generics_method_on_alias",
    "struct Value<T> { val: T; }\n"
    "impl<T> Value<T> { fn get(self: *Value<T>) -> T { return self.val; } }\n"
    "alias IntVal = Value<i32>;\n"
    "fn main() -> i32 {\n"
    "    v: IntVal;\n"
    "    v.val = 789;\n"
    "    return v.get();\n"
    "}\n",
    789
)*/

CODEGEN_EXIT("generics_with_primitive_alias",
    "alias MyInt = i32;\n"
    "struct Box<T> { val: T; }\n"
    "fn main() -> i32 {\n"
    "    b: Box<MyInt>;\n"
    "    b.val = 55;\n"
    "    return b.val;\n"
    "}\n",
    55
)

CODEGEN_EXIT("generics_alloc_alias",
    "import std;\n"
    "alias MyInt = i32;\n"
    "struct Box<T> { val: T; }\n"
    "alias IntBox = Box<MyInt>;\n"
    "fn main() -> i32 {\n"
    "    b: *IntBox = @alloc(IntBox, std.heap.allocator, 1);\n"
    "    b.val = 77;\n"
    "    return b.val;\n"
    "}\n",
    77
)

CODEGEN_EXIT("generics_fn_basic",
    "fn id<T>(x: T) -> T { return x; }\n"
    "fn main() -> i32 {\n"
    "    return id<i32>(100);\n"
    "}\n",
    100
)

CODEGEN_EXIT("generics_fn_multiple",
    "fn first<T, U>(x: T, y: U) -> T { return x; }\n"
    "fn main() -> i32 {\n"
    "    return first<i32, f64>(42, 3.14);\n"
    "}\n",
    42
)

CODEGEN_EXIT("generics_fn_nested_call",
    "fn add<T>(x: T, y: T) -> T { return x + y; }\n"
    "fn main() -> i32 {\n"
    "    return add<i32>(add<i32>(10, 20), 30);\n"
    "}\n",
    60
)

CODEGEN_EXIT("generics_fn_different_types",
    "fn add_double<T>(x: T) -> T { return x + x; }\n"
    "fn main() -> i32 {\n"
    "    a: i32 = add_double<i32>(20);\n"
    "    b: i64 = add_double<i64>(10);\n"
    "    return a + (b as i32);\n"
    "}\n",
    60
)
