/* BF-046 regression: map.set DEEP-COPIES the value into the node. When the value
   arg is a TEMPORARY has_drop struct/enum rvalue (inline `N { ... }` / `Holds(..)`),
   the original temp's owned fields (a Str) were never released → leak. Fix
   registers a statement-end drop for the temp. Named-IDENT values keep their scope
   drop (must not double-drop). memcheck must be clean. */

import std.core.map
import std.core.str

struct N { Str s; int n }
enum  E { Empty, Holds(Str) }

def main() -> int {
    Map(Str, N) m
    /* inline temp struct value (the leaking case) */
    m.set("k1".upper(), N { s: "v1".upper(), n: 1 })
    /* named value (must not regress / double-drop) */
    N v = N { s: "v2".upper(), n: 2 }
    m.set("k2".upper(), v)
    /* read values back */
    Str a_s = "missing"
    Str b_s = "missing"
    match m.get("k1".upper()) {
        Some(v1) => { a_s = v1.s }
        None => {}
    }
    match m.get("k2".upper()) {
        Some(v2) => { b_s = v2.s }
        None => {}
    }
    @print(f"a={a_s} b={b_s}")

    /* has_drop enum value, inline temp */
    Map(Str, E) e
    e.set("e".upper(), Holds("payload".upper()))
    Str es = "?"
    match e.get("e".upper()) {
        Some(got) => {
            match got { Empty => { es = "empty" } Holds(p) => { es = p } }
        }
        None => {}
    }
    @print(f"e={es}")

    if a_s.eq?("V1") && b_s.eq?("V2") && es.eq?("PAYLOAD") {
        @print("BF046 PASS")
    }
    return 0
}
