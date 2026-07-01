// plot_skeleton_test.ls — Phase 1 skeleton acceptance for std/plot.ls.
// Builds a Figure via the builder API and verifies the textual summary.
// Prints "PLOT PASS" on success, "PLOT FAIL: ..." on mismatch.

import std.chart.plot as plot
import std.core.math as math
import std.core.vec
import std.core.str

def check(Str got, Str want, Str name) -> bool {
    if got.eq?(want) { return true }
    @print(f"PLOT FAIL: {name} got=[{got}] want=[{want}]")
    return false
}

def main() {
    bool ok = true

    // ---- build a sine + cosine line plot ----
    plot.Axes ax1 = plot.axes()
    plot.set_title(&!ax1, "trig")
    plot.set_ylabel(&!ax1, "amp")
    plot.grid(&!ax1)
    plot.legend(&!ax1)

    Vec(f64) xs = {}
    Vec(f64) ys_sin = {}
    Vec(f64) ys_cos = {}
    int i = 0
    while i < 16 {
        f64 t = (i as f64) / 16.0 * math.TAU
        xs.push(t)
        ys_sin.push(math.sin(t))
        ys_cos.push(math.cos(t))
        i = i + 1
    }
    plot.line(&!ax1, xs, ys_sin, plot.LineOpts{color: "#e6194b", label: "sin"})
    // second series: auto-x, auto-color
    plot.plot(&!ax1, ys_cos)

    // ---- a bar chart on a second axes ----
    plot.Axes ax2 = plot.axes()
    plot.set_title(&!ax2, "counts")
    Vec(Str) labels = ["a", "b", "c"]
    Vec(f64) vals = [3.0, 7.0, 5.0]
    plot.bar(&!ax2, labels, vals, plot.BarOpts{color: "#4363d8", label: "n"})

    // ---- assemble figure (MOVE axes in) ----
    plot.Figure fig = plot.figure(plot.FigureOpts{})
    plot.add_axes(&!fig, ax1)
    plot.add_axes(&!fig, ax2)

    // ---- verify summary ----
    Str summary = plot.show_text(fig)
    Str want = "Figure 800x500 axes=2\n  axes[0] 'trig': lines=2 bars=0 grid legend\n  axes[1] 'counts': lines=0 bars=1\n"
    ok = check(summary, want, "summary") && ok

    // ---- to_svg placeholder is well-formed ----
    Str svg = plot.to_svg(fig)
    bool svg_ok = svg.starts_with?("<svg") && svg.ends_with?("</svg>")
    if !svg_ok { @print(f"PLOT FAIL: svg malformed: {svg}"); ok = false }

    if ok { @print("PLOT PASS") }
}
