module mod_b

import std.vec
import std.str

/* Same names as mod_a (Node, Box) but DIFFERENT layouts. */
struct Node { int id; Str desc }
impl Node {
    fn tag(&self) -> Str { return self.desc }
}
fn make() -> Node { return Node { id: 10, desc: "B".upper() } }

enum Box { Empty, Holds(Str) }
fn wrap() -> Box { return Holds("BX".upper()) }
fn unwrap(Box b) -> Str { match b { Empty => { return "none" } Holds(v) => { return v } } }

fn nodes() -> Vec(Node) {
    Vec(Node) v = {}
    v.push(Node { id: 1, desc: "b1".upper() })
    return v
}
