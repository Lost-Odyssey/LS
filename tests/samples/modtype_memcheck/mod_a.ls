module mod_a

import std.vec

/* has_drop struct (owned string field) + same name as mod_b's. */
struct Node { string label; int weight }
impl Node {
    fn tag(&self) -> string { return self.label }
}
/* Build inline (owned string literal in the struct literal) — avoids the
   string-param-into-returned-struct AOT path tracked as BF-045. */
fn make() -> Node { return Node { label: "A".upper(), weight: 1 } }

/* has_drop enum (string payload) — same name as mod_b's. */
enum Box { Empty, Holds(string) }
fn wrap() -> Box { return Holds("AX".upper()) }
fn unwrap(Box b) -> string { match b { Empty => { return "none" } Holds(v) => { return v } } }

/* function returning a vec of has_drop structs (built inline) */
fn nodes() -> Vec(Node) {
    Vec(Node) v = {}
    v.push(Node { label: "a1".upper(), weight: 1 })
    v.push(Node { label: "a2".upper(), weight: 2 })
    return v
}
