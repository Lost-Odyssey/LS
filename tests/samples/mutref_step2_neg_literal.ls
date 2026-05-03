/* Step 2: &! on a non-ident expression is rejected. */
fn f(&!string s) {
    print(s)
}

fn main() -> int {
    f(&!"literal")
    return 0
}
