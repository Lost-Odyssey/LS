// Phase 0 (borrow extension): returning a borrow `-> &T` must be rejected
// cleanly. Previously: checker accepted it and codegen emitted invalid IR
// ("ret type does not match"). Now: clean "borrows cannot escape via return".
struct Foo { int x }

fn f(&Foo a) -> &Foo { return a }

fn main() -> int {
    print(1)
    return 0
}
