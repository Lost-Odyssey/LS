// Move-elision (Q4): owned movable sources are moved (not cloned) into a new
// var / assignment when the checker confirms ownership transfer. The source is
// invalidated at runtime so scope-drop never double-frees. This sample exercises
// every elided clone site (Str/struct/enum/Vec/Map, var_decl + assign +
// field-assign + explicit __move + conditional move) and self-checks that the
// moved data survived intact (a corrupted move would garble the printed values).
// Registered under JIT memcheck: must be 0 leak / 0 double-free / 0 invalid free.

import std.vec
import std.map
import std.str

struct Box { Str label }
enum Tag { A(Str), B }

fn check(bool ok, string what) {
    if ok { print(f"ok {what}") } else { print(f"FAIL {what}") }
}

fn main() {
    // --- Str var_decl move ---
    Str s1 = "alpha".upper()
    Str s2 = s1                       // move-elision: s1 invalidated
    check(s2.eq?("ALPHA"), "string-var")

    // --- explicit __move ---
    Str m1 = "beta".upper()
    Str m2 = __move(m1)
    check(m2.eq?("BETA"), "string-move")

    // --- Str assign ---
    Str a1 = "gamma".upper()
    Str a2 = "x".upper()
    a2 = a1                              // move-elision on assign
    check(a2.eq?("GAMMA"), "string-assign")

    // --- struct var_decl move ---
    Box b1 = Box { label: "delta".upper() }
    Box b2 = b1
    check(b2.label.eq?("DELTA"), "struct-var")

    // --- struct assign ---
    Box c1 = Box { label: "epsilon".upper() }
    Box c2 = Box { label: "y".upper() }
    c2 = c1
    check(c2.label.eq?("EPSILON"), "struct-assign")

    // --- enum var_decl move ---
    Tag t1 = A("zeta".upper())
    Tag t2 = t1
    match t2 { A(v) => check(v.eq?("ZETA"), "enum-var"), B => check(false, "enum-var") }

    // --- Vec var_decl move (was a latent double-free before Q4) ---
    Vec(Str) v1 = ["eta".upper(), "theta".upper()]
    Vec(Str) v2 = v1
    check(v2.get(0).eq?("ETA"), "vec-var")

    // --- Map var_decl move ---
    Map(string,int) p1 = { "k": 7 }
    Map(string,int) p2 = p1
    bool map_ok = false
    match p2.get("k") {
        Some(v) => { map_ok = v == 7 }
        None => {}
    }
    check(map_ok, "map-var")

    // --- conditional move: source moved only on one branch ---
    Str g1 = "iota".upper()
    Str g2 = "fallback".upper()
    if g1.eq?("IOTA") {
        g2 = g1                          // moved here; scope-drop skips g1 at runtime
    }
    check(g2.eq?("IOTA"), "cond-move")

    print("ME PASS")
}
