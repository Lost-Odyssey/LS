// fib.ls — LS benchmark: naive recursive Fibonacci
// Works in both JIT and AOT modes:
//   ls run fib.ls [n]          (JIT)
//   ls compile fib.ls -o fib.exe && fib.exe [n]   (AOT)
//
// Uses perf.now() as an opaque branch to prevent LICM in AOT mode:
// LLVM cannot prove perf.now() > 0 is always true (it's an external API call),
// so cur_n is NOT loop-invariant and fib(cur_n) cannot be hoisted.

import perf

fn fib(int n) -> int {
    if n <= 1 { return n }
    return fib(n - 1) + fib(n - 2)
}

fn main() {
    int n = 35
    int iters = 10

    // warm-up
    int warm = fib(n)

    i64 total_ns = 0
    int result = 0
    for (int _i = 0; _i < iters; _i = _i + 1) {
        // opaque barrier: LLVM cannot prove perf.now() > 0 is always true,
        // so cur_n depends on a runtime-only value → NOT loop-invariant
        int cur_n = n
        if perf.now() > 0 { cur_n = n } else { cur_n = n + 1 }
        i64 t0 = perf.now()
        result = result + fib(cur_n)
        i64 t1 = perf.now()
        total_ns = total_ns + (t1 - t0)
    }

    f64 mean_ns = total_ns as f64 / iters as f64
    print(f"result: {result}")
    print(f"[@bench] mean {mean_ns:.1f} ns ({iters} iterations)")
}
