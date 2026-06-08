import std.vec

/* Phase 5.6: &!Vec(T) supports pop/clear/reverse; &Vec(T) supports read-only methods. */
fn info(&Vec(int) v) -> int {
    if v.empty?() { return -1 }
    return v.len() + v[0]
}

fn shrink(&!Vec(int) v) {
    v.pop()
    v.reverse()
}

fn main() -> int {
    Vec(int) v = {}
    v.push(1)
    v.push(2)
    v.push(3)
    print(info(v))   /* expect: 4 (len=3 + v[0]=1) */
    shrink(&!v)
    print(v.len())  /* expect: 2 */
    print(v[0])      /* expect: 2 (reversed from [1,2]) */
    print(v[1])      /* expect: 1 */
    return 0
}
