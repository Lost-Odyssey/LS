/* Phase 5: string methods on borrowed values return fresh owned strings. */

fn up_then_print(&string s) {
    string u = s.upper()    /* s is borrow, result is new owned */
    print(u)
    print("\n")
}

fn check(&string s) -> bool {
    return s.contains("l")
}

fn main() -> int {
    string owned = "hello"
    up_then_print(owned)
    up_then_print("world")
    bool b1 = check(owned)
    bool b2 = check("xyz")
    print(b1)
    print("\n")
    print(b2)
    print("\n")
    return 0
}
