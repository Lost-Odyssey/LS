// vec Batch A end-to-end test: is_empty(), first(), last()

fn main() -> int {
    // --- is_empty() ---
    vec(int) v
    bool e1 = v.is_empty()
    print(e1)              // true

    v.push(10)
    v.push(20)
    v.push(30)

    bool e2 = v.is_empty()
    print(e2)              // false

    // --- first() ---
    int f = v.first()
    print(f)               // 10

    // --- last() ---
    int l = v.last()
    print(l)               // 30

    // --- first/last on single-element vec ---
    vec(int) single
    single.push(99)
    print(single.first())  // 99
    print(single.last())   // 99

    // --- first/last on empty vec (should warn, return 0) ---
    vec(int) empty
    int ef = empty.first()  // [warning] vec.first() called on empty vec
    int el = empty.last()   // [warning] vec.last() called on empty vec
    print(ef)               // 0
    print(el)               // 0

    // --- string vec: first() and last() return owned clones ---
    vec(string) sv
    sv.push("alpha")
    sv.push("beta")
    sv.push("gamma")

    bool se = sv.is_empty()
    print(se)               // false

    string sf = sv.first()
    print(sf)               // alpha

    string sl = sv.last()
    print(sl)               // gamma

    // Mutations on the original vec do not affect the clones
    sv.pop()
    print(sf)               // alpha  (clone, unaffected)
    print(sl)               // gamma  (clone, unaffected)

    // --- is_empty() after clear ---
    sv.clear()
    bool sc = sv.is_empty()
    print(sc)               // true

    return 0
}
