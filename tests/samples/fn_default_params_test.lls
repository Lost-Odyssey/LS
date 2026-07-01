// fn_default_params_test.ls — function positional default parameters (档1).
// Prints "FNDEF PASS" / "FNDEF FAIL: ...".

import std.core.str

struct Opts {
    int w = 800
    Str theme = "rainbow"
    bool grid = true
}

def check(Str got, Str want, Str name) -> bool {
    if got.eq?(want) { return true }
    @print(f"FNDEF FAIL: {name} got=[{got}] want=[{want}]")
    return false
}

// literal defaults (Str / int / bool)
def label(Str base, Str sep = "-", int n = 1, bool up = false) -> Str {
    Str s = base
    int i = 0
    while i < n { s = s + sep; i = i + 1 }
    if up { s = s + "U" }
    return s
}

// struct param default (options-struct pattern: opts = Opts{})
def render(Str title, Opts o = Opts{}) -> Str {
    return f"{title}/{o.w}/{o.theme}/{o.grid}"
}

def main() {
    bool ok = true

    // all defaults
    ok = check(label("x"), "x-", "all_defaults") && ok
    // override one (sep)
    ok = check(label("x", "+"), "x+", "override_1") && ok
    // override two
    ok = check(label("x", "+", 3), "x+++", "override_2") && ok
    // override all
    ok = check(label("x", ".", 2, true), "x..U", "override_all") && ok

    // struct param default
    ok = check(render("a"), "a/800/rainbow/true", "struct_default") && ok
    ok = check(render("b", Opts{theme: "viridis"}), "b/800/viridis/true", "struct_override") && ok
    ok = check(render("c", Opts{w: 1, grid: false}), "c/1/rainbow/false", "struct_override2") && ok

    if ok { @print("FNDEF PASS") }
}
