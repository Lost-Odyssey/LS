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
    Map(Str,Vec(int)) v2 = {}
    Vec(int) v3 = [-2, -2]
    Vec(Str) v4 = ["beta", "gamma"]
    Map(Str,int) v5 = {}
    SpinGuard(Str) v6 = {}
    SpinGuard(Str) v7 = {}
    Str v8 = "hi"
    Guard(Str) v9 = {}
    v9.init()
    Str v10 = match 0.0 { 1.5 => v8 + "!"  2.5 => f"two"  _ => "the quick" }
    Str v11 = v4.get(-3).unwrap_or("val")
    Vec(int) v12 = v3.map(int)(|x| x + 9)
    Arena(int) v13 = {}
    return 0
}
