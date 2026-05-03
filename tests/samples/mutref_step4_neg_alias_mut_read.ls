/* Step 4: writable + read-only borrow of same variable. */
fn f(&!string a, &string b) {
    print(a)
    print(b)
}

fn main() -> int {
    string x = "hi".upper()
    f(&!x, x)
    return 0
}
