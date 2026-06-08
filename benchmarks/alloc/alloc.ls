// alloc.ls — LS benchmark: allocation / RAII / container throughput.
// Exercises the machinery that distinguishes LS from plain C:
//   - Vec(string) growth + per-element heap strings + scope-exit drop  (Phase A)
//   - map(string,int) insert / lookup / rehash with string keys        (Phase B)
// Both phases build and drop a string every iteration, stressing the
// clone/drop/move paths (not just the LLVM backend, like fib/string do).
//
//   ls run alloc.ls [n]                              (JIT)
//   ls compile alloc.ls -o alloc.exe && alloc.exe [n]   (AOT)
//
// N is read from argv (bug #22 fix makes this work in AOT too); default 200000.

import std.vec
import perf
import proc

fn parse_n(int dflt) -> int {
    Vec(string) a = proc.args()
    if a.len() >= 1 {
        Result(int, string) r = a[0].to_int()
        match r {
            Ok(v)  => { return v }
            Err(e) => { return dflt }
        }
    }
    return dflt
}

// Phase A: build a Vec of n heap strings, sum their lengths, drop everything.
fn vec_stress(int n) -> i64 {
    Vec(string) v = {}
    for i in 0..n {
        v.push(f"item_{i}")
    }
    i64 chk = 0
    for s in v {
        chk = chk + s.length as i64
    }
    return chk
    // scope exit: all n strings + Vec buffer freed
}

// Phase B: word-frequency map with `keyspace` distinct string keys.
// n tokens → map grows through several rehashes; returns distinct key count.
fn map_stress(int n) -> i64 {
    map(string, int) freq
    int keyspace = 8192
    for i in 0..n {
        string key = f"key_{i % keyspace}"
        int cur = freq.get(key)
        freq.set(key, cur + 1)
    }
    return freq.length as i64
}

fn main() -> int {
    int n = parse_n(200000)
    int iters = 5

    // warm-up (not timed)
    i64 warm = vec_stress(n) + map_stress(n)

    i64 total_ns = 0
    i64 chk = 0
    for (int _i = 0; _i < iters; _i = _i + 1) {
        i64 t0 = perf.now()
        chk = chk + vec_stress(n) + map_stress(n)
        i64 t1 = perf.now()
        total_ns = total_ns + (t1 - t0)
    }

    f64 mean_ns = total_ns as f64 / iters as f64
    print(f"result: {chk}")
    print(f"[@bench] mean {mean_ns:.1f} ns ({iters} iterations)")
    return 0
}
