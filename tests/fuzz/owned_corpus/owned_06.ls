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
    Vec(Str) v1 = ["zzz", "-5"]
    SpinGuard(Str) v2 = {}
    Vec(Str) v3 = ["key"]
    Tag v4 = C
    Vec(Str) v5 = ["0"]
    match v4 { A(s) => { @print(s.len()) } B(n) => { @print(n) } C => {} }
    Vec(int) v6 = [2, 2, 9, 2]
    Str v7 = v3.get(5).unwrap_or("-5")
    Str v8 = v3.get(3).unwrap_or("y")
    return 0
}
