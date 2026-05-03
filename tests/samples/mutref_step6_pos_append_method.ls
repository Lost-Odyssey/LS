/* Step 6: string.append through a writable borrow. */
fn grow(&!string s) {
    s.append(" world")
    s.append('!')
}

fn main() -> int {
    string x = "hello".upper()
    grow(&!x)
    print(x)  /* expect: HELLO world! */
    return 0
}
