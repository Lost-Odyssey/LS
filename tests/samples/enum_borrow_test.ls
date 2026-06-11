// enum_borrow_test.ls — Phase 9: &Enum read-only borrow + zero-copy match
// Tests:
//   1. &Tree borrow traversal (self-recursive enum, box payload)
//   2. auto-borrow: sum_tree(tr) where tr: Tree, fn takes &Tree
//   3. explicit borrow: sum_tree_ex(&tr)
//   4. nested recursive calls via box pointer (zero-copy)
//   5. Leaf(scalar) payload extraction via borrow match
//   6. Non-self-recursive enum borrow (Option-like)

import std.str

enum Tree {
    Leaf(i64 val)
    Node(Tree left, Tree right)
}

fn build(int depth, i64 v) -> Tree {
    if depth == 0 { return Leaf(v) }
    return Node(build(depth - 1, v), build(depth - 1, v))
}

fn sum_tree(&Tree t) -> i64 {
    match t {
        Leaf(v)    => { return v }
        Node(l, r) => { return sum_tree(l) + sum_tree(r) }
    }
}

fn depth_tree(&Tree t) -> int {
    match t {
        Leaf(v)    => { return 0 }
        Node(l, r) => {
            int dl = depth_tree(l)
            int dr = depth_tree(r)
            if dl > dr { return dl + 1 }
            return dr + 1
        }
    }
}

fn count_nodes(&Tree t) -> int {
    match t {
        Leaf(v)    => { return 1 }
        Node(l, r) => { return count_nodes(l) + count_nodes(r) + 1 }
    }
}

enum Color {
    Red
    Green
    Blue
}

fn color_name(&Color c) -> Str {
    match c {
        Red   => { return "red" }
        Green => { return "green" }
        Blue  => { return "blue" }
    }
}

fn main() -> int {
    int pass = 0
    int fail = 0

    // Test 1: depth=3 tree, all leaves = 7
    // build(3,7): Node(Node(Node(Leaf(7),Leaf(7)),Node(Leaf(7),Leaf(7))),
    //                  Node(Node(Leaf(7),Leaf(7)),Node(Leaf(7),Leaf(7))))
    // 8 leaves * 7 = 56
    Tree tr = build(3, 7)
    i64 s = sum_tree(tr)
    if s == 56 {
        print("T01 sum depth=3 leaves=7: PASS")
        pass = pass + 1
    } else {
        print(f"T01 FAIL: expected 56, got {s}")
        fail = fail + 1
    }

    // Test 2: depth_tree
    int d = depth_tree(tr)
    if d == 3 {
        print("T02 depth: PASS")
        pass = pass + 1
    } else {
        print(f"T02 FAIL: expected 3, got {d}")
        fail = fail + 1
    }

    // Test 3: count_nodes for depth=3 full binary tree: 2^4 - 1 = 15
    int n = count_nodes(tr)
    if n == 15 {
        print("T03 count_nodes: PASS")
        pass = pass + 1
    } else {
        print(f"T03 FAIL: expected 15, got {n}")
        fail = fail + 1
    }

    // Test 4: single leaf
    Tree leaf = Leaf(42 as i64)
    i64 sv = sum_tree(leaf)
    if sv == 42 {
        print("T04 leaf sum: PASS")
        pass = pass + 1
    } else {
        print(f"T04 FAIL: expected 42, got {sv}")
        fail = fail + 1
    }

    // Test 5: larger tree sum (depth=5, leaf value=1 → 32 leaves → sum=32)
    Tree tr5 = build(5, 1)
    i64 s5 = sum_tree(tr5)
    if s5 == 32 {
        print("T05 depth=5 sum: PASS")
        pass = pass + 1
    } else {
        print(f"T05 FAIL: expected 32, got {s5}")
        fail = fail + 1
    }

    // Test 6: non-recursive enum borrow (no box payload)
    Color c = Green
    Str cn = color_name(c)
    if cn.eq?("green") {
        print("T06 color borrow: PASS")
        pass = pass + 1
    } else {
        print(f"T06 FAIL: expected green, got {cn}")
        fail = fail + 1
    }

    // Test 7: depth after larger build
    int d5 = depth_tree(tr5)
    if d5 == 5 {
        print("T07 depth=5: PASS")
        pass = pass + 1
    } else {
        print(f"T07 FAIL: expected 5, got {d5}")
        fail = fail + 1
    }

    // Test 8: sum called multiple times on same tree (borrow doesn't consume)
    i64 s1 = sum_tree(tr)
    i64 s2 = sum_tree(tr)
    if s1 == 56 && s2 == 56 {
        print("T08 multiple borrows: PASS")
        pass = pass + 1
    } else {
        print(f"T08 FAIL: s1={s1} s2={s2}")
        fail = fail + 1
    }

    if fail == 0 {
        print("ALL PASS")
    } else {
        print(f"FAILED {fail} / {pass + fail}")
    }
    return fail
}
