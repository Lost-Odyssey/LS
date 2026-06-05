// structbench.rs — Rust reference for struct/enum benchmark.
//   rustc -O structbench.rs -o structbench_rs.exe
use std::time::Instant;
use std::hint::black_box;

struct Point { x: f64, y: f64 }
impl Point { fn dist2(&self) -> f64 { self.x * self.x + self.y * self.y } }
struct Circle { center: Point, r: f64 }
impl Circle { fn area(&self) -> f64 { 3.14159 * self.r * self.r } }

enum Shape { Dot, Seg(f64), Box(f64, f64) }
fn shape_measure(s: &Shape) -> f64 {
    match s {
        Shape::Dot => 0.0,
        Shape::Seg(l) => *l,
        Shape::Box(w, h) => w * h,
    }
}

#[inline(never)]
fn bench_scalar(n: i64) -> f64 {
    let mut s = 0.0;
    for i in 0..n {
        let c = (i % 1000) as f64;
        let p = Point { x: c, y: c * 2.0 };
        s += p.dist2();
    }
    s
}
#[inline(never)]
fn bench_nested(n: i64) -> f64 {
    let mut s = 0.0;
    for i in 0..n {
        let c = (i % 1000) as f64;
        let cir = Circle { center: Point { x: c, y: 0.0 }, r: c };
        s += cir.area() + cir.center.x;
    }
    s
}
#[inline(never)]
fn bench_enum(n: i64) -> f64 {
    let mut s = 0.0;
    for i in 0..n {
        let c = (i % 1000) as f64;
        let k = i % 3;
        let mut sh = Shape::Dot;
        if k == 1 { sh = Shape::Seg(c); }
        if k == 2 { sh = Shape::Box(2.0, c); }
        s += shape_measure(&sh);
    }
    s
}

fn main() {
    let args: Vec<String> = std::env::args().collect();
    let n: i64 = if args.len() >= 2 { args[1].parse().unwrap_or(1000000) } else { 1000000 };

    black_box(bench_scalar(1000) + bench_nested(1000) + bench_enum(1000));

    let t0 = Instant::now();
    let r1 = black_box(bench_scalar(n));
    let t1 = Instant::now();
    let r2 = black_box(bench_nested(n));
    let t2 = Instant::now();
    let r3 = black_box(bench_enum(n));
    let t3 = Instant::now();

    let chk = (r1 + r2 + r3) as i64;
    println!("result: {}", chk);
    println!("[@bench] scalar    {} us", t1.duration_since(t0).as_micros());
    println!("[@bench] nested    {} us", t2.duration_since(t1).as_micros());
    println!("[@bench] enum      {} us", t3.duration_since(t2).as_micros());
    println!("[@bench] mean {} us ({} iterations)", t3.duration_since(t0).as_micros(), n);
}
