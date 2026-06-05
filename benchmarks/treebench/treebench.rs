// treebench.rs — Rust reference: tree traversal by reference (zero-copy).
//   rustc -O treebench.rs -o treebench_rs.exe
use std::time::Instant;
use std::hint::black_box;

enum Tree { Leaf(i64), Node(Box<Tree>, Box<Tree>) }

fn build(depth: i32, v: i64) -> Tree {
    if depth == 0 { return Tree::Leaf(v); }
    Tree::Node(Box::new(build(depth - 1, v * 2)), Box::new(build(depth - 1, v * 2 + 1)))
}
fn sum_tree(t: &Tree) -> i64 {
    match t { Tree::Leaf(v) => *v, Tree::Node(l, r) => sum_tree(l) + sum_tree(r) }
}

fn main() {
    let args: Vec<String> = std::env::args().collect();
    let depth: i32 = if args.len() >= 2 { args[1].parse().unwrap_or(16) } else { 16 };
    let iters = 20;
    let tr = build(depth, 1);
    let mut s = 0i64;
    let t0 = Instant::now();
    for _ in 0..iters { s = black_box(sum_tree(&tr)); }
    let t1 = Instant::now();
    let mean = t1.duration_since(t0).as_nanos() as f64 / iters as f64 / 1000.0;
    println!("result: {}", s);
    println!("[@bench] mean {:.1} us (depth={}, {} sum)", mean, depth, iters);
}
