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

import std.vec
import math
import plotfmt
import std.str

// ---- Data model ----

struct LineStyle {
    Vec(f64) xs
    Vec(f64) ys
    string color
    string label
    f64 linewidth
    bool is_scatter
}

struct BarSeries {
    Vec(string) labels
    Vec(f64) values
    string color
    string label
}

struct Axes {
    Vec(LineStyle) lines
    Vec(BarSeries) bars
    string title
    string xlabel
    string ylabel
    bool auto_scale
    f64 xmin
    f64 xmax
    f64 ymin
    f64 ymax
    Vec(f64) xticks
    Vec(f64) yticks
    bool legend_visible
    bool grid_visible
}

struct Figure {
    Vec(Axes) axes_list
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

// ---- Options structs (field defaults + partial init) ----

struct FigureOpts {
    int svg_w = 800
    int svg_h = 500
    int text_w = 70
    int text_h = 20
    string background = "#ffffff"
}

// LineOpts: color "" means auto-assign from the palette by series index.
struct LineOpts {
    string color = ""
    string label = ""
    f64 width = 2.0
    bool scatter = false
}

struct BarOpts {
    string color = "#4363d8"
    string label = ""
}

// ---- Constructors ----

fn figure(FigureOpts opts) -> Figure {
    Vec(Axes) al = {}
    return Figure {
        axes_list: al,
        rows: 1, cols: 1,
        svg_width: opts.svg_w, svg_height: opts.svg_h,
        text_width: opts.text_w, text_height: opts.text_h,
        background: opts.background
    }
}

fn axes() -> Axes {
    Vec(LineStyle) ls = {}
    Vec(BarSeries) bs = {}
    Vec(f64) xt = {}
    Vec(f64) yt = {}
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

fn _range(int n) -> Vec(f64) {
    Vec(f64) xs = {}
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

fn generate_ticks(f64 lo, f64 hi, int max_ticks) -> Vec(f64) {
    Vec(f64) ticks = {}
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
    while li < ax.lines.len() {
        LineStyle ln = ax.lines[li]
        int n = math.min(ln.xs.len(), ln.ys.len())
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

fn plot(&!Axes ax, Vec(f64) ys) {
    Vec(f64) xs = _range(ys.len())
    string c = color_at(ax.lines.len())
    ax.lines.push(LineStyle {
        xs: xs, ys: ys, color: c, label: "", linewidth: 2.0, is_scatter: false
    })
}

// line: one series (line or scatter) with styling via LineOpts.
// Replaces plot_xy / plot_styled / scatter. opts.color == "" -> auto palette.
fn line(&!Axes ax, Vec(f64) xs, Vec(f64) ys, LineOpts opts) {
    string c = opts.color
    if c.length == 0 { c = color_at(ax.lines.len()) }
    ax.lines.push(LineStyle {
        xs: xs, ys: ys, color: c, label: opts.label,
        linewidth: opts.width, is_scatter: opts.scatter
    })
}

fn bar(&!Axes ax, Vec(string) labels, Vec(f64) values, BarOpts opts) {
    string c = opts.color
    if c.length == 0 { c = color_at(ax.bars.len()) }
    ax.bars.push(BarSeries {
        labels: labels, values: values, color: c, label: opts.label
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
    string s = f"Figure {fig.svg_width}x{fig.svg_height} axes={fig.axes_list.len()}\n"
    int i = 0
    while i < fig.axes_list.len() {
        Axes ax = fig.axes_list[i]
        s = s + f"  axes[{i}] '{ax.title}': lines={ax.lines.len()} bars={ax.bars.len()}"
        if ax.grid_visible { s = s + " grid" }
        if ax.legend_visible { s = s + " legend" }
        s = s + "\n"
        i = i + 1
    }
    return s
}

fn print_text(&Figure fig) {
    print(to_text(fig))
}

// ---- SVG backend ----

struct Layout {
    int left
    int top
    int width
    int height
}

fn _svg_escape(string s) -> string {
    string r = ""
    int i = 0
    int n = s.length
    while i < n {
        int ch = s.at(i)
        if ch == '&' { r.append("&amp;") }
        else if ch == '<' { r.append("&lt;") }
        else if ch == '>' { r.append("&gt;") }
        else if ch == '"' { r.append("&quot;") }
        else { r.append(ch) }
        i = i + 1
    }
    return r
}

fn _layout(int sw, int sh) -> Layout {
    int left = 70                 // y tick labels + y axis label
    int top = 40                  // title
    int right = sw - 25
    int bottom = sh - 45          // x tick labels + x axis label
    return Layout { left: left, top: top, width: right - left, height: bottom - top }
}

// Render one finalized Axes (owned by value) into SVG fragments.
fn _render_axes_svg(Axes ax, Layout lo) -> string {
    string s = ""
    int bottom = lo.top + lo.height
    int right = lo.left + lo.width

    // plot-area background + border
    s = s + f"<rect x=\"{lo.left}\" y=\"{lo.top}\" width=\"{lo.width}\" height=\"{lo.height}\" fill=\"#fafafa\" stroke=\"#333333\" stroke-width=\"1\"/>"

    // x ticks: optional grid + tick mark + label
    int i = 0
    while i < ax.xticks.len() {
        f64 tx = ax.xticks[i]
        f64 pxf = map_x(tx, ax.xmin, ax.xmax, lo.left, lo.width)
        if ax.grid_visible {
            s = s + f"<line x1=\"{pxf:.1f}\" y1=\"{lo.top}\" x2=\"{pxf:.1f}\" y2=\"{bottom}\" stroke=\"#e0e0e0\" stroke-width=\"0.5\"/>"
        }
        s = s + f"<line x1=\"{pxf:.1f}\" y1=\"{bottom}\" x2=\"{pxf:.1f}\" y2=\"{bottom + 4}\" stroke=\"#333333\" stroke-width=\"1\"/>"
        string lbl = plotfmt.fmt_auto(tx)
        s = s + f"<text x=\"{pxf:.1f}\" y=\"{bottom + 16}\" font-size=\"10\" font-family=\"monospace\" fill=\"#555555\" text-anchor=\"middle\">{lbl}</text>"
        i = i + 1
    }

    // y ticks
    int j = 0
    while j < ax.yticks.len() {
        f64 ty = ax.yticks[j]
        f64 pyf = map_y(ty, ax.ymin, ax.ymax, lo.top, lo.height)
        if ax.grid_visible {
            s = s + f"<line x1=\"{lo.left}\" y1=\"{pyf:.1f}\" x2=\"{right}\" y2=\"{pyf:.1f}\" stroke=\"#e0e0e0\" stroke-width=\"0.5\"/>"
        }
        s = s + f"<line x1=\"{lo.left - 4}\" y1=\"{pyf:.1f}\" x2=\"{lo.left}\" y2=\"{pyf:.1f}\" stroke=\"#333333\" stroke-width=\"1\"/>"
        string lbl = plotfmt.fmt_auto(ty)
        s = s + f"<text x=\"{lo.left - 6}\" y=\"{pyf:.1f}\" font-size=\"10\" font-family=\"monospace\" fill=\"#555555\" text-anchor=\"end\">{lbl}</text>"
        j = j + 1
    }

    // data series
    int li = 0
    while li < ax.lines.len() {
        LineStyle ln = ax.lines[li]
        int n = math.min(ln.xs.len(), ln.ys.len())
        if ln.is_scatter {
            int k = 0
            while k < n {
                f64 cx = map_x(ln.xs[k], ax.xmin, ax.xmax, lo.left, lo.width)
                f64 cy = map_y(ln.ys[k], ax.ymin, ax.ymax, lo.top, lo.height)
                s = s + f"<circle cx=\"{cx:.1f}\" cy=\"{cy:.1f}\" r=\"3\" fill=\"{ln.color}\"/>"
                k = k + 1
            }
        } else {
            string pts = ""
            int k = 0
            while k < n {
                if k > 0 { pts = pts + " " }
                f64 px = map_x(ln.xs[k], ax.xmin, ax.xmax, lo.left, lo.width)
                f64 py = map_y(ln.ys[k], ax.ymin, ax.ymax, lo.top, lo.height)
                pts = pts + f"{px:.1f},{py:.1f}"
                k = k + 1
            }
            s = s + f"<polyline fill=\"none\" stroke=\"{ln.color}\" stroke-width=\"{ln.linewidth:.1f}\" points=\"{pts}\"/>"
        }
        li = li + 1
    }

    // title + axis labels
    int cx = lo.left + lo.width / 2
    if ax.title.length > 0 {
        s = s + f"<text x=\"{cx}\" y=\"24\" font-size=\"16\" font-family=\"sans-serif\" font-weight=\"bold\" fill=\"#000000\" text-anchor=\"middle\">{_svg_escape(ax.title)}</text>"
    }
    if ax.xlabel.length > 0 {
        s = s + f"<text x=\"{cx}\" y=\"{bottom + 34}\" font-size=\"12\" font-family=\"sans-serif\" fill=\"#333333\" text-anchor=\"middle\">{_svg_escape(ax.xlabel)}</text>"
    }
    if ax.ylabel.length > 0 {
        int yc = lo.top + lo.height / 2
        s = s + f"<text x=\"16\" y=\"{yc}\" font-size=\"12\" font-family=\"sans-serif\" fill=\"#333333\" text-anchor=\"middle\" transform=\"rotate(-90 16 {yc})\">{_svg_escape(ax.ylabel)}</text>"
    }
    return s
}

// to_svg: render the whole Figure to an SVG document string. Each Axes is
// deep-copied and finalized locally (limits/margins/ticks), so the call is
// read-only on `fig` and idempotent. (Multi-subplot layout: Phase 2b.)
fn to_svg(&Figure fig) -> string {
    Layout lo = _layout(fig.svg_width, fig.svg_height)
    string body = ""
    int i = 0
    while i < fig.axes_list.len() {
        Axes a = fig.axes_list[i]
        finalize(&!a)
        body = body + _render_axes_svg(a, lo)
        i = i + 1
    }
    string head = f"<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"{fig.svg_width}\" height=\"{fig.svg_height}\">"
    string bg = f"<rect width=\"100%\" height=\"100%\" fill=\"{fig.background}\"/>"
    return head + bg + body + "</svg>"
}

// ---- Text / ASCII backend ----
//
// The grid is a Vec(string), one string per row, each a run of single-byte
// ASCII chars. (Unicode block glyphs ▁▂▃█ are multi-byte UTF-8 and would
// desync the byte-offset _put_char column math; an Unicode cell-grid is a
// later enhancement.)

fn _make_grid(int w, int h) -> Vec(string) {
    string row = ""
    int j = 0
    while j < w { row = row + " "; j = j + 1 }
    Vec(string) g = {}
    int i = 0
    while i < h { g.push(row.copy()); i = i + 1 }
    return g
}

// Write a single ASCII char at (col,row). NOTE: build the new row in a local
// before assigning to the vec element — assigning a concat rvalue directly to
// g[row] leaks a temporary string in current LS.
fn _put(&!Vec(string) g, int col, int row, string ch) {
    if row < 0 || row >= g.len() { return }
    string r = g[row]
    if col < 0 || col >= r.length { return }
    string nr = r.substr(0, col) + ch + r.substr(col + 1, r.length - col - 1)
    g[row] = nr
}

fn _col_text(f64 x, f64 xmin, f64 xmax, int gw) -> int {
    f64 denom = xmax - xmin
    if denom == 0.0 { denom = 1.0 }
    f64 frac = (x - xmin) / denom
    int c = (frac * (gw as f64)) as int
    return plotfmt.clamp_i(c, 0, gw - 1)
}

fn _row_text(f64 y, f64 ymin, f64 ymax, int gh) -> int {
    f64 denom = ymax - ymin
    if denom == 0.0 { denom = 1.0 }
    f64 frac = (y - ymin) / denom
    int r = (gh - 1) - ((frac * (gh as f64)) as int)
    return plotfmt.clamp_i(r, 0, gh - 1)
}

// DDA line rasterization, choosing a slope-appropriate ASCII glyph.
fn _rasterize_line(&!Vec(string) g, LineStyle ln,
                   f64 xmin, f64 xmax, f64 ymin, f64 ymax, int gw, int gh) {
    int n = math.min(ln.xs.len(), ln.ys.len())
    if n == 1 {
        _put(&!g, _col_text(ln.xs[0], xmin, xmax, gw),
                  _row_text(ln.ys[0], ymin, ymax, gh), "*")
        return
    }
    int i = 0
    while i < n - 1 {
        int c0 = _col_text(ln.xs[i], xmin, xmax, gw)
        int r0 = _row_text(ln.ys[i], ymin, ymax, gh)
        int c1 = _col_text(ln.xs[i + 1], xmin, xmax, gw)
        int r1 = _row_text(ln.ys[i + 1], ymin, ymax, gh)
        int dc = c1 - c0
        int dr = r1 - r0
        string ch = "*"
        if ln.is_scatter { ch = "o" }
        else if math.abs(dr) * 2 < math.abs(dc) { ch = "-" }
        else if math.abs(dc) * 2 < math.abs(dr) { ch = "|" }
        else if dc * dr < 0 { ch = "/" }
        else { ch = "\\" }
        int steps = math.max(math.abs(dc), math.abs(dr))
        if steps == 0 {
            _put(&!g, c0, r0, ch)
        } else {
            int t = 0
            while t <= steps {
                int c = c0 + dc * t / steps
                int r = r0 + dr * t / steps
                _put(&!g, c, r, ch)
                t = t + 1
            }
        }
        i = i + 1
    }
}

fn _render_axes_text(Axes ax, int w, int h) -> string {
    int label_w = 6
    int gw = w - label_w - 1
    int gh = h - 1
    if gw < 1 { gw = 1 }
    if gh < 1 { gh = 1 }

    Vec(string) g = _make_grid(gw, gh)
    int li = 0
    while li < ax.lines.len() {
        LineStyle ln = ax.lines[li]
        _rasterize_line(&!g, ln, ax.xmin, ax.xmax, ax.ymin, ax.ymax, gw, gh)
        li = li + 1
    }

    string s = ""
    if ax.title.length > 0 { s = s + ax.title + "\n" }
    int row = 0
    while row < gh {
        Str ylab = ""
        if row == 0 { ylab = plotfmt.fmt_auto(ax.ymax) }
        else if row == gh - 1 { ylab = plotfmt.fmt_auto(ax.ymin) }
        string padded = plotfmt.pad_left(ylab, label_w)
        string line = g[row]
        s = s + padded + "|" + line + "\n"
        row = row + 1
    }

    // x axis
    string axisline = plotfmt.pad_left("", label_w) + "+"
    int c = 0
    while c < gw { axisline = axisline + "-"; c = c + 1 }
    s = s + axisline + "\n"

    // x range labels (xmin left, xmax right)
    Str lo_lab = plotfmt.fmt_auto(ax.xmin)
    Str hi_lab = plotfmt.fmt_auto(ax.xmax)
    string xline = plotfmt.pad_left("", label_w + 1) + plotfmt.pad_right(lo_lab, gw - hi_lab.len()) + hi_lab
    s = s + xline + "\n"
    return s
}

// to_text: render the whole Figure to a terminal string. Each Axes is
// deep-copied and finalized locally (read-only on `fig`, idempotent).
fn to_text(&Figure fig) -> string {
    string out = ""
    int ai = 0
    while ai < fig.axes_list.len() {
        Axes a = fig.axes_list[ai]
        finalize(&!a)
        out = out + _render_axes_text(a, fig.text_width, fig.text_height)
        ai = ai + 1
    }
    return out
}
