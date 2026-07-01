module mod_b
import std.core.str

def helper() -> int {
    return 2
}

def read_file(Str s) -> Str {
    return f"b:{s}"
}

/* Bare intra-module call: must resolve to mod_b's own helper (2). */
def combined() -> int {
    return helper() + 20
}
