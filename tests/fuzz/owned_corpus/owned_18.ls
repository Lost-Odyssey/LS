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
    Box v1 = Box{ name: f"{0}", nums: [4, 1, 0] }
    Guard(Map(Str,int)) v2 = {}
    v2.init()
    RwLock(Box) v3 = {}
    v3.init()
    int v4 = v2.get(int)(|w| { return w.len() })
    Vec(int) v5 = [6, v4 * 2, v4]
    Str v6 = "the quick"
    Vec(int) v7 = v5.filter(|x| x > 0).map(int)(|x| x + 1)
    Vec(Vec(int)) v8 = []
    Vec(Vec(int)) v9 = []
    return 0
}
