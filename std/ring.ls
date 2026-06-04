// std/ring.ls — rte_ring-style fixed-capacity ring buffer (single-threaded).
//
// Borrows DPDK rte_ring's *data-structure* design — power-of-2 capacity with
// `idx & mask` indexing and free-running producer/consumer cursors — but is
// single-threaded: LS has no atomics/threads yet. The public API and internal
// cursor layout are chosen so a future lock-free MP/MC version can slot in
// without breaking callers (see docs/plan_std_ring.md §2).
//
// Element ownership: slots are Option(T), not bare T. A fixed-length backing
// vec auto-drops ALL `size` slots on scope exit; Some(payload) drops once, None
// drops nothing. This makes the ring correct for has_drop T (e.g. Ring(string)).
//
// Cursors `prod`/`cons` are free-running (never reset); they map to rte_ring's
// prod_tail/cons_tail. count = prod - cons; full when count == cap; empty when
// count == 0. NOTE: dequeue deep-copies the slot out (vec has no move-out
// primitive yet) — for POD/pointer T that is a trivial memcpy.

struct Ring(T) {
    vec(Option(T)) buf
    int            cap     // usable capacity = size (power of 2)
    int            mask    // size - 1, for idx & mask
    int            prod    // write cursor (free-running) — rte_ring prod_tail
    int            cons    // read cursor  (free-running) — rte_ring cons_tail
}

// new_ring(T)(capacity) -> Ring(T)
// `capacity` is rounded UP to the next power of 2; the actual usable capacity is
// available via .capacity(). All slots start empty (None).
//
// NOTE: the power-of-2 rounding is inlined rather than calling a module-private
// helper. A generic function is monomorphized at the IMPORTER's call site and
// its body is re-checked in the importer's scope, where module-private helpers
// are not visible (see docs/plan_std_containers.md §0.0 deferred item). Generic
// bodies may only reference their params, builtins, and exported symbols.
fn new_ring(T)(int capacity) -> Ring(T) {
    int size = 1
    while size < capacity {
        size = size * 2
    }
    vec(Option(T)) b = []
    for (int i = 0; i < size; i = i + 1) {
        b.push(None)
    }
    return Ring(T) { buf: b, cap: size, mask: size - 1, prod: 0, cons: 0 }
}

impl(T) Ring(T) {
    // Number of elements currently buffered.
    fn len(&self) -> int {
        return self.prod - self.cons
    }

    // Usable capacity (rounded-up power of 2).
    fn capacity(&self) -> int {
        return self.cap
    }

    fn is_empty(&self) -> bool {
        return self.prod == self.cons
    }

    fn is_full(&self) -> bool {
        return self.prod - self.cons >= self.cap
    }

    // Slots still available for enqueue.
    fn free_space(&self) -> int {
        return self.cap - (self.prod - self.cons)
    }

    // Enqueue one element at the producer cursor. Returns false (and leaves the
    // ring unchanged) when full. On success, x is moved into the ring.
    fn enqueue(&!self, T x) -> bool {
        if self.prod - self.cons >= self.cap {
            return false
        }
        int i = self.prod & self.mask
        self.buf[i] = Some(x)
        self.prod = self.prod + 1
        return true
    }

    // Dequeue one element from the consumer cursor (FIFO). Returns None when
    // empty, else Some(payload). The vacated slot is reset to None.
    fn dequeue(&!self) -> Option(T) {
        if self.prod == self.cons {
            return None
        }
        int i = self.cons & self.mask
        Option(T) v = self.buf.get(i)   // clone the slot's Some(payload) out
        self.buf[i] = None              // drop the original, free the slot
        self.cons = self.cons + 1
        return v
    }

    // Dequeue up to n elements into a fresh vec (FIFO order). Returns fewer than
    // n if the ring drains first.
    fn dequeue_burst(&!self, int n) -> vec(T) {
        vec(T) out = []
        int k = 0
        while k < n && self.prod != self.cons {
            int i = self.cons & self.mask
            Option(T) v = self.buf.get(i)
            self.buf[i] = None
            self.cons = self.cons + 1
            match v {
                Some(x) => { out.push(x) }
                None    => { }
            }
            k = k + 1
        }
        return out
    }

    // Drop all buffered elements and reset to empty.
    fn clear(&!self) {
        for (int i = 0; i < self.cap; i = i + 1) {
            self.buf[i] = None
        }
        self.prod = 0
        self.cons = 0
    }
}
