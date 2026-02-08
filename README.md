# Compiler V3

A custom compiler implemented in C for a strongly-typed, procedural language with syntax inspired by Rust and C. This project features a hand-written recursive descent parser, a robust semantic analysis phase, and a custom build system.

## 🚀 Features

### Language Features
* **Primitive Types**: `i32`, `i64`, `f32`, `f64`, `bool`, `char`, `string`.
* **Type Inference**: Automatic type inference for array sizes and implicit numeric promotions (e.g., `i32` to `f64`).
* **Complex Arrays**: 
    * Multi-dimensional arrays (e.g., `f64[3][3]`).
    * Inferred array sizes (e.g., `i32[] = {1, 2, 3}`).
    * Strict dimension and size validation during compilation.
* **Pointers & Memory**: Full pointer support (`*`, `&`, `**`), including arrays of pointers.
* **Functions**: 
    * Function declarations with return types.
    * Function pointers and higher-order functions (passing functions as arguments).
* **Control Flow**: `if`, `else`, `while`, `return`, `break`, `continue`.
* **Scope & Shadowing**: Block-scoped variables with support for variable shadowing.
* **Constants**: Compile-time constant evaluation and folding (`const x: i32 = 10 + 5;`).

### Compiler Architecture
* **Lexer**: Efficient tokenization handling identifiers, keywords, literals, and operators.
* **Parser**: Recursive descent parser generating a detailed Abstract Syntax Tree (AST).
* **Semantic Analyzer (Sema)**: 
    * Symbol table management with scope hierarchy.
    * Type checking with implicit casting logic.
    * Structural validation for initializer lists (e.g., matching nesting levels).
    * Constant expression evaluation.
* **Error Reporting**: Rich error messages with source highlighting, line/column numbers, and specific error codes (e.g., dimension mismatches).

---

## 🛠️ Building and Running

### Prerequisites
* GCC or Clang
* Make

### Compilation
To build the compiler:
```bash
make
```

To build and immediately run the compiler on the default input file (input/test.rs):
```bash
make run
```

### Usage
Run the compiled executable with a source file argument:
```bash
./out/compiler input/test.rs
```

### 📄 Language Syntax Examples
Variables and Constants
```rust
// Compile-time constants
const MAX_SIZE: i64 = 100;
const PI: f64 = 3.14159;

// Variable declarations
val: i32 = 10;
val_float: f32 = 10; // Implicit promotion: 10 (int) -> 10.0 (float)
```

Arrays and Matrices
```rust
// 1D Fixed Size
arr: i32[3] = {1, 2, 3};

// 1D Inferred Size
list: i32[] = {10, 20, 30, 40}; // Type becomes i32[4]

// Multi-dimensional Arrays (Matrix)
matrix: f64[2][2] = {
    {1.0, 0.0},
    {0.0, 1.0}
};

// Mixed types in initializers are auto-promoted based on the array base type
mixed_arr: f64[] = {1, 2.5, 3}; // Becomes {1.0, 2.5, 3.0}
```

Functions and Function Pointers
```rust
fn add(a: i32, b: i32) -> i32 {
    return a + b;
}

fn apply_op(a: i32, b: i32, op: fn(i32, i32) -> i32) -> i32 {
    return op(a, b);
}

fn main() {
    // Passing function as argument
    res: i32 = apply_op(10, 20, add);

    // Array of functions
    ops: (fn(i32, i32) -> i32)[] = { add, add };
}
```

Pointers
```rust
fn main() {
    x: i32 = 50;
    
    // Address-of operator
    ptr: i32* = &x;
    
    // Dereference operator
    val: i32 = *ptr;
    
    // Pointer to pointer
    ptr_ptr: i32** = &ptr;
}
```

📂 Project Structure
```txt
├── include/
│   ├── cli/            # Command line interface & metrics headers
│   ├── core/           # File I/O, Utilities, Source handling
│   ├── datastructures/ # Dynamic Arrays, Hash Maps, Arenas, Interners
│   ├── lexing/         # Lexer headers
│   ├── parsing/        # AST definitions & Parser headers
│   └── sema/           # Semantic Analysis, Type definitions & Reporting
├── src/
│   ├── cli/            # CLI implementation
│   ├── core/           # Core utilities implementation
│   ├── datastructures/ # Data structure implementations
│   ├── lexing/         # Lexer logic
│   ├── parsing/        # Recursive descent parser logic
│   ├── sema/           # Type checking, Symbol resolution, Expr validation
│   └── main.c          # Entry point
├── input/              # Test source files (.rs)
├── test/               # C-based Unit testing suite
├── makefile            # Build configuration
└── lang.bnf            # Formal grammar definition
```

### 🧪 Testing
The project includes a regression test harness located in the test/ directory. It validates the parser and semantic analyzer against various edge cases, including argument mismatches, complex type resolutions, and scope rules.

To run the tests
```bash
make test
```


