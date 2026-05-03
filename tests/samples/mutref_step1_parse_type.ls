/* Step 1 smoke: &!string parses as parameter type. The function is never
   called, so codegen's "not implemented" stub is not triggered. */

fn f(&!string s) -> int {
    return s.length
}

fn main() -> int {
    return 0
}
