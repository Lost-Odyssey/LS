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
    Vec(Str) v1 = ["x"]
    Str v2 = f"{-3}"
    Tree v3 = Leaf(2)
    Str v4 = match v1.get(0) { Some(x) => x  None => "zzz" }
    Guard(Vec(int)) v5 = {}
    v5.init()
    @print(v1.len())
    Tree v6 = Leaf(6)
    Str v7 = match v1.get(8) { Some(x) => x  None => "y" }
    Tag v8 = A("0")
    Str v9 = v1.get(4).unwrap_or("y")
    Str v10 = match v1.get(4) { Some(x) => x  None => "val" }
    int v11 = 0
    return 0
}
