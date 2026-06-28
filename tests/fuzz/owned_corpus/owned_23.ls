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
    Map(Str,Vec(int)) v1 = {}
    Vec(Str) v2 = ["y"]
    SpinGuard(Str) v3 = {}
    SpinGuard(Str) v4 = {}
    Map(Str,Vec(int)) v5 = {}
    int v6 = 0
    Guard(Str) v7 = {}
    v7.init()
    Str v8 = v2.get(v6).unwrap_or("hi")
    Vec(int) v9 = [-1]
    Vec(int) v10 = v9.filter(|x| x > 0).map(int)(|x| x + 1)
    SpinGuard(Str) v11 = {}
    return 0
}
