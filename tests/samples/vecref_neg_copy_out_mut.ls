/* Neg: cannot copy out of &!vec(T) either. */
fn f(&!vec(int) v) {
    vec(int) t = v
}

fn main() -> int {
    vec(int) v
    f(&!v)
    return 0
}
