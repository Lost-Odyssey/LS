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
    Map(Str,Vec(int)) v1 = {}
    Vec(Str) v2 = ["hi", "alpha"]
    Guard(Vec(int)) v3 = {}
    v3.init()
    Guard(Vec(int)) v4 = {}
    v4.init()
    Vec(int) v5 = [4, 0, -3, -2]
    Tree v6 = Leaf(6)
    int v7 = 0
    Tree v8 = Leaf(6)
    Str v9 = match -1 { 0 => f"z"  1 | 2 => f"{8}"  _ => "y" }
    for v10 in 0..4 { v9 = v9 + f"{v10}" }
    Str v11 = f"{-2}"
    int v12 = match v7 * 2 { 0 => 0  1 | 2 | 3 => 1  _ => 5 }
    return 0
}
