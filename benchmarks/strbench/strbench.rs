// strbench.rs — Rust String reference.
//   rustc -O strbench.rs -o strbench_rs.exe
use std::time::Instant;
use std::hint::black_box;

fn main() {
    let args: Vec<String> = std::env::args().collect();
    let n: i64 = if args.len() >= 2 { args[1].parse().unwrap_or(1000000) } else { 1000000 };
    let base = String::from("The Quick Brown Fox Jumps Over The Lazy Dog");
    let needles: Vec<String> = vec!["Fox","Dog","The","Lazy","Quick"]
        .into_iter().map(String::from).collect();

    let t0 = Instant::now();
    let mut a1: i64 = 0;
    for _ in 0..n { let u = base.to_uppercase(); a1 += u.len() as i64; }
    black_box(a1);
    let t1 = Instant::now();

    let mut a2: i64 = 0;
    for i in 0..n {
        let nd = &needles[(i % 5) as usize];
        if base.contains(nd.as_str()) { a2 += 1; }
    }
    black_box(a2);
    let t2 = Instant::now();

    let mut a3: i64 = 0;
    for _ in 0..n { let parts: Vec<&str> = base.split(' ').collect(); a3 += parts.len() as i64; }
    black_box(a3);
    let t3 = Instant::now();

    let mut a4: i64 = 0;
    for _ in 0..n { let r = base.replace("o", "0"); a4 += r.len() as i64; }
    black_box(a4);
    let t4 = Instant::now();

    let mut a5: i64 = 0;
    for i in 0..n {
        let p = (i % 10) as usize;
        let s: String = base[p..p+5].to_string();
        a5 += s.as_bytes()[0] as i64;
    }
    black_box(a5);
    let t5 = Instant::now();

    let chk = a1 + a2 + a3 + a4 + a5;
    println!("result: {}", chk);
    println!("[@bench] upper     {} us", t1.duration_since(t0).as_micros());
    println!("[@bench] contains  {} us", t2.duration_since(t1).as_micros());
    println!("[@bench] split     {} us", t3.duration_since(t2).as_micros());
    println!("[@bench] replace   {} us", t4.duration_since(t3).as_micros());
    println!("[@bench] substr    {} us", t5.duration_since(t4).as_micros());
    println!("[@bench] mean {} us ({} iterations)", t5.duration_since(t0).as_micros(), n);
}
