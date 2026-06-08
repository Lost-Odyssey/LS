// plot_csv_test.ls — TL-1.5: CSV input parser for timeline events.
// Prints "CSV PASS" / "CSV FAIL: ...".

import plottl
import std.vec

fn main() {
    bool ok = true

    // header row + blank line + row missing the color column (-> auto palette)
    string csv = "start_ns,end_ns,lane,label,color\n0,5000000,main,compute,#4363d8\n3000000,8000000,worker,io\n\n6000000,12000000,main,compute2,#3cb44b\n"
    Vec(TimelineEvent) ev = plottl.parse_timeline_csv(csv)

    if ev.len() != 3 {
        print("CSV FAIL: count=" + f"{ev.len()}" + " want=3")
        ok = false
    }

    // peek parsed fields (deep-copy reads)
    TimelineEvent e0 = ev[0]
    if e0.lane != "main" { print("CSV FAIL: e0.lane=[" + e0.lane + "]"); ok = false }
    if e0.start_ns != 0 { print("CSV FAIL: e0.start"); ok = false }
    if e0.end_ns != 5000000 { print("CSV FAIL: e0.end"); ok = false }
    if e0.color != "#4363d8" { print("CSV FAIL: e0.color=[" + e0.color + "]"); ok = false }

    // worker row had no color -> auto palette index 1 (#e6194b)
    TimelineEvent e1 = ev[1]
    if e1.lane != "worker" { print("CSV FAIL: e1.lane=[" + e1.lane + "]"); ok = false }
    if e1.color != "#e6194b" { print("CSV FAIL: e1.auto-color=[" + e1.color + "]"); ok = false }

    // parsed events render
    string svg = plottl.timeline_svg(ev, 600, 200, "from csv")
    if !svg.contains("<title>compute2</title>") { print("CSV FAIL: render missing rect"); ok = false }

    if ok { print("CSV PASS") }
}
