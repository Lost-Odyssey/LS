/* Neg: &map cannot call clear. */
fn f(&map(string, int) m) {
    m.clear()
}

fn main() -> int {
    map(string, int) m
    f(m)
    return 0
}
