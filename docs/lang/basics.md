# Language Specification: Basics

Newt is a statically typed, compiled language with a strict syntax designed for predictability, robust scoping, and memory safety. This specification outlines the foundational aspects of the language, ranging from variable declarations and compile-time evaluations to structural constructs like `struct` and `impl` blocks, control flow mechanisms, and intricate scoping rules.

## Variables and Constants

### Variable Declarations

Variables must be explicitly typed; there is no implicit type inference for declarations. A declaration uses the syntax `name: type = value;`. This intentional design choice ensures that types are strictly enforced at boundaries and are immediately apparent to developers reading the code.

```rust
fn main() -> i32 {
    x: i32 = 10;
    y: f64 = 3.14159;
    flag: bool = true;
    
    // Arrays require the size to be known at compile time.
    arr: i32[5]; 
    return 0;
}
```

Uninitialized variables are permitted but they are governed by strict memory initialization rules before they can be read. If a type does not have a default zero-initialization strategy, reading from it before assignment yields undefined behavior or compile-time warnings depending on the strictness flags.

### Compile-Time Constants and Constant Folding

Constants are evaluated strictly at compile time. They are declared using the `const` keyword and must be explicitly typed. Constants can be defined globally at the module level or locally within a function block. 

Newt supports complex compile-time constant folding, resolving expressions aggressively before the code is emitted. This reduces runtime overhead and allows values to be utilized in static contexts. Supported foldings include, but are not limited to:
- **Arithmetic Expressions**: `const X: i32 = (1 + 2) * 3;`
- **Bitwise Operations**: `const X: i32 = (1 << 5) | 1;`
- **Comparisons and Logic**: `const X: bool = 10 > 5;`
- **Floating Point Math**: `const X: f64 = 3.14 * 2.0;`
- **Compile-Time Type Casts**: `const X: i32 = 3.14 as i32;` (Truncates at compile time)

Because constants are fully resolved during the semantic analysis phase, they can be used in contexts requiring compile-time known values, such as fixed-size array declarations:

```rust
const SZ: i32 = 10;
const OFFSET: i32 = SZ + 5;

fn main() -> void {
    // SZ is a compile-time constant, so this is valid.
    arr: i32[SZ]; 
    
    // Complex expressions also work if entirely constant
    arr2: i32[SZ * 2]; 
}
```

Attempting to use a non-constant value for an array size yields a compile-time error (`TE_NOT_CONST`). 

```rust
fn main() -> void {
    x: i32 = 10;
    // arr: i32[x]; // ERROR: TE_NOT_CONST. Array size must be constant.
}
```

## Functions and Overloading

Functions are declared with the `fn` keyword, explicitly defining parameters and the return type. If a function yields no value, its return type must be `void`. This avoids ambiguity and enforces clear contracts for APIs.

```rust
fn multiply(a: i32, b: i32) -> i32 {
    return a * b;
}
```

### Function Overloading

Newt supports function overloading, permitting multiple functions to share the same name within the same scope, provided their parameter types differ. 

When a new function is defined, the compiler checks the current lexical scope. If a function with the same name exists, their signatures are compared. If the parameter counts or types differ, the compiler transparently upgrades the symbol to an **overload set**. Method resolution at the call site relies on matching the provided argument types to the specific signature inside the overload set.

```rust
fn process(val: i32) -> void { /* Integer implementation */ }
fn process(val: f64) -> void { /* Float implementation */ }
```

**Edge Cases & Restrictions in Overloading:**
- Functions with identical signatures (same parameter counts and types) yield a duplicate symbol error.
- The `main` function is the program entry point and cannot be overloaded under any circumstance.
- Functions declared with an external `@link` attribute (usually C-FFI bindings) cannot be overloaded, as they must map directly to a single symbol in the linked object file.
- If one overload in a set is marked `pub`, the entire overload set becomes visible, though specific signature resolution still enforces visibility rules.

## Structs and `impl` Blocks

### Struct Definition
Structs define the contiguous memory layout of custom data types. By default, struct fields are private.

```rust
pub struct Vector2 {
    pub x: f32;
    pub y: f32;
    internal_flag: bool; // Private to the module
}
```

### Methods and `impl` Blocks
Behavior is attached to structs using `impl` blocks. Method resolution distinguishes between **static methods** and **instance methods** strictly based on whether the first parameter is named `self`.

- **Static Methods**: Do not accept `self`. Typically used for constructors or utility functions. Called via the namespace of the type (`Vector2.new()`).
- **Instance Methods**: Accept `self` as the first parameter. Can take `self` by value (read-only) or by pointer (`self: *Vector2`) to modify the instance. Called via dot notation on an instance (`v.scale(2.0)`).

```rust
impl Vector2 {
    // Static method
    fn new(x: f32, y: f32) -> Vector2 {
        return Vector2 { x: x, y: y, internal_flag: false };
    }

    // Instance method (mutable via pointer)
    fn scale(self: *Vector2, factor: f32) -> void {
        self.x *= factor;
        self.y *= factor;
    }

    // Instance method (read-only, pass-by-value)
    fn length_squared(self: Vector2) -> f32 {
        return (self.x * self.x) + (self.y * self.y);
    }
}
```

When calling an instance method on a value, the compiler automatically handles taking the address of the value if the method expects a pointer (`*Type`), enhancing ergonomics.

## Control Flow

Newt provides standard, predictable control flow mechanisms without implicit truthiness—conditions must strictly evaluate to a `bool`.

### `if` / `else`
Conditional branching requires boolean expressions. Parentheses are required around the condition.

```rust
x: i32 = 15;
if (x > 10) {
    print("Large");
} else if (x == 10) {
    print("Exact");
} else {
    print("Small");
}
```

### `while` Loops
Standard pre-condition loops execute as long as the condition evaluates to `true`. You can use `break` and `continue` to manipulate the loop's execution path.

```rust
count: i32 = 0;
while (count < 10) {
    if (count == 5) {
        count += 1;
        continue;
    }
    count += 1;
}
```

### `for` Loops
C-style `for` loops are fully supported. The loop variable can be scoped directly to the loop header, ensuring it does not leak into the surrounding scope.

```rust
for (i: i32 = 0; i < 10; i += 1) {
    // i is only valid within this block
}
// i is undefined here
```

## Modules, Imports, and Visibility

Newt's module system maps directly to the filesystem, avoiding complex build configuration files for module resolution.

### Imports and Aliases
Code from other files or the standard library is brought into scope using the `import` keyword. If module paths are deeply nested or unwieldy, the `alias` keyword creates a local namespace proxy.

```rust
import std.fs.file;
alias FileSys = std.fs.file;

fn main() -> void {
    FileSys.open("test.txt");
}
```

### Visibility Rules (`pub`)
By default, all declarations (variables, constants, functions, structs) are **private** to their file/module. The `pub` keyword exports them for external usage. 

Crucially, struct visibility is decoupled from field visibility. If a struct is exported via `pub`, its fields remain private by default unless explicitly marked `pub`. This allows authors to encapsulate internal state while exposing a public API.

```rust
// auth.nt
pub const MAX_RETRIES: i32 = 3; // Exported
const INTERNAL_STATE: i32 = 1;  // Local to auth.nt

pub struct Session {
    pub token: i32;       // Accessible externally
    secret_key: i32;      // Private, inaccessible externally
}
```

## Advanced Scoping Rules

Scope resolution in Newt dictates how symbols (variables, functions, aliases) are resolved by the compiler. Scopes are hierarchical, constructed as a tree structure where each block (function body, loop, conditional) introduces a new scope depth.

### Lexical Scoping and Shadowing
When resolving a symbol, the compiler traverses outward from the innermost scope:
1. **Local Hash Map Lookup**: The compiler first checks the symbol hash map in the current scope.
2. **Ascending the Tree**: If the symbol is absent, the lookup ascends to the `parent` scope, repeating until it reaches the global module scope or the `Universe` scope.

This mechanism inherently supports **shadowing**, allowing inner blocks to declare variables that hide variables of the same name from outer blocks.

```rust
fn shadowing_example() -> void {
    x: i32 = 10;
    if (true) {
        x: i32 = 20; // Shadows the outer 'x'
        print_i32(x); // Prints 20
    }
    print_i32(x); // Prints 10
}
```

### Visibility Resolution Algorithm
Finding a symbol in the scope tree is only the first step. The compiler performs a strict visibility check based on the calling context:

1. **Alias Resolution**: If the found symbol is an alias, it is recursively resolved to its underlying target before visibility rules are applied.
2. **Same Unit Check**: If the symbol belongs to the same compilation unit (source file) as the caller, access is unconditionally granted, bypassing `pub` restrictions.
3. **Local Scope Check**: If the symbol is declared in a local block (e.g., inside a function, signified by a scope depth > 0 and no specific module unit attached), it is inherently visible to any nested blocks within it.
4. **Public Check**: If the symbol originates from an external module, the compiler checks the `is_pub` flag. If it is `false`, access is denied.
5. **Universe Fallback**: Built-in primitives (`i32`, `bool`) and keywords (belonging to the unitless "Universe" scope) are globally visible.

If a matching symbol is found but fails the visibility check (e.g., a private function in an imported module), the compiler treats it as if it were not visible, which may result in an "undefined symbol" error or cause it to continue searching parent scopes depending on the exact context.
