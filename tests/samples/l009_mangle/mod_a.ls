module mod_a

/* Same function name as mod_b.helper — L-009 must keep them distinct. */
fn helper() -> int {
    return 1
}

/* Same name as the user's local read_file and mod_b's — distinct symbols. */
fn read_file(string s) -> string {
    return f"a:{s}"
}

/* Bare intra-module call: must resolve to mod_a's own helper (1), not mod_b's. */
fn combined() -> int {
    return helper() + 10
}
