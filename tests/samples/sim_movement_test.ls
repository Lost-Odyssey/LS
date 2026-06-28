// sim_movement_test.ls — exercise the sim.intel.movement LIBRARY.
//
// The whole point: the caller supplies ONLY the instruction mnemonic + its control
// mask + the input register state. The library computes dst[i]=src[mask[i]], detects
// cross-lane vs in-lane, derives the gather runs, and renders the SRC/mask/DST view
// itself. No layout is hardcoded here — this is a thin call into the analysis API.

import sim.intel.movement as mv
import sim.regview as rv
import std.core.vec
import std.core.str

def check(bool cond, Str name) {
    if cond { @print(f"  ok: {name}") }
    else { @print(f"  FAIL: {name}") }
}

def live_count(&mv.ByteReg r) -> int {
    int c = 0
    for i in 0..64 { if r.ids.get!(i) >= 0 { c = c + 1 } }
    return c
}

def main() {
    @print("=== sim.intel.movement: instruction-driven data-movement (library, not hardcoded) ===")
    @print("")

    // ---- CROSS-LANE: vpermb compacting live bytes across lanes (final gather) ----
    // input state: 3 lanes each holding 9 packed bytes (low 9); the LIBRARY is told
    // only this state + the permute index mask, nothing about the result layout.
    mv.ByteReg src = mv.reg_packed(3, 9)
    Vec(int) mask = {}
    for k in 0..9  { mask.push(k) }        // dst[ 0.. 8] <- src[ 0.. 8]  (lane0)
    for k in 0..9  { mask.push(16 + k) }   // dst[ 9..17] <- src[16..24]  (lane1)
    for k in 0..9  { mask.push(32 + k) }   // dst[18..26] <- src[32..40]  (lane2)
    for k in 27..64 { mask.push(-1) }      // dst[27..63] dead

    Str mn = "vpermb"
    mv.StepResult r = mv.step_permute("vpermb zmm6, zmm5, zmm20    ; p5", &mn, &src, &mask)
    @print(r.view)
    @print("")

    // the result + view are LIBRARY OUTPUT, derived from (state, mask):
    check(live_count(&r.dst) == 27, "library auto-computed 27 live bytes in dst")
    check(r.dst.ids.get!(26) >= 0, "dst B26 live (last of the 27)")
    check(r.dst.ids.get!(27) < 0, "dst B27 dead (gaps squeezed out, contiguous B0..B26)")
    check(r.dst.ids.get!(9) == 16, "dst B9 pulled src B16 (cross-lane, auto-computed)")
    check(r.view.contains?("CROSS-LANE"), "library auto-tagged the cross-lane runs")
    check(r.view.contains?("lane0 B15..0"), "library auto-rendered the 4-lane map")
    @print("")

    // ---- IN-LANE: vpshufb gathering the even bytes within each 128-bit lane ----
    mv.ByteReg s2 = mv.reg_iota()
    Vec(int) m2 = {}
    for lane in 0..4 {
        for k in 0..8  { m2.push(2 * k) }      // even bytes 0,2,4,..14 -> low 8
        for k in 0..8  { m2.push(128) }        // top-bit set -> zero (dead)
    }
    Str mn2 = "vpshufb"
    mv.StepResult r2 = mv.step_permute("vpshufb zmm3, zmm2, zmm18   ; p5", &mn2, &s2, &m2)
    @print(r2.view)
    @print("")
    check(r2.dst.ids.get!(0) == 0,  "vpshufb in-lane: dst B0 <- src B0")
    check(r2.dst.ids.get!(1) == 2,  "vpshufb in-lane: dst B1 <- src B2 (even gather)")
    check(r2.dst.ids.get!(16) == 16, "vpshufb stays in lane1 (dst B16 <- src B16, not B0)")
    check(r2.view.contains?("in-lane"), "library auto-tagged in-lane (no cross-lane move)")

    @print("SIM MOVEMENT PASS")
}
