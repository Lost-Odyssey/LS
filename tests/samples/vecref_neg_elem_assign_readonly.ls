import std.vec

/* Neg: &Vec(T) cannot write v[i] = x. */
fn f(&Vec(int) v) {
    v[0] = 99
}

fn main() -> int {
    Vec(int) v = {}
    v.push(1)
    f(v)
    return 0
}
