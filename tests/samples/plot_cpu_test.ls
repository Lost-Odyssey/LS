// plot_cpu_test.ls — TL-2: CPU scheduling timeline + Hyper-Threading coloring.
// Prints "TL2 PASS" / "TL2 FAIL: ...".

import plottl
import std.vec
import std.str

fn make_cpu_events() -> Vec(CpuSchedEvent) {
    Vec(CpuSchedEvent) ev = {}
    ev.push(plottl.cpu_event(0, 5000000, 1234, "main", 0, "app"))        // core 0 primary
    ev.push(plottl.cpu_event(3000000, 8000000, 5678, "worker-1", 32, "app"))   // core 0 HT sibling
    ev.push(plottl.cpu_event(6000000, 12000000, 1234, "main", 1, "app"))        // core 1 primary
    ev.push(plottl.cpu_event(9000000, 15000000, 9012, "worker-2", 33, "app"))   // core 1 HT sibling
    return ev
}

// helpers take Str: plottl's color helpers now return Str, and call-rvalue
// args don't pass through the Str->string bridge (IDENT-only).
fn has(Str hay, Str needle, Str name) -> bool {
    if hay.contains?(needle) { return true }
    print(f"TL2 FAIL: {name} missing [{needle}]")
    return false
}

fn check(Str got, Str want, Str name) -> bool {
    if got == want { return true }
    print(f"TL2 FAIL: {name} got=[{got}] want=[{want}]")
    return false
}

fn count_occ(Str hay, Str needle) -> int {
    return hay.count(needle)
}

fn main() {
    bool ok = true

    // ---- color math ----
    ok = check(plottl.cpu_color(0, 32), "#bf4343", "cpu_color.0") && ok
    // CPU 0 and CPU 32 are on the same physical core -> identical hue
    ok = check(f"{plottl.cpu_hue(0, 32):.2f}", f"{plottl.cpu_hue(32, 32):.2f}", "same.hue") && ok
    // ... but distinguishable colors (HT sibling is dimmer)
    if plottl.cpu_color(32, 32) == plottl.cpu_color(0, 32) {
        print("TL2 FAIL: HT sibling not distinguishable"); ok = false
    }
    if plottl.is_ht_sibling(0, 32) { print("TL2 FAIL: cpu0 flagged HT"); ok = false }
    if !plottl.is_ht_sibling(32, 32) { print("TL2 FAIL: cpu32 not HT"); ok = false }
    if plottl.physical_core(33, 32) != 1 { print("TL2 FAIL: phys core 33"); ok = false }

    // ---- color themes ----
    ok = check(plottl.cpu_theme_color(0, 4, "viridis"), "#440154", "theme.viridis0") && ok
    ok = check(plottl.cpu_theme_color(0, 32, "rainbow"), "#bf4343", "theme.rainbow0") && ok

    // topology2 (total CPUs + HT flag)
    plottl.CpuTopology t2 = plottl.topology2(64, true)
    if t2.total_physical != 32 { print("TL2 FAIL: topology2 phys"); ok = false }

    // ---- SVG ----
    plottl.CpuTopology topo = plottl.topology(64, 32)
    Str svg = plottl.cpu_timeline_svg(make_cpu_events(), topo, plottl.CpuPlotOpts{})
    ok = has(svg, "CPU Scheduling Timeline", "svg.title") && ok
    ok = has(svg, "<pattern id=\"ht32\"", "ht.pattern.def") && ok
    ok = has(svg, "url(#ht32)", "ht.fill") && ok
    ok = has(svg, "fill=\"#bf4343\"", "cpu0.solid") && ok
    // legend: only "CPU x" (no "core X"), sorted ascending
    ok = has(svg, ">CPU 0<", "legend.cpu0") && ok
    ok = has(svg, ">CPU 32 (HT)<", "legend.cpu32.ht") && ok
    if svg.contains?("core 0<") { print("TL2 FAIL: legend still shows core"); ok = false }
    // legend entry count == unique CPUs (0,1,32,33) = 4 (14x10 swatch only in legend)
    int legn = count_occ(svg, "width=\"14\" height=\"10\"")
    if legn != 4 {
        print(f"TL2 FAIL: legend count got={legn} want=4"); ok = false
    }

    // ---- options-struct theme override flows through (cross-module literal) ----
    Str svg_v = plottl.cpu_timeline_svg(make_cpu_events(), topo, plottl.CpuPlotOpts{theme: "viridis"})
    ok = has(svg_v, "fill=\"#440154\"", "opts.theme.viridis") && ok

    // ---- Text backend: thread activity only, no CPU info ----
    Str txt = plottl.cpu_timeline_text(make_cpu_events(), 50)
    ok = has(txt, "main", "text.main") && ok
    ok = has(txt, "worker-1", "text.worker1") && ok
    if txt.contains?("CPU") { print("TL2 FAIL: text leaks 'CPU'"); ok = false }
    if txt.contains?("core") { print("TL2 FAIL: text leaks 'core'"); ok = false }

    if ok { print("TL2 PASS") }
}
