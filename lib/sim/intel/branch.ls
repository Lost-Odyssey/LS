// sim.intel.branch — data-dependent early-exit + branch-misprediction cost model.
//
// THE capability that beats llvm-mca / uiCA / SDE (docs/plan_std_sim.md §10.2,
// conclusion 3). Those tools structurally cannot model a data-dependent branch
// distribution: they run all blocks of a loop and emit ONE number regardless of
// how often the early-exit fires. This module predicts the *expected* executed
// block count E[k] from an exit distribution and adds a branch-misprediction term,
// so the hoist-scan-vs-batch verdict FLIPS with the workload — the moat.
//
// Layering (plan §10.2): the per-cycle engine (port/latency) is a solved problem
// (engine-1 / engine-2 ≈ llvm-mca). This module sits ON TOP of it and supplies the
// two things the engine cannot: the distribution-driven block count and the
// mispredict penalty. The engine still provides the component cycle costs.
//
// Conventions (shared with the early-exit test): all costs in CENTI-CYCLES
// (cycles x100); all probabilities in PERMILLE (x1000). Integer math only.
//
// Pure LS, zero compiler changes (same discipline as lib/oran).

import std.core.vec
import std.core.str

// ============================================================================
// Exit distribution: P(the data-dependent loop exits at block k), in permille.
// k is 1-based (block 1 = first scan). The probabilities should sum to ~1000.
// ============================================================================
struct ExitDist {
    Vec(int) ks            // exit block index (>= 1)
    Vec(int) probs         // P(exit at ks[i]) in permille
    int rho_permille       // temporal correlation of the workload (0=IID random,
                           // 1000=perfectly correlated). A real workload signal has
                           // high rho (adjacent PRBs have similar power -> the exit
                           // block barely moves -> the predictor stays warm), which
                           // is exactly why hoist beats batch on it. A synthetic
                           // rand%K "mixed" workload has rho=0.
}

// empty distribution (rho defaults to 0 = IID); build with ed_add.
def exit_dist() -> ExitDist {
    Vec(int) k = {}
    Vec(int) p = {}
    return ExitDist { ks: k, probs: p, rho_permille: 0 }
}

// add one (block, probability-permille) point.
def ed_add(&!ExitDist d, int k, int permille) {
    d.ks.push(k)
    d.probs.push(permille)
}

// set the workload's temporal correlation (permille). Measured per-signal; the
// branch predictor exploits it (correlation-aware predictor below).
def ed_set_rho(&!ExitDist d, int rho_permille) {
    d.rho_permille = rho_permille
}

// per-block conditional exit probability c_k = P(exit=k | reached k), in permille.
// THE moat diagnostic no cycle tool exposes: which loop-exit branches are biased
// (c near 0 or 1000 = predictable) vs near 500 (a coin flip = unpredictable).
def conditional_exit_permille(&ExitDist d) -> Vec(int) {
    Vec(int) out = {}
    int reached = 1000     // P(reached the first block) = 1
    int n = d.ks.len()
    for i in 0..n {
        int p = d.probs.get!(i)
        int ck = 0
        if reached > 0 { ck = (p * 1000) / reached }
        out.push(ck)
        reached = reached - p
        if reached < 0 { reached = 0 }
    }
    return out
}

// E[k] x100  =  (Σ k * permille) / 10   (Σ k*permille is x1000; /10 -> x100).
def expected_blocks_x100(&ExitDist d) -> int {
    int acc = 0
    int n = d.ks.len()
    for i in 0..n { acc = acc + d.ks.get!(i) * d.probs.get!(i) }
    return acc / 10
}

// the most likely single exit block's probability (permille) — a concentration
// proxy the mispredict heuristic keys on.
def dominant_permille(&ExitDist d) -> int {
    int mx = 0
    int n = d.probs.len()
    for i in 0..n {
        int p = d.probs.get!(i)
        if p > mx { mx = p }
    }
    return mx
}

// total permille (should be ~1000); useful as a sanity assertion in tests.
def total_permille(&ExitDist d) -> int {
    int s = 0
    int n = d.probs.len()
    for i in 0..n { s = s + d.probs.get!(i) }
    return s
}

// ============================================================================
// Branch-misprediction model (swappable predictors). `predictor` selects the model;
// `penalty_cc` is a uarch property (ICX ~16c = 1600 centi-cycles). Returned rate is
// EXPECTED MISPREDICTS PER LOOP TRAVERSAL x1000 (can exceed 1000 for the per-block
// model, since one traversal evaluates several branches).
//
//   predictor 0  concentration : (1 - p_dom)^2          — IID concentration proxy
//   predictor 1  bimodal       : Σ P(reached k)·min(c_k, 1-c_k)
//                                — a per-branch 2-bit/bimodal predictor that bets the
//                                  majority outcome at each block and misses the rest
//   predictor 2  loop          : (1 - p_dom)
//                                — a loop predictor learns the dominant trip count and
//                                  mispredicts only when the actual exit block differs
//   predictor 3  loop+corr     : (1 - p_dom)·(1 - rho)
//                                — same, but temporal correlation keeps it warm; THIS
//                                  is what explains hoist winning on the correlated
//                                  signal (rho high) yet losing on synthetic uniform
//                                  (rho 0). The differentiator no cycle tool models.
// ============================================================================
struct BranchModel {
    int penalty_cc     // mispredict penalty, centi-cycles
    int predictor      // 0 concentration / 1 bimodal / 2 loop / 3 loop+correlation
}

def branch_model(int penalty_cc, int predictor) -> BranchModel {
    return BranchModel { penalty_cc: penalty_cc, predictor: predictor }
}

// Ice Lake / ICX default: ~16c penalty, concentration heuristic (back-compat).
def branch_model_icx() -> BranchModel {
    return BranchModel { penalty_cc: 1600, predictor: 0 }
}

// The principled model for the early-exit loop: a loop predictor that exploits the
// workload's temporal correlation (predictor 3). Use with ed_set_rho on the dist.
def branch_model_loop(int penalty_cc) -> BranchModel {
    return BranchModel { penalty_cc: penalty_cc, predictor: 3 }
}

// expected mispredicts per loop traversal x1000, under this model's predictor.
def mispred_permille(&BranchModel m, &ExitDist d) -> int {
    int pdom = dominant_permille(d)
    if m.predictor == 0 {
        int q = 1000 - pdom
        return (q * q) / 1000
    }
    if m.predictor == 2 {
        return 1000 - pdom
    }
    if m.predictor == 3 {
        int base = 1000 - pdom
        return (base * (1000 - d.rho_permille)) / 1000
    }
    // predictor 1: bimodal per-block majority predictor.
    Vec(int) ck = conditional_exit_permille(d)
    int reached = 1000
    int acc = 0
    int n = d.ks.len()
    for i in 0..n {
        int c = ck.get!(i)
        int miss = c
        int other = 1000 - c
        if other < miss { miss = other }      // min(c, 1-c)
        acc = acc + (reached * miss) / 1000
        reached = reached - d.probs.get!(i)
        if reached < 0 { reached = 0 }
    }
    return acc
}

// expected mispredict cost in centi-cycles = rate(permille) * penalty / 1000.
def mispred_cost_x100(&BranchModel m, &ExitDist d) -> int {
    return (mispred_permille(m, d) * m.penalty_cc) / 1000
}

// ============================================================================
// Early-exit cost synthesis (centi-cycles per loop iteration).
//
// A loop is split into component costs that the ENGINE supplies (port/latency
// layer), and this module composes them with the distribution + branch model:
//
//   common_cc    : per-iteration cost shared by both strategies (e.g. a SIMD pack),
//                  throughput-bound -> engine-1 ResMII.
//   HOIST (data-dependent early-exit):
//     setup_cc      : serial prefix paid once/iteration (load + abs + first test).
//     per_block_cc  : cost of ONE scan block; paid E[k] times on average.
//     + branch mispredict term (this module).
//   BATCH (constant):
//     reduce_cc     : the full reduction, always paid (no data-dependent branch).
// ============================================================================

// hoist end-to-end = common + setup + E[k]*per_block + mispredict
def hoist_total_x100(int common_cc, int setup_cc, int per_block_cc,
                     &ExitDist d, &BranchModel m) -> int {
    int eb = expected_blocks_x100(d)            // E[k] x100
    int scan = (eb * per_block_cc) / 100        // (x100 * x100) / 100 -> x100
    int mis = mispred_cost_x100(m, d)
    return common_cc + setup_cc + scan + mis
}

// batch end-to-end = common + full reduction (constant; branch model unused)
def batch_total_x100(int common_cc, int reduce_cc) -> int {
    return common_cc + reduce_cc
}

// speedup batch/hoist x100; >100 => hoist faster, <100 => batch faster.
def speedup_x100(int batch_cc, int hoist_cc) -> int {
    if hoist_cc <= 0 { return 0 }
    return (batch_cc * 100) / hoist_cc
}
