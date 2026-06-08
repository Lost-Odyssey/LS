// plot_html_test.ls — TL-2b: self-contained scrollable HTML wrapper.
// Prints "HTML PASS" / "HTML FAIL: ...".

import plottl
import std.vec

fn make_events() -> Vec(CpuSchedEvent) {
    Vec(CpuSchedEvent) ev = {}
    ev.push(plottl.cpu_event(0, 6000000, 100, "main", 0, "app"))
    ev.push(plottl.cpu_event(2000000, 7000000, 101, "worker-a", 4, "app"))  // HT of core 0
    ev.push(plottl.cpu_event(8000000, 14000000, 100, "main", 1, "app"))
    return ev
}

fn has(string hay, string needle, string name) -> bool {
    if hay.contains(needle) { return true }
    print("HTML FAIL: " + name + " missing [" + needle + "]")
    return false
}

fn main() {
    bool ok = true

    plottl.CpuTopology topo = plottl.topology(8, 4)
    string html = plottl.cpu_timeline_html(make_events(), topo, plottl.CpuPlotOpts{})

    // document + scroll container (pure scheme A)
    ok = has(html, "<!DOCTYPE html>", "doctype") && ok
    ok = has(html, "overflow-x:auto", "scroll.css") && ok
    ok = has(html, "class=\"scroll\"", "scroll.div") && ok
    ok = has(html, "class=\"lanes\"", "lanes.col") && ok
    // fixed lane column entries
    ok = has(html, "class=\"lane\">main (100)</div>", "lane.main") && ok
    ok = has(html, "class=\"lane\">worker-a (101)</div>", "lane.worker") && ok
    // wide SVG (chart_width = 2400 -> needs horizontal scroll)
    ok = has(html, "<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"2400\"", "wide.svg") && ok
    // HT pattern for cpu 4 + its use
    ok = has(html, "<pattern id=\"ht4\"", "ht.pattern") && ok
    ok = has(html, "url(#ht4)", "ht.fill") && ok
    // legend (CSS-striped HT swatch)
    ok = has(html, "repeating-linear-gradient", "legend.stripe") && ok
    ok = has(html, "</span>CPU 0</span>", "legend.cpu0") && ok

    if ok { print("HTML PASS") }
}
