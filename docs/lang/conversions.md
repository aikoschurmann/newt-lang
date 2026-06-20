# Type Conversions, Promotions & Decay Rules

Newt enforces strict conversion semantics. There are two completely separate mechanisms: **implicit promotions** (the compiler inserts a conversion automatically) and **explicit casts** (you write `as` to force a conversion). Understanding the exact boundary between these is critical for writing correct systems-level code.

---

## Implicit Promotions

An implicit promotion occurs automatically when you assign a value or pass an argument to a function. Promotions are only allowed when there is **zero risk of data loss**. The compiler will insert a hidden cast node in the AST; no extra syntax is needed from you.

### Integer Widening

Integer widening is allowed when the target type can represent every value the source can hold. The rules follow directly from the bit widths and signedness.

| Source | May implicitly widen to |
|--------|------------------------|
| `i8`   | `i16`, `i32`, `i64`, `f32`, `f64` |
| `i16`  | `i32`, `i64`, `f32`, `f64` |
| `i32`  | `i64`, `f64` |
| `i64`  | *(nothing — widest signed integer)* |
| `u8`   | `i16`, `i32`, `i64`, `u16`, `u32`, `u64`, `f32`, `f64` |
| `u16`  | `i32`, `i64`, `u32`, `u64`, `f32`, `f64` |
| `u32`  | `i64`, `u64`, `f64` |
| `u64`  | *(nothing — widest unsigned integer)* |
| `f32`  | `f64` |
| `f64`  | *(nothing — widest float)* |

**Why does `i32` not promote to `f32`?**  
An `f32` has only 24 bits of mantissa. Values of `i32` larger than ~16 million cannot be represented exactly in `f32`, which is a form of data loss. Therefore, `i32 -> f32` is not a safe implicit promotion. You must write `x as f32` explicitly.

**Why does `u8` promote to `i16` but not `i8`?**  
A `u8` can hold values 0–255. An `i16` covers -32,768 to 32,767, which is a strict superset of 0–255. But an `i8` only covers -128 to 127 — it cannot represent values 128–255, so the promotion would lose data.

**Narrowing is always an error:**
```rust
large: i64 = 1_000_000;
small: i32 = large;  // ERROR: Type mismatch — i64 cannot implicitly narrow to i32
```

### Float Widening

Only `f32 -> f64` is allowed. `f64 -> f32` is narrowing and requires `as`.

### No Implicit Conversions Across These Boundaries

The following conversions are **never** implicit regardless of direction, and always require an explicit `as` cast:
- Any narrowing integer conversion (`i64 -> i32`, `u32 -> i16`, etc.)
- Any float-to-integer conversion (`f64 -> i32`)
- Any integer-to-float that is not lossless (e.g. `i32 -> f32`)
- `bool` to/from any integer
- `char` to/from any integer
- Any pointer to/from any integer
- Any pointer to/from `bool`

---

## Decay Rules

Decay rules are a specific class of implicit conversions that apply to array and pointer types at assignment and function call boundaries. The name "decay" comes from C and refers to a value *losing* (decaying away) some of its compile-time type information to become a more general form.

### 1. Array-to-Slice Decay: `T[N]` → `T[]`

This is the most commonly encountered decay. When you pass a fixed-size array to a function expecting a slice, or assign it to a slice variable, the compiler automatically constructs a fat pointer using:
- The address of the array's first element as `ptr`
- The compile-time-known size `N` as `len`

```rust
fn process(data: i32[]) -> void {
    // data.len is 5 here — the decay preserved the compile-time size
}

fn main() -> i32 {
    nums: i32[5] = {1, 2, 3, 4, 5};
    process(nums);  // T[5] decays to T[] — no copy, just a fat pointer
    return 0;
}
```

The key property: **the length is not lost**. The `N` from the fixed-size array becomes the `len` field of the fat-pointer slice at the decay site.

### 2. Array-to-Pointer Decay: `T[N]` → `*T`

An array can also decay to a raw pointer to its first element. This discards the length entirely — the resulting `*T` has no information about how many elements follow.

```rust
fn fill(ptr: *i32, count: i32, val: i32) -> void {
    for (i: i32 = 0; i < count; i += 1) {
        (ptr as u64 + (i as u64 * 4)) as *i32;  // Manual arithmetic
    }
}

fn main() -> i32 {
    arr: i32[10];
    fill(arr, 10, 0);  // T[10] decays to *T
    return 0;
}
```

**Prefer slices over raw-pointer decay.** The slice form preserves the length and is safer. The pointer form is available for FFI with C libraries.

### 3. Typed-Pointer-to-Void-Pointer: `*T` → `*void`

Any typed pointer can implicitly decay to `*void`, discarding type information. This is necessary for generic allocator interfaces and FFI. The reverse (`*void` → `*T`) is always an explicit cast.

```rust
fn raw_copy(dst: *void, src: *void, bytes: i64) -> void {
    // works with any pointer type
}

fn main() -> i32 {
    x: i32 = 10;
    raw_copy(&x, &x, 4);  // &x is *i32, implicitly becomes *void
    return 0;
}
```

### 4. Pointer-to-Array-to-Pointer-to-Slice: `*T[N]` → `*T[]`

If you have a pointer to a fixed-size array, it can implicitly decay into a pointer to a slice of the same element type. This is a narrower rule, mainly useful when storing a slice reference into a pointer target.

```rust
fn get_slice_ptr(p: *i32[]) -> void { }

fn main() -> i32 {
    arr: i32[8];
    get_slice_ptr(&arr);  // *i32[8] decays to *i32[]
    return 0;
}
```

### Summary Table

| Implicit Decay | Trigger | What is Preserved |
|---|---|---|
| `T[N]` → `T[]`   | Assign/pass to slice  | Element type, length (as `len`) |
| `T[N]` → `*T`    | Assign/pass to pointer | Element type only |
| `*T` → `*void`   | Assign/pass to *void   | Nothing (raw address only) |
| `*T[N]` → `*T[]` | Assign/pass to *slice ptr | Element type, length preserved in pointed-to slice |

---

## Explicit Casts (`as`)

When a conversion falls outside the implicit rules above, you must use the `as` keyword. The `as` operator is evaluated at compile time when the source is a constant (constant folding), and at runtime otherwise.

**Syntax:** `expression as TargetType`

### Numeric Narrowing

You can cast any numeric type to any other numeric type. If the value does not fit, it is **truncated** (bits are cut off). The compiler will not warn on truncation — this is intentional behavior for systems code.

```rust
large: i64 = 70000;
small: i16 = large as i16;   // 70000 mod 65536 = 4464 — data truncated!

pi: f64 = 3.14159;
approx: f32 = pi as f32;     // Precision reduced, not truncated
truncated: i32 = pi as i32;  // Truncates toward zero: 3
```

**Float-to-integer truncation always truncates toward zero:**
- `3.9 as i32` → `3`
- `-3.9 as i32` → `-3`

### Boolean Casts

`bool` cannot be implicitly converted to integers, but an explicit cast is allowed:
- `true as i32` → `1`
- `false as i32` → `0`

```rust
flag: bool = true;
count: i32 = flag as i32;   // 1
weight: f64 = flag as f64;  // 1.0
```

The reverse (integer-to-bool) is not supported via `as`. Use a comparison instead:
```rust
n: i32 = 5;
flag: bool = n != 0;   // Correct: explicit comparison
```

### Character Casts

`char` can be explicitly cast to any integer type, treating the character as its ASCII code point:

```rust
c: char = 'A';
code: i32 = c as i32;   // 65
upper: u8 = c as u8;     // 65
```

Any numeric type can be explicitly cast to `char`, treating the integer as a code point:
```rust
n: i32 = 65;
letter: char = n as char;  // 'A'
```

### Pointer Bitcasting

Any pointer can be explicitly cast to any other pointer type. This is a raw bitcast — no runtime checking occurs. This is intentional for low-level code, but dangerous if misused.

```rust
x: i32 = 0x42424242;
raw: *void = &x;
as_bytes: *u8 = raw as *u8;  // Reinterpret the int as a byte array

first_byte: u8 = *as_bytes;  // Reads the first byte of x's memory
```

### Pointer-to-Integer and Integer-to-Pointer

Pointers can be cast to `u64` or `i64` to perform raw address arithmetic. This is the only way to do pointer arithmetic in Newt.

```rust
arr: i32[4] = {10, 20, 30, 40};
base: u64 = (&arr[0]) as u64;

// Advance by one i32 = 4 bytes
second: *i32 = (base + 4) as *i32;
val: i32 = *second;  // val = 20
```

Only `i64` and `u64` are valid targets for pointer-to-integer casts. Casting a pointer to `i32` or `u32` is an error because on a 64-bit platform the pointer would be truncated.

### Compile-Time Constant Folding for Casts

When the source of an `as` cast is a compile-time constant, the cast is evaluated by the compiler at zero runtime cost. This applies recursively:

```rust
const MAX: i64 = 10_000;
const MAX_AS_F32: f32 = MAX as f32;      // Folded at compile time
const TRUNCATED: i32 = 3.99 as i32;     // 3, computed at compile time
const CHAR_CODE: i32 = 'Z' as i32;      // 90, computed at compile time
```

This is identical in behavior to a `const` expression cast — the code generator never sees the original source value, only the result.
