# Getting Started

Welcome to Newt! This guide covers the basics of compiling and running Newt programs.

## Prerequisites

You need the following tools installed:
- **Clang** & **LLVM**
- **Make**

## Building the Compiler

Build the compiler from source by running `make` in the root directory:

```bash
make
```

This creates the compiler binary at `./out/compiler`. You can also run the full test suite to ensure everything compiled correctly:

```bash
make test
```

## Running Your First Program

Create a file named `hello.nt` and write the following code:

```rust
fn main() -> i32 {
    print("Hello, World!\n");
    return 0;
}
```

To compile and execute it immediately, pass the file to the compiler using the `--run` flag:

```bash
./out/compiler hello.nt --run
```
