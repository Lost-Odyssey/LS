// implicit_empty_init_test.ls — M-DEF: `T v` ≡ `T v = {}` for any type whose
// `= {}` init is already legal (user containers Vec/Map, struct zero-init, the
// built-in map). POD `int x` keeps its existing no-init behavior (garbage until
// assigned). See docs/plan_std_map.md §F2 / M-DEF. JIT + AOT + memcheck 0/0/0.

import std.vec
import std.map

struct Point { int x; int y }

fn check(bool c, string l) { if c { print(f"ok {l}") } else { print(f"FAIL {l}") } }

fn get_int(&Map(string, int) m, string k) -> int {
    match m.get(k) {
        Some(v) => { return v }
        None => { return -1 }
    }
}

// no-init Vec returned from a function (has_drop, scope-drop on caller side)
fn make() -> Vec(int) {
    Vec(int) out
    out.push(100)
    out.push(200)
    return out
}

fn main() {
    // ---- user container Vec(int), no init ≡ {} ----
    Vec(int) v
    v.push(1)
    v.push(2)
    v.push(3)
    check(v.len() == 3, "Vec(int) no-init then push")
    check(v.get(0) + v.get(1) + v.get(2) == 6, "Vec(int) values")

    // ---- built-in map, no init ≡ {} ----
    Map(string, int) m
    m.set("a", 10)
    m.set("b", 20)
    check(get_int(m, "a") == 10, "map a")
    check(get_int(m, "b") == 20, "map b")

    // ---- all-default-field struct zero-init ----
    Point p
    check(p.x == 0 && p.y == 0, "struct zero-init")

    // ---- has_drop element container (Vec(string)) ----
    Vec(string) names
    names.push(f"hello")
    string n0 = names.get(0)
    check(n0.length == 5, "Vec(string) no-init element")

    // ---- nested generic container, no init ----
    Vec(Vec(int)) vv
    Vec(int) inner
    inner.push(7)
    vv.push(inner)
    Vec(int) got = vv.get(0)
    check(got.get(0) == 7, "nested Vec no-init")

    // ---- no-init Vec from a function (scope-drop both sides) ----
    Vec(int) fromfn = make()
    check(fromfn.len() == 2 && fromfn.get(1) == 200, "no-init Vec across fn")

    // ---- POD int no-init is UNCHANGED: not zero-initialized, assignable ----
    int x
    x = 42
    check(x == 42, "POD int no-init still assignable")

    // ---- regression: adjacent expr stmts on the SAME line stay expressions ----
    print(f"adj1") print(f"adj2")

    print("MDEF PASS")
}
