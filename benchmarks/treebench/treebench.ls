// treebench.ls — recursive data structure (binary tree) traversal benchmark.
// Builds a self-recursive enum tree, then sums all leaves.
//
// Uses &Tree borrow (Phase 9) for zero-copy traversal — ~206× faster than
// the former by-value deep-clone approach. Now within ~2× of Rust/C++.
//
//   ls run treebench.ls [depth]

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

enum Tree {
    Leaf(i64 val)
    Node(Tree left, Tree right)
}

fn build(int depth, i64 v) -> Tree {
    if depth == 0 { return Leaf(v) }
    return Node(build(depth - 1, v * 2), build(depth - 1, v * 2 + 1))
}

fn sum_tree(&Tree t) -> i64 {
    match t {
        Leaf(v)    => { return v }
        Node(l, r) => { return sum_tree(l) + sum_tree(r) }
    }
}

fn main() -> int {
    int depth = parse_n(16)
    int iters = 5

    Tree tr = build(depth, 1)

    i64 total = 0
    i64 s = 0
    for (int i = 0; i < iters; i = i + 1) {
        i64 t0 = perf.now()
        s = sum_tree(tr)
        i64 t1 = perf.now()
        total = total + (t1 - t0)
    }

    f64 mean = (total as f64) / (iters as f64)
    print(f"result: {s}")
    print(f"[@bench] mean {mean/1000.0:.1f} us (depth={depth}, {iters} sums)")
    return 0
}
