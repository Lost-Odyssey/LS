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
    SpinGuard(Str) v1 = {}
    int v2 = 2
    Vec(Str) v3 = ["zzz", "gamma", "zzz"]
    Str v4 = v3.get(v2 * 2).unwrap_or("hi")
    Guard(Map(Str,int)) v5 = {}
    v5.init()
    int v6 = v2 + 1
    SpinGuard(Str) v7 = {}
    Box v8 = Box{ name: "", nums: [v6 * 2, 5] }
    int v9 = v6 * 2
    int v10 = v1.get(int)(|w| { return w.len() })
    @print(v8.name.len())
    return 0
}
