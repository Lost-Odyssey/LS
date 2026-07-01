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
    Guard(Str) v1 = {}
    v1.init()
    Vec(Vec(int)) v2 = []
    Arena(int) v3 = {}
    Guard(Vec(int)) v4 = {}
    v4.init()
    Vec(int) v5 = [-2]
    Str v6 = match 1.5 { 1.5 => f"{2}"  2.5 => f"two"  _ => "-5" }
    Guard(Vec(int)) v7 = {}
    v7.init()
    SpinGuard(Str) v8 = {}
    RwLock(Box) v9 = {}
    v9.init()
    RwLock(Box) v10 = {}
    v10.init()
    Tag v11 = C
    return 0
}
