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
    Vec(int) v1 = [8, 0]
    Map(Str,Vec(int)) v2 = {}
    Vec(int) v3 = [0, -1]
    SpinGuard(Str) v4 = {}
    SpinGuard(Str) v5 = {}
    Vec(int) v6 = v3.filter(|x| x > 0)
    bool v7 = v3.get(9).is_none?()
    Guard(Map(Str,int)) v8 = {}
    v8.init()
    Map(Str,Vec(int)) v9 = {}
    return 0
}
