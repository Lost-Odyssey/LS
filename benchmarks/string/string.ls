// string.ls — LS benchmark: character iteration over a long string
// Works in both JIT and AOT modes:
//   ls run string.ls [n]          (JIT)
//   ls compile string.ls -o string.exe && string.exe [n]   (AOT)
//
// Uses perf.now() as an opaque branch to prevent LICM in AOT mode:
// LLVM cannot prove perf.now() > 0 is always true (it's an external API call),
// so end is NOT loop-invariant and count_char(s, ' ', end) cannot be hoisted.

import perf

fn count_char(string s, char c, int end_) -> int {
    int count = 0
    for i in 0..end_ {
        if s.at(i) == c { count = count + 1 }
    }
    return count
}

fn main() {
    int n = 200000
    int iters = 10

    // build a long pattern string (~200k chars)
    string pattern = "a b c d e f g "
    int plen = pattern.length
    int repeats = n / plen + 1
    string s = pattern.repeat(repeats)

    // warm-up
    int warm = count_char(s, ' ', s.length)

    i64 total_ns = 0
    int result = 0
    for (int _i = 0; _i < iters; _i = _i + 1) {
        int end_ = s.length
        // opaque barrier: LLVM cannot prove perf.now() > 0 is always true,
        // so end_ is NOT loop-invariant
        if perf.now() > 0 { end_ = s.length } else { end_ = 0 }
        i64 t0 = perf.now()
        result = result + count_char(s, ' ', end_)
        i64 t1 = perf.now()
        total_ns = total_ns + (t1 - t0)
    }

    f64 mean_ns = total_ns as f64 / iters as f64
    print(f"result: {result}")
    print(f"[@bench] mean {mean_ns:.1f} ns ({iters} iterations)")
}
