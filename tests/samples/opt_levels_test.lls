// Optimization-pipeline regression (docs/plan_opt_pipeline.md).
// A reduction loop that the pipeline may fold / vectorize at higher levels; the
// result must be identical regardless of -O level or --native. The driver runs
// this at -O0/-O2/-O3 (JIT) and -O0/-O3 --native (AOT) and checks "OPT PASS".
def main() {
    i64 acc = 0
    for i in 0..10000 {
        acc = acc + (i as i64) * 3
    }
    // sum_{i=0}^{9999} 3*i = 3 * (9999*10000/2) = 149985000
    if acc == 149985000 {
        @print("OPT PASS")
    } else {
        @print(f"OPT FAIL acc={acc}")
    }
}
