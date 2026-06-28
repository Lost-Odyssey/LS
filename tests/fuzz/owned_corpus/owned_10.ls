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
    Vec(Vec(int)) v1 = []
    Vec(Vec(int)) v2 = []
    int v3 = 3
    Tag v4 = B(v3)
    Vec(Str) v5 = ["key", "-5", "val"]
    v5.pop()
    @print(v5.len())
    return 0
}
