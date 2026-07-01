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
    Tag v1 = B(-3)
    Str v2 = f"{6}"
    Str v3 = match 1.5 { 1.5 => "hi"  2.5 => f"two"  _ => "1" }
    Vec(Str) v4 = ["-5", "1"]
    Box v5 = Box{ name: "gamma", nums: [3, 5, 8] }
    Str v6 = match v1 { A(inner) => match 4 { 0 => f"z" _ => inner } B(k) => f"{k}" C => "c" }
    Str v7 = match v4.get(3) { Some(x) => x  None => "0" }
    Str v8 = v4.get(0).unwrap_or("-5")
    for v9 in 0..3 { v2 = v2 + f"{v9}" }
    Vec(Vec(int)) v10 = []
    return 0
}
