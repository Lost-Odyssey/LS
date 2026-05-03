/* Step 5: &! param can be forwarded to a &string (read-only) param. */
fn show(&string s) {
    print(s)
}

fn wrap(&!string s) {
    show(s)   /* auto-reborrow &!T → &T → T */
    s += "?"
}

fn main() -> int {
    string x = "hi".upper()
    wrap(&!x)
    print(x)  /* expect: HI? */
    return 0
}
