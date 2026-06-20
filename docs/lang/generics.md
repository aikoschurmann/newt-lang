# Generics

The language supports a powerful generics system, allowing types and functions to be parameterized over other types. This provides strong static typing without the need for type erasure, enabling the compiler to generate highly optimized, specialized code for each type instantiation.

## Overview

Generics in the language are implemented using **monomorphization**. This means that for every unique combination of type arguments provided to a generic entity (a struct or a function), the compiler generates a completely separate, concrete version of that entity. This approach ensures zero runtime overhead, as generic code is transformed into highly specific machine code tailored to the exact types being used.

The monomorphization process is driven by the semantic analyzer (Sema), which acts on the Abstract Syntax Tree (AST). It performs AST copying and name mangling to create concrete representations of generic templates.

---

## Generic Functions

Functions can be parameterized over one or more types. Type parameters are specified within angle brackets `< >` immediately following the function name.

### Syntax

```rust
fn identity<T>(x: T) -> T {
    return x;
}

fn pair<T, U>(first: T, second: U) -> T {
    return first;
}
```

### Instantiation

When a generic function is called, the type arguments must be explicitly provided in angle brackets. The compiler will substitute the type parameters with the provided concrete types and type-check the resulting specialized function.

```rust
fn main() -> i32 {
    // Instantiates identity with T = i32
    val: i32 = identity<i32>(42);
    
    // Instantiates pair with T = f64, U = i32
    first_val: f64 = pair<f64, i32>(3.14, 100);
    
    return val;
}
```

### Monomorphization and Name Mangling

When `identity<i32>` is encountered, Sema performs the following steps:
1.  **Template Lookup**: The generic template `identity` is retrieved from the semantic environment.
2.  **AST Cloning**: The entire AST for the `identity` function is copied.
3.  **Type Substitution**: A new semantic scope is created where the type parameter `T` is bound to the concrete type `i32`. The cloned AST is then type-checked within this scope.
4.  **Name Mangling**: A unique symbol name is generated to avoid linker collisions. The mangling scheme concatenates the base function name with the mangled names of its type arguments, separated by double underscores (`__`).
    For example, `identity<i32>` becomes `identity__i32`.

This means that calling `identity<i32>` and `identity<f64>` results in two distinct functions being compiled into the final binary.

---

## Generic Structs

Structs can also be parameterized, enabling the creation of generic data structures like linked lists, dynamically sized arrays, and generic wrappers.

### Syntax

```rust
struct Box<T> {
    val: T;
}

struct Pair<T, U> {
    first: T;
    second: U;
}

struct Node<T> {
    val: T;
    next: *Node<T>;
}
```

### Instantiation

Generic structs are instantiated by providing the concrete types in angle brackets where the type is used.

```rust
fn main() -> i32 {
    // Instantiates Box with T = i32
    b: Box<i32>;
    b.val = 42;
    
    // Instantiates Node with T = f64
    n1: Node<f64>;
    n2: Node<f64>;
    n1.val = 1.0;
    n1.next = &n2;
    
    return b.val;
}
```

### Monomorphization via AST Copying

The instantiation of generic structs is slightly more complex than functions, as it involves the semantic analyzer's queueing system.

1.  **Queueing**: When a generic instantiation expression (e.g., `Box<i32>`) is encountered during type checking, the `instantiate_generic_struct` function is invoked. Instead of immediately monomorphizing the struct, it creates a `MonoJob` and adds it to the `mono_queue`. It returns an incomplete `TYPE_GENERIC_INST` placeholder type.
2.  **Deduplication**: Before queueing, the compiler checks the `mono_queue` to see if a job for the same struct template and type arguments already exists. If it does, the existing placeholder type is returned, preventing redundant instantiations and infinite loops in self-referential types.
3.  **Draining**: Periodically (e.g., at the end of a semantic pass), the `drain_mono_queue` function is called. It processes each queued `MonoJob`.
4.  **Instantiation**: For each job, a new semantic scope is created, binding the type parameters (e.g., `T`) to the concrete arguments (e.g., `i32`). The compiler iterates over the struct's fields, copying their AST representations and resolving their types within the new scope.
5.  **Caching**: Once instantiated, the concrete struct type is cached on the `TYPE_GENERIC_INST` node (`inst_type->as.generic_inst.concrete_type = concrete_struct;`). Subsequent lookups for `Box<i32>` will quickly return this cached concrete type.

### Struct Name Mangling

Like functions, instantiated structs receive mangled names based on their base name and type arguments.
`Box<i32>` becomes `Box__i32`. `Pair<i32, f64>` becomes `Pair__i32__f64`.
Nested generics are recursively mangled: `Outer<Inner<i32>>` becomes `Outer__Inner__i32`.

### Cyclic Generic Memory Constraints

The language strictly enforces layout constraints for value-type structs to prevent infinite memory size requirements. A struct cannot contain itself by value.

```rust
// ERROR: Recursive struct definition (infinite size)
struct BadNode<T> {
    val: T;
    next: BadNode<T>; 
}
```

However, generic structs can safely contain pointers to themselves, as pointers have a fixed size regardless of the pointee type. This allows for valid recursive data structures like linked lists.

```rust
// VALID: Contains a pointer to itself
struct Node<T> {
    val: T;
    next: *Node<T>; 
}
```

The compiler employs cycle detection during semantic analysis (`check_struct_cycle`). It maintains a traversal path and issues a `TE_INCOMPLETE_TYPE` error if a struct reaches itself by value. For generics, the `drain_mono_queue` process also includes safeguards against infinite cyclic instantiations using depth limits (capped at 64 nested instantiations) and the `SYMBOL_FLAG_COMPUTING` flag.

---

## Generic Methods and `impl` Blocks

Methods can be implemented for generic structs using `impl` blocks. The `impl` block itself must declare the type parameters it uses.

### Syntax

```rust
struct Value<T> {
    val: T;
}

impl<T> Value<T> {
    // Instance method (takes 'self')
    fn get(self: *Value<T>) -> T {
        return self.val;
    }

    // Instance method modifying state
    fn set(self: *Value<T>, v: T) {
        self.val = v;
    }
}
```

### Static vs. Instance Methods

The language distinguishes between static and instance methods based on the presence of a `self` parameter.
-   **Instance Methods**: Must have a first parameter named `self` which is a pointer to the enclosing struct type (`*Value<T>`).
-   **Static Methods**: Do not have a `self` parameter and act as associated functions scoped to the struct namespace.

### Resolving Methods in Generic `impl` Blocks

When a method is called on an instantiated generic struct (e.g., `v.get()`), the compiler invokes `instantiate_generic_method`.

1.  **Resolution**: The base generic struct type is identified. The compiler looks up the method definition within the generic `impl` blocks associated with the base struct.
2.  **Validation**: It ensures the type arguments provided to the struct match the expected type parameters in the `impl` block.
3.  **AST Cloning**: The AST node for the specific method (e.g., `get`) is cloned.
4.  **Monomorphization Scope**: A new scope is created, inheriting from the global scope where the struct was originally defined. The type parameters (e.g., `T`) are bound to the concrete arguments used by the instantiated struct (e.g., `i32`).
5.  **Type Checking**: The cloned method AST is type-checked within this specialized scope. The `self` pointer type naturally resolves to the correct concrete struct type (`*Value<i32>`).
6.  **Caching**: The newly monomorphized method symbol is added to the `methods` hashmap of the *concrete* struct (`concrete_struct->as.struct_type.methods`). This ensures that `v.get()` for `Value<i32>` and `v.get()` for `Value<f64>` resolve to distinct, specialized method instances, and subsequent calls to the same method on the same concrete type reuse the cached instance.

### Method Name Mangling

Monomorphized methods receive heavily mangled names to link correctly. The mangling scheme includes the base struct name, the mangled type arguments, and the method name.
For example, the `get` method on `Value<i32>` will be mangled as `Value__i32_get`.
This ensures that the LLVM backend generates distinct functions for `Value<i32>::get` and `Value<f64>::get`.

---

## Aliases and Generics

Type aliases interact seamlessly with generics, allowing for shorthand names for specific instantiations.

```rust
struct Box<T> { 
    val: T; 
}

// Alias for a specific instantiation
alias IntBox = Box<i32>;

fn main() -> i32 {
    b: IntBox; // Equivalent to b: Box<i32>;
    b.val = 123;
    return b.val;
}
```

When an alias to a generic instantiation is encountered, the type checker resolves the alias to the underlying generic instantiation expression and processes it through the normal `mono_queue` pipeline, ensuring consistent deduplication and instantiation.

## Advanced Examples

### Nested Generic Instantiations

Generics can be deeply nested, and the monomorphization engine recursively unwraps and instantiates them.

```rust
struct Inner<T> { 
    val: T; 
}

struct Outer<T> { 
    inner: T; 
}

fn main() -> i32 {
    // Instantiates Outer with T = Inner<i32>
    // Which recursively instantiates Inner with T = i32
    o: Outer<Inner<i32>>;
    
    o.inner.val = 456;
    return o.inner.val;
}
```
The mangled name for the `Outer` struct in this case would be `Outer__Inner__i32`.

### Multiple Type Parameters

```rust
struct DictionaryEntry<K, V> {
    key: K;
    value: V;
}

impl<K, V> DictionaryEntry<K, V> {
    fn new(k: K, v: V) -> DictionaryEntry<K, V> {
        entry: DictionaryEntry<K, V>;
        entry.key = k;
        entry.value = v;
        return entry;
    }
    
    fn get_key(self: *DictionaryEntry<K, V>) -> K {
        return self.key;
    }
}
```
When `DictionaryEntry<i32, f64>` is used, a type with the mangled name `DictionaryEntry__i32__f64` is created, and its methods are specialized accordingly.

## Limitations

- **No Type Erasure Runtime Reflection**: Because generics are monomorphized away during compilation, there is no runtime representation of generic type parameters. You cannot query the runtime type of `T` using reflection mechanisms typical in languages with type erasure (like Java).
- **Code Bloat**: Extensive use of generics with many different concrete types can lead to increased binary size (code bloat) due to the duplication of code for each instantiation. However, this is a standard trade-off for the performance benefits of zero-cost abstractions.
- **Instantiation Depth Limit**: To prevent compiler hangs from pathological recursive generic patterns, the semantic analyzer enforces a hard limit on generic instantiation depth (currently `64`). Exceeding this triggers a `TE_INSTANTIATION_DEPTH` error.
