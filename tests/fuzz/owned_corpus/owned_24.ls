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
    Guard(Map(Str,int)) v1 = {}
    v1.init()
    Guard(Map(Str,int)) v2 = {}
    v2.init()
    Box v3 = Box{ name: "x", nums: [5, 9, 2] }
    int v4 = v2.get(int)(|w| { return w.len() })
    Guard(Vec(int)) v5 = {}
    v5.init()
    @print(v3.nums.len())
    return 0
}
