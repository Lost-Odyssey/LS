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
    Vec(Str) v1 = ["0"]
    Vec(Vec(int)) v2 = []
    Tree v3 = Leaf(-3)
    Vec(Str) v4 = ["zzz"]
    Str v5 = match 2.5 { 1.5 => f"{6}"  2.5 => f"two"  _ => "hi" }
    Vec(int) v6 = [2, 7, 1, 3]
    Arena(int) v7 = {}
    Vec(Vec(int)) v8 = []
    bool v9 = v6.get(-1).is_none?()
    RwLock(Box) v10 = {}
    v10.init()
    for v11 in 0..4 { v5 = v5 + f"{v11}" }
    return 0
}
