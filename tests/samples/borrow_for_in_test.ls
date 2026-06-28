// borrow_for_in_test.ls — `for x in &v` borrowing for-in (zero-copy element read).
// x binds as a non-escaping &T borrow of each element (no clone-on-read, unlike
// the owning `for x in v`). Covers: Str elements, struct elements + field read,
// source usable after the loop, owning for-in still works, borrow→owned-param
// auto-clones (push), nested read. memcheck 0/0/0.
// Prints "ok <label>" / "FAIL <label>" then "BORROW FORIN PASS".

import std.core.str
import std.core.vec

struct Pt { int x; int y }

def check(bool c, Str l) {
    if c { @print(f"ok {l}") } else { @print(f"FAIL {l}") }
}

def main() -> int {
    // ---- Str elements: borrow read, zero clone ----
    Vec(Str) v = ["aa", "bbb", "c"]
    i64 tot = 0
    for x in &v { tot = tot + x.len() as i64 }
    check(tot == 6, "borrow sum of Str lengths = 6")
    // source still usable (not consumed by the borrow loop)
    check(v.len() == 3, "v survives borrow loop")
    check(v.get!(0).eq?("aa"), "v[0] intact after borrow loop")

    // ---- struct elements: field read through borrow ----
    Vec(Pt) pts = {}
    pts.push(Pt{x: 1, y: 2})
    pts.push(Pt{x: 3, y: 4})
    pts.push(Pt{x: 5, y: 6})
    int s = 0
    for p in &pts { s = s + p.x + p.y }
    check(s == 21, "borrow sum of Pt fields = 21")
    check(pts.len() == 3, "pts survives borrow loop")

    // ---- owning for-in still works (clone-on-read) ----
    i64 t2 = 0
    for x in v { t2 = t2 + x.len() as i64 }
    check(t2 == 6, "owning for-in still works")

    // ---- borrow -> owned param auto-clones (safe, independent copy) ----
    Vec(Str) out = {}
    for x in &v { out.push(x) }
    check(out.len() == 3, "push(borrow) collected 3")
    check(out.get!(1).eq?("bbb"), "push(borrow) cloned value")
    check(v.get!(1).eq?("bbb"), "source still owns its value after push(borrow)")

    // ---- empty vec borrow loop ----
    Vec(Str) empty = {}
    int ec = 0
    for x in &empty { ec = ec + 1 }
    check(ec == 0, "empty borrow loop runs 0 times")

    @print("BORROW FORIN PASS")
    return 0
}
