// vec Batch E end-to-end test: sort(), sort_by(), slice(), shrink_to_fit()

fn cmp_desc(int a, int b) -> int {
    return b - a
}

fn main() -> int {
    // ===================== sort() =====================

    // sort int vec ascending
    vec(int) v
    v.push(5)
    v.push(2)
    v.push(8)
    v.push(1)
    v.push(9)
    v.push(3)
    v.sort()
    print(v[0])   // 1
    print(v[1])   // 2
    print(v[2])   // 3
    print(v[3])   // 5
    print(v[4])   // 8
    print(v[5])   // 9

    // sort already sorted — nop
    v.sort()
    print(v[0])   // 1
    print(v[5])   // 9

    // sort single element — nop
    vec(int) s1
    s1.push(42)
    s1.sort()
    print(s1[0])  // 42

    // sort empty vec — nop
    vec(int) se
    se.sort()
    print(se.length)  // 0

    // sort string vec (lexicographic)
    vec(string) sv
    sv.push("banana")
    sv.push("apple")
    sv.push("cherry")
    sv.push("avocado")
    sv.sort()
    print(sv[0])  // apple
    print(sv[1])  // avocado
    print(sv[2])  // banana
    print(sv[3])  // cherry

    // ===================== sort_by() =====================

    // sort_by descending
    vec(int) d
    d.push(3)
    d.push(1)
    d.push(4)
    d.push(1)
    d.push(5)
    d.sort_by(cmp_desc)
    print(d[0])   // 5
    print(d[1])   // 4
    print(d[2])   // 3
    print(d[3])   // 1
    print(d[4])   // 1

    // ===================== slice() =====================

    vec(int) orig
    orig.push(10)
    orig.push(20)
    orig.push(30)
    orig.push(40)
    orig.push(50)

    // normal slice [1, 4)
    vec(int) sl = orig.slice(1, 4)
    print(sl.length)  // 3
    print(sl[0])      // 20
    print(sl[1])      // 30
    print(sl[2])      // 40

    // slice full range
    vec(int) sl2 = orig.slice(0, 5)
    print(sl2.length) // 5
    print(sl2[0])     // 10
    print(sl2[4])     // 50

    // slice with out-of-bounds end — clamps to len
    vec(int) sl3 = orig.slice(3, 99)
    print(sl3.length) // 2
    print(sl3[0])     // 40
    print(sl3[1])     // 50

    // slice with start >= end — returns empty
    vec(int) sl4 = orig.slice(3, 2)
    print(sl4.length) // 0
    print(sl4.is_empty())  // true

    // slice with negative start — clamps to 0
    vec(int) sl5 = orig.slice(-5, 2)
    print(sl5.length) // 2
    print(sl5[0])     // 10
    print(sl5[1])     // 20

    // slice empty vec
    vec(int) empty_sl
    vec(int) sl6 = empty_sl.slice(0, 10)
    print(sl6.length) // 0

    // slice string vec — independent deep copies
    vec(string) ssrc
    ssrc.push("alpha")
    ssrc.push("beta")
    ssrc.push("gamma")
    ssrc.push("delta")
    vec(string) sslice = ssrc.slice(1, 3)
    print(sslice.length)  // 2
    print(sslice[0])      // beta
    print(sslice[1])      // gamma

    // ===================== shrink_to_fit() =====================

    vec(int) big
    big.reserve(100)
    big.push(1)
    big.push(2)
    big.push(3)
    // cap is 100, len is 3
    big.shrink_to_fit()
    // after shrink: cap == len == 3
    print(big.length)     // 3
    print(big[0])         // 1
    print(big[1])         // 2
    print(big[2])         // 3
    // can still push after shrink
    big.push(4)
    print(big.length)     // 4
    print(big[3])         // 4

    // shrink_to_fit on empty vec — frees data, cap=0
    vec(int) empty_big
    empty_big.reserve(50)
    empty_big.shrink_to_fit()
    print(empty_big.length)     // 0
    print(empty_big.is_empty()) // true
    // can still push after shrinking empty
    empty_big.push(99)
    print(empty_big.length)     // 1
    print(empty_big[0])         // 99

    // shrink_to_fit when cap == len — nop
    vec(int) exact
    exact.push(7)
    exact.shrink_to_fit()
    print(exact.length)   // 1
    print(exact[0])       // 7

    return 0
}
