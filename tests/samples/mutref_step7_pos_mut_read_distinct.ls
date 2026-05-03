/* Step 7: &!x + &y for DISTINCT vars in the same call — allowed. */
fn f(&!string a, &string b) {
    a += " + "
    a += b
}

fn main() -> int {
    string x = "hi".upper()
    string y = "world".upper()
    f(&!x, y)
    print(x)  /* expect: HI + WORLD */
    print(y)  /* expect: WORLD (untouched) */
    return 0
}
