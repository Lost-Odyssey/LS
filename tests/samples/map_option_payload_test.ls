// map_option_payload_test.ls — regression for B-MAP-OPT-001: an OWNED rvalue
// `Option(has_drop)` match subject must be dropped exactly once even when the
// arm contains nested control flow (for/while). Previously double-freed because
// an arm-internal temp-drop flush AND the merge-block drop both ran; fixed by
// making emit_enum_drop idempotent (zero the slot after dropping).
// Reproduces only inside a function (not inlined in main). JIT + AOT + memcheck.

import std.map
import std.vec
import std.str

fn check(bool c, Str l) { if c { print(f"ok {l}") } else { print(f"FAIL {l}") } }

// OPT-001: Option(Map) binder + for-in over it.
fn nested_for_in() -> int {
    Map(Str, Map(int, int)) outer = {}
    Map(int, int) inner = {}
    inner.set(1, 10)
    inner.set(2, 20)
    outer.set("g", inner)
    int sum = 0
    match outer.get("g") {
        Some(m) => { for e in m { sum = sum + e.val } }
        None => {}
    }
    return sum
}

// OPT-001 sibling: Option(Map) binder + method call in the arm.
fn nested_get() -> int {
    Map(Str, Map(int, int)) outer = {}
    Map(int, int) inner = {}
    inner.set(7, 70)
    outer.set("g", inner)
    int got = -1
    match outer.get("g") {
        Some(m) => { match m.get(7) { Some(v) => { got = v } None => {} } }
        None => {}
    }
    return got
}

// M5-004 shape: recursive has_drop enum with a Map payload, consumed recursively.
enum JV { Nil, Num(int), Obj(Map(Str, JV)) }
fn jv_total(&JV v) -> int {
    match v {
        Nil => { return 0 }
        Num(n) => { return n }
        Obj(m) => {
            int t = 0
            Vec(Str) ks = m.keys()
            for k in ks {
                match m.get(k) { Some(val) => { t = t + jv_total(val) } None => {} }
            }
            return t
        }
    }
}

fn main() {
    check(nested_for_in() == 30, "Option(Map) binder + for-in")
    check(nested_get() == 70, "Option(Map) binder + get")

    Map(Str, JV) o = {}
    o.set("a", Num(5))
    o.set("b", Num(7))
    JV root = Obj(o)
    check(jv_total(root) == 12, "recursive Map-in-enum consume")

    // B-MAP-M5-004 regression: the value retrieved from the Map is itself an
    // Obj(Map) — i.e. m.get(k) returns Option(JV) whose payload owns a container.
    // Before the has_drop fixpoint, Option(JV) was wrongly non-has_drop (JV is
    // has_drop only through its recursive Map payload), so no __drop was emitted
    // and the inner Map's buffers leaked.
    Map(Str, JV) di = {}
    di.set("x", Num(3))
    di.set("y", Num(4))
    Map(Str, JV) doo = {}
    doo.set("g", Obj(di))
    JV deep = Obj(doo)
    check(jv_total(deep) == 7, "container-payload value via get (M5-004)")

    print("OPTPAY PASS")
}
