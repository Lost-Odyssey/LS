// Phase 2 (borrow extension): single-input lifetime elision requires exactly
// one BORROW input. A function whose only parameter is owned by-value (no borrow
// input at all) cannot return a borrow — there is no input lifetime to inherit,
// so `-> &T` must be rejected. (A free fn with one `&T` param IS allowed; see
// test_borrow_return.)
struct Foo { int x }

fn g(Foo a) -> &Foo { return a }

fn main() -> int {
    print(1)
    return 0
}
