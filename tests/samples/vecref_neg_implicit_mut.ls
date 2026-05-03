/* Neg: &!vec must be explicit at the call site — no auto-upgrade. */
fn f(&!vec(int) v) {
    v.push(1)
}

fn main() -> int {
    vec(int) v
    f(v)       /* should require &!v */
    return 0
}
