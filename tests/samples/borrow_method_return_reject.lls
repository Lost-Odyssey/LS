// Phase 2 (borrow extension): single-input lifetime elision lets an `&self`
// method return a borrow DERIVED FROM self (`return self.field`). But returning
// a borrow of a LOCAL must still be rejected — it would dangle once the method
// returns. This is the core escape-analysis negative.
struct Inner { int v }
struct Outer { Inner inner }

methods Outer {
    def bad(&self) -> &Inner {
        Inner local = Inner{v: 9}
        return &local            // borrow of a local → must reject (dangling)
    }
}

def main() -> int {
    Outer o = Outer{inner: Inner{v: 1}}
    @print(o.bad().v)
    return 0
}
