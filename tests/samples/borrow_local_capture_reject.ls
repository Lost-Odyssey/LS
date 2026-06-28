// Phase 1 (borrow extension): capturing the referent of a live local borrow
// by-move into a closure must be rejected — the env would outlive / drop the
// referent out from under the borrow (a latent use-after-free that .len() alone
// would not surface). `|| { return a.x.len() }` captures the has_drop `a` while
// `r` borrows it.
import std.core.str
struct S { Str x }
def use_it(Block()->int f) { @print(f()) }

def main() -> int {
    S a = S{x: "hello"}
    &S r = &a
    use_it(|| { return a.x.len() })   // by-move capture of borrowed `a` → must reject
    @print(r.x.len())
    return 0
}
