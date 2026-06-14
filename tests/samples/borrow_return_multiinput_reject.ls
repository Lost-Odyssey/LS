// Phase 2 (borrow extension): single-input lifetime elision requires exactly
// ONE borrow input. An `&self` method that ALSO takes another borrow parameter
// is ambiguous (which input's lifetime does the result inherit?) and must be
// rejected.
struct Foo { int x }
struct Bar { Foo f }

impl Bar {
    fn pick(&self, &Foo other) -> &Foo { return self.f }
}

fn main() -> int {
    print(1)
    return 0
}
