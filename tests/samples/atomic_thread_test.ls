// std.atomic — cross-thread atomicity. N workers hammer a SHARED global
// Atomic; without real atomicity the increments would race and lose updates.
// The global is named directly by every closure (globals are shared by
// reference, not captured), which is what makes it shared across workers.
//
// NO --memcheck (the tracker is not thread-safe — same as std.task). Soundness
// + correctness are checked by the exact final count over repeated AOT runs.

import std.vec
import std.task
import std.atomic

Atomic(int) g_counter = {}
Atomic(i64) g_sum = {}

fn main() {
    int N = 8
    int per = 10000

    Vec(Task(int)) tasks = []
    for i in 0..N {
        Task(int) t = {}
        t.run(|| {
            for j in 0..10000 {
                g_counter.fetch_add(1)
                g_sum.fetch_add(2)
            }
            return 0
        })
        tasks.push(t)
    }
    for i in 0..N {
        int r = tasks.get!(i).join()
    }

    int expect = N * per           // 80000
    if g_counter.get() != expect {
        print("ATOMIC FAIL counter")
        print(g_counter.get())
        return
    }
    if g_sum.get() != (expect as i64) * 2 {
        print("ATOMIC FAIL sum")
        return
    }
    print("ATOMIC OK")
}
