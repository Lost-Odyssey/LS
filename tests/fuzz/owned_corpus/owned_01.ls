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
    Box v1 = Box{ name: "alpha", nums: [1, -1] }
    Tag v2 = B(9)
    Str v3 = "val"
    Box v4 = Box{ name: f"{v3}-{v3}", nums: [1] }
    Box v5 = Box{ name: f"{v3}-{v3}", nums: [1] }
    Map(Str,int) v6 = {}
    @print(v1.name.len())
    Str v7 = match v2 { A(s) => s  B(k) => f"{k}"  C => "c" }
    return 0
}
