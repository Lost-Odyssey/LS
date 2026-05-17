// Phase V.5 — vec.sort_by(Block(T,T)->int) inline insertion sort

fn main() {
    // === ascending sort with lambda ===
    vec(int) nums = [3, 1, 4, 1, 5, 9, 2, 6]
    nums.sort_by(|a, b| a - b)
    if nums[0] != 1 {
        print("FAIL: asc sort[0] expected 1 got")
        print(nums[0])
        return
    }
    if nums[7] != 9 {
        print("FAIL: asc sort[7] expected 9 got")
        print(nums[7])
        return
    }
    print("PASS: ascending sort")

    // === descending sort — closure captures bool ===
    bool desc = true
    vec(int) vals = [5, 2, 8, 1, 9]
    vals.sort_by(|a, b| {
        if desc { return b - a }
        return a - b
    })
    if vals[0] != 9 {
        print("FAIL: desc sort[0] expected 9 got")
        print(vals[0])
        return
    }
    if vals[4] != 1 {
        print("FAIL: desc sort[4] expected 1 got")
        print(vals[4])
        return
    }
    print("PASS: descending sort with captured bool")

    // === sort strings by length ===
    vec(string) words = []
    string w1 = "banana"
    string w2 = "hi"
    string w3 = "apple"
    words.push(w1)
    words.push(w2)
    words.push(w3)
    words.sort_by(|a, b| a.length - b.length)
    if words[0].compare("hi") != 0 {
        print("FAIL: string sort by length[0] expected 'hi' got")
        print(words[0])
        return
    }
    if words[2].compare("banana") != 0 {
        print("FAIL: string sort by length[2] expected 'banana' got")
        print(words[2])
        return
    }
    print("PASS: sort strings by length")

    // === sort by distance from pivot (captured int) ===
    int pivot = 5
    vec(int) around = [1, 9, 4, 6, 3, 7]
    around.sort_by(|a, b| {
        int da = a - pivot
        if da < 0 { da = 0 - da }
        int db = b - pivot
        if db < 0 { db = 0 - db }
        return da - db
    })
    // Closest to 5: 4(d=1), 6(d=1), 3(d=2), 7(d=2), 1(d=4), 9(d=4)
    if around[4] != 1 && around[4] != 9 {
        print("FAIL: pivot sort tail expected 1 or 9 got")
        print(around[4])
        return
    }
    if around[0] != 4 && around[0] != 6 {
        print("FAIL: pivot sort head expected 4 or 6 got")
        print(around[0])
        return
    }
    print("PASS: sort by distance from captured pivot")

    // === empty vec (edge case) ===
    vec(int) empty = []
    empty.sort_by(|a, b| a - b)
    if empty.length != 0 {
        print("FAIL: empty sort changed length")
        return
    }
    print("PASS: empty vec sort")

    // === single element (edge case) ===
    vec(int) single = [42]
    single.sort_by(|a, b| a - b)
    if single[0] != 42 {
        print("FAIL: single element sort")
        return
    }
    print("PASS: single element sort")

    // === already sorted (no swaps needed) ===
    vec(int) sorted = [1, 2, 3, 4, 5]
    sorted.sort_by(|a, b| a - b)
    if sorted[0] != 1 || sorted[4] != 5 {
        print("FAIL: already-sorted vec corrupted")
        return
    }
    print("PASS: already sorted vec")

    // === reverse sorted input ===
    vec(int) rev = [5, 4, 3, 2, 1]
    rev.sort_by(|a, b| a - b)
    if rev[0] != 1 || rev[4] != 5 {
        print("FAIL: reverse sorted input")
        return
    }
    print("PASS: reverse sorted input")

    print("ALL PASS")
}
