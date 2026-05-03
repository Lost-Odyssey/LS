/* Neg: cannot copy out of &vec(T) into a new owned vec. */
fn f(&vec(int) v) {
    vec(int) t = v
}

fn main() -> int {
    vec(int) v
    f(v)
    return 0
}
