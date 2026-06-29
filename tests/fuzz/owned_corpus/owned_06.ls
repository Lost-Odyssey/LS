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
    Map(Str,int) v2 = {}
    Guard(Map(Str,int)) v3 = {}
    v3.init()
    Guard(Str) v4 = {}
    v4.init()
    v4.lock(|w| { w.push_str("the quick") })
    Guard(Vec(int)) v5 = {}
    v5.init()
    Str v6 = "a,b,c"
    SpinGuard(Str) v7 = {}
    int v8 = v5.get(int)(|w| { return w.len() })
    return 0
}
