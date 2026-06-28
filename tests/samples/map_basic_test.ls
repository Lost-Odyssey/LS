// map_basic_test.ls — std.core.map M-0: construct + set/get/has?/len + overwrite +
// grow/rehash for POD K/V (Map(int,int)). Robin Hood open addressing with
// Fibonacci scatter. has_drop K/V (M-2), remove (M-1), iter (M-3) come later.
// See docs/plan_std_map.md. JIT + AOT + memcheck 0/0/0.

import std.core.map

def check(bool c, Str l) { if c { @print(f"ok {l}") } else { @print(f"FAIL {l}") } }

// helper: Option(int) -> int with sentinel for miss
def gv(&Map(int, int) m, int k) -> int {
    match m.get(k) {
        Some(v) => { return v }
        None => { return -1 }
    }
}

def main() {
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

    // ---- remove (backward-shift) ----
    Map(int, int) rm = {}
    rm.set(10, 1)
    rm.set(20, 2)
    rm.set(30, 3)
    match rm.remove(20) {
        Some(v) => { check(v == 2, "remove returns the value") }
        None => { check(false, "remove returns the value") }
    }
    check(rm.len() == 2, "len drops after remove")
    check(!rm.has?(20), "removed key is gone")
    check(gv(rm, 10) == 1, "neighbor 10 still readable after remove")
    check(gv(rm, 30) == 3, "neighbor 30 still readable after remove")
    match rm.remove(999) {
        Some(v2) => { check(false, "remove missing returns None") }
        None => { check(true, "remove missing returns None") }
    }
    check(rm.len() == 2, "len unchanged on missing remove")
    rm.set(20, 22)
    check(gv(rm, 20) == 22, "re-insert after remove")
    check(rm.len() == 3, "len back to 3 after re-insert")

    // ---- backward-shift stress: remove all evens, odds must survive ----
    Map(int, int) s = {}
    int a = 0
    while a < 300 {
        s.set(a, a + 1000)
        a = a + 1
    }
    int e = 0
    while e < 300 {
        s.remove(e)
        e = e + 2
    }
    check(s.len() == 150, "150 entries remain after removing 150 evens")
    bool shift_ok = true
    int t = 0
    while t < 300 {
        if t % 2 == 0 {
            if s.has?(t) { shift_ok = false }            // evens gone
        } else {
            if gv(s, t) != t + 1000 { shift_ok = false } // odds intact
        }
        t = t + 1
    }
    check(shift_ok, "backward-shift preserves all surviving keys")

    // ---- clear ----
    s.clear()
    check(s.len() == 0, "len 0 after clear")
    check(s.empty?(), "empty? true after clear")
    check(!s.has?(101), "no keys after clear")
    s.set(7, 77)
    check(gv(s, 7) == 77, "usable again after clear")
    check(s.len() == 1, "len 1 after clear + insert")

    @print("MAP PASS")
}
