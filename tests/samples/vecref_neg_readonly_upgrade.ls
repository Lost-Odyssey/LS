import std.vec

/* Neg: cannot take &!v of a read-only borrow. */
fn inner(&!Vec(int) v) {
    v.push(1)
}

fn outer(&Vec(int) v) {
    inner(&!v)   /* upgrade not allowed */
}

fn main() -> int {
    Vec(int) v
    outer(v)
    return 0
}
