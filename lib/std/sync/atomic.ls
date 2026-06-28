// std/atomic.ls — lock-free atomic scalars.
//
//   import std.sync.atomic
//   Atomic(int) a = {}        // value zero-initialised (0)
//   a.set(10)                 // atomic store
//   int old = a.fetch_add(1)  // atomic read-modify-write, returns prior value
//   if a.compare_set(11, 0) { ... }   // CAS: 11 -> 0, true if it swapped
//
// Atomic(T) wraps one lock-free scalar T (int/i64/u64/f64/bool/char/pointer,
// size <= 8). Every method lowers to a SINGLE inline LLVM atomic instruction
// (SequentiallyConsistent — a full memory barrier), so there is no lock and no
// call overhead. For anything larger than a scalar, use Mutex (std.sync.lock).
//
// Atomic is NOT has_drop (its field is POD): move it freely, make it a global,
// no destructor. To share across threads, put it in a global the workers name
// directly (closures capture by-move, but globals are shared by reference).
//
// Mechanism: the methods call compiler intrinsics __atomic_* on the `value`
// place; codegen emits the atomic instruction in place (docs/plan_atomic_mutex.md).

struct Atomic(T) { T value }

methods(T) Atomic(T) {
    // Atomic load (SeqCst). Read-only borrow — callable on a shared &Atomic.
    def get(&self) -> T { return __atomic_load(self.value) }

    // Atomic store (SeqCst).
    def set(&!self, T v) { __atomic_store(self.value, v) }

    // ---- relaxed-ordering variants (lock-free fast paths) ----
    // These weaken the memory order. SeqCst (get/set above) is the safe default;
    // reach for these only in hand-rolled lock-free code (e.g. the SPSC ring),
    // where on x64 an acquire load / release store is a plain mov but a SeqCst
    // store is a locked xchg. Pair them correctly: a release store on one thread
    // synchronizes-with an acquire load of the same location on another. relaxed
    // gives NO cross-thread ordering — use only for a value only this thread
    // writes (e.g. a producer reading its own cursor).

    // Acquire load: see all writes the matching release-store made visible.
    def load_acquire(&self) -> T { return __atomic_load_acquire(self.value) }

    // Relaxed load: atomic, but no ordering w.r.t. other locations.
    def load_relaxed(&self) -> T { return __atomic_load_relaxed(self.value) }

    // Release store: publish — prior writes become visible to an acquire reader.
    def store_release(&!self, T v) { __atomic_store_release(self.value, v) }

    // Relaxed store: atomic, no ordering. For a value only this thread writes.
    def store_relaxed(&!self, T v) { __atomic_store_relaxed(self.value, v) }

    // Atomic value += d; returns the PRIOR value.
    def fetch_add(&!self, T d) -> T { return __atomic_add(self.value, d) }

    // Atomic value -= d; returns the PRIOR value.
    def fetch_sub(&!self, T d) -> T { return __atomic_sub(self.value, d) }

    // Atomic exchange: store v, return the PRIOR value.
    def swap(&!self, T v) -> T { return __atomic_swap(self.value, v) }

    // Strong compare-and-swap: if value == expected, set it to desired and
    // return true; otherwise leave it and return false. The building block for
    // spinlocks and lock-free structures.
    def compare_set(&!self, T expected, T desired) -> bool {
        return __atomic_cas(self.value, expected, desired)
    }
}

// Standalone full memory barrier (SeqCst). Rarely needed — ordered atomic ops
// already carry their own fences; reach for this only in hand-rolled lock-free
// code that fences between plain loads/stores.
def fence() { __atomic_fence() }
