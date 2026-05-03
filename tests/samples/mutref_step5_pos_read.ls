/* Step 5: &! param — read is OK (mut borrow supports read, and writes too). */
fn show(&!string s) {
    print(s)
}

fn main() -> int {
    string x = "hello".upper()
    show(&!x)
    print(x)
    return 0
}
