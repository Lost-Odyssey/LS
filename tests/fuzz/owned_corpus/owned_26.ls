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
    int v2 = 1
    Str v3 = match v2 * 2 { 0 => f"z"  1 | 2 => "hi"  _ => "x" }
    Str v4 = f"{v2 * 2}"
    Vec(int) v5 = [2, -3, 1, 2]
    return 0
}
