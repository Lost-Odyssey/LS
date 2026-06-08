import std.vec

/* Phase 5.6: &!Vec(T) — v[i] = x writes through to caller. */
fn double_each(&!Vec(int) v) {
    for (int i = 0; i < v.len(); i = i + 1) {
        v[i] = v[i] * 2
    }
}

fn main() -> int {
    Vec(int) v = {}
    v.push(3)
    v.push(5)
    v.push(7)
    double_each(&!v)
    print(v[0])  /* expect: 6 */
    print(v[1])  /* expect: 10 */
    print(v[2])  /* expect: 14 */
    return 0
}
