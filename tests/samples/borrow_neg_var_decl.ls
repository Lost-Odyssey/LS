/* Phase 5 negative: borrow types in variable declarations — should be rejected
   or at least not silently "leak" a reference type into local scope. */

fn main() -> int {
    string owned = "hi"
    &string bad = owned
    return 0
}
