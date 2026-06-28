// L-012 ③: a match arm whose body directly returns a heap-value call result
// (`return f(binding)`) must MOVE the temp out, not clone-and-leak it. The
// subject param is still dropped by normal scope cleanup. Must be clean.

import std.core.vec

enum Node {
    Leaf(Vec(Str) items)
    Empty
}

def collect(Vec(Str) xs) -> Vec(Str) {
    Vec(Str) out = {}
    int i = 0
    while i < xs.len() { out.push(xs.get!(i)); i = i + 1 }
    return out
}

def links(Node n) -> Vec(Str) {
    match n {
        Leaf(items) => { return collect(items) }   // direct heap-value call return
        Empty       => { Vec(Str) e = {}; return e }
    }
}

def main() {
    Vec(Str) a = {}
    a.push("x")
    a.push("y")
    Node n = Leaf(a)
    Vec(Str) r = links(n)
    @print(f"len={r.len()}")
}
