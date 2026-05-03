/* Phase 5.7: &!map(K,V) — set propagates to caller. */
fn populate(&!map(string, int) m) {
    m.set("a", 1)
    m.set("b", 2)
    m.set("c", 3)
}

fn main() -> int {
    map(string, int) m
    populate(&!m)
    print(m.length)           /* expect: 3 */
    print(m.get("a"))         /* expect: 1 */
    print(m.get("b"))         /* expect: 2 */
    print(m.get("c"))         /* expect: 3 */
    return 0
}
