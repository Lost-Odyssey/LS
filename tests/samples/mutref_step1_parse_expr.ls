/* Step 1 smoke: &!ident parses as AST_MUT_BORROW. Codegen should reject
   with "not implemented" in this step since Step 5 isn't done yet. */

fn take(&!string s) -> int {
    return s.length
}

fn main() -> int {
    string s = "hi".upper()
    int n = take(&!s)
    return n
}
