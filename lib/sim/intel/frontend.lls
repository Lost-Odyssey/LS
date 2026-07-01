// sim.intel.frontend — parameterized front-end model (plan §5.2).
//
// This is the substance of "more precise than llvm-mca": llvm-mca models the
// front-end as zero (assumes the back-end is always fed). Real Intel cores deliver
// uops from one of three sources with very different rates:
//   * LSD  (loop stream detector) — a tiny loop is locked, no fetch limit, runs at
//          rename width.
//   * DSB  (uop cache)            — a loop body that fits delivers ~rename width,
//          bypassing instruction fetch/decode.
//   * MITE (legacy decode)        — a body too big for the DSB is fetch-bound by the
//          16-byte/cycle window: long EVEX instructions throttle delivery well below
//          rename width.
// The modeled delivery rate then refines engine-1's front-end bound.
//
// Pure LS, zero compiler changes (same discipline as lib/oran).

import std.core.vec
import std.core.str
import sim.core.ir as ir
import sim.intel.uarch as uarch

// delivery source
def fe_mite() -> int { return 0 }
def fe_dsb()  -> int { return 1 }
def fe_lsd()  -> int { return 2 }
def source_name(int s) -> Str {
    if s == 1 { return "DSB" }
    if s == 2 { return "LSD" }
    return "MITE"
}

// residency capacities for the modeled loop body (Ice Lake-ish, uop counts).
def lsd_max() -> int { return 70 }    // LSD locks loops up to ~this many uops
def dsb_max() -> int { return 200 }   // DSB-resident if the body fits

// 16-byte fetch window per cycle (MITE).
def fetch_bytes() -> int { return 16 }

struct FrontendModel {
    int source         // fe_mite / fe_dsb / fe_lsd
    int deliver_x10    // effective front-end uops/cycle, x10 (fetch can be fractional)
    int total_uops
    int total_bytes
    int avg_len_x10    // average instruction length x10
}

def analyze_frontend(&Vec(ir.Inst) prog, &uarch.Uarch u) -> FrontendModel {
    int n = prog.len()
    int bytes = 0
    for ins in &prog { bytes = bytes + ins.length }
    int avg_x10 = 0
    if n > 0 { avg_x10 = (bytes * 10) / n }

    int src = fe_mite()
    int deliver = 0      // x10
    if n <= lsd_max() {
        src = fe_lsd()
        deliver = u.fe_width * 10
    } else {
        if n <= dsb_max() {
            src = fe_dsb()
            int d = u.dsb_width
            if u.fe_width < d { d = u.fe_width }   // rename caps DSB delivery
            deliver = d * 10
        } else {
            // MITE: fetch-bound = (16 bytes/cycle) / avg_len uops/cycle, x10.
            src = fe_mite()
            int fetch_x10 = 0
            if avg_x10 > 0 { fetch_x10 = (fetch_bytes() * 100) / avg_x10 }  // 16/avg *10
            int cap = u.mite_width * 10
            deliver = fetch_x10
            if cap < deliver { deliver = cap }     // decoder width also caps
        }
    }
    return FrontendModel { source: src, deliver_x10: deliver, total_uops: n,
                           total_bytes: bytes, avg_len_x10: avg_x10 }
}

// integer effective front-end width to feed engine-1 (rounded, >= 1).
def effective_fe_width(&FrontendModel fm) -> int {
    int w = (fm.deliver_x10 + 5) / 10
    if w < 1 { w = 1 }
    return w
}

// x10 fixed-point -> "N.N"
def _dec1(int x10) -> Str {
    int whole = x10 / 10
    int frac = x10 - whole * 10
    return f"{whole}.{frac}"
}

def report(&FrontendModel fm) -> Str {
    Str s = source_name(fm.source)
    Str dl = _dec1(fm.deliver_x10)
    Str al = _dec1(fm.avg_len_x10)
    Str out = "=== front-end model ===\n"
    out = f"{out}  source: {s}   delivery: {dl} uops/cycle\n"
    out = f"{out}  body: {fm.total_uops} uops, {fm.total_bytes} bytes, avg {al} bytes/inst\n"
    if fm.source == fe_mite() {
        out = f"{out}  NOTE: MITE fetch-bound by the 16B/cycle window (long instructions throttle delivery)\n"
    }
    return out
}
