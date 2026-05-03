/* Phase 5.6: &vec(T) read-only borrow — pointer ABI, caller retains ownership.
   Callee can read/iterate but not mutate. */
fn sum_all(&vec(int) v) -> int {
    int s = 0
    for (int i = 0; i < v.length; i = i + 1) {
        s = s + v[i]
    }
    return s
}

fn main() -> int {
    vec(int) v
    v.push(10)
    v.push(20)
    v.push(30)
    int total = sum_all(v)  /* auto-borrow */
    print(total)  /* expect: 60 */
    print(v.length)  /* expect: 3 — caller still owns */
    return 0
}
