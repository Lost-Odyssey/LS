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
    Vec(Str) v1 = ["val", "y", "gamma"]
    Vec(int) v2 = []
    Guard(Map(Str,int)) v3 = {}
    v3.init()
    int v4 = match 9 { 0 => 0  1 | 2 | 3 => 1  _ => 5 }
    Str v5 = match 1 { 0 => f"z"  1 | 2 => f"{9}"  _ => "  " }
    Tree v6 = Leaf(-2)
    Vec(Vec(int)) v7 = []
    v7.push(mk_veci(9))
    Str v8 = match v1.get(4) { Some(x) => x  None => "hi" }
    Map(Str,Vec(int)) v9 = {}
    return 0
}
