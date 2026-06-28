// vec Batch E end-to-end test: sort(), sort_by(), slice(), shrink_to_fit()
import std.core.vec
import std.core.str

def cmp_desc(int a, int b) -> int {
    return b - a
}

def main() -> int {
    // ===================== sort() =====================

    // sort int vec ascending
    Vec(int) v = {}
    v.push(5)
    v.push(2)
    v.push(8)
    v.push(1)
    v.push(9)
    v.push(3)
    v.sort()
    @print(v[0])   // 1
    @print(v[1])   // 2
    @print(v[2])   // 3
    @print(v[3])   // 5
    @print(v[4])   // 8
    @print(v[5])   // 9

    // sort already sorted — nop
    v.sort()
    @print(v[0])   // 1
    @print(v[5])   // 9

    // sort single element — nop
    Vec(int) s1 = {}
    s1.push(42)
    s1.sort()
    @print(s1[0])  // 42

    // sort empty vec — nop
    Vec(int) se = {}
    se.sort()
    @print(se.len())  // 0

    // sort Str Vec(lexicographic)
    Vec(Str) sv = {}
    sv.push("banana")
    sv.push("apple")
    sv.push("cherry")
    sv.push("avocado")
    sv.sort()
    @print(sv[0])  // apple
    @print(sv[1])  // avocado
    @print(sv[2])  // banana
    @print(sv[3])  // cherry

    // ===================== sort_by() =====================

    // sort_by descending
    Vec(int) d = {}
    d.push(3)
    d.push(1)
    d.push(4)
    d.push(1)
    d.push(5)
    d.sort_by(|a, b| cmp_desc(a, b))
    @print(d[0])   // 5
    @print(d[1])   // 4
    @print(d[2])   // 3
    @print(d[3])   // 1
    @print(d[4])   // 1

    // ===================== slice() =====================

    Vec(int) orig = {}
    orig.push(10)
    orig.push(20)
    orig.push(30)
    orig.push(40)
    orig.push(50)

    // normal slice [1, 4)
    Vec(int) sl = orig.slice(1, 4)
    @print(sl.len())  // 3
    @print(sl[0])      // 20
    @print(sl[1])      // 30
    @print(sl[2])      // 40

    // slice full range
    Vec(int) sl2 = orig.slice(0, 5)
    @print(sl2.len()) // 5
    @print(sl2[0])     // 10
    @print(sl2[4])     // 50

    // slice with out-of-bounds end — clamps to len
    Vec(int) sl3 = orig.slice(3, 99)
    @print(sl3.len()) // 2
    @print(sl3[0])     // 40
    @print(sl3[1])     // 50

    // slice with start >= end — returns empty
    Vec(int) sl4 = orig.slice(3, 2)
    @print(sl4.len()) // 0
    @print(sl4.empty?)  // true

    // slice with negative start — clamps to 0
    Vec(int) sl5 = orig.slice(-5, 2)
    @print(sl5.len()) // 2
    @print(sl5[0])     // 10
    @print(sl5[1])     // 20

    // slice empty vec
    Vec(int) empty_sl = {}
    Vec(int) sl6 = empty_sl.slice(0, 10)
    @print(sl6.len()) // 0

    // slice Str vec — independent deep copies
    Vec(Str) ssrc = {}
    ssrc.push("alpha")
    ssrc.push("beta")
    ssrc.push("gamma")
    ssrc.push("delta")
    Vec(Str) sslice = ssrc.slice(1, 3)
    @print(sslice.len())  // 2
    @print(sslice[0])      // beta
    @print(sslice[1])      // gamma

    // ===================== shrink_to_fit() =====================

    Vec(int) big = {}
    big.reserve(100)
    big.push(1)
    big.push(2)
    big.push(3)
    // cap is 100, len is 3
    big.shrink_to_fit()
    // after shrink: cap == len == 3
    @print(big.len())     // 3
    @print(big[0])         // 1
    @print(big[1])         // 2
    @print(big[2])         // 3
    // can still push after shrink
    big.push(4)
    @print(big.len())     // 4
    @print(big[3])         // 4

    // shrink_to_fit on empty vec — frees data, cap=0
    Vec(int) empty_big = {}
    empty_big.reserve(50)
    empty_big.shrink_to_fit()
    @print(empty_big.len())     // 0
    @print(empty_big.empty?) // true
    // can still push after shrinking empty
    empty_big.push(99)
    @print(empty_big.len())     // 1
    @print(empty_big[0])         // 99

    // shrink_to_fit when cap == len — nop
    Vec(int) exact = {}
    exact.push(7)
    exact.shrink_to_fit()
    @print(exact.len())   // 1
    @print(exact[0])       // 7

    return 0
}
