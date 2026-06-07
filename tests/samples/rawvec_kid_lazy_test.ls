// rawvec_kid_lazy_test.ls -- KI-D: Vec(Pt) must not require Eq unless an
// equality-search method is actually called.

import std.vec

struct Pt { string tag; int v }

fn check(bool c, string l) { if c { print(f"ok {l}") } else { print(f"FAIL {l}") } }

fn main() {
    Vec(Pt) ps = {}
    Pt a = Pt { tag: f"a", v: 10 }
    Pt b = Pt { tag: f"b", v: 20 }
    ps.push(__move(a))
    ps.push(__move(b))

    check(ps.len() == 2, "len")
    Pt got = ps.get(1)
    check(got.tag == "b" && got.v == 20, "get clone")

    print("KID LAZY PASS")
}
