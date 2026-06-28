// vec Batch B end-to-end test: truncate(), remove(), swap(), reverse()
import std.core.vec
import std.core.str

def main() -> int {
    // ===================== truncate() =====================

    Vec(int) v = {}
    v.push(10)
    v.push(20)
    v.push(30)
    v.push(40)
    v.push(50)

    // truncate to 3 — drops [3], [4]
    v.truncate(3)
    @print(v.len())     // 3
    @print(v[0])        // 10
    @print(v[1])        // 20
    @print(v[2])        // 30

    // truncate to same length — nop
    v.truncate(3)
    @print(v.len())     // 3

    // truncate to 0
    v.truncate(0)
    @print(v.len())     // 0
    @print(v.empty?)    // true

    // truncate on Str vec — must drop freed strings without double-free
    Vec(Str) sv = {}
    sv.push("alpha")
    sv.push("beta")
    sv.push("gamma")
    sv.truncate(1)
    @print(sv.len())    // 1
    @print(sv[0])       // alpha

    // ===================== remove() =====================

    Vec(int) r = {}
    r.push(100)
    r.push(200)
    r.push(300)
    r.push(400)

    // remove middle element — [100, 200, 300, 400] → [100, 300, 400]
    r.remove(1)
    @print(r.len())     // 3
    @print(r[0])        // 100
    @print(r[1])        // 300
    @print(r[2])        // 400

    // remove first element — [100, 300, 400] → [300, 400]
    r.remove(0)
    @print(r.len())     // 2
    @print(r[0])        // 300

    // remove last element — [300, 400] → [300]
    r.remove(1)
    @print(r.len())     // 1
    @print(r[0])        // 300

    // remove on Str vec — must drop Str without leaking
    Vec(Str) sr = {}
    sr.push("one")
    sr.push("two")
    sr.push("three")
    sr.remove(1)
    @print(sr.len())    // 2
    @print(sr[0])       // one
    @print(sr[1])       // three

    // ===================== swap() =====================

    Vec(int) sw = {}
    sw.push(1)
    sw.push(2)
    sw.push(3)

    // swap first and last
    sw.swap(0, 2)
    @print(sw[0])       // 3
    @print(sw[1])       // 2
    @print(sw[2])       // 1

    // swap same index — nop
    sw.swap(1, 1)
    @print(sw[1])       // 2

    // swap on Str vec — raw byte swap, no clone/free
    Vec(Str) ssw = {}
    ssw.push("first")
    ssw.push("second")
    ssw.push("third")
    ssw.swap(0, 2)
    @print(ssw[0])      // third
    @print(ssw[2])      // first

    // ===================== reverse() =====================

    Vec(int) rev = {}
    rev.push(1)
    rev.push(2)
    rev.push(3)
    rev.push(4)

    rev.reverse()
    @print(rev[0])      // 4
    @print(rev[1])      // 3
    @print(rev[2])      // 2
    @print(rev[3])      // 1

    // reverse odd-length vec
    Vec(int) rev3 = {}
    rev3.push(10)
    rev3.push(20)
    rev3.push(30)
    rev3.reverse()
    @print(rev3[0])     // 30
    @print(rev3[1])     // 20
    @print(rev3[2])     // 10

    // reverse single-element — nop
    Vec(int) rev1 = {}
    rev1.push(42)
    rev1.reverse()
    @print(rev1[0])     // 42

    // reverse empty — nop, no crash
    Vec(int) reve = {}
    reve.reverse()
    @print(reve.len())  // 0

    // reverse on Str vec — raw byte swap, no leak
    Vec(Str) srev = {}
    srev.push("a")
    srev.push("b")
    srev.push("c")
    srev.reverse()
    @print(srev[0])     // c
    @print(srev[1])     // b
    @print(srev[2])     // a

    return 0
}
