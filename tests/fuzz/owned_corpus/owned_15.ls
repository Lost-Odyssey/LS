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
    SpinGuard(Str) v1 = {}
    Vec(Str) v2 = []
    Guard(Str) v3 = {}
    v3.init()
    Box v4 = Box{ name: f"{9}", nums: [8, 8] }
    int v5 = v3.get(int)(|w| { return w.len() })
    Guard(Str) v6 = {}
    v6.init()
    Arena(int) v7 = {}
    Vec(Str) v8 = ["key"]
    Guard(Map(Str,int)) v9 = {}
    v9.init()
    Box v10 = Box{ name: f"{8}", nums: [4, v5] }
    Str v11 = match 1.5 { 1.5 => f"{3}"  2.5 => f"two"  _ => "a,b,c" }
    Guard(Str) v12 = {}
    v12.init()
    for v13 in 0..3 { v11 = v11 + f"{v13}" }
    return 0
}
