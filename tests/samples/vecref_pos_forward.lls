import std.core.vec

/* Phase 5.6: &!vec chained forwarding — pointer transparently passes through. */
def inner(&!Vec(int) v) {
    v.push(100)
}

def outer(&!Vec(int) v) {
    v.push(10)
    inner(&!v)
    v.push(20)
}

def main() -> int {
    Vec(int) v = {}
    outer(&!v)
    @print(v.len())  /* expect: 3 */
    @print(v[0])      /* expect: 10 */
    @print(v[1])      /* expect: 100 */
    @print(v[2])      /* expect: 20 */
    return 0
}
