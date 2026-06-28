// sim.intel.movement — instruction-driven register data-movement analysis (§7.6).
//
// This is the LIBRARY that turns an instruction into its register data-movement
// picture. You feed it a byte-permute/shuffle mnemonic + the control-mask bytes +
// the input register's byte state; it COMPUTES dst[i]=src[mask[i]] itself, detects
// cross-lane vs in-lane and the contiguous gather runs, and renders the SRC/mask/DST
// lane views — no per-kernel hardcoding. (Demos/tests just call analyze/step.)
//
// A 512-bit register is modelled as 64 byte slots, each a byte IDENTITY (>= 0) or
// -1 (dead/zero). Tracking identities (not concrete values) is enough to show where
// every byte moves through a chain of shuffles. Bit-level ops (shift/OR) are a
// separate concern (use regview.bit_xform); this module owns the byte-permute class.
//
// Pure LS, zero compiler changes (same discipline as lib/oran).

import std.core.vec
import std.core.str
import sim.regview as rv

// ---- symbolic byte register (64 byte identities) ---------------------------
struct ByteReg { Vec(int) ids }

// identity register: byte i holds identity i (the canonical "input").
def reg_iota() -> ByteReg {
    Vec(int) v = {}
    for i in 0..64 { v.push(i) }
    return ByteReg { ids: v }
}
// register seeded so each of the first `nlanes` lanes has its low `per_lane` bytes
// live (identities), the rest dead — the common "N packed bytes per lane" state.
def reg_packed(int nlanes, int per_lane) -> ByteReg {
    Vec(int) v = {}
    for l in 0..4 {
        for b in 0..16 {
            if l < nlanes { if b < per_lane { v.push(l * 16 + b) } else { v.push(-1) } }
            else { v.push(-1) }
        }
    }
    return ByteReg { ids: v }
}

// ---- instruction semantics (byte-permute class) ----------------------------
// is this a CROSS-LANE byte/word permute (can move bytes between 128-bit lanes)?
def is_cross_lane(&Str mn) -> bool {
    if mn.eq?("vpermb") { return true }
    if mn.eq?("vpermw") { return true }
    if mn.eq?("vpermd") { return true }
    if mn.eq?("vpermq") { return true }
    return false       // vpshufb / vpshufd are IN-LANE
}

// effective SOURCE byte index per dst position (-1 = dead/zero), resolving cross-lane
// (direct dst[i]=src[mask[i]]) vs in-lane (lane-relative: lane(i)*16 + mask[i]%16,
// with the vpshufb top-bit-set rule -> 0). Both perm_apply and the run renderer work
// off this single resolved vector, so the two never disagree.
def eff_source(&Vec(int) mask, bool cross_lane, int n) -> Vec(int) {
    Vec(int) out = {}
    int mn = mask.len()
    for i in 0..n {
        int m = -1
        if i < mn { m = mask.get!(i) }
        int s = -1
        if m >= 0 {
            if cross_lane { s = m }
            else {
                if m >= 128 { s = -1 }                 // vpshufb top-bit-set -> zero
                else { s = (i / 16) * 16 + (m % 16) }  // in-lane, lane-relative
            }
        }
        out.push(s)
    }
    return out
}

// apply a permute given the resolved effective-source vector: dst[i]=src[eff[i]].
def perm_apply(&ByteReg src, &Vec(int) eff) -> ByteReg {
    Vec(int) out = {}
    int n = src.ids.len()
    int en = eff.len()
    for i in 0..n {
        int s = -1
        if i < en { s = eff.get!(i) }
        int id = -1
        if s >= 0 { if s < n { id = src.ids.get!(s) } }
        out.push(id)
    }
    return ByteReg { ids: out }
}

// ---- gather-run detection (auto-derives the "dst run <- src run" edges) -----
struct Run { int dlo; int dhi; int slo; int shi }

// find maximal runs where mask increments by 1 (contiguous source bytes landing in
// contiguous dst bytes) — i.e. the library figures out the gather structure itself.
def find_runs(&Vec(int) mask) -> Vec(Run) {
    Vec(Run) runs = {}
    int n = mask.len()
    int i = 0
    while i < n {
        int m = mask.get!(i)
        if m < 0 { i = i + 1; continue }
        int j = i
        while j + 1 < n {
            int nxt = mask.get!(j + 1)
            if nxt == mask.get!(j) + 1 { j = j + 1 } else { break }
        }
        runs.push(Run { dlo: i, dhi: j, slo: m, shi: mask.get!(j) })
        i = j + 1
    }
    return runs
}

// render the resolved source vector as MSB-first "dst[hi..lo] <- src[hi..lo]" runs
// with auto CROSS-LANE tagging (a run is cross-lane when its src and dst sit in
// different 128-bit lanes). Takes the EFFECTIVE-source vector, not the raw mask.
def render_runs(Str regname, &Vec(int) eff) -> Str {
    Vec(Run) runs = find_runs(eff)
    Str out = f"  {regname} (auto-derived runs, dst[i]=src[idx], MSB-first):\n"
    int nr = runs.len()
    int ri = nr - 1
    while ri >= 0 {
        Run r = runs.get!(ri)
        Str dpos = f"dst[{r.dhi}..{r.dlo}]".pad_right(13, 32)
        Str spos = f"<- src[{r.shi}..{r.slo}]".pad_right(15, 32)
        Str tag = ""
        if r.dlo / 16 != r.slo / 16 { tag = "CROSS-LANE" }
        else { tag = "in-lane" }
        out = f"{out}    {dpos} {spos} {tag}\n"
        ri = ri - 1
    }
    return out
}

// ============================================================================
// the analysis entry point: given a byte-permute instruction, its control mask,
// and the input register state, the library renders the full data-movement view
// (SRC liveness -> mask runs -> DST liveness) AND returns the resulting state, so
// a caller can chain instructions. Nothing about the layout is hardcoded.
// ============================================================================
struct StepResult { ByteReg dst; Str view }

def step_permute(Str header, &Str mn, &ByteReg src, &Vec(int) mask) -> StepResult {
    bool cross = is_cross_lane(mn)
    Vec(int) eff = eff_source(mask, cross, 64)
    ByteReg dst = perm_apply(src, &eff)
    // build sub-views into locals first (LS f-strings can't nest string-literal args
    // inside {..}), then assemble.
    if cross {
        // CROSS-LANE: the full 64-byte / 4-lane picture is needed (§7.6).
        Str src_label = "  SRC (# = live byte, . = dead):"
        Str dst_label = "  DST (library-computed dst[i]=src[mask[i]]):"
        Str src_map = rv.lane_map_ids(src_label, &src.ids)
        Str runs = render_runs(mn, &eff)
        Str dst_map = rv.lane_map_ids(dst_label, &dst.ids)
        Str out = f"{header}  [CROSS-LANE byte permute]\n{src_map}\n\n{runs}\n{dst_map}"
        return StepResult { dst: dst, view: out }
    }
    // IN-LANE: all 4 lanes do the same thing, so collapse to ONE lane (§7.6). Show
    // the 16-byte src(A..P) -> dst mapping derived from the lane-relative mask.
    Vec(int) lane_perm = {}
    for b in 0..16 {
        int m = -1
        if b < mask.len() { m = mask.get!(b) }
        if m >= 0 { if m < 128 { lane_perm.push(m % 16) } else { lane_perm.push(-1) } }
        else { lane_perm.push(-1) }
    }
    Str sv = rv.shuffle_view("  one lane (x4 identical); src A..P -> dst (. = zeroed):", &lane_perm)
    Str out2 = f"{header}  [in-lane byte shuffle]\n{sv}"
    return StepResult { dst: dst, view: out2 }
}
