// sim_analysis_test.ls — shared dep-graph + provenance analysis (plan §4.3).
//
// Builds the DepGraph for a generic table-lookup kernel (vpermb LUT -> shuffle ->
// add), cross-checks its critical path against engine-1, and classifies operand
// provenance: an annotated LUT table is LoopInvariant (a hoist/specialization gate),
// the live-in data is Unknown, produced values are LoopVariant, immediates Const.

import sim.intel.decode as decode
import sim.intel.ports as ports
import sim.core.engine as engine
import sim.core.analysis as an
import sim.core.ir as ir
import std.core.vec
import std.core.str

def check(bool cond, Str name) {
    if cond { @print(f"  ok: {name}") }
    else { @print(f"  FAIL: {name}") }
}

def prov_at(&Vec(ir.Inst) prog, int i, int j, &Vec(Str) written, &Vec(Str) cregs) -> int {
    &ir.Inst ins = prog.get_ref(i)
    return an.classify(ins.ops.get_ref(j), written, cregs)
}

def main() {
    // generic table-lookup kernel: vpermb LUT -> shuffle -> add. zmm0 is live-in
    // data, zmm16 is an annotated loop-invariant LUT, zmm1/zmm2/zmm3 are produced.
    Str src = ""
    src = f"{src}vpermb  zmm1, zmm0, zmm16\n"
    src = f"{src}vpshufd zmm2, zmm1, 0x4e\n"
    src = f"{src}vpaddd  zmm3, zmm1, zmm2\n"
    Vec(ir.Inst) prog = decode.parse_listing(&src, 0x400 as i64)
    Vec(ir.Uop) uops = ports.build_uops(&prog)

    // ---- dependency graph + critical path cross-check ----
    an.DepGraph g = an.build_depgraph(&uops)
    int cp = an.crit_path(&g)
    engine.Bottleneck b = engine.analyze(&uops, 8, 5)
    @print(f"analysis crit-path: {cp}c   engine-1 crit-path: {b.critical}c")
    check(cp == b.critical, "analysis critical path == engine-1 critical path")
    check(cp >= 3, "critical path is the vpermb->shufd->add chain")
    // vpaddd (uop 2) has two producers (vpermb result + vpshufd result)
    check(an.in_degree(&g, 2) == 2, "vpaddd has in-degree 2 (vpermb + vpshufd sources)")
    // vpermb (uop 0) has no producers
    check(an.in_degree(&g, 0) == 0, "vpermb has in-degree 0 (live-in + const table)")

    // ---- provenance ----
    Vec(Str) written = an.written_regs(&prog)
    Vec(Str) cregs = {}              // annotate the LUT table as constant
    cregs.push("zmm16")
    @print(an.report_provenance(&prog, &cregs))

    // vpermb reads zmm0 (live-in data) and zmm16 (constant LUT)
    int pz0 = prov_at(&prog, 0, 1, &written, &cregs)    // zmm0
    int pz16 = prov_at(&prog, 0, 2, &written, &cregs)   // zmm16
    check(pz0 == an.prov_unknown(), "zmm0 (live-in data) = Unknown")
    check(pz16 == an.prov_loop_invariant(), "zmm16 (annotated table) = LoopInvariant")
    check(an.is_invariant(pz16), "zmm16 is invariant (a hoist/specialization gate)")
    check(!an.is_invariant(pz0), "zmm0 is not invariant")

    // vpshufd reads zmm1 (produced by vpermb this iteration) -> LoopVariant
    int pz1 = prov_at(&prog, 1, 1, &written, &cregs)    // zmm1
    check(pz1 == an.prov_loop_variant(), "zmm1 (produced in-kernel) = LoopVariant")

    // immediate operand -> Const
    Str imsrc = "vpsrld zmm0, zmm1, 9\n"
    Vec(ir.Inst) imk = decode.parse_listing(&imsrc, 0x500 as i64)
    Vec(Str) iw = an.written_regs(&imk)
    Vec(Str) ic = {}
    int pimm = prov_at(&imk, 0, 2, &iw, &ic)            // the "9" immediate
    check(pimm == an.prov_const(), "immediate operand = Const")

    @print("SIM ANALYSIS PASS")
}
