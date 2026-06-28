// sim_viz_test.ls — std.sim step-1: instruction + register text visualization.
// Exercises std.sim.core.ir (instruction listing) and std.sim.regview
// (bit-level funnel, byte-level shuffle). Deterministic output, memcheck-clean.

import sim.core.ir as ir
import sim.regview as rv
import std.core.vec
import std.core.str

def check(bool cond, Str name) {
    if cond { @print(f"  ok: {name}") }
    else { @print(f"  FAIL: {name}") }
}

def main() {
    // ---- instruction text view (ir) ----
    Vec(ir.Operand) o1 = {}
    o1.push(ir.reg_w("zmm1"))
    o1.push(ir.reg_r("zmm2"))
    o1.push(ir.reg_r("zmm3"))
    Vec(ir.Operand) o2 = {}
    o2.push(ir.reg_rw("acc"))
    o2.push(ir.reg_r("Bv"))
    o2.push(ir.imm("9"))

    Vec(ir.Inst) prog = {}
    prog.push(ir.inst(4096 as i64, 6, "vpermw", o1, 4))
    prog.push(ir.inst(4102 as i64, 7, "vpshldvq", o2, 8))

    Str listing = ir.dump_insts(&prog)
    @print(listing)
    check(listing.contains?("vpermw"), "listing has vpermw")
    check(listing.contains?("isa=AVX512"), "listing tags AVX512")

    // ---- bit-level register view: vpshldvq <<9 funnel (bit-field insert) ----
    i64 before = 0x07C as i64
    i64 after = (before << (9 as i64)) | (7 as i64)
    @print("")
    @print(rv.bit_xform("vpshldvq 128b<<9 funnel (m=0x007 enters B8..B0):", before, after, 27, 9))
    @print(rv.caret_low(27, 9, 9, "new field in B8..B0"))
    Str arow = rv.bit_row(" after", after, 27, 9)
    check(arow.contains?("000000111"), "low 9 bits = 0x007")

    // ---- byte-level register view: vpshufb byte-reverse on a 128-bit lane ----
    Vec(int) perm = {}
    for i in 0..16 { perm.push(15 - i) }
    @print("")
    @print(rv.shuffle_view("vpshufb byte-reverse (one 128-bit lane, x4):", &perm))
    Str hdr = rv.byte_header(16)
    check(hdr.contains?("B15"), "byte header labels B15..B0")

    // ---- byte_row hex rendering ----
    Vec(int) bytes = {}
    bytes.push(0x01)
    bytes.push(0xFF)
    bytes.push(0xA0)
    Str brow = rv.byte_row(" bytes", &bytes)
    @print(brow)
    check(brow.contains?("FF"), "byte_row hex FF")
    check(brow.contains?("A0"), "byte_row hex A0")

    // ---- clearer views: change-row, field brackets, source provenance, legend ----
    @print("")
    @print(rv.legend())
    // bit change-row: which bits flipped in a <<5 shift of a bit-field
    i64 f = 0x1A5 as i64
    i64 fs = f << (5 as i64)
    @print(rv.bit_xform("vpsllvw <<5 (bit-field 0x1A5, word B15..B0):", f, fs, 16, 8))
    Str chg = rv.bit_change_row(f, fs, 16, 8)
    check(chg.contains?("^"), "change-row flags the flipped bits")
    // field brackets: a 9-wide field sits in bits 8..0 before, 13..5 after
    @print(rv.bit_field_bracket(16, 8, 0, 9, "field before (bits 8..0)"))
    @print(rv.bit_field_bracket(16, 8, 5, 9, "field after  (bits 13..5)"))
    Str fb = rv.bit_field_bracket(16, 8, 5, 9, "after")
    check(fb.contains?("["), "field bracket marks the field extent")

    // explicit source-index provenance row (dst<-src)
    @print("")
    @print(rv.shuffle_view_full("vpshufb byte-reverse with provenance:", &perm))
    Str frow = rv.from_row(" dst<-src", &perm)
    check(frow.contains?("0F"), "from_row shows src index 0x0F for dst[0]")

    // ---- cross-lane view: 4-lane live/dead map + gather edges (vpermb compaction) ----
    @print("")
    Vec(int) live = {}
    live.push(9); live.push(9); live.push(9); live.push(0)
    @print(rv.lane_map("4-lane live/dead map (3x9 live, lane3 unused):", &live))
    Str lm = rv.lane_map("m", &live)
    check(lm.contains?("lane0"), "lane_map labels lane0")
    check(lm.contains?("lane3"), "lane_map shows all 4 lanes")
    Str ge = rv.gather_edge(9, 17, 16, 24, "CROSS-LANE down 7B")
    @print(ge)
    check(ge.contains?("dst B9..B17"), "gather_edge shows the dst run")
    check(ge.contains?("src B16..B24"), "gather_edge shows the cross-lane src run")

    @print("SIM VIZ PASS")
}
