/* Neg: cannot pass &!m and &m (or &!m twice) aliased in same call. */
fn both(&!map(string, int) a, &map(string, int) b) {
    a.set("x", 1)
}

fn main() -> int {
    map(string, int) m
    both(&!m, m)
    return 0
}
