/* Step 7: .append(int) via mut borrow — int treated as single byte. */
fn tag(&!string s) {
    s.append(65)    /* 'A' */
    s.append(66)    /* 'B' */
    s += "!"
}

fn main() -> int {
    string x = "x".upper()
    tag(&!x)
    print(x)  /* expect: XAB! */
    return 0
}
