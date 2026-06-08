import std.vec

/* Step 2: &!string can't be moved into a vec (checker must reject). */
fn f(&!string s) {
    Vec(string) v
    v.push(s)
}

fn main() -> int {
    return 0
}
