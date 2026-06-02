// Regression: transiently reading THROUGH a has_drop struct field must not leak.
//
// Root cause (fixed): an AST_FIELD read whose object is itself a struct field
// (e.g. the `o.inner` in `o.inner.items.length`) used to deep-clone the whole
// intermediate has_drop struct, then read one sub-field. That clone was an
// owned temporary that was never registered for statement-end drop, so its
// vec/map/string/nested heap leaked (and user __drop side effects re-fired).
//
// Fix: when the object of a field access is a chained struct field rooted in
// stable named storage, borrow its address via GEP (codegen_lvalue_ptr) and
// read the sub-field in place — no transient clone. A terminal binding
// (`Box b = o.inner`) still deep-clones (object is an IDENT) so `b` owns an
// independent copy.
//
// Convention: print "<label> PASS"/"<label> FAIL"; the cmake driver asserts the
// sample emits "READTHROUGH PASS" and never "FAIL", under JIT + AOT + memcheck.

struct Box   { vec(int) items }
struct Outer { Box inner }
struct A { vec(int) v }
struct B { A a }
struct C { B b }
struct Bag  { string name  map(string,int) m }
struct Wrap { Bag bag }

fn check(bool cond, string label) {
    if (cond) { print(f"{label} PASS") } else { print(f"{label} FAIL") }
}

fn main() {
    // --- transient read-through of a nested has_drop struct field (the leak) ---
    vec(int) iv = []
    iv.push(1) iv.push(2)
    Outer o = Outer { inner: Box { items: iv } }
    check(o.inner.items.length == 2, "transient")

    // --- consume path: `Box b = o.inner` deep-clones; b is independent of o ---
    Box b = o.inner
    b.items.push(99)
    check(b.items.length == 3, "consume_independent")
    check(o.inner.items.length == 2, "source_unchanged")   // o not affected

    // --- mutation through a chained field must persist to the source ---
    o.inner.items.push(7)
    check(o.inner.items.length == 3, "mutate_through")
    check(o.inner.items[2] == 7, "mutate_value")

    // --- 3-level chain read-through + mid-level consume ---
    vec(int) jv = []
    jv.push(5)
    C c = C { b: B { a: A { v: jv } } }
    check(c.b.a.v.length == 1, "chain3_transient")
    A mid = c.b.a
    check(mid.v.length == 1, "chain3_consume")

    // --- string + map fields read through a chain, then consume ---
    map(string,int) mm = {}
    mm.set("k", 5)
    Wrap w = Wrap { bag: Bag { name: "hello", m: mm } }
    check(w.bag.name.length == 5, "chain_string")
    check(w.bag.m.length == 1, "chain_map")
    Bag bag2 = w.bag
    check(bag2.name.length == 5 && bag2.m.length == 1, "chain_consume")

    print("READTHROUGH PASS")
}
