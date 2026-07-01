// std.sync.chan MPMC blocking — N producer threads + M consumer threads over a
// SHARED global Chan. The mutex makes it MPMC-safe; the condvars make send block
// while full and recv block while empty, so this genuinely drives __cond_wait on
// both sides (the channel capacity is far smaller than the total, so producers
// WILL block on full and consumers WILL block on empty).
//
// Protocol: spawn consumers + producers concurrently. Producers each send PER
// items, then main joins all producers and close()s the channel. close()
// broadcasts not_empty, so blocked consumers wake, drain the remainder, then see
// (closed && empty) -> None and exit. Verify exact total count + sum.
//
// NO --memcheck (tracker not thread-safe — same as task/atomic/sync). Soundness
// via exact count/sum over repeated AOT runs (a lost wakeup would HANG; a torn
// move would crash or skew the totals).

import std.sync.chan
import std.sync.task
import std.sync.atomic
import std.core.vec

Chan(int) g_ch = {}
Atomic(int) g_recv = {}
Atomic(i64) g_sum = {}

def main() {
    g_ch = channel(int)(64)        // small cap vs 100k total -> real blocking
    int NP = 4
    int NC = 4
    int PER = 25000
    int TOTAL = NP * PER           // 100000

    // consumers: recv until the channel is closed AND drained
    Vec(Task(int)) cons = []
    for ci in 0..NC {
        Task(int) t = {}
        t.run(|| {
            while true {
                match g_ch.recv() {
                    Some(x) => {
                        int pa = g_recv.fetch_add(1)
                        i64 pb = g_sum.fetch_add(x as i64)
                    }
                    None => { return 0 }
                }
            }
            return 0
        })
        cons.push(t)
    }

    // producers: each sends 0..PER-1 (POD int -> send blocks if full)
    Vec(Task(int)) prods = []
    for pi in 0..NP {
        Task(int) t = {}
        t.run(|| {
            for j in 0..25000 {
                bool ok = g_ch.send(j)
            }
            return 0
        })
        prods.push(t)
    }

    for pi in 0..NP { int rp = prods.get!(pi).join() }
    g_ch.close()                   // after all producers done
    for ci in 0..NC { int rc = cons.get!(ci).join() }

    if g_recv.get() != TOTAL {
        @print("CHAN FAIL count")
        @print(g_recv.get())
        return
    }
    i64 psum = (PER as i64) * ((PER as i64) - 1) / 2
    if g_sum.get() != psum * (NP as i64) {
        @print("CHAN FAIL sum")
        @print(g_sum.get())
        return
    }
    @print("CHAN OK mpmc")
}
