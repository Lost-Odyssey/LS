// Phase 2 (borrow extension): when a returned borrow is bound to a local
// (`&Inner r = o.get()`), the call's receiver `o` is its provenance and must
// outlive the borrow. Moving `o` while `r` is alive must be rejected (it would
// dangle r). Uses a has_drop (Str) field so the move is a real ownership move.
import std.core.str
struct Inner { Str v }
struct Outer { Inner inner }

methods Outer {
    def get(&self) -> &Inner { return self.inner }
}

def main() -> int {
    Outer o = Outer{inner: Inner{v: "hi"}}
    &Inner r = o.get()
    Outer o2 = o              // move the pinned receiver → must reject
    @print(r.v.len())
    return 0
}
