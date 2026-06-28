// guard_thread_test.ls — Guard(T) cross-thread mutual exclusion.
// N workers each push 5000 ints into a SHARED GLOBAL Guard(Vec(int)); the lock
// serialises the non-atomic Vec.push → exact final count. A forgotten lock (or
// broken guard) loses updates or crashes. NO --memcheck (tracker not thread-
// safe, same as task/sync); correctness via exact count + repeated AOT runs.
import std.core.vec
import std.sync.task
import std.sync.lock

Guard(Vec(int)) g_data = {}

def worker() -> int {
    for i in 0..5000 {
        g_data.lock(|v| { v.push(1) })     // nested closure inside the task closure
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
    for w in 0..N {
        tasks[w].join()
    }

    int total = g_data.get(int)(|v| { return v.len() })
    if (total == 40000) {
        @print("GUARD OK")
    } else {
        @print(f"GUARD FAIL total={total}")
    }
    return 0
}
