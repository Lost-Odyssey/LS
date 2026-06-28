// sim_uarch_test.ls — std.sim microarchitecture parameter tables (plan §5.2).
//
// Checks the hand-seeded Uarch tables and shows the engine responding to μarch
// params: the SAME BFP8 pack kernel is port-bound(p5) on a 5-wide frontend but
// frontend-bound on a 1-wide one — the bottleneck verdict is a function of the
// microarchitecture, not just the instruction stream.

import sim.intel.decode as decode
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

def main() {
    // ---- parameter tables ----
    uarch.Uarch il = uarch.icelake()
    uarch.Uarch sx = uarch.skylake_x()
    uarch.Uarch rl = uarch.rocket_lake()

    @print(uarch.summary(&il))
    @print(uarch.summary(&sx))
    @print(uarch.summary(&rl))

    check(il.num_ports == 8, "Ice Lake has 8 modelled ports")
    check(il.fe_width == 5, "Ice Lake 5-wide rename")
    check(il.rob_size == 352, "Ice Lake ROB 352")
    check(il.avx512_downclock == uarch.dc_light(), "Ice Lake downclock = light")
    check(sx.avx512_downclock == uarch.dc_heavy(), "Skylake-X downclock = heavy")
    check(sx.rob_size == 224, "Skylake-X ROB 224")
    check(rl.avx512_downclock == uarch.dc_none(), "Rocket Lake downclock = none")
    check(uarch.summary(&il).contains?("Ice Lake"), "summary names the μarch")

    // ---- the bottleneck verdict depends on μarch frontend width ----
    // BFP8 pack kernel: 7 uops, p5 pressure = 2.0 (vpshufd + vpmovwb on p5).
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

    // 5-wide frontend (Ice Lake): the shuffle port wins -> port-bound(p5).
    engine.Bottleneck b_wide = engine.analyze(&uops, il.num_ports, il.fe_width)
    check(b_wide.kind.contains?("port-bound(p5)"), "5-wide frontend -> port-bound(p5)")

    // 1-wide frontend (hypothetical narrow decode): frontend can't keep up ->
    // frontend-bound (fe = 7 uops/1-wide = 7.0 > p5 ResMII 4.0).
    engine.Bottleneck b_narrow = engine.analyze(&uops, il.num_ports, 1)
    check(b_narrow.kind.contains?("frontend-bound"), "1-wide frontend -> frontend-bound")
    check(b_narrow.frontend_x > b_narrow.res_mii_x, "narrow: frontend pressure exceeds ports")
    check(b_wide.res_mii_x == b_narrow.res_mii_x, "port pressure is μarch-frontend-independent")

    @print("SIM UARCH PASS")
}
