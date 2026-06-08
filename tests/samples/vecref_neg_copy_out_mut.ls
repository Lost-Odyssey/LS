import std.vec

/* Neg: cannot copy out of &!Vec(T) either. */
fn f(&!Vec(int) v) {
    Vec(int) t = v
}

fn main() -> int {
    Vec(int) v
    f(&!v)
    return 0
}
