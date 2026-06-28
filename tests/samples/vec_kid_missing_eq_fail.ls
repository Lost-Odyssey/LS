// vec_kid_missing_eq_fail.ls -- KI-D negative case: calling an equality
// search method on Vec(Pt) must report the method-level where bound.

import std.core.vec
import std.core.str

struct Pt { Str tag; int v }

def main() {
    Vec(Pt) ps = {}
    Pt a = Pt { tag: f"a", v: 10 }
    ps.push(__move(a))

    Pt needle = Pt { tag: f"a", v: 10 }
    bool ok = ps.has?(needle)
    @print(ok)
}
