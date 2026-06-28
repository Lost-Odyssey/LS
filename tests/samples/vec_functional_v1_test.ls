// Phase V.1 — Vec functional methods: any, all, count, each
import std.core.vec
import std.core.str

def main() {
    // === Setup ===
    Vec(int) nums = [1, 2, 3, 4, 5]

    // === any: at least one element > 3 ===
    bool has_big = nums.any(|x| x > 3)
    if !has_big {
        @print("FAIL: any(x > 3) should be true")
        return
    }
    @print("PASS: any(x > 3) = true")

    // === any: none > 10 ===
    bool has_huge = nums.any(|x| x > 10)
    if has_huge {
        @print("FAIL: any(x > 10) should be false")
        return
    }
    @print("PASS: any(x > 10) = false")

    // === all: all > 0 ===
    bool all_pos = nums.all(|x| x > 0)
    if !all_pos {
        @print("FAIL: all(x > 0) should be true")
        return
    }
    @print("PASS: all(x > 0) = true")

    // === all: not all > 3 ===
    bool all_big = nums.all(|x| x > 3)
    if all_big {
        @print("FAIL: all(x > 3) should be false")
        return
    }
    @print("PASS: all(x > 3) = false")

    // === count: how many > 2 ===
    int c = nums.count(|x| x > 2)
    if c != 3 {
        @print("FAIL: count(x > 2) expected 3 got")
        @print(c)
        return
    }
    @print("PASS: count(x > 2) = 3")

    // === count: how many == 99 ===
    int c0 = nums.count(|x| x == 99)
    if c0 != 0 {
        @print("FAIL: count(x == 99) expected 0")
        return
    }
    @print("PASS: count(x == 99) = 0")

    // === each: execute closure for every element ===
    nums.each(|x| { int y = x + 1 })
    @print("PASS: each closure executed")

    // === empty Vec ===
    Vec(int) empty = {}
    bool ea = empty.any(|x| x > 0)
    bool el = empty.all(|x| x > 0)
    int ec = empty.count(|x| x > 0)
    if ea {
        @print("FAIL: empty.any should be false")
        return
    }
    if !el {
        @print("FAIL: empty.all should be true")
        return
    }
    if ec != 0 {
        @print("FAIL: empty.count should be 0")
        return
    }
    @print("PASS: empty Vec any=false all=true count=0")

    // === Str elements (borrowed) ===
    Vec(Str) names = {}
    Str n1 = "alice"
    Str n2 = "bob"
    Str n3 = "charlie"
    names.push(n1)
    names.push(n2)
    names.push(n3)
    int long_names = names.count(|s| s.len() > 3)
    if long_names != 2 {
        @print("FAIL: string count(len > 3) expected 2 got")
        @print(long_names)
        return
    }
    @print("PASS: string count(len > 3) = 2")

    // Regression: expression-body closure whose Block return is void, e.g.
    // `|x| @print(x)` (print returns void). The desugared `{ return @print(x) }`
    // used to emit `ret <value>` into a void function and fail codegen
    // verification; now the value is evaluated for side effects and discarded.
    Vec(int) ev = [1, 2, 3]
    ev.each(|x| @print(x))                 // void expression body, no inner { }
    Vec(Str) sv = ["aa", "bbbb"]
    sv.each(|s| @print(s.len()))           // has_drop element, void expr body
    @print("PASS: expression-body void closure")

    // Regression: type-arg + trailing-closure sugar — `v.map(int) { |x| ... }`
    // (the closure is the sole value arg, written as a trailing block instead
    // of `v.map(int)(|x| ...)`).
    Vec(int) tc = [1, 2, 3]
    Vec(int) tcm = tc.map(int) { |x|
        int yy = x * 10
        return yy
    }
    if tcm.get!(0) == 10 && tcm.get!(2) == 30 {
        @print("PASS: type-arg trailing closure")
    } else {
        @print("FAIL: type-arg trailing closure")
    }

    // Regression: closure-return type inference — `v.map(|x| ...)` with NO
    // explicit `(U)`; U is inferred from the closure body's return type.
    Vec(int) inf = [1, 2, 3]
    Vec(int) infm = inf.map(|x| x + 100)             // U inferred = int
    Vec(Str) infs = inf.map(|x| f"k{x}")             // U inferred = Str
    if infm.get!(1) == 102 && infs.get!(0).eq?("k1") {
        @print("PASS: closure-return inference")
    } else {
        @print("FAIL: closure-return inference")
    }

    @print("ALL PASS")
}
