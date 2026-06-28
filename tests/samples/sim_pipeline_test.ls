// sim_pipeline_test.ls — std.sim full MVP pipeline (plan §3.1, all 5 layers).
//
//   text listing -> decode.parse_listing -> ports.build_uops -> engine.analyze
//                -> patterns.advise -> text render
//
// The kernel is the BFP8 block-compress pack, written as a plain Intel-syntax listing
// (no hand-built Vec(Inst)). This is the end-to-end "write a SIMD kernel -> sim finds
// the p5 bottleneck -> advisor recommends the fold" loop (plan §6.9).

import sim.intel.decode as decode
import sim.intel.ports as ports
import sim.intel.uarch as uarch
import sim.core.engine as engine
import sim.intel.patterns as adv
import sim.core.ir as ir
import std.core.vec
import std.core.str

def check(bool cond, Str name) {
    if cond { @print(f"  ok: {name}") }
    else { @print(f"  FAIL: {name}") }
}

def fired(&Vec(adv.Suggestion) sg, Str id) -> bool {
    for s in &sg {
        if s.id.eq?(&id) { return true }
    }
    return false
}

// spot-check helpers: take a clean &ir.Inst param borrow (field access on a named
// param borrow does not clone — avoids the get_ref().field leak and the leading-&
// statement-boundary ambiguity).
def mn_is(&ir.Inst ins, Str want) -> bool { return ins.mnemonic.eq?(&want) }
def isa_of(&ir.Inst ins) -> int { return ins.isa_class }
def dest_is_store(&ir.Inst ins) -> bool {
    if ins.ops.len() == 0 { return false }
    return ins.ops.get_ref(0).kind == 1 && ins.ops.get_ref(0).is_write
}

def main() {
    // ---- layer 1: input as a text instruction listing ----
    Str src = ""
    src = f"{src}; BFP8 block-compress kernel\n"
    src = f"{src}vpabsw   zmm1, zmm0\n"
    src = f"{src}vpshufd  zmm2, zmm1, 0x4e\n"
    src = f"{src}vpmaxuw  zmm1, zmm1, zmm2\n"
    src = f"{src}vplzcntd zmm3, zmm1\n"
    src = f"{src}vpsraw   zmm4, zmm0, zmm3\n"
    src = f"{src}vpmovwb  ymm5, zmm4\n"
    src = f"{src}vmovdqu8 [rdi], ymm5\n"

    // ---- layer 1->2: decode ----
    Vec(ir.Inst) prog = decode.parse_listing(&src, 0x400 as i64)
    @print(ir.dump_insts(&prog))
    check(prog.len() == 7, "decoded 7 instructions (comment skipped)")

    // spot-check decode classified operands & isa right
    check(mn_is(prog.get_ref(0), "vpabsw"), "first mnemonic = vpabsw")
    check(isa_of(prog.get_ref(0)) == 4, "vpabsw inferred AVX512")
    check(mn_is(prog.get_ref(6), "vmovdqu8"), "last mnemonic = vmovdqu8")
    check(dest_is_store(prog.get_ref(6)), "store dest [rdi] = memory write")

    // ---- layer 3: model + engine ----
    Vec(ir.Uop) uops = ports.build_uops(&prog)
    uarch.Uarch u = uarch.icelake()
    engine.Bottleneck b = engine.analyze(&uops, u.num_ports, u.fe_width)
    Vec(ir.Port) pset = uarch.port_set(&u)
    @print(uarch.summary(&u))
    @print("")
    @print(engine.report(&b, &uops, &pset))
    check(b.kind.contains?("port-bound(p5)"), "engine: port-bound(p5)")

    // ---- layer 4: advisor ----
    int bk = adv.bk_frontend()
    int bport = -1
    if b.port_id >= 0 { bk = adv.bk_port(); bport = b.port_id }
    Vec(Str) isa = {}
    isa.push("AVX2")
    isa.push("AVX512")
    isa.push("VBMI")
    Vec(adv.Suggestion) sg = adv.advise(&prog, bk, bport, &isa, u.avx512_downclock)
    @print(adv.render(&sg))

    check(fired(&sg, "shuffle-port-saturated"), "advisor flags p5 saturation")
    // Ice Lake = light downclock -> no AVX-512 downclock warning
    check(!fired(&sg, "avx512-downclock"), "Ice Lake: no downclock warning")

    @print("SIM PIPELINE PASS")
}
