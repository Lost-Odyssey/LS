// alloc.rs — Rust reference for the LS alloc benchmark.
// Must reproduce the exact checksum: same string formats, same map semantics.
//   rustc -O alloc.rs -o alloc_rs.exe && alloc_rs.exe [n]
use std::collections::HashMap;
use std::time::Instant;

fn vec_stress(n: i64) -> i64 {
    let mut v: Vec<String> = Vec::new();
    for i in 0..n {
        v.push(format!("item_{}", i));
    }
    let mut chk: i64 = 0;
    for s in &v {
        chk += s.len() as i64;
    }
    chk
}

fn map_stress(n: i64) -> i64 {
    let mut freq: HashMap<String, i64> = HashMap::new();
    let keyspace: i64 = 8192;
    for i in 0..n {
        let key = format!("key_{}", i % keyspace);
        let cur = *freq.get(&key).unwrap_or(&0);
        freq.insert(key, cur + 1);
    }
    freq.len() as i64
}

fn main() {
    let args: Vec<String> = std::env::args().collect();
    let n: i64 = if args.len() >= 2 {
        args[1].parse().unwrap_or(200000)
    } else {
        200000
    };
    let iters = 5;

    let _warm = vec_stress(n) + map_stress(n);

    let mut total_ns: u128 = 0;
    let mut chk: i64 = 0;
    for _ in 0..iters {
        let t0 = Instant::now();
        chk += vec_stress(n) + map_stress(n);
        total_ns += t0.elapsed().as_nanos();
    }
    let mean_ns = total_ns as f64 / iters as f64;
    println!("result: {}", chk);
    println!("[@bench] mean {:.1} ns ({} iterations)", mean_ns, iters);
}
