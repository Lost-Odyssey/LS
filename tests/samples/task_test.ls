// std.task — generic structured concurrency, end to end. Validates Task(T):
//
//   Task(T) t = {}            // construct an unstarted task
//   t.run(|| body)            // run the body on an OS worker thread
//   T r = t.join()            // wait, MOVE the result back
//
// The body MOVE-captures owned heap (Vec/Str), so each task is single-owner and
// sound across the thread boundary (no auto-drop double-free). Covers POD and
// aggregate results, fork/join over Vec(Task(Vec(f64))), and Task as a first-
// class field. NO --memcheck (the tracker is not thread-safe); soundness is
// covered by repeated AOT runs (a cross-thread double-free surfaces as a crash).

import std.vec
import std.str
import std.task

// Each worker squares [base, base+n) and returns the chunk (aggregate result).
fn squares(int base, int n) -> Vec(f64) {
    Vec(f64) v = []
    for (int i = 0; i < n; i = i + 1) {
        f64 x = (base + i) as f64
        v.push(x * x)
    }
    return v
}

// Task held as a first-class struct field.
struct Holder { Task(int) t }

fn main() {
    // ---- POD result ----
    Task(int) ti = {}
    ti.run(|| 6 * 7)
    if ti.join() != 42 { print("TASK FAIL pod") return }

    // ---- aggregate move-out: a Vec move-captured into the worker, mutated,
    //      and moved back out. `seed` is MOVED after run. ----
    Vec(int) seed = [10, 20, 30]
    Task(Vec(int)) tv = {}
    tv.run(|| {
        Vec(int) acc = seed
        acc.push(40)
        return acc
    })
    Vec(int) rv = tv.join()
    int vsum = 0
    for x in rv { vsum = vsum + x }
    if vsum != 100 { print("TASK FAIL vec") return }

    // ---- Str result (owned heap moved out) ----
    Task(Str) ts = {}
    ts.run(|| {
        Str z = "ab"
        z = z + "cd"
        return z
    })
    Str rs = ts.join()
    if rs.len() != 4 { print("TASK FAIL str") return }

    // ---- fork/join map-reduce: Vec(Task(Vec(f64))) — Task as a first-class
    //      element type. Spawn 4 workers, each returns a Vec(f64); join all,
    //      reduce. Sum of k^2 for k in 0..11 == 506. ----
    Vec(Task(Vec(f64))) workers = []
    for (int k = 0; k < 4; k = k + 1) {
        Task(Vec(f64)) w = {}
        w.run(|| squares(k * 3, 3))
        workers.push(w)
    }
    f64 total = 0.0
    for (int i = 0; i < workers.len(); i = i + 1) {
        Task(Vec(f64)) w = workers.get!(i)
        Vec(f64) chunk = w.join()
        for v in chunk { total = total + v }
    }
    if total != 506.0 { print("TASK FAIL forkjoin") return }

    // ---- Task as a struct field ----
    Holder h = {}
    h.t.run(|| 77)
    if h.t.join() != 77 { print("TASK FAIL field") return }

    print("TASK OK")
}
