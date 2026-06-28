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
    Guard(Vec(int)) v1 = {}
    v1.init()
    Guard(Str) v2 = {}
    v2.init()
    Vec(int) v3 = []
    Map(Str,Vec(int)) v4 = {}
    int v5 = 9
    Tree v6 = Leaf(5)
    int v7 = v1.get(int)(|w| { return w.len() })
    SpinGuard(Str) v8 = {}
    return 0
}
