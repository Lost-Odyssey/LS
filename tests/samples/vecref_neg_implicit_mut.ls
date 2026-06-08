import std.vec

/* Neg: &!vec must be explicit at the call site — no auto-upgrade. */
fn f(&!Vec(int) v) {
    v.push(1)
}

fn main() -> int {
    Vec(int) v
    f(v)       /* should require &!v */
    return 0
}
