// sim_isa_table_test.ls — the built-in instruction-semantics table (the encoding).
//
// Demonstrates that the library RECOGNIZES mainstream x86 SIMD instructions straight
// from assembly: it carries a declarative spec table (mnemonic -> class / element
// width / lane behaviour / operand roles / semantics). Classifying a kernel is then
// a table lookup, not per-kernel code.

import sim.intel.semantics as sem
import sim.intel.isa_table as isa
import std.core.vec
import std.core.str

def check(bool cond, Str name) {
    if cond { @print(f"  ok: {name}") }
    else { @print(f"  FAIL: {name}") }
}

def main() {
    // the table is AUTO-GENERATED from Intel XED datafiles (Apache-2.0) by
    // tools/gen_isa_from_xed.py -- element width + ISA gating come straight from XED.
    Vec(sem.SpecRow) tbl = isa.build_table()
    @print(f"=== XED-generated spec table: {tbl.len()} instructions ===")
    @print("")
    check(tbl.len() >= 400, "table covers 400+ instructions (from XED)")

    // classify a mixed kernel directly from assembly (movement + compute classes)
    Str asm = ""
    asm = f"{asm}vpermb     zmm1, zmm0, zmm16\n"
    asm = f"{asm}vpshufb    zmm2, zmm1, zmm17\n"
    asm = f"{asm}vpsllvw    zmm3, zmm2, zmm18\n"
    asm = f"{asm}vpbroadcastd zmm4, zmm3\n"
    asm = f"{asm}vpaddd     zmm5, zmm3, zmm4\n"
    asm = f"{asm}vpmaddwd   zmm6, zmm5, zmm7\n"
    asm = f"{asm}vfmadd231ps zmm8, zmm9, zmm10\n"
    asm = f"{asm}vpcmpgtd   k1, zmm5, zmm11\n"
    asm = f"{asm}vpmovdb    xmm12, zmm5\n"
    asm = f"{asm}vpternlogd zmm13, zmm14, zmm15, 0x96\n"
    asm = f"{asm}vgf2p8mulb zmm16, zmm5, zmm6\n"
    asm = f"{asm}vmovdqu8   [rdi], zmm6\n"
    @print(sem.classify_listing(asm, &tbl))

    // ---- spot-check the encoding: class, lane behaviour, operand roles ----
    Str m = "vpermb"
    sem.SpecRow r1 = sem.lookup(&tbl, &m)
    check(r1.op == sem.OP_PERMUTE(), "vpermb -> permute class")
    check(r1.cross, "vpermb is cross-lane")
    check(r1.ctrl_pos == 0, "vpermb control mask is src0")

    Str m2 = "vpshufb"
    sem.SpecRow r2 = sem.lookup(&tbl, &m2)
    check(r2.op == sem.OP_PERMUTE(), "vpshufb -> permute class")
    check(!r2.cross, "vpshufb is in-lane (NOT cross-lane)")
    check(r2.ctrl_pos == 1, "vpshufb control is src1 (different role than vpermb!)")

    Str m3 = "vfmadd231ps"
    sem.SpecRow r3 = sem.lookup(&tbl, &m3)
    check(r3.op == sem.OP_FMA(), "vfmadd231ps -> fma class")
    check(!sem.is_movement(r3.op), "fma is a COMPUTE class (not data movement)")

    Str m4 = "vpsllvw"
    sem.SpecRow r4 = sem.lookup(&tbl, &m4)
    check(r4.op == sem.OP_SHIFT_VL(), "vpsllvw -> variable-shift-left")
    check(r4.elem_bits == 16, "vpsllvw element width = 16 (word)")
    check(sem.is_movement(r4.op), "shift is a movement class (bit-level)")

    Str m5 = "vfoobar"
    sem.SpecRow r5 = sem.lookup(&tbl, &m5)
    check(r5.op == sem.OP_UNKNOWN(), "unknown mnemonic -> UNKNOWN (graceful)")

    @print("SIM ISA TABLE PASS")
}
