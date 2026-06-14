// Phase 0 (borrow extension): a borrow enum payload `&T` must be rejected.
// Same dangling landmine as a borrow field (the &T can outlive its referent).
// Now: clean "enum payloads cannot be borrows yet".
struct Foo { int x }
enum E { Wrap(&Foo), None }

fn main() -> int {
    print(1)
    return 0
}
