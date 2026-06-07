// std/rawvec.ls — generic dynamic array over RAW malloc/realloc/free.
//
// A pure-LS container that manages its OWN heap buffer (unlike std.stack, which
// wraps the builtin vec(T)). Goal: be memory-identical and behave identically to
// the builtin vec(T). Construction matches vec:
//
//     RawVec(string) v = {}            // empty   (= vec(string) v = [])
//     RawVec(int)    v = [1, 2, 3]     // literal (= vec(int) v = [1,2,3])
//
// Ownership primitives used:
//   * sizeof(T)         — element byte size (compile-time, per monomorphization)
//   * realloc / free    — grow / release the raw buffer (memcheck-tracked)
//   * p[i] (read)       — typed element read; DEEP-CLONES owned data (matches vec[i])
//   * p[i] = x (write)  — raw store (no drop of the old slot)
//   * __take(slot)      — move OUT of a slot (bit-read, no clone)
//   * __drop_at(slot)   — recursive destructor on a slot (frees owned data)
//   * __move(local)     — transfer ownership of a named local
//   * fn __clone(&self) — user deep-copy hook (RawVec(RawVec(T)) reads deep-clone)
//   * fn __from_list    — list-literal init opt-in (the `[..]` protocol)
//
// Ownership contract (identical to vec(T)):
//   push/insert  move T into the buffer.   pop/remove  move T out (no clone).
//   get/first/last  return a deep CLONE.   set  drops old, moves new in.
//   clear / scope-drop  drop every live element, then free the buffer.

struct RawVec(T) { *T data; int len; int cap }

impl(T) RawVec(T) {
    // ---- capacity ----

    // Ensure capacity for at least `need` elements (geometric growth).
    fn reserve(&!self, int need) {
        if need <= self.cap { return }
        int n = self.cap
        if n < 4 { n = 4 }
        while n < need { n = n * 2 }
        self.data = realloc(self.data as *u8, n * sizeof(T)) as *T
        self.cap = n
    }

    // Release unused capacity (cap -> len).
    fn shrink_to_fit(&!self) {
        if self.cap == self.len { return }
        if self.len == 0 {
            if self.cap > 0 { free(self.data as *u8) }
            *T p = nil
            self.data = p
            self.cap = 0
            return
        }
        self.data = realloc(self.data as *u8, self.len * sizeof(T)) as *T
        self.cap = self.len
    }

    // ---- queries ----

    fn length(&self) -> int { return self.len }
    fn capacity(&self) -> int { return self.cap }
    fn is_empty(&self) -> bool { return self.len == 0 }
    fn as_ptr(&self) -> object { return self.data as object }

    // ---- add ----

    // Move x onto the end of the buffer.
    fn push(&!self, T x) {
        self.reserve(self.len + 1)
        self.data[self.len] = x          // raw store into uninitialized slot
        self.len = self.len + 1
    }

    // Insert x at index i, shifting [i, len) right by one.
    fn insert(&!self, int i, T x) {
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
    // having this method enables `RawVec(T) v = [a, b, c]` (matches vec(T) v=[..]).
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
    fn remove(&!self, int i) -> T {
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

    // Drop elements [n, len); keep the first n.
    fn truncate(&!self, int n) {
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

    // Deep clone of the element at i (the buffer keeps its own). Caller must keep
    // i in [0, len) — the raw read is unchecked (matches vec[i] at the API edge).
    fn get(&self, int i) -> T {
        T tmp = self.data[i]
        return tmp
    }

    fn get_unsafe(&self, int i) -> T { return self.get(i) }

    // Overwrite element i: drop the old, move the new in.
    fn set(&!self, int i, T x) {
        __drop_at(self.data[i])
        self.data[i] = x
    }

    // Index / IndexMut protocol (reserved methods): enables `v[i]` (read, clone)
    // and `v[i] = x` (write, drop old + move new), matching builtin vec[i].
    fn __index(&self, int i) -> T { return self.get(i) }
    fn __index_set(&!self, int i, T x) { self.set(i, x) }

    // Append deep clones of every element in src. The source RawVec is borrowed
    // and remains usable after extend, matching builtin vec.extend.
    fn extend(&!self, &RawVec(T) src) {
        self.reserve(self.len + src.len)
        for (int i = 0; i < src.len; i = i + 1) {
            T e = src.data[i]
            self.push(e)
        }
    }

    // Deep-clone [start, end), clamping to [0, len].
    fn slice(&self, int start, int stop) -> RawVec(T) {
        RawVec(T) out = {}
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

    // Exchange elements i and j (bit-swap, no clone).
    fn swap(&!self, int i, int j) {
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

    // Grow to n (filling new slots with copies of `fill`) or shrink (dropping tail).
    fn resize(&!self, int n, T fill) {
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
    // method monomorphization means RawVec(Pt) remains usable when Pt has no Eq,
    // and only `v.contains(p)` reports the missing bound.
    fn index_of(&self, T x) -> int where T: Eq {
        for (int i = 0; i < self.len; i = i + 1) {
            T e = self.data[i]
            if e == x { return i }
        }
        return -1
    }

    fn contains(&self, T x) -> bool where T: Eq {
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

    fn filter(&self, Block(T) -> bool pred) -> RawVec(T) {
        RawVec(T) out = {}
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

    fn find_index(&self, Block(T) -> bool pred) -> int {
        for (int i = 0; i < self.len; i = i + 1) {
            T e = self.data[i]
            if pred(e) { return i }
        }
        return -1
    }

    // Transform each element with f, collecting results into a new RawVec(U).
    // Ownership: each element is deep-cloned from the buffer and moved into f;
    // f returns U which is moved into the output vector.
    fn map(U)(&self, Block(T)->U f) -> RawVec(U) {
        RawVec(U) out = {}
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

    fn sort_by(&!self, Block(T, T) -> int cmp) {
        if self.len < 2 { return }
        for (int i = 1; i < self.len; i = i + 1) {
            int j = i
            while j > 0 {
                T a = self.data[j - 1]
                T b = self.data[j]
                if cmp(a, b) <= 0 { break }
                self.swap(j - 1, j)
                j = j - 1
            }
        }
    }

    // ---- copy ----

    // Independent deep copy.
    fn copy(&self) -> RawVec(T) {
        RawVec(T) out = {}
        out.reserve(self.len)
        for (int i = 0; i < self.len; i = i + 1) {
            T e = self.data[i]               // clone-on-read each element
            out.push(e)
        }
        return out
    }

    // User deep-copy hook: emit_clone_value calls this when RawVec(T) is cloned
    // (e.g. read by value as a nested element), instead of the field-wise auto-clone
    // (which cannot deep-copy the raw *T buffer).
    fn __clone(&self) -> RawVec(T) { return self.copy() }

    // Drop every live element (recursively), then free the buffer.
    fn __drop() {
        for (int i = 0; i < self.len; i = i + 1) { __drop_at(self.data[i]) }
        if self.cap > 0 { free(self.data as *u8) }
    }
}
