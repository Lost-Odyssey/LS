// std.sim.core.engine — engine 1: analytical bottleneck model (MVP).
//
// Fast, no per-cycle simulation. For a loop kernel's steady state the throughput
// limiter is II = max(ResMII, RecMII) (docs §4.2/§7.1):
//   ResMII  = port pressure: max over ports of (uops on port / port count)
//   frontend = total uops / issue width
//   critical = single-iteration latency (longest dependency chain) — reported as
//              latency info; for independent iterations it overlaps away.
// MVP classifies the throughput bottleneck = max(ResMII, frontend); RecMII
// (loop-carried recurrence) comes once the dep builder marks distances.
//
// Port loads are kept as fixed-point integers scaled by SCALE (=12, divisible by
// every candidate port-count 1/2/3/4/6) so a uop spreading over k ports adds
// SCALE/k exactly — no floats needed in the hot path. f64 used only for display.
//
// Pure LS; consumes only the neutral ir.Uop/ir.Port (arch-independent).

import std.core.vec
import std.core.str
import sim.core.ir as ir

// SCALE = 12 = lcm(1,2,3,4,6): SCALE/k is integer for every realistic port-set size.
def scale() -> int { return 12 }

struct Bottleneck {
    Str kind          // "port-bound(pN)" / "frontend-bound" / "recurrence-bound"
    int port_id       // saturated port (-1 if frontend/recurrence)
    int res_mii_x     // max port pressure (x SCALE) — the throughput/port bound
    int rec_mii_x     // loop-carried recurrence bound (x SCALE); 0 if no recurrence
    int ii_x          // initiation interval = max(res_mii_x, rec_mii_x) — the ACTUAL
                      // steady-state cyc/iter (matches llvm-mca Total Cycles / iters)
    int frontend_x    // (uops * SCALE) / width
    int critical      // single-iteration critical-path latency (cycles)
    int total_uops
}

// fractional port load x SCALE: each uop adds SCALE/k to each of its k ports
def port_pressure(&Vec(ir.Uop) uops, int nports) -> Vec(int) {
    Vec(int) pload = {}
    for p in 0..nports { pload.push(0) }
    for u in &uops {
        int k = u.port_mask.len()
        if k > 0 {
            int share = scale() / k
            for pidx in 0..k {
                int pid = u.port_mask.get!(pidx)
                int cur = pload.get!(pid)
                pload.set(pid, cur + share)
            }
        }
    }
    return pload
}

// popcount of the low `nports` bits of mask
def _popcount(int mask, int nports) -> int {
    int c = 0
    for p in 0..nports {
        if (mask & (1 << p)) != 0 { c = c + 1 }
    }
    return c
}

// Optimal fractional port-pressure bound — the REAL ResMII, matching llvm-mca's
// "Ports" bound. Naive per-port even-split (port_pressure) over-counts: a uop that
// CAN use a saturated port gets charged a share of it even when an optimal schedule
// would route it elsewhere. The exact fractional bound is, by LP duality / Hall
// density,  max over port-subsets S of (uops confined to S) / |S|  — a uop is
// "confined to S" when its whole port_mask ⊆ S, so it MUST land inside S. This both
// fixes spread kernels (4 ALU ops over {p0,p1,p5} -> 4/3 = 1.33, not 2.0) and keeps
// shuffle-saturated kernels exact (5 ops forced to {p5} -> 5/1 = 5.0, a flexible
// vpor on {0,1,5} can't lower it). Returns x SCALE, comparable to frontend_x.
def flow_port_bound(&Vec(ir.Uop) uops, int nports) -> int {
    int nsub = 1 << nports
    int best_cnt = 0
    int best_ss = 1
    for mask in 1..nsub {
        int ss = _popcount(mask, nports)
        int cnt = 0
        for u in &uops {                 // borrow-for-in over struct elements (POD reads)
            int k = u.port_mask.len()
            if k > 0 {
                bool conf = true
                for i in 0..k {
                    int pid = u.port_mask.get!(i)
                    if (mask & (1 << pid)) == 0 { conf = false }
                }
                if conf { cnt = cnt + 1 }
            }
        }
        // cnt/ss > best_cnt/best_ss  (cross-multiply, no division)
        if cnt * best_ss > best_cnt * ss { best_cnt = cnt; best_ss = ss }
    }
    return (best_cnt * scale()) / best_ss
}

// per-uop longest-latency dependency chain (DP; uops in program/topological order).
// cp[i] = result-ready time of uop i measured from the start of its (replicated) stream.
def critical_path_vec(&Vec(ir.Uop) uops) -> Vec(int) {
    int n = uops.len()
    Vec(int) cp = {}
    for i in 0..n { cp.push(0) }
    for i in 0..n {
        // Named borrow first: reaching `uops.get_ref(i).src_uops` directly clones
        // the Vec(int) field into a leaking temp (see ports.build_uops note).
        &ir.Uop u = uops.get_ref(i)
        int base = 0
        int m = u.src_uops.len()
        for j in 0..m {
            int s = u.src_uops.get!(j)
            int cs = cp.get!(s)
            if cs > base { base = cs }
        }
        cp.set(i, base + u.latency)
    }
    return cp
}

// longest-latency dependency chain (single-iteration critical path).
def critical_path(&Vec(ir.Uop) uops) -> int {
    Vec(int) cp = critical_path_vec(uops)
    int best = 0
    int n = cp.len()
    for i in 0..n { int v = cp.get!(i); if v > best { best = v } }
    return best
}

// ---- RecMII: loop-carried recurrence bound -----------------------------------
// A reduction/scan with a single accumulator is throughput-light (low ResMII) yet
// latency-bound across iterations: the accumulator's value must complete before the
// next iteration can update it. llvm-mca's Block RThroughput MISSES this (it is the
// port bound); its Total Cycles SHOWS it. RecMII closes that gap.
//
// We replicate the body K iterations with the loop-carried edges WIRED (iteration t's
// reader consumes iteration t-1's writer), run the per-uop critical path, and read the
// steady-state recurrence delay = cp[writer@iter K-1] - cp[writer@iter K-2] (the extra
// cycles each iteration's carried writer is pushed back by the chain). Max over carried
// registers. Distance is always 1, so this equals the classic Σlatency/Σdistance for
// the single-distance recurrences these kernels form (self-accumulator and short chains).
def replicate_carried(&Vec(ir.Uop) uops, &Vec(ir.Carried) edges, int iters) -> Vec(ir.Uop) {
    int n = uops.len()
    int ne = edges.len()
    Vec(ir.Uop) out = {}
    for t in 0..iters {
        int off = t * n
        for i in 0..n {
            &ir.Uop u = uops.get_ref(i)
            Vec(int) pm = {}
            int km = u.port_mask.len()
            for k in 0..km { pm.push(u.port_mask.get!(k)) }
            Vec(int) sr = {}
            int sm = u.src_uops.len()
            for k in 0..sm { sr.push(u.src_uops.get!(k) + off) }
            if t > 0 {
                for e in 0..ne {
                    &ir.Carried c = edges.get_ref(e)
                    if c.reader == i { sr.push(c.writer + off - n) }  // previous iter's writer
                }
            }
            out.push(ir.uop(u.inst_id, pm, u.latency, sr, u.is_load, u.is_store))
        }
    }
    return out
}

// RecMII in cycles x SCALE (comparable to res_mii_x). 0 when there is no recurrence.
def rec_mii_x(&Vec(ir.Uop) uops, &Vec(ir.Carried) edges) -> int {
    int ne = edges.len()
    if ne == 0 { return 0 }
    int n = uops.len()
    int K = 6                       // enough iterations to settle the steady recurrence
    Vec(ir.Uop) rep = replicate_carried(uops, edges, K)
    Vec(int) cp = critical_path_vec(&rep)
    int best = 0
    for e in 0..ne {
        &ir.Carried c = edges.get_ref(e)
        int w = c.writer
        int a = cp.get!(w + (K - 1) * n)
        int b = cp.get!(w + (K - 2) * n)
        int d = a - b
        if d > best { best = d }
    }
    return best * scale()
}

def analyze(&Vec(ir.Uop) uops, int nports, int fe_width) -> Bottleneck {
    // naive per-port demand: used only to pick a representative bottleneck port
    // (argmax) and to draw the report bars. The ResMII value comes from the exact
    // optimal flow bound below.
    Vec(int) pload = port_pressure(uops, nports)
    int parg = 0
    int pdemand = 0
    for p in 0..nports {
        int lp = pload.get!(p)
        if lp > pdemand { pdemand = lp; parg = p }
    }
    int pmax = flow_port_bound(uops, nports)        // accurate ResMII (llvm-mca "Ports")
    int fe_x = (uops.len() * scale()) / fe_width
    int cp = critical_path(uops)

    Str kind = ""
    int pid = -1
    if pmax >= fe_x {
        kind = f"port-bound(p{parg})"
        pid = parg
    } else {
        kind = "frontend-bound"
    }
    // res_mii layer only (no recurrence). ii_x defaults to the port/frontend bound.
    int base_ii = pmax
    if fe_x > base_ii { base_ii = fe_x }
    return Bottleneck { kind: kind, port_id: pid, res_mii_x: pmax, rec_mii_x: 0,
                        ii_x: base_ii, frontend_x: fe_x, critical: cp,
                        total_uops: uops.len() }
}

// recurrence-aware analysis: combines the port/frontend bound with RecMII so the
// reported II (and verdict) matches the ACTUAL steady-state cost (llvm-mca Total/iters)
// for reduction/scan kernels, not just the optimistic throughput bound.
def analyze_rec(&Vec(ir.Uop) uops, &Vec(ir.Carried) edges, int nports, int fe_width) -> Bottleneck {
    Bottleneck b = analyze(uops, nports, fe_width)
    int rec = rec_mii_x(uops, edges)
    b.rec_mii_x = rec
    int ii = b.res_mii_x
    if b.frontend_x > ii { ii = b.frontend_x }
    if rec > ii {
        ii = rec
        b.kind = "recurrence-bound"
        b.port_id = -1
    }
    b.ii_x = ii
    return b
}

// ---- text report: bottleneck verdict + ASCII port-pressure bars ----
def _bar(int v, int vmax) -> Str {
    int width = 28
    int fill = 0
    if vmax > 0 { fill = (v * width) / vmax }
    if fill > width { fill = width }
    Str s = ""
    for i in 0..width {
        if i < fill { s = f"{s}#" } else { s = f"{s}." }
    }
    return s
}

// integer x SCALE -> "N.NN" decimal string
def _dec(int x) -> Str {
    int whole = x / scale()
    int frac100 = ((x - whole * scale()) * 100) / scale()
    Str fs = itoa(frac100)
    if frac100 < 10 { fs = f"0{fs}" }
    return f"{itoa(whole)}.{fs}"
}
def itoa(int n) -> Str {
    if n == 0 { return "0" }
    int v = n
    bool neg = false
    if v < 0 { neg = true; v = 0 - v }
    Str out = ""
    while v > 0 {
        int d = v % 10
        Str ds = ""
        ds.push_byte(48 + d)
        out = f"{ds}{out}"
        v = v / 10
    }
    if neg { out = f"-{out}" }
    return out
}

def report(&Bottleneck b, &Vec(ir.Uop) uops, &Vec(ir.Port) ports) -> Str {
    int nports = ports.len()
    Vec(int) pload = port_pressure(uops, nports)
    int pmax = b.res_mii_x

    Str out = "=== engine-1 bottleneck analysis ===\n"
    out = f"{out}  uops={b.total_uops}  ResMII={_dec(b.res_mii_x)}  frontend={_dec(b.frontend_x)}  crit-path={b.critical}c\n"
    // recurrence layer: when a loop-carried dependency exists, RecMII (and the resulting
    // II = max(ResMII, RecMII)) is the ACTUAL steady-state cyc/iter — the port bound alone
    // (ResMII) under-reports a single-accumulator reduction by up to its latency.
    if b.rec_mii_x > 0 {
        out = f"{out}  RecMII={_dec(b.rec_mii_x)} (loop-carried)  ->  II=max(ResMII,RecMII)={_dec(b.ii_x)} cyc/iter\n"
    }
    out = f"{out}  VERDICT: {b.kind}\n\n  port pressure (uops/port, fractional):\n"
    // borrow-for-in over ports (avoids the get_ref().field clone-leak); track index.
    int p = 0
    for pt in &ports {
        int lp = pload.get!(p)
        Str lbl = pt.label.pad_right(18, 32)
        Str bar = _bar(lp, pmax)
        Str mark = ""
        if p == b.port_id { mark = "  <== bottleneck" }
        out = f"{out}    {lbl} {bar} {_dec(lp)}{mark}\n"
        p = p + 1
    }
    return out
}
