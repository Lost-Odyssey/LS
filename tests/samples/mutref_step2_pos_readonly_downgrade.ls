/* Step 2: &!x can be passed where &string is expected (mut → readonly downgrade).
   Should type-check fine. Codegen will bail on &! (Step 5 stub). */
fn readonly(&string r) -> int {
    return r.length
}

fn mutable(&!string m) -> int {
    return readonly(m)    /* &!string -> &string OK */
}

fn main() -> int {
    return 0
}
