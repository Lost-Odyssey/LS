import std.core.vec
import std.core.map
import std.core.str
import std.sync.lock
import std.mem.arena

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
    RwLock(Box) v2 = {}
    v2.init()
    Tag v3 = A(f"{7}")
    RwLock(Box) v4 = {}
    v4.init()
    Vec(int) v5 = [8, 6, 4, 6]
    v5.push(0)
    Tag v6 = A("zzz")
    Str v7 = match 5 { 0 => f"z"  1 | 2 => "gamma"  _ => "  " }
    Vec(Vec(int)) v8 = []
    Vec(Str) v9 = ["x", "zzz", "0"]
    int v10 = -3
    Str v11 = f"{-2}"
    v4.write(|b| { b.nums.push(v10) })
    Vec(Str) v12 = []
    return 0
}
