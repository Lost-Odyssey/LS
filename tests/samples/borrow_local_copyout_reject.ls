// Phase 1 (borrow extension): copying the value out of a struct borrow into a
// new owned variable must be rejected (it would alias / double-free the
// referent's heap). `S b = r` copies out of the borrow `r`.
import std.core.str
struct S { Str x }

def main() -> int {
    S a = S{x: "hello"}
    &S r = &a
    S b = r              // copy-out of struct borrow → must reject
    @print(b.x.len())
    return 0
}
