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
    Guard(Str) v1 = {}
    v1.init()
    Map(Str,Vec(int)) v2 = {}
    Map(Str,int) v3 = {}
    Box v4 = Box{ name: f"{6}", nums: [] }
    Str v5 = "hi"
    Vec(Vec(int)) v6 = []
    Map(Str,int) v7 = {}
    for v8 in 0..5 { v5 = v5 + f"{v8}" }
    v2.set("-5", mk_veci(9))
    Tag v9 = B(-3)
    Guard(Map(Str,int)) v10 = {}
    v10.init()
    SpinGuard(Str) v11 = {}
    return 0
}
