// sim.core.engine2 — engine 2: per-cycle out-of-order simulation (plan §4.2).
//
// Where engine-1 gives the analytical bottleneck, engine-2 produces an actual
// cycle timeline: a greedy oldest-ready-first list scheduler advances cycle by
// cycle (wake ready uops -> issue to a free port -> execute -> write back) and
// records a UopTrace per uop for the Gantt view. Running K independent iterations
// recovers the steady-state throughput, which should match engine-1's ResMII —
// a cross-check between the two engines.
//
// Pure LS, zero compiler changes (same discipline as lib/oran).

import std.core.vec
import std.core.str
import sim.core.ir as ir
import sim.core.engine as engine

// greedy list scheduler with a front-end delivery cap. `fe_per_cycle` is the most
// uops the front-end hands the back-end per cycle (= sim.intel.frontend delivery);
// pass a high value (>= nports) to disable the cap (back-end-only). Returns one
// UopTrace per uop (program order == index).
def simulate_fe(&Vec(ir.Uop) uops, int nports, int fe_per_cycle) -> Vec(ir.UopTrace) {
    int n = uops.len()
    Vec(int) done = {}      // cycle each uop's result is ready; -1 = not issued
    Vec(bool) issued = {}
    Vec(ir.UopTrace) tr = {}
    for i in 0..n {
        done.push(-1)
        issued.push(false)
        // trace.uop_id carries the source INST index (uops.inst_id), so renderers map
        // a uop row back to its instruction even after multi-uop expansion.
        tr.push(ir.uoptrace(uops.get_ref(i).inst_id, 0, 0, 0, -1))
    }
    int remaining = n
    int cyc = 0
    int guard = 0
    while remaining > 0 {
        Vec(bool) busy = {}            // one issue slot per port per cycle
        for p in 0..nports { busy.push(false) }
        int fe_left = fe_per_cycle    // front-end delivery budget this cycle
        for i in 0..n {
            if fe_left > 0 {
                if !issued.get!(i) {
                    &ir.Uop u = uops.get_ref(i)
                    bool ready = true
                    int rdyc = 0       // max source-done cycle = ready cycle
                    int m = u.src_uops.len()
                    for j in 0..m {
                        int s = u.src_uops.get!(j)
                        int ds = done.get!(s)
                        if ds < 0 { ready = false }
                        else {
                            if ds > cyc { ready = false }
                            if ds > rdyc { rdyc = ds }
                        }
                    }
                    if ready {
                        int chosen = -1    // first free port in the mask
                        int km = u.port_mask.len()
                        for k in 0..km {
                            int p = u.port_mask.get!(k)
                            if chosen < 0 { if !busy.get!(p) { chosen = p } }
                        }
                        if chosen >= 0 {
                            busy.set(chosen, true)
                            issued.set(i, true)
                            int fin = cyc + u.latency
                            done.set(i, fin)
                            remaining = remaining - 1
                            fe_left = fe_left - 1
                            tr.set(i, ir.uoptrace(u.inst_id, rdyc, cyc, fin, chosen))
                        }
                    }
                }
            }
        }
        cyc = cyc + 1
        guard = guard + 1
        if guard > 200000 { remaining = 0 }   // safety against a stuck schedule
    }
    return tr
}

// back-end-only schedule (front-end never the limit).
def simulate(&Vec(ir.Uop) uops, int nports) -> Vec(ir.UopTrace) {
    return simulate_fe(uops, nports, nports)
}

def total_cycles(&Vec(ir.UopTrace) tr) -> int {
    int mx = 0
    for t in &tr {
        if t.cycle_done > mx { mx = t.cycle_done }
    }
    return mx
}

// replicate the kernel `iters` times as independent iterations (src ids offset by
// iter*n) so the scheduler can overlap them and reveal steady-state throughput.
def replicate(&Vec(ir.Uop) uops, int iters) -> Vec(ir.Uop) {
    int n = uops.len()
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
            // keep the original inst_id (renderers need it); src ids are POSITION-offset.
            out.push(ir.uop(u.inst_id, pm, u.latency, sr, u.is_load, u.is_store))
        }
    }
    return out
}

// steady-state cycles per iteration: the average completion gap between the last
// uop of two iterations sampled in the steady MIDDLE of the run (iters/4 .. 3·iters/4),
// avoiding both the ramp-up and the drain tail. Rounded to nearest integer. Should
// equal engine-1 ResMII for a kernel with no loop-carried dependency. iters >= 8.
def steady_cycles_per_iter(&Vec(ir.Uop) uops, int nports, int iters) -> int {
    int n = uops.len()
    Vec(ir.Uop) rep = replicate(uops, iters)
    Vec(ir.UopTrace) tr = simulate(&rep, nports)
    if iters < 8 { return total_cycles(&tr) }
    int a = iters / 4
    int b = (iters * 3) / 4
    int span = b - a
    int ai = a * n + (n - 1)
    int bi = b * n + (n - 1)
    int dnum = tr.get_ref(bi).cycle_done - tr.get_ref(ai).cycle_done
    return (dnum + span / 2) / span     // round to nearest
}

// steady-state cycles per iteration in CENTI-CYCLES (x100) — the overlap-aware
// cost in the convention shared with sim.intel.branch. This is the "overlap"
// layer (plan §10.2 conclusion 3): engine-2 sits between the optimistic ResMII
// (full overlap) and the pessimistic critical path (no overlap), so for a kernel
// with a long serial dependency chain (e.g. a long SIMD reduction) it reports
// a higher, more realistic per-iteration cost than engine-1's ResMII.
def steady_cc_x100(&Vec(ir.Uop) uops, int nports, int iters) -> int {
    return steady_cycles_per_iter(uops, nports, iters) * 100
}

// recurrence-aware steady state: same per-cycle scheduler, but the replicated stream
// keeps the LOOP-CARRIED dependencies (iteration t's reader consumes iteration t-1's
// writer) instead of treating every iteration as independent. For a reduction/scan
// kernel this makes the simulation slow down to the recurrence rate — matching
// llvm-mca Total Cycles / iters — instead of collapsing to the optimistic port bound.
// With no carried edges it is identical to steady_cycles_per_iter (independent iters).
def steady_cycles_per_iter_carried(&Vec(ir.Uop) uops, &Vec(ir.Carried) edges, int nports, int iters) -> int {
    int n = uops.len()
    Vec(ir.Uop) rep = engine.replicate_carried(uops, edges, iters)
    Vec(ir.UopTrace) tr = simulate(&rep, nports)
    if iters < 8 { return total_cycles(&tr) }
    int a = iters / 4
    int b = (iters * 3) / 4
    int span = b - a
    int ai = a * n + (n - 1)
    int bi = b * n + (n - 1)
    int dnum = tr.get_ref(bi).cycle_done - tr.get_ref(ai).cycle_done
    return (dnum + span / 2) / span     // round to nearest
}

def steady_cc_carried_x100(&Vec(ir.Uop) uops, &Vec(ir.Carried) edges, int nports, int iters) -> int {
    return steady_cycles_per_iter_carried(uops, edges, nports, iters) * 100
}

// ---- text Gantt timeline (one row per uop; '#'=executing, '.'=ready-but-waiting) ----
def _mn(&Vec(ir.Inst) prog, int i) -> Str {
    // Bind the borrowed Inst first: `prog.get_ref(i).mnemonic.copy()` chained leaks
    // the cloned-field temp even when .mnemonic is a method receiver (compiler
    // field-on-get_ref clone limitation — same as in ports.build_uops).
    if i < prog.len() {
        &ir.Inst ins = prog.get_ref(i)
        return ins.mnemonic.copy()
    }
    return "uop"
}

def gantt(&Vec(ir.UopTrace) tr, &Vec(ir.Inst) prog) -> Str {
    int total = total_cycles(tr)
    Str out = f"=== engine-2 cycle timeline ({total} cycles) ===\n"
    Str ruler = "".pad_right(22, 32)
    for c in 0..(total + 1) {
        if c % 10 == 0 { ruler = f"{ruler}|" } else { ruler = f"{ruler} " }
    }
    out = f"{out}{ruler}\n"
    int n = tr.len()
    for i in 0..n {
        &ir.UopTrace t = tr.get_ref(i)
        int ready = t.cycle_ready
        int iss = t.cycle_issued
        int dn = t.cycle_done
        int port = t.port_used
        int de = dn                            // exec end; a lat-0 micro-op still
        if de <= iss { de = iss + 1 }          // occupies one issue slot -> draw 1 cell
        Str bar = ""
        for c in 0..(total + 1) {
            if c >= iss {
                if c < de { bar = f"{bar}#" } else { bar = f"{bar} " }
            } else {
                if c >= ready { bar = f"{bar}." } else { bar = f"{bar} " }
            }
        }
        Str mn = _mn(prog, t.uop_id).pad_right(10, 32)
        Str lead = f"  u{i} {mn} p{port} "
        out = f"{out}{lead.pad_right(22, 32)}{bar}\n"
    }
    return out
}

// per-iteration marker: 0-9 then A-.. (so distinct iterations are visually separable)
def _iter_char(int it) -> Str {
    Str s = ""
    if it < 10 { s.push_byte(48 + it) }        // '0'..'9'
    else { s.push_byte(65 + (it - 10)) }       // 'A'..
    return s
}

// ---- multi-iteration cycle timeline (look several iterations ahead) -------------
// Replicate the kernel `iters` times as INDEPENDENT iterations (src ids offset by
// iter*n, same as replicate()/steady_cycles_per_iter) and show the OVERLAPPED
// out-of-order schedule the scheduler actually runs. The steady state forms in the
// middle once ramp-up fills the window. Each iteration's executing cells use its own
// marker char (0-9,A-..) so cross-iteration overlap is visible at a glance: in the
// steady region one column (cycle) carries several iterations' markers on different
// rows. '.' = ready-but-waiting. Rows are grouped per iteration. Same scheduler as
// the single-iteration gantt() — this just feeds it the replicated stream.
def gantt_iters(&Vec(ir.Uop) uops, &Vec(ir.Inst) prog, int nports, int iters) -> Str {
    int n = uops.len()
    Vec(ir.Uop) rep = replicate(uops, iters)
    Vec(ir.UopTrace) tr = simulate(&rep, nports)
    int total = total_cycles(&tr)
    Str out = f"=== engine-2 multi-iteration timeline ({iters} iters overlapped, {total} cycles) ===\n"
    Str ruler = "".pad_right(24, 32)
    for c in 0..(total + 1) {
        if c % 10 == 0 { ruler = f"{ruler}|" } else { ruler = f"{ruler} " }
    }
    out = f"{out}{ruler}\n"
    int total_uops = tr.len()
    for pos in 0..total_uops {
        int it = pos / n
        int idx = pos - it * n
        if idx == 0 { out = f"{out}  -- iteration {it} --\n" }
        &ir.UopTrace t = tr.get_ref(pos)
        int ready = t.cycle_ready
        int iss = t.cycle_issued
        int dn = t.cycle_done
        int port = t.port_used
        Str fill = _iter_char(it)
        int de = dn                            // lat-0 micro-op still occupies a slot
        if de <= iss { de = iss + 1 }
        Str bar = ""
        for c in 0..(total + 1) {
            if c >= iss {
                if c < de { bar = f"{bar}{fill}" } else { bar = f"{bar} " }
            } else {
                if c >= ready { bar = f"{bar}." } else { bar = f"{bar} " }
            }
        }
        Str mn = _mn(prog, t.uop_id).pad_right(10, 32)   // uop_id = inst index
        Str lead = f"  u{idx} {mn} p{port} "
        out = f"{out}{lead.pad_right(24, 32)}{bar}\n"
    }
    return out
}
