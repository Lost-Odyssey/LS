// RawVec(T) vs builtin vec(T) micro-benchmark. AOT-compiled (default<O2>).
import std.rawvec
import std.time as T

fn bench_int(int N) {
    i64 t0 = T.now_unix_ns()
    vec(int) bv = []
    for (int i = 0; i < N; i = i + 1) { bv.push(i) }
    i64 bsum = 0
    for (int i = 0; i < N; i = i + 1) { bsum = bsum + bv[i] }
    i64 t1 = T.now_unix_ns()
    RawVec(int) rv = {}
    for (int i = 0; i < N; i = i + 1) { rv.push(i) }
    i64 rsum = 0
    for (int i = 0; i < N; i = i + 1) { rsum = rsum + rv.get(i) }
    i64 t2 = T.now_unix_ns()
    f64 vms = (t1 - t0) as f64 / 1000000.0
    f64 rms = (t2 - t1) as f64 / 1000000.0
    print(f"int  N={N}: vec={vms}ms raw={rms}ms ratio={rms/vms}  (ck {bsum}/{rsum})")
}

fn bench_str(int N) {
    i64 t0 = T.now_unix_ns()
    vec(string) bv = []
    for (int i = 0; i < N; i = i + 1) { bv.push(f"item_{i}") }
    i64 t1 = T.now_unix_ns()
    RawVec(string) rv = {}
    for (int i = 0; i < N; i = i + 1) { rv.push(f"item_{i}") }
    i64 t2 = T.now_unix_ns()
    f64 vms = (t1 - t0) as f64 / 1000000.0
    f64 rms = (t2 - t1) as f64 / 1000000.0
    print(f"str  N={N}: vec={vms}ms raw={rms}ms ratio={rms/vms}  (len {bv.length}/{rv.length()})")
}

fn main() {
    bench_int(3000000)
    bench_int(3000000)
    bench_str(1000000)
    bench_str(1000000)
}
