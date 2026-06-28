# LS (LLVM Script)

A compiled language that pairs the familiarity of C with the expressiveness of
Ruby, backed by Rust-style move semantics and RAII — memory safety without a
garbage collector.

LS compiles to native code through an LLVM backend, with both AOT and JIT
execution from a single statically-linked binary.

## Key Characteristics

- **Move semantics + RAII** — ownership is tracked at compile time; string, vec,
  map, and struct values free themselves automatically. No GC, no manual free.
- **C + Ruby syntax** — C-style type annotations and block structure, Ruby-style
  `impl` blocks, trailing closures, and operator overloading (`fn +`).
- **Algebraic types** — tagged unions (`enum`) with exhaustive `match`, OR-patterns,
  borrowed payloads, and `Option(T)` / `Result(T, E)` built in.
- **First-class closures** — by-copy, by-move, and by-ref capture; deep-copied
  environments let closures outlive their creator.
- **Dynamic FFI** — load shared libraries at runtime and call `extern fn` directly.
- **Built-in tooling** — bundled memory checker (`--memcheck`), REPL with incremental
  JIT compilation, and a module system with namespace isolation.
- **Self-hosted stdlib** — JSON, Markdown, HTML, time, and math, all written in LS.

## Build

Requires CMake >= 3.20 and a static LLVM build (18 or newer).

**Windows** (primary, fully tested):

```
cmake -B build -G "Visual Studio 17 2022" -A x64 -DLLVM_DIR="C:\llvm\lib\cmake\llvm"
cmake --build build --config Release
```

**Linux** (experimental — not yet fully validated):

```
cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Release -DLLVM_DIR=/usr/lib/llvm-18/cmake
cmake --build build
```

> Full build configuration is in [docs/build.md](docs/build.md).

## Run

```
ls compile input.ls -o out.exe   # AOT
ls run input.ls                  # JIT
ls run --memcheck input.ls       # JIT + memory checker
ls repl                          # REPL
```

## License

MIT — © 2026 Yang Liu. See [LICENSE](LICENSE).
