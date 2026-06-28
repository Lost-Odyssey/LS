// sim_bfp8_walk.ls — a clean-room BFP8 block-compress example for the sim.
//
// Block Floating Point with 8-bit mantissas is byte-aligned: an 8-bit mantissa is
// exactly one byte, so packing is trivial (a word->byte truncate), with NO sub-byte
// bit-stagger. This is an original, obvious implementation written from the BFP
// definition -- it does not use anyone's bit-packing code.
//
//   block of int16 samples  ->  exponent (block-shared)  +  int8 mantissas
//     1. |x|            vpabsw                         (magnitude)
//     2. block max      vpshufd + vpmaxuw  tree        (reduce to maxabs)
//     3. exponent       vplzcntd                       (from the leading-zero count)
//     4. quantize       vpsraw  (arith >> exp)         (scale to int8 range)
//     5. PACK           vpmovwb (32 int16 -> 32 int8)  (the byte-aligned pack)
//     6. store          vmovdqu8                       (contiguous mantissa bytes)
//
// The data-movement steps (the max-reduction shuffle and the pack) are rendered by
// the sim's instruction-driven movement library; nothing about the layout is
// hand-written here.

import sim.intel.decode as decode
import sim.intel.ports as ports
import sim.intel.uarch as uarch
import sim.core.engine as engine
import sim.intel.movement as mv
import sim.regview as rv
import sim.core.ir as ir
import std.core.vec
import std.core.str

def rule_line() { @print("--------------------------------------------------------------------") }

def main() {
    Str src = ""
    src = f"{src}vpabsw   zmm1, zmm0\n"
    src = f"{src}vpshufd  zmm2, zmm1, 0x4e\n"
    src = f"{src}vpmaxuw  zmm1, zmm1, zmm2\n"
    src = f"{src}vplzcntd zmm3, zmm1\n"
    src = f"{src}vpsraw   zmm4, zmm0, zmm3\n"
    src = f"{src}vpmovwb  ymm5, zmm4\n"
    src = f"{src}vmovdqu8 [rdi], ymm5\n"

    Vec(ir.Inst) prog = decode.parse_listing(&src, 0x400 as i64)
    Vec(ir.Uop) uops = ports.build_uops(&prog)
    uarch.Uarch u = uarch.icelake()
    engine.Bottleneck b = engine.analyze(&uops, u.num_ports, u.fe_width)

    @print("====================================================================")
    @print("  BFP8 BLOCK COMPRESS  ---  per-instruction data movement")
    @print("  int16 samples -> shared exponent + int8 mantissas (byte-aligned, no stagger)")
    @print(f"  steady-state bottleneck: {b.kind}  (ResMII {b.res_mii_x / engine.scale()}c)")
    @print("====================================================================")
    @print(rv.legend())
    @print("")

    @print("[1] vpabsw   zmm1, zmm0           ; p0/1/5 -- magnitude |sample|  (compute)")
    @print("[2] vpshufd  zmm2, zmm1, 0x4e     ; p5     -- swap 64-bit halves for the max tree")
    @print("[3] vpmaxuw  zmm1, zmm1, zmm2     ; p0     -- fold: running block maxabs  (compute)")
    @print("    (steps 2-3 repeat log2(N) times to reduce the block to a single maxabs)")
    rule_line()

    @print("[4] vplzcntd zmm3, zmm1           ; p0/1   -- exponent from leading zeros (compute)")
    @print("    exp = max(0, bitlen(maxabs) - 7);  the whole block shares this exponent.")
    rule_line()

    @print("[5] vpsraw   zmm4, zmm0, zmm3     ; p0/1   -- quantize: arithmetic >> exp")
    @print("    drop the low `exp` bits so each sample fits the signed 8-bit mantissa.")
    rule_line()

    // ---- [6] vpmovwb: THE pack. word->byte truncate = gather each word's low byte ----
    @print("[6] vpmovwb  ymm5, zmm4           ; p5     -- PACK: 32 int16 -> 32 int8")
    @print("    each output byte i is the LOW byte of input word i (truncate). Because an")
    @print("    8-bit mantissa is one byte, packing is just this gather -- no bit-stagger.")
    @print("")
    // hand the LIBRARY the gather: dst byte i <- src byte 2*i (low byte of word i).
    mv.ByteReg words = mv.reg_iota()
    Vec(int) packmask = {}
    for i in 0..32 { packmask.push(2 * i) }     // dst byte i <- src byte 2i (word i low)
    for i in 32..64 { packmask.push(-1) }       // upper half unused (32 bytes out)
    Str pmn = "vpermb"
    mv.StepResult sr = mv.step_permute("    vpmovwb movement (auto-analyzed by sim.intel.movement):", &pmn, &words, &packmask)
    @print(sr.view)
    @print("    => 32 mantissa bytes packed contiguously in B0..B31; no gaps, no stagger.")
    rule_line()

    @print("[7] vmovdqu8 [rdi], ymm5          ; p4     -- store the 32 mantissa bytes")
    @print("")
    @print("BFP8 WALK DONE")
}
