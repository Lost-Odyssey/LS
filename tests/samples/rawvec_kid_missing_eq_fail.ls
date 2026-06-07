// rawvec_kid_missing_eq_fail.ls -- KI-D negative case: calling an equality
// search method on RawVec(Pt) must report the method-level where bound.

import std.rawvec

struct Pt { string tag; int v }

fn main() {
    RawVec(Pt) ps = {}
    Pt a = Pt { tag: f"a", v: 10 }
    ps.push(__move(a))

    Pt needle = Pt { tag: f"a", v: 10 }
    bool ok = ps.contains(needle)
    print(ok)
}
