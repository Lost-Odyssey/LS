// vec Batch D end-to-end test: contains(), index_of(), resize(), copy()

fn main() -> int {
    // ===================== contains() =====================

    vec(int) v
    v.push(10)
    v.push(20)
    v.push(30)

    print(v.contains(20))   // true
    print(v.contains(99))   // false
    print(v.contains(10))   // true
    print(v.contains(30))   // true

    // contains on empty vec
    vec(int) empty
    print(empty.contains(1))  // false

    // contains on string vec
    vec(string) sv
    sv.push("alpha")
    sv.push("beta")
    sv.push("gamma")
    print(sv.contains("beta"))   // true
    print(sv.contains("delta"))  // false
    print(sv.contains("alpha"))  // true

    // ===================== index_of() =====================

    vec(int) idx_v
    idx_v.push(100)
    idx_v.push(200)
    idx_v.push(300)
    idx_v.push(200)  // duplicate

    print(idx_v.index_of(100))  // 0
    print(idx_v.index_of(200))  // 1  (first occurrence)
    print(idx_v.index_of(300))  // 2
    print(idx_v.index_of(999))  // -1 (not found)

    // index_of on empty vec
    vec(int) empty2
    print(empty2.index_of(1))   // -1

    // index_of on string vec
    vec(string) sidx
    sidx.push("x")
    sidx.push("y")
    sidx.push("z")
    print(sidx.index_of("y"))   // 1
    print(sidx.index_of("w"))   // -1

    // ===================== resize() =====================

    // grow: zero-fills new slots
    vec(int) r
    r.push(1)
    r.push(2)
    r.push(3)
    r.resize(6)
    print(r.length)   // 6
    print(r[0])       // 1
    print(r[1])       // 2
    print(r[2])       // 3
    print(r[3])       // 0
    print(r[4])       // 0
    print(r[5])       // 0

    // shrink: drops excess elements
    r.resize(2)
    print(r.length)   // 2
    print(r[0])       // 1
    print(r[1])       // 2

    // resize to same length — nop
    r.resize(2)
    print(r.length)   // 2

    // resize to 0
    r.resize(0)
    print(r.length)   // 0
    print(r.is_empty())  // true

    // resize with negative — clamps to 0
    vec(int) r2
    r2.push(1)
    r2.push(2)
    r2.resize(-3)
    print(r2.length)  // 0

    // resize string vec — new slots get empty strings
    vec(string) sr
    sr.push("hello")
    sr.resize(3)
    print(sr.length)  // 3
    print(sr[0])      // hello
    // sr[1] and sr[2] are empty strings (safe to print)

    // shrink string vec — drops freed strings
    sr.push("world")
    sr.resize(1)
    print(sr.length)  // 1
    print(sr[0])      // hello

    // ===================== copy() =====================

    vec(int) orig
    orig.push(10)
    orig.push(20)
    orig.push(30)

    vec(int) cp = orig.copy()
    print(cp.length)  // 3
    print(cp[0])      // 10
    print(cp[1])      // 20
    print(cp[2])      // 30

    // Mutations to copy don't affect original
    cp.push(40)
    print(orig.length) // 3  (unchanged)
    print(cp.length)   // 4

    // copy of empty vec
    vec(int) empty3
    vec(int) cp2 = empty3.copy()
    print(cp2.length)  // 0
    print(cp2.is_empty()) // true

    // copy of string vec — independent deep copies
    vec(string) sorig
    sorig.push("hello")
    sorig.push("world")
    vec(string) scp = sorig.copy()
    print(scp.length)  // 2
    print(scp[0])      // hello
    print(scp[1])      // world

    return 0
}
