// json_bench.rs — Rust reference for JSON benchmark.
// Uses serde_json (de facto standard). Build:
//   cargo build --release
// Run:
//   ./target/release/json_bench [n]
//
// If building without cargo (single file), you need serde + serde_json deps.
// For a fair comparison, this uses the simplest serde_json API (Value, no typed structs).

use std::time::Instant;

fn build_json(n: i64) -> String {
    let mut records: Vec<serde_json::Value> = Vec::new();
    for i in 0..n {
        records.push(serde_json::json!({
            "id": i,
            "name": format!("user_{}", i),
            "score": i * 7,
            "active": i % 2 == 0,
        }));
    }
    serde_json::to_string(&records).unwrap()
}

fn main() {
    let args: Vec<String> = std::env::args().collect();
    let n: i64 = if args.len() >= 2 { args[1].parse().unwrap_or(1000) } else { 1000 };
    let iters = 5;

    let raw = build_json(n);

    // warm-up
    let _w: serde_json::Value = serde_json::from_str(&raw).unwrap();
    let _ws = serde_json::to_string(&_w).unwrap();

    let mut parse_ns: u128 = 0;
    let mut stringify_ns: u128 = 0;
    let mut chk: i64 = 0;
    for _ in 0..iters {
        let t0 = Instant::now();
        let v: serde_json::Value = serde_json::from_str(&raw).unwrap();
        parse_ns += t0.elapsed().as_nanos();

        let t2 = Instant::now();
        let out = serde_json::to_string(&v).unwrap();
        stringify_ns += t2.elapsed().as_nanos();
        chk += out.len() as i64;
    }
    let parse_mean = parse_ns as f64 / iters as f64;
    let str_mean = stringify_ns as f64 / iters as f64;
    let total_mean = parse_mean + str_mean;
    println!("result: {}", chk);
    println!("[@bench] mean {:.1} ns ({} iterations)", total_mean, iters);
    println!("  parse:     {:.1} ns", parse_mean);
    println!("  stringify: {:.1} ns", str_mean);
}
