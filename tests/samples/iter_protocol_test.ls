// iter_protocol_test.ls — `for x in v` over the pure-LS Vec(T) via the
// Iterator(T) protocol (Vec.iter() -> VecIter(T); for-in desugars to
// while/match next()). See docs/plan_userdef_for_in.md.

import std.vec
import std.str

struct Person { Str name; int age }

fn check(bool c, Str l) { if c { print(f"ok {l}") } else { print(f"FAIL {l}") } }

// rvalue source: a fresh owned Vec that must outlive the loop (materialized).
fn make_ints() -> Vec(int) {
    Vec(int) v = [10, 20, 30]
    return v
}

fn main() {
    // ---- Vec(int): basic sum ----
    Vec(int) vi = [1, 2, 3, 4, 5]
    int sum = 0
    for x in vi { sum = sum + x }
    check(sum == 15, "Vec(int) sum")
    check(vi.len() == 5, "Vec(int) still usable after for-in (borrow)")

    // ---- break ----
    int seen = 0
    for x in vi {
        if x == 3 { break }
        seen = seen + 1
    }
    check(seen == 2, "break stops at 3rd")

    // ---- continue ----
    int odds = 0
    for x in vi {
        if x % 2 == 0 { continue }
        odds = odds + 1
    }
    check(odds == 3, "continue skips evens (1,3,5)")

    // ---- empty container: zero iterations ----
    Vec(int) empty = []
    int n = 0
    for x in empty { n = n + 1 }
    check(n == 0, "empty vec: 0 iterations")

    // ---- Vec(string): has_drop element clone-on-read ----
    Vec(Str) vs = [f"a", f"bb", f"ccc"]
    int total = 0
    for s in vs { total = total + s.len() }
    check(total == 6, "Vec(string) length sum")
    check(vs.len() == 3, "Vec(string) still usable after for-in")

    // ---- Vec(Person): has_drop struct, field read in body ----
    Vec(Person) ps = {}
    ps.push(Person{ name: f"Ann", age: 30 })
    ps.push(Person{ name: f"Bob", age: 25 })
    int agesum = 0
    for p in ps { agesum = agesum + p.age }
    check(agesum == 55, "Vec(Person) age sum")

    // ---- rvalue source: temporary vec iterated then dropped ----
    int rsum = 0
    for x in make_ints() { rsum = rsum + x }
    check(rsum == 60, "rvalue source sum")

    // ---- nested loops: two independent iterators over the same vec ----
    Vec(int) sm = [1, 2, 3]
    int pairs = 0
    for a in sm {
        for b in sm { pairs = pairs + 1 }
    }
    check(pairs == 9, "nested for-in: 3x3 = 9 pairs")

    print("ITER PASS")
}
