/* Step 4: two writable borrows of the same variable in one call. */
fn f(&!string a, &!string b) {
    print(a)
    print(b)
}

fn main() -> int {
    string x = "hi".upper()
    f(&!x, &!x)
    return 0
}
