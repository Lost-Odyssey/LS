/* Phase 5.8: &!struct chain forwarding. */
struct Counter {
    int n;
}

def bump(&!Counter c) {
    c.n = c.n + 1
}

def bump_twice(&!Counter c) {
    bump(&!c)
    bump(&!c)
}

def main() -> int {
    Counter c = Counter { n: 0 }
    bump_twice(&!c)
    bump_twice(&!c)
    @print(c.n)                /* expect: 4 */
    return 0
}
