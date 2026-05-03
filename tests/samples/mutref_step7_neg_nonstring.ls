/* Step 7: &! only supports string (Phase 5.5 scope). */
fn bump(&!int n) {
    n = n + 1
}

fn main() -> int {
    int v = 10
    bump(&!v)
    return 0
}
