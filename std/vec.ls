// std/vec.ls — generic dynamic array over RAW malloc/realloc/free.
//
// A pure-LS container that manages its OWN heap buffer. Goal: be
// memory-identical to the old builtin dynamic array and preserve its behavior.
// Construction matches Vec:
//
//     Vec(string) v = {}            // empty
//     Vec(int)    v = [1, 2, 3]     // literal
//
// Ownership primitives used:
//   * sizeof(T)         — element byte size (compile-time, per monomorphization)
//   * realloc / free    — grow / release the raw buffer (memcheck-tracked)
//   * p[i] (read)       — typed element read; DEEP-CLONES owned data (matches vec[i])
//   * p[i] = x (write)  — raw store (no drop of the old slot)
//   * __take(slot)      — move OUT of a slot (bit-read, no clone)
//   * __drop_at(slot)   — recursive destructor on a slot (frees owned data)
//   * __move(local)     — transfer ownership of a named local
//   * fn __clone(&self) — user deep-copy hook (Vec(Vec(T)) reads deep-clone)
//   * fn __from_list    — list-literal init opt-in (the `[..]` protocol)
//
// Ownership contract:
//   push/insert  move T into the buffer.   pop/remove  move T out (no clone).
//   get/first/last  return a deep CLONE.   set  drops old, moves new in.
//   clear / scope-drop  drop every live element, then free the buffer.
//
// Bounds checking:
//   * v[i] / get(i) / set(i,x)   bounds-checked; out-of-range aborts the process
//                                with a diagnostic (the safe default).
//   * get!(i) / set!(i,x)        UNCHECKED raw read/store (the `!` = unsafe escape
//                                hatch for hot paths; out-of-range is UB).

struct Vec(T) { *T data; int len; int cap }

impl(T) Vec(T) {
    // ---- capacity ----

    // Ensure capacity for at least `need` elements (geometric growth).
    fn reserve(&!self, int need) {
        if need <= self.cap { return }
        int n = self.cap
        if n < 4 { n = 4 }
        while n < need { n = n * 2 }
        self.data = std.c.realloc(self.data as *u8, n * sizeof(T)) as *T
        self.cap = n
    }

    // Release unused capacity (cap -> len).
    fn shrink_to_fit(&!self) {
        if self.cap == self.len { return }
        if self.len == 0 {
            if self.cap > 0 { std.c.free(self.data as *u8) }
            *T p = nil
            self.data = p
            self.cap = 0
            return
        }
        self.data = std.c.realloc(self.data as *u8, self.len * sizeof(T)) as *T
        self.cap = self.len
    }

    // ---- queries ----

    fn len(&self) -> int { return self.len }
    fn cap(&self) -> int { return self.cap }
    fn empty?(&self) -> bool { return self.len == 0 }
    fn as_ptr(&self) -> object { return self.data as object }

    // ---- iteration (Iterator(T) protocol — docs/plan_userdef_for_in.md) ----

    // Borrowing iterator over the elements. `&self` only reads v, so v stays
    // usable after the loop. VecIter holds a raw pointer into v's buffer (it does
    // NOT own it), so it must not outlive v. `for x in v` desugars to driving this
    // iterator's next() under the hood.
    fn iter(&self) -> VecIter(T) {
        return VecIter(T){ data: self.data, len: self.len, i: 0 }
    }

    // ---- add ----

    // Move x onto the end of the buffer.
    fn push(&!self, T x) {
        self.reserve(self.len + 1)
        self.data[self.len] = x          // raw store into uninitialized slot
        self.len = self.len + 1
    }

    // Insert x at index i, shifting [i, len) right by one. Valid i is [0, len]
    // (i == len appends); out-of-range aborts (matches get/set).
    fn insert(&!self, int i, T x) {
        if i < 0 || i > self.len {
            print(f"Vec.insert index out of bounds: len={self.len} index={i}")
            std.c.abort()
        }
        self.reserve(self.len + 1)
        int j = self.len
        while j > i {
            T e = __take(self.data[j - 1])   // move element right (no clone)
            self.data[j] = e
            j = j - 1
        }
        self.data[i] = x
        self.len = self.len + 1
    }

    // List-literal init opt-in (reserved-method protocol, like __drop/__clone):
    // having this method enables `Vec(T) v = [a, b, c]`.
    fn __from_list(&!self, T x) { self.push(x) }

    // ---- remove ----

    // Remove and return the last element (moved out), or None when empty.
    fn pop(&!self) -> Option(T) {
        if self.len == 0 { return None }
        self.len = self.len - 1
        T out = __take(self.data[self.len])   // move out (no clone); slot vacated
        return Some(out)
    }

    // Remove and return the element at i, shifting [i+1, len) left by one.
    // Valid i is [0, len); out-of-range aborts.
    fn remove(&!self, int i) -> T {
        if i < 0 || i >= self.len {
            print(f"Vec.remove index out of bounds: len={self.len} index={i}")
            std.c.abort()
        }
        T out = __take(self.data[i])
        int j = i
        while j < self.len - 1 {
            T e = __take(self.data[j + 1])    // move element left (no clone)
            self.data[j] = e
            j = j + 1
        }
        self.len = self.len - 1
        return out
    }

    // Drop elements [n, len); keep the first n. n >= len is a no-op; n < 0 is an
    // invalid length and aborts (would otherwise __drop_at a negative slot).
    fn truncate(&!self, int n) {
        if n < 0 {
            print(f"Vec.truncate negative length: n={n}")
            std.c.abort()
        }
        if n >= self.len { return }
        for (int i = n; i < self.len; i = i + 1) { __drop_at(self.data[i]) }
        self.len = n
    }

    // Drop every element, keep the buffer (len -> 0).
    fn clear(&!self) {
        for (int i = 0; i < self.len; i = i + 1) { __drop_at(self.data[i]) }
        self.len = 0
    }

    // ---- access ----

    // UNCHECKED raw read of slot i — deep clone of the element (the buffer keeps
    // its own). Caller MUST keep i in [0, len): out-of-range is undefined behavior
    // (reads garbage / segfaults). The `!` marks this as the unsafe escape hatch.
    fn get!(&self, int i) -> T {
        T tmp = self.data[i]
        return tmp
    }

    // Bounds-checked read: deep clone of element i, aborting on out-of-range.
    fn get(&self, int i) -> T {
        if i < 0 || i >= self.len {
            print(f"Vec index out of bounds: len={self.len} index={i}")
            std.c.abort()
        }
        return self.get!(i)
    }

    // UNCHECKED raw overwrite of slot i: drop the old, move the new in. Caller MUST
    // keep i in [0, len); out-of-range drops/stores at a bogus address (UB).
    fn set!(&!self, int i, T x) {
        __drop_at(self.data[i])
        self.data[i] = x
    }

    // Bounds-checked overwrite, aborting on out-of-range.
    fn set(&!self, int i, T x) {
        if i < 0 || i >= self.len {
            print(f"Vec index out of bounds: len={self.len} index={i}")
            std.c.abort()
        }
        self.set!(i, x)
    }

    // Index / IndexMut protocol (reserved methods): enables `v[i]` (read, clone)
    // and `v[i] = x` (write, drop old + move new). These delegate to the checked
    // get/set, so `v[i]` / `v[i] = x` are bounds-checked (abort on out-of-range).
    fn __index(&self, int i) -> T { return self.get(i) }
    fn __index_set(&!self, int i, T x) { self.set(i, x) }

    // Append deep clones of every element in src. The source Vec is borrowed
    // and remains usable after extend, matching builtin vec.extend.
    fn extend(&!self, &Vec(T) src) {
        self.reserve(self.len + src.len)
        for (int i = 0; i < src.len; i = i + 1) {
            T e = src.data[i]
            self.push(e)
        }
    }

    // Deep-clone [start, end), clamping to [0, len].
    fn slice(&self, int start, int stop) -> Vec(T) {
        Vec(T) out = {}
        int s = start
        int e = stop
        if s < 0 { s = 0 }
        if e < 0 { e = 0 }
        if s > self.len { s = self.len }
        if e > self.len { e = self.len }
        if e <= s { return out }
        out.reserve(e - s)
        for (int i = s; i < e; i = i + 1) {
            T x = self.data[i]
            out.push(x)
        }
        return out
    }

    // Clone of the first / last element, or None when empty.
    fn first(&self) -> Option(T) {
        if self.len == 0 { return None }
        T e = self.data[0]
        return Some(e)
    }
    fn last(&self) -> Option(T) {
        if self.len == 0 { return None }
        T e = self.data[self.len - 1]
        return Some(e)
    }

    // ---- reorder ----

    // Exchange elements i and j (bit-swap, no clone). Both must be in [0, len).
    fn swap(&!self, int i, int j) {
        if i < 0 || i >= self.len || j < 0 || j >= self.len {
            print(f"Vec.swap index out of bounds: len={self.len} i={i} j={j}")
            std.c.abort()
        }
        if i == j { return }
        T a = __take(self.data[i])
        T b = __take(self.data[j])
        self.data[i] = b
        self.data[j] = a
    }

    // Reverse in place.
    fn reverse(&!self) {
        int i = 0
        int j = self.len - 1
        while i < j {
            T a = __take(self.data[i])
            T b = __take(self.data[j])
            self.data[i] = b
            self.data[j] = a
            i = i + 1
            j = j - 1
        }
    }

    // Grow to n (filling new slots with copies of `fill`) or shrink (dropping
    // tail). n < 0 is an invalid length and aborts.
    fn resize(&!self, int n, T fill) {
        if n < 0 {
            print(f"Vec.resize negative length: n={n}")
            std.c.abort()
        }
        if n <= self.len {
            for (int i = n; i < self.len; i = i + 1) { __drop_at(self.data[i]) }
            self.len = n
            return                           // `fill` unused → dropped at scope exit
        }
        self.reserve(n)
        // Move `fill` into the first new slot (a var-decl/assign binds-by-move, so we
        // cannot move it repeatedly in a loop); clone the remaining slots from it.
        self.data[self.len] = fill
        self.len = self.len + 1
        while self.len < n {
            T c = self.data[self.len - 1]    // clone-read the fill already in the buffer
            self.data[self.len] = c
            self.len = self.len + 1
        }
    }

    // ---- search ----

    // Search helpers require equality on T. Method-level `where` plus lazy generic
    // method monomorphization means Vec(Pt) remains usable when Pt has no Eq,
    // and only `v.has?(p)` reports the missing bound.
    fn index_of(&self, T x) -> int where T: Eq {
        for (int i = 0; i < self.len; i = i + 1) {
            T e = self.data[i]
            if e == x { return i }
        }
        return -1
    }

    fn has?(&self, T x) -> bool where T: Eq {
        return self.index_of(x) >= 0
    }

    fn count_eq(&self, T x) -> int where T: Eq {
        int n = 0
        for (int i = 0; i < self.len; i = i + 1) {
            T e = self.data[i]
            if e == x { n = n + 1 }
        }
        return n
    }

    // ---- functional ----

    fn any(&self, Block(T) -> bool pred) -> bool {
        for (int i = 0; i < self.len; i = i + 1) {
            T e = self.data[i]
            if pred(e) { return true }
        }
        return false
    }

    fn all(&self, Block(T) -> bool pred) -> bool {
        for (int i = 0; i < self.len; i = i + 1) {
            T e = self.data[i]
            if !pred(e) { return false }
        }
        return true
    }

    fn count(&self, Block(T) -> bool pred) -> int {
        int n = 0
        for (int i = 0; i < self.len; i = i + 1) {
            T e = self.data[i]
            if pred(e) { n = n + 1 }
        }
        return n
    }

    fn each(&self, Block(T) pred) {
        for (int i = 0; i < self.len; i = i + 1) {
            T e = self.data[i]
            pred(e)
        }
    }

    fn filter(&self, Block(T) -> bool pred) -> Vec(T) {
        Vec(T) out = {}
        for (int i = 0; i < self.len; i = i + 1) {
            T e = self.data[i]
            if pred(e) {
                T keep = self.data[i]
                out.push(keep)
            }
        }
        return out
    }

    fn find(&self, Block(T) -> bool pred) -> Option(T) {
        for (int i = 0; i < self.len; i = i + 1) {
            T e = self.data[i]
            if pred(e) {
                T out = self.data[i]
                return Some(out)
            }
        }
        return None
    }

    fn pos(&self, Block(T) -> bool pred) -> int {
        for (int i = 0; i < self.len; i = i + 1) {
            T e = self.data[i]
            if pred(e) { return i }
        }
        return -1
    }

    // Transform each element with f, collecting results into a new Vec(U).
    // Ownership: each element is deep-cloned from the buffer and moved into f;
    // f returns U which is moved into the output vector.
    fn map(U)(&self, Block(T)->U f) -> Vec(U) {
        Vec(U) out = {}
        out.reserve(self.len)
        for (int i = 0; i < self.len; i = i + 1) {
            T e = self.data[i]
            U u = f(e)
            out.push(u)
        }
        return out
    }

    // Left fold: acc = f(f(f(init, v[0]), v[1]), ..., v[n-1]).
    // init is moved into the accumulator; each element is deep-cloned from the
    // buffer and passed by value to f together with the current accumulator.
    // The accumulator value is replaced by f's return value each iteration.
    fn reduce(U)(&self, U init, Block(U, T)->U f) -> U {
        U acc = init
        for (int i = 0; i < self.len; i = i + 1) {
            T e = self.data[i]
            acc = f(acc, e)
        }
        return acc
    }

    fn sort(&!self) where T: Ord {
        self.sort_by(|a, b| {
            if a < b { return -1 }
            if a > b { return 1 }
            return 0
        })
    }

    // Bottom-up merge sort — O(n log n) worst case and STABLE (equal elements
    // keep their original order, matching the previous insertion sort). `cmp(a,b)`
    // returns <0 if a<b, 0 if equal, >0 if a>b; the result is ascending by cmp.
    // (Was an O(n^2) insertion sort.) Uses a scratch buffer of n slots; elements
    // are MOVED (`__take`, no clone) between data and scratch, so has_drop T is
    // never double-freed. Comparisons still clone-read the two operands (cmp takes
    // T by value) — that per-compare clone is the separate concern tracked in
    // docs/limitations.md (functional/sort clone-on-read).
    fn sort_by(&!self, Block(T, T) -> int cmp) {
        int n = self.len
        if n < 2 { return }
        *T buf = std.c.malloc(n * sizeof(T)) as *T   // uninitialized scratch
        int width = 1
        while width < n {
            int lo = 0
            while lo < n {
                int mid = lo + width
                int hi = lo + width + width
                if mid > n { mid = n }
                if hi > n { hi = n }
                // Merge runs [lo,mid) and [mid,hi) into buf[lo,hi) by moving.
                int i = lo
                int j = mid
                int k = lo
                while i < mid && j < hi {
                    T a = self.data[i]                 // clone for compare
                    T b = self.data[j]
                    if cmp(a, b) <= 0 {                // <= keeps left first on tie (stable)
                        buf[k] = __take(self.data[i]); i = i + 1
                    } else {
                        buf[k] = __take(self.data[j]); j = j + 1
                    }
                    k = k + 1
                }
                while i < mid { buf[k] = __take(self.data[i]); i = i + 1; k = k + 1 }
                while j < hi  { buf[k] = __take(self.data[j]); j = j + 1; k = k + 1 }
                // Move the merged run back into place (source slots were vacated).
                int t = lo
                while t < hi { self.data[t] = __take(buf[t]); t = t + 1 }
                lo = lo + width + width
            }
            width = width + width
        }
        std.c.free(buf as *u8)                          // all elements moved back; buf empty
    }

    // ---- copy ----

    // Independent deep copy.
    fn copy(&self) -> Vec(T) {
        Vec(T) out = {}
        out.reserve(self.len)
        for (int i = 0; i < self.len; i = i + 1) {
            T e = self.data[i]               // clone-on-read each element
            out.push(e)
        }
        return out
    }

    // User deep-copy hook: emit_clone_value calls this when Vec(T) is cloned
    // (e.g. read by value as a nested element), instead of the field-wise auto-clone
    // (which cannot deep-copy the raw *T buffer).
    fn __clone(&self) -> Vec(T) { return self.copy() }

    // Drop every live element (recursively), then free the buffer.
    fn __drop() {
        for (int i = 0; i < self.len; i = i + 1) { __drop_at(self.data[i]) }
        if self.cap > 0 { std.c.free(self.data as *u8) }
    }
}

// Borrowing iterator over a Vec(T). Holds a RAW pointer into the source vec's
// buffer plus a cursor — it does NOT own the buffer, so it is non-has_drop and
// must not outlive the vec it iterates. Produced by Vec.iter(); driven by the
// `for x in v` desugaring (see docs/plan_userdef_for_in.md).
struct VecIter(T) { *T data; int len; int i }

impl(T) VecIter(T) {
    // Iterator(T) protocol: yield the next element by value (clone-on-read for
    // has_drop T, matching Vec.get), or None when exhausted.
    fn next(&!self) -> Option(T) {
        if self.i >= self.len { return None }
        T e = self.data[self.i]          // clone-on-read; buffer keeps its own
        self.i = self.i + 1
        return Some(e)                   // move the clone into Some
    }
}
