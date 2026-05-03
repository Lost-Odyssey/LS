/* Step 2: calling a &!string fn without explicit &! is rejected. */
fn f(&!string s) {
    print(s)
}

fn main() -> int {
    string x = "hi".upper()
    f(x)        /* must be &!x */
    return 0
}
