/* Neg: cannot copy out of &!map into a new owned map. */
fn f(&!map(string, int) m) {
    map(string, int) t = m
}

fn main() -> int {
    map(string, int) m
    f(&!m)
    return 0
}
