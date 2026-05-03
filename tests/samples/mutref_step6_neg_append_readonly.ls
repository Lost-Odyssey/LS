/* Step 6: .append() forbidden on read-only borrow. */
fn bad(&string s) {
    s.append("!")
}

fn main() -> int {
    string x = "hi".upper()
    bad(x)
    return 0
}
