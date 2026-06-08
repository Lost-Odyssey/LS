import std.vec

/* Neg: &Vec(T) cannot call mutating method push. */
fn f(&Vec(int) v) {
    v.push(1)
}

fn main() -> int {
    Vec(int) v = {}
    f(v)
    return 0
}
