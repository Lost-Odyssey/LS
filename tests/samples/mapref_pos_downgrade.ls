/* Phase 5.7: &!map -> &map downgrade. */
fn read_only(&map(string, int) m) -> int {
    return m.get("x") + m.get("y")
}

fn mut_caller(&!map(string, int) m) -> int {
    m.set("z", 100)
    return read_only(m)    /* downgrade */
}

fn main() -> int {
    map(string, int) m
    m.set("x", 3)
    m.set("y", 5)
    int r = mut_caller(&!m)
    print(r)              /* expect: 8 */
    print(m.length)       /* expect: 3 */
    print(m.get("z"))     /* expect: 100 */
    return 0
}
