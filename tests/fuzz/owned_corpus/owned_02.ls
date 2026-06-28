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
    Str v1 = "beta"
    int v2 = 6
    Tag v3 = B(7)
    Guard(Str) v4 = {}
    v4.init()
    Str v5 = match v3 { A(s) => s  B(k) => f"{k}"  C => "c" }
    Guard(Str) v6 = {}
    v6.init()
    int v7 = 0
    return 0
}
