// std/map.ls — pure-LS hash map (open addressing + Robin Hood + Fibonacci
// scatter), the replacement for the builtin `map`. See docs/plan_std_map.md.
//
// M-0: struct + construct + set / get / has? / len (+ grow/rehash) for POD K/V
// (e.g. Map(int,int)). has_drop K/V ownership (M-2), remove/backward-shift (M-1),
// and MapIter/literals (M-3/M-LIT) land in later phases.
//
// Layout (SoA): three parallel buffers of `cap` slots —
//   ctrl[i]  : 1 byte per slot. 255 (0xFF) = EMPTY; otherwise the slot's probe
//              sequence length (PSL = distance from its Fibonacci home bucket).
//   keys[i]  : the key   (live only where ctrl[i] != EMPTY)
//   vals[i]  : the value (live only where ctrl[i] != EMPTY)
// cap is always a power of two; `shift = 64 - log2(cap)` drives the Fibonacci
// scatter. No tombstones (backward-shift delete, M-1, keeps the invariant).
//
// Ownership (M-0 is POD-only, so these are all bit-copies; M-2 wires up the
// __take/__move/__drop_at paths for has_drop K/V):
//   set    moves K,V into the table (overwrite drops the old value).
//   get    returns a CLONE of the value (Option(V)); buffer keeps its own.

import std.hash

struct Map(K, V) { *u8 ctrl; *K keys; *V vals; int len; int cap; int shift }

impl(K, V) Map(K, V) {
    // ---- queries ----

    fn len(&self) -> int { return self.len }
    fn cap(&self) -> int { return self.cap }
    fn empty?(&self) -> bool { return self.len == 0 }

    // Fibonacci scatter: map a 64-bit hash to a home bucket in [0, cap) using the
    // HIGH bits of (h * 2^64/phi). FxHash's good entropy lives in the high bits,
    // so this beats `h % cap` (low bits, weak) — see docs/plan_std_map.md §3/§5.1.
    fn _home(&self, u64 h) -> int {
        u64 fib = 11400714819323198485 as u64    // 0x9E3779B97F4A7C15
        u64 sh = self.shift as u64
        u64 scattered = (h * fib) >> sh
        return scattered as int
    }

    // ---- capacity ----

    // Allocate the next power-of-two table (8 first, else 2x), fill ctrl with
    // EMPTY, and reinsert every live entry (moved, not cloned). Frees the old
    // buffers. shift tracks 64 - log2(cap) incrementally across doublings.
    fn _grow(&!self) where K: Hash + Eq {
        int newcap = 8
        int newshift = 61                         // 64 - log2(8)
        if self.cap > 0 {
            newcap = self.cap * 2
            newshift = self.shift - 1
        }
        // Save the old buffers (these `*`-leading decls need ';' so the next line
        // is not glued on as `* <next>`; the preceding `}` makes the first safe).
        *u8 oldctrl = self.ctrl;
        *K oldkeys = self.keys;
        *V oldvals = self.vals;
        int oldcap = self.cap;
        // Fresh buffers (realloc(nil, n) == malloc(n); reuse one *u8 nil).
        *u8 z = nil
        self.ctrl = realloc(z, newcap) as *u8
        self.keys = realloc(z, newcap * sizeof(K)) as *K
        self.vals = realloc(z, newcap * sizeof(V)) as *V
        self.cap = newcap
        self.shift = newshift
        self.len = 0
        for (int i = 0; i < newcap; i = i + 1) { self.ctrl[i] = 255 as u8 }
        // Rehash: move each live entry into the new table.
        for (int i = 0; i < oldcap; i = i + 1) {
            int c = oldctrl[i] as int
            if c != 255 {
                K k = __take(oldkeys[i])
                V v = __take(oldvals[i])
                u64 h = k.hash()
                self._insert_no_grow(k, v, h)
            }
        }
        if oldcap > 0 {
            free(oldctrl)
            free(oldkeys as *u8)
            free(oldvals as *u8)
        }
    }

    // ---- insert / update ----

    // Place (k, v, h) into the table without checking load factor (caller grows
    // first). Robin Hood by FORWARD-shift: scan (read-only) for the target slot —
    // a key match (update), an empty slot, or the first resident closer to its
    // home than us (the swap point). Then move k/v in exactly ONCE at that slot;
    // if it was occupied, first shift the contiguous run forward by one (PSL+1) to
    // open it. Moving only slot entries (via __take) — never reassigning k inside
    // the loop — keeps the move checker happy with has_drop K/V. h = hash(k).
    fn _insert_no_grow(&!self, K k, V v, u64 h) where K: Eq {
        int mask = self.cap - 1
        int idx = self._home(h)
        int psl = 0
        // Phase 1 — classify (no moves of k/v). On exit: idx/psl = insertion slot.
        while true {
            int c = self.ctrl[idx] as int
            if c == 255 { break }            // empty → place here
            K existing = self.keys[idx]
            if existing == k {
                // Update in place: drop the old value, move the new one in.
                // k is unused here and is dropped at scope exit (RAII).
                __drop_at(self.vals[idx])
                self.vals[idx] = v
                return
            }
            if c < psl { break }             // swap point → open here, shift run fwd
            psl = psl + 1
            idx = (idx + 1) & mask
        }
        // Phase 2 — if the insertion slot is occupied, shift the run [idx..end)
        // forward by one (end = first empty slot), bumping each PSL by 1.
        int here = self.ctrl[idx] as int
        if here != 255 {
            int tail = idx
            int tc = self.ctrl[tail] as int
            while tc != 255 {
                tail = (tail + 1) & mask
                tc = self.ctrl[tail] as int
            }
            int p = tail
            while p != idx {
                int prev = (p - 1) & mask
                K mk = __take(self.keys[prev])
                V mv = __take(self.vals[prev])
                self.keys[p] = mk
                self.vals[p] = mv
                self.ctrl[p] = ((self.ctrl[prev] as int) + 1) as u8
                p = prev
            }
        }
        // Place the new entry (k/v moved exactly once).
        self.keys[idx] = k
        self.vals[idx] = v
        self.ctrl[idx] = psl as u8
        self.len = self.len + 1
    }

    // Insert or update: move k,v into the table.
    fn set(&!self, K k, V v) where K: Hash + Eq {
        // Grow when empty, or when load factor would exceed 7/8.
        if self.cap == 0 {
            self._grow()
        } else {
            int need = (self.len + 1) * 8
            int lim = self.cap * 7
            if need > lim { self._grow() }
        }
        u64 h = k.hash()
        self._insert_no_grow(k, v, h)
    }

    // ---- lookup ----

    // Slot index of key k (hash h precomputed), or -1 if absent. Early-terminates
    // on the Robin Hood invariant: once our PSL exceeds the resident's, k cannot
    // be further along.
    fn _find(&self, K k, u64 h) -> int where K: Eq {
        if self.cap == 0 { return -1 }
        int mask = self.cap - 1
        int idx = self._home(h)
        int psl = 0
        while true {
            int c = self.ctrl[idx] as int
            if c == 255 { return -1 }
            if c < psl { return -1 }
            K existing = self.keys[idx]
            if existing == k { return idx }
            psl = psl + 1
            idx = (idx + 1) & mask
        }
    }

    // Clone of the value for k, or None when absent.
    fn get(&self, K k) -> Option(V) where K: Hash + Eq {
        u64 h = k.hash()
        int idx = self._find(k, h)
        if idx < 0 { return None }
        V v = self.vals[idx]
        return Some(v)
    }

    fn has?(&self, K k) -> bool where K: Hash + Eq {
        u64 h = k.hash()
        return self._find(k, h) >= 0
    }

    // ---- remove ----

    // Remove key k and return its value (moved out), or None if absent.
    // Backward-shift deletion (no tombstones): after vacating the slot, pull each
    // following entry back one position and decrement its PSL, until the next slot
    // is empty or already at its home (PSL 0). This keeps the Robin Hood invariant
    // intact so the table never degrades under churn. See docs/plan_std_map.md §5.4.
    fn remove(&!self, K k) -> Option(V) where K: Hash + Eq {
        u64 h = k.hash()
        int i = self._find(k, h)
        if i < 0 { return None }
        // Take the value out (to return), drop the key in place.
        V out = __take(self.vals[i])
        __drop_at(self.keys[i])
        // Backward-shift the run that follows.
        int mask = self.cap - 1
        int j = i
        while true {
            int nk = (j + 1) & mask
            int c = self.ctrl[nk] as int
            if c == 255 {
                self.ctrl[j] = 255 as u8        // next is empty → done
                break
            }
            if c == 0 {
                self.ctrl[j] = 255 as u8        // next is at home → must not move it
                break
            }
            K mk = __take(self.keys[nk])
            V mv = __take(self.vals[nk])
            self.keys[j] = mk
            self.vals[j] = mv
            self.ctrl[j] = (c - 1) as u8        // moved one closer to its home
            j = nk
        }
        self.len = self.len - 1
        return Some(out)
    }

    // Drop every live entry, keep the buffer (len -> 0). Cheap reuse without
    // reallocating. Needs no Hash/Eq (no home recompute).
    fn clear(&!self) {
        for (int i = 0; i < self.cap; i = i + 1) {
            int c = self.ctrl[i] as int
            if c != 255 {
                __drop_at(self.keys[i])
                __drop_at(self.vals[i])
                self.ctrl[i] = 255 as u8
            }
        }
        self.len = 0
    }

    // ---- copy / drop hooks ----

    // Deep copy preserving the exact table layout (no rehash): clone each live
    // entry into the same slot, copy ctrl bytes verbatim. Empty slots' key/val
    // memory is never read (ctrl marks them EMPTY), so leaving it uninitialized
    // is safe. Needs no Hash/Eq since it never recomputes a home bucket.
    fn __clone(&self) -> Map(K, V) {
        Map(K, V) out = {}
        if self.cap == 0 { return out }
        *u8 z = nil
        out.ctrl = realloc(z, self.cap) as *u8
        out.keys = realloc(z, self.cap * sizeof(K)) as *K
        out.vals = realloc(z, self.cap * sizeof(V)) as *V
        out.cap = self.cap
        out.shift = self.shift
        out.len = self.len
        for (int i = 0; i < self.cap; i = i + 1) {
            int c = self.ctrl[i] as int
            out.ctrl[i] = c as u8
            if c != 255 {
                K k = self.keys[i]
                V v = self.vals[i]
                out.keys[i] = k
                out.vals[i] = v
            }
        }
        return out
    }

    // Drop every live entry, then free the buffers.
    fn __drop() {
        for (int i = 0; i < self.cap; i = i + 1) {
            int c = self.ctrl[i] as int
            if c != 255 {
                __drop_at(self.keys[i])
                __drop_at(self.vals[i])
            }
        }
        if self.cap > 0 {
            free(self.ctrl)
            free(self.keys as *u8)
            free(self.vals as *u8)
        }
    }
}
