// container_methods.ls — Vec / Map / Set method-completion batch (A/B/C/D).
// Prints "ok <label>" / "FAIL <label>", then "CM PASS".

import std.core.vec
import std.core.map
import std.core.set
import std.core.str
import std.core.num

def check(bool cond, Str label) {
    if cond { @print(f"ok {label}") } else { @print(f"FAIL {label}") }
}

def main() {
    // ===== A: Vec in-place =====
    Vec(int) a = [1, 2, 3, 4, 5, 6]
    a.retain(|x| x % 2 == 0)
    check(a.len() == 3 && a[0] == 2 && a[2] == 6, "retain evens -> {2,4,6}")

    Vec(int) d = [1, 1, 2, 2, 2, 3, 1]
    d.dedup()
    check(d.len() == 4, "dedup consecutive -> len 4")
    check(d[0] == 1 && d[1] == 2 && d[2] == 3 && d[3] == 1, "dedup {1,2,3,1}")

    Vec(int) sw = [10, 20, 30, 40]
    int got = sw.swap_remove(1)
    check(got == 20, "swap_remove returns 20")
    check(sw.len() == 3 && sw[1] == 40, "swap_remove moved last into slot")

    // has_drop element retain (memcheck probe)
    Vec(Str) ws = ["alpha", "beta", "gamma", "delta"]
    ws.retain(|s| s.len() == 5)        // alpha, gamma, delta
    check(ws.len() == 3, "retain Str by len -> 3")

    // ===== B: Vec reductions =====
    Vec(int) n = [5, 3, 8, 1, 9, 2]
    check(n.min().unwrap() == 1, "min = 1")
    check(n.max().unwrap() == 9, "max = 9")
    check(n.sum() == 28, "sum = 28")

    Vec(int) p = [1, 2, 3, 4]
    check(p.product() == 24, "product = 24")

    Vec(f64) f = [1.5, 2.5, 4.0]
    check(f.sum() == 8.0, "f64 sum = 8.0")

    Vec(int) empty = {}
    check(empty.sum() == 0, "empty sum = 0 (T.zero seed)")
    check(empty.product() == 1, "empty product = 1 (T.one seed)")
    match empty.min() { Some(_) => { check(false, "empty min None") } None => { check(true, "empty min None") } }

    check(n.is_sorted() == false, "unsorted is_sorted false")
    n.sort()
    check(n.is_sorted(), "after sort is_sorted true")

    // ===== C: Map =====
    Map(Str, int) freq = {}
    freq.set("x", 3)
    check(freq.get_or("x", 0) == 3, "get_or present = 3")
    check(freq.get_or("missing", 0) == 0, "get_or absent = dflt 0")
    check(freq.len() == 1, "get_or did not insert")

    Map(Str, int) m1 = {}
    m1.set("a", 1)
    m1.set("b", 2)
    Map(Str, int) m2 = {}
    m2.set("b", 20)
    m2.set("c", 3)
    m1.merge(&m2)
    check(m1.len() == 3, "merge -> 3 keys")
    check(m1.get_or("b", 0) == 20, "merge overwrites b -> 20")
    check(m1.get_or("c", 0) == 3, "merge adds c -> 3")

    // ===== D: Set retain =====
    Set(int) s = [1, 2, 3, 4, 5, 6]
    s.retain(|x| x > 3)
    check(s.len() == 3, "set retain >3 -> 3")
    check(s.has?(4) && s.has?(6) && s.has?(1) == false, "set retain kept {4,5,6}")

    @print("CM PASS")
}
