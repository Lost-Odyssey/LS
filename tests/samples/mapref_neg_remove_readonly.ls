/* Neg: &map cannot call remove. */
fn f(&map(string, int) m) {
    m.remove("x")
}

fn main() -> int {
    map(string, int) m
    f(m)
    return 0
}
