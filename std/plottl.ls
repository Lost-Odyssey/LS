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

// ---- Color themes ----
//
// cpu_theme_color(cpu, phys, theme): physical core -> hue/position, HT sibling
// dimmer. theme one of: "rainbow" (default, == cpu_color), "viridis"
// (colorblind-safe), "warm", "cool".

fn _lerp(f64 a, f64 b, f64 t) -> f64 { return a + (b - a) * t }

// viridis-like colormap (5 anchors), frac in [0,1]; mul dims for HT siblings.
fn _viridis_hex(f64 frac, f64 mul) -> string {
    f64 f = frac
    if f < 0.0 { f = 0.0 }
    if f > 1.0 { f = 1.0 }
    f64 r = 0.0
    f64 g = 0.0
    f64 b = 0.0
    if f < 0.25 {
        f64 t = f / 0.25
        r = _lerp(68.0, 59.0, t); g = _lerp(1.0, 82.0, t); b = _lerp(84.0, 139.0, t)
    } else if f < 0.5 {
        f64 t = (f - 0.25) / 0.25
        r = _lerp(59.0, 33.0, t); g = _lerp(82.0, 144.0, t); b = _lerp(139.0, 140.0, t)
    } else if f < 0.75 {
        f64 t = (f - 0.5) / 0.25
        r = _lerp(33.0, 93.0, t); g = _lerp(144.0, 200.0, t); b = _lerp(140.0, 99.0, t)
    } else {
        f64 t = (f - 0.75) / 0.25
        r = _lerp(93.0, 253.0, t); g = _lerp(200.0, 231.0, t); b = _lerp(99.0, 37.0, t)
    }
    int ri = (r * mul + 0.5) as int
    int gi = (g * mul + 0.5) as int
    int bi = (b * mul + 0.5) as int
    return plotfmt.rgb_to_hex(ri, gi, bi)
}

fn cpu_theme_color(int cpu_id, int total_physical, string theme) -> string {
    if theme == "rainbow" { return cpu_color(cpu_id, total_physical) }
    int pc = physical_core(cpu_id, total_physical)
    bool ht = is_ht_sibling(cpu_id, total_physical)
    f64 frac = 0.0
    if total_physical > 1 { frac = (pc as f64) / (total_physical as f64) }
    if theme == "viridis" {
        f64 mul = 1.0
        if ht { mul = 0.62 }
        return _viridis_hex(frac, mul)
    }
    f64 hue = frac * 285.0          // default-ish
    if theme == "warm" { hue = frac * 55.0 }
    else if theme == "cool" { hue = 180.0 + frac * 120.0 }
    f64 sat = 0.65
    f64 val = 0.78
    if ht { sat = 0.58; val = 0.56 }
    return plotfmt.hsv_to_hex(hue, sat, val)
}

// ---- Ergonomic topology constructor: total logical CPUs + HT flag ----
fn topology2(int total_cpus, bool hyperthreading) -> CpuTopology {
    int physical = total_cpus
    if hyperthreading { physical = total_cpus / 2 }
    if physical < 1 { physical = 1 }
    return topology(total_cpus, physical)
}

// ---- ascending in-place sort of vec(int) (small n) ----
fn _sort_int(&!vec(int) v) {
    int n = v.length
    int i = 0
    while i < n {
        int j = 0
        while j < n - 1 - i {
            if v[j] > v[j + 1] {
                int tmp = v[j]
                v[j] = v[j + 1]
                v[j + 1] = tmp
            }
            j = j + 1
        }
        i = i + 1
    }
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

fn cpu_timeline_svg(vec(CpuSchedEvent) events, CpuTopology topo, int w, int h, string theme) -> string {
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
            string dim = cpu_theme_color(cpu, phys, theme)
            string acc = cpu_theme_color(cpu % phys, phys, theme)
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
        string fill = cpu_theme_color(e.cpu_id, phys, theme)
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

    // legend: CPU -> color/style (unique CPUs, sorted ascending)
    _sort_int(&!cpus)
    int leg_y = plot_bottom + 32
    s = s + f"<text x=\"{left}\" y=\"{leg_y}\" font-size=\"11\" font-family=\"sans-serif\" font-weight=\"bold\" fill=\"#000000\">Legend (CPU)</text>"
    ci = 0
    while ci < cpus.length {
        int cpu = cpus[ci]
        int ry = leg_y + 8 + ci * 16
        string fill = cpu_theme_color(cpu, phys, theme)
        if cpu >= phys { fill = f"url(#ht{cpu})" }
        s = s + f"<rect x=\"{left}\" y=\"{ry}\" width=\"14\" height=\"10\" fill=\"{fill}\" stroke=\"#999999\" stroke-width=\"0.5\"/>"
        string htmark = ""
        if cpu >= phys { htmark = " (HT)" }
        s = s + f"<text x=\"{left + 20}\" y=\"{ry + 9}\" font-size=\"10\" font-family=\"monospace\" fill=\"#333333\">CPU {cpu}{htmark}</text>"
        ci = ci + 1
    }

    int svgh = leg_y + 8 + cpus.length * 16 + 12
    if svgh < h { svgh = h }
    string head = f"<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"{w}\" height=\"{svgh}\">"
    string bg = "<rect width=\"100%\" height=\"100%\" fill=\"#ffffff\"/>"
    return head + bg + defs + s + "</svg>"
}

// ---- HTML wrapper: fixed lane column + horizontally-scrollable wide SVG ----
//
// Pure scheme "A": a wide SVG strip lives in an overflow-x:auto container so the
// browser provides a native horizontal scrollbar; the thread-name column sits
// outside the scroll area and stays fixed. Zero JS, self-contained single file.

fn cpu_timeline_html(vec(CpuSchedEvent) events, CpuTopology topo, int chart_width, string theme) -> string {
    int n = events.length
    int phys = topo.total_physical
    if phys < 1 { phys = 1 }

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

    int lane_h = 28
    int axis_h = 26
    int cw = chart_width
    if cw < 200 { cw = 200 }
    int pad = 8                       // left/right inset so edge ticks/events aren't clipped
    int svg_h = axis_h + lane_tids.length * lane_h + 4

    // <defs> HT stripe patterns
    string defs = "<defs>"
    int ci = 0
    while ci < cpus.length {
        int cpu = cpus[ci]
        if cpu >= phys {
            string dim = cpu_theme_color(cpu, phys, theme)
            string acc = cpu_theme_color(cpu % phys, phys, theme)
            defs = defs + f"<pattern id=\"ht{cpu}\" width=\"6\" height=\"6\" patternUnits=\"userSpaceOnUse\" patternTransform=\"rotate(45)\"><rect width=\"6\" height=\"6\" fill=\"{dim}\"/><line x1=\"0\" y1=\"0\" x2=\"0\" y2=\"6\" stroke=\"{acc}\" stroke-width=\"1.5\"/></pattern>"
        }
        ci = ci + 1
    }
    defs = defs + "</defs>"

    // wide SVG strip (no left labels): top time axis + row separators + rects
    string strip = ""
    strip = strip + f"<line x1=\"0\" y1=\"{axis_h}\" x2=\"{cw}\" y2=\"{axis_h}\" stroke=\"#333333\" stroke-width=\"1\"/>"
    int nt = 8
    int ti = 0
    while ti <= nt {
        i64 tv = tmin + (tmax - tmin) * (ti as i64) / (nt as i64)
        f64 tx = _map_time(tv, tmin, tmax, pad, cw - 2 * pad)
        strip = strip + f"<line x1=\"{tx:.1f}\" y1=\"{axis_h - 4}\" x2=\"{tx:.1f}\" y2=\"{axis_h}\" stroke=\"#333333\" stroke-width=\"1\"/>"
        string tl = plotfmt.fmt_time(tv - tmin)
        string anch = "middle"
        if ti == 0 { anch = "start" }
        else if ti == nt { anch = "end" }
        strip = strip + f"<text x=\"{tx:.1f}\" y=\"{axis_h - 9}\" font-size=\"10\" font-family=\"monospace\" text-anchor=\"{anch}\" fill=\"#555555\">{tl}</text>"
        ti = ti + 1
    }
    int li = 0
    while li < lane_tids.length {
        int ry = axis_h + li * lane_h
        strip = strip + f"<line x1=\"0\" y1=\"{ry}\" x2=\"{cw}\" y2=\"{ry}\" stroke=\"#f0f0f0\" stroke-width=\"0.5\"/>"
        li = li + 1
    }
    i = 0
    while i < n {
        CpuSchedEvent e = events[i]
        int idx = 0
        int j = 0
        while j < lane_tids.length {
            if lane_tids[j] == e.tid { idx = j }
            j = j + 1
        }
        f64 x0 = _map_time(e.start_ns, tmin, tmax, pad, cw - 2 * pad)
        f64 x1 = _map_time(e.end_ns, tmin, tmax, pad, cw - 2 * pad)
        f64 ww = x1 - x0
        if ww < 1.0 { ww = 1.0 }
        int ry = axis_h + idx * lane_h + 4
        int rh = lane_h - 8
        int pc = e.cpu_id % phys
        string fill = cpu_theme_color(e.cpu_id, phys, theme)
        if e.cpu_id >= phys { fill = f"url(#ht{e.cpu_id})" }
        int htflag = 0
        if e.cpu_id >= phys { htflag = 1 }
        strip = strip + f"<rect x=\"{x0:.1f}\" y=\"{ry}\" width=\"{ww:.1f}\" height=\"{rh}\" rx=\"1\" fill=\"{fill}\"><title>Thread {_tl_escape(e.tname)} (TID {e.tid}) on CPU {e.cpu_id} [core {pc}, HT={htflag}]</title></rect>"
        i = i + 1
    }
    string svg = f"<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"{cw}\" height=\"{svg_h}\">" + defs + strip + "</svg>"

    // left fixed lane column (aligned row heights)
    string lanes_html = "<div class=\"lhead\"></div>"
    li = 0
    while li < lane_tids.length {
        string lname = lane_names[li]
        int tid = lane_tids[li]
        lanes_html = lanes_html + f"<div class=\"lane\">{_tl_escape(lname)} ({tid})</div>"
        li = li + 1
    }

    // legend (CSS stripes mimic HT pattern; unique CPUs sorted ascending)
    _sort_int(&!cpus)
    string legend = "<div class=\"legend\"><b>Legend (CPU)</b><br>"
    ci = 0
    while ci < cpus.length {
        int cpu = cpus[ci]
        string style = ""
        if cpu >= phys {
            string dim = cpu_theme_color(cpu, phys, theme)
            string acc = cpu_theme_color(cpu % phys, phys, theme)
            style = f"background:repeating-linear-gradient(45deg,{dim},{dim} 3px,{acc} 3px,{acc} 4px)"
        } else {
            style = f"background:{cpu_theme_color(cpu, phys, theme)}"
        }
        string htmark = ""
        if cpu >= phys { htmark = " (HT)" }
        legend = legend + f"<span class=\"item\"><span class=\"sw\" style=\"{style}\"></span>CPU {cpu}{htmark}</span>"
        ci = ci + 1
    }
    legend = legend + "</div>"

    string css = "<style>body{font-family:sans-serif;margin:12px}.wrap{display:flex;border:1px solid #cccccc;width:fit-content;max-width:100%}.lanes{flex:0 0 150px;background:#fafafa;border-right:1px solid #cccccc}.lhead{height:26px;border-bottom:1px solid #eeeeee;box-sizing:border-box}.lane{height:28px;line-height:28px;padding:0 8px;font:11px monospace;white-space:nowrap;overflow:hidden;border-bottom:1px solid #f5f5f5;box-sizing:border-box}.scroll{overflow-x:auto;overflow-y:hidden}.sw{display:inline-block;width:16px;height:11px;margin-right:5px;border:1px solid #999999;vertical-align:middle}.item{margin-right:18px;white-space:nowrap}.legend{margin-top:10px;font:12px sans-serif}.hint{color:#888888;font:11px sans-serif;margin-top:6px}</style>"

    string html = "<!DOCTYPE html><html><head><meta charset=\"utf-8\">" + css + "</head><body>"
    html = html + "<h3 style=\"margin:0 0 8px\">CPU Scheduling Timeline</h3>"
    html = html + "<div class=\"wrap\"><div class=\"lanes\">" + lanes_html + "</div>"
    html = html + "<div class=\"scroll\">" + svg + "</div></div>"
    html = html + legend
    html = html + "<div class=\"hint\">&larr; scroll horizontally to see the full timeline &rarr;</div>"
    html = html + "</body></html>"
    return html
}

// ---- HTML wrapper "B": drag-to-pan + wheel-to-zoom (inline JS, viewBox-style) ----
//
// A normal-width SVG whose event rects live in a clipped <g> that JS transforms
// with translate+scaleX. Thread labels and row separators stay fixed; the time
// axis is redrawn by JS on each transform so labels never stretch. Self-contained
// single file, no external deps.

fn cpu_timeline_html_zoom(vec(CpuSchedEvent) events, CpuTopology topo, int w, int h, string theme) -> string {
    int n = events.length
    int phys = topo.total_physical
    if phys < 1 { phys = 1 }

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

    int left = 120
    int top = 50
    int lane_h = 28
    int plotw = w - left - 20
    if plotw < 100 { plotw = 100 }
    int ploth = lane_tids.length * lane_h
    int bottom = top + ploth
    int svgh = bottom + 30
    if svgh < h { svgh = h }
    i64 span = tmax - tmin

    // defs: HT patterns + clip
    string defs = "<defs>"
    int ci = 0
    while ci < cpus.length {
        int cpu = cpus[ci]
        if cpu >= phys {
            string dim = cpu_theme_color(cpu, phys, theme)
            string acc = cpu_theme_color(cpu % phys, phys, theme)
            defs = defs + f"<pattern id=\"ht{cpu}\" width=\"6\" height=\"6\" patternUnits=\"userSpaceOnUse\" patternTransform=\"rotate(45)\"><rect width=\"6\" height=\"6\" fill=\"{dim}\"/><line x1=\"0\" y1=\"0\" x2=\"0\" y2=\"6\" stroke=\"{acc}\" stroke-width=\"1.5\"/></pattern>"
        }
        ci = ci + 1
    }
    defs = defs + f"<clipPath id=\"clip\"><rect x=\"{left}\" y=\"{top}\" width=\"{plotw}\" height=\"{ploth}\"/></clipPath></defs>"

    // fixed layer: title, lane labels, plot border, row separators
    string fixed = ""
    int cx = left + plotw / 2
    fixed = fixed + f"<text x=\"{cx}\" y=\"24\" font-size=\"15\" font-family=\"sans-serif\" font-weight=\"bold\" text-anchor=\"middle\" fill=\"#000000\">CPU Scheduling Timeline (drag = pan, wheel = zoom, dbl-click = reset)</text>"
    fixed = fixed + f"<rect x=\"{left}\" y=\"{top}\" width=\"{plotw}\" height=\"{ploth}\" fill=\"#fafafa\" stroke=\"#333333\" stroke-width=\"1\"/>"
    int li = 0
    while li < lane_tids.length {
        string lname = lane_names[li]
        int tid = lane_tids[li]
        int ly = top + li * lane_h
        fixed = fixed + f"<text x=\"{left - 8}\" y=\"{ly + lane_h / 2 + 4}\" font-size=\"10\" font-family=\"monospace\" text-anchor=\"end\" fill=\"#333333\">{_tl_escape(lname)} ({tid})</text>"
        fixed = fixed + f"<line x1=\"{left}\" y1=\"{ly}\" x2=\"{left + plotw}\" y2=\"{ly}\" stroke=\"#eeeeee\" stroke-width=\"0.5\"/>"
        li = li + 1
    }

    // content layer (transformed by JS): event rects in content space [0,plotw]
    string content = ""
    i = 0
    while i < n {
        CpuSchedEvent e = events[i]
        int idx = 0
        int j = 0
        while j < lane_tids.length {
            if lane_tids[j] == e.tid { idx = j }
            j = j + 1
        }
        f64 cx0 = ((e.start_ns - tmin) as f64) / (span as f64) * (plotw as f64)
        f64 cx1 = ((e.end_ns - tmin) as f64) / (span as f64) * (plotw as f64)
        f64 ww = cx1 - cx0
        if ww < 0.5 { ww = 0.5 }
        int ry = top + idx * lane_h + 4
        int rh = lane_h - 8
        int pc = e.cpu_id % phys
        string fill = cpu_theme_color(e.cpu_id, phys, theme)
        if e.cpu_id >= phys { fill = f"url(#ht{e.cpu_id})" }
        int htflag = 0
        if e.cpu_id >= phys { htflag = 1 }
        content = content + f"<rect x=\"{cx0:.2f}\" y=\"{ry}\" width=\"{ww:.2f}\" height=\"{rh}\" rx=\"1\" fill=\"{fill}\"><title>Thread {_tl_escape(e.tname)} (TID {e.tid}) on CPU {e.cpu_id} [core {pc}, HT={htflag}]</title></rect>"
        i = i + 1
    }

    // legend (HTML, CSS stripes; unique CPUs sorted ascending)
    _sort_int(&!cpus)
    string legend = "<div class=\"legend\"><b>Legend (CPU)</b><br>"
    ci = 0
    while ci < cpus.length {
        int cpu = cpus[ci]
        string style = ""
        if cpu >= phys {
            string dim = cpu_theme_color(cpu, phys, theme)
            string acc = cpu_theme_color(cpu % phys, phys, theme)
            style = f"background:repeating-linear-gradient(45deg,{dim},{dim} 3px,{acc} 3px,{acc} 4px)"
        } else {
            style = f"background:{cpu_theme_color(cpu, phys, theme)}"
        }
        string htmark = ""
        if cpu >= phys { htmark = " (HT)" }
        legend = legend + f"<span class=\"item\"><span class=\"sw\" style=\"{style}\"></span>CPU {cpu}{htmark}</span>"
        ci = ci + 1
    }
    legend = legend + "</div>"

    // PLOT params for the JS (built via concat to keep literal braces out of f-strings)
    string plotvar = "<script>var PLOT={tmin:" + f"{tmin}" + ",span:" + f"{span}" + ",left:" + f"{left}" + ",top:" + f"{top}" + ",width:" + f"{plotw}" + ",vbw:" + f"{w}" + "};</script>"

    // pan/zoom JS (single quotes only -> no escaping needed in this string literal)
    string js = "<script>(function(){var P=PLOT;var svg=document.getElementById('chart');var content=document.getElementById('content');var axis=document.getElementById('axis');var tx=0,sx=1;function fmt(ns){var a=Math.abs(ns);if(a>=1e9)return (ns/1e9).toFixed(2)+'s';if(a>=1e6)return (ns/1e6).toFixed(1)+'ms';if(a>=1e3)return (ns/1e3).toFixed(0)+'us';return ns.toFixed(0)+'ns';}function clamp(){var mn=P.width*(1-sx);if(tx>0)tx=0;if(tx<mn)tx=mn;}function drawAxis(){while(axis.firstChild)axis.removeChild(axis.firstChild);var SVGNS='http://www.w3.org/2000/svg';var nn=8;for(var i=0;i<=nn;i++){var sX=P.width*i/nn;var cX=(sX-tx)/sx;var t=P.tmin+cX/P.width*P.span;var x=P.left+sX;var ln=document.createElementNS(SVGNS,'line');ln.setAttribute('x1',x);ln.setAttribute('y1',P.top-4);ln.setAttribute('x2',x);ln.setAttribute('y2',P.top);ln.setAttribute('stroke','#333');axis.appendChild(ln);var tt=document.createElementNS(SVGNS,'text');tt.setAttribute('x',x);tt.setAttribute('y',P.top-8);tt.setAttribute('font-size','10');tt.setAttribute('font-family','monospace');tt.setAttribute('text-anchor','middle');tt.setAttribute('fill','#555');tt.textContent=fmt(t-P.tmin);axis.appendChild(tt);}}function apply(){clamp();content.setAttribute('transform','translate('+(P.left+tx)+' 0) scale('+sx+' 1)');drawAxis();}var drag=false,lx=0;svg.addEventListener('mousedown',function(e){drag=true;lx=e.clientX;});window.addEventListener('mouseup',function(){drag=false;});window.addEventListener('mousemove',function(e){if(!drag)return;var r=svg.getBoundingClientRect();var sf=P.vbw/r.width;tx+=(e.clientX-lx)*sf;lx=e.clientX;apply();});svg.addEventListener('wheel',function(e){e.preventDefault();var r=svg.getBoundingClientRect();var sf=P.vbw/r.width;var mx=(e.clientX-r.left)*sf-P.left;if(mx<0)mx=0;if(mx>P.width)mx=P.width;var f=e.deltaY<0?1.15:1/1.15;var ns=sx*f;if(ns<1)ns=1;if(ns>60)ns=60;tx=mx-((mx-tx)/sx)*ns;sx=ns;apply();},{passive:false});svg.addEventListener('dblclick',function(){tx=0;sx=1;apply();});apply();})();</script>"

    string css = "<style>body{font-family:sans-serif;margin:12px}svg{border:1px solid #ddd;cursor:grab;user-select:none;width:100%;height:auto;display:block}svg:active{cursor:grabbing}.sw{display:inline-block;width:16px;height:11px;margin-right:5px;border:1px solid #999;vertical-align:middle}.item{margin-right:18px;white-space:nowrap}.legend{margin-top:10px;font:12px sans-serif}</style>"

    string svg = f"<svg id=\"chart\" xmlns=\"http://www.w3.org/2000/svg\" viewBox=\"0 0 {w} {svgh}\" width=\"{w}\" height=\"{svgh}\">" + defs + fixed + f"<g id=\"vp\" clip-path=\"url(#clip)\"><g id=\"content\">" + content + "</g></g><g id=\"axis\"></g></svg>"

    string html = "<!DOCTYPE html><html><head><meta charset=\"utf-8\">" + css + "</head><body>"
    html = html + svg + legend + plotvar + js
    html = html + "</body></html>"
    return html
}
