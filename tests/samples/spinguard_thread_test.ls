// spinguard_thread_test.ls — SpinGuard(T) cross-thread mutual exclusion.
// 8 workers each push 5000 into a shared global SpinGuard(Vec(int)); the spin
// lock serialises the non-atomic push → exact 40000. NO --memcheck (tracker not
// thread-safe, same as task/sync); correctness via exact count + AOT x8.
import std.core.vec
import std.sync.task
import std.sync.lock

SpinGuard(Vec(int)) g_data = {}

def worker() -> int {
    for i in 0..5000 {
        g_data.lock(|v| { v.push(1) })
    }
    return 0
}

def main() -> int {
    int N = 8
    Vec(Task(int)) tasks = []
    for w in 0..N {
        Task(int) t = {}
        t.run(|| { return worker() })
        tasks.push(t)
    }
    for w in 0..N { tasks[w].join() }

    int total = g_data.get(int)(|v| { return v.len() })
    if (total == 40000) { @print("SPINGUARD OK") } else { @print(f"SPINGUARD FAIL total={total}") }
    return 0
}
