// string_ord_test.ls -- Str satisfies the built-in Ord trait (lexicographic
// byte order via impl Ord for Str → compare). `<` is defined; `>`/`<=`/`>=`
// derive. Generic `min_ord(T: Ord)` and Vec(Str).sort() both rely on it.

import std.vec
import std.str

fn check(bool c, Str l) {
    if c { print(f"ok {l}") } else { print(f"FAIL {l}") }
}

fn min_ord(T: Ord)(T a, T b) -> T {
    if a < b { return a }
    return b
}

fn main() {
    Str apple = "apple"
    Str banana = "banana"
    Str pear = "pear"
    Str peach = "peach"

    check(apple < banana, "Str <")
    check(banana > apple, "Str >")
    check(apple <= apple, "Str <=")
    check(banana >= banana, "Str >=")
    check(min_ord(Str)(pear, peach).eq?("peach"), "Str satisfies Ord")

    Vec(Str) words = [f"banana", f"apple", f"cherry", f"avocado"]
    words.sort()
    check(words[0].eq?("apple"), "rawvec Str sort[0]")
    check(words[1].eq?("avocado"), "rawvec Str sort[1]")
    check(words[2].eq?("banana"), "rawvec Str sort[2]")
    check(words[3].eq?("cherry"), "rawvec Str sort[3]")

    Vec(int) nums = [5, 2, 8, 1]
    nums.sort()
    check(nums[0] == 1 && nums[3] == 8, "rawvec int sort")

    print("STRING ORD PASS")
}
