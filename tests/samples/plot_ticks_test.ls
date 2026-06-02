// plot_ticks_test.ls — Phase 2: tick engine + coordinate mapping + auto-scale.
// Prints "TICKS PASS" / "TICKS FAIL: ...".

import plot
import plotfmt
import math

fn ticks_str(vec(f64) t) -> string {
    string s = ""
    int i = 0
    while i < t.length {
        if i > 0 { s = s + "," }
        s = s + plotfmt.fmt_fixed(t[i], 3)
        i = i + 1
    }
    return s
}

fn check(string got, string want, string name) -> bool {
    if got == want { return true }
    print("TICKS FAIL: " + name + " got=[" + got + "] want=[" + want + "]")
    return false
}

fn main() {
    bool ok = true

    // ---- generate_ticks: 5 rows from plan_plot.md §4.2 (standard Heckbert) ----
    ok = check(ticks_str(plot.generate_ticks(0.37, 5.82, 6)), "2.000,4.000", "row1") && ok
    ok = check(ticks_str(plot.generate_ticks(0.03, 0.17, 6)), "0.050,0.100,0.150", "row2") && ok
    ok = check(ticks_str(plot.generate_ticks(12.3, 98.7, 6)), "20.000,40.000,60.000,80.000", "row3") && ok
    ok = check(ticks_str(plot.generate_ticks(-2.7, 1.3, 6)), "-2.000,-1.000,0.000,1.000", "row4") && ok
    ok = check(ticks_str(plot.generate_ticks(0.0, 0.012, 6)), "0.000,0.005,0.010", "row5") && ok

    // ---- coordinate mapping ----
    ok = check(plotfmt.fmt_fixed(plot.map_x(5.0, 0.0, 10.0, 80, 640), 1), "400.0", "map_x.mid") && ok
    ok = check(plotfmt.fmt_fixed(plot.map_x(0.0, 0.0, 10.0, 80, 640), 1), "80.0", "map_x.lo") && ok
    ok = check(plotfmt.fmt_fixed(plot.map_y(0.0, -1.0, 1.0, 50, 400), 1), "250.0", "map_y.mid") && ok
    ok = check(plotfmt.fmt_fixed(plot.map_y(1.0, -1.0, 1.0, 50, 400), 1), "50.0", "map_y.top") && ok

    // ---- auto-scale: finalize computes limits + margins + ticks ----
    plot.Axes ax = plot.axes()
    vec(f64) xs = [0.0, 1.0, 2.0, 3.0]
    vec(f64) ys = [0.0, 10.0, 5.0, 20.0]
    plot.plot_xy(&!ax, xs, ys)
    plot.finalize(&!ax)

    ok = check(plotfmt.fmt_fixed(ax.xmin, 2), "-0.15", "xmin") && ok
    ok = check(plotfmt.fmt_fixed(ax.xmax, 2), "3.15", "xmax") && ok
    ok = check(plotfmt.fmt_fixed(ax.ymin, 2), "-1.00", "ymin") && ok
    ok = check(plotfmt.fmt_fixed(ax.ymax, 2), "21.00", "ymax") && ok
    ok = check(ticks_str(ax.xticks.copy()), "0.000,1.000,2.000,3.000", "xticks") && ok
    ok = check(ticks_str(ax.yticks.copy()), "0.000,10.000,20.000", "yticks") && ok

    if ok { print("TICKS PASS") }
}
