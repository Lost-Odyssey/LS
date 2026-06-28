// map_upsert_test.ls — Map.upsert (update-or-insert, single hash+probe).
// Covers: POD counter (word-freq), absent-then-present transitions, Str key
// (has_drop key: present path must drop the redundant passed key), Vec value
// (has_drop value: group-by, old value moved into update closure), equivalence
// with get-then-set. memcheck must be 0/0/0.
// Prints "ok <label>" / "FAIL <label>" then "MAP UPSERT PASS".

import std.core.map
import std.core.str
import std.core.vec

def check(bool c, Str l) {
    if c { @print(f"ok {l}") } else { @print(f"FAIL {l}") }
}

def main() -> int {
    // ---- POD counter: word frequency ----
    Map(Str, int) freq = {}
    Vec(Str) toks = ["a", "b", "a", "c", "a", "b", "a"]
    for t in toks { freq.upsert(t, 1, |v| v + 1) }
    Str ka = "a"
    Str kb = "b"
    Str kc = "c"
    Str kz = "z"
    check(freq.get(ka).unwrap_or(0) == 4, "a counted 4")
    check(freq.get(kb).unwrap_or(0) == 2, "b counted 2")
    check(freq.get(kc).unwrap_or(0) == 1, "c counted 1")
    check(freq.get(kz).is_none?(), "z absent")
    check(freq.len() == 3, "3 distinct keys")

    // ---- absent-insert then present-update transitions ----
    Map(int, int) m = {}
    m.upsert(5, 100, |v| v + 1)        // absent -> insert dflt 100
    check(m.get(5).unwrap_or(0) == 100, "absent inserts dflt")
    m.upsert(5, 100, |v| v + 1)        // present -> 101
    m.upsert(5, 100, |v| v * 2)        // present -> 202
    check(m.get(5).unwrap_or(0) == 202, "present applies update")
    check(m.len() == 1, "still one key")

    // ---- has_drop value: group-by (append index to a Vec bucket) ----
    Map(Str, Vec(int)) groups = {}
    Vec(Str) words = ["x", "y", "x", "x", "y"]
    for (int i = 0; i < words.len(); i = i + 1) {
        &Str w = words.get_ref(i)
        // absent -> new Vec with [i]; present -> push i onto existing
        Str wk = w.copy()
        Vec(int) seed = [i]
        groups.upsert(wk, seed, |bucket| {
            Vec(int) b = bucket
            b.push(i)
            return b
        })
    }
    Str gx = "x"
    Str gy = "y"
    int xlen = match groups.get(gx) { Some(b) => b.len() None => 0 }
    int ylen = match groups.get(gy) { Some(b) => b.len() None => 0 }
    check(xlen == 3, "group x has 3 members")
    check(ylen == 2, "group y has 2 members")

    // ---- equivalence: upsert vs manual get-then-set ----
    Map(int, int) up = {}
    Map(int, int) gs = {}
    for (int i = 0; i < 500; i = i + 1) {
        int k = i % 37
        up.upsert(k, 1, |v| v + 1)
        int cur = gs.get(k).unwrap_or(0)
        gs.set(k, cur + 1)
    }
    int diff = 0
    for (int k = 0; k < 37; k = k + 1) {
        if up.get(k).unwrap_or(0 - 1) != gs.get(k).unwrap_or(0 - 2) { diff = diff + 1 }
    }
    check(diff == 0, "upsert matches get-then-set over 500 ops")
    check(up.len() == gs.len(), "same key count")

    @print("MAP UPSERT PASS")
    return 0
}
