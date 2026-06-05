// treebench.ls — recursive data structure (binary tree) traversal benchmark.
// Builds a self-recursive enum tree, then sums all leaves.
//
// ⚠️ KNOWN LIMITATION (see treebench_analysis.md + docs/plan_enum_borrow.md):
// LS enums do NOT support borrows (`&Tree` is rejected by the checker), so
// sum_tree must take the tree BY VALUE, which DEEP-CLONES the entire subtree on
// every recursive call. This makes traversal ~250x slower than C++/Rust (which
// traverse by reference, zero-copy). LS reports ONE sum (already very slow);
// C++/Rust/Python report the mean of several.
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

fn sum_tree(Tree t) -> i64 {
    match t {
        Leaf(v) => { return v }
        Node(l, r) => { return sum_tree(l) + sum_tree(r) }
    }
}

fn main() -> int {
    int depth = parse_n(16)
    int iters = 1   // one sum is already slow (deep-clones the whole tree)

    Tree tr = build(depth, 1)

    i64 t0 = perf.now()
    i64 s = sum_tree(tr)
    i64 t1 = perf.now()

    f64 mean = ((t1 - t0) as f64) / (iters as f64)
    print(f"result: {s}")
    print(f"[@bench] mean {mean/1000.0:.1f} us (depth={depth}, {iters} sum)")
    return 0
}
