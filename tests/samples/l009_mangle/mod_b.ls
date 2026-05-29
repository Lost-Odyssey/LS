module mod_b

fn helper() -> int {
    return 2
}

fn read_file(string s) -> string {
    return f"b:{s}"
}

/* Bare intra-module call: must resolve to mod_b's own helper (2). */
fn combined() -> int {
    return helper() + 20
}
