// std/par.ls — data-parallel for-loop over OS worker threads.
//
//   import std.par
//   par_for(0, n, |i| { g_out.set!(i, f(i)) })   // body runs in parallel
//
// par_for splits [start, end) into chunks (one per logical CPU, capped at the
// iteration count) and runs each chunk on its own Task(int) worker. The chunk
// closure captures `body` BY-CLONE (closure-foundation Phase A): each worker
// gets an independent deep copy of the body's env, so the source `body` stays
// live to be captured again on the next chunk, and each worker drops exactly
// its own clone (no shared-env double-free). Chunk bounds are POD by-copy.
//
// CONCURRENCY CONTRACT (the compiler does NOT check this): body(i) must be
// independent across i — writes must not overlap (e.g. pre-size a Vec and use
// `set!` to distinct indices) or must be synchronised via std.atomic / std.sync.
// Two workers mutating the same slot, or growing a shared Vec concurrently, is
// a data race / UB. See docs/plan_atomic_mutex.md.

import std.task
import std.vec
import std.c as c

fn par_for(int start, int stop, Block(int) body) {
    int n = stop - start
    if n <= 0 { return }

    int p_count = c.__ls_cpu_count()
    if p_count > n { p_count = n }
    int chunk = (n + p_count - 1) / p_count        // ceil-divide

    Vec(Task(int)) tasks = {}
    for p in 0..p_count {
        int lo = start + p * chunk
        int hi = lo + chunk
        if hi > stop { hi = stop }
        if lo >= hi { continue }                   // nothing left for this worker
        Task(int) t = {}
        t.run(|| {                                 // captures body(by-clone)+lo/hi(POD)
            for i in lo..hi { body(i) }
            return 0
        })
        tasks.push(t)
    }

    // Join every worker (terminal — each Task is joined exactly once).
    for k in 0..tasks.len() {
        int r = tasks.get!(k).join()
    }
}
