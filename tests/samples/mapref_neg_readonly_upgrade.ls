/* Neg: &map (read-only) cannot be passed to &!map parameter. */
fn sink(&!map(string, int) m) {
    m.set("x", 1)
}

fn forward(&map(string, int) m) {
    sink(&!m)
}

fn main() -> int {
    map(string, int) m
    forward(m)
    return 0
}
