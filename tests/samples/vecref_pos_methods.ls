/* Phase 5.6: &!vec(T) supports pop/clear/reverse; &vec(T) supports read-only methods. */
fn info(&vec(int) v) -> int {
    if v.is_empty() { return -1 }
    return v.length + v[0]
}

fn shrink(&!vec(int) v) {
    v.pop()
    v.reverse()
}

fn main() -> int {
    vec(int) v
    v.push(1)
    v.push(2)
    v.push(3)
    print(info(v))   /* expect: 4 (len=3 + v[0]=1) */
    shrink(&!v)
    print(v.length)  /* expect: 2 */
    print(v[0])      /* expect: 2 (reversed from [1,2]) */
    print(v[1])      /* expect: 1 */
    return 0
}
