// std/plottl.ls — Timeline / Gantt swimlane rendering (plot TL-1).
//
// Pure-LS, depends on plotfmt + math; no compiler changes. Renders a list of
// time intervals grouped into horizontal lanes (one row per lane), to SVG
// (rects + hover <title> + time-axis ticks) or terminal text (active '#').
//
// TL-2 adds CPU-scheduling + Hyper-Threading coloring on top of this model.

import math
import plotfmt
import io

struct TimelineEvent {
    i64 start_ns
    i64 end_ns
    string lane
    string label
    string color
}

fn event(i64 s, i64 e, string lane, string label, string color) -> TimelineEvent {
    return TimelineEvent { start_ns: s, end_ns: e, lane: lane, label: label, color: color }
}

// ---- helpers ----

fn _tl_escape(string s) -> string {
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

fn _map_time(i64 t, i64 tmin, i64 tmax, int left, int width) -> f64 {
    f64 denom = (tmax - tmin) as f64
    if denom == 0.0 { denom = 1.0 }
    return (left as f64) + ((t - tmin) as f64) / denom * (width as f64)
}

// ---- SVG backend ----

fn timeline_svg(vec(TimelineEvent) events, int w, int h, string title) -> string {
    int n = events.length

    // 1. time range
    bool first = true
    i64 tmin = 0
    i64 tmax = 1
    int i = 0
    while i < n {
        TimelineEvent e = events[i]
        if first { tmin = e.start_ns; tmax = e.end_ns; first = false }
        else {
            if e.start_ns < tmin { tmin = e.start_ns }
            if e.end_ns > tmax { tmax = e.end_ns }
        }
        i = i + 1
    }
    if tmax <= tmin { tmax = tmin + 1 }

    // 2. unique lanes (preserve first-seen order)
    vec(string) lanes = []
    i = 0
    while i < n {
        TimelineEvent e = events[i]
        bool found = false
        int j = 0
        while j < lanes.length {
            string ln = lanes[j]
            if ln == e.lane { found = true }
            j = j + 1
        }
        if !found { lanes.push(e.lane.copy()) }
        i = i + 1
    }

    // 3. layout
    int label_w = 90
    int left = label_w
    int top = 36
    int right = w - 20
    int width = right - left
    int lane_h = 24
    int plot_bottom = top + lanes.length * lane_h

    string s = ""
    // title
    if title.length > 0 {
        int cx = left + width / 2
        s = s + f"<text x=\"{cx}\" y=\"22\" font-size=\"15\" font-family=\"sans-serif\" font-weight=\"bold\" text-anchor=\"middle\" fill=\"#000000\">{_tl_escape(title)}</text>"
    }

    // lane labels + row separators
    int li = 0
    while li < lanes.length {
        string lname = lanes[li]
        int ly = top + li * lane_h
        s = s + f"<text x=\"{left - 8}\" y=\"{ly + lane_h / 2 + 4}\" font-size=\"11\" font-family=\"monospace\" text-anchor=\"end\" fill=\"#333333\">{_tl_escape(lname)}</text>"
        s = s + f"<line x1=\"{left}\" y1=\"{ly}\" x2=\"{right}\" y2=\"{ly}\" stroke=\"#eeeeee\" stroke-width=\"0.5\"/>"
        li = li + 1
    }

    // event rects
    i = 0
    while i < n {
        TimelineEvent e = events[i]
        int idx = 0
        int j = 0
        while j < lanes.length {
            string ln = lanes[j]
            if ln == e.lane { idx = j }
            j = j + 1
        }
        f64 x0 = _map_time(e.start_ns, tmin, tmax, left, width)
        f64 x1 = _map_time(e.end_ns, tmin, tmax, left, width)
        f64 ww = x1 - x0
        if ww < 1.0 { ww = 1.0 }
        int ry = top + idx * lane_h + 3
        int rh = lane_h - 6
        s = s + f"<rect x=\"{x0:.1f}\" y=\"{ry}\" width=\"{ww:.1f}\" height=\"{rh}\" rx=\"2\" fill=\"{e.color}\"><title>{_tl_escape(e.label)}</title></rect>"
        i = i + 1
    }

    // time axis + ticks
    s = s + f"<line x1=\"{left}\" y1=\"{plot_bottom}\" x2=\"{right}\" y2=\"{plot_bottom}\" stroke=\"#333333\" stroke-width=\"1\"/>"
    int nt = 5
    int ti = 0
    while ti <= nt {
        i64 tv = tmin + (tmax - tmin) * (ti as i64) / (nt as i64)
        f64 tx = _map_time(tv, tmin, tmax, left, width)
        s = s + f"<line x1=\"{tx:.1f}\" y1=\"{plot_bottom}\" x2=\"{tx:.1f}\" y2=\"{plot_bottom + 4}\" stroke=\"#333333\" stroke-width=\"1\"/>"
        string tl = plotfmt.fmt_time(tv - tmin)
        s = s + f"<text x=\"{tx:.1f}\" y=\"{plot_bottom + 16}\" font-size=\"10\" font-family=\"monospace\" text-anchor=\"middle\" fill=\"#555555\">{tl}</text>"
        ti = ti + 1
    }

    int svgh = plot_bottom + 30
    if svgh < h { svgh = h }
    string head = f"<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"{w}\" height=\"{svgh}\">"
    string bg = "<rect width=\"100%\" height=\"100%\" fill=\"#ffffff\"/>"
    return head + bg + s + "</svg>"
}

// ---- Text backend ----

fn timeline_text(vec(TimelineEvent) events, int w) -> string {
    int n = events.length

    bool first = true
    i64 tmin = 0
    i64 tmax = 1
    int i = 0
    while i < n {
        TimelineEvent e = events[i]
        if first { tmin = e.start_ns; tmax = e.end_ns; first = false }
        else {
            if e.start_ns < tmin { tmin = e.start_ns }
            if e.end_ns > tmax { tmax = e.end_ns }
        }
        i = i + 1
    }
    if tmax <= tmin { tmax = tmin + 1 }

    vec(string) lanes = []
    i = 0
    while i < n {
        TimelineEvent e = events[i]
        bool found = false
        int j = 0
        while j < lanes.length {
            string ln = lanes[j]
            if ln == e.lane { found = true }
            j = j + 1
        }
        if !found { lanes.push(e.lane.copy()) }
        i = i + 1
    }

    int label_w = 12
    int gw = w - label_w
    if gw < 1 { gw = 1 }
    i64 span = tmax - tmin

    string out = ""
    int li = 0
    while li < lanes.length {
        string lname = lanes[li]
        string row = ""
        int c = 0
        while c < gw {
            i64 t = tmin + span * (c as i64) / (gw as i64)
            bool active = false
            int k = 0
            while k < n {
                TimelineEvent e = events[k]
                if e.lane == lname {
                    if e.start_ns <= t {
                        if t < e.end_ns { active = true }
                    }
                }
                k = k + 1
            }
            if active { row = row + "#" }
            else { row = row + " " }
            c = c + 1
        }
        string padded = plotfmt.pad_right(lname, label_w)
        out = out + padded + row + "\n"
        li = li + 1
    }
    return out
}

// ---- CSV input ----
//
// Format (header row optional, auto-skipped):
//   start_ns,end_ns,lane,label[,color]
// Blank lines and malformed rows are skipped. When the color column is absent
// or empty, a stable palette color is assigned per lane (first-seen order).

fn _starts_num(string s) -> bool {
    if s.length == 0 { return false }
    int c = s.at(0)
    if c == '-' || c == '+' { return true }
    if c >= '0' && c <= '9' { return true }
    return false
}

fn _to_i64_or(string s, i64 dflt) -> i64 {
    match s.to_i64() {
        Ok(v) => { return v }
        Err(e) => { return dflt }
    }
}

fn _palette(int i) -> string {
    int k = i % 8
    if k == 0 { return "#4363d8" }
    else if k == 1 { return "#e6194b" }
    else if k == 2 { return "#3cb44b" }
    else if k == 3 { return "#f58231" }
    else if k == 4 { return "#911eb4" }
    else if k == 5 { return "#42d4f4" }
    else if k == 6 { return "#f032e6" }
    return "#469990"
}

fn parse_timeline_csv(string text) -> vec(TimelineEvent) {
    vec(TimelineEvent) out = []
    vec(string) lane_seen = []
    vec(string) rows = text.lines()
    int r = 0
    while r < rows.length {
        string line = rows[r]
        string t = line.trim()
        if t.length > 0 {
            vec(string) f = t.split(",")
            if f.length >= 4 {
                string c0 = f[0].trim()
                if _starts_num(c0) {
                    i64 s64 = _to_i64_or(c0, 0)
                    i64 e64 = _to_i64_or(f[1].trim(), 0)
                    string lane = f[2].trim()
                    string label = f[3].trim()
                    string color = ""
                    if f.length >= 5 {
                        string c4 = f[4].trim()
                        if c4.length > 0 { color = c4 }
                    }
                    // lane index (also tracks first-seen order for auto color)
                    int idx = 0 - 1
                    int j = 0
                    while j < lane_seen.length {
                        string sn = lane_seen[j]
                        if sn == lane { idx = j }
                        j = j + 1
                    }
                    if idx < 0 {
                        idx = lane_seen.length
                        lane_seen.push(lane.copy())
                    }
                    if color.length == 0 { color = _palette(idx) }
                    out.push(TimelineEvent {
                        start_ns: s64, end_ns: e64,
                        lane: lane, label: label, color: color
                    })
                }
            }
        }
        r = r + 1
    }
    return out
}

// Read a CSV file and parse it. Returns an empty vec if the file can't be read.
fn load_timeline_csv(string path) -> vec(TimelineEvent) {
    match io.read_file(path) {
        Ok(text) => { return parse_timeline_csv(text) }
        Err(e) => {
            vec(TimelineEvent) empty = []
            return empty
        }
    }
}

// ====================================================================
//  TL-2 — CPU scheduling timeline with Hyper-Threading coloring
// ====================================================================

struct CpuSchedEvent {
    i64 start_ns
    i64 end_ns
    int tid
    string tname
    int cpu_id
    string proc
}

struct CpuTopology {
    int total_logical
    int total_physical
    int threads_per_core
}

fn cpu_event(i64 s, i64 e, int tid, string tname, int cpu_id, string proc) -> CpuSchedEvent {
    return CpuSchedEvent { start_ns: s, end_ns: e, tid: tid, tname: tname, cpu_id: cpu_id, proc: proc }
}

fn topology(int logical, int physical) -> CpuTopology {
    int tpc = 1
    if physical > 0 { tpc = logical / physical }
    return CpuTopology { total_logical: logical, total_physical: physical, threads_per_core: tpc }
}

// ---- HT coloring (pure math; physical core drives hue, HT sibling drives brightness) ----

fn physical_core(int cpu_id, int total_physical) -> int {
    if total_physical < 1 { return cpu_id }
    return cpu_id % total_physical
}

fn is_ht_sibling(int cpu_id, int total_physical) -> bool {
    return cpu_id >= total_physical
}

// Hue (degrees) for a CPU, keyed on its physical core so the two logical CPUs
// of one core share a hue. CPU/core 0 -> 0 (red); others spread over 15..285.
fn cpu_hue(int cpu_id, int total_physical) -> f64 {
    int pc = physical_core(cpu_id, total_physical)
    if pc == 0 { return 0.0 }
    int phys = total_physical
    if phys < 1 { phys = 1 }
    return 15.0 + (pc as f64) / (phys as f64) * 270.0
}

// Fill color: bright primary for first logical CPU, dimmer for the HT sibling.
fn cpu_color(int cpu_id, int total_physical) -> string {
    f64 h = cpu_hue(cpu_id, total_physical)
    if is_ht_sibling(cpu_id, total_physical) {
        return plotfmt.hsv_to_hex(h, 0.60, 0.55)
    }
    return plotfmt.hsv_to_hex(h, 0.65, 0.75)
}

// ---- Text backend: thread activity only (no CPU distinction) ----

fn cpu_timeline_text(vec(CpuSchedEvent) events, int w) -> string {
    int n = events.length
    bool first = true
    i64 tmin = 0
    i64 tmax = 1
    int i = 0
    while i < n {
        CpuSchedEvent e = events[i]
        if first { tmin = e.start_ns; tmax = e.end_ns; first = false }
        else {
            if e.start_ns < tmin { tmin = e.start_ns }
            if e.end_ns > tmax { tmax = e.end_ns }
        }
        i = i + 1
    }
    if tmax <= tmin { tmax = tmin + 1 }

    vec(string) lanes = []
    i = 0
    while i < n {
        CpuSchedEvent e = events[i]
        bool found = false
        int j = 0
        while j < lanes.length {
            string ln = lanes[j]
            if ln == e.tname { found = true }
            j = j + 1
        }
        if !found { lanes.push(e.tname.copy()) }
        i = i + 1
    }

    int label_w = 12
    int gw = w - label_w
    if gw < 1 { gw = 1 }
    i64 span = tmax - tmin

    string out = ""
    int li = 0
    while li < lanes.length {
        string lname = lanes[li]
        string row = ""
        int c = 0
        while c < gw {
            i64 t = tmin + span * (c as i64) / (gw as i64)
            bool active = false
            int k = 0
            while k < n {
                CpuSchedEvent e = events[k]
                if e.tname == lname {
                    if e.start_ns <= t {
                        if t < e.end_ns { active = true }
                    }
                }
                k = k + 1
            }
            if active { row = row + "#" }
            else { row = row + " " }
            c = c + 1
        }
        string padded = plotfmt.pad_right(lname, label_w)
        out = out + padded + row + "\n"
        li = li + 1
    }
    return out
}

// ---- SVG backend: swimlanes per thread, HT coloring, CPU legend ----

fn cpu_timeline_svg(vec(CpuSchedEvent) events, CpuTopology topo, int w, int h) -> string {
    int n = events.length
    int phys = topo.total_physical
    if phys < 1 { phys = 1 }

    // time range
    bool first = true
    i64 tmin = 0
    i64 tmax = 1
    int i = 0
    while i < n {
        CpuSchedEvent e = events[i]
        if first { tmin = e.start_ns; tmax = e.end_ns; first = false }
        else {
            if e.start_ns < tmin { tmin = e.start_ns }
            if e.end_ns > tmax { tmax = e.end_ns }
        }
        i = i + 1
    }
    if tmax <= tmin { tmax = tmin + 1 }

    // unique lanes by tid (parallel tid + name)
    vec(int) lane_tids = []
    vec(string) lane_names = []
    i = 0
    while i < n {
        CpuSchedEvent e = events[i]
        bool found = false
        int j = 0
        while j < lane_tids.length {
            if lane_tids[j] == e.tid { found = true }
            j = j + 1
        }
        if !found { lane_tids.push(e.tid); lane_names.push(e.tname.copy()) }
        i = i + 1
    }

    // unique CPUs (first-seen)
    vec(int) cpus = []
    i = 0
    while i < n {
        CpuSchedEvent e = events[i]
        bool found = false
        int j = 0
        while j < cpus.length {
            if cpus[j] == e.cpu_id { found = true }
            j = j + 1
        }
        if !found { cpus.push(e.cpu_id) }
        i = i + 1
    }

    int label_w = 100
    int left = label_w
    int top = 36
    int right = w - 20
    int width = right - left
    int lane_h = 24
    int plot_bottom = top + lane_tids.length * lane_h

    // <defs>: a diagonal-stripe pattern per HT-sibling CPU
    string defs = "<defs>"
    int ci = 0
    while ci < cpus.length {
        int cpu = cpus[ci]
        if cpu >= phys {
            string dim = cpu_color(cpu, phys)
            string acc = cpu_color(cpu % phys, phys)
            defs = defs + f"<pattern id=\"ht{cpu}\" width=\"6\" height=\"6\" patternUnits=\"userSpaceOnUse\" patternTransform=\"rotate(45)\"><rect width=\"6\" height=\"6\" fill=\"{dim}\"/><line x1=\"0\" y1=\"0\" x2=\"0\" y2=\"6\" stroke=\"{acc}\" stroke-width=\"1.5\"/></pattern>"
        }
        ci = ci + 1
    }
    defs = defs + "</defs>"

    string s = ""
    int cx = left + width / 2
    s = s + f"<text x=\"{cx}\" y=\"22\" font-size=\"15\" font-family=\"sans-serif\" font-weight=\"bold\" text-anchor=\"middle\" fill=\"#000000\">CPU Scheduling Timeline</text>"

    // lane labels
    int li = 0
    while li < lane_tids.length {
        string lname = lane_names[li]
        int tid = lane_tids[li]
        int ly = top + li * lane_h
        s = s + f"<text x=\"{left - 8}\" y=\"{ly + lane_h / 2 + 4}\" font-size=\"10\" font-family=\"monospace\" text-anchor=\"end\" fill=\"#333333\">{_tl_escape(lname)} ({tid})</text>"
        s = s + f"<line x1=\"{left}\" y1=\"{ly}\" x2=\"{right}\" y2=\"{ly}\" stroke=\"#eeeeee\" stroke-width=\"0.5\"/>"
        li = li + 1
    }

    // event rects (no visible CPU text on bars; CPU is in the hover <title> only)
    i = 0
    while i < n {
        CpuSchedEvent e = events[i]
        int idx = 0
        int j = 0
        while j < lane_tids.length {
            if lane_tids[j] == e.tid { idx = j }
            j = j + 1
        }
        f64 x0 = _map_time(e.start_ns, tmin, tmax, left, width)
        f64 x1 = _map_time(e.end_ns, tmin, tmax, left, width)
        f64 ww = x1 - x0
        if ww < 1.0 { ww = 1.0 }
        int ry = top + idx * lane_h + 3
        int rh = lane_h - 6
        int pc = e.cpu_id % phys
        string fill = cpu_color(e.cpu_id, phys)
        if e.cpu_id >= phys { fill = f"url(#ht{e.cpu_id})" }
        int htflag = 0
        if e.cpu_id >= phys { htflag = 1 }
        s = s + f"<rect x=\"{x0:.1f}\" y=\"{ry}\" width=\"{ww:.1f}\" height=\"{rh}\" rx=\"1\" fill=\"{fill}\"><title>Thread {_tl_escape(e.tname)} (TID {e.tid}) on CPU {e.cpu_id} [core {pc}, HT={htflag}]</title></rect>"
        i = i + 1
    }

    // time axis + ticks
    s = s + f"<line x1=\"{left}\" y1=\"{plot_bottom}\" x2=\"{right}\" y2=\"{plot_bottom}\" stroke=\"#333333\" stroke-width=\"1\"/>"
    int nt = 5
    int ti = 0
    while ti <= nt {
        i64 tv = tmin + (tmax - tmin) * (ti as i64) / (nt as i64)
        f64 tx = _map_time(tv, tmin, tmax, left, width)
        s = s + f"<line x1=\"{tx:.1f}\" y1=\"{plot_bottom}\" x2=\"{tx:.1f}\" y2=\"{plot_bottom + 4}\" stroke=\"#333333\" stroke-width=\"1\"/>"
        string tl = plotfmt.fmt_time(tv - tmin)
        s = s + f"<text x=\"{tx:.1f}\" y=\"{plot_bottom + 16}\" font-size=\"10\" font-family=\"monospace\" text-anchor=\"middle\" fill=\"#555555\">{tl}</text>"
        ti = ti + 1
    }

    // legend: CPU -> color/style (one row per unique CPU seen)
    int leg_y = plot_bottom + 32
    s = s + f"<text x=\"{left}\" y=\"{leg_y}\" font-size=\"11\" font-family=\"sans-serif\" font-weight=\"bold\" fill=\"#000000\">Legend (CPU -&gt; physical core)</text>"
    ci = 0
    while ci < cpus.length {
        int cpu = cpus[ci]
        int pc = cpu % phys
        int ry = leg_y + 8 + ci * 16
        string fill = cpu_color(cpu, phys)
        if cpu >= phys { fill = f"url(#ht{cpu})" }
        s = s + f"<rect x=\"{left}\" y=\"{ry}\" width=\"14\" height=\"10\" fill=\"{fill}\" stroke=\"#999999\" stroke-width=\"0.5\"/>"
        string htmark = ""
        if cpu >= phys { htmark = " (HT)" }
        s = s + f"<text x=\"{left + 20}\" y=\"{ry + 9}\" font-size=\"10\" font-family=\"monospace\" fill=\"#333333\">CPU {cpu}  core {pc}{htmark}</text>"
        ci = ci + 1
    }

    int svgh = leg_y + 8 + cpus.length * 16 + 12
    if svgh < h { svgh = h }
    string head = f"<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"{w}\" height=\"{svgh}\">"
    string bg = "<rect width=\"100%\" height=\"100%\" fill=\"#ffffff\"/>"
    return head + bg + defs + s + "</svg>"
}
