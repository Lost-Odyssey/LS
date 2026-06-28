// std.sync.ring SPSC — cross-thread LOCK-FREE single-producer / single-consumer.
// One producer thread enqueues, the main thread consumes; the cursors are
// Atomic(i64), so this is genuinely lock-free (no mutex). The POD path verifies
// exact count + sum; the Str path verifies that owned has_drop elements MOVE
// across the thread boundary intact (no torn handle, no double-free crash).
//
// NO --memcheck (tracker not thread-safe — same as task/atomic/sync). Soundness
// is checked by exact count/sum/checksum over repeated AOT runs.
//
// NB SPSC move-safety: enqueue(T x)->bool MOVES x in; on `false` (full) x is
// consumed. So `while !enqueue(s)` would be use-after-move for has_drop T (the
// compiler rejects it). With a SINGLE producer, free space only grows from the
// consumer, so "wait until not full, then enqueue ONCE" is safe and the enqueue
// always succeeds. POD int is copied (not moved), so its retry loop is fine.

import std.sync.ring
import std.sync.task
import std.core.str

Ring(int) g_ints = {}
Ring(Str) g_strs = {}

def main() {
    // ---------------- POD path: count + sum ----------------
    g_ints = new_ring(int)(1024)
    int N = 100000

    Task(int) prod = {}
    prod.run(|| {
        for i in 0..100000 {
            while !g_ints.enqueue(i) { }     // int is POD (copy) — retry is safe
        }
        return 0
    })

    i64 sum = 0
    int got = 0
    while got < N {
        match g_ints.dequeue() {
            Some(x) => { sum = sum + (x as i64); got = got + 1 }
            None    => { }
        }
    }
    prod.join()

    i64 expect = (N as i64) * ((N as i64) - 1) / 2     // sum of 0..N-1
    if got != N { @print("SPSC FAIL pod count"); return }
    if sum != expect { @print("SPSC FAIL pod sum"); @print(sum); return }
    @print("SPSC OK pod")

    // ------------- has_drop Str path: cross-thread move -------------
    g_strs = new_ring(Str)(256)
    int M = 20000

    Task(int) sp = {}
    sp.run(|| {
        for i in 0..20000 {
            Str s = f"item-{i}"
            while g_strs.is_full() { }        // single producer: wait for space
            bool ok = g_strs.enqueue(s)       // moves s in exactly once (succeeds)
        }
        return 0
    })

    i64 bytes = 0
    int sgot = 0
    while sgot < M {
        match g_strs.dequeue() {
            Some(s) => { bytes = bytes + (s.len() as i64); sgot = sgot + 1 }
            None    => { }
        }
    }
    sp.join()

    if sgot != M { @print("SPSC FAIL str count"); return }
    @print("SPSC OK str")
    @print(bytes)
}
