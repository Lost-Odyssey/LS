// std/sync.ls — bare locks: Mutex and SpinLock.
//
//   import std.sync.lock
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
// Use Atomic(T) (std.sync.atomic) for a single shared word (counter/flag) — a lock is
// for multi-step operations on compound data (Vec/Map/cross-field invariants)
// that hardware atomics can't make indivisible.
//
// Share across worker threads by putting the lock (and the data) in GLOBALS the
// closures name directly — globals are shared by reference, not captured. See
// docs/plan_atomic_mutex.md.
//
// The `!` on lock!/unlock!/trylock! marks them unsafe-by-contract: YOU guarantee
// balanced pairing and synchronised access (mirrors get!/set!).

import std.sync.atomic

// ============================== Mutex ==============================
// The OS lock lives behind an opaque heap handle so LS's move semantics never
// relocate it. has_drop: __drop destroys the OS lock.

struct Mutex { object lock }

methods Mutex {
    // Create the OS lock. Call once after construction (`Mutex m = {}`).
    def init(&!self) { self.lock = __mutex_init() }

    def lock!(&!self) { __mutex_lock(self.lock) }
    def unlock!(&!self) { __mutex_unlock(self.lock) }
    def trylock!(&!self) -> bool { return __mutex_trylock(self.lock) != 0 }

}

methods Mutex: Destroy {
    // Destroy the OS lock. NOT unlock — dropping a still-held Mutex is a logic
    // bug (structured code holds no lock at drop time).
    def ~(&!self) { __mutex_destroy(self.lock) }
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

methods SpinLock {
    // Acquire: CAS 0->1. Spin with a pause hint; after SPIN_BEFORE_YIELD failed
    // tries, yield the core each iteration instead of burning it.
    def lock!(&!self) {
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
    def unlock!(&!self) { self.flag.set(0) }
    def trylock!(&!self) -> bool { return self.flag.compare_set(0, 1) }
}

// =============================== Guard ===============================
// Guard(T) — a DATA GUARD: the value T is private, reachable only through a
// closure that runs while the lock is held. Unlike the bare Mutex above, the
// compiler enforces "no access without the lock" — forgetting to lock is a
// compile error, not a silent data race.
//
//   Guard(Vec(int)) g_data = {}   // value private, lock handle null
//   g_data.init()                 // create the OS lock once, before sharing
//   g_data.lock(|v| { v.push(1) })          // exclusive: v is &!Vec(int)
//   int n = g_data.get(int)(|v| { return v.len() })   // lock + read out a value
//
// Why this is sound without a lifetime system: `lock` lends &!self.value into
// the closure as a PARAMETER (`v`). LS borrows are arg-only and non-escaping —
// `v` cannot be returned, stored, put in a field, or captured out — so the
// borrow provably lives only inside the closure body = inside the critical
// section. Private `value` means the only way to reach the data is through that
// closure. Together: you cannot touch the data without holding the lock.
//
// Share across threads via a GLOBAL Guard the worker closures name directly
// (globals are shared by reference, not captured); call init() in main before
// spawning. For fine-grained / custom locking, drop to the bare Mutex/SpinLock.
//
//   v1 scope: exclusive access only (no shared-read / RwLock); construction is
//   `{}` zero-init + init() (initial non-default value set via the first lock);
//   the lock is the OS Mutex (blocking). Early unlock / guard-style RAII await
//   a lifetime system (docs/plan_concurrency.md §4).

struct Guard(T) {
    private T value
    private object handle
}

methods Guard(T) {
    // Create the OS lock. Call once after construction, before sharing.
    def init(&!self) { self.handle = __mutex_init() }

    // Exclusive access: run f while holding the lock, lending it &!value.
    def lock(&!self, Block(&!T) f) {
        __mutex_lock(self.handle)
        f(&!self.value)
        __mutex_unlock(self.handle)
    }

    // Exclusive access returning a value out of the critical section. R is an
    // explicit method-level type arg: g.get(int)(|v| { return v.len() }).
    def get(R)(&!self, Block(&!T)->R f) -> R {
        __mutex_lock(self.handle)
        R out = f(&!self.value)
        __mutex_unlock(self.handle)
        return out
    }

}

methods Guard(T): Destroy {
    // Destroy the OS lock. The protected value's own drop runs automatically.
    def ~(&!self) { __mutex_destroy(self.handle) }
}

// ============================== RwLock ==============================
// RwLock(T) — a Guard that distinguishes readers from writers: MANY readers in
// parallel OR one exclusive writer. Use it for read-mostly shared data (config,
// cache, lookup tables) where a plain Guard would needlessly serialise readers.
//
//   RwLock(Map(Str,int)) g_cfg = {}; g_cfg.init()
//   int v = g_cfg.read(int)(|m| { return m["timeout"] })   // readers run together
//   g_cfg.write(|m| { m.set("timeout", 30) })              // writer is exclusive
//
//   * read(R)  — acquires the SHARED lock; the closure gets a read-only &T, so
//                concurrent readers cannot mutate (the checker rejects it). R is
//                an explicit method-level type arg, like Guard.get.
//   * write    — acquires the EXCLUSIVE lock; the closure gets &!T.
//
// Soundness is identical to Guard (borrow arg-only/non-escaping + private value).
// read() takes &!self only as ABI for forming &!self.value, which it then hands
// to the closure WEAKENED to &T (read-only) — there is no shared-read field-
// borrow primitive yet, so this is how a reader gets a zero-copy &T. The OS lock
// (SRWLOCK / pthread_rwlock) does the real reader/writer arbitration.
//
// Cost: rwlock ops are heavier than a plain mutex; this wins only when reads
// dominate and critical sections aren't trivially short. Writer starvation is
// handled by the OS primitive. Share across threads via a GLOBAL, same as Guard.
// Works for struct (Vec/Map/Str/...) AND POD payloads. (For a shared scalar a
// lock-free Atomic is usually the better tool than RwLock(int).)

struct RwLock(T) {
    private T value
    private object handle
}

methods RwLock(T) {
    def init(&!self) { self.handle = __rwlock_init() }

    // Shared read: many readers run in parallel. The closure gets a read-only
    // &T (it cannot mutate — enforced at compile time).
    def read(R)(&!self, Block(&T)->R f) -> R {
        __rwlock_rdlock(self.handle)
        R out = f(&!self.value)        // &!T weakens to &T at the &T param
        __rwlock_rdunlock(self.handle)
        return out
    }

    // Exclusive write: one writer, blocks all readers and other writers.
    def write(&!self, Block(&!T) f) {
        __rwlock_wrlock(self.handle)
        f(&!self.value)
        __rwlock_wrunlock(self.handle)
    }

}

methods RwLock(T): Destroy {
    def ~(&!self) { __rwlock_destroy(self.handle) }
}

// ============================= SpinGuard =============================
// SpinGuard(T) — a Guard backed by the bare adaptive SpinLock instead of the OS
// mutex: under contention it busy-waits (pause → yield) rather than blocking.
// For VERY SHORT critical sections where the OS-mutex syscall would dominate.
// Same data-guard guarantee as Guard; the only difference is the underlying lock.
//
//   SpinGuard(int) ctr = {}                 // no init() — starts unlocked
//   ctr.lock(|n| { ... })
//
// No init() (the embedded SpinLock's flag starts 0 = free via {} zero-init) and
// no __drop (no OS handle; the value auto-drops). Reuses SpinLock wholesale —
// see its adaptive two-stage backoff (pause then yield) above.

struct SpinGuard(T) {
    private T value
    private SpinLock slock
}

methods SpinGuard(T) {
    def lock(&!self, Block(&!T) f) {
        self.slock.lock!()
        f(&!self.value)
        self.slock.unlock!()
    }

    def get(R)(&!self, Block(&!T)->R f) -> R {
        self.slock.lock!()
        R out = f(&!self.value)
        self.slock.unlock!()
        return out
    }
}
