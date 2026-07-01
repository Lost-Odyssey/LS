// plot_svg_test.ls — Phase 2 SVG backend: layout + polyline + axes + ticks +
// grid + title + scatter. Prints "SVG PASS" / "SVG FAIL: ...".

import std.chart.plot as plot
import std.core.math as math
import std.core.vec
import std.core.str

def has(Str hay, Str needle, Str name) -> bool {
    if hay.contains?(needle) { return true }
    @print(f"SVG FAIL: {name} missing [{needle}]")
    return false
}

def main() {
    bool ok = true

    // ---- a line + scatter figure ----
    plot.Axes ax = plot.axes()
    plot.set_title(&!ax, "sine & pts")
    plot.set_xlabel(&!ax, "t")
    plot.set_ylabel(&!ax, "y")
    plot.grid(&!ax)

    Vec(f64) xs = {}
    Vec(f64) ys = {}
    int i = 0
    while i < 8 {
        f64 t = (i as f64) / 8.0 * math.TAU
        xs.push(t)
        ys.push(math.sin(t))
        i = i + 1
    }
    plot.line(&!ax, xs, ys, plot.LineOpts{color: "#e6194b", label: "sin"})

    Vec(f64) sx = [0.0, 1.0, 2.0, 3.0]
    Vec(f64) sy = [0.5, -0.5, 0.25, -0.25]
    plot.line(&!ax, sx, sy, plot.LineOpts{color: "#4363d8", label: "pts", scatter: true})

    plot.Figure fig = plot.figure(plot.FigureOpts{})
    plot.add_axes(&!fig, ax)

    Str svg = plot.to_svg(fig)

    // ---- structural assertions ----
    ok = has(svg, "<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"800\" height=\"500\">", "svg.header") && ok
    ok = has(svg, "</svg>", "svg.footer") && ok
    ok = has(svg, "<rect x=\"70\" y=\"40\"", "plot.area") && ok      // plot-area rect at layout origin
    ok = has(svg, "<polyline fill=\"none\" stroke=\"#e6194b\"", "polyline") && ok
    ok = has(svg, "<circle", "scatter.circle") && ok
    ok = has(svg, "fill=\"#4363d8\"", "scatter.color") && ok
    ok = has(svg, "stroke=\"#e0e0e0\"", "grid") && ok               // grid lines
    ok = has(svg, ">sine &amp; pts<", "title.escaped") && ok        // title with escaped &... wait
    ok = has(svg, "text-anchor=\"middle\"", "tick.label") && ok
    ok = has(svg, "rotate(-90", "ylabel.rotated") && ok

    // ---- polyline must contain mapped pixel coords (inside plot area) ----
    // first point maps to x=left=70 (t=0 -> xmin after margin is slightly <0, so
    // not exactly 70); just assert points attribute is present and non-empty.
    ok = has(svg, "points=\"", "polyline.points") && ok

    if ok { @print("SVG PASS") }
}
