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
    Vec(Str) v2 = ["beta", "beta"]
    Map(Str,Vec(int)) v3 = {}
    Str v4 = "a,b,c"
    int v5 = match 3 { 0 => 0  1 | 2 | 3 => 1  _ => 8 }
    SpinGuard(Str) v6 = {}
    Str v7 = match 2.5 { 1.5 => "beta"  2.5 => f"two"  _ => "-5" }
    Str v8 = match -1 { 0 => f"z"  1 | 2 => "the quick"  _ => "  " }
    return 0
}
