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
    Map(Str,Vec(int)) v1 = {}
    RwLock(Box) v2 = {}
    v2.init()
    Map(Str,Vec(int)) v3 = {}
    Vec(int) v4 = [1]
    int v5 = v2.read(int)(|b| { return b.nums.len() })
    int v6 = v5
    Vec(Str) v7 = []
    int v8 = v5
    Guard(Vec(int)) v9 = {}
    v9.init()
    return 0
}
