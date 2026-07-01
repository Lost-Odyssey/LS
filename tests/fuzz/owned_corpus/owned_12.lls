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
    int v1 = 4
    Box v2 = Box{ name: "1", nums: [] }
    Str v3 = f"{8}"
    Str v4 = match 0.0 { 1.5 => f"{v1 * 2}"  2.5 => f"two"  _ => "beta" }
    Guard(Map(Str,int)) v5 = {}
    v5.init()
    Map(Str,int) v6 = {}
    Map(Str,Vec(int)) v7 = {}
    return 0
}
