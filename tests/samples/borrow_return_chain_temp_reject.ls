// Phase 2 (borrow extension): a transitively-chained borrow return is only
// sound when the inner call's receiver is itself rooted at the borrow input.
// Chaining through a TEMPORARY (`mkmid().get()`) borrows a value dropped at
// statement end → must be rejected (it would dangle).
struct Leaf { int v }
struct Mid { Leaf leaf }
struct Top { Mid mid }

impl Mid { fn get(&self) -> &Leaf { return self.leaf } }
fn mkmid() -> Mid { return Mid{leaf: Leaf{v: 1}} }

impl Top {
    fn bad(&self) -> &Leaf { return mkmid().get() }   // chain via temporary → reject
}

fn main() -> int {
    print(1)
    return 0
}
