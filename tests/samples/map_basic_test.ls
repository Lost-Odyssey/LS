// map_basic_test.ls — std.map M-0: construct + set/get/has?/len + overwrite +
// grow/rehash for POD K/V (Map(int,int)). Robin Hood open addressing with
// Fibonacci scatter. has_drop K/V (M-2), remove (M-1), iter (M-3) come later.
// See docs/plan_std_map.md. JIT + AOT + memcheck 0/0/0.

import std.map

fn check(bool c, string l) { if c { print(f"ok {l}") } else { print(f"FAIL {l}") } }

// helper: Option(int) -> int with sentinel for miss
fn gv(&Map(int, int) m, int k) -> int {
    match m.get(k) {
        Some(v) => { return v }
        None => { return -1 }
    }
}

fn main() {
    // ---- construct ----
    Map(int, int) m = {}
    check(m.empty?(), "empty on construct")
    check(m.len() == 0, "len 0")
    check(!m.has?(7), "has? on empty is false")
    check(gv(m, 7) == -1, "get on empty is None")

    // ---- basic insert / lookup ----
    m.set(1, 100)
    m.set(2, 200)
    m.set(3, 300)
    check(m.len() == 3, "len 3 after inserts")
    check(!m.empty?(), "not empty")
    check(gv(m, 1) == 100, "get 1")
    check(gv(m, 2) == 200, "get 2")
    check(gv(m, 3) == 300, "get 3")
    check(gv(m, 99) == -1, "miss 99")
    check(m.has?(2), "has 2")
    check(!m.has?(99), "no 99")

    // ---- overwrite existing key (len unchanged, value replaced) ----
    m.set(2, 222)
    check(m.len() == 3, "len still 3 after overwrite")
    check(gv(m, 2) == 222, "overwrite value")
    m.set(2, 200)
    check(gv(m, 2) == 200, "overwrite back")

    // ---- negative keys ----
    m.set(-5, 55)
    check(gv(m, -5) == 55, "negative key")

    // ---- grow: many inserts trigger several rehashes, all readable ----
    Map(int, int) big = {}
    int i = 0
    while i < 500 {
        big.set(i, i * 7)
        i = i + 1
    }
    check(big.len() == 500, "500 entries")
    bool all_ok = true
    int j = 0
    while j < 500 {
        if gv(big, j) != j * 7 { all_ok = false }
        j = j + 1
    }
    check(all_ok, "all 500 readable after grows")
    check(gv(big, 500) == -1, "miss just past range")
    check(gv(big, -1) == -1, "miss negative")
    check(big.cap() >= 512, "cap grew to hold 500 at <=7/8 load")

    // ---- overwrite-heavy: re-set every key, len stays 500 ----
    int r = 0
    while r < 500 {
        big.set(r, r * 7 + 1)
        r = r + 1
    }
    check(big.len() == 500, "len stable after re-setting all keys")
    check(gv(big, 250) == 250 * 7 + 1, "values updated in place")

    print("MAP PASS")
}
