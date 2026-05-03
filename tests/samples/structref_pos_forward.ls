/* Phase 5.8: &!struct chain forwarding. */
struct Counter {
    int n;
}

fn bump(&!Counter c) {
    c.n = c.n + 1
}

fn bump_twice(&!Counter c) {
    bump(&!c)
    bump(&!c)
}

fn main() -> int {
    Counter c = Counter { n: 0 }
    bump_twice(&!c)
    bump_twice(&!c)
    print(c.n)                /* expect: 4 */
    return 0
}
