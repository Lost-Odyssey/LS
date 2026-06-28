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
    Map(Str,Vec(int)) v2 = {}
    Guard(Map(Str,int)) v3 = {}
    v3.init()
    Vec(Vec(int)) v4 = []
    Str v5 = f"{-1}"
    Vec(Str) v6 = ["the quick", "x", "zzz"]
    match v1.get("gamma") { Some(inner) => { @print(inner.len()) } None => {} }
    for s in &v6 { @print(s.len()) }
    Guard(Map(Str,int)) v7 = {}
    v7.init()
    @print(v6.len())
    v2.set("the quick", mk_veci(2))
    int v8 = v3.get(int)(|w| { return w.len() })
    Str v9 = f"{v5}-{v5}"
    return 0
}
