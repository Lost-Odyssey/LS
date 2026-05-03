/* Phase 5: borrow can be re-borrowed (borrow → borrow). */

fn inner(&string s) -> int {
    return s.length
}

fn outer(&string s) -> int {
    return inner(s)
}

fn main() -> int {
    string owned = "hello".upper()
    int n1 = outer(owned)
    int n2 = outer("literal")
    print(owned)
    print("\n")
    print(n1)
    print("\n")
    print(n2)
    print("\n")
    return 0
}
