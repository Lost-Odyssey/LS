// Phase 0 (borrow extension): a method returning a borrow of a field
// (`fn(&self) -> &T { return self.field }`) must be rejected cleanly. This is
// the Phase 2 target (single-input lifetime elision) — until then it is a
// latent IR crash and must be a clean "borrows cannot escape via return".
struct Inner { int v }
struct Outer { Inner inner }

impl Outer {
    fn get(&self) -> &Inner { return self.inner }
}

fn main() -> int {
    print(1)
    return 0
}
