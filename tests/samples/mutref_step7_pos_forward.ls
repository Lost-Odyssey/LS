/* Step 7: forward &!string through a call chain — the pointer passes through. */
fn inner(&!string s) {
    s += "<"
    s.append(">")
}

fn outer(&!string s) {
    s += "["
    inner(&!s)
    s += "]"
}

fn main() -> int {
    string x = "hi".upper()
    outer(&!x)
    print(x)  /* expect: HI[<>] */
    return 0
}
