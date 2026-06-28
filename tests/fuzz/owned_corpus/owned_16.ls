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
    Guard(Map(Str,int)) v2 = {}
    v2.init()
    Str v3 = f"{1}"
    Vec(int) v4 = [-2, -1, 1, 1]
    v1.set("-5", mk_veci(9))
    Map(Str,Vec(int)) v5 = {}
    SpinGuard(Str) v6 = {}
    bool v7 = v4.get(-1).is_none?()
    SpinGuard(Str) v8 = {}
    int v9 = v2.get(int)(|w| { return w.len() })
    Guard(Str) v10 = {}
    v10.init()
    return 0
}
