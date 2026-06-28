module mod_a

import std.core.vec
import std.core.str

/* has_drop struct (owned Str field) + same name as mod_b's. */
struct Node { Str label; int weight }
methods Node {
    def tag(&self) -> Str { return self.label }
}
/* Build inline (owned Str literal in the struct literal) — avoids the
   Str-param-into-returned-struct AOT path tracked as BF-045. */
def make() -> Node { return Node { label: "A".upper(), weight: 1 } }

/* has_drop enum (Str payload) — same name as mod_b's. */
enum Box { Empty, Holds(Str) }
def wrap() -> Box { return Holds("AX".upper()) }
def unwrap(Box b) -> Str { match b { Empty => { return "none" } Holds(v) => { return v } } }

/* function returning a vec of has_drop structs (built inline) */
def nodes() -> Vec(Node) {
    Vec(Node) v = {}
    v.push(Node { label: "a1".upper(), weight: 1 })
    v.push(Node { label: "a2".upper(), weight: 2 })
    return v
}
