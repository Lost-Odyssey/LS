// B-4 cross-module struct literal + struct field defaults (options-struct pattern).
// Prints "MSL PASS" / "MSL FAIL: ...".
import opt
import std.core.str

def check(Str got, Str want, Str name) -> bool {
    if got.eq?(want) { return true }
    @print(f"MSL FAIL: {name} got=[{got}] want=[{want}]")
    return false
}

def main() {
    bool ok = true
    // all defaults (empty qualified literal)
    opt.PlotOpts a = opt.PlotOpts{}
    ok = check(opt.describe(a), "800x600/rainbow/true", "all_defaults") && ok
    // override some fields (qualified literal + defaults for the rest)
    opt.PlotOpts b = opt.PlotOpts{theme: "viridis", w: 1280}
    ok = check(opt.describe(b), "1280x600/viridis/true", "partial_override") && ok
    // all explicit
    opt.PlotOpts d = opt.PlotOpts{w: 1, h: 2, theme: "x", grid: false}
    ok = check(opt.describe(d), "1x2/x/false", "all_explicit") && ok
    if ok { @print("MSL PASS") }
}
