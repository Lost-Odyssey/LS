// Phase 0 (borrow extension): a borrow struct field `&T` must be rejected.
// Previously silently accepted with zero safety checks → dangling landmine
// (the Ref can outlive its referent). Now: clean "struct fields cannot be
// borrows yet".
struct Foo { int x }
struct Holder { &Foo f }

def main() -> int {
    @print(1)
    return 0
}
