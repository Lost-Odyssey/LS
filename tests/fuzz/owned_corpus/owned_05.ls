import std.core.vec
import std.core.map
import std.core.str
import std.sync.lock

struct Box {
    Str name
    Vec(int) nums
}

enum Tag {
    A(Str)
    B(int)
    C
}

enum Tree {
    Leaf(int)
    Node(Vec(Tree))
}

def mk_str(int k) -> Str { return f"s{k}" }
def mk_veci(int k) -> Vec(int) { Vec(int) r = [k, k] return r }
def mk_leaf(int k) -> Tree { return Leaf(k) }

def main() -> int {
    SpinGuard(Str) v1 = {}
    Tag v2 = A(f"{9}")
    match v2 { A(s) => { @print(s.len()) } B(n) => { @print(n) } C => {} }
    v1.lock(|w| { w.push_str("x") })
    SpinGuard(Str) v3 = {}
    Tree v4 = Leaf(0)
    return 0
}
