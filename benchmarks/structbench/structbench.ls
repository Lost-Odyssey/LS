// structbench.ls — compiler built-in struct/enum/method benchmark.
// Pure compute (no heap alloc) — measures codegen quality for the core
// value types: struct construction, field access, method dispatch, nested
// struct, enum/match. Compares against C++/Rust/Python.
//
//   ls run structbench.ls [n]
//   ls run -O structbench.ls [n]    (with O2 inlining)
//   ls compile structbench.ls -o structbench.exe && structbench.exe [n]

import perf
import proc

fn parse_n(int dflt) -> int {
    vec(string) a = proc.args()
    if a.length >= 1 {
        Result(int, string) r = a[0].to_int()
        match r {
            Ok(v)  => { return v }
            Err(e) => { return dflt }
        }
    }
    return dflt
}

// ── scalar struct: construct + method call ──
struct Point { f64 x; f64 y }
impl Point {
    fn dist2(&self) -> f64 { return self.x * self.x + self.y * self.y }
}

// ── nested struct: field pass-through ──
struct Circle { Point center; f64 r }
impl Circle {
    fn area(&self) -> f64 { return 3.14159 * self.r * self.r }
}

// ── enum + match dispatch ──
enum Shape {
    Dot
    Seg(f64 len)
    Box(f64 w, f64 h)
}
fn shape_measure(Shape s) -> f64 {
    match s {
        Dot => { return 0.0 }
        Seg(l) => { return l }
        Box(w, h) => { return w * h }
    }
}

fn bench_scalar(int n) -> f64 {
    // coords bounded by i%1000 so the checksum stays within i64/f64-exact range
    // even at 100M iterations (otherwise sum of i^2 overflows).
    f64 s = 0.0
    for i in 0..n {
        f64 c = (i % 1000) as f64
        Point p = Point { x: c, y: c * 2.0 }
        s = s + p.dist2()
    }
    return s
}

fn bench_nested(int n) -> f64 {
    f64 s = 0.0
    for i in 0..n {
        f64 c = (i % 1000) as f64
        Circle cir = Circle { center: Point { x: c, y: 0.0 }, r: c }
        s = s + cir.area() + cir.center.x
    }
    return s
}

fn bench_enum(int n) -> f64 {
    f64 s = 0.0
    for i in 0..n {
        f64 c = (i % 1000) as f64
        int k = i % 3
        Shape sh = Dot
        if k == 1 { sh = Seg(c) }
        if k == 2 { sh = Box(2.0, c) }
        s = s + shape_measure(sh)
    }
    return s
}

fn main() -> int {
    int n = parse_n(1000000)

    // warm-up
    f64 w = bench_scalar(1000) + bench_nested(1000) + bench_enum(1000)

    i64 t0 = perf.now()
    f64 r1 = bench_scalar(n)
    i64 t1 = perf.now()
    f64 r2 = bench_nested(n)
    i64 t2 = perf.now()
    f64 r3 = bench_enum(n)
    i64 t3 = perf.now()

    // checksum (sum of all three, rounded) prevents dead-code elimination
    i64 chk = (r1 + r2 + r3) as i64
    // Report TOTAL elapsed (us), not per-iter: for ~ns/iter loops the integer
    // divide by n destroys all precision. Total time is the honest measure.
    print(f"result: {chk}")
    print(f"[@bench] scalar    {(t1-t0)/1000} us")
    print(f"[@bench] nested    {(t2-t1)/1000} us")
    print(f"[@bench] enum      {(t3-t2)/1000} us")
    print(f"[@bench] mean {(t3-t0)/1000} us ({n} iterations)")
    return 0
}
