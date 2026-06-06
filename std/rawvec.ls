// std/rawvec.ls — generic dynamic array over RAW malloc/realloc/free.
//
// Unlike std.stack (which is built on the builtin vec(T)), RawVec(T) manages its
// own heap buffer directly: it is the proof that a pure-LS container can own raw
// memory and be as memory-safe and behave identically to the builtin vec(T).
//
// It uses the four ownership primitives:
//   * sizeof(T)        — byte size of an element (compile-time, per monomorphization)
//   * realloc/free     — grow / release the raw buffer (tracked by memcheck)
//   * p[i]             — typed element access; READ deep-clones (matches vec[i]),
//                        WRITE is a raw store (no drop of the old slot)
//   * __drop_at(slot)  — recursive destructor on a slot (frees owned element data)
//   * __move(local)    — transfer ownership of a named local into the container
//   * fn __clone(&self) — user deep-copy hook, so RawVec(RawVec(T)) reads deep-clone
//                          the inner buffer just like vec(vec(T))
//
// Ownership contract (identical to vec(T)):
//   push   moves T into the buffer.
//   pop    yields Some(T) moved out (slot dropped, len shrinks), None when empty.
//   get    returns a deep CLONE of the element (the buffer keeps its own).
//   set    drops the old element, moves the new one in.
//   clear / scope-drop  drop every live element, then free the buffer.

struct RawVec(T) { *T data; int len; int cap }

// Empty, owned RawVec(T). The nil pointer is bound to a local *T first
// (a nil literal cannot be written directly into a struct-literal field).
fn new_rawvec(T)() -> RawVec(T) {
    *T p = nil
    return RawVec(T) { data: p, len: 0, cap: 0 }
}

impl(T) RawVec(T) {
    // Ensure capacity for at least `need` elements (geometric growth).
    fn reserve(&!self, int need) {
        if need <= self.cap { return }
        int n = self.cap
        if n < 4 { n = 4 }
        while n < need { n = n * 2 }
        self.data = realloc(self.data as *u8, n * sizeof(T)) as *T
        self.cap = n
    }

    // Move x onto the end of the buffer.
    fn push(&!self, T x) {
        self.reserve(self.len + 1)
        self.data[self.len] = x          // raw store into uninitialized slot
        self.len = self.len + 1
    }

    // Remove and return the last element (moved out), or None when empty.
    fn pop(&!self) -> Option(T) {
        if self.len == 0 { return None }
        self.len = self.len - 1
        T out = self.data[self.len]      // clone out
        __drop_at(self.data[self.len])   // drop the slot original
        return Some(out)
    }

    // Deep clone of the element at i (the buffer keeps its own copy). Reading
    // out of bounds returns a default-constructed clone of slot 0 is unsafe, so
    // callers must stay in [0,len); use length() to check (matches vec semantics
    // at the API boundary — the raw p[i] itself is unchecked).
    fn get(&self, int i) -> T {
        T tmp = self.data[i]
        return tmp
    }

    // Overwrite element i: drop the old, move the new in.
    fn set(&!self, int i, T x) {
        __drop_at(self.data[i])
        self.data[i] = x
    }

    fn length(&self) -> int { return self.len }
    fn capacity(&self) -> int { return self.cap }
    fn is_empty(&self) -> bool { return self.len == 0 }

    // Drop every element, keep the buffer (len -> 0).
    fn clear(&!self) {
        for (int i = 0; i < self.len; i = i + 1) { __drop_at(self.data[i]) }
        self.len = 0
    }

    // User deep-copy hook: lets RawVec(T) be cloned (e.g. read by value as a
    // nested element). emit_clone_value calls this instead of the field-wise
    // auto-clone (which cannot deep-copy the raw *T buffer).
    fn __clone(&self) -> RawVec(T) {
        // Construct inline rather than calling new_rawvec(T)(): a generic free
        // function called from inside a generic method body does not get its type
        // arg substituted at monomorphization (would emit `new_rawvec(T)` literally).
        *T p = nil
        RawVec(T) out = RawVec(T) { data: p, len: 0, cap: 0 }
        out.reserve(self.len)
        for (int i = 0; i < self.len; i = i + 1) {
            T e = self.data[i]           // clone-on-read each element
            out.push(e)
        }
        return out
    }

    // Drop every live element (recursively), then free the buffer.
    fn __drop() {
        for (int i = 0; i < self.len; i = i + 1) { __drop_at(self.data[i]) }
        if self.cap > 0 { free(self.data as *u8) }
    }
}
