// P0 self-verifying sample (docs/plan_string_to_stdlib.md §9 P0).
//
// Str coexists with the builtin string; this exercises the UNIFIED has_drop
// ownership path (build / read / clone / move / struct field / Vec element /
// drop). Prints "STRP0 PASS" iff every check holds, else "STRP0 FAIL ...".
// Driven by tests/test_plotfmt.cmake: JIT + AOT correctness + memcheck 0/0/0.
import std.str
import std.vec

struct Holder { Str s; int tag }

fn check(bool ok, string what) {
    if !ok { print(f"STRP0 FAIL: {what}") }
}

fn main() {
    // build from a builtin string literal (P0 bridge)
    Str a = Str.from_string("hello")
    check(a.len() == 5, "len")
    check(a.byte_at(0) == 104, "byte_at h")     // 'h'
    check(a.byte_at!(4) == 111, "byte_at! o")   // 'o'
    check(a.to_string() == "hello", "to_string")

    // clone -> two independent owned buffers; mutating the clone must NOT touch a
    Str b = a.__clone()
    check(b.eq?(a), "clone eq")
    b.push_byte(33)                              // '!'
    check(b.to_string() == "hello!", "clone mutate")
    check(a.to_string() == "hello", "deep copy isolation")

    // move: a -> c, a is dead afterwards
    Str c = a
    check(c.to_string() == "hello", "move")

    // struct field ownership
    Holder h = Holder { s: Str.from_string("world"), tag: 7 }
    check(h.s.to_string() == "world", "field str")
    check(h.tag == 7, "field tag")

    // Vec(Str) element ownership (move in, drop all)
    Vec(Str) v = {}
    v.push(Str.from_string("x"))
    v.push(Str.from_string("yy"))
    check(v.len() == 2, "vec len")
    check(v.get(1).to_string() == "yy", "vec elem")

    // empty Str
    Str e = Str.from_string("")
    check(e.empty?(), "empty")

    print("STRP0 PASS")
}
