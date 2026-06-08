// Phase E.2 + E.4 closure parameter / array-capture correctness.
//
// E.2: Vec(T) closure parameters follow struct ABI: by-value moves the
//      Vec into the closure. Callers must clone to keep the original alive.
//      map(K,V) closure params remain borrowed (builtin map).
//
// E.4: array(POD, N) captured by value (full copy into env).
//      The outer array remains live; no heap allocation / drop needed.

import std.vec

type Reducer    = Block(Vec(int)) -> int
type MapQuery   = Block(map(string, int)) -> int
type ArrGetter  = Block(int) -> int
type ArrSummer  = Block() -> int

// ── helper functions ─────────────────────────────────────────────────────────

fn sum_vec(Vec(int) v) -> int {
    int s = 0
    int i = 0
    while i < v.len() { s = s + v[i]; i = i + 1 }
    return s
}

fn map_size(map(string, int) m) -> int {
    return m.length
}

// apply_reducer clones the input Vec so the caller retains ownership.
fn apply_reducer(&Vec(int) data, Reducer r) -> int {
    Vec(int) copy = data.copy()
    return r(copy)
}

fn apply_query(map(string, int) m, MapQuery q) -> int {
    return q(m)
}

// ── E.2.1: Vec(int) closure parameter — by-value, clone for multiple calls ──
fn test_e2_vec() {
    Vec(int) nums = [1, 2, 3, 4, 5]
    Reducer r = |v| { return sum_vec(v) }
    int result = apply_reducer(nums, r)
    print(result)           // 15
    // apply_reducer clones before passing to r, so nums stays alive
    int result2 = apply_reducer(nums, r)
    print(result2)          // 15
}

// ── E.2.2: map(string,int) closure parameter — borrowed ─────────────────────
fn test_e2_map() {
    map(string, int) scores = {}
    scores["a"] = 10
    scores["b"] = 20
    MapQuery q = |m| { return map_size(m) }
    int n1 = apply_query(scores, q)
    print(n1)               // 2
    scores["c"] = 30
    int n2 = apply_query(scores, q)
    print(n2)               // 3
}

// ── E.4.1: array(int, N) captured by value — independent copy ────────────────
fn test_e4_basic() {
    array(int, 5) arr = [10, 20, 30, 40, 50]
    ArrGetter get = |i| { return arr[i] }
    print(get(0))           // 10
    print(get(2))           // 30
    print(get(4))           // 50
}

// ── E.4.2: captured array is a snapshot — outer mutation invisible ────────────
fn test_e4_snapshot() {
    array(int, 3) src = [1, 2, 3]
    ArrSummer summer = || {
        int s = 0
        int i = 0
        while i < 3 { s = s + src[i]; i = i + 1 }
        return s
    }
    print(summer())         // 6  (snapshot of [1,2,3])
}

// ── E.4.3: multiple closures capture same array independently ─────────────────
fn test_e4_multi() {
    array(int, 4) data = [5, 10, 15, 20]
    ArrGetter first = |i| { return data[0] }
    ArrGetter last  = |i| { return data[3] }
    print(first(0))         // 5
    print(last(0))          // 20
}

fn main() {
    test_e2_vec()
    test_e2_map()
    test_e4_basic()
    test_e4_snapshot()
    test_e4_multi()
}
