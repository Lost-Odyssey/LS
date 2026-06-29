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
    Box v1 = Box{ name: "  ", nums: [0] }
    Guard(Map(Str,int)) v2 = {}
    v2.init()
    RwLock(Box) v3 = {}
    v3.init()
    Guard(Vec(int)) v4 = {}
    v4.init()
    Str v5 = match -3 { 0 => f"z"  1 | 2 => "alpha"  _ => "  " }
    Box v6 = Box{ name: "alpha", nums: [2, 9] }
    v2.lock(|w| { w.set("beta", 9) })
    for v7 in 0..3 { v5 = v5 + f"{v7}" }
    v3.write(|b| { b.nums.push(5) })
    for v8 in 0..5 { v5 = v5 + f"{v8}" }
    return 0
}
