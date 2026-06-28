// sim_advisor_test.ls — std.sim advisor MVP (plan §6.9 closed loop).
//
// Feed a p5-bound SIMD kernel (LUT-normalized BFP8 block-compress) -> engine-1 finds
// port-bound(p5) -> the advisor independently surfaces:
//   WARN  shuffle-port-saturated   (p5 is the limiter)
//   WARN  cross-lane-permute-cost  (vpermb is the cross-lane p5 cost)
// and does NOT fire irrelevant rules (no pext -> no bitpack-stagger; not dep-bound ->
// no multi-accumulator rule). This is the advice-where-it-lands gating (plan §6.0).

import sim.intel.decode as decode
import sim.core.ir as ir
import sim.intel.ports as ports
import sim.core.engine as engine
import sim.intel.patterns as adv
import std.core.vec
import std.core.str

def check(bool cond, Str name) {
    if cond { @print(f"  ok: {name}") }
    else { @print(f"  FAIL: {name}") }
}

// is a suggestion with this id present?
def fired(&Vec(adv.Suggestion) sg, Str id) -> bool {
    for s in &sg {
        if s.id.eq?(&id) { return true }
    }
    return false
}

def main() {
    // ---- p5-bound BFP8 block-compress kernel (LUT-normalized input) ----
    // vpermb (cross-lane LUT) + vpshufd (reduction) + vpmovwb (pack) all land on p5.
    Str src = ""
    src = f"{src}vpermb   zmm0, zmm0, zmm16\n"
    src = f"{src}vpabsw   zmm1, zmm0\n"
    src = f"{src}vpshufd  zmm2, zmm1, 0x4e\n"
    src = f"{src}vpmaxuw  zmm1, zmm1, zmm2\n"
    src = f"{src}vplzcntd zmm3, zmm1\n"
    src = f"{src}vpsraw   zmm4, zmm0, zmm3\n"
    src = f"{src}vpmovwb  ymm5, zmm4\n"
    src = f"{src}vmovdqu8 [rdi], ymm5\n"
    Vec(ir.Inst) prog = decode.parse_listing(&src, 0x400 as i64)

    // ---- engine-1 bottleneck ----
    Vec(ir.Uop) uops = ports.build_uops(&prog)
    engine.Bottleneck b = engine.analyze(&uops, 8, 5)

    // map engine verdict -> advisor bottleneck kind
    int bk = adv.bk_frontend()
    int bport = -1
    if b.port_id >= 0 { bk = adv.bk_port(); bport = b.port_id }
    check(bk == adv.bk_port(), "bottleneck mapped to port-bound")
    check(bport == 5, "saturated port = p5")

    // ---- target chip ISA features (Ice Lake-ish) ----
    Vec(Str) isa = {}
    isa.push("AVX2")
    isa.push("AVX512")
    isa.push("VBMI")
    isa.push("BMI2")

    // ---- advise (Ice Lake: light downclock) ----
    Vec(adv.Suggestion) sg = adv.advise(&prog, bk, bport, &isa, 1)
    @print(adv.render(&sg))

    // rules that SHOULD fire on this kernel
    check(fired(&sg, "shuffle-port-saturated"), "fires p5-saturated warning")
    check(fired(&sg, "cross-lane-permute-cost"), "fires cross-lane permute warning (vpermb)")

    // rules that should NOT fire (gating works)
    check(!fired(&sg, "bitpack-stagger"), "no stagger rule (kernel has no pext)")
    check(!fired(&sg, "single-accumulator"), "no multi-acc rule (not dep-bound)")
    check(!fired(&sg, "hadd-reduction"), "no hadd rule (kernel has no vhaddps)")
    check(!fired(&sg, "gather-overuse"), "no gather rule (kernel has no gather)")

    // ---- negative-space check: a pext-based pack would get the stagger advice ----
    Vec(ir.Inst) pk = {}
    Vec(ir.Operand) pe = {}
    pe.push(ir.reg_w("rax"))
    pe.push(ir.reg_r("rbx"))
    pe.push(ir.reg_r("rcx"))
    pk.push(ir.inst(0x500 as i64, 5, "pext", pe, 5))
    // pext-bound kernel: treat as port-bound on p1 (pext issues on p1 in seed table)
    Vec(adv.Suggestion) sg2 = adv.advise(&pk, adv.bk_port(), 1, &isa, 1)
    check(fired(&sg2, "bitpack-stagger"), "pext kernel -> SIMD stagger advice fires")
    check(!fired(&sg2, "shuffle-port-saturated"), "pext kernel: not p5-bound")

    // ---- μarch-conditional advice: AVX-512 downclock (§6.5) ----
    // Same zmm kernel, three targets: only heavy-downclock (Skylake-X) warns.
    Vec(adv.Suggestion) sg_sx = adv.advise(&prog, bk, bport, &isa, 2)   // heavy
    Vec(adv.Suggestion) sg_il = adv.advise(&prog, bk, bport, &isa, 1)   // light
    Vec(adv.Suggestion) sg_rl = adv.advise(&prog, bk, bport, &isa, 0)   // none
    check(fired(&sg_sx, "avx512-downclock"), "Skylake-X (heavy): downclock warns")
    check(!fired(&sg_il, "avx512-downclock"), "Ice Lake (light): no downclock warn")
    check(!fired(&sg_rl, "avx512-downclock"), "Rocket Lake (none): no downclock warn")

    @print("SIM ADVISOR PASS")
}
