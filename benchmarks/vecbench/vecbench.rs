// vecbench.rs — Rust Vec reference.
//   rustc -O vecbench.rs -o vecbench_rs.exe
use std::time::Instant;
use std::hint::black_box;

fn main() {
    let args: Vec<String> = std::env::args().collect();
    let n: i64 = if args.len() >= 2 { args[1].parse().unwrap_or(10000000) } else { 10000000 };

    let t0 = Instant::now();
    let mut v: Vec<i64> = Vec::new();
    for i in 0..n { v.push(i % 1000); }
    let t1 = Instant::now();

    let mut sum: i64 = 0;
    for i in 0..n { sum += v[i as usize]; }
    black_box(sum);
    let t2 = Instant::now();

    let mut sum2: i64 = 0;
    for x in &v { sum2 += *x; }
    black_box(sum2);
    let t3 = Instant::now();

    for i in 0..n { v[i as usize] = v[i as usize] + 1; }
    let t4 = Instant::now();

    let chk = sum + sum2 + v[0] + v[(n - 1) as usize];
    println!("result: {}", chk);
    println!("[@bench] push      {} us", t1.duration_since(t0).as_micros());
    println!("[@bench] index_r   {} us", t2.duration_since(t1).as_micros());
    println!("[@bench] for_in    {} us", t3.duration_since(t2).as_micros());
    println!("[@bench] index_w   {} us", t4.duration_since(t3).as_micros());
    println!("[@bench] mean {} us ({} iterations)", t4.duration_since(t0).as_micros(), n);
}
