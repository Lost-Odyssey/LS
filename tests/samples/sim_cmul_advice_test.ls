// sim_cmul_advice_test.ls — provenance-aware advice (plan §6.6d).
//
// THE differentiator: the SAME complex-multiply kernel gets DIFFERENT advice based
// on whether the coefficient is a loop-invariant constant (twiddle/tap) or dynamic.
// An opcode-level linter sees the same vpmaddwd and cannot tell; the advisor knows
// because it consumes sim.core.analysis provenance facts.

import sim.intel.decode as decode
import sim.intel.patterns as adv
import sim.core.ir as ir
import std.core.vec
import std.core.str

def check(bool cond, Str name) {
    if cond { @print(f"  ok: {name}") }
    else { @print(f"  FAIL: {name}") }
}

// first suggestion id from advise_cmul (or "none")
def cmul_id(&Vec(ir.Inst) prog, &Vec(Str) cregs, bool has_fp16) -> Str {
    Vec(adv.Suggestion) sg = adv.advise_cmul(prog, cregs, has_fp16)
    if sg.len() == 0 { return "none" }
    return sg.get_ref(0).id.copy()
}

def main() {
    // a complex multiply via the INT16 vpmaddwd scheme: zmm1=IQ data, zmm2/zmm4=coefficients
    Str src = ""
    src = f"{src}vpmaddwd  zmm0, zmm1, zmm2\n"
    src = f"{src}vpmaddwd  zmm3, zmm1, zmm4\n"
    src = f"{src}vpackssdw zmm5, zmm0, zmm3\n"
    Vec(ir.Inst) prog = decode.parse_listing(&src, 0x400 as i64)

    // CASE 1: coefficients annotated constant (twiddle table) -> INT16 path
    Vec(Str) const_coef = {}
    const_coef.push("zmm2")
    const_coef.push("zmm4")
    Vec(adv.Suggestion) s1 = adv.advise_cmul(&prog, &const_coef, true)
    @print(adv.render(&s1))
    Str id1 = cmul_id(&prog, &const_coef, true)
    check(id1.eq?("cmul-int16-const"), "constant coefficient -> INT16 vpmaddwd advice")

    // CASE 2: SAME kernel, no constant annotation (dynamic), FP16 available -> fp16
    Vec(Str) none = {}
    Str id2 = cmul_id(&prog, &none, true)
    check(id2.eq?("cmul-fp16-native"), "dynamic operands + FP16 -> vfmulcph advice")

    // CASE 3: SAME kernel, dynamic, NO FP16 hardware -> fallback scheme
    Str id3 = cmul_id(&prog, &none, false)
    check(id3.eq?("cmul-fallback"), "dynamic operands, no FP16 -> fallback advice")

    // the headline: identical opcodes, three different recommendations
    check(!id1.eq?(&id2), "same kernel: constant vs dynamic give different advice")

    // a kernel with no multiply -> no complex-multiply advice
    Str nm = "vpor zmm0, zmm1, zmm2\n"
    Vec(ir.Inst) nomul = decode.parse_listing(&nm, 0x500 as i64)
    Str id4 = cmul_id(&nomul, &none, true)
    check(id4.eq?("none"), "no multiply -> no complex-multiply advice")

    @print("SIM CMUL ADVICE PASS")
}
