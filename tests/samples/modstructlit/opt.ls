module opt

import std.str

// options struct with field defaults, used cross-module via opt.PlotOpts{...}
struct PlotOpts {
    int w = 800
    int h = 600
    Str theme = "rainbow"
    bool grid = true
}

fn describe(PlotOpts o) -> Str {
    return f"{o.w}x{o.h}/{o.theme}/{o.grid}"
}
