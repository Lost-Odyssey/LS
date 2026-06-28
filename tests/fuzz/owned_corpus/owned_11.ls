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
    int v2 = -2
    Vec(Vec(int)) v3 = []
    Box v4 = Box{ name: f"{5}", nums: [] }
    SpinGuard(Str) v5 = {}
    SpinGuard(Str) v6 = {}
    return 0
}
