// string_ord_test.ls -- builtin string satisfies Ord and uses lexicographic strcmp.

import std.vec

fn check(bool c, string l) {
    if c { print(f"ok {l}") } else { print(f"FAIL {l}") }
}

fn min_ord(T: Ord)(T a, T b) -> T {
    if a < b { return a }
    return b
}

fn main() {
    check("apple" < "banana", "string <")
    check("banana" > "apple", "string >")
    check("apple" <= "apple", "string <=")
    check("banana" >= "banana", "string >=")
    check(min_ord(string)("pear", "peach") == "peach", "string satisfies Ord")

    Vec(string) words = [f"banana", f"apple", f"cherry", f"avocado"]
    words.sort()
    check(words[0] == "apple", "rawvec string sort[0]")
    check(words[1] == "avocado", "rawvec string sort[1]")
    check(words[2] == "banana", "rawvec string sort[2]")
    check(words[3] == "cherry", "rawvec string sort[3]")

    Vec(int) nums = [5, 2, 8, 1]
    nums.sort()
    check(nums[0] == 1 && nums[3] == 8, "rawvec int sort")

    print("STRING ORD PASS")
}
