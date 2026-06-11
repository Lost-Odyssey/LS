// vec_parity_p1_test.ls -- Vec parity P1 with builtin vec APIs that do
// not require method-level generic result types.

import std.vec
import std.str

fn check(bool c, Str l) { if c { print(f"ok {l}") } else { print(f"FAIL {l}") } }

fn main() {
    Vec(int) v = [3, 1, 4, 1, 5]
    check(v.get!(2) == 4, "get_unsafe")
    check(v.as_ptr() != nil, "as_ptr")

    Vec(int) extra = [9, 2]
    v.extend(extra)
    check(v.len() == 7 && extra.len() == 2, "extend keeps src")
    check(v[5] == 9 && v[6] == 2, "extend values")

    Vec(int) sl = v.slice(1, 4)
    check(sl.len() == 3 && sl[0] == 1 && sl[2] == 1, "slice normal")
    Vec(int) clamped = v.slice(-5, 2)
    check(clamped.len() == 2 && clamped[0] == 3 && clamped[1] == 1, "slice clamp")

    Vec(Str) words = [f"banana", f"hi", f"apple"]
    Vec(Str) tail = words.slice(1, 3)
    check(tail.len() == 2 && tail[0].eq?("hi") && tail[1].eq?("apple"), "string slice")

    print("RAWVEC PARITY P1 PASS")
}
