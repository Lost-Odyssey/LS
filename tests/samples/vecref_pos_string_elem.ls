import std.vec

/* Phase 5.6: &!Vec(Str) — push owned Str elements through a borrow. */
fn add_words(&!Vec(Str) v) {
    v.push("hello".upper())
    v.push("world".upper())
}

fn main() -> int {
    Vec(Str) v = {}
    v.push("start".upper())
    add_words(&!v)
    print(v.len())  /* expect: 3 */
    print(v[0])      /* expect: START */
    print(v[1])      /* expect: HELLO */
    print(v[2])      /* expect: WORLD */
    return 0
}
