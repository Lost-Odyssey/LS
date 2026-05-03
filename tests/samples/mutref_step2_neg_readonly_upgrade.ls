/* Step 2: read-only borrow cannot be taken as &! inside a &string fn. */
fn outer(&string r) {
    /* even inside the function body, we cannot upgrade r to &! */
    inner(&!r)
}

fn inner(&!string s) {
    print(s)
}

fn main() -> int {
    return 0
}
