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
    int v2 = 9
    Vec(Vec(int)) v3 = []
    Arena(int) v4 = {}
    Str v5 = match 1.5 { 1.5 => f"{v2}"  2.5 => f"two"  _ => "0" }
    Vec(int) v6 = [-1]
    int v7 = v2
    int v8 = match v2 * 2 { 0 => 0  1 | 2 | 3 => 1  _ => -1 }
    Guard(Str) v9 = {}
    v9.init()
    v9.lock(|w| { w.push_str("zzz") })
    for v10 in 0..6 { v5 = v5 + f"{v10}" }
    Guard(Str) v11 = {}
    v11.init()
    int v12 = v6.get(-1).unwrap_or(v2 * 2)
    return 0
}
