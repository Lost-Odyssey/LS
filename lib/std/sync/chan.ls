// std/chan.ls — blocking bounded channel (the ergonomic default).
//
//   import std.sync.chan
//   Chan(int) ch = channel(int)(8)
//   ch.send(42)                  // blocks if full
//   Option(int) v = ch.recv()    // blocks if empty; None once closed & drained
//   ch.close()
//   for x in ch { ... }          // drains until closed   (std.sync.chan Phase 4)
//
// v1 is a mutex + two condition variables over a bounded ring (a classic bounded
// buffer) — NOT the lock-free std.sync.ring. Two independently-correct pieces beat a
// lock-free-ring-plus-eventcount that has to dodge lost wakeups
// (docs/plan_channel.md §7). A lock-free fast path is a later opt (Phase 6).
// Because every operation is under the mutex, Chan is MPMC-safe by construction
// (many producers AND many consumers).
//
// Ownership: send MOVES T into the ring (owned-param ABI for an rvalue, clone
// for a named var — same as Vec.push); recv MOVES it out (__take, no clone).
// __drop drops any unconsumed residual, then frees the buffer and destroys the
// lock + condvars.
//
// Cross-thread: put the Chan in a GLOBAL the worker closures name directly
// (globals are shared by reference, not captured); construct it in main before
// spawning. NO --memcheck on threaded use (tracker not thread-safe — like task).
//
// The mutex/condvar handles live behind opaque object handles (so LS's move
// semantics never relocate the OS objects), reached via the global intrinsics
// __mutex_*/__cond_* (they survive generic-method instantiation without an
// import alias — same as std.sync.lock / std.sync.task).

import std.sys.c

struct Chan(T) {
    private *T     data
    private int    cap        // power-of-2 capacity
    private int    mask       // cap - 1
    private int    head       // read cursor   (all four guarded by mtx)
    private int    tail       // write cursor
    private int    count      // live element count
    private bool   closed
    private object mtx
    private object not_full
    private object not_empty
}

// Iterator over a channel: `for x in ch` drains it (blocking on empty) until the
// channel is closed AND empty. Holds a RAW pointer back to the channel (not a
// copy); next() forwards to recv(). Declared before Chan's impl so iter() can
// return it. NB the pointer must not outlive the channel — fine for for-in,
// where the channel (the loop subject) outlives the loop.
struct ChanIter(T) { *Chan(T) ch }

// channel(T)(capacity) -> Chan(T). `capacity` rounds UP to the next power of 2.
// Generic body: only params / builtins / exported symbols + std.sys.c canonical path
// (see std/ring.ls note) — so the rounding is inlined and init does the rest.
def channel(T)(int capacity) -> Chan(T) {
    Chan(T) c = {}
    c.init(capacity)
    return c
}

methods(T) Chan(T) {
    // Allocate the buffer and create the OS lock + condvars. Call once after
    // construction (channel() does this for you).
    def init(&!self, int capacity) {
        int n = 1
        while n < capacity {
            n = n * 2
        }
        self.data = std.sys.c.malloc(n * sizeof(T)) as *T
        self.cap = n
        self.mask = n - 1
        self.mtx = __mutex_init()
        self.not_full = __cond_init()
        self.not_empty = __cond_init()
    }

    // Send one element, BLOCKING while the channel is full. Returns false iff the
    // channel is closed (the value is then dropped — sending on a closed channel
    // is a logic error). On success the element is moved into the ring.
    def send(&!self, T v) -> bool {
        __mutex_lock(self.mtx)
        while self.count == self.cap && !self.closed {
            __cond_wait(self.not_full, self.mtx)
        }
        if self.closed {
            __mutex_unlock(self.mtx)
            return false
        }
        self.data[self.tail] = v
        self.tail = (self.tail + 1) & self.mask
        self.count = self.count + 1
        __cond_signal(self.not_empty)
        __mutex_unlock(self.mtx)
        return true
    }

    // Receive one element, BLOCKING while the channel is empty. Returns None once
    // the channel is closed AND drained; otherwise Some(payload) moved out.
    def recv(&!self) -> Option(T) {
        __mutex_lock(self.mtx)
        while self.count == 0 && !self.closed {
            __cond_wait(self.not_empty, self.mtx)
        }
        if self.count == 0 {
            __mutex_unlock(self.mtx)
            return None
        }
        T out = __take(self.data[self.head])
        self.head = (self.head + 1) & self.mask
        self.count = self.count - 1
        __cond_signal(self.not_full)
        __mutex_unlock(self.mtx)
        return Some(out)
    }

    // Non-blocking send. None = sent; Some(v) = full or closed (v handed back so
    // the caller keeps ownership).
    def try_send(&!self, T v) -> Option(T) {
        __mutex_lock(self.mtx)
        if self.closed || self.count == self.cap {
            __mutex_unlock(self.mtx)
            return Some(v)
        }
        self.data[self.tail] = v
        self.tail = (self.tail + 1) & self.mask
        self.count = self.count + 1
        __cond_signal(self.not_empty)
        __mutex_unlock(self.mtx)
        return None
    }

    // Non-blocking receive. None = empty (or closed & drained); else Some(out).
    def try_recv(&!self) -> Option(T) {
        __mutex_lock(self.mtx)
        if self.count == 0 {
            __mutex_unlock(self.mtx)
            return None
        }
        T out = __take(self.data[self.head])
        self.head = (self.head + 1) & self.mask
        self.count = self.count - 1
        __cond_signal(self.not_full)
        __mutex_unlock(self.mtx)
        return Some(out)
    }

    // Buffered element count (approximate under concurrency).
    def len(&self) -> int { return self.count }

    def capacity(&self) -> int { return self.cap }

    def is_closed(&self) -> bool { return self.closed }

    // Close the channel: blocked receivers wake and drain to None; blocked
    // senders wake and return false. Idempotent.
    def close(&!self) {
        __mutex_lock(self.mtx)
        self.closed = true
        __cond_broadcast(self.not_empty)
        __cond_broadcast(self.not_full)
        __mutex_unlock(self.mtx)
    }

    // Iterator(T) protocol: `for x in ch` desugars to iter()/next(). Returns a
    // ChanIter holding a raw pointer to this channel (&self yields *Chan to the
    // actual receiver), so next() drains the same channel.
    def iter(&!self) -> ChanIter(T) {
        return ChanIter(T){ ch: &self }
    }

}

methods(T) Chan(T): Destroy {
    // Drop any unconsumed residual, then free the buffer + destroy lock/condvars.
    def ~(&!self) {
        int h = self.head
        for (int i = 0; i < self.count; i = i + 1) {
            __drop_at(self.data[h])
            h = (h + 1) & self.mask
        }
        if self.cap > 0 {
            std.sys.c.free(self.data as *u8)
        }
        __cond_destroy(self.not_full)
        __cond_destroy(self.not_empty)
        __mutex_destroy(self.mtx)
    }
}

methods(T) ChanIter(T) {
    // Iterator(T) protocol: blocking recv until the channel is closed & drained.
    // (*self.ch) derefs the raw pointer so recv() runs on the real channel.
    def next(&!self) -> Option(T) {
        return (*self.ch).recv()
    }
}
