# Memory Management in Newt

Newt employs explicit, manual memory management. There is no hidden garbage collector pausing your application, nor are there implicit allocations behind your back. You have complete, deterministic control over when and how memory is requested from the operating system and how it is released.

This guide provides an in-depth look into the tools Newt gives you to manage memory safely and efficiently: the `Allocator` interface, the `@alloc` and `@free` intrinsics, `Arena` allocators, and deterministic cleanup using `defer` statements.

---

## The Allocator Interface

At the core of Newt's memory management is the `std.mem.Allocator` struct. Instead of hardcoding memory allocations to a global heap allocator like `malloc` in C, Newt functions and data structures typically accept an `Allocator` as a parameter. This pattern, known as the "Allocator Pattern," allows callers to dictate *how* memory is allocated.

The `Allocator` struct is defined in `std.mem` as follows:

```rust
pub struct Allocator {
    ctx: *void;
    _alloc: fn(*void, i64) -> *void;
    _free: fn(*void, *void) -> void;
}
```

- **`ctx`**: A type-erased pointer to the internal state of the specific allocator implementation (e.g., a pointer to an `Arena` instance).
- **`_alloc`**: A function pointer that takes the context and a byte size, returning a generic pointer `*void` to the allocated memory.
- **`_free`**: A function pointer that takes the context and the pointer to free, releasing the memory.

The `Allocator` struct also provides convenient wrapper methods:

```rust
impl Allocator {
    pub fn alloc(self: *Allocator, size: i64) -> *void {
        return self._alloc(self.ctx, size);
    }

    pub fn alloc(self: *Allocator, count: i64, size: i64) -> *void {
        return self.alloc(count * size);
    }

    pub fn free(self: *Allocator, ptr: *void) -> void {
        self._free(self.ctx, ptr);
    }
}
```

The standard library provides a default heap allocator via `std.heap.allocator`, which maps to the system's standard malloc/free.

---

## The `@alloc` and `@free` Intrinsics

While you can call the `alloc` method on an `Allocator` directly, this returns a `*void` which requires manual casting and byte-size calculations. To provide type-safe allocations, the compiler provides the `@alloc` and `@free` intrinsics.

### Semantics of `@alloc`

The `@alloc` intrinsic expects three arguments:
1. **Type**: The type of the object(s) you want to allocate.
2. **Allocator**: An instance of an `Allocator`.
3. **Count**: The number of elements to allocate (as an `i64` or integer type).

```rust
import std.heap;

fn main() {
    // Allocate a single i32 integer.
    // The compiler knows the size of i32 at compile time and handles the allocation size automatically.
    single_ptr: *i32 = @alloc(i32, std.heap.allocator, 1);
    *single_ptr = 42;

    // Allocate an array of 10 i32 integers.
    // The compiler knows to treat this as a slice context if assigned to a slice.
    slice: i32[] = @alloc(i32, std.heap.allocator, 10);
    slice[0] = 100;
}
```

**Key Behaviors:**
- The compiler automatically computes the size of the provided type `T` at compile time.
- It calculates the total allocation size by multiplying the size of `T` by the `Count` argument.
- It invokes the provided `Allocator`'s `_alloc` function behind the scenes.
- It returns a strongly-typed pointer `*T` or a slice `T[]` depending on the assignment context. No manual casting is necessary.

### Semantics of `@free`

The `@free` intrinsic is the counterpart to `@alloc`. It takes two arguments: the allocator instance and the pointer (or slice) to free.

```rust
// Freeing the previously allocated memory
@free(std.heap.allocator, single_ptr);
@free(std.heap.allocator, slice);
```

---

## Arena Allocators (`std.arena.Arena`)

While general-purpose heap allocators are highly flexible, they can be slow and suffer from fragmentation over time. For many workloads, especially when processing trees, graphs, or handling request-scoped data, an **Arena Allocator** is a vastly superior choice.

An `Arena` is a bump allocator. It pre-allocates a large block of memory (the backing buffer) and satisfies allocation requests by simply advancing an internal offset pointer.

### Arena Internal Mechanics

```rust
pub struct Arena {
    buffer: *char; // raw bytes
    capacity: i64;
    offset: i64;
    backing_allocator: std.mem.Allocator;
}
```

When you request memory from an `Arena`:
1. It checks if `offset + requested_size > capacity`.
2. If it fits, it calculates the return pointer as `buffer + offset`.
3. It increments `offset` by `requested_size`.
4. It returns the pointer.
5. If it does *not* fit, it returns `null` (Out of Memory).

Because allocating memory is just simple integer addition, it is incredibly fast compared to finding a free block in a standard heap.

### Bulk Reclaim

The most significant advantage of an `Arena` is how it handles deallocation. The `free` method on an `Arena` is completely empty—a **no-op**:

```rust
pub fn free(self: *Arena, ptr: *void) -> void {
    // Intentionally left blank.
}
```

Instead of freeing individual allocations, memory is reclaimed in bulk. You either:
- Call `arena.reset()`, which simply sets `offset = 0`, instantly invalidating all previous allocations and making the entire buffer available again for new data.
- Call `arena.destroy()`, which uses the `backing_allocator` to free the entire underlying buffer.

### Arena Example

```rust
import std.arena;
import std.heap;
import std.mem;

fn process_data() {
    // Create an arena with a 1MB capacity backed by the standard heap
    my_arena: Arena = Arena.new(1024 * 1024);
    
    // Obtain the generic Allocator interface for this arena
    alloc: std.mem.Allocator = my_arena.get_allocator();

    // Perform many fast allocations
    for (i: i32 = 0; i < 1000; i = i + 1) {
        node: *i32 = @alloc(i32, alloc, 1);
        *node = i;
    }

    // Free everything at once! No need to call @free on the individual nodes.
    my_arena.destroy();
}
```

---

## Resource Cleanup with `defer` Statements

Manual memory management requires diligence. If you allocate memory or acquire a resource, you must remember to free it before returning from the function. In complex functions with early returns, multiple error conditions, or loop breaks, ensuring every resource is freed correctly becomes incredibly error-prone.

Newt solves this with the `defer` statement.

A `defer` block schedules a block of code to be executed when the current lexical scope exits, regardless of *how* it exits.

### LIFO Execution Stack

Defers are pushed onto a hidden stack at runtime. When the scope exits, the defers are popped and executed in **Last-In, First-Out (LIFO)** order.

```rust
fn main() -> i32 {
    defer { /* Executes Second */ print("World
"); }
    defer { /* Executes First */ print("Hello "); }
    return 0;
}
// Conceptual Output: Hello World
```

### Execution on Scope Exits

Defers execute when control flow leaves the block they are defined in. This applies to normal block completion, `return` statements, and loop control flow (`break` and `continue`).

**Nested Scopes:**
When a nested scope exits, only the defers within that scope are executed.

```rust
fn nested() -> i32 {
    res: i32 = 0;
    {
        defer { res = res + 100; }
        if (res == 0) {
            defer { res = res + 10; }
            return res + 1; // Evaluates to 1, then inner defer runs (+10), then outer defer runs (+100).
        }
    }
    return res;
}
```

**Loop Control Flow (`break` and `continue`):**
If you `break` or `continue` inside a loop, any `defer` statements registered *during that specific loop iteration* will be executed before the jump occurs. 

```rust
fn loop_defer() -> i32 {
    res: i32 = 0;
    i: i32 = 0;
    while (i < 3) {
        defer { res = res + 1; }
        
        if (i == 1) {
            defer { res = res + 10; }
            i = i + 1;
            continue; // Executes inner defer (+10), then outer defer (+1)
        }
        
        i = i + 1;
        // Natural end of loop iteration executes outer defer (+1)
    }
    return res; // Returns 13
}
```
*Iteration `i=0`*: Outer defer (+1) executes at natural loop end. `res` becomes 1.
*Iteration `i=1`*: Inner defer (+10) then Outer defer (+1) execute due to `continue`. `res` becomes 12.
*Iteration `i=2`*: Outer defer (+1) executes at natural loop end. `res` becomes 13.

### Return Value Overriding

When a `return <expr>;` statement is encountered:
1. The compiler evaluates the return expression.
2. The result is saved to a hidden return slot.
3. The `defer` blocks are executed.
4. The function actually returns the value in the hidden return slot.

However, if a `defer` block explicitly issues its own `return` statement, it will **override** the original return value. This is powerful for enforcing failure states or altering returns during cleanup.

```rust
fn test_override() -> i32 {
    defer { return 99; }
    return 42; 
}
// Calling test_override() returns 99, not 42!
```

---

## Complex Memory Management Example

Let's combine all these concepts—`@alloc`, Arenas, Loops, early returns, and `defer`—into a realistic, robust code snippet.

```rust
import std.mem;
import std.arena;
import std.heap;

struct Node {
    val: i32;
    next: *Node;
}

fn process_nodes(count: i32) -> i32 {
    // 1. Initialize our Arena.
    // Capacity of 1MB, backed by the global heap.
    temp_arena: Arena = Arena.new(1024 * 1024);
    
    // 2. GUARANTEE the arena is destroyed when this function exits.
    // Whether we return successfully, naturally, or early, this defer will run.
    defer { temp_arena.destroy(); }

    alloc: std.mem.Allocator = temp_arena.get_allocator();
    sum: i32 = 0;
    
    // 3. Allocate some working memory. 
    // We don't need to manually free these because the arena bulk-frees!
    for (i: i32 = 0; i < count; i = i + 1) {
        node: *Node = @alloc(Node, alloc, 1);
        
        // Out of Memory check from our Arena
        if (node == null as *Node) {
            // Early return! 
            // The defer block will trigger and destroy the arena cleanly.
            return -1; 
        }
        
        node.val = i * 10;
        
        // 4. We can use scoped defers inside the loop for complex logic
        {
            // Imagine we acquire a file handle or a lock here
            // lock();
            // defer { unlock(); }
            
            if (i % 2 == 0) {
                // Skips to the next iteration. 
                // Any inner loop defers would trigger here before the jump.
                continue; 
            }
        }
        
        sum = sum + node.val;
        
        if (sum > 500) {
            // Complex exit:
            // 1. Evaluates sum to be returned.
            // 2. Executes outer function defer (temp_arena.destroy()).
            // 3. Returns the sum.
            return sum;
        }
    }
    
    return sum; // Also triggers temp_arena.destroy() on the way out.
}
```

## Best Practices

1. **Allocator Injection**: Always pass an `Allocator` to functions and structs that need memory. Avoid using `std.heap.allocator` directly inside your core logic; this allows callers to use an `Arena` when appropriate, enhancing performance.
2. **Defer Immediately**: As soon as you acquire a resource or allocate memory (that isn't arena-managed), write the `defer { @free(...) }` statement on the very next line. This prevents leaks during code refactoring and ensures cleanup is always tied to resource acquisition.
3. **Use Arenas for Scoped Data**: If you find yourself allocating many small objects that all share the same lifetime (e.g., handling an HTTP request, parsing an abstract syntax tree), use an `Arena`. It eliminates memory fragmentation and turns cleanup into a single, instant operation.
