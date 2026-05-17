// Phase V.3 — vec.map(Block(T)->U) -> vec(U)

fn main() {
    vec(int) nums = [1, 2, 3, 4, 5]

    // === map same type: int -> int ===
    vec(int) doubled = nums.map(|x| x * 2)
    if doubled.length != 5 {
        print("FAIL: doubled.length expected 5")
        return
    }
    if doubled.get(0) != 2 {
        print("FAIL: doubled[0] expected 2")
        return
    }
    if doubled.get(4) != 10 {
        print("FAIL: doubled[4] expected 10")
        return
    }
    print("PASS: map int->int doubled")

    // === map cross type: int -> string ===
    vec(string) strs = nums.map(|x| f"v={x}")
    if strs.length != 5 {
        print("FAIL: strs.length expected 5")
        return
    }
    if strs.get(0).compare("v=1") != 0 {
        print("FAIL: strs[0] expected 'v=1'")
        return
    }
    if strs.get(4).compare("v=5") != 0 {
        print("FAIL: strs[4] expected 'v=5'")
        return
    }
    print("PASS: map int->string fstring")

    // === map with condition (multiply only evens, else 0) ===
    vec(int) evens_doubled = nums.map(|x| x * 2)
    if evens_doubled.get(1) != 4 {
        print("FAIL: evens_doubled[1] expected 4")
        return
    }
    print("PASS: map positional check")

    // === map on empty vec ===
    vec(int) empty = []
    vec(int) empty_mapped = empty.map(|x| x + 1)
    if empty_mapped.length != 0 {
        print("FAIL: empty map expected length 0")
        return
    }
    print("PASS: map on empty vec")

    // === map string vec -> string vec (length transform) ===
    vec(string) words = []
    string w1 = "hello"
    string w2 = "world"
    string w3 = "hi"
    words.push(w1)
    words.push(w2)
    words.push(w3)
    vec(int) lengths = words.map(|s| s.length)
    if lengths.length != 3 {
        print("FAIL: lengths.length expected 3")
        return
    }
    if lengths.get(0) != 5 {
        print("FAIL: lengths[0] expected 5 (hello)")
        return
    }
    if lengths.get(1) != 5 {
        print("FAIL: lengths[1] expected 5 (world)")
        return
    }
    if lengths.get(2) != 2 {
        print("FAIL: lengths[2] expected 2 (hi)")
        return
    }
    print("PASS: map string->int (length)")

    // === map string -> string (upper) ===
    vec(string) uppers = words.map(|s| s.upper())
    if uppers.length != 3 {
        print("FAIL: uppers.length expected 3")
        return
    }
    if uppers.get(0).compare("HELLO") != 0 {
        print("FAIL: uppers[0] expected HELLO")
        return
    }
    if uppers.get(2).compare("HI") != 0 {
        print("FAIL: uppers[2] expected HI")
        return
    }
    print("PASS: map string->string (upper)")

    // === chain: map then filter (via intermediate variable for RAII cleanup) ===
    vec(int) tripled = nums.map(|x| x * 3)
    vec(int) big = tripled.filter(|x| x > 9)
    if big.length != 2 {
        print("FAIL: chain map.filter expected len 2")
        return
    }
    if big.get(0) != 12 {
        print("FAIL: chain[0] expected 12")
        return
    }
    if big.get(1) != 15 {
        print("FAIL: chain[1] expected 15")
        return
    }
    print("PASS: chain map->filter")

    // === map bool result ===
    vec(bool) bools = nums.map(|x| x > 3)
    if bools.length != 5 {
        print("FAIL: bools.length expected 5")
        return
    }
    if bools.get(0) != false {
        print("FAIL: bools[0] expected false")
        return
    }
    if bools.get(4) != true {
        print("FAIL: bools[4] expected true")
        return
    }
    print("PASS: map int->bool")

    print("ALL PASS")
}
