import std.vec

/* Phase 5: borrow does NOT move the caller's variable — later use
   (including an actual move) still works. */

fn peek(&string s) -> int {
    return s.length
}

fn main() -> int {
    Vec(string) names

    string alice = "Alice".upper()
    int n1 = peek(alice)     /* borrow */
    int n2 = peek(alice)     /* borrow again — still LIVE */
    print(n1)
    print("\n")
    print(n2)
    print("\n")

    /* Now the REAL move — after this, alice is moved. */
    names.push(alice)
    print(names.len())
    print("\n")
    return 0
}
