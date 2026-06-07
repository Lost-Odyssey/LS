// L-012 ③: a match arm whose body directly returns a heap-value call result
// (`return f(binding)`) must MOVE the temp out, not clone-and-leak it. The
// subject param is still dropped by normal scope cleanup. Must be clean.

import std.vec

enum Node {
    Leaf(Vec(string) items)
    Empty
}

fn collect(Vec(string) xs) -> Vec(string) {
    Vec(string) out = {}
    int i = 0
    while i < xs.len() { out.push(xs.get(i)); i = i + 1 }
    return out
}

fn links(Node n) -> Vec(string) {
    match n {
        Leaf(items) => { return collect(items) }   // direct heap-value call return
        Empty       => { Vec(string) e = {}; return e }
    }
}

fn main() {
    Vec(string) a = {}
    a.push("x")
    a.push("y")
    Node n = Leaf(a)
    Vec(string) r = links(n)
    print(f"len={r.len()}")
}
