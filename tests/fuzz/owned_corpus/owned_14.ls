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
    Tag v1 = C
    Vec(Vec(int)) v2 = []
    Vec(Vec(int)) v3 = []
    match v1 { A(s) => { @print(s.len()) } B(n) => { @print(n) } C => {} }
    int v4 = 0
    Guard(Str) v5 = {}
    v5.init()
    Str v6 = match v1 { A(s) => s  B(k) => f"{k}"  C => "c" }
    @print(v2.len())
    Vec(int) v7 = [6]
    Guard(Str) v8 = {}
    v8.init()
    Str v9 = match v1 { A(s) => s  B(k) => f"{k}"  C => "c" }
    Str v10 = match v1 { A(s) => s  B(k) => f"{k}"  C => "c" }
    int v11 = v7.get(v4).unwrap_or(v4 * 2)
    return 0
}
