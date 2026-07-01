module mod_a
import std.core.str

/* Same function name as mod_b.helper — L-009 must keep them distinct. */
def helper() -> int {
    return 1
}

/* Same name as the user's local read_file and mod_b's — distinct symbols. */
def read_file(Str s) -> Str {
    return f"a:{s}"
}

/* Bare intra-module call: must resolve to mod_a's own helper (1), not mod_b's. */
def combined() -> int {
    return helper() + 10
}
