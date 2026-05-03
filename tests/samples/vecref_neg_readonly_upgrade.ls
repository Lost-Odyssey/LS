/* Neg: cannot take &!v of a read-only borrow. */
fn inner(&!vec(int) v) {
    v.push(1)
}

fn outer(&vec(int) v) {
    inner(&!v)   /* upgrade not allowed */
}

fn main() -> int {
    vec(int) v
    outer(v)
    return 0
}
