import std.core.vec

/* Neg: &Vec(T) cannot call mutating method push. */
def f(&Vec(int) v) {
    v.push(1)
}

def main() -> int {
    Vec(int) v = {}
    f(v)
    return 0
}
