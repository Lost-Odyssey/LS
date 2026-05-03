/* Step 5: two distinct variables via two &! params. */
fn swap_append(&!string a, &!string b) {
    a += "-A"
    b += "-B"
}

fn main() -> int {
    string x = "one".upper()
    string y = "two".upper()
    swap_append(&!x, &!y)
    print(x)  /* expect: ONE-A */
    print(y)  /* expect: TWO-B */
    return 0
}
