module opt

// options struct with field defaults, used cross-module via opt.PlotOpts{...}
struct PlotOpts {
    int w = 800
    int h = 600
    string theme = "rainbow"
    bool grid = true
}

fn describe(PlotOpts o) -> string {
    return f"{o.w}x{o.h}/{o.theme}/{o.grid}"
}
