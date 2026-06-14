// Phase 0 (borrow extension): a borrow type `&T` cannot be a generic type
// argument — it would let a borrow be stored inside a container / Option and
// outlive its referent. Now: clean "a borrow type cannot be a generic type
// argument". (Covers Vec(&T) too via the struct-template instantiation path.)
struct Foo { int x }

fn main() -> int {
    Vec(&Foo) v = []
    print(1)
    return 0
}
