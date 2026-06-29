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
    Guard(Vec(int)) v2 = {}
    v2.init()
    Arena(int) v3 = {}
    int v4 = v1.read(int)(|b| { return b.nums.len() })
    Box v5 = Box{ name: f"{0}", nums: [v4 * 2] }
    v3.reset()
    return 0
}
