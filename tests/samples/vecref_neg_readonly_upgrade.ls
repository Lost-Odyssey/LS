import std.core.vec

/* Neg: cannot take &!v of a read-only borrow. */
def inner(&!Vec(int) v) {
    v.push(1)
}

def outer(&Vec(int) v) {
    inner(&!v)   /* upgrade not allowed */
}

def main() -> int {
    Vec(int) v = {}
    outer(v)
    return 0
}
