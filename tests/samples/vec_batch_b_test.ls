// vec Batch B end-to-end test: truncate(), remove(), swap(), reverse()

fn main() -> int {
    // ===================== truncate() =====================

    vec(int) v
    v.push(10)
    v.push(20)
    v.push(30)
    v.push(40)
    v.push(50)

    // truncate to 3 — drops [3], [4]
    v.truncate(3)
    print(v.length)    // 3
    print(v[0])        // 10
    print(v[1])        // 20
    print(v[2])        // 30

    // truncate to same length — nop
    v.truncate(3)
    print(v.length)    // 3

    // truncate to 0
    v.truncate(0)
    print(v.length)    // 0
    print(v.is_empty()) // true

    // truncate negative — clamps to 0, same as truncate(0)
    vec(int) v2
    v2.push(1)
    v2.push(2)
    v2.truncate(-5)
    print(v2.length)   // 0

    // truncate on string vec — must drop freed strings without double-free
    vec(string) sv
    sv.push("alpha")
    sv.push("beta")
    sv.push("gamma")
    sv.truncate(1)
    print(sv.length)   // 1
    print(sv[0])       // alpha

    // ===================== remove() =====================

    vec(int) r
    r.push(100)
    r.push(200)
    r.push(300)
    r.push(400)

    // remove middle element — [100, 200, 300, 400] → [100, 300, 400]
    r.remove(1)
    print(r.length)    // 3
    print(r[0])        // 100
    print(r[1])        // 300
    print(r[2])        // 400

    // remove first element — [100, 300, 400] → [300, 400]
    r.remove(0)
    print(r.length)    // 2
    print(r[0])        // 300

    // remove last element — [300, 400] → [300]
    r.remove(1)
    print(r.length)    // 1
    print(r[0])        // 300

    // out-of-bounds remove — warns, no crash
    r.remove(5)        // [warning] vec.remove() index out of bounds
    print(r.length)    // 1  (unchanged)

    // remove on string vec — must drop string without leaking
    vec(string) sr
    sr.push("one")
    sr.push("two")
    sr.push("three")
    sr.remove(1)
    print(sr.length)   // 2
    print(sr[0])       // one
    print(sr[1])       // three

    // ===================== swap() =====================

    vec(int) sw
    sw.push(1)
    sw.push(2)
    sw.push(3)

    // swap first and last
    sw.swap(0, 2)
    print(sw[0])       // 3
    print(sw[1])       // 2
    print(sw[2])       // 1

    // swap same index — nop
    sw.swap(1, 1)
    print(sw[1])       // 2

    // out-of-bounds swap — warns, no crash
    sw.swap(0, 99)     // [warning] vec.swap() index out of bounds
    print(sw[0])       // 3  (unchanged)

    // swap on string vec — raw byte swap, no clone/free
    vec(string) ssw
    ssw.push("first")
    ssw.push("second")
    ssw.push("third")
    ssw.swap(0, 2)
    print(ssw[0])      // third
    print(ssw[2])      // first

    // ===================== reverse() =====================

    vec(int) rev
    rev.push(1)
    rev.push(2)
    rev.push(3)
    rev.push(4)

    rev.reverse()
    print(rev[0])      // 4
    print(rev[1])      // 3
    print(rev[2])      // 2
    print(rev[3])      // 1

    // reverse odd-length vec
    vec(int) rev3
    rev3.push(10)
    rev3.push(20)
    rev3.push(30)
    rev3.reverse()
    print(rev3[0])     // 30
    print(rev3[1])     // 20
    print(rev3[2])     // 10

    // reverse single-element — nop
    vec(int) rev1
    rev1.push(42)
    rev1.reverse()
    print(rev1[0])     // 42

    // reverse empty — nop, no crash
    vec(int) reve
    reve.reverse()
    print(reve.length) // 0

    // reverse on string vec — raw byte swap, no leak
    vec(string) srev
    srev.push("a")
    srev.push("b")
    srev.push("c")
    srev.reverse()
    print(srev[0])     // c
    print(srev[1])     // b
    print(srev[2])     // a

    return 0
}
