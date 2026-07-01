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
    Str v1 = f"{-3}"
    Str v2 = "1"
    Guard(Vec(int)) v3 = {}
    v3.init()
    for v4 in 0..5 { v2 = v2 + f"{v4}" }
    Vec(Vec(int)) v5 = []
    Str v6 = match 1.5 { 1.5 => "-5"  2.5 => f"two"  _ => "a,b,c" }
    Str v7 = match -1.0 { 1.5 => "gamma"  2.5 => f"two"  _ => "val" }
    return 0
}
