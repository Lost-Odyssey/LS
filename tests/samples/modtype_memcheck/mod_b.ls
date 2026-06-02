module mod_b

/* Same names as mod_a (Node, Box) but DIFFERENT layouts. */
struct Node { int id; string desc }
impl Node {
    fn tag(&self) -> string { return self.desc }
}
fn make() -> Node { return Node { id: 10, desc: "B".upper() } }

enum Box { Empty, Holds(string) }
fn wrap() -> Box { return Holds("BX".upper()) }
fn unwrap(Box b) -> string { match b { Empty => { return "none" } Holds(v) => { return v } } }

fn nodes() -> vec(Node) {
    vec(Node) v = []
    v.push(Node { id: 1, desc: "b1".upper() })
    return v
}
