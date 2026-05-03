/* Phase 5.7: &!map forwarding + remove. */
fn drop_key(&!map(string, int) m, string key) {
    if m.contains_key(key) { m.remove(key) }
}

fn cleanup(&!map(string, int) m) {
    drop_key(&!m, "tmp")
    drop_key(&!m, "debug")
}

fn main() -> int {
    map(string, int) m
    m.set("keep", 1)
    m.set("tmp", 2)
    m.set("debug", 3)
    cleanup(&!m)
    print(m.length)                /* expect: 1 */
    print(m.contains_key("keep"))  /* expect: true */
    print(m.contains_key("tmp"))   /* expect: false */
    return 0
}
