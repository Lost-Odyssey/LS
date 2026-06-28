// rwlock_thread_test.ls — RwLock(T) cross-thread: 8 workers each do 5000
// (exclusive write-push + a shared read) on a global RwLock(Vec(int)); the
// writer side serialises the non-atomic push → exact 40000, and the concurrent
// reads exercise the shared side without corrupting it. NO --memcheck (tracker
// not thread-safe, same as task/sync); correctness via exact count + AOT x8.
import std.core.vec
import std.sync.task
import std.sync.lock

RwLock(Vec(int)) g_data = {}

def worker() -> int {
    for i in 0..5000 {
        g_data.write(|v| { v.push(1) })
        int seen = g_data.read(int)(|v| { return v.len() })   // concurrent reads
    }
    return 0
}

def main() -> int {
    g_data.init()
    int N = 8
    Vec(Task(int)) tasks = []
    for w in 0..N {
        Task(int) t = {}
        t.run(|| { return worker() })
        tasks.push(t)
    }
    for w in 0..N { tasks[w].join() }

    int total = g_data.read(int)(|v| { return v.len() })
    if (total == 40000) { @print("RWLOCK OK") } else { @print(f"RWLOCK FAIL total={total}") }
    return 0
}
