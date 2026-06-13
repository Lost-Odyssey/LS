// std/sync.ls — bare locks: Mutex and SpinLock.
//
//   import std.sync
//   Mutex m = {}            // construct, then create the OS lock once:
//   m.init()
//   Vec(int) data = []      // the data you protect — paired by YOU
//   m.lock!()
//   data.push(1)            // exclusive access while held
//   m.unlock!()
//
// These are PLAIN locks, not data guards. LS has no lifetime system and structs
// have no private fields, so a Rust-style Mutex<T> couldn't actually enforce
// "no access without the lock" — it would be encapsulation theatre. So the lock
// is just a lock: YOU pair it with the data it protects (a separate variable, or
// bundle them in your own struct) and YOU hold it on every access. A lock only
// prevents a race if EVERY access to the shared data is made while holding the
// SAME lock — it is a cooperative protocol.
//
//   Mutex    — OS mutex; under contention it BLOCKS (yields the core).
//   SpinLock — busy-waits with a CPU pause hint; keeps the cache warm, never
//              yields. For very short critical sections under contention.
//
// Use Atomic(T) (std.atomic) for a single shared word (counter/flag) — a lock is
// for multi-step operations on compound data (Vec/Map/cross-field invariants)
// that hardware atomics can't make indivisible.
//
// Share across worker threads by putting the lock (and the data) in GLOBALS the
// closures name directly — globals are shared by reference, not captured. See
// docs/plan_atomic_mutex.md.
//
// The `!` on lock!/unlock!/trylock! marks them unsafe-by-contract: YOU guarantee
// balanced pairing and synchronised access (mirrors get!/set!).

import std.atomic

// ============================== Mutex ==============================
// The OS lock lives behind an opaque heap handle so LS's move semantics never
// relocate it. has_drop: __drop destroys the OS lock.

struct Mutex { object lock }

impl Mutex {
    // Create the OS lock. Call once after construction (`Mutex m = {}`).
    fn init(&!self) { self.lock = __mutex_init() }

    fn lock!(&!self) { __mutex_lock(self.lock) }
    fn unlock!(&!self) { __mutex_unlock(self.lock) }
    fn trylock!(&!self) -> bool { return __mutex_trylock(self.lock) != 0 }

    // Destroy the OS lock. NOT unlock — dropping a still-held Mutex is a logic
    // bug (structured code holds no lock at drop time).
    fn __drop(&!self) { __mutex_destroy(self.lock) }
}

// ============================ SpinLock ============================
// Just an Atomic(int) flag (0=free, 1=held). No OS handle, no custom drop. No
// init needed (`SpinLock s = {}` starts free).
//
// Adaptive two-stage backoff:
//   * brief contention  -> CPU pause (__cpu_relax): keep the core, keep the
//     cache warm — the fast path a spinlock is for.
//   * prolonged spinning -> yield the core (__cpu_yield = sched_yield /
//     SwitchToThread) once a try count crosses SPIN_BEFORE_YIELD. This bounds
//     CPU burn under heavy contention AND breaks priority inversion: a low-
//     priority lock holder can be scheduled to release the lock instead of being
//     starved forever by a high-priority spinner (a pure spinlock can deadlock
//     there). The yield targets lower-priority threads too (Sleep(0) wouldn't).

struct SpinLock { Atomic(int) flag }

impl SpinLock {
    // Acquire: CAS 0->1. Spin with a pause hint; after SPIN_BEFORE_YIELD failed
    // tries, yield the core each iteration instead of burning it.
    fn lock!(&!self) {
        int SPIN_BEFORE_YIELD = 100
        int tries = 0
        while !self.flag.compare_set(0, 1) {
            tries = tries + 1
            if tries < SPIN_BEFORE_YIELD {
                __cpu_relax()
            } else {
                __cpu_yield()
            }
        }
    }
    fn unlock!(&!self) { self.flag.set(0) }
    fn trylock!(&!self) -> bool { return self.flag.compare_set(0, 1) }
}
