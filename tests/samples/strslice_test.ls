// strslice_test.ls — StrSlice zero-copy fat view (the &str-equivalent).
// See docs/plan_strslice_view.md. ALL samples keep the source Str alive for the
// view's whole lifetime (the safety contract) — no dangling exercised (UB can't
// be asserted). Covers: as_slice/subslice + clamp, at/at!, eq/eq_str, find/
// contains?/starts_with?/ends_with?, sub, compare/Order, hash == Str.hash,
// Map(StrSlice,int) content-keyed symbol table, to_str independence, split_view
// direct read + slice_str compat. StrSlice is POD (no alloc) → memcheck 0/0/0.
// Prints "ok <label>" / "FAIL <label>" then "STRSLICE PASS".

import std.core.str
import std.core.vec
import std.core.map

def check(bool c, Str l) {
    if c { @print(f"ok {l}") } else { @print(f"FAIL {l}") }
}

def main() -> int {
    Str src = "hello world hello foo"   // "hello" appears at 0 and 12

    // ---- as_slice + basic reads ----
    StrSlice whole = src.as_slice()
    check(whole.len() == 21, "as_slice whole len")
    check(!whole.empty?(), "whole not empty")
    check(whole.at(0) == 104, "at(0) == 'h'")       // 'h' = 104
    check(whole.at!(1) == 101, "at!(1) == 'e'")     // 'e' = 101

    // ---- subslice + zero-copy compare ----
    StrSlice hello = src.subslice(0, 5)
    StrSlice world = src.subslice(6, 5)
    StrSlice hello2 = src.subslice(12, 5)
    check(hello.eq_str("hello"), "subslice eq_str literal")
    check(world.eq_str("world"), "subslice world")
    check(hello.len() == 5, "subslice len")
    check(hello.eq(hello2), "two slices same content eq (different ptr)")
    check(!hello.eq(world), "different content not eq")

    // ---- lenient clamping (never abort) ----
    check(src.subslice(0 - 3, 4).eq_str("hell"), "negative start clipped")
    check(src.subslice(100, 5).empty?(), "start past end -> empty")
    check(src.subslice(6, 1000).eq_str("world hello foo"), "overflow len clipped")
    check(src.subslice(3, 0).empty?(), "len 0 -> empty")

    // ---- search ----
    check(whole.find("world") == 6, "find world @6")
    check(whole.find("zzz") == 0 - 1, "find miss -> -1")
    check(whole.contains?("foo"), "contains foo")
    check(hello.starts_with?("he"), "starts_with he")
    check(world.ends_with?("ld"), "ends_with ld")
    check(!hello.starts_with?("wo"), "not starts_with wo")

    // ---- sub-view of a view ----
    StrSlice ell = hello.sub(1, 3)
    check(ell.eq_str("ell"), "sub-view ell")

    // ---- Order ----
    check(hello.compare(world) < 0, "compare hello < world")
    check(hello < world, "operator < ")
    check(!(world < hello), "world not < hello")

    // ---- hash interchangeable with Str ----
    Str hs = "hello"
    check(hello.hash() == hs.hash(), "StrSlice hash == Str hash (same bytes)")

    // ---- Map(StrSlice, int): content-keyed symbol table, no materialization ----
    Map(StrSlice, int) sym = {}
    sym.set(hello, 1)
    sym.set(world, 2)
    check(sym.get(hello2).unwrap_or(0 - 1) == 1, "map keyed by content (hello2 finds hello)")
    check(sym.get(world).unwrap_or(0 - 1) == 2, "map get world")
    check(sym.len() == 2, "map has 2 distinct keys")

    // ---- materialize: to_str is independent owned ----
    Str owned = hello.to_str()
    check(owned.eq?("hello"), "to_str value")
    check(owned.cap() > 0, "to_str owns a heap buffer (cap>0)")

    // ---- split_view: direct read (no slice_str) + slice_str compat ----
    Str line = "the quick brown fox"
    Str sp = " "
    Vec(StrSlice) toks = line.split_view(&sp)
    check(toks.len() == 4, "split_view count")
    check(toks[0].eq_str("the"), "split_view direct read [0]")
    check(toks[2].eq_str("brown"), "split_view direct read [2]")
    check(line.slice_str(toks[3]).eq?("fox"), "slice_str compat materialize")

    @print("STRSLICE PASS")
    return 0
}
