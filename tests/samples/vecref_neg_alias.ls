import std.core.vec

/* Neg: same Vec cannot appear as both &!v and (auto) v in one call. */
def f(&!Vec(int) a, &Vec(int) b) {
    a.push(1)
    @print(b.len())
}

def main() -> int {
    Vec(int) v = {}
    f(&!v, v)     /* aliasing: reject */
    return 0
}
