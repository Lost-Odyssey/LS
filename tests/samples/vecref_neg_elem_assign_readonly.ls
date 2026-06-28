import std.core.vec

/* Neg: &Vec(T) cannot write v[i] = x. */
def f(&Vec(int) v) {
    v[0] = 99
}

def main() -> int {
    Vec(int) v = {}
    v.push(1)
    f(v)
    return 0
}
