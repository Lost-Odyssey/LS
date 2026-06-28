import std.core.vec

/* Neg: &!vec must be explicit at the call site — no auto-upgrade. */
def f(&!Vec(int) v) {
    v.push(1)
}

def main() -> int {
    Vec(int) v = {}
    f(v)       /* should require &!v */
    return 0
}
