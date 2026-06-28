// vec_kid_lazy_test.ls -- KI-D: Vec(Pt) must not require Equal unless an
// equality-search method is actually called.

import std.core.vec
import std.core.str

struct Pt { Str tag; int v }

def check(bool c, Str l) { if c { @print(f"ok {l}") } else { @print(f"FAIL {l}") } }

def main() {
    Vec(Pt) ps = {}
    Pt a = Pt { tag: f"a", v: 10 }
    Pt b = Pt { tag: f"b", v: 20 }
    ps.push(__move(a))
    ps.push(__move(b))

    check(ps.len() == 2, "len")
    Pt got = ps.get!(1)
    check(got.tag.eq?("b") && got.v == 20, "get clone")

    @print("KID LAZY PASS")
}
