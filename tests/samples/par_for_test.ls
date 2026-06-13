// Closure-foundation Phase A verification: par_for runs a Block(int) body in
// parallel across worker threads, each chunk's closure capturing `body`
// by-clone. Workers write to DISJOINT, pre-allocated slots of a global Vec(int)
// (no overlap → no synchronisation needed), then we join and check every slot.
// Self-verifying: prints "PAR PASS" only if all N results are correct.

import std.par as par
import std.vec

int N = 2000

Vec(int) g_out = {}

fn compute(int i) -> int {
    // a little work so chunks aren't trivially instant
    int acc = i
    int j = 0
    for j in 0..50 { acc = acc + i * 3 - j }
    return acc
}

fn main() {
    bool ok = true

    // pre-size g_out to N slots so workers only set! (never grow concurrently)
    int z = 0
    for z in 0..N { g_out.push(0) }

    // run compute(i) -> slot i in parallel
    par.par_for(0, N, |i| { g_out.set!(i, compute(i)) })

    // verify every slot against the sequential result
    int k = 0
    for k in 0..N {
        if g_out.get!(k) != compute(k) {
            print(k)
            print("PAR FAIL")
            ok = false
        }
    }

    if ok { print("PAR PASS") }
}
