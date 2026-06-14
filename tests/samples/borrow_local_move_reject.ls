// Phase 1 (borrow extension): moving the referent of a live named local borrow
// must be rejected — otherwise the borrow dangles. `S b = a` moves the has_drop
// struct `a` while `r` borrows it.
import std.str
struct S { Str x }

fn main() -> int {
    S a = S{x: "hello"}
    &S r = &a
    S b = a              // move `a` while borrowed → must reject
    print(r.x.len())
    return 0
}
