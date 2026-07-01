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
    Str v1 = "a,b,c"
    Map(Str,int) v2 = {}
    Str v3 = v1 + "!"
    int v4 = match 8 { 0 => 0  1 | 2 | 3 => 1  _ => -2 }
    Str v5 = match 0 { 0 => f"z"  1 | 2 => "  "  _ => "y" }
    return 0
}
