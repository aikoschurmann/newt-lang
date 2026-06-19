# The Newt Programming Language

Welcome to **Newt**! Newt is a statically-typed, procedural programming language designed for systems programming where predictability, speed, and safety are key. 

Newt is built from the ground up for developers who want absolute control over memory without the overhead of a garbage collector, and without the runtime hidden allocations found in many modern languages. This is just a passion project I work on in my spare time, so please be patient as the language is still in its early stages of development.

## Why Newt?

- **Zero Hidden Allocations**: What you write is what gets executed. Every single heap allocation is explicit in your code.
- **Modern Predictability**: Combines a clean, Rust-inspired syntax layout with the low-overhead procedural design of C.
- **Deterministic Resource Management**: Utilize scoped `defer` statements to guarantee cleanups occur immediately upon exiting scope blocks.
- **Modular Design**: A simple import and namespace resolution mechanism allows for clean project layout and code reuse.

---

## Language Tour by Example

Here is a complete, compile-ready example that demonstrates structs, methods, explicit arena allocation, control flow, type casting, and standard library interaction.

```rust
import std;
import std.math;

// Conveniently alias standard memory types
alias Allocator = std.mem.Allocator;
alias Arena = std.arena.Arena;

// Define a public Particle structure
pub struct Particle {
    x: f32;
    y: f32;
    vx: f32;
    vy: f32;
}

// Implement instance methods using standard static naming conventions
pub fn Particle.update(self: *Particle, dt: f32) -> void {
    self.x += self.vx * dt;
    self.y += self.vy * dt;
}

fn main() -> i32 {
    // 1. Initialize a lightweight memory Arena (1024 bytes capacity)
    arena: Arena = Arena.new(1024);
    defer { arena.destroy(); } // Bulk-reclaims all allocated memory when main exits

    a: Allocator = arena.get_allocator();

    // 2. Allocate contiguous memory for 3 Particle structures on the arena
    count: i64 = 3;
    particles: *Particle = @alloc(Particle, a, count);
    if particles == null {
        print("Memory allocation failed!\n");
        return 1;
    }

    // 3. Initialize particle state
    for (i: i32 = 0; i < count as i32; i += 1) {
        p: *Particle = &particles[i]; // Pointer index lookup
        p.x = (i as f32) * 10.0;
        p.y = 0.0;
        p.vx = 1.5;
        p.vy = 2.5;
    }

    // 4. Update coordinates and calculate distance from origin
    dt: f32 = 0.5;
    for (i: i32 = 0; i < count as i32; i += 1) {
        p: *Particle = &particles[i];
        p.update(dt);
        
        // Calculate Euclidean distance utilizing standard math utilities
        dist: f32 = std.math.sqrt(p.x * p.x + p.y * p.y);
        print("Particle ", i, " -> Position: (", p.x, ", ", p.y, "), Distance: ", dist, "\n");
    }

    return 0;
}
```

---

## Getting Started

### Prerequisites

You will need the following tools configured in your environment:
- **Clang** & **LLVM**
- **Make**

### Building the Compiler

Build both development and release versions of the Newt compiler:
```bash
make          # Creates the compiler binary in `./out/compiler`
make test     # Runs the test verification suite
```

### Compiling and Running a Program

To compile and immediately execute a Newt source file (`.nt`):
```bash
./out/compiler path/to/program.nt --run
```