/* Step 4: distinct variables for two writable borrows — OK. */
fn f(&!string a, &!string b) {
    print(a)
    print(b)
}

fn main() -> int {
    string x = "hi".upper()
    string y = "world".upper()
    f(&!x, &!y)
    return 0
}
