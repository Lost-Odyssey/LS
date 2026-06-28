// std.sync.ring MPMC — many producers + many consumers, LOCK-FREE (CAS reservation).
// new_mpmc_ring uses prod_head/cons_head CAS to hand each producer/consumer a
// unique slot, so there is no mutex. POD path proves exact conservation (count +
// sum); the Str path is the sharper test — if two consumers reserved the SAME
// slot, the second __take would double-free a string (crash), so a clean run
// over owned has_drop elements proves the reservation is correct.
//
// Ring has no close, so consumers stop once the global received count hits the
// known total (each consumer: dequeue if available, else cpu_relax and re-check).
//
// NO --memcheck (tracker not thread-safe). Soundness via JIT + repeated AOT runs
// (a double-take crashes; a lost slot hangs the count below total → TIMEOUT).

import std.sync.ring
import std.sync.task
import std.sync.atomic
import std.core.vec

Ring(int) g_ints = {}
Ring(Str) g_strs = {}
Atomic(int) g_recv = {}
Atomic(i64) g_sum = {}
Atomic(int) g_srecv = {}
Atomic(i64) g_sbytes = {}

def main() {
    int NP = 4
    int NC = 4

    // ---------------- POD: 4 producers x 25000, 4 consumers ----------------
    g_ints = new_mpmc_ring(int)(1024)
    int PER = 25000
    int TOTAL = NP * PER          // 100000

    Vec(Task(int)) cons = []
    for ci in 0..NC {
        Task(int) t = {}
        t.run(|| {
            while true {
                if g_recv.get() >= 100000 { return 0 }
                match g_ints.dequeue() {
                    Some(x) => {
                        int a = g_recv.fetch_add(1)
                        i64 b = g_sum.fetch_add(x as i64)
                    }
                    None => { __cpu_relax() }
                }
            }
            return 0
        })
        cons.push(t)
    }
    Vec(Task(int)) prods = []
    for pi in 0..NP {
        Task(int) t = {}
        t.run(|| {
            for j in 0..25000 {
                while !g_ints.enqueue(j) { __cpu_relax() }   // int = copy, retry safe
            }
            return 0
        })
        prods.push(t)
    }
    for pi in 0..NP { int rp = prods.get!(pi).join() }
    for ci in 0..NC { int rc = cons.get!(ci).join() }

    if g_recv.get() != TOTAL { @print("MPMC FAIL pod count"); @print(g_recv.get()); return }
    i64 psum = (PER as i64) * ((PER as i64) - 1) / 2
    if g_sum.get() != psum * (NP as i64) { @print("MPMC FAIL pod sum"); @print(g_sum.get()); return }
    @print("MPMC OK pod")

    // ---------------- Str: owned has_drop across MPMC ----------------
    g_strs = new_mpmc_ring(Str)(512)
    int SPER = 10000
    int STOTAL = NP * SPER        // 40000

    Vec(Task(int)) scons = []
    for ci in 0..NC {
        Task(int) t = {}
        t.run(|| {
            while true {
                if g_srecv.get() >= 40000 { return 0 }
                match g_strs.dequeue() {
                    Some(s) => {
                        int a = g_srecv.fetch_add(1)
                        i64 b = g_sbytes.fetch_add(s.len() as i64)
                    }
                    None => { __cpu_relax() }
                }
            }
            return 0
        })
        scons.push(t)
    }
    Vec(Task(int)) sprods = []
    for pi in 0..NP {
        Task(int) t = {}
        t.run(|| {
            for j in 0..10000 {
                Str s = f"x{j}"
                while !g_strs.enqueue(s) { __cpu_relax() }    // named var = clone, retry safe
            }
            return 0
        })
        sprods.push(t)
    }
    for pi in 0..NP { int rp = sprods.get!(pi).join() }
    for ci in 0..NC { int rc = scons.get!(ci).join() }

    if g_srecv.get() != STOTAL { @print("MPMC FAIL str count"); @print(g_srecv.get()); return }
    @print("MPMC OK str")
    @print(g_sbytes.get())
}
