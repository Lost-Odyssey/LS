// A-2 (docs/bugs_deferred_p5_4.md §2): explicitly calling __drop() is rejected.
// The compiler destroys values automatically at scope exit; an explicit call is
// a double-free footgun (and for a compiler-generated member __drop the symbol
// may not be emitted → JIT "Symbols not found"). Must be a clean checker error.

struct Inner {
    Str data;
}
struct Outer {
    Inner inner;
}
methods Outer {
}

methods Outer: Destroy {
    def ~(&!self) {
        @print("Outer.__drop called")
        self.inner.__drop()
    }
}
def main() -> int {
    Outer o
    o.inner.data = "test"
    @print(o.inner.data)
    return 0
}
