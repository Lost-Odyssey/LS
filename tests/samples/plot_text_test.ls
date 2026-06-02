// plot_text_test.ls — Phase 2 Text/ASCII backend: grid + DDA rasterization +
// axes + y/x labels. Prints "TEXT PASS" / "TEXT FAIL: ...".

import plot
import math

fn has(string hay, string needle, string name) -> bool {
    if hay.contains(needle) { return true }
    print("TEXT FAIL: " + name + " missing [" + needle + "]")
    return false
}

fn count_lines(string s) -> int {
    int n = 0
    int i = 0
    while i < s.length {
        if s.at(i) == '\n' { n = n + 1 }
        i = i + 1
    }
    return n
}

fn main() {
    bool ok = true

    plot.Axes ax = plot.axes()
    plot.set_title(&!ax, "ramp")
    // simple ascending line y = x
    vec(f64) xs = [0.0, 1.0, 2.0, 3.0, 4.0]
    vec(f64) ys = [0.0, 1.0, 2.0, 3.0, 4.0]
    plot.plot_xy(&!ax, xs, ys)

    plot.Figure fig = plot.figure(800, 500, 50, 12)
    plot.add_axes(&!fig, ax)

    string txt = plot.to_text(fig)

    // structural assertions
    ok = has(txt, "ramp\n", "title") && ok
    ok = has(txt, "|", "y.axis") && ok
    ok = has(txt, "+----", "x.axis") && ok
    ok = has(txt, "4.20", "y.max.label") && ok          // ymax = 4.0 + 5% margin
    // ascending line -> at least one slope glyph present
    bool glyph = txt.contains("/") || txt.contains("*") || txt.contains("\\") || txt.contains("-")
    if !glyph { print("TEXT FAIL: no line glyph"); ok = false }

    // line count: title(1) + grid rows(h-1=11) + x-axis(1) + x-labels(1) = 14
    int lc = count_lines(txt)
    if lc != 14 {
        print("TEXT FAIL: line count got=" + f"{lc}" + " want=14")
        ok = false
    }

    if ok { print("TEXT PASS") }
}
