// std/ring.ls — rte_ring-style bounded ring buffer (lock-free SPSC and MPMC).
//
// DPDK rte_ring's data-structure design: power-of-2 capacity, `idx & mask`
// indexing, free-running producer/consumer cursors that are Atomic(i64). Two
// modes, chosen at construction (rte_ring's SP/SC vs MP/MC flags):
//
//   new_ring(T)(cap)        SPSC — a SINGLE producer and a SINGLE consumer (on
//                           different threads) use it lock-free with NO CAS.
//                           Single-threaded use is the degenerate case (one
//                           thread does both) and is the cheapest path.
//   new_mpmc_ring(T)(cap)   MPMC — MANY producers and MANY consumers, lock-free
//                           via CAS slot reservation + a publish-order wait.
//
// MPMC splits each cursor into head (reserve) + tail (publish). A producer CASes
// prod_head to claim a slot, writes it, then waits until prod_tail reaches its
// reserved index before advancing prod_tail (so consumers, which read prod_tail,
// never observe a slot out of order). Symmetric for consumers (cons_head/tail).
//
// Element ownership: slots are raw T in a *T buffer. The cursor protocol grants
// each slot to exactly one side at a time. enqueue MOVES T into the slot (owned-
// param ABI, like Vec.push — a NAMED var is cloned, an rvalue is moved); dequeue
// MOVES it out (__take, no clone). __drop drops any [cons_tail, prod_tail)
// residual, then frees buf.
//
// Caveat (MPMC under oversubscription): the publish-order wait spins on a peer
// (cpu_relax). With more threads than cores + preemption, a preempted producer
// can briefly stall its peers (obstruction-free, not strictly lock-free) — keep
// threads <= cores. Memory order is SeqCst (stronger than the acq/rel rte_ring
// needs: correct, not max throughput; acq/rel is a later opt, plan §4.6).

import std.sys.c
import std.core.vec
import std.sync.atomic

struct Ring(T) {
    *T          data
    int         cap        // usable capacity = size (power of 2)
    int         mask       // size - 1
    bool        mp         // multi-producer (CAS reserve) vs single
    bool        mc         // multi-consumer
    Atomic(i64) prod_head  // producer reserve cursor (MP)
    Atomic(i64) prod_tail  // producer publish cursor
    Atomic(i64) cons_head  // consumer reserve cursor (MC)
    Atomic(i64) cons_tail  // consumer publish cursor
}

// new_ring(T)(capacity) -> Ring(T): SPSC. `capacity` rounds UP to a power of 2.
def new_ring(T)(int capacity) -> Ring(T) {
    Ring(T) r = {}
    int size = 1
    while size < capacity {
        size = size * 2
    }
    r.data = std.sys.c.malloc(size * sizeof(T)) as *T
    r.cap = size
    r.mask = size - 1
    r.mp = false
    r.mc = false
    return r                 // cursors zero-init (value 0) via {}
}

// new_mpmc_ring(T)(capacity) -> Ring(T): MPMC (many producers / consumers).
def new_mpmc_ring(T)(int capacity) -> Ring(T) {
    Ring(T) r = {}
    int size = 1
    while size < capacity {
        size = size * 2
    }
    r.data = std.sys.c.malloc(size * sizeof(T)) as *T
    r.cap = size
    r.mask = size - 1
    r.mp = true
    r.mc = true
    return r
}

methods Ring(T) {
    // Buffered count (approximate under concurrency).
    def len(&self) -> int {
        return (self.prod_tail.get() - self.cons_tail.get()) as int
    }

    def capacity(&self) -> int {
        return self.cap
    }

    def is_empty(&self) -> bool {
        return self.prod_tail.get() == self.cons_tail.get()
    }

    def is_full(&self) -> bool {
        return self.prod_tail.get() - self.cons_tail.get() >= (self.cap as i64)
    }

    def free_space(&self) -> int {
        return self.cap - ((self.prod_tail.get() - self.cons_tail.get()) as int)
    }

    // Enqueue one element. Returns false (ring unchanged) when full; on success
    // x is MOVED into the ring. SP path (no CAS) when mp==false, else MP path.
    //
    // x is moved at exactly ONE site (`self.data[slot] = x`) so the move checker
    // sees a single conditional move (the early `return false` paths don't touch
    // x). Splitting the move across the mp/sp branches would read as use-of-
    // maybe-moved because the analyzer doesn't treat a branch's `return` as
    // diverging before the other branch's move.
    def enqueue(&!self, T x) -> bool {
        i64 slot = 0
        if self.mp {
            // ---- MP: CAS-reserve a slot ----
            i64 oh = 0
            while true {
                oh = self.prod_head.get()
                i64 ct = self.cons_tail.get()
                if oh - ct >= (self.cap as i64) {
                    return false
                }
                if self.prod_head.compare_set(oh, oh + 1) {
                    break
                }
                __cpu_relax()
            }
            slot = oh
        } else {
            // ---- SP: single producer, no reservation ----
            // own cursor: relaxed (only this thread writes prod_tail).
            // peer cursor: acquire (sync-with the consumer's release of cons_tail).
            i64 p = self.prod_tail.load_relaxed()
            i64 c = self.cons_tail.load_acquire()
            if p - c >= (self.cap as i64) {
                return false
            }
            slot = p
        }
        self.data[(slot as int) & self.mask] = x         // single move site
        if self.mp {
            while self.prod_tail.get() != slot {         // wait for predecessors
                __cpu_relax()
            }
        }
        // release publish: the slot write above happens-before the consumer's
        // acquire load of prod_tail observing this new value (correct for SP & MP).
        self.prod_tail.store_release(slot + 1)
        return true
    }

    // Dequeue one element (FIFO). Returns None when empty, else Some(payload)
    // MOVED out. SC path (no CAS) when mc==false, else MC path.
    def dequeue(&!self) -> Option(T) {
        if self.mc {
            // ---- MC: CAS-reserve a slot, read, publish in order ----
            i64 oh = 0
            i64 nh = 0
            while true {
                oh = self.cons_head.get()
                i64 pt = self.prod_tail.get()
                if oh >= pt {
                    return None
                }
                nh = oh + 1
                if self.cons_head.compare_set(oh, nh) {
                    break
                }
                __cpu_relax()
            }
            T out = __take(self.data[(oh as int) & self.mask])   // move out
            while self.cons_tail.get() != oh {                   // wait for predecessors
                __cpu_relax()
            }
            self.cons_tail.set(nh)                               // publish
            return Some(out)
        }
        // ---- SC: single consumer ----
        // own cursor: relaxed (only this thread writes cons_tail).
        // peer cursor: acquire (sync-with the producer's release of prod_tail, so
        // the slot write is visible before we read it).
        i64 c = self.cons_tail.load_relaxed()
        i64 p = self.prod_tail.load_acquire()
        if c >= p {
            return None
        }
        T out = __take(self.data[(c as int) & self.mask])
        self.cons_tail.store_release(c + 1)              // release publish: frees the slot
        return Some(out)
    }

    // Dequeue up to n elements into a fresh vec (FIFO). Fewer than n if drained.
    def dequeue_burst(&!self, int n) -> Vec(T) {
        Vec(T) out = {}
        int k = 0
        while k < n {
            match self.dequeue() {
                Some(x) => { out.push(x) }
                None    => { return out }
            }
            k = k + 1
        }
        return out
    }

    // Drop all buffered elements and reset to empty. SINGLE-THREADED only.
    def clear(&!self) {
        i64 c = self.cons_tail.get()
        i64 p = self.prod_tail.get()
        while c < p {
            __drop_at(self.data[(c as int) & self.mask])
            c = c + 1
        }
        self.prod_head.set(0)
        self.prod_tail.set(0)
        self.cons_head.set(0)
        self.cons_tail.set(0)
    }

}

methods Ring(T): Destroy {
    // Drop any [cons_tail, prod_tail) residual, then free buf.
    def ~(&!self) {
        i64 c = self.cons_tail.get()
        i64 p = self.prod_tail.get()
        while c < p {
            __drop_at(self.data[(c as int) & self.mask])
            c = c + 1
        }
        if self.cap > 0 {
            std.sys.c.free(self.data as *u8)
        }
    }
}
