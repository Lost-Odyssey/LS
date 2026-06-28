// trait_conflict_reject.ls — negative: conflicting method names across traits
// should produce compile error: "conflicting method 'greet'"

interface Greet {
    def greet(&self) -> Str
}

interface HasValue {
    def greet(&self) -> int
}

struct Person {
    Str name
    int age
}

methods Person: Greet {
    def gr lock serialises them, so the final length is EXACTLY N*per. Both Mutex and
// SpinLock are exercised, each guarding its own global Vec.
//
// The lock and the Vec are GLOBALS the closures name directly (globals are
// shared by reference, not captured). Workers use the raw lock!/unlock! API.
//
// NO --memcheck (tracker not thread-safe — like std.sync.task). Correctness +
// heap-soundness via the exact length over repeated AOT runs.

import std.core.vec
import std.sync.lock
import std.sync.task

Mutex g_mtx = {}
Vec(int) g_mdata = []

SpinLock g_spin = {}
Vec(int) g_sdata = []

def main() {
    g_mtx.init()
    int N = 8
    int per = 5000

    Vec(Task(int)) tasks = []
    for i in 0..N {
        Task(int) t = {}
        t.run(|| {
            for j in 0..5000 {
                g_mtx.lock!()
                g_mdata.push(1)
                g_mtx.unlock!()
                g_spin.lock!()
                g_sdata.push(1)
                g_spin.unlock!()
            }
            return 0
        })
        tasks.push(t)
    }
    for i in 0..N { int r = tasks.get!(i).join() }

    int expect = N * per   // 40000
    g_mtx.lock!()
    int mlen = g_mdata.len()
    g_mtx.unlock!()
    g_spin.lock!()
    int slen = g_sdata.len()
    g_spin.unlock!()

    if mlen != expect { @print("SYNC FAIL mutex len") @print(mlen) return }
    if slen != expect { @print("SYNC FAIL spin len") @print(slen) return }
    @print("SYNC OK")
}
