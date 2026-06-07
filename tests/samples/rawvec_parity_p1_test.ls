// rawvec_parity_p1_test.ls -- RawVec parity P1 with builtin vec APIs that do
// not require method-level generic result types.

import std.rawvec

fn check(bool c, string l) { if c { print(f"ok {l}") } else { print(f"FAIL {l}") } }

fn main() {
    RawVec(int) v = [3, 1, 4, 1, 5]
    check(v.get_unsafe(2) == 4, "get_unsafe")
    check(v.as_ptr() != nil, "as_ptr")

    RawVec(int) extra = [9, 2]
    v.extend(extra)
    check(v.length() == 7 && extra.length() == 2, "extend keeps src")
    check(v[5] == 9 && v[6] == 2, "extend values")

    RawVec(int) sl = v.slice(1, 4)
    check(sl.length() == 3 && sl[0] == 1 && sl[2] == 1, "slice normal")
    RawVec(int) clamped = v.slice(-5, 2)
    check(clamped.length() == 2 && clamped[0] == 3 && clamped[1] == 1, "slice clamp")

    RawVec(string) words = [f"banana", f"hi", f"apple"]
    RawVec(string) tail = words.slice(1, 3)
    check(tail.length() == 2 && tail[0] == "hi" && tail[1] == "apple", "string slice")

    print("RAWVEC PARITY P1 PASS")
}
