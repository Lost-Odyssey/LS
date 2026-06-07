// vec_owndrop_test.ls — Vec(T) element ownership / clone / drop correctness.
// Covers plan_vec_ownership_drop.md §008 (index-read-through of has_drop struct)
// and §009 (rvalue string moved into the container via push/insert/set).
//
// §008: v[i].field / v[i].inner.field / f(v[i]) — the intermediate has_drop
//       struct rvalue (returned by Vec.__index, a normal method call) and any
//       owned-clone produced by a chained field read must be temp-dropped.
// §009: push/insert/set of an owned rvalue string moves the buffer into the
//       container (no leak, no double-free); named-var args still clone.
//
// memcheck must be 0/0/0. Prints "ok <l>" / "FAIL <l>" then "OWNDROP PASS".

import std.vec

struct Inner { string tag }
struct Person { int age; string name; Inner inner }

fn check(bool c, string l) { if c { print(f"ok {l}") } else { print(f"FAIL {l}") } }

fn name_age(Person p) -> int { return p.age }

fn main() {
    // ───────────── §008: index-read-through of has_drop struct ─────────────
    Vec(Person) v = {}
    v.push(Person{age: 10, name: "amy".upper(), inner: Inner{tag: "x".upper()}})
    v.push(Person{age: 20, name: "bob".upper(), inner: Inner{tag: "y".upper()}})

    // v[i].field — intermediate Person rvalue consumed by field access.
    check(v[0].name == "AMY", "v[0].name")
    check(v[1].name == "BOB", "v[1].name")
    check(v[0].age == 10, "v[0].age")

    // v[i].inner.field — nested chained read; the `v[i].inner` clone must drop.
    check(v[0].inner.tag == "X", "v[0].inner.tag")
    check(v[1].inner.tag == "Y", "v[1].inner.tag")

    // f(v[i]) — owned rvalue struct passed by value.
    check(name_age(v[0]) == 10, "f(v[0])")

    // Terminal bind still moves (no temp leak) and stays independent.
    Person p = v[0]
    check(p.name == "AMY" && p.age == 10, "terminal bind")

    // ───────────── §009: rvalue string moved into the container ─────────────
    Vec(string) w = {}
    w.push("hello".upper())              // owned rvalue → moved in
    w.push("world".upper())
    check(w[0] == "HELLO" && w[1] == "WORLD", "push rvalue")

    w.insert(1, "mid".upper())           // [HELLO, MID, WORLD]
    check(w[1] == "MID" && w.len() == 3, "insert rvalue")

    w.set(0, "first".upper())            // drop old, move new in
    check(w[0] == "FIRST", "set rvalue")

    w[2] = "last".upper()                // __index_set rvalue
    check(w[2] == "LAST", "index_set rvalue")

    // Named-var arg borrows → clones in (caller stays valid).
    string keep = "keep".upper()
    w.push(keep)
    check(w.len() == 4 && keep == "KEEP", "push named (clone)")

    match w.pop() { Some(s) => { check(s == "KEEP", "pop moved-out") } None => { check(false, "pop") } }

    print("OWNDROP PASS")
}
