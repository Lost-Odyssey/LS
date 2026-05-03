/* Phase 5.6: &!vec(T) writable borrow — push propagates to caller. */
fn grow(&!vec(int) v, int n) {
    for (int i = 0; i < n; i = i + 1) {
        v.push(i * 10)
    }
}

fn main() -> int {
    vec(int) v
    v.push(1)
    grow(&!v, 3)
    print(v.length)  /* expect: 4 */
    print(v[0])      /* expect: 1 */
    print(v[1])      /* expect: 0 */
    print(v[2])      /* expect: 10 */
    print(v[3])      /* expect: 20 */
    return 0
}
