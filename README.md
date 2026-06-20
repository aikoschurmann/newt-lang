# The Newt Programming Language

Welcome to **Newt**, a statically-typed, procedural programming language designed for systems programming. Newt prioritizes predictability, explicit memory management, and safe abstractions without the runtime overhead of a garbage collector.

*Note: Newt is currently a personal passion project in active development. While the compiler is highly functional, some features are experimental and the language ecosystem is still evolving. Things are not perfect yet, but progress is steady!*

## Overview

Newt is built from the ground up for developers who demand absolute control over memory and system resources. It combines the predictable, low-overhead nature of C with modern language features like generic programming, deterministic scoped cleanup (`defer`), and zero hidden allocations.

## A Quick Taste

Here is what Newt code looks like:

```rust
import std.arena;
alias Arena = std.arena.Arena;

pub struct Box<T> {
    pub value: T;
}

impl<T> Box<T> {
    pub fn new(val: T) -> Box<T> {
        return Box<T> { value: val };
    }
}

fn main() -> i32 {
    arena: Arena = Arena.new(1024);
    defer { arena.destroy(); } 

    my_box: Box<i32> = Box<i32>.new(42);
    print("The generic box holds: ", my_box.value, "\n");
    return 0;
}
```

## Language Documentation

To learn how to use Newt in depth, please refer to the following official guides:

- [Getting Started](docs/lang/getting_started.md)
- [Basic Syntax & Control Flow](docs/lang/basics.md)
- [Type System (Primitives, Arrays, Slices)](docs/lang/types.md)
- [Type Conversions (Promotions & Cast Rules)](docs/lang/conversions.md)
- [Explicit Memory & Defer](docs/lang/memory.md)
- [Generics Deep Dive](docs/lang/generics.md)
