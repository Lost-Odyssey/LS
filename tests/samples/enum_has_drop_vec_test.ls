// enum_has_drop_vec_test.ls
// Phase E-1/E-2 TDD: vec(has_drop_enum) scope drop / clear / truncate /
//   extend / resize / copy / slice / push / pop / remove / first / last / get
// Verifies: 0 leaks, 0 double-frees with --memcheck

enum Data {
    Empty
    Text(string s)
    Items(vec(string) items)
}

fn main() {
    // ---- A: basic construction + scope drop ----
    vec(Data) v1 = [Text("a".copy()), Text("b".copy())]
    print("A: len =", v1.length)
    // v1 leaves scope → should drop each Data's string payload

    // ---- B: push + pop ----
    vec(Data) v2 = []
    v2.push(Text("hello".copy()))
    v2.push(Empty)
    v2.pop()
    print("B: len =", v2.length)
    // v2 leaves scope → drops remaining element

    // ---- C: clear ----
    vec(Data) v3 = [Text("c1".copy()), Text("c2".copy()), Text("c3".copy())]
    v3.clear()
    print("C: len =", v3.length)

    // ---- D: copy (deep clone) ----
    vec(Data) v4 = [Text("d1".copy()), Text("d2".copy())]
    vec(Data) v4_copy = v4.copy()
    print("D: copy len =", v4_copy.length)
    // v4 and v4_copy are independent → each drops cleanly

    // ---- E: extend (deep clone from source) ----
    vec(Data) v5 = [Text("e1".copy())]
    vec(Data) v5_src = [Text("e2".copy()), Text("e3".copy())]
    v5.extend(v5_src)
    print("E: extend len =", v5.length)

    // ---- F: truncate ----
    vec(Data) v6 = [Text("f1".copy()), Text("f2".copy()), Text("f3".copy())]
    v6.truncate(1)
    print("F: truncate len =", v6.length)

    // ---- G: remove ----
    vec(Data) v7 = [Text("g1".copy()), Text("g2".copy()), Text("g3".copy())]
    v7.remove(1)
    print("G: remove len =", v7.length)

    // ---- H: slice (deep clone) ----
    vec(Data) v8 = [Text("h1".copy()), Text("h2".copy()), Text("h3".copy())]
    vec(Data) v8_sl = v8.slice(0, 2)
    print("H: slice len =", v8_sl.length)

    // ---- I: resize (shrink drops, grow zero-fills) ----
    vec(Data) v9 = [Text("i1".copy()), Text("i2".copy()), Text("i3".copy())]
    v9.resize(1)
    print("I: resize len =", v9.length)

    // ---- J: first (deep-clone, source vec unaffected) ----
    vec(Data) v10 = [Text("j1".copy()), Text("j2".copy()), Text("j3".copy())]
    Data j_first = v10.first()
    print("J: first done, vec len =", v10.length)
    // j_first and v10[0] are independent copies

    // ---- K: last (deep-clone, source vec unaffected) ----
    vec(Data) v11 = [Text("k1".copy()), Text("k2".copy()), Text("k3".copy())]
    Data k_last = v11.last()
    print("K: last done, vec len =", v11.length)

    // ---- L: get(i) (deep-clone, source vec unaffected) ----
    vec(Data) v12 = [Text("l1".copy()), Text("l2".copy()), Text("l3".copy())]
    Data l_mid = v12.get(1)
    print("L: get done, vec len =", v12.length)
    // l_mid owns its copy; v12[1] still alive

    print("all done")
}
