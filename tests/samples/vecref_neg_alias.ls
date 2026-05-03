/* Neg: same vec cannot appear as both &!v and (auto) v in one call. */
fn f(&!vec(int) a, &vec(int) b) {
    a.push(1)
    print(b.length)
}

fn main() -> int {
    vec(int) v
    f(&!v, v)     /* aliasing: reject */
    return 0
}
