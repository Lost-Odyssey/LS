/* Step 2: can't extract ownership via string t = s (s is writable borrow). */
fn f(&!string s) {
    string t = s
    print(t)
}

fn main() -> int {
    return 0
}
