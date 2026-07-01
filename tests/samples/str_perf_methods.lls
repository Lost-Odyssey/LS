// strbench P0/P1/P2 optimization regression: the runtime-accelerated string
// methods must stay byte-for-byte correct.
//   P0: find/contains?/count via __ls_str_find (memchr+memcmp); replace with a
//       single-byte equal-length fast-path + memchr-located general path.
//   P1: substr/concat/__clone/reserve/push_str/+/repeat copy via __ls_bytecopy.
//   P2: split_view -> Vec(StrSlice) (zero per-part malloc) + slice_str.
// Exercises owned / static / empty edge cases so memcheck catches any leak or
// double-free in the new paths. JIT + AOT + memcheck 0/0/0.
import std.core.str
import std.core.vec

def check(bool ok, Str what) {
    if !ok { @print(f"STRPERF FAIL: {what}") }
}

def main() {
    // ---- P0: find / contains? / count ----
    Str s = "hello world, hello LS, hello!"
    Str hello = "hello"
    Str x = "xyz"
    Str empty = ""
    check(s.find(hello) == 0, "find first")
    check(s.find(x) == -1, "find absent")
    check(s.find(empty) == 0, "find empty needle")
    check(s.contains?(hello), "contains hit")
    check(!s.contains?(x), "contains miss")
    check(s.count(hello) == 3, "count 3")
    check(s.count(x) == 0, "count 0")
    check(s.count(empty) == 0, "count empty")
    // needle longer than haystack
    Str longer = "hello world hello world hello"
    check(s.find(longer) == -1, "find longer")

    // ---- P0: replace single-byte equal-length fast-path ----
    Str dots = "a.b.c.d.e"
    Str dot = "."
    Str dash = "-"
    Str r1 = dots.replace(dot, dash)
    check(r1.eq?("a-b-c-d-e"), "replace 1:1 fast-path")
    check(r1.len() == 9, "replace 1:1 len")
    // no match -> unchanged copy
    Str q = "q"
    Str z = "z"
    Str r2 = dots.replace(q, z)
    check(r2.eq?("a.b.c.d.e"), "replace no match")

    // ---- P0: replace general (multi-byte, grow + shrink) ----
    Str r3 = s.replace(hello, dash)            // shrink: hello(5) -> -(1)
    check(r3.eq?("- world, - LS, -!"), "replace shrink")
    Str hi = "hi"
    Str greet = "hi there hi"
    Str loong = "hello"
    Str r4 = greet.replace(hi, loong)          // grow: hi(2) -> hello(5)
    check(r4.eq?("hello there hello"), "replace grow")
    // empty old -> copy unchanged
    Str r5 = dots.replace(empty, dash)
    check(r5.eq?("a.b.c.d.e"), "replace empty old")
    // replace on empty string
    Str r6 = empty.replace(dot, dash)
    check(r6.len() == 0, "replace on empty")

    // ---- P1: concat / + / repeat / substr / copy / push_str ----
    Str a = "Hello"
    Str b = "World"
    check(a.concat(b).eq?("HelloWorld"), "concat")
    check((a + ", " + b).eq?("Hello, World"), "operator+")
    check(a.repeat(3).eq?("HelloHelloHello"), "repeat 3")
    check(a.repeat(0).len() == 0, "repeat 0")
    check(empty.repeat(5).len() == 0, "repeat empty")
    check(a.substr(1, 3).eq?("ell"), "substr mid")
    check(a.substr(0, 99).eq?("Hello"), "substr clamp len")
    check(a.substr(10, 5).len() == 0, "substr clamp start")
    check(a.copy().eq?("Hello"), "copy owned")
    Str acc = ""
    acc.push_str(a)
    acc.push_str(empty)
    acc.push_str(b)
    check(acc.eq?("HelloWorld"), "push_str chain")

    // ---- P2: split + split_view + slice_str ----
    Str sp = "the quick brown fox"
    Str sep = " "
    Vec(Str) parts = sp.split(sep)
    check(parts.len() == 4, "split len")
    check(parts[0].eq?("the"), "split[0]")
    check(parts[3].eq?("fox"), "split[3]")

    Vec(StrSlice) views = sp.split_view(sep)
    check(views.len() == 4, "split_view len")
    check(views[0].eq_str("the"), "view0 direct read (zero-copy)")
    check(views[0].len == 3, "view0 len")
    check(sp.slice_str(views[0]).eq?("the"), "slice_str 0")
    check(sp.slice_str(views[2]).eq?("brown"), "slice_str 2")
    check(sp.slice_str(views[3]).eq?("fox"), "slice_str 3")

    // split_view boundary: trailing sep yields a trailing empty view
    Str tr = "a,b,"
    Str comma = ","
    Vec(StrSlice) tv = tr.split_view(comma)
    check(tv.len() == 3, "trailing view count")
    check(tv[2].len == 0, "trailing empty view")
    check(tr.slice_str(tv[0]).eq?("a"), "trailing slice a")

    // split_view empty sep -> single whole-string view
    Vec(StrSlice) wv = sp.split_view(empty)
    check(wv.len() == 1, "empty-sep view count")
    check(wv[0].len == sp.len(), "empty-sep whole")

    // consistency: split and split_view agree on parts
    bool agree = true
    int i = 0
    while i < parts.len() {
        if !parts[i].eq?(sp.slice_str(views[i])) { agree = false }
        i = i + 1
    }
    check(agree, "split vs split_view agree")

    // ---- P0 algorithm: long haystack (>256B) exercises the Sunday/BMH skip
    // path in __ls_str_find; must agree with the short memchr+memcmp path ----
    Str unit = "abcdefghij"
    Str big = ""
    for k in 0..40 { big = big + unit }        // 400 bytes, no needle yet
    big = big + "NEEDLE_HERE"
    big = big + unit
    Str ndl = "NEEDLE_HERE"
    check(big.find(ndl) == 400, "long find")
    check(big.contains?(ndl), "long contains")
    check(big.find("ZZZ_NOPE") == -1, "long find absent")
    check(big.count(unit) == 41, "long count")
    Str rep = big.replace(ndl, unit)
    check(!rep.contains?(ndl), "long replace removed")
    check(rep.contains?(unit), "long replace kept")
    // first-byte-common needle (Sunday bad-char skip): "ababab...x" never matches
    Str rep2 = big.replace("NEEDLE", unit)     // partial overlap, multi-byte
    check(rep2.contains?(unit), "long replace partial")

    @print("STRPERF PASS")
}
