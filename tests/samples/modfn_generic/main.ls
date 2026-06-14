import defmod
import std.c as c
fn fail(Str m) { print(m); c.abort() }
fn main() {
    PBox(int) b = PBox(int){ item: 7 }
    Str s = b.label()
    if (!s.eq?("ff")) { fail("FAIL label") }       // 0xff
    Str o = b.label2()
    if (!o.eq?("10")) { fail("FAIL label2") }       // 8 in octal = "10"
    print("MODFN2 PASS")
}
