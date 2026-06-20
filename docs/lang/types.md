# Type System

Newt is a statically and strongly typed language. All types are resolved at compile time. There is no type inference for variable declarations — every variable must explicitly declare its type. This section covers the full type system: primitives, compound types, function types, and how the type store works internally.

---

## Primitive Types

Newt provides a fixed set of built-in primitive types. All integer and float types are fixed-width, meaning their size in memory is part of their type contract and never platform-dependent.

### Integer Types

| Type  | Signed | Bits | Range |
|-------|--------|------|-------|
| `i8`  | Yes    | 8    | -128 to 127 |
| `i16` | Yes    | 16   | -32,768 to 32,767 |
| `i32` | Yes    | 32   | -2,147,483,648 to 2,147,483,647 |
| `i64` | Yes    | 64   | -9,223,372,036,854,775,808 to 9,223,372,036,854,775,807 |
| `u8`  | No     | 8    | 0 to 255 |
| `u16` | No     | 16   | 0 to 65,535 |
| `u32` | No     | 32   | 0 to 4,294,967,295 |
| `u64` | No     | 64   | 0 to 18,446,744,073,709,551,615 |

Signed integers use standard two's complement representation. Unsigned integers wrap around on overflow (no undefined behavior, unlike C).

### Floating Point Types

| Type  | Bits | Precision |
|-------|------|-----------|
| `f32` | 32   | ~7 decimal digits (IEEE 754 single) |
| `f64` | 64   | ~15 decimal digits (IEEE 754 double) |

Floating-point literals without a suffix are treated as `f64` by default. Assignment to `f32` requires either an explicit cast or a context where `f32` is unambiguous.

### Boolean Type

`bool` is a distinct type with two literal values: `true` and `false`. It is **not** an integer. You cannot pass a `bool` where an `i32` is expected without an explicit cast. Conditions in `if`, `while`, and `for` must strictly evaluate to `bool`.

### Character Type

`char` represents a single byte (8-bit value). Internally it stores a Unicode code point in the ASCII range, but the type system treats it as distinct from `u8`. This prevents accidental arithmetic on character data. Characters are written with single quotes: `'a'`, `'\n'`, `'\0'`.

```rust
c: char = 'A';
n: u8 = c as u8; // Explicit cast required
```

---

## Compound Types

### Structs

Structs define a named, contiguous block of memory. Fields are laid out in declaration order. There is no hidden padding reordering.

```rust
pub struct Transform {
    pub x: f32;
    pub y: f32;
    pub rotation: f32;
    active: bool;  // private field
}
```

A struct's total memory footprint is the sum of all its field sizes, in order. Fields themselves can be any type including arrays, pointers, or other structs.

---

### Fixed-Size Arrays

The array type `T[N]` is a contiguous block of `N` values of type `T`. The size `N` **must** be a compile-time constant — either an integer literal or a `const` expression.

```rust
const CAPACITY: i32 = 64;

buffer: u8[CAPACITY];         // 64 bytes, on the stack
primes: i32[5] = {2,3,5,7,11};  // Initialized array
matrix: f32[3][3];               // 3x3 matrix of f32
```

**Key properties of arrays:**
- The size is baked into the type itself. `i32[3]` and `i32[4]` are completely distinct types.
- Arrays are value types. Assigning one array to another copies all elements.
- Array indexing uses `arr[i]`. Indices are zero-based.
- You cannot have a runtime-sized stack array. All stack arrays must have compile-time-known sizes.

```rust
n: i32 = 10;
// bad: i32[n]; // ERROR: TE_NOT_CONST — n is not a compile-time constant
```

---

### Slices

A slice `T[]` is a dynamically-sized *view* into contiguous memory. It does not own the memory it refers to. Internally, a slice is a **fat pointer** — a two-word struct containing:

1. `ptr: *T` — the address of the first element
2. `len: i64` — the number of accessible elements

This means slices are exactly 16 bytes on a 64-bit platform regardless of the element type. They are cheap to copy and to pass by value.

```rust
// A function that accepts any i32 slice, regardless of source size
fn sum(nums: i32[]) -> i64 {
    total: i64 = 0;
    for (i: i32 = 0; i < nums.len; i += 1) {
        total = total + (nums[i] as i64);
    }
    return total;
}
```

**Accessing the length:**
The `.len` property on a slice returns its `i64` element count. This is the *only* intrinsic property available on slices.

```rust
fn print_length(data: u8[]) -> void {
    print("Length: ", data.len, "\n");
}
```

**Slices are views, not owners.** The underlying memory must outlive the slice. Passing a stack array's slice out of its function scope results in a dangling pointer.

---

### Pointers

A pointer `*T` stores the 64-bit memory address of a value of type `T`. Newt's pointer model is intentionally minimal:

- **Take address:** `&value` returns a `*T`
- **Dereference:** `*ptr` reads the value at the address
- **No pointer arithmetic syntax:** You cannot do `ptr + 1` directly. To advance through memory manually, you cast to `u64`, perform arithmetic, and cast back.

```rust
x: i32 = 99;
ptr: *i32 = &x;
*ptr = 100;        // x is now 100

// Manual address arithmetic
addr: u64 = ptr as u64;
next: *i32 = (addr + 4) as *i32;
```

**Auto-deref through dot notation:**
When accessing fields or calling methods on a pointer to a struct, you use `.` directly — Newt automatically dereferences the pointer. There is no `->` operator.

```rust
t: Transform;
p: *Transform = &t;

p.x = 1.0;          // Equivalent to (*p).x = 1.0;
p.active = true;    // Works even for private-within-same-module access
```

**`*void` — Untyped Pointer:**
`*void` is a raw, typeless memory address. Any typed pointer can be implicitly cast to `*void`, discarding type information. Casting back to a typed pointer requires an explicit `as` cast and is entirely unchecked.

```rust
x: i32 = 42;
raw: *void = &x;          // Implicit: *i32 -> *void
back: *i32 = raw as *i32; // Explicit required: *void -> *i32
```

---

### Function Types

Functions are first-class values in Newt. A function's type encodes its full signature: the parameter types and the return type.

**Syntax:** `fn(ParamType1, ParamType2, ...) -> ReturnType`

```rust
// A variable holding a function that takes two i32s and returns an i32
op: fn(i32, i32) -> i32;

fn add(a: i32, b: i32) -> i32 { return a + b; }
fn mul(a: i32, b: i32) -> i32 { return a * b; }

op = add;
result: i32 = op(3, 4);  // result = 7

op = mul;
result = op(3, 4);        // result = 12
```

**Passing functions as arguments:**
Function types are used directly as parameter types, enabling higher-order functions.

```rust
fn apply(a: i32, b: i32, f: fn(i32, i32) -> i32) -> i32 {
    return f(a, b);
}

fn main() -> i32 {
    fn add(a: i32, b: i32) -> i32 { return a + b; }
    return apply(10, 20, add);  // returns 30
}
```

**Arrays of function pointers:**
Since function types are regular types, you can form arrays of them.

```rust
fn double(x: i32) -> i32 { return x * 2; }
fn triple(x: i32) -> i32 { return x * 3; }

ops: fn(i32) -> i32[2] = {double, triple};
result: i32 = ops[0](5);  // result = 10
```

**Function types and generics:**
A generic function with type parameter `T` can accept or return a function type parameterized on `T`.

```rust
fn transform<T>(val: T, f: fn(T) -> T) -> T {
    return f(val);
}

fn negate(x: i32) -> i32 { return -x; }

fn main() -> i32 {
    return transform<i32>(5, negate);  // returns -5
}
```

---

## The Type Store and Type Identity

Internally, Newt uses a **TypeStore** (built on an arena allocator and a dense hash-based interner) to guarantee a fundamental invariant: **every distinct type has exactly one canonical representation in memory**. Two types are equal if and only if their pointers are equal.

This means:
- `i32 == i32` is a single pointer comparison, not a structural walk.
- `*i32 == *i32` is guaranteed if both were obtained through the same TypeStore.
- `Pair<i32, f64> == Pair<i32, f64>` is a single pointer comparison after the generic instantiation cache deduplicates them.

This invariant makes type checking extremely fast and ensures the instantiation cache for generics is correct-by-construction.
