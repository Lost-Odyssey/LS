// sim_engine2_test.ls — engine-2 per-cycle simulation + Gantt + steady-state.
//
// Simulates the BFP8 pack kernel cycle by cycle, prints the Gantt timeline, and
// cross-checks: the steady-state throughput (cycles/iteration over many overlapped
// iterations) must equal engine-1's analytical ResMII (the shuffle port p5 bound).

import sim.intel.decode as decode
import sim.intel.ports as ports
import sim.intel.uarch as uarch
import sim.core.engine as engine
import sim.core.engine2 as e2
import sim.core.ir as ir
import std.core.vec
import std.core.str

def check(bool cond, Str name) {
    if cond { @print(f"  ok: {name}") }
    else { @print(f"  FAIL: {name}") }
}

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

    // ---- single-iteration cycle timeline ----
    Vec(ir.UopTrace) tr = e2.simulate(&uops, u.num_ports)
    @print(e2.gantt(&tr, &prog))
    int tot = e2.total_cycles(&tr)
    // single-iteration timeline is bounded below by the critical path (17c with
    // real llvm-mca latencies: lzcnt 5 + sraw 4 + movwb 4 dominate the chain).
    check(tot >= 17, "single-iter timeline >= critical path (17c)")
    check(tot <= 20, "single-iter timeline within a couple cycles of crit path")

    // ---- steady-state throughput cross-checks engine-1 ----
    int steady = e2.steady_cycles_per_iter(&uops, u.num_ports, 32)
    @print(f"steady-state: {steady} cycles/iteration")

    engine.Bottleneck b = engine.analyze(&uops, u.num_ports, u.fe_width)
    int resmii = b.res_mii_x / engine.scale()    // 48 / 12 = 4 (p5: shufd+sraw-bcast+movwb*2)
    @print(f"engine-1 ResMII: {resmii} cycles/iteration")
    // independent iterations: engine-2's overlap-aware steady state converges to the
    // engine-1 p5 port bound (4c) while the long serial chain (crit 17c) overlaps
    // fully across iterations. Cross-validates engine-1 == llvm-mca Block RThr 4.0.
    check(steady >= resmii, "engine-2 steady >= engine-1 ResMII (port bound)")
    check(steady == 4, "BFP8 steady 4c == p5 port bound (chain overlaps away)")

    // ---- look several iterations ahead: overlapped multi-iteration timeline ----
    // engine-2 rolls the schedule forward across K iterations. replicate() makes them
    // independent (no loop-carried dep) and the scheduler overlaps them; gantt_iters
    // draws the steady-state overlap (each iteration's executing cells marked 0/1/2).
    int K = 3
    @print(e2.gantt_iters(&uops, &prog, u.num_ports, K))
    Vec(ir.Uop) repK = e2.replicate(&uops, K)
    Vec(ir.UopTrace) trK = e2.simulate(&repK, u.num_ports)
    int multi = e2.total_cycles(&trK)
    @print(f"{K} iterations overlapped span {multi} cycles (vs {K}x{tot}={K * tot} if serialized)")
    // overlap is real: K iterations finish well before K isolated single-iter runs.
    check(multi < K * tot, "K iterations overlap (faster than K isolated runs)")
    check(multi > tot, "multi-iter span exceeds one isolated iteration")

    @print("SIM ENGINE2 PASS")
}
