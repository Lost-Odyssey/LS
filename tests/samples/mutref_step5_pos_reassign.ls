/* Step 5: &! param — full reassignment visible to caller. */
fn overwrite(&!string s) {
    s = "world".upper()
}

fn main() -> int {
    string x = "hi".upper()
    overwrite(&!x)
    print(x)  /* expect: WORLD */
    return 0
}
