// RawVec(T) vs builtin vec(T) — comprehensive micro-benchmark. AOT (default<O2>).
// Each op: builtin vec vs pure-LS RawVec; prints ms + ratio (raw/vec, ~1.0 = parity).
import std.rawvec
import std.time as T

fn ms(i64 a, i64 b) -> f64 { return (b - a) as f64 / 1000000.0 }

fn bench_push_int(int N) {
    i64 t0 = T.now_unix_ns()
    vec(int) bv = []
    for (int i = 0; i < N; i = i + 1) { bv.push(i) }
    i64 t1 = T.now_unix_ns()
    RawVec(int) rv = {}
    for (int i = 0; i < N; i = i + 1) { rv.push(i) }
    i64 t2 = T.now_unix_ns()
    print(f"push int  N={N}: vec={ms(t0,t1)}ms raw={ms(t1,t2)}ms ratio={ms(t1,t2)/ms(t0,t1)}")
}

fn bench_sum_int(int N) {
    vec(int) bv = []
    RawVec(int) rv = {}
    for (int i = 0; i < N; i = i + 1) { bv.push(i); rv.push(i) }
    i64 t0 = T.now_unix_ns()
    i64 bs = 0
    for (int i = 0; i < N; i = i + 1) { bs = bs + bv[i] }
    i64 t1 = T.now_unix_ns()
    i64 rs = 0
    for (int i = 0; i < N; i = i + 1) { rs = rs + rv.get(i) }
    i64 t2 = T.now_unix_ns()
    print(f"get  int  N={N}: vec={ms(t0,t1)}ms raw={ms(t1,t2)}ms ratio={ms(t1,t2)/ms(t0,t1)}  (ck {bs}/{rs})")
}

fn bench_push_str(int N) {
    i64 t0 = T.now_unix_ns()
    vec(string) bv = []
    for (int i = 0; i < N; i = i + 1) { bv.push(f"item") }
    i64 t1 = T.now_unix_ns()
    RawVec(string) rv = {}
    for (int i = 0; i < N; i = i + 1) { rv.push(f"item") }
    i64 t2 = T.now_unix_ns()
    print(f"push str  N={N}: vec={ms(t0,t1)}ms raw={ms(t1,t2)}ms ratio={ms(t1,t2)/ms(t0,t1)}")
}

fn main() {
    bench_push_int(3000000)
    bench_sum_int(3000000)
    bench_push_str(1000000)
}
