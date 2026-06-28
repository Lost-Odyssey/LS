// sim_frontend_test.ls — front-end model (plan §5.2): LSD/DSB/MITE delivery.
//
// Shows the front-end model changing engine-1's verdict — the precision llvm-mca
// lacks. A small kernel is LSD/DSB-resident (delivers at rename width); a large
// body of long instructions runs from MITE and is fetch-bound by the 16B window,
// turning a back-end-light loop into a front-end-bound one.

import sim.intel.decode as decode
import sim.intel.ports as ports
import sim.intel.uarch as uarch
import sim.intel.frontend as fe
import sim.core.engine as engine
import sim.core.engine2 as e2
import sim.core.ir as ir
import std.core.vec
import std.core.str

def check(bool cond, Str name) {
    if cond { @print(f"  ok: {name}") }
    else { @print(f"  FAIL: {name}") }
}

// n independent `add` ops (4-port p_galu), each `len` bytes -> a large loop body
def big_add_kernel(int n, int len) -> Vec(ir.Inst) {
    Vec(ir.Inst) prog = {}
    int addr = 0x1000
    for i in 0..n {
        Vec(ir.Operand) o = {}
        o.push(ir.reg_w("rax"))
        o.push(ir.reg_r("rbx"))
        o.push(ir.reg_r("rcx"))
        prog.push(ir.inst(addr as i64, len, "add", o, 0))
        addr = addr + len
    }
    return prog
}

def main() {
    uarch.Uarch u = uarch.icelake()

    // ---- small kernel: BFP8 pack (7 uops) -> LSD-resident, full rename delivery ----
    Str src = ""
    src = f"{src}vpabsw   zmm1, zmm0\n"
    src = f"{src}vpshufd  zmm2, zmm1, 0x4e\n"
    src = f"{src}vpmaxuw  zmm1, zmm1, zmm2\n"
    src = f"{src}vplzcntd zmm3, zmm1\n"
    src = f"{src}vpsraw   zmm4, zmm0, zmm3\n"
    src = f"{src}vpmovwb  ymm5, zmm4\n"
    src = f"{src}vmovdqu8 [rdi], ymm5\n"
    Vec(ir.Inst) small = decode.parse_listing(&src, 0x400 as i64)
    fe.FrontendModel fsmall = fe.analyze_frontend(&small, &u)
    @print(fe.report(&fsmall))
    check(fsmall.source == fe.fe_lsd(), "small kernel is LSD-resident")
    check(fe.effective_fe_width(&fsmall) == 5, "small kernel delivers at rename width (5)")

    // ---- large kernel: 250 long (8-byte) insts -> MITE, fetch-bound ----
    Vec(ir.Inst) big = big_add_kernel(250, 8)
    fe.FrontendModel fbig = fe.analyze_frontend(&big, &u)
    @print(fe.report(&fbig))
    check(fbig.source == fe.fe_mite(), "large body runs from MITE")
    int ebig = fe.effective_fe_width(&fbig)
    @print(f"large-kernel effective front-end width: {ebig}")
    // 16 bytes/cycle / 8 bytes/inst = 2 uops/cycle
    check(ebig == 2, "MITE fetch-bound at 2 uops/cycle (16B / 8B)")
    check(ebig < u.fe_width, "MITE delivery below rename width (front-end throttles)")

    // ---- integration: the model flips engine-1's verdict for the large kernel ----
    Vec(ir.Uop) buops = ports.build_uops(&big)
    // with the RAW rename width (5), the front-end looks fine -> back-end bound
    engine.Bottleneck b_raw = engine.analyze(&buops, u.num_ports, u.fe_width)
    check(b_raw.kind.contains?("port-bound"), "raw rename width: misjudged as port-bound")
    // with the MODELED MITE delivery (2), it is actually front-end bound
    engine.Bottleneck b_fe = engine.analyze(&buops, u.num_ports, ebig)
    check(b_fe.kind.contains?("frontend-bound"), "modeled MITE: correctly front-end bound")

    // small kernel stays port-bound either way (front-end isn't the limit there)
    Vec(ir.Uop) suops = ports.build_uops(&small)
    engine.Bottleneck b_small = engine.analyze(&suops, u.num_ports, fe.effective_fe_width(&fsmall))
    check(b_small.kind.contains?("port-bound(p5)"), "small kernel still port-bound(p5)")

    // ---- engine-2: the front-end cap actually throttles the cycle timeline ----
    Vec(ir.UopTrace) tr_be = e2.simulate(&buops, u.num_ports)            // back-end only
    Vec(ir.UopTrace) tr_fe = e2.simulate_fe(&buops, u.num_ports, ebig)   // MITE-capped (2/cyc)
    int c_be = e2.total_cycles(&tr_be)
    int c_fe = e2.total_cycles(&tr_fe)
    @print(f"engine-2 cycles: back-end-only={c_be}, MITE-capped={c_fe}")
    check(c_fe > c_be, "MITE front-end cap increases the cycle count (throttles delivery)")

    @print("SIM FRONTEND PASS")
}
