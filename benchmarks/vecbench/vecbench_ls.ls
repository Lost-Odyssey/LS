// vecbench_ls.ls — pure LS Vec(T) (std/vec.ls) throughput benchmark.
// Same 4 operations as vecbench.ls, using Vec(T) instead of builtin vec(T):
// push (with growth), index read, for-in iteration, index write.
//
//   ls run vecbench_ls.ls [n]

import vec
import perf
import proc

fn parse_n(int dflt) -> int {
    vec(string) a = proc.args()
    if a.length >= 1 {
        Result(int, string) r = a[0].to_int()
        match r { Ok(v) => { return v } Err(e) => { return dflt } }
    }
    return dflt
}

fn main() -> int {
    int n = parse_n(10000000)

    // ① push with growth
    i64 t0 = perf.now()
    Vec(i64) v = {}
    for i in 0..n { v.push((i % 1000) as i64) }
    i64 t1 = perf.now()

    // ② index read via v[i]
    i64 sum = 0
    for i in 0..n { sum = sum + v[i] }
    i64 t2 = perf.now()

    // ③ for-in via C-style for + v.get(i) (for x in v not yet supported on Vec)
    i64 sum2 = 0
    for (int i = 0; i < v.len(); i = i + 1) { sum2 = sum2 + v.get(i) }
    i64 t3 = perf.now()

    // ④ index write via v[i]
    for i in 0..n { v[i] = v[i] + 1 }
    i64 t4 = perf.now()

    i64 chk = sum + sum2 + v[0] + v[n - 1]
    print(f"result: {chk}")
    print(f"[@bench] push      {(t1-t0)/1000} us")
    print(f"[@bench] index_r   {(t2-t1)/1000} us")
    print(f"[@bench] for_in    {(t3-t2)/1000} us")
    print(f"[@bench] index_w   {(t4-t3)/1000} us")
    print(f"[@bench] mean {(t4-t0)/1000} us ({n} iterations)")
    return 0
}