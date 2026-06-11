// map_owndrop_test.ls — std.map M-2: has_drop K/V ownership. Verifies set
// (move-in + overwrite drops old), get (clone), remove (move out + drop key),
// clear, grow/rehash (moves has_drop entries via __take), and auto-drop of Map
// as a struct field — all memcheck 0/0/0 for has_drop keys, Str values,
// container values (Vec), and nested maps. See docs/plan_std_map.md §8.

import std.map
import std.vec
import std.str

fn check(bool c, Str l) { if c { print(f"ok {l}") } else { print(f"FAIL {l}") } }

struct Holder { Map(Str, int) m; int n }

fn main() {
    // ---- has_drop KEYS: Map(Str, int) ----
    Map(Str, int) sk = {}
    int i = 0
    while i < 200 {                       // many inserts → several rehashes move Str keys
        Str ky = f"key{i}"
        sk.set(ky, i)
        i = i + 1
    }
    check(sk.len() == 200, "Str-key: 200 entries")
    int hit = 0
    int j = 0
    while j < 200 {
        Str q = f"key{j}"
        match sk.get(q) { Some(v) => { if v == j { hit = hit + 1 } } None => {} }
        j = j + 1
    }
    check(hit == 200, "Str-key: all readable")
    Str rk = f"key100"
    match sk.remove(rk) { Some(v) => { check(v == 100, "Str-key: remove value") } None => { check(false, "remove") } }
    check(sk.len() == 199, "Str-key: len after remove")
    Str ok2 = f"key50"
    sk.set(ok2, 5000)                     // overwrite existing key
    check(sk.len() == 199, "Str-key: overwrite keeps len")

    // ---- has_drop Str VALUES: Map(int, Str) ----
    Map(int, Str) iv = {}
    iv.set(1, "one")
    iv.set(2, "two")
    iv.set(1, f"ONE-{1}")                 // overwrite → old "one" dropped
    int l1 = 0
    match iv.get(1) { Some(s) => { l1 = s.len() } None => {} }
    check(l1 == 5, "int->Str: overwritten value")
    match iv.remove(2) { Some(s) => { check(s.len() == 3, "int->Str: remove returns value") } None => { check(false, "r2") } }

    // ---- both has_drop: Map(Str, Str) + grow ----
    Map(Str, Str) ss = {}
    int a = 0
    while a < 80 {
        Str skk = f"k{a}"
        Str svv = f"value-number-{a}"
        ss.set(skk, svv)
        a = a + 1
    }
    check(ss.len() == 80, "Str->Str: 80 entries")
    Str qk = f"k40"
    int vl = 0
    match ss.get(qk) { Some(s) => { vl = s.len() } None => {} }
    check(vl == 15, "Str->Str: value len")
    ss.clear()
    check(ss.empty?(), "Str->Str: empty after clear")

    // ---- container VALUES: Map(Str, Vec(int)) (idiomatic match-rvalue get) ----
    Map(Str, Vec(int)) mv = {}
    Vec(int) v1 = [1, 2, 3]
    mv.set("a", v1)
    Vec(int) v2 = [4, 5]
    mv.set("a", v2)                       // overwrite → old [1,2,3] dropped
    int sz = 0
    match mv.get("a") { Some(vv) => { sz = vv.len() } None => {} }
    check(sz == 2, "Str->Vec: overwritten container value")
    Vec(int) v3 = [9, 9, 9, 9]
    mv.set("b", v3)
    match mv.remove("b") { Some(vv) => { check(vv.len() == 4, "Str->Vec: remove returns container") } None => { check(false, "rv") } }

    // ---- nested maps: Map(Str, Map(int,int)) ----
    Map(Str, Map(int, int)) nm = {}
    Map(int, int) inner = {}
    inner.set(1, 10)
    inner.set(2, 20)
    nm.set("g", inner)
    int innersz = 0
    match nm.get("g") { Some(im) => { innersz = im.len() } None => {} }
    check(innersz == 2, "nested map value size")

    // ---- Map as a struct field → auto-drop on scope exit ----
    Holder h = {}
    h.m.set("x", 1)
    h.m.set("y", 2)
    h.n = 2
    check(h.m.len() == 2, "map-as-field usable")

    print("OWNDROP PASS")
}
