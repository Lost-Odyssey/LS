// std/deque.ls — pure-LS double-ended queue (growable ring buffer).
//
// A contiguous *T buffer used circularly: `head` is the physical index of the
// front element, `len` the count; logical index i lives at physical
// (head + i) & (cap - 1). cap is always a power of two (8, then doubling), so the
// wrap is a cheap mask and a push_front of head-1 wraps correctly under two's
// complement. Both ends are O(1) (amortized for growth); random access by logical
// index is O(1). The efficient deque/queue — Vec only pushes/pops one end.
//
// Manages its own raw buffer (like std.core.map), NOT a Vec(T): a ring can't be a
// linear Vec without O(n) front shifts. A raw `*T` field is not has_drop, so the
// compiler does not auto-derive drop/clone — Destroy and Clone are written
// explicitly (mirroring Map).
//
// Ownership: push moves T in; pop moves T OUT (no clone) via __take; front/back/
// get/iter CLONE. Growth and the destructor move/drop only the LIVE elements
// (head .. head+len), so has_drop T (Str/Vec/Map/struct) is never double-freed.

import std.core.vec

struct Deque(T) { *T data; int cap; int head; int len }

// Borrowing iterator over a Deque(T) in logical (front→back) order. Holds RAW
// pointers into the deque's buffer (does NOT own them), so it must not outlive the
// deque, nor may the deque be mutated during iteration. Drives `for x in d`.
struct DequeIter(T) { *T data; int cap; int head; int len; int i }

// Construct empty with `Deque(T) d = {}` (zero-init: data=nil, cap=head=len=0 —
// a valid empty ring). `{}` is the default constructor — dual of the `~` destructor.

methods(T) Deque(T) {
    // ---- queries ----

    def len(&self) -> int { return self.len }
    def empty?(&self) -> bool { return self.len == 0 }

    // Clone of the front / back element, or None if empty.
    def front(&self) -> Option(T) {
        if self.len == 0 { return None }
        T v = self.data[self.head]
        return Some(v)
    }
    def back(&self) -> Option(T) {
        if self.len == 0 { return None }
        int idx = (self.head + self.len - 1) & (self.cap - 1)
        T v = self.data[idx]
        return Some(v)
    }

    // Clone of the element at logical index i (0 = front), or None if out of range.
    def get(&self, int i) -> Option(T) {
        if i < 0 || i >= self.len { return None }
        int idx = (self.head + i) & (self.cap - 1)
        T v = self.data[idx]
        return Some(v)
    }

    // ---- capacity ----

    // Double the buffer (8 first), relocating the live elements into a compacted
    // [0, len) layout (head resets to 0). Elements are MOVED (__take, no clone).
    def _grow(&!self) {
        int newcap = 8
        if self.cap > 0 { newcap = self.cap * 2 }
        *u8 z = nil
        *T nd = std.sys.c.realloc(z, newcap * sizeof(T)) as *T
        for (int i = 0; i < self.len; i = i + 1) {
            int src = (self.head + i) & (self.cap - 1)
            nd[i] = __take(self.data[src])
        }
        if self.cap > 0 { std.sys.c.free(self.data as *u8) }
        self.data = nd
        self.cap = newcap
        self.head = 0
    }

    // ---- ends ----

    // Append x at the back (moves in). O(1) amortized.
    def push_back(&!self, T x) {
        if self.len == self.cap { self._grow() }
        int idx = (self.head + self.len) & (self.cap - 1)
        self.data[idx] = x
        self.len = self.len + 1
    }

    // Prepend x at the front (moves in). O(1) amortized.
    def push_front(&!self, T x) {
        if self.len == self.cap { self._grow() }
        self.head = (self.head - 1) & (self.cap - 1)
        self.data[self.head] = x
        self.len = self.len + 1
    }

    // Remove & return the front element (moved out — no clone), or None if empty.
    def pop_front(&!self) -> Option(T) {
        if self.len == 0 { return None }
        T out = __take(self.data[self.head])
        self.head = (self.head + 1) & (self.cap - 1)
        self.len = self.len - 1
        return Some(out)
    }

    // Remove & return the back element (moved out — no clone), or None if empty.
    def pop_back(&!self) -> Option(T) {
        if self.len == 0 { return None }
        int idx = (self.head + self.len - 1) & (self.cap - 1)
        T out = __take(self.data[idx])
        self.len = self.len - 1
        return Some(out)
    }

    // ---- bulk ----

    // Drop every element, keep the buffer (cheap reuse).
    def clear(&!self) {
        for (int i = 0; i < self.len; i = i + 1) {
            int idx = (self.head + i) & (self.cap - 1)
            __drop_at(self.data[idx])
        }
        self.len = 0
        self.head = 0
    }

    // Deque-literal `Deque(T) d = [a, b, c]` opt-in (reserved-method protocol):
    // each element is pushed at the back, so the literal order is front→back.
    def __from_list(&!self, T x) { self.push_back(x) }

    // ---- iteration / export ----

    def iter(&self) -> DequeIter(T) {
        return DequeIter(T){ data: self.data, cap: self.cap,
                             head: self.head, len: self.len, i: 0 }
    }

    // Collect clones of all elements into a Vec(T), front→back order.
    def to_vec(&self) -> Vec(T) {
        Vec(T) out = {}
        out.reserve(self.len)
        for (int i = 0; i < self.len; i = i + 1) {
            int idx = (self.head + i) & (self.cap - 1)
            T v = self.data[idx]
            out.push(v)
        }
        return out
    }
}

methods(T) DequeIter(T) {
    def next(&!self) -> Option(T) {
        if self.i >= self.len { return None }
        int idx = (self.head + self.i) & (self.cap - 1)
        self.i = self.i + 1
        T v = self.data[idx]
        return Some(v)
    }
}

methods(T) Deque(T): Clone {
    // Deep copy into a compacted [0, len) layout (head = 0). Each live element is
    // cloned; empty slots are never read.
    def clone(&self) -> Deque(T) {
        Deque(T) out = {}
        if self.cap == 0 { return out }
        *u8 z = nil
        out.data = std.sys.c.realloc(z, self.cap * sizeof(T)) as *T
        out.cap = self.cap
        out.head = 0
        out.len = self.len
        for (int i = 0; i < self.len; i = i + 1) {
            int idx = (self.head + i) & (self.cap - 1)
            T v = self.data[idx]
            out.data[i] = v
        }
        return out
    }
}

methods(T) Deque(T): Destroy {
    // Drop every live element, then free the buffer.
    def ~(&!self) {
        for (int i = 0; i < self.len; i = i + 1) {
            int idx = (self.head + i) & (self.cap - 1)
            __drop_at(self.data[idx])
        }
        if self.cap > 0 { std.sys.c.free(self.data as *u8) }
    }
}
