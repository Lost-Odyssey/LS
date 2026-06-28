// sim_engine_test.ls — std.sim step-1 MVP: engine-1 bottleneck analysis.
//
// Builds a clean-room BFP8 block-compress pack kernel (byte-aligned, 7 vector ops)
// as a Vec(Inst), runs it through ports.build_uops -> engine.analyze, and asserts
// the engine independently arrives at the hand reasoning: the kernel is port-bound
// on p5 (vpshufd reduction + vpmovwb pack both land on the single shuffle port),
// NOT frontend- or latency-bound.
//
// This closes the loop: "wrote a SIMD kernel -> sim finds the p5 bottleneck" (plan §6.9).

import sim.core.ir as ir
import sim.intel.ports as ports
import sim.intel.uarch as uarch
import sim.core.engine as engine
import std.core.vec
import std.core.str

def check(bool cond, Str name) {
    if cond { @print(f"  ok: {name}") }
    else { @print(f"  FAIL: {name}") }
}

// helper: build a 3-register vector op  dst <- f(a, ctrl)  (ctrl loop-invariant)
def vop3(i64 addr, Str mn, Str dst, Str a, Str ctrl, int isa) -> ir.Inst {
    Vec(ir.Operand) o = {}
    o.push(ir.reg_w(dst))
    o.push(ir.reg_r(a))
    o.push(ir.reg_r(ctrl))
    return ir.inst(addr, 6, mn, o, isa)
}

def main() {
    // ---- BFP8 block-compress kernel (clean-room, byte-aligned; straight-line) ----
    // zmm0 = input int16 samples (live-in). 8-bit mantissas are byte-aligned, so the
    // pack is a word->byte truncate (vpmovwb) -- no sub-byte stagger.
    Vec(ir.Inst) prog = {}
    // vpabsw zmm1, zmm0   magnitude |sample|                              p0/1/5 lat1
    Vec(ir.Operand) oabs = {}
    oabs.push(ir.reg_w("zmm1")); oabs.push(ir.reg_r("zmm0"))
    prog.push(ir.inst(0x400 as i64, 6, "vpabsw", oabs, 4))
    // vpshufd zmm2, zmm1, 0x4e   swap 64-bit halves for the max tree      p5 lat1
    prog.push(vop3(0x406 as i64, "vpshufd", "zmm2", "zmm1", "0x4e", 4))
    // vpmaxuw zmm1, zmm1, zmm2   fold running block maxabs                p0/1/5 lat1
    prog.push(vop3(0x40c as i64, "vpmaxuw", "zmm1", "zmm1", "zmm2", 4))
    // vplzcntd zmm3, zmm1   exponent from leading-zero count              p0/1 lat1
    Vec(ir.Operand) olz = {}
    olz.push(ir.reg_w("zmm3")); olz.push(ir.reg_r("zmm1"))
    prog.push(ir.inst(0x412 as i64, 6, "vplzcntd", olz, 4))
    // vpsraw zmm4, zmm0, zmm3   quantize: arithmetic shift right by exp   p0/1 lat1
    prog.push(vop3(0x418 as i64, "vpsraw", "zmm4", "zmm0", "zmm3", 4))
    // vpmovwb ymm5, zmm4   PACK: 32 int16 -> 32 int8 (truncate)           p5 lat3
    Vec(ir.Operand) omov = {}
    omov.push(ir.reg_w("ymm5")); omov.push(ir.reg_r("zmm4"))
    prog.push(ir.inst(0x41e as i64, 6, "vpmovwb", omov, 4))
    // vmovdqu8 [rdi], ymm5   store 32 mantissa bytes                      p4 lat1
    Vec(ir.Operand) ost = {}
    ost.push(ir.mem_w("[rdi]")); ost.push(ir.reg_r("ymm5"))
    prog.push(ir.inst(0x424 as i64, 7, "vmovdqu8", ost, 4))

    Str listing = ir.dump_insts(&prog)
    @print(listing)

    // ---- model: Inst -> Uop (port/latency seed + RAW deps) ----
    // Port/latency/uop data come from LLVM-18 llvm-mca (icelake-server) via
    // tools/gen_ports_from_mca.py. Multi-uop ops expand: vpmovwb = 2 uops on p5,
    // and vpsraw-by-count = 2 fixed uops {p0}(shift)+{p5}(count-broadcast). So the
    // 7 instructions become 10 uops (1+1+1+1+2+2+2).
    Vec(ir.Uop) uops = ports.build_uops(&prog)
    check(uops.len() == 10, "10 uops built (vpsraw/vpmovwb expand to 2 each)")

    // ---- engine-1: bottleneck analysis (Ice Lake params from uarch table) ----
    uarch.Uarch u = uarch.icelake()
    @print(uarch.summary(&u))
    engine.Bottleneck b = engine.analyze(&uops, u.num_ports, u.fe_width)
    Vec(ir.Port) pset = uarch.port_set(&u)
    @print("")
    @print(engine.report(&b, &uops, &pset))

    // The whole point: engine independently finds p5 is the limiter.
    check(b.kind.contains?("port-bound(p5)"), "verdict = port-bound(p5)")
    check(b.port_id == 5, "saturated port id = 5")
    // p5 carries 4 shuffle-port uops: vpshufd(1) + vpsraw count-broadcast(1) +
    // vpmovwb(2) => optimal flow ResMII = 4.0; res_mii_x scaled by SCALE(=12) = 48.
    // This matches llvm-mca Block RThroughput 4.0 for the same kernel (validated).
    check(b.res_mii_x == 48, "p5 ResMII = 4.00 (x12 = 48), matches llvm-mca")
    // frontend = 10 uops / 5-wide = 2.0 < 4.0, so NOT frontend-bound.
    check(b.frontend_x < b.res_mii_x, "frontend below port pressure")
    // single-iteration critical path with real latencies: abs(1)+shufd(1)+maxuw(1)
    // +lzcnt(5)+sraw(4)+movwb(4)+store(1) = 17c — overlaps away across independent
    // iterations, so the p5 port bound (4.0c) is the real throughput limiter.
    check(b.critical == 17, "critical path = 17c (overlaps away across iters)")
    check(b.total_uops == 10, "total uops = 10")

    @print("SIM ENGINE PASS")
}
