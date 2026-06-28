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
    RwLock(Box) v1 = {}
    v1.init()
    Tag v2 = A(f"{-1}")
    Tag v3 = C
    Box v4 = Box{ name: "hi", nums: [] }
    RwLock(Box) v5 = {}
    v5.init()
    Vec(Vec(int)) v6 = []
    return 0
}
