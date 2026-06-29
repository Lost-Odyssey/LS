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
    Vec(Vec(int)) v2 = []
    RwLock(Box) v3 = {}
    v3.init()
    Vec(Str) v4 = ["hi", "key", "y"]
    Str v5 = v4.get(3).unwrap_or("y")
    Str v6 = v4.get(6).unwrap_or("val")
    v3.write(|b| { b.nums.push(2) })
    return 0
}
