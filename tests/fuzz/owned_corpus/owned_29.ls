import std.core.vec
import std.core.map
import std.core.str
import std.sync.lock

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
    Map(Str,int) v1 = {}
    Map(Str,int) v2 = {}
    Map(Str,int) v3 = {}
    Guard(Str) v4 = {}
    v4.init()
    int v5 = v4.get(int)(|w| { return w.len() })
    SpinGuard(Str) v6 = {}
    v4.lock(|w| { w.push_str("zzz") })
    Box v7 = Box{ name: f"{v5}", nums: [4] }
    Vec(Str) v8 = []
    return 0
}
