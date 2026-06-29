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
    Arena(int) v1 = {}
    Guard(Str) v2 = {}
    v2.init()
    SpinGuard(Str) v3 = {}
    Guard(Map(Str,int)) v4 = {}
    v4.init()
    Box v5 = Box{ name: "0", nums: [1, 4, -3] }
    Str v6 = match -1.0 { 1.5 => "1"  2.5 => f"two"  _ => "y" }
    Box v7 = Box{ name: "", nums: [-1, 3] }
    Arena(int) v8 = {}
    return 0
}
