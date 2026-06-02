// std/plot.ls — Data-visualization framework (Phase 1 skeleton).
//
// Compilable rewrite of docs/plan_plot.md adapted to LS's current feature set:
//   * no function overloading  -> distinct names (plot / plot_xy / plot_styled)
//   * no borrow-returning fns   -> Axes is built as a value, then add_axes()
//                                  MOVES it into the Figure
//   * no tuples                 -> small structs / out-params
//   * no `elif` / `=>`          -> standard `else if {}`
//   * `math.` prefix on intrinsics; formatting via std/plotfmt
//
// Phase 1 provides the data model + builder API + a textual summary output so
// the whole pipeline compiles, runs, and is memcheck-clean. Phase 2 fills the
// tick engine; Phase 3+ the real SVG / Text renderers.

import math
import plotfmt

// ---- Data model ----

struct LineStyle {
    vec(f64) xs
    vec(f64) ys
    string color
    string label
    f64 linewidth
    bool is_scatter
}

struct BarSeries {
    vec(string) labels
    vec(f64) values
    string color
    string label
}

struct Axes {
    vec(LineStyle) lines
    vec(BarSeries) bars
    string title
    string xlabel
    string ylabel
    bool auto_scale
    f64 xmin
    f64 xmax
    f64 ymin
    f64 ymax
    vec(f64) xticks
    vec(f64) yticks
    bool legend_visible
    bool grid_visible
}

struct Figure {
    vec(Axes) axes_list
    int rows
    int cols
    int svg_width
    int svg_height
    int text_width
    int text_height
    string background
}

// ---- Color palette (replaces `const COLORS`) ----

fn color_at(int i) -> string {
    int k = i % 10
    if k == 0 { return "#4363d8" }      // blue
    else if k == 1 { return "#e6194b" } // red
    else if k == 2 { return "#3cb44b" } // green
    else if k == 3 { return "#f032e6" } // magenta
    else if k == 4 { return "#f58231" } // orange
    else if k == 5 { return "#42d4f4" } // cyan
    else if k == 6 { return "#ffe119" } // yellow
    else if k == 7 { return "#911eb4" } // purple
    else if k == 8 { return "#469990" } // teal
    return "#fabebe"                    // pink
}

// ---- Constructors ----

fn figure(int svg_w, int svg_h, int text_w, int text_h) -> Figure {
    vec(Axes) al = []
    return Figure {
        axes_list: al,
        rows: 1, cols: 1,
        svg_width: svg_w, svg_height: svg_h,
        text_width: text_w, text_height: text_h,
        background: "#ffffff"
    }
}

fn axes() -> Axes {
    vec(LineStyle) ls = []
    vec(BarSeries) bs = []
    vec(f64) xt = []
    vec(f64) yt = []
    return Axes {
        lines: ls, bars: bs,
        title: "", xlabel: "", ylabel: "",
        auto_scale: true,
        xmin: 0.0, xmax: 1.0, ymin: 0.0, ymax: 1.0,
        xticks: xt, yticks: yt,
        legend_visible: false, grid_visible: false
    }
}

// ---- Internal helpers ----

fn _range(int n) -> vec(f64) {
    vec(f64) xs = []
    int i = 0
    while i < n {
        xs.push(i as f64)
        i = i + 1
    }
    return xs
}

// ---- Tick engine (standard Heckbert "nice numbers") ----
//
// NOTE: docs/plan_plot.md §4.2 swapped the round/ceil branch bodies, making its
// example table internally inconsistent. This implements the textbook Heckbert
// algorithm: `range = nice(span, ceil)`, then `step = nice(range/(n-1), round)`.

fn _nice_number(f64 v, bool round) -> f64 {
    if v == 0.0 { return 0.0 }
    f64 sign = 1.0
    f64 x = v
    if x < 0.0 { sign = -1.0; x = 0.0 - x }
    f64 expo = math.floor(math.log10(x))
    f64 frac = x / math.pow(10.0, expo)   // normalized to [1, 10)
    f64 nice = 10.0
    if round {
        if frac < 1.5 { nice = 1.0 }
        else if frac < 3.0 { nice = 2.0 }
        else if frac < 7.0 { nice = 5.0 }
        else { nice = 10.0 }
    } else {
        if frac <= 1.0 { nice = 1.0 }
        else if frac <= 2.0 { nice = 2.0 }
        else if frac <= 5.0 { nice = 5.0 }
        else { nice = 10.0 }
    }
    return sign * nice * math.pow(10.0, expo)
}

fn generate_ticks(f64 lo, f64 hi, int max_ticks) -> vec(f64) {
    vec(f64) ticks = []
    if max_ticks < 2 { ticks.push(lo); return ticks }
    f64 span = _nice_number(hi - lo, false)
    f64 step = _nice_number(span / ((max_ticks - 1) as f64), true)
    if step == 0.0 { ticks.push(0.0); return ticks }
    f64 start = math.ceil(lo / step) * step
    f64 stop = math.floor(hi / step) * step
    f64 v = start
    while v <= stop + step * 0.0001 {
        if v == 0.0 { ticks.push(0.0) }   // normalize -0.0 -> 0.0
        else { ticks.push(v) }
        v = v + step
    }
    return ticks
}

// ---- Coordinate mapping (data -> SVG pixels) ----

fn map_x(f64 x, f64 xmin, f64 xmax, int left, int width) -> f64 {
    f64 denom = xmax - xmin
    if denom == 0.0 { denom = 1.0 }
    return (left as f64) + (x - xmin) / denom * (width as f64)
}

fn map_y(f64 y, f64 ymin, f64 ymax, int top, int height) -> f64 {
    // SVG y grows downward, data y grows upward -> flip
    f64 denom = ymax - ymin
    if denom == 0.0 { denom = 1.0 }
    return (top as f64) + (height as f64) - (y - ymin) / denom * (height as f64)
}

// ---- Auto-scale: compute data limits, margins, and ticks ----

fn update_limits(&!Axes ax) {
    bool first = true
    f64 xmn = 0.0
    f64 xmx = 1.0
    f64 ymn = 0.0
    f64 ymx = 1.0
    int li = 0
    while li < ax.lines.length {
        LineStyle ln = ax.lines[li]
        int n = math.min(ln.xs.length, ln.ys.length)
        int k = 0
        while k < n {
            f64 xv = ln.xs[k]
            f64 yv = ln.ys[k]
            if first {
                xmn = xv; xmx = xv; ymn = yv; ymx = yv
                first = false
            } else {
                xmn = math.min(xmn, xv)
                xmx = math.max(xmx, xv)
                ymn = math.min(ymn, yv)
                ymx = math.max(ymx, yv)
            }
            k = k + 1
        }
        li = li + 1
    }
    if first {
        // no data -> default unit box
        xmn = 0.0; xmx = 1.0; ymn = 0.0; ymx = 1.0
    }
    ax.xmin = xmn
    ax.xmax = xmx
    ax.ymin = ymn
    ax.ymax = ymx
}

// finalize: apply 5% margins (when auto) and regenerate ticks. Call after all
// data has been added, before rendering.
fn finalize(&!Axes ax) {
    if ax.auto_scale {
        update_limits(&!ax)
        f64 dx = (ax.xmax - ax.xmin) * 0.05
        f64 dy = (ax.ymax - ax.ymin) * 0.05
        if dx == 0.0 { dx = 1.0 }
        if dy == 0.0 { dy = 1.0 }
        ax.xmin = ax.xmin - dx
        ax.xmax = ax.xmax + dx
        ax.ymin = ax.ymin - dy
        ax.ymax = ax.ymax + dy
    }
    ax.xticks = generate_ticks(ax.xmin, ax.xmax, 6)
    ax.yticks = generate_ticks(ax.ymin, ax.ymax, 6)
}

// ---- Data-adding builders (mutable borrow &!Axes) ----

fn plot(&!Axes ax, vec(f64) ys) {
    vec(f64) xs = _range(ys.length)
    string c = color_at(ax.lines.length)
    ax.lines.push(LineStyle {
        xs: xs, ys: ys, color: c, label: "", linewidth: 2.0, is_scatter: false
    })
}

fn plot_xy(&!Axes ax, vec(f64) xs, vec(f64) ys) {
    string c = color_at(ax.lines.length)
    ax.lines.push(LineStyle {
        xs: xs, ys: ys, color: c, label: "", linewidth: 2.0, is_scatter: false
    })
}

fn plot_styled(&!Axes ax, vec(f64) xs, vec(f64) ys, string color, string label) {
    ax.lines.push(LineStyle {
        xs: xs, ys: ys, color: color, label: label, linewidth: 2.0, is_scatter: false
    })
}

fn scatter(&!Axes ax, vec(f64) xs, vec(f64) ys, string color, string label) {
    ax.lines.push(LineStyle {
        xs: xs, ys: ys, color: color, label: label, linewidth: 0.0, is_scatter: true
    })
}

fn bar(&!Axes ax, vec(string) labels, vec(f64) values, string color, string label) {
    ax.bars.push(BarSeries {
        labels: labels, values: values, color: color, label: label
    })
}

// ---- Style controls (mutable borrow &!Axes) ----

fn set_title(&!Axes ax, string t) { ax.title = t }
fn set_xlabel(&!Axes ax, string t) { ax.xlabel = t }
fn set_ylabel(&!Axes ax, string t) { ax.ylabel = t }

fn set_xlim(&!Axes ax, f64 lo, f64 hi) {
    ax.auto_scale = false
    ax.xmin = lo
    ax.xmax = hi
}

fn set_ylim(&!Axes ax, f64 lo, f64 hi) {
    ax.auto_scale = false
    ax.ymin = lo
    ax.ymax = hi
}

fn legend(&!Axes ax) { ax.legend_visible = true }
fn grid(&!Axes ax) { ax.grid_visible = true }

// ---- Figure assembly: MOVE an Axes into the Figure ----

fn add_axes(&!Figure fig, Axes ax) {
    fig.axes_list.push(ax)
}

// ---- Output (Phase 1 stubs: textual summary; Phase 2+ adds tick engine,
//      real SVG and Text/Unicode renderers) ----

fn show_text(&Figure fig) -> string {
    string s = f"Figure {fig.svg_width}x{fig.svg_height} axes={fig.axes_list.length}\n"
    int i = 0
    while i < fig.axes_list.length {
        Axes ax = fig.axes_list[i]
        s = s + f"  axes[{i}] '{ax.title}': lines={ax.lines.length} bars={ax.bars.length}"
        if ax.grid_visible { s = s + " grid" }
        if ax.legend_visible { s = s + " legend" }
        s = s + "\n"
        i = i + 1
    }
    return s
}

fn print_text(&Figure fig) {
    print(show_text(fig))
}

// to_svg: Phase 1 placeholder (well-formed but minimal). Phase 2 replaces this
// with the real layout + tick + polyline renderer.
fn to_svg(&Figure fig) -> string {
    string head = f"<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"{fig.svg_width}\" height=\"{fig.svg_height}\">"
    string body = f"<rect width=\"100%\" height=\"100%\" fill=\"{fig.background}\"/>"
    string foot = "</svg>"
    return head + body + foot
}
