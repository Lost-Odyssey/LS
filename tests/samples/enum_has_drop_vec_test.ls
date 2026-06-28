// enum_has_drop_vec_test.ls
// Phase E-1/E-2 TDD: Vec(has_drop_enum) scope drop / clear / truncate /
//   extend / resize / copy / slice / push / pop / remove / first / last / get
// Verifies: 0 leaks, 0 double-frees with --memcheck

import std.core.vec
import std.core.str

enum Data {
    Empty
    Text(Str s)
    Items(Vec(Str) items)
}

def owned(Str x) -> Str { return x.copy() }

def main() {
    // ---- A: basic construction + scope drop ----
    Vec(Data) v1 = [Text(owned("a")), Text(owned("b"))]
    @print("A: len =", v1.len())
    // v1 leaves scope -> should drop each Data's Str payload

    // ---- B: push + pop ----
    Vec(Data) v2 = {}
    v2.push(Text(owned("hello")))
    v2.push(Empty)
    v2.pop()
    @print("B: len =", v2.len())
    // v2 leaves scope -> drops remaining element

    // ---- C: clear ----
    Vec(Data) v3 = [Text(owned("c1")), Text(owned("c2")), Text(owned("c3"))]
    v3.clear()
    @print("C: len =", v3.len())

    // ---- D: copy (deep clone) ----
    Vec(Data) v4 = [Text(owned("d1")), Text(owned("d2"))]
    Vec(Data) v4_copy = v4.copy()
    @print("D: copy len =", v4_copy.len())
    // v4 and v4_copy are independent -> each drops cleanly

    // ---- E: extend (deep clone from source) ----
    Vec(Data) v5 = [Text(owned("e1"))]
    Vec(Data) v5_src = [Text(owned("e2")), Text(owned("e3"))]
    v5.extend(v5_src)
    @print("E: extend len =", v5.len())

    // ---- F: truncate ----
    Vec(Data) v6 = [Text(owned("f1")), Text(owned("f2")), Text(owned("f3"))]
    v6.truncate(1)
    @print("F: truncate len =", v6.len())

    // ---- G: remove ----
    Vec(Data) v7 = [Text(owned("g1")), Text(owned("g2")), Text(owned("g3"))]
    v7.remove(1)
    @print("G: remove len =", v7.len())

    // ---- H: slice (deep clone) ----
    Vec(Data) v8 = [Text(owned("h1")), Text(owned("h2")), Text(owned("h3"))]
    Vec(Data) v8_sl = v8.slice(0, 2)
    @print("H: slice len =", v8_sl.len())

    // ---- I: resize (shrink drops, grow zero-fills) ----
    Vec(Data) v9 = [Text(owned("i1")), Text(owned("i2")), Text(owned("i3"))]
    v9.resize(1, Empty)
    @print("I: resize len =", v9.len())

    // ---- J: first (deep-clone, source vec unaffected) ----
    Vec(Data) v10 = [Text(owned("j1")), Text(owned("j2")), Text(owned("j3"))]
    Data j_first = match v10.first() { Some(x) => { x } None => { Empty } }
    @print("J: first done, vec len =", v10.len())
    // j_first and v10[0] are independent copies

    // ---- K: last (deep-clone, source vec unaffected) ----
    Vec(Data) v11 = [Text(owned("k1")), Text(owned("k2")), Text(owned("k3"))]
    Data k_last = match v11.last() { Some(x) => { x } None => { Empty } }
    @print("K: last done, vec len =", v11.len())

    // ---- L: get(i) (deep-clone, source vec unaffected) ----
    Vec(Data) v12 = [Text(owned("l1")), Text(owned("l2")), Text(owned("l3"))]
    Data l_mid = v12.get!(1)
    @print("L: get done, vec len =", v12.len())
    // l_mid owns its copy; v12[1] still alive

    @print("all done")
}
