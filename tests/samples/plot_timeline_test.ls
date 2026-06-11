// plot_timeline_test.ls — TL-1: timeline swimlane SVG + text.
// Prints "TL PASS" / "TL FAIL: ...".

import plottl
import std.vec
import std.str

fn make_events() -> Vec(TimelineEvent) {
    Vec(TimelineEvent) ev = {}
    ev.push(plottl.event(0, 5000000, "main", "compute", "#4363d8"))
    ev.push(plottl.event(3000000, 8000000, "worker", "io", "#e6194b"))
    ev.push(plottl.event(6000000, 12000000, "main", "compute2", "#3cb44b"))
    return ev
}

fn has(Str hay, Str needle, Str name) -> bool {
    if hay.contains?(needle) { return true }
    print(f"TL FAIL: {name} missing [{needle}]")
    return false
}

fn count_lines(Str s) -> int {
    int n = 0
    int i = 0
    while i < s.len() {
        if s.byte_at(i) == '\n' { n = n + 1 }
        i = i + 1
    }
    return n
}

fn count_occ(Str hay, Str needle) -> int {
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

fn main() {
    bool ok = true

    // ---- SVG ----
    Str svg = plottl.timeline_svg(make_events(), 600, 200, "sched")
    ok = has(svg, "<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"600\"", "svg.header") && ok
    ok = has(svg, "</svg>", "svg.footer") && ok
    ok = has(svg, ">sched<", "title") && ok
    ok = has(svg, ">main<", "lane.main") && ok
    ok = has(svg, ">worker<", "lane.worker") && ok
    ok = has(svg, "<title>compute</title>", "rect.title") && ok
    ok = has(svg, "fill=\"#4363d8\"", "rect.color") && ok
    // 3 events -> 3 <rect ...><title> (plus the bg rect without title)
    int rects = count_occ(svg, "<title>")
    if rects != 3 {
        print(f"TL FAIL: rect/title count got={rects} want=3")
        ok = false
    }

    // ---- Text ----
    Str txt = plottl.timeline_text(make_events(), 50)
    ok = has(txt, "main", "text.lane.main") && ok
    ok = has(txt, "worker", "text.lane.worker") && ok
    ok = has(txt, "#", "text.active") && ok
    // 2 unique lanes -> 2 rows
    int lc = count_lines(txt)
    if lc != 2 {
        print(f"TL FAIL: text line count got={lc} want=2")
        ok = false
    }

    if ok { print("TL PASS") }
}
