/* Neg: &vec(T) cannot call mutating method push. */
fn f(&vec(int) v) {
    v.push(1)
}

fn main() -> int {
    vec(int) v
    f(v)
    return 0
}
