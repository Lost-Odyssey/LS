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
    Vec(int) v1 = [8, 1]
    Map(Str,Vec(int)) v2 = {}
    Str v3 = match 5 { 0 => f"z"  1 | 2 => f"{-3}"  _ => "1" }
    Str v4 = match -1.0 { 1.5 => "hi"  2.5 => f"two"  _ => "hi" }
    return 0
}
