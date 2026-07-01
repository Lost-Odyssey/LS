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
    int v1 = 3
    Guard(Map(Str,int)) v2 = {}
    v2.init()
    Str v3 = match 6 { 0 => f"z"  1 | 2 => f"{v1 * 2}"  _ => "val" }
    Str v4 = ""
    int v5 = match 6 { 0 => 0  1 | 2 | 3 => 1  _ => v1 }
    for v6 in 0..3 { v4 = v4 + f"{v6}" }
    return 0
}
