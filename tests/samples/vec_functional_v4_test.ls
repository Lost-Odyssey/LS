// Phase V.4 — Vec.reduce(U)(init, Block(U,T)->U) -> U
import std.vec

fn main() {
    Vec(int) nums = [1, 2, 3, 4, 5]

    // === reduce: sum ===
    int sum = nums.reduce(int)(0, |acc, x| acc + x)
    if sum != 15 {
        print("FAIL: sum expected 15 got")
        print(sum)
        return
    }
    print("PASS: reduce sum = 15")

    // === reduce: product ===
    int prod = nums.reduce(int)(1, |acc, x| acc * x)
    if prod != 120 {
        print("FAIL: product expected 120 got")
        print(prod)
        return
    }
    print("PASS: reduce product = 120")

    // === reduce: max ===
    Vec(int) vals = [3, 1, 4, 1, 5, 9, 2, 6]
    int mx = vals.reduce(int)(0, |acc, x| {
        if x > acc {
            return x
        }
        return acc
    })
    if mx != 9 {
        print("FAIL: max expected 9 got")
        print(mx)
        return
    }
    print("PASS: reduce max = 9")

    // === reduce: count matches ===
    int big_count = nums.reduce(int)(0, |acc, x| {
        if x > 3 {
            return acc + 1
        }
        return acc
    })
    if big_count != 2 {
        print("FAIL: count>3 expected 2 got")
        print(big_count)
        return
    }
    print("PASS: reduce count>3 = 2")

    // === reduce on empty Vec ===
    Vec(int) empty = {}
    int empty_sum = empty.reduce(int)(42, |acc, x| acc + x)
    if empty_sum != 42 {
        print("FAIL: empty reduce expected 42 got")
        print(empty_sum)
        return
    }
    print("PASS: reduce empty = init value")

    // === reduce: single element ===
    Vec(int) single = [7]
    int sv = single.reduce(int)(0, |acc, x| acc + x)
    if sv != 7 {
        print("FAIL: single reduce expected 7 got")
        print(sv)
        return
    }
    print("PASS: reduce single = 7")

    // === reduce with string concatenation ===
    Vec(string) words = {}
    string w1 = "hello"
    string w2 = " "
    string w3 = "world"
    words.push(w1)
    words.push(w2)
    words.push(w3)
    string joined = words.reduce(string)(f"", |acc, s| acc + s)
    if joined.compare("hello world") != 0 {
        print("FAIL: string reduce expected 'hello world' got")
        print(joined)
        return
    }
    print("PASS: reduce string concat = 'hello world'")

    // === reduce: string length sum ===
    int total_len = words.reduce(int)(0, |acc, s| acc + s.length)
    if total_len != 11 {
        print("FAIL: total_len expected 11 got")
        print(total_len)
        return
    }
    print("PASS: reduce string lengths sum = 11")

    // === reduce f64 sum ===
    Vec(int) fv = [1, 2, 3]
    f64 fsum = fv.reduce(f64)(0.0, |acc, x| acc + x)
    if fsum < 5.99 {
        print("FAIL: f64 sum expected 6.0")
        return
    }
    print("PASS: reduce f64 sum = 6.0")

    print("ALL PASS")
}
