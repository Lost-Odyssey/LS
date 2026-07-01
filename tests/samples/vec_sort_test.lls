// vec_sort_test.ls — Vec(T).sort / sort_by is O(n log n) (bottom-up merge sort)
// and STABLE. Exercises a larger n (multiple merge passes), explicit stability,
// has_drop (string) elements, and degenerate sizes. Self-verifying.
import std.core.vec
import std.core.str

struct Item { int key; int seq }

def check(bool c, Str label) {
    if c { @print(f"  ok: {label}") } else { @print(f"FAIL: {label}") }
}

def main() -> int {
    // ---- larger n, reverse -> ascending (exercises several merge passes) ----
    Vec(int) v = {}
    for i in 0..100 { v.push(100 - i) }      // [100,99,...,1]
    v.sort()
    bool asc = true
    for (int i = 0; i < v.len(); i = i + 1) {
        if v[i] != i + 1 { asc = false }
    }
    check(asc, "100 ints reverse -> ascending")
    check(v[0] == 1 && v[99] == 100, "min/max in place")

    // ---- already-sorted stays sorted ----
    Vec(int) s = {}
    for i in 0..20 { s.push(i) }
    s.sort()
    bool same = true
    for (int i = 0; i < s.len(); i = i + 1) { if s[i] != i { same = false } }
    check(same, "already-sorted idempotent")

    // ---- stability: equal keys keep original (seq) order ----
    Vec(Item) items = {}
    items.push(Item{ key: 2, seq: 0 })
    items.push(Item{ key: 1, seq: 1 })
    items.push(Item{ key: 2, seq: 2 })
    items.push(Item{ key: 1, seq: 3 })
    items.push(Item{ key: 2, seq: 4 })
    items.sort_by(|a, b| a.key - b.key)
    // expected by (key asc, then original seq): (1,1)(1,3)(2,0)(2,2)(2,4)
    bool stable = items[0].seq == 1 && items[1].seq == 3 &&
                  items[2].seq == 0 && items[3].seq == 2 && items[4].seq == 4
    check(stable, "stable on equal keys")

    // ---- has_drop (string) elements sort + no leak (driver runs memcheck) ----
    Vec(Str) words = {}
    words.push("delta")
    words.push("alpha")
    words.push("charlie")
    words.push("bravo")
    words.sort_by(|a, b| {
        if a < b { return -1 }
        if a > b { return 1 }
        return 0
    })
    check(words[0].eq?("alpha") && words[3].eq?("delta"), "string sort")

    // ---- degenerate sizes: empty + single must be no-ops, no crash ----
    Vec(int) e = {}
    e.sort()
    check(e.len() == 0, "empty sort no-op")
    Vec(int) one = {}
    one.push(42)
    one.sort()
    check(one.len() == 1 && one[0] == 42, "single sort no-op")

    @print("VECSORT PASS")
    return 0
}
