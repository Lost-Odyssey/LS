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
    Str v1 = "the quick"
    Box v2 = Box{ name: "-5", nums: [] }
    Guard(Str) v3 = {}
    v3.init()
    Guard(Str) v4 = {}
    v4.init()
    Map(Str,Vec(int)) v5 = {}
    Vec(Vec(int)) v6 = []
    Tag v7 = A(f"{-2}")
    Map(Str,int) v8 = {}
    match v5.get(f"{7}") { Some(inner) => { @print(inner.len()) } None => {} }
    Vec(Vec(int)) v9 = []
    return 0
}
