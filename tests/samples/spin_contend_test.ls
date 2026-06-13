// std.sync — SpinLock under HEAVY contention with a LONG critical section, to
// exercise the adaptive backoff's yield path (__cpu_yield after SPIN_BEFORE_YIELD
// failed tries) and confirm it stays correct and DEADLOCK-FREE.
//
// Many workers (> typical core count) hammer ONE SpinLock; each holds it for a
// burst of pushes, so waiters spin long past the pause-only threshold and fall
// through to yielding the core. The final length must be exact (mutual exclusion
// held) AND the program must terminate (no priority-inversion starvation /
// livelock under the yield backoff).
//
// NO --memcheck (tracker not thread-safe). Correctness via exact length + the
// fact that it finishes, over repeated AOT runs.

import std.vec
import std.sync
import std.task

SpinLock g_lock = {}
Vec(int) g_data = []

fn main() {
    int M = 12       // more workers than cores → guaranteed contention
    int rounds = 80
    int burst = 50   // long critical section → waiters spin past the yield threshold

    Vec(Task(int)) tasks = []
    for i in 0..M {
        Task(int) t = {}
        t.run(|| {
            for r in 0..80 {
                g_lock.lock!()
                for b in 0..50 { g_data.push(1) }
                g_lock.unlock!()
            }
            return 0
        })
        tasks.push(t)
    }
    for i in 0..M { int r = tasks.get!(i).join() }

    int expect = M * rounds * burst   // 12 * 80 * 50 = 48000
    g_lock.lock!()
    int len = g_data.len()
    g_lock.unlock!()
    if len != expect { print("SPIN FAIL len") print(len) return }
    print("SPIN OK")
}
