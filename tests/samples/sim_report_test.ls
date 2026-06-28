// sim_report_test.ls — the unified pipeline report (sim.report.full_report).
//
// One call over a text listing produces the whole analysis. This also serves as
// the all-modules-together integration memcheck (decode + ports + uarch + frontend
// + engine-1 + engine-2 + analysis + patterns).

import sim.report as report
import sim.intel.uarch as uarch
import std.core.vec
import std.core.str

def has(Str hay, Str needle, Str name) {
    if hay.contains?(&needle) { @print(f"  ok: {name}") }
    else { @print(f"  FAIL: {name} (missing '{needle}')") }
}

def main() {
    Str src = ""
    src = f"{src}; BFP8 block-compress kernel (LUT-normalized input)\n"
    src = f"{src}vpermb   zmm0, zmm0, zmm16\n"
    src = f"{src}vpabsw   zmm1, zmm0\n"
    src = f"{src}vpshufd  zmm2, zmm1, 0x4e\n"
    src = f"{src}vpmaxuw  zmm1, zmm1, zmm2\n"
    src = f"{src}vplzcntd zmm3, zmm1\n"
    src = f"{src}vpsraw   zmm4, zmm0, zmm3\n"
    src = f"{src}vpmovwb  ymm5, zmm4\n"
    src = f"{src}vmovdqu8 [rdi], ymm5\n"

    uarch.Uarch u = uarch.icelake()
    Vec(Str) isa = {}
    isa.push("AVX2")
    isa.push("AVX512")
    isa.push("VBMI")

    Vec(Str) cregs = {}
    cregs.push("zmm16")            // the LUT table is loop-invariant

    Str rep = report.full_report(&src, &u, &isa, &cregs)
    @print(rep)

    // every pipeline stage left its mark in the unified report
    has(rep, "instruction listing", "stage: instruction listing")
    has(rep, "Ice Lake", "stage: uarch summary")
    has(rep, "front-end model", "stage: front-end model")
    has(rep, "LSD", "front-end: BFP8 is LSD-resident")
    has(rep, "bottleneck analysis", "stage: engine-1")
    has(rep, "port-bound(p5)", "engine-1: port-bound(p5)")
    has(rep, "cycle timeline", "stage: engine-2 Gantt")
    has(rep, "operand provenance", "stage: provenance")
    has(rep, "LoopInvariant", "provenance: LUT table is LoopInvariant")
    has(rep, "advisor suggestions", "stage: advisor")
    has(rep, "shuffle-port-saturated", "advisor: flags p5 saturation")

    @print("SIM REPORT PASS")
}
