// region_intern_test.ls — std.mem.arena: Region.str_substr (zero-malloc slice
// interning, the parser/AST token pattern). See docs/plan_region_intern_str.md.
//
// Covers: slice intern + value, multi-token carve from one source, used()
// advance, clone-promote-survives-reset, mutation-promote-survives-reset,
// lenient clamping (negative / overflow start+len, empty slice), reset reuse.
// str_substr never mallocs; the region block + promoted clones/mutations are all
// freed by their owners → memcheck must be 0/0/0.
// Prints "ok <label>" / "FAIL <label>" then "REGION INTERN PASS".

import std.mem.arena
import std.core.str

def check(bool c, Str l) {
    if c { @print(f"ok {l}") } else { @print(f"FAIL {l}") }
}

def main() -> int {
    Region r = {}
    r.reserve(4096)

    // source buffer (static literal, cap == 0); tokens are slices of it
    Str src = "hello world foo bar"   // len 19

    // ---- basic slice intern: carve tokens straight from the source ----
    Str t_hello = r.str_substr(src, 0, 5)
    Str t_world = r.str_substr(src, 6, 5)
    Str t_foo = r.str_substr(src, 12, 3)
    Str t_bar = r.str_substr(src, 16, 3)
    check(t_hello.eq?("hello"), "slice [0,5) = hello")
    check(t_world.eq?("world"), "slice [6,11) = world")
    check(t_foo.eq?("foo"), "slice [12,15) = foo")
    check(t_bar.eq?("bar"), "slice [16,19) = bar")
    check(t_world.len() == 5, "slice len is byte count")
    check(r.used() > 0, "interned bytes live in region (used advanced)")

    // ---- multiple tokens coexist, independent, correct ----
    check(t_hello.eq?("hello") && t_bar.eq?("bar"), "multi-token no cross-talk")

    // ---- lenient clamping (matches Str.substr): never abort ----
    Str c_neg = r.str_substr(src, 0 - 3, 4)        // start clipped to 0 -> "hell"
    check(c_neg.eq?("hell"), "negative start clipped to 0")
    Str c_over_s = r.str_substr(src, 100, 5)       // start past end -> empty
    check(c_over_s.empty?(), "start past end -> empty slice")
    Str c_over_l = r.str_substr(src, 6, 1000)      // len clipped to end
    check(c_over_l.eq?("world foo bar"), "overflow len clipped to end")
    Str c_zero = r.str_substr(src, 3, 0)           // explicit empty
    check(c_zero.empty?(), "len 0 -> empty slice")

    // ---- clone PROMOTES off the region (deep copy to malloc) ----
    Str keep_hello = t_hello.copy()
    Str keep_bar = t_bar.copy()

    // ---- mutation PROMOTES too (reserve copy-on-grow on cap <= 0) ----
    Str m = r.str_substr(src, 0, 5)                // "hello", region-backed
    m.push_str("!!!")                              // first grow -> promote to malloc
    check(m.eq?("hello!!!"), "mutation auto-promotes off region")

    // ---- reset rewinds the region; promoted copies are independent ----
    i64 used_before = r.used()
    check(used_before > 0, "region used before reset")
    r.reset()
    check(r.used() == 0, "reset rewinds used to 0")
    check(r.cap() == 4096, "reset keeps the block (cap unchanged)")
    // t_* now dangle (contract: not read post-reset); clones survive:
    check(keep_hello.eq?("hello"), "cloned token survives reset (promoted)")
    check(keep_bar.eq?("bar"), "second cloned token survives reset")
    check(m.eq?("hello!!!"), "promoted-by-mutation Str survives reset")

    // ---- reuse the region after reset ----
    Str again = r.str_substr(src, 12, 3)
    check(again.eq?("foo"), "intern works again after reset")
    check(r.used() > 0, "region reused after reset")

    @print("REGION INTERN PASS")
    return 0
}
