/* Neg: &map cannot call set. */
fn f(&map(string, int) m) {
    m.set("x", 1)
}

fn main() -> int {
    map(string, int) m
    f(m)
    return 0
}
