// Str method-port batch 2: rfind/count/compare/replace/pad_left/pad_right +
// collection returners bytes/split/lines (Vec results). Pure-LS impl Str over
// the byte buffer; owned Str results + Vec(Str)/Vec(int) drop clean. JIT+AOT+memcheck.
import std.str
import std.vec

fn check(bool ok, Str what) {
    if !ok { print(f"STRM2 FAIL: {what}") }
}

fn main() {
    Str s = "a.b.c.b"
    Str dot = "."
    Str b = "b"
    Str ab = "ab"

    // rfind / count
    check(s.rfind(b) == 6, "rfind")
    check(s.rfind(dot) == 5, "rfind dot")
    check(s.count(b) == 2, "count b")
    check(s.count(dot) == 3, "count dot")

    // compare
    Str apple = "apple"
    Str apply = "apply"
    Str app = "app"
    check(apple.compare(apply) == -1, "cmp lt")
    check(apply.compare(apple) == 1, "cmp gt")
    check(apple.compare(apple) == 0, "cmp eq")
    check(apple.compare(app) == 1, "cmp prefix")

    // replace
    Str src = "a.b.c"
    Str dash = "-"
    check(src.replace(dot, dash).eq?("a-b-c"), "replace")
    Str hello = "hello"
    Str ll = "ll"
    Str LL = "[LL]"
    check(hello.replace(ll, LL).eq?("he[LL]o"), "replace mid")

    // pad
    Str x = "42"
    check(x.pad_left(5, 48).eq?("00042"), "pad_left")    // '0' = 48
    check(x.pad_right(5, 46).eq?("42..."), "pad_right")  // '.' = 46
    check(x.pad_left(1, 48).eq?("42"), "pad noop")

    // bytes (bind literal to a Str var first — bare literal receiver is pre-P5)
    Str AB = "AB"
    Vec(int) bs = AB.bytes()
    check(bs.len() == 2, "bytes len")
    check(bs.get!(0) == 65, "bytes 0")
    check(bs.get!(1) == 66, "bytes 1")

    // split
    Vec(Str) parts = s.split(dot)
    check(parts.len() == 4, "split len")
    check(parts.get!(0).eq?("a"), "split 0")
    check(parts.get!(3).eq?("b"), "split 3")

    Str csv = "a,b,"
    Str comma = ","
    Vec(Str) cp = csv.split(comma)
    check(cp.len() == 3, "split trailing")
    check(cp.get!(2).empty?(), "split trailing empty")

    // lines
    Str text = "one\ntwo\r\nthree"
    Vec(Str) ls = text.lines()
    check(ls.len() == 3, "lines len")
    check(ls.get!(0).eq?("one"), "lines 0")
    check(ls.get!(1).eq?("two"), "lines crlf")
    check(ls.get!(2).eq?("three"), "lines 2")

    print("STRM2 PASS")
}
