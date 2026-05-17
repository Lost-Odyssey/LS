/* generics_g1_test.ls — Phase G1: user-defined generic structs */

/* ---- 1. Simple generic pair ---- */
struct Pair(T, U) {
    T first
    U second
}

fn make_pair_int_str(int a, string b) -> Pair(int, string) {
    return new Pair(int, string) { first: a, second: b }
}

fn test_pair() {
    Pair(int, string) p = new Pair(int, string) { first: 42, second: "hello" }
    print(p.first)
    print(p.second)

    Pair(int, int) q = new Pair(int, int) { first: 10, second: 20 }
    print(q.first)
    print(q.second)

    Pair(int, string) p2 = make_pair_int_str(99, "world")
    print(p2.first)
    print(p2.second)
}

/* ---- 2. Generic box (single type param) ---- */
struct Box(T) {
    T value
}

fn test_box() {
    Box(int) bi = new Box(int) { value: 7 }
    print(bi.value)

    Box(f64) bf = new Box(f64) { value: 3.14 }
    print(bf.value)

    Box(bool) bb = new Box(bool) { value: true }
    print(bb.value)
}

/* ---- 3. Generic struct with string field (has_drop) ---- */
struct Named(T) {
    string name
    T data
}

fn test_named() {
    Named(int) n = new Named(int) { name: "count", data: 100 }
    print(n.name)
    print(n.data)

    Named(f64) m = new Named(f64) { name: "ratio", data: 0.5 }
    print(m.name)
    print(m.data)
}

/* ---- 4. Three type params ---- */
struct Triple(A, B, C) {
    A x
    B y
    C z
}

fn test_triple() {
    Triple(int, string, bool) t = new Triple(int, string, bool) {
        x: 1,
        y: "two",
        z: true
    }
    print(t.x)
    print(t.y)
    print(t.z)
}

/* ---- 5. Generic struct in function arg / return ---- */
fn swap_pair(Pair(int, string) p) -> Pair(string, int) {
    return new Pair(string, int) { first: p.second, second: p.first }
}

fn test_swap() {
    Pair(int, string) p = new Pair(int, string) { first: 5, second: "five" }
    Pair(string, int) q = swap_pair(p)
    print(q.first)
    print(q.second)
}

fn main() {
    test_pair()
    test_box()
    test_named()
    test_triple()
    test_swap()
}
