module mod_b

import std.core.vec
import std.core.str

/* Same names as mod_a (Node, Box) but DIFFERENT layouts. */
struct Node { int id; Str desc }
methods Node {
    def tag(&self) -> Str { return self.desc }
}
def make() -> Node { return Node { id: 10, desc: "B".upper() } }

enum Box { Empty, Holds(Str) }
def wrap() -> Box { return Holds("BX".upper()) }
def unwrap(Box b) -> Str { match b { Empty => { return "none" } Holds(v) => { return v } } }

def nodes() -> Vec(Node) {
    Vec(Node) v = {}
    v.push(Node { id: 1, desc: "b1".upper() })
    return v
}
