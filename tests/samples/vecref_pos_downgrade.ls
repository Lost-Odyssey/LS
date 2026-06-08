import std.vec

/* Phase 5.6: &!Vec(T) can downgrade to &Vec(T) at a nested call site. */
fn read_only(&Vec(int) v) -> int {
    return v[0] + v[1]
}

fn mut_caller(&!Vec(int) v) -> int {
    v.push(99)
    int r = read_only(v)  /* &!vec -> &vec downgrade */
    return r
}

fn main() -> int {
    Vec(int) v = {}
    v.push(4)
    v.push(6)
    int r = mut_caller(&!v)
    print(r)         /* expect: 10 (4+6) */
    print(v.len())  /* expect: 3 */
    print(v[2])      /* expect: 99 */
    return 0
}
