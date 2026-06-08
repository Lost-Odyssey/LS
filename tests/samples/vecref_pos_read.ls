import std.vec

/* Phase 5.6: &Vec(T) read-only borrow — pointer ABI, caller retains ownership.
   Callee can read/iterate but not mutate. */
fn sum_all(&Vec(int) v) -> int {
    int s = 0
    for (int i = 0; i < v.len(); i = i + 1) {
        s = s + v[i]
    }
    return s
}

fn main() -> int {
    Vec(int) v
    v.push(10)
    v.push(20)
    v.push(30)
    int total = sum_all(v)  /* auto-borrow */
    print(total)  /* expect: 60 */
    print(v.len())  /* expect: 3 — caller still owns */
    return 0
}
