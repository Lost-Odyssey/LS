# LS Profiling & Timing — Implementation Plan

## Overview

Add Julia-style profiling capabilities to LS, from low-level CPU cycle counting
up to a sampling profiler with Linux `perf` integration.

Planned in four independent phases that can be shipped separately.

---

## Phase P.1 — Low-level Timing Primitives (`perf` module built-in)

### Goal

Expose a `perf` built-in module that wraps hardware/OS timing at near-zero cost.

### API Design

```ls
import perf

// --- Monotonic wall clock (nanoseconds since some unspecified epoch) ---
i64 t0 = perf.now()          // CLOCK_MONOTONIC via clock_gettime (Linux)
                              // QueryPerformanceCounter  (Windows)

// --- CPU cycle counter (no serialisation) ---
i64 c0 = perf.rdtsc()        // x86: RDTSC  (out-of-order, cheapest)
i64 c1 = perf.rdtscp()       // x86: RDTSCP (serialises, slightly heavier)

// --- Elapsed helpers ---
i64 ns = perf.elapsed_ns(t0)          // perf.now() - t0
f64 ms = perf.elapsed_ms(t0)          // elapsed_ns / 1_000_000.0
f64 s  = perf.elapsed_s(t0)           // elapsed_ns / 1_000_000_000.0
```

### Implementation — codegen paths

| Function | Windows IR | Linux IR |
|----------|-----------|----------|
| `perf.now()` | `call i64 @QueryPerformanceCounter(i64* slot)` then scale by freq | `call i32 @clock_gettime(i32 1, %timespec* slot)` → `sec*1e9+nsec` |
| `perf.rdtsc()` | `call i64 @llvm.x86.rdtsc()` | same |
| `perf.rdtscp()` | `call {i64,i32} @llvm.x86.rdtscp(i32* aux)`, extract i64 | same |
| `perf.elapsed_ns()` | `perf.now() - t0` | same |

All functions are **inlined at call site** via direct LLVM intrinsic emission —
no libc call overhead for rdtsc/rdtscp.

### Struct `TimeSpec` (optional, for raw access)

```ls
import perf

perf.TimeSpec ts = perf.clock_gettime(perf.CLOCK_MONOTONIC)
// ts.sec : i64
// ts.nsec: i64
```

### Files to create/modify

- `src/builtins_perf.c` — type registration (checker side)
- `src/builtins_perf_cg.c` — IR emission
- `src/codegen.c` — dispatch hook in `check_builtin_module_call`
- `CMakeLists.txt` — add the two new `.c` files to `LS_SOURCES`

### Effort estimate: 3–5 days

---

## Phase P.2 — `@time` Expression Sugar

### Goal

Julia-style `@time expr` that prints execution time and returns the value,
without requiring manual `perf.now()` calls.

### Syntax

```ls
// Wraps any expression; returns the expression's value.
int result = @time fib(40)
// prints: [time] fib(40)  elapsed: 342.17 ms   (or ns/us/ms/s, auto-selected)

// Works in JIT REPL too:
@time sort_big_vec(data)
// prints wall time + cycle count when CG_DEBUG=1
```

### Desugaring (done at checker / codegen level, not parser)

```
@time EXPR
  →  { i64 __t0 = perf.now()
       T   __v  = EXPR
       i64 __dt = perf.elapsed_ns(__t0)
       __perf_print_elapsed("EXPR text", __dt)   // IR-emitted printf
       __v }
```

The expression text is captured as a compile-time string literal (like `#expr` in C macros)
from the AST pretty-printer.

### `@bench(n) expr` variant

```ls
f64 avg_ns = @bench(1000) matrix_mul(a, b)
// runs EXPR 1000 times, prints min/max/mean/stddev, returns mean ns
```

### Files

- `src/scanner.c` — add `TOKEN_AT_TIME`, `TOKEN_AT_BENCH`
- `src/parser.c` — parse `@time expr` as `AST_AT_TIME { expr }`
- `src/checker.c` — type-check; resolved_type = expr's type
- `src/codegen.c` — desugar to perf.now() + call + elapsed print

### Effort estimate: 2–3 days

---

## Phase P.3 — Instrumentation Profiler (function-level)

### Goal

Compile-time instrumented profiling: every function entry/exit records a
timestamp. At program exit, print a sorted call profile.

Similar to how `--memcheck` adds alloc/free hooks, `--profile` adds timing hooks.

### CLI

```bash
ls run --profile input.ls              # JIT + profiler
ls compile --profile input.ls -o out   # AOT + profiler
```

### Runtime data structure (`runtime/profiler.c`)

```c
typedef struct {
    const char *fn_name;
    const char *file;
    int         line;
    uint64_t    total_ns;
    uint64_t    call_count;
    uint64_t    self_ns;        // total - time in callees
} LsProfEntry;
```

Global hash table keyed by `fn_name` pointer (same scheme as memcheck).

### Codegen injection (parallel to memcheck Phase D.1)

Each user function (`codegen_fn_decl`) in `--profile` mode gets:

```llvm
; entry
call void @ls_prof_enter(i8* @fn_name, i8* @file, i32 line)

; before each ret
call void @ls_prof_leave()
```

`ls_prof_enter` / `ls_prof_leave` use rdtsc internally for cycle-accurate timing
(falls back to `clock_gettime` on non-x86).

### Output format

```
=== LS Profile Report ===
  calls     total ms   self ms    function
  ------  ----------  ---------  --------
   1000     1234.5      123.4     matrix_mul   input.ls:42
      1      234.1      234.1     main         input.ls:1
    500       89.3       89.3     dot_product  input.ls:15
=== top 10 hot functions shown; use --profile-all for full list ===
```

### Files

- `runtime/profiler.c` — hash table + enter/leave/report
- `src/codegen.c` — `cg_install_profiler_hooks()` (parallel to `cg_install_memcheck_wrappers`)
- `src/jit.c` — register `ls_prof_enter` / `ls_prof_leave` / `ls_prof_report` symbols
- `src/main.c` — `--profile` flag, AOT link with `libls_profiler.a`
- `CMakeLists.txt` — `add_library(ls_profiler STATIC runtime/profiler.c)`

### Effort estimate: 5–7 days

---

## Phase P.4 — Linux `perf` Integration

### Goal

On Linux, automatically wrap `ls run` / `ls compile` output with `perf` tooling,
removing the need to manually invoke `perf`.

### CLI flags

```bash
# perf stat — hardware counter summary (IPC, cache-miss, branch-miss, etc.)
ls run --perf-stat input.ls

# perf record → perf report — sampling profiler, flame graph
ls run --perf-record input.ls           # saves perf.data, auto-opens perf report
ls run --perf-flamegraph input.ls       # requires flamegraph.pl in PATH

# AOT: compile and annotate with source-level perf data
ls compile input.ls -o out
ls perf-annotate out                    # wraps perf annotate on the binary
```

### Implementation — `--perf-stat` path

`main.c::cmd_run_perf_stat()`:

```c
// Detect perf availability
if (access("/usr/bin/perf", X_OK) != 0) {
    fprintf(stderr, "warning: perf not found; run: sudo apt install linux-perf\n");
}
// Build JIT-executed binary to a temp AOT binary, then:
char cmd[2048];
snprintf(cmd, sizeof(cmd),
    "perf stat -e cycles,instructions,cache-misses,branch-misses \"%s\"",
    aot_path);
system(cmd);
```

For JIT mode: compile to a temp AOT binary first, then run under perf.
Pure JIT + perf requires `PERF_ATTR_TYPE_SOFTWARE` and `perf_event_open` syscall
(Phase P.4 v2).

### `perf_event_open` low-level access (v2)

```ls
import perf

// Open a hardware perf event counter
perf.Counter c = perf.open_counter(perf.HW_CPU_CYCLES)
perf.reset(c)
perf.enable(c)
// ... code to measure ...
perf.disable(c)
i64 cycles = perf.read(c)
perf.close(c)
```

Implemented via `extern` wrapper over `perf_event_open(2)` syscall.
Linux-only; on Windows the same API falls back to `rdtsc`.

### Files

- `src/main.c` — `--perf-stat`, `--perf-record`, `--perf-flamegraph` CLI sub-commands
- `stdlib/perf_linux.ls` — `perf.open_counter`, `perf.read`, etc. (pure-LS via extern)
- `docs/linux_deployment.md` — add note about `linux-perf` / `linux-tools-common`

### Prerequisites for `perf record` flamegraph

```bash
sudo apt-get install -y linux-perf linux-tools-common
# For flamegraph.pl:
git clone https://github.com/brendangregg/FlameGraph
export PATH=$PATH:$(pwd)/FlameGraph
```

### Effort estimate: 4–6 days (stat) + 3–5 days (perf_event_open v2)

---

## Phase Summary

| Phase | Feature | Dependency | Effort |
|-------|---------|------------|--------|
| P.1 | `perf.now()` / `rdtsc()` / `rdtscp()` | none | 3–5 d |
| P.2 | `@time` / `@bench` expression sugar | P.1 | 2–3 d |
| P.3 | `--profile` instrumentation profiler | P.1 | 5–7 d |
| P.4 | Linux `perf` integration | P.3 (for AOT) | 4–8 d |

**Recommended order:** P.1 → P.2 → P.3 → P.4

P.1 and P.2 are self-contained and high-value; they can ship independently.
