/* Neg: &vec(T) cannot write v[i] = x. */
fn f(&vec(int) v) {
    v[0] = 99
}

fn main() -> int {
    vec(int) v
    v.push(1)
    f(v)
    return 0
}
