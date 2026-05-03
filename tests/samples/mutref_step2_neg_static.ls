/* Step 2: &! on a static-string variable is rejected. */
fn f(&!string s) {
    print(s)
}

fn main() -> int {
    string x = "hi"   /* static — no .upper() / format call */
    f(&!x)
    return 0
}
