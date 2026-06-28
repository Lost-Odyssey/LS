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
    Guard(Str) v1 = {}
    v1.init()
    Map(Str,int) v2 = {}
    Str v3 = "the quick"
    Str v4 = f"{v3}-{v3}"
    Vec(Str) v5 = ["hi", "beta", "1"]
    match v5.get(0) { Some(s) => { @print(s) } None => {} }
    v5.pop()
    return 0
}
