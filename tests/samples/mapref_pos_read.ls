/* Phase 5.7: &map(K,V) read-only borrow — auto-borrow, read via .get/.contains_key. */
fn lookup(&map(string, int) m, string key) -> int {
    if m.contains_key(key) {
        return m.get(key)
    }
    return -1
}

fn main() -> int {
    map(string, int) m
    m.set("alice", 95)
    m.set("bob", 87)
    print(lookup(m, "alice"))   /* expect: 95 */
    print(lookup(m, "carol"))   /* expect: -1 */
    print(m.length)             /* expect: 2 — caller still owns */
    return 0
}
