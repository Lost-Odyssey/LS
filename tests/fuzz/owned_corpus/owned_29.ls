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
    SpinGuard(Str) v1 = {}
    RwLock(Box) v2 = {}
    v2.init()
    Str v3 = match 0 { 0 => f"z"  1 | 2 => "hi"  _ => "zzz" }
    RwLock(Box) v4 = {}
    v4.init()
    Vec(Str) v5 = ["zzz", "a,b,c", "0"]
    for v6 in 0..5 { v3 = v3 + f"{v6}" }
    return 0
}
