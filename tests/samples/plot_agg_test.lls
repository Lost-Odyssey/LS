// plot_agg_test.ls — TL-3: aggregated CPU timeline (time-window dominant CPU).
// Prints "AGG PASS" / "AGG FAIL: ...".

import std.chart.plottl as plottl
import std.core.vec
import std.core.str

def make_events() -> Vec(CpuSchedEvent) {
    Vec(CpuSchedEvent) ev = {}
    // main: window 0 [0,10ms] -> cpu0 7ms vs cpu1 2ms -> dominant cpu0
    ev.push(plottl.cpu_event(0,        7000000,  1, "main", 0, "app"))
    ev.push(plottl.cpu_event(7000000,  9000000,  1, "main", 1, "app"))
    // main: window 1 [10,20ms] -> cpu1 8ms -> dominant cpu1
    ev.push(plottl.cpu_event(12000000, 20000000, 1, "main", 1, "app"))
    return ev
}

def has(Str hay, Str needle, Str name) -> bool {
    if hay.contains?(needle) { return true }
    @print(f"AGG FAIL: {name} missing [{needle}]")
    return false
}

def count_occ(Str hay, Str needle) -> int {
    int n = 0
    int i = 0
    int hl = hay.len()
    int nl = needle.len()
    while i + nl <= hl {
        if hay.substr(i, nl).eq?(needle) { n = n + 1; i = i + nl }
        else { i = i + 1 }
    }
    return n
}

def main() {
    bool ok = true

    plottl.CpuTopology topo = plottl.topology(4, 4)   // 4 physical, no HT
    // 10ms aggregation window
    Str svg = plottl.cpu_timeline_aggregated(make_events(), topo, 10000000, plottl.CpuPlotOpts{})

    ok = has(svg, "aggregated, window=", "title") && ok
    ok = has(svg, "window=10.0ms", "window_label") && ok
    // window 0 dominant = cpu 0, window 1 dominant = cpu 1
    ok = has(svg, "on CPU 0 [core 0", "win0_dominant_cpu0") && ok
    ok = has(svg, "on CPU 1 [core 1", "win1_dominant_cpu1") && ok
    // cpu0 solid color (rainbow)
    ok = has(svg, "fill=\"#bf4343\"", "cpu0_color") && ok
    // exactly 2 aggregated cells (2 windows with activity); "on CPU " only in cell titles
    int cells = count_occ(svg, "on CPU ")
    if cells != 2 {
        @print(f"AGG FAIL: cell count got={cells} want=2"); ok = false
    }
    // legend has both CPUs
    ok = has(svg, ">CPU 0<", "legend_cpu0") && ok
    ok = has(svg, ">CPU 1<", "legend_cpu1") && ok

    if ok { @print("AGG PASS") }
}
