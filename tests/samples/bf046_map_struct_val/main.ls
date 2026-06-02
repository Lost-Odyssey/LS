/* BF-046 regression: map.set DEEP-COPIES the value into the node. When the value
   arg is a TEMPORARY has_drop struct/enum rvalue (inline `N { ... }` / `Holds(..)`),
   the original temp's owned fields (a string) were never released → leak. Fix
   registers a statement-end drop for the temp. Named-IDENT values keep their scope
   drop (must not double-drop). memcheck must be clean. */

struct N { string s; int n }
enum  E { Empty, Holds(string) }

fn main() -> int {
    map(string, N) m
    /* inline temp struct value (the leaking case) */
    m.set("k1".upper(), N { s: "v1".upper(), n: 1 })
    /* named value (must not regress / double-drop) */
    N v = N { s: "v2".upper(), n: 2 }
    m.set("k2".upper(), v)
    /* read values back */
    N a = m["k1".upper()]
    N b = m["k2".upper()]
    print(f"a={a.s} b={b.s}")

    /* has_drop enum value, inline temp */
    map(string, E) e
    e.set("e".upper(), Holds("payload".upper()))
    E ev = e["e".upper()]
    string es = "?"
    match ev { Empty => { es = "empty" } Holds(p) => { es = p } }
    print(f"e={es}")

    if a.s == "V1" && b.s == "V2" && es == "PAYLOAD" {
        print("BF046 PASS")
    }
    return 0
}
