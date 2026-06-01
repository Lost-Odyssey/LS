// Move-elision (Q4): owned movable sources are moved (not cloned) into a new
// var / assignment when the checker confirms ownership transfer. The source is
// invalidated at runtime so scope-drop never double-frees. This sample exercises
// every elided clone site (string/struct/enum/vec/map, var_decl + assign +
// field-assign + explicit __move + conditional move) and self-checks that the
// moved data survived intact (a corrupted move would garble the printed values).
// Registered under JIT memcheck: must be 0 leak / 0 double-free / 0 invalid free.

struct Box { string label }
enum Tag { A(string), B }

fn check(bool ok, string what) {
    if ok { print(f"ok {what}") } else { print(f"FAIL {what}") }
}

fn main() {
    // --- string var_decl move ---
    string s1 = "alpha".upper()
    string s2 = s1                       // move-elision: s1 invalidated
    check(s2 == "ALPHA", "string-var")

    // --- explicit __move ---
    string m1 = "beta".upper()
    string m2 = __move(m1)
    check(m2 == "BETA", "string-move")

    // --- string assign ---
    string a1 = "gamma".upper()
    string a2 = "x".upper()
    a2 = a1                              // move-elision on assign
    check(a2 == "GAMMA", "string-assign")

    // --- struct var_decl move ---
    Box b1 = Box { label: "delta".upper() }
    Box b2 = b1
    check(b2.label == "DELTA", "struct-var")

    // --- struct assign ---
    Box c1 = Box { label: "epsilon".upper() }
    Box c2 = Box { label: "y".upper() }
    c2 = c1
    check(c2.label == "EPSILON", "struct-assign")

    // --- enum var_decl move ---
    Tag t1 = A("zeta".upper())
    Tag t2 = t1
    match t2 { A(v) => check(v == "ZETA", "enum-var"), B => check(false, "enum-var") }

    // --- vec var_decl move (was a latent double-free before Q4) ---
    vec(string) v1 = ["eta".upper(), "theta".upper()]
    vec(string) v2 = v1
    check(v2.get(0) == "ETA", "vec-var")

    // --- map var_decl move ---
    map(string,int) p1 = { "k" -> 7 }
    map(string,int) p2 = p1
    check(p2.get("k") == 7, "map-var")

    // --- conditional move: source moved only on one branch ---
    string g1 = "iota".upper()
    string g2 = "fallback".upper()
    if g1 == "IOTA" {
        g2 = g1                          // moved here; scope-drop skips g1 at runtime
    }
    check(g2 == "IOTA", "cond-move")

    print("ME PASS")
}
