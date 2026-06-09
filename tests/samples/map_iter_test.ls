// map_iter_test.ls — std.map M-3: MapIter + keys/values/each + `for e in m`
// (Iterator protocol, yields Entry(K,V); no `for (k,v)` destructuring, per Q4).
// Covers POD, has_drop keys (string), and has_drop container values (Vec) — the
// last drives the owned-rvalue match fix in the for-in `next()` desugar.
// See docs/plan_std_map.md §7. JIT + AOT + memcheck 0/0/0.

import std.map
import std.vec

fn check(bool c, string l) { if c { print(f"ok {l}") } else { print(f"FAIL {l}") } }

fn main() {
    // ---- POD: for-in + keys/values/each ----
    Map(int, int) m = {}
    int i = 0
    while i < 10 {
        m.set(i, i * i)
        i = i + 1
    }
    int ksum = 0
    int vsum = 0
    int cnt = 0
    for e in m {
        ksum = ksum + e.key
        vsum = vsum + e.val
        cnt = cnt + 1
    }
    check(cnt == 10, "for-in visits every entry")
    check(ksum == 45, "for-in key sum")
    check(vsum == 285, "for-in val sum")

    Vec(int) ks = m.keys()
    Vec(int) vs = m.values()
    check(ks.len() == 10, "keys() len")
    check(vs.len() == 10, "values() len")
    int ksAll = 0
    int vsAll = 0
    int n = 0
    while n < 10 {
        ksAll = ksAll + ks.get(n)
        vsAll = vsAll + vs.get(n)
        n = n + 1
    }
    check(ksAll == 45, "keys() sum")
    check(vsAll == 285, "values() sum")

    // each() — observed via a by-ref builtin map (int capture is by-copy; user
    // Map/struct capture is by-move; the builtin map is by-ref → observable)
    map(int, int) acc = {}
    m.each(|k, v| { acc.set(k, v + 1) })
    int seen = 0
    int q = 0
    while q < 10 {
        if acc.contains_key(q) { seen = seen + 1 }
        q = q + 1
    }
    check(seen == 10, "each visited every entry")

    // empty map: zero iterations
    Map(int, int) em = {}
    int ez = 0
    for e in em { ez = ez + 1 }
    check(ez == 0, "for-in over empty map: 0 iterations")

    // ---- has_drop keys: Map(string,int) ----
    Map(string, int) sm = {}
    sm.set("a", 1)
    sm.set("bb", 2)
    sm.set("ccc", 3)
    int klen = 0
    int sv = 0
    for e in sm {
        klen = klen + e.key.length
        sv = sv + e.val
    }
    check(klen == 6, "string-key for-in: key length sum")
    check(sv == 6, "string-key for-in: val sum")
    Vec(string) sks = sm.keys()
    check(sks.len() == 3, "string keys() len")

    // ---- has_drop container values: Map(string,Vec(int)) for-in ----
    Map(string, Vec(int)) mv = {}
    Vec(int) a = [1, 2, 3]
    mv.set("a", a)
    Vec(int) b = [4, 5]
    mv.set("b", b)
    int total = 0
    for e in mv {
        total = total + e.val.len()
    }
    check(total == 5, "container-value for-in: summed lengths")

    print("ITER PASS")
}
