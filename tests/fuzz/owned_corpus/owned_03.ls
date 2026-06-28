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
    Tag v1 = C
    Vec(int) v2 = []
    Map(Str,Vec(int)) v3 = {}
    Guard(Vec(int)) v4 = {}
    v4.init()
    Map(Str,int) v5 = {}
    Vec(int) v6 = v2.filter(|x| x > 0).map(int)(|x| x + 1)
    int v7 = 8
    return 0
}
