import std.core.vec

/* Neg: cannot copy out of &Vec(T) into a new owned vec. */
def f(&Vec(int) v) {
    Vec(int) t = v
}

def main() -> int {
    Vec(int) v = {}
    f(v)
    return 0
}
