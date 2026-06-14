// Closure-foundation Phase B — headline integration: a nested closure literal
// inside a Task worker closure, touching a shared global Atomic.
//
// Each task body (the enclosing closure O, run on an OS worker thread) contains
// a NESTED closure `add` that (a) accesses the global Atomic g_counter directly
// (globals are shared by reference across threads, not captured) and (b) captures
// the function-scope POD `step` transitively. This is the v1 stand-in for
// `t.run(|| { ...; m.with(|v| ...); ... })` (Mutex.with-in-thread) — Mutex is a
// bare lock today, so we use a shared Atomic to prove nested closures work inside
// thread closures.
//
// Self-verifying: prints "NCT PASS" only if the shared counter lands on 120.
// NO --memcheck (the tracker is not thread-safe — same as task/sync); a
// cross-thread double-free of a worker's nested-closure env would surface as an
// intermittent crash, so the cmake driver runs the AOT binary several times.

import std.task
import std.atomic

type IntFn = Block(int) -> int

Atomic(int) g_counter = {}

fn main() {
    int step = 10
    Task(int) t1 = {}
    Task(int) t2 = {}
    Task(int) t3 = {}
    Task(int) t4 = {}
    // |k| g_counter.fetch_add(k*step): nested closure capturing `step` transitively
    // and touching the shared global. Each task adds (1+2)*10 = 30.
    t1.run(|| { IntFn add = |k| g_counter.fetch_add(k * step) add(1) add(2) return 0 })
    t2.run(|| { IntFn add = |k| g_counter.fetch_add(k * step) add(1) add(2) return 0 })
    t3.run(|| { IntFn add = |k| g_counter.fetch_add(k * step) add(1) add(2) return 0 })
    t4.run(|| { IntFn add = |k| g_counter.fetch_add(k * step) add(1) add(2) return 0 })
    int r = t1.join() + t2.join() + t3.join() + t4.join()
    if g_counter.get() == 120 { print("NCT PASS") } else { print(g_counter.get()) print("NCT FAIL") }
}
