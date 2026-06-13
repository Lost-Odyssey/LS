// std/atomic.ls — lock-free atomic scalars.
//
//   import std.atomic
//   Atomic(int) a = {}        // value zero-initialised (0)
//   a.set(10)                 // atomic store
//   int old = a.fetch_add(1)  // atomic read-modify-write, returns prior value
//   if a.compare_set(11, 0) { ... }   // CAS: 11 -> 0, true if it swapped
//
// Atomic(T) wraps one lock-free scalar T (int/i64/u64/f64/bool/char/pointer,
// size <= 8). Every method lowers to a SINGLE inline LLVM atomic instruction
// (SequentiallyConsistent — a full memory barrier), so there is no lock and no
// call overhead. For anything larger than a scalar, use Mutex (std.sync).
//
// Atomic is NOT has_drop (its field is POD): move it freely, make it a global,
// no destructor. To share across threads, put it in a global the workers name
// directly (closures capture by-move, but globals are shared by reference).
//
// Mechanism: the methods call compiler intrinsics __atomic_* on the `value`
// place; codegen emits the atomic instruction in place (docs/plan_atomic_mutex.md).

struct Atomic(T) { T value }

impl(T) Atomic(T) {
    // Atomic load (SeqCst). Read-only borrow — callable on a shared &Atomic.
    fn get(&self) -> T { return __atomic_load(self.value) }

    // Atomic store (SeqCst).
    fn set(&!self, T v) { __atomic_store(self.value, v) }

    // Atomic value += d; returns the PRIOR value.
    fn fetch_add(&!self, T d) -> T { return __atomic_add(self.value, d) }

    // Atomic value -= d; returns the PRIOR value.
    fn fetch_sub(&!self, T d) -> T { return __atomic_sub(self.value, d) }

    // Atomic exchange: store v, return the PRIOR value.
    fn swap(&!self, T v) -> T { return __atomic_swap(self.value, v) }

    // Strong compare-and-swap: if value == expected, set it to desired and
    // return true; otherwise leave it and return false. The building block for
    // spinlocks and lock-free structures.
    fn compare_set(&!self, T expected, T desired) -> bool {
        return __atomic_cas(self.value, expected, desired)
    }
}

// Standalone full memory barrier (SeqCst). Rarely needed — ordered atomic ops
// already carry their own fences; reach for this only in hand-rolled lock-free
// code that fences between plain loads/stores.
fn fence() { __atomic_fence() }
