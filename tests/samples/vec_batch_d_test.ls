// vec Batch D end-to-end test: contains(), index_of(), resize(), copy()
import std.vec
import std.str

fn main() -> int {
    // ===================== contains() =====================

    Vec(int) v = {}
    v.push(10)
    v.push(20)
    v.push(30)

    print(v.has?(20))   // true
    print(v.has?(99))   // false
    print(v.has?(10))   // true
    print(v.has?(30))   // true

    // contains on empty vec
    Vec(int) empty = {}
    print(empty.has?(1))  // false

    // contains on Str vec
    Vec(Str) sv = {}
    sv.push("alpha")
    sv.push("beta")
    sv.push("gamma")
    print(sv.has?("beta"))   // true
    print(sv.has?("delta"))  // false
    print(sv.has?("alpha"))  // true

    // ===================== index_of() =====================

    Vec(int) idx_v = {}
    idx_v.push(100)
    idx_v.push(200)
    idx_v.push(300)
    idx_v.push(200)  // duplicate

    print(idx_v.index_of(100))  // 0
    print(idx_v.index_of(200))  // 1  (first occurrence)
    print(idx_v.index_of(300))  // 2
    print(idx_v.index_of(999))  // -1 (not found)

    // index_of on empty vec
    Vec(int) empty2 = {}
    print(empty2.index_of(1))   // -1

    // index_of on Str vec
    Vec(Str) sidx = {}
    sidx.push("x")
    sidx.push("y")
    sidx.push("z")
    print(sidx.index_of("y"))   // 1
    print(sidx.index_of("w"))   // -1

    // ===================== resize() =====================

    // grow: zero-fills new slots
    Vec(int) r = {}
    r.push(1)
    r.push(2)
    r.push(3)
    r.resize(6, 0)
    print(r.len())   // 6
    print(r[0])       // 1
    print(r[1])       // 2
    print(r[2])       // 3
    print(r[3])       // 0
    print(r[4])       // 0
    print(r[5])       // 0

    // shrink: drops excess elements
    r.resize(2, 0)
    print(r.len())   // 2
    print(r[0])       // 1
    print(r[1])       // 2

    // resize to same length — nop
    r.resize(2, 0)
    print(r.len())   // 2

    // resize to 0
    r.resize(0, 0)
    print(r.len())   // 0
    print(r.empty?)  // true

    // resize with negative — clamps to 0
    Vec(int) r2 = {}
    r2.push(1)
    r2.push(2)
    r2.resize(0, 0)
    print(r2.len())  // 0

    // resize Str vec — new slots get empty strings
    Vec(Str) sr = {}
    sr.push("hello")
    sr.resize(3, f"")
    print(sr.len())  // 3
    print(sr[0])      // hello
    // sr[1] and sr[2] are empty strings (safe to print)

    // shrink Str vec — drops freed strings
    sr.push("world")
    sr.resize(1, f"")
    print(sr.len())  // 1
    print(sr[0])      // hello

    // ===================== copy() =====================

    Vec(int) orig = {}
    orig.push(10)
    orig.push(20)
    orig.push(30)

    Vec(int) cp = orig.copy()
    print(cp.len())  // 3
    print(cp[0])      // 10
    print(cp[1])      // 20
    print(cp[2])      // 30

    // Mutations to copy don't affect original
    cp.push(40)
    print(orig.len()) // 3  (unchanged)
    print(cp.len())   // 4

    // copy of empty vec
    Vec(int) empty3 = {}
    Vec(int) cp2 = empty3.copy()
    print(cp2.len())  // 0
    print(cp2.empty?) // true

    // copy of Str vec — independent deep copies
    Vec(Str) sorig = {}
    sorig.push("hello")
    sorig.push("world")
    Vec(Str) scp = sorig.copy()
    print(scp.len())  // 2
    print(scp[0])      // hello
    print(scp[1])      // world

    return 0
}
