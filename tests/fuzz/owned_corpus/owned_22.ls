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
    RwLock(Box) v1 = {}
    v1.init()
    RwLock(Box) v2 = {}
    v2.init()
    Guard(Vec(int)) v3 = {}
    v3.init()
    Tag v4 = C
    Vec(Str) v5 = ["", "-5", "the quick"]
    v1.write(|b| { b.nums.push(0) })
    Str v6 = match v4 { A(s) => s  B(k) => f"{k}"  C => "c" }
    return 0
}
