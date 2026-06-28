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
    int v1 = 3
    RwLock(Box) v2 = {}
    v2.init()
    SpinGuard(Str) v3 = {}
    Vec(Str) v4 = ["x"]
    Guard(Str) v5 = {}
    v5.init()
    Map(Str,int) v6 = {}
    Str v7 = match v4.get(v1 * 2) { Some(x) => x  None => "0" }
    v2.write(|b| { b.nums.push(0) })
    Vec(int) v8 = [v1 * 2, v1 * 2, v1 * 2]
    return 0
}
