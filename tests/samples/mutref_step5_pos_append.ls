/* Step 5: &! param — in-place append (+=) visible to caller. */
fn append_bang(&!string s) {
    s += "!"
}

fn main() -> int {
    string x = "hi".upper()
    append_bang(&!x)
    print(x)  /* expect: HI! */
    return 0
}
