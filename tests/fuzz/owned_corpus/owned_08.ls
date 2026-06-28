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
    Vec(Vec(int)) v2 = []
    Guard(Map(Str,int)) v3 = {}
    v3.init()
    Vec(Str) v4 = []
    Str v5 = v4.get(6).unwrap_or("the quick")
    Str v6 = v4.get(4).unwrap_or("beta")
    Str v7 = match v4.get(2) { Some(x) => x  None => "0" }
    Str v8 = v4.get(1).unwrap_or("gamma")
    Guard(Str) v9 = {}
    v9.init()
    int v10 = 0
    Guard(Vec(int)) v11 = {}
    v11.init()
    return 0
}
