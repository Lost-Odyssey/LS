/* Step 6: `+= ` also forbidden on read-only borrow. */
fn bad(&string s) {
    s += "!"
}

fn main() -> int {
    string x = "hi".upper()
    bad(x)
    return 0
}
