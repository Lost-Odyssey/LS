# LS Future Ideas — Exploration Backlog

This document collects ideas that are not yet planned in detail but are worth
tracking. Each section has a rough feasibility and effort assessment so the
most valuable ideas can be promoted to a full plan document when the time comes.

---

## 1. User-Defined Generics

**Motivation**: `Option(T)` and `Result(T,E)` are compiler-synthesised generics.
User code cannot define `struct List<T>` or `fn map<T,U>(vec(T), Block(T)->U) -> vec(U)`.

**Design sketch**:

```ls
// Generic struct
struct Pair<A, B> {
    A first
    B second
}

// Generic function
fn map<T, U>(vec(T) v, Block(T) -> U f) -> vec(U) {
    vec(U) result = []
    for i in 0..v.length {
        result.push(f(v[i]))
    }
    return result
}

// Usage (explicit type args, or inferred)
Pair(int, string) p = Pair { first: 42, second: "hello" }
vec(int) squares = map(nums, |x| x * x)
```

**Implementation**:
- Add `<TypeParam, ...>` syntax to `struct` and `fn` declarations.
- Checker: type parameter substitution at instantiation site (monomorphisation).
- Codegen: emit a separate LLVM function per concrete instantiation (same as `Option(T)`).
- Type inference: unify type parameters from argument types (Hindley-Milner style).
- Constraints (Phase 2): `<T: Comparable>`, `<T: Printable>` — trait-like bounds.

**Effort**: 21–30 days (large, affects checker + codegen deeply)
**Value**: ★★★★★ — transforms the language's expressiveness

---

## 2. Concurrency & Parallelism

**Motivation**: LS has no thread model. HPC, servers, and UIs all benefit from concurrency.

### Option A: OS threads with message passing (Go-inspired)

```ls
import sync

// Spawn a thread
sync.Thread t = sync.spawn(|| {
    int result = heavy_compute()
    return result
})
int r = sync.join(t)     // blocks until thread finishes, returns result

// Channel (unbuffered and buffered)
sync.Chan(int) ch = sync.channel(int, capacity: 10)
sync.send(ch, 42)
int v = sync.recv(ch)     // blocks if empty

// Select (wait on multiple channels):
sync.select {
    case v = sync.recv(ch1): print(v)
    case sync.send(ch2, 99): print("sent")
    default: print("no-op")
}
```

### Option B: Green threads / coroutines (Lua-inspired, no OS threads)

```ls
fn producer(sync.Chan(int) ch) {
    for i in 0..100 { sync.yield(ch, i) }
}
sync.Coro c = sync.coroutine(producer)
int v = sync.resume(c)    // runs producer until next yield
```

**Design decision**: Option A is the pragmatic choice. LLVM supports `pthreads` on all
platforms. The key challenge is making the move/borrow system thread-safe (no shared
mutable references across threads without explicit synchronisation).

**Effort**: 21–30 days
**Value**: ★★★★☆

---

## 3. WebAssembly Target

**Motivation**: LS compiles to native code via LLVM. LLVM has a mature WASM backend.
Adding `--target wasm32-wasi` would let LS programs run in the browser, on edge
runtimes (Cloudflare Workers, Deno, Node.js), and in embedded WASM hosts.

**Required changes**:
- CMakeLists: add `WebAssembly` to `LLVM_TARGETS_TO_BUILD` (requires re-building LLVM or installing LLVM with WASM target).
- `main.c` compile path: accept `--target wasm32-wasi`, pass appropriate triple to LLVM.
- `runtime/`: WASI versions of `malloc`/`free`/`write` (provided by WASI SDK libc).
- stdlib: `io.ls` needs WASI file API variants (no `fopen`, uses `wasi_fd_*`).
- FFI: `load()` is not available in WASM (no dynamic linking); emit compile-time error.

**Demo target**: `ls compile --target wasm32-wasi hello.ls -o hello.wasm`

**Effort**: 10–14 days (LLVM WASM backend is mature; most work is runtime adaptation)
**Value**: ★★★☆☆ (niche but impressive)

---

## 4. Package Manager (`ls pkg`)

**Motivation**: LS has no way to distribute or consume third-party libraries.

**Design**:

```bash
ls pkg init                        # create ls.toml in current directory
ls pkg add github.com/user/mylib   # add dependency
ls pkg update                      # update all dependencies
ls pkg build                       # download deps, build project
```

**`ls.toml`**:

```toml
[package]
name = "myapp"
version = "1.0.0"

[dependencies]
"github.com/alice/json-utils" = "1.2.0"
"github.com/bob/csv-reader"  = "~2.0"
```

**Implementation**:
- `ls pkg` sub-command in `main.c`
- `ls.toml` parser (TOML is simple enough for a pure-LS parser)
- Git-based dependency fetching via `process.run("git clone ...")`
- Module path resolution: `~/.ls/pkg/<name>@<version>/` cache
- Build system integration: dependency modules added to the module search path

**Effort**: 14–21 days
**Value**: ★★★★☆ (essential for ecosystem growth)

---

## 5. Compile-Time Execution (`comptime`)

**Motivation**: Constants and lookup tables that can be computed at compile time
should not waste runtime cycles.

```ls
comptime int TABLE_SIZE = 256
comptime array(f64, 8) SIN_TABLE = comptime {
    array(f64, 8) t = array(f64, 8)
    for i in 0..8 {
        t[i] = math.sin(i as f64 * math.PI / 4.0)
    }
    return t
}

// Use at runtime — already computed in the binary
f64 v = SIN_TABLE[3]
```

**Implementation**:
- `comptime` expressions are evaluated by an interpreter running over the AST
  before codegen (no LLVM needed for comptime).
- Supported: arithmetic, array/vec/map literals, `for` loops, `math.*` calls.
- Result embedded in LLVM IR as a global constant (`LLVMAddGlobal` + `LLVMSetInitializer`).

**Effort**: 14–21 days
**Value**: ★★★☆☆

---

## 6. Debug Tooling (DWARF + DAP)

**Motivation**: `ls.exe` produces binaries with no debug info. Adding DWARF enables
GDB/LLDB breakpoints and VS Code debugging.

### Phase D.1 — DWARF debug info emission

- Enable `LLVMSetCurrentDebugLocation2` and LLVM DIBuilder API.
- Emit file/line/column for every statement.
- Emit variable names and types in DILocalVariable.
- AOT only (JIT debug info is harder and lower priority).
- Build flag: `ls compile -g input.ls -o out`

### Phase D.2 — VS Code DAP adapter

- A small Node.js / LS-compiled adapter that speaks the Debug Adapter Protocol.
- Wraps GDB/LLDB as the actual debugger backend.
- Enables breakpoints, variable inspection, step/continue in VS Code.

**Effort**: D.1: 10–14 days · D.2: 14–21 days
**Value**: ★★★★☆ (greatly improves developer experience)

---

## 7. Operator Overloading

**Motivation**: Custom numeric types (`complex`, `vec2`, matrix) need `+` `-` `*` `/`.

```ls
struct Vec2 {
    f64 x
    f64 y
}

impl Vec2 {
    fn +(Vec2 other) -> Vec2 { return Vec2 { x: self.x + other.x, y: self.y + other.y } }
    fn *(f64 s)      -> Vec2 { return Vec2 { x: self.x * s,       y: self.y * s       } }
    fn ==(Vec2 other) -> bool { return self.x == other.x && self.y == other.y }
}

Vec2 a = Vec2 { x: 1.0, y: 2.0 }
Vec2 b = Vec2 { x: 3.0, y: 4.0 }
Vec2 c = a + b     // calls a.+(b)
Vec2 d = c * 2.0   // calls c.*(2.0)
```

**Implementation**:
- Checker: if `a + b` where `a` is a struct, look for `impl` method named `+`.
- Operators: `+` `-` `*` `/` `%` `==` `!=` `<` `>` `<=` `>=` `[]` (index) `[]=` (index-assign).
- No implicit conversions — both sides must match.

**Effort**: 5–7 days
**Value**: ★★★★☆ (especially important for HPC numeric types)

---

## 8. REPL Improvements

**Current REPL limitations**: no history persistence, no tab completion, no
multi-line input, no syntax highlighting.

**Improvements**:

```
ls> fn add(int a, int b) -> int {    // multi-line: detects unclosed {
...     return a + b
... }
ls> add(3, 4)
7
ls> <Tab>          // complete: add  array  as  ...
ls> :type add      // REPL command: print inferred type
fn(int, int) -> int
ls> :history       // show recent expressions
ls> :clear         // clear all defined symbols
```

**Implementation**:
- Use `linenoise` or `editline` (both small, MIT, C99) for readline-like input.
- Multi-line detection: track unmatched `{` / `(` / `[` in the scanner.
- Tab completion: query the symbol table for all known names with the given prefix.
- Persistent history: `~/.ls_history` file.

**Effort**: 5–8 days
**Value**: ★★★☆☆

---

## 9. AOT Optimisation: PGO and LTO

**Profile-Guided Optimisation (PGO)**:

```bash
# Step 1: instrument build
ls compile --pgo-instrument input.ls -o out_instrumented
./out_instrumented <typical-input>      # generates profraw data

# Step 2: optimised build using profile
ls compile --pgo-use input.ls -o out_optimised
```

Maps to LLVM's `-fprofile-instr-generate` / `-fprofile-instr-use` pipeline.

**Link-Time Optimisation (LTO)**:

```bash
ls compile --lto input.ls -o out      # enables LLVM ThinLTO
```

Both are purely `LLVMPassManagerBuilder` / `LLVMRunPassManager` configuration changes.

**Effort**: PGO: 3–5 days · LTO: 1–2 days
**Value**: ★★☆☆☆ (nice for power users; most gains from algorithmic work)

---

## 10. GPU / Accelerator Target (long-term research)

**Motivation**: LLVM supports NVPTX (NVIDIA) and AMDGPU backends.
A `@gpu` annotation on a function could emit GPU kernel code.

```ls
@gpu
fn vector_add(vec(f32) a, vec(f32) b, vec(f32) out, int n) {
    int i = gpu.thread_id()
    if i < n { out[i] = a[i] + b[i] }
}

// Host call:
gpu.launch(vector_add, grid: n/256, block: 256, args: [a, b, out, n])
```

This is research-level work. The memory model (host vs device), data transfer,
and kernel compilation are all substantial engineering challenges.

**Effort**: 60–90 days (research prototype)
**Value**: ★★★☆☆ (very high if successful)

---

## Priority Matrix

| Idea | Effort | Value | Recommended order |
|------|--------|-------|-------------------|
| Operator overloading | 5–7 d | ★★★★☆ | 1st — enables HPC types |
| User-defined generics | 21–30 d | ★★★★★ | 2nd — language cornerstone |
| Debug tooling (DWARF) | 10–14 d | ★★★★☆ | 3rd — developer experience |
| Package manager | 14–21 d | ★★★★☆ | 4th — ecosystem |
| Concurrency | 21–30 d | ★★★★☆ | 5th — advanced use cases |
| REPL improvements | 5–8 d | ★★★☆☆ | alongside above |
| Compile-time execution | 14–21 d | ★★★☆☆ | with generics |
| WebAssembly target | 10–14 d | ★★★☆☆ | later |
| PGO / LTO | 4–7 d | ★★☆☆☆ | easy win, late stage |
| GPU target | 60–90 d | ★★★☆☆ | long-term research |
