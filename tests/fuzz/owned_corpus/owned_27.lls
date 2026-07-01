import std.core.vec
import std.core.map
import std.core.str
import std.sync.lock
import std.mem.arena

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
    Guard(Map(Str,int)) v1 = {}
    v1.init()
    int v2 = 1
    SpinGuard(Str) v3 = {}
    int v4 = -2
    Tag v5 = A(f"{v4 + 1}")
    int v6 = v2
    Map(Str,Vec(int)) v7 = {}
    Vec(Vec(int)) v8 = []
    return 0
}
