// enum_has_drop_vec_test.ls
// Phase E-1/E-2 TDD: Vec(has_drop_enum) scope drop / clear / truncate /
//   extend / resize / copy / slice / push / pop / remove / first / last / get
// Verifies: 0 leaks, 0 double-frees with --memcheck

import std.vec

enum Data {
    Empty
    Text(string s)
    Items(Vec(string) items)
}

fn main() {
    // ---- A: basic construction + scope drop ----
    Vec(Data) v1 = [Text("a".copy()), Text("b".copy())]
    print("A: len =", v1.len())
    // v1 leaves scope → should drop each Data's string payload

    // ---- B: push + pop ----
    Vec(Data) v2 = {}
    v2.push(Text("hello".copy()))
    v2.push(Empty)
    v2.pop()
    print("B: len =", v2.len())
    // v2 leaves scope → drops remaining element

    // ---- C: clear ----
    Vec(Data) v3 = [Text("c1".copy()), Text("c2".copy()), Text("c3".copy())]
    v3.clear()
    print("C: len =", v3.len())

    // ---- D: copy (deep clone) ----
    Vec(Data) v4 = [Text("d1".copy()), Text("d2".copy())]
    Vec(Data) v4_copy = v4.copy()
    print("D: copy len =", v4_copy.len())
    // v4 and v4_copy are independent → each drops cleanly

    // ---- E: extend (deep clone from source) ----
    Vec(Data) v5 = [Text("e1".copy())]
    Vec(Data) v5_src = [Text("e2".copy()), Text("e3".copy())]
    v5.extend(v5_src)
    print("E: extend len =", v5.len())

    // ---- F: truncate ----
    Vec(Data) v6 = [Text("f1".copy()), Text("f2".copy()), Text("f3".copy())]
    v6.truncate(1)
    print("F: truncate len =", v6.len())

    // ---- G: remove ----
    Vec(Data) v7 = [Text("g1".copy()), Text("g2".copy()), Text("g3".copy())]
    v7.remove(1)
    print("G: remove len =", v7.len())

    // ---- H: slice (deep clone) ----
    Vec(Data) v8 = [Text("h1".copy()), Text("h2".copy()), Text("h3".copy())]
    Vec(Data) v8_sl = v8.slice(0, 2)
    print("H: slice len =", v8_sl.len())

    // ---- I: resize (shrink drops, grow zero-fills) ----
    Vec(Data) v9 = [Text("i1".copy()), Text("i2".copy()), Text("i3".copy())]
    v9.resize(1, Empty)
    print("I: resize len =", v9.len())

    // ---- J: first (deep-clone, source vec unaffected) ----
    Vec(Data) v10 = [Text("j1".copy()), Text("j2".copy()), Text("j3".copy())]
    Data j_first = match v10.first() { Some(x) => { x } None => { Empty } }
    print("J: first done, vec len =", v10.len())
    // j_first and v10[0] are independent copies

    // ---- K: last (deep-clone, source vec unaffected) ----
    Vec(Data) v11 = [Text("k1".copy()), Text("k2".copy()), Text("k3".copy())]
    Data k_last = match v11.last() { Some(x) => { x } None => { Empty } }
    print("K: last done, vec len =", v11.len())

    // ---- L: get(i) (deep-clone, source vec unaffected) ----
    Vec(Data) v12 = [Text("l1".copy()), Text("l2".copy()), Text("l3".copy())]
    Data l_mid = v12.get(1)
    print("L: get done, vec len =", v12.len())
    // l_mid owns its copy; v12[1] still alive

    print("all done")
}
