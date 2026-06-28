// sim_branch_test.ls — unit test for sim.intel.branch (the moat module).
//
// Exercises the distribution + branch-misprediction math IN ISOLATION (component
// costs fed by hand here; the engine-1 auto-derivation lives in the early-exit
// integration test). The headline property: ONE model, fed two different exit
// distributions, FLIPS the hoist-vs-batch verdict — which llvm-mca/uiCA/SDE
// cannot do (they emit one number regardless of the distribution).

import sim.intel.branch as br
import std.core.vec
import std.core.str

def check(bool cond, Str name) {
    if cond { @print(f"  ok: {name}") }
    else { @print(f"  FAIL: {name}") }
}

// x100 fixed-point -> "N.NN"
def d2(int x100) -> Str {
    int w = x100 / 100
    int f = x100 - w * 100
    Str fs = f"{f}"
    if f < 10 { fs = f"0{fs}" }
    return f"{w}.{fs}"
}

def main() {
    @print("=== sim.branch: distribution-driven early-exit cost model ===")
    @print("")

    // component costs (centi-cycles) — same as the validated prototype, so this
    // isolates branch.ls's math. Auto-derived from engine-1 in the integration test.
    int c_common = 600     // 6.00 c/PRB  shared pack stage
    int c_reduce = 600     // 6.00 c/PRB  batch4 constant reduction
    int c_setup  = 150     // 1.50 c      hoist serial prefix
    int c_block  = 50      // 0.50 c      one scan block
    br.BranchModel m = br.branch_model_icx()

    // ---- distribution 1: product 3/80/17 (user's real ICX signal) ----
    br.ExitDist prod = br.exit_dist()
    br.ed_add(&!prod, 1, 30)
    br.ed_add(&!prod, 2, 800)
    br.ed_add(&!prod, 3, 170)
    check(br.total_permille(&prod) == 1000, "product dist sums to 1000 permille")
    int eb_p = br.expected_blocks_x100(&prod)
    check(eb_p == 214, "product E[k] = 2.14")
    int mr_p = br.mispred_permille(&m, &prod)
    check(mr_p == 40, "product mispredict = (1-0.8)^2 = 0.04 = 40 permille")

    int hp = br.hoist_total_x100(c_common, c_setup, c_block, &prod, &m)
    int bp = br.batch_total_x100(c_common, c_reduce)
    int sp_p = br.speedup_x100(bp, hp)
    @print(f"  [product 3/80/17]  E[k]={d2(eb_p)}  miss={mr_p}/1000")
    @print(f"    batch={d2(bp)}  hoist={d2(hp)}  speedup(b4/hoist)={d2(sp_p)}x")
    check(sp_p > 100, "product: HOIST wins (distribution concentrated)")
    check(sp_p >= 125, "product: hoist ~30% faster (matches ICX ground truth)")
    check(sp_p <= 135, "product: speedup near 1.3x (not wildly off)")
    @print("")

    // ---- distribution 2: uniform 1..8 (spike's synthetic SDE study) ----
    br.ExitDist uni = br.exit_dist()
    for k in 1..9 { br.ed_add(&!uni, k, 125) }     // 8 x 125 = 1000
    check(br.total_permille(&uni) == 1000, "uniform dist sums to 1000 permille")
    int eb_u = br.expected_blocks_x100(&uni)
    check(eb_u == 450, "uniform E[k] = 4.50")

    int hu = br.hoist_total_x100(c_common, c_setup, c_block, &uni, &m)
    int bu = br.batch_total_x100(c_common, c_reduce)
    int sp_u = br.speedup_x100(bu, hu)
    @print(f"  [uniform 1..8]  E[k]={d2(eb_u)}  miss={br.mispred_permille(&m, &uni)}/1000")
    @print(f"    batch={d2(bu)}  hoist={d2(hu)}  speedup(b4/hoist)={d2(sp_u)}x")
    check(sp_u < 100, "uniform: BATCH wins (distribution spread, mispredicts dominate)")
    @print("")

    // ---- the headline: ONE model, the verdict FLIPS with the distribution ----
    check(sp_p > 100 && sp_u < 100, "SAME model flips verdict with distribution (the moat)")

    // ---- swappable branch model: a cheaper-mispredict uarch shifts the magnitude ----
    br.BranchModel m_cheap = br.branch_model(400, 0)   // 4c penalty instead of 16c
    int hp_cheap = br.hoist_total_x100(c_common, c_setup, c_block, &prod, &m_cheap)
    check(hp_cheap < hp, "cheaper mispredict penalty lowers hoist cost (model is swappable)")
    @print(f"  swappable: ICX(16c) hoist={d2(hp)}  vs cheap(4c) hoist={d2(hp_cheap)}")
    @print("")

    // ---- predictor models + temporal correlation (the enhanced moat) ----
    // per-block conditional exit prob: which loop-exit branches are biased.
    Vec(int) ckp = br.conditional_exit_permille(&prod)
    check(ckp.get!(0) == 30, "c_1 = P(exit@1 | reached) = 30/1000 (rarely exits at block 1)")
    check(ckp.get!(2) == 1000, "c_3 = 1000/1000 (if reached, always exits at the last block)")
    @print(f"  product per-block exit prob: c1={ckp.get!(0)} c2={ckp.get!(1)} c3={ckp.get!(2)} /1000")

    // four predictor models on the same distribution (mispredicts/1000 per traversal)
    br.BranchModel p0 = br.branch_model(1600, 0)   // concentration (1-p)^2
    br.BranchModel p1 = br.branch_model(1600, 1)   // bimodal per-block
    br.BranchModel p2 = br.branch_model(1600, 2)   // loop predictor
    br.BranchModel p3 = br.branch_model_loop(1600) // loop + correlation
    br.ed_set_rho(&!prod, 900)                     // measured temporal correlation 0.90
    @print(f"  predictors: concentration={br.mispred_permille(&p0,&prod)} bimodal={br.mispred_permille(&p1,&prod)} loop={br.mispred_permille(&p2,&prod)} loop+corr={br.mispred_permille(&p3,&prod)} /1000")
    check(br.mispred_permille(&p2, &prod) == 200, "loop predictor misses non-dominant trips: 1-0.8 = 200/1000")
    check(br.mispred_permille(&p3, &prod) == 20, "correlation rho=0.9 cuts loop mispredicts 10x: 200*(1-0.9) = 20")

    // the headline of the ENHANCED moat: correlation is the tipping factor. The loop
    // predictor alone (no correlation) over-penalizes hoist; modeling temporal
    // correlation widens the hoist margin dramatically. With these (prototype) costs
    // it goes 1.01x -> 1.34x; with the engine-derived costs in sim_earlyexit_test the
    // verdict actually FLIPS (no-corr -> batch wins).
    int h_p2 = br.hoist_total_x100(c_common, c_setup, c_block, &prod, &p2)
    int h_p3 = br.hoist_total_x100(c_common, c_setup, c_block, &prod, &p3)
    int bp2 = br.batch_total_x100(c_common, c_reduce)
    int marg_p2 = br.speedup_x100(bp2, h_p2)
    int marg_p3 = br.speedup_x100(bp2, h_p3)
    check(marg_p3 - marg_p2 >= 25, "temporal correlation widens the hoist margin substantially")
    check(marg_p3 > 125, "loop predictor WITH correlation: clear hoist win (matches ICX)")
    @print(f"  correlation effect: no-corr={d2(marg_p2)}x  ->  corr={d2(marg_p3)}x (loop predictor)")

    @print("SIM BRANCH PASS")
}
