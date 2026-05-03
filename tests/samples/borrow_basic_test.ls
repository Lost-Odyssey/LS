/* Phase 5: &string read-only borrow — basic positive cases. */

fn greet(&string name) {
    print("hello, ")
    print(name)
    print("\n")
}

fn length_of(&string s) -> int {
    return s.length
}

fn main() -> int {
    string alice = "Alice"
    string bob = "Bob".upper()

    greet(alice)      /* static string auto-borrow */
    greet(bob)        /* owned string auto-borrow */
    greet(alice)      /* reuse after borrow — alice still LIVE */

    int la = length_of(alice)
    int lb = length_of(bob)
    print(la)
    print("\n")
    print(lb)
    print("\n")

    /* owned value is usable after multiple borrows */
    print(bob)
    print("\n")

    return 0
}
