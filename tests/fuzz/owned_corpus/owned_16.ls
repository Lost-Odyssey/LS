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
    int v1 = 0
    Vec(Vec(int)) v2 = []
    Vec(Str) v3 = []
    Tag v4 = B(0)
    Str v5 = match v4 { A(inner) => match v1 { 0 => f"z" _ => inner } B(k) => f"{k}" C => "c" }
    Vec(Str) v6 = ["1", "  "]
    Str v7 = v3.get(-2).unwrap_or("zzz")
    Vec(Vec(int)) v8 = []
    v2.push(mk_veci(v1 * 2))
    return 0
}
