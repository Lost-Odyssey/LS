// A (FMA contract) + B (noreturn/cold abort path) — docs/plan_fma_coldpath.md.
// FP accumulation `acc = acc + x*y` exercises the contract flag; an in-bounds
// v[i] read exercises the (cold) bounds-check path without aborting. Correctness
// is the gate — FMA must not change the (tolerance-checked) result, and the cold
// annotation must not perturb normal control flow.
import std.core.vec

def dot(&Vec(f64) a, &Vec(f64) b) -> f64 {
    f64 acc = 0.0
    for i in 0..a.len() { acc = acc + a.get!(i) * b.get!(i) }  // a*b+c → fma
    return acc
}

def main() {
    Vec(f64) a = {}
    Vec(f64) b = {}
    for i in 0..1000 {
        a.push(((i % 7) + 1) as f64)
        b.push(((i % 5) + 1) as f64)
    }
    f64 d = dot(&a, &b)            // deterministic dot product

    // bounds-checked (cold path present, taken in-range)
    int n = a.len()
    f64 last = a[n - 1]           // a[999] = (999%7)+1 = 6.0

    // expected dot = sum over i of ((i%7)+1)*((i%5)+1), i=0..999 = 11996.0
    bool ok = true
    if d < 11995.5 { ok = false }      // tolerance for FMA rounding
    if d > 11996.5 { ok = false }
    if last < 5.5 { ok = false }
    if last > 6.5 { ok = false }
    if ok {
        @print("FMACOLD PASS")
    } else {
        @print(f"FMACOLD FAIL d={d} last={last}")
    }
}
