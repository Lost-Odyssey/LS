// std/arena.ls — typed bump (region) allocator over RAW malloc/realloc/free.
//
// An Arena(T) is a pool of POD elements carved from ONE backing buffer. Objects
// are addressed by INDEX HANDLES (int), not pointers — so the buffer may grow
// (realloc may move it) without invalidating any handle, and link nodes together
// by storing handles in their fields (graphs / trees / linked lists / ECS).
//
//     Arena(Node) a = {}            // empty pool
//     int h = a.alloc(Node { .. })  // bump-allocate; h is a stable handle
//     Node n = a.get!(h)            // copy a node out by handle
//     a.reset()                     // O(1) bulk reclaim — keep the buffer
//     // a.__drop() at scope exit frees the single backing block
//
// Why an arena beats per-object malloc/free: building N small nodes costs N
// malloc + N free (~75 ns each, see benchmarks/arena). An arena pays 1 malloc
// (amortized over geometric growth) + N raw stores + 1 free, and reset() reuses
// the block across rounds for free. Measured 10–30x on POD node churn.
//
// POD-ONLY (the `where T: Pod` bound on alloc): T must own no heap (no Str / Vec
// / Map / has_drop field, no user __drop). This is enforced at compile time —
// `Arena(Str).alloc(...)` is a type error. The reason is RAII: a bulk reset()
// reclaims the block WITHOUT running any per-element __drop, so a T that owned a
// malloc'd buffer would leak it. Keeping T POD makes that impossible by
// construction. See docs/plan_arena_allocator.md §1/§2/§4 for the full analysis
// (and Phase 2 for a future cap==ARENA sentinel that lifts the POD restriction).
//
// Ownership: Arena owns exactly its one backing buffer (freed by __drop, like
// Vec). The POD elements it hands out are never auto-dropped (raw-pointer / POD
// fields are not has_drop), so bulk reset/free never collides with RAII.
//
// TWO flavors:
//   * Arena(T)  — a TYPED pool of one element type, addressed by INDEX handles.
//                 Auto-grows (realloc safe: handles are indices). Below.
//   * Region    — a HETEROGENEOUS byte region: one *u8 block, sub-allocates
//                 typed `*T` POINTER slices for many POD types (the classic
//                 region allocator for ASTs / mixed node graphs). FIXED capacity
//                 (hands out raw pointers → must NOT realloc). See bottom.

import std.sys.c as c
import std.core.str

struct Arena(T) { *T data; int len; int cap }

methods(T) Arena(T) {
    // ---- capacity ----

    // Ensure capacity for at least `need` elements (geometric growth). A realloc
    // may move the buffer, but handles are indices, so live handles stay valid.
    def reserve(&!self, int need) {
        if need <= self.cap { return }
        int n = self.cap
        if n < 4 { n = 4 }
        while n < need { n = n * 2 }
        self.data = std.sys.c.realloc(self.data as *u8, n * sizeof(T)) as *T
        self.cap = n
    }

    // ---- queries ----

    def len(&self) -> int { return self.len }
    def cap(&self) -> int { return self.cap }
    def empty?(&self) -> bool { return self.len == 0 }

    // ---- allocate ----

    // Bump-allocate one element (auto-grows). Returns its stable index handle.
    // `where T: Pod` rejects has_drop element types at compile time (see header).
    def alloc(&!self, T x) -> int where T: Pod {
        self.reserve(self.len + 1)
        int i = self.len
        self.data[i] = x                 // raw store into uninitialized slot
        self.len = i + 1
        return i
    }

    // ---- access (handles are in [0, len)) ----

    // UNCHECKED copy-out of the element at handle i. Caller must keep i in
    // [0, len): out-of-range is undefined behavior. POD T, so this is a value
    // copy (no clone / no aliasing concern). The `!` marks it unchecked.
    def get!(&self, int i) -> T {
        T tmp = self.data[i]
        return tmp
    }

    // Safe recoverable read: a copy of element i, or None when out-of-range.
    def get(&self, int i) -> Option(T) {
        if i < 0 || i >= self.len { return None }
        return Some(self.get!(i))
    }

    // UNCHECKED raw overwrite of slot i (no drop of the old value — POD). Caller
    // must keep i in [0, len). Lets you mutate a node in place by handle.
    def set!(&!self, int i, T x) {
        self.data[i] = x
    }

    // ---- reclaim ----

    // Bulk reclaim: drop the whole pool in O(1), KEEP the backing buffer for
    // reuse (the per-request / per-frame arena pattern). No per-element work.
    def reset(&!self) { self.len = 0 }

}

methods(T) Arena(T): Destroy {
    // Destructor: free the single backing block. Elements are POD → never need
    // per-element drop, so this is the only free the arena ever does.
    def ~(&!self) {
        if self.cap > 0 { std.sys.c.free(self.data as *u8) }
    }
}

// ============================================================================
// Region — heterogeneous byte-region (bump) allocator.
//
// One *u8 backing block. `alloc(T)()` carves an 8-byte-aligned, sizeof(T)-wide
// slice and returns a raw `*T` into the block (via std.sys.c.__ls_ptr_at — the
// pointer-offset primitive). Use for building mixed-type node graphs / ASTs
// where many small POD objects of DIFFERENT types share one lifetime:
//
//     Region r = {}
//     r.reserve(1 << 16)              // one fixed backing block (bytes)
//     *Node n = r.alloc(Node)()       // carve a *Node
//     n[0] = Node { .. }
//     *Leaf l = r.alloc(Leaf)()       // carve a *Leaf from the SAME block
//     r.reset()                       // O(1) rewind — reuse the block
//     // r.__drop() at scope exit frees the block once
//
//     Region r = {}
//     r.reserve(1 << 16)
//     *Node n = r.alloc_bytes(sizeof(Node)) as *Node   // carve + cast
//     n[0] = Node { .. }
//     r.reset() ; // r.__drop() frees the block
//
// FIXED CAPACITY: Region hands out raw pointers INTO the block, so it must NOT
// realloc (that would move the block and dangle every live pointer). reserve()
// once with enough headroom; alloc_bytes aborts on overflow. (Contrast Arena(T),
// which auto-grows because index handles survive a realloc.)
//
// LOW-LEVEL / RAW: unlike the typed Arena(T), Region deals in `*u8` + sizeof +
// cast — there is no `where T: Pod` gate (you are already casting raw pointers,
// the manual-memory escape hatch, like get!/set!). The POD discipline still
// applies by contract: carve only POD types, since reset/free runs no per-object
// __drop. For a safe, type-checked pool use Arena(T). See
// docs/plan_arena_allocator.md §1/§2/§4. (Method-level generics on a non-generic
// struct aren't supported, so a typed `alloc(T)()` convenience isn't offered;
// callers cast at the use site.)
struct Region { *u8 base; i64 off; i64 cap }

methods Region {
    // Allocate the one fixed backing block (bytes). Call once before alloc_bytes.
    def reserve(&!self, i64 cap) {
        self.base = std.sys.c.malloc(cap) as *u8
        self.off = 0
        self.cap = cap
    }

    def used(&self) -> i64 { return self.off }
    def cap(&self) -> i64 { return self.cap }

    // Carve `size` bytes, 8-byte aligned; return a raw `*u8` into the block (cast
    // it: `r.alloc_bytes(sizeof(T)) as *T`). Aborts on overflow (fixed capacity).
    def alloc_bytes(&!self, i64 size) -> *u8 {
        i64 a = (self.off + 7) / 8 * 8       // align up to 8
        i64 need = a + size
        if need > self.cap {
            @print(f"Region out of capacity: cap={self.cap} need={need}")
            c.abort()
        }
        *u8 p = c.__ls_ptr_at(self.base, a)
        self.off = need
        return p
    }

    // Intern a copy of `src`'s bytes into the region; return a Str VIEW over them
    // (cap == ARENA sentinel = -2). Phase 2 of docs/plan_arena_allocator.md — lets
    // a region hold has_drop Str values (e.g. interned identifiers / AST keys)
    // with zero per-string malloc/free: the bytes live in the block and are
    // reclaimed in bulk by reset()/__drop.
    //
    // The cap == -2 sentinel rides the existing Str cap convention (str.ls):
    //   * __drop  skips free (`if cap > 0`)        → no double-free with bulk free
    //   * __clone deep-copies to a malloc'd Str     → a clone PROMOTES off the
    //       region and safely outlives reset (copy-out / by-value read escape)
    //   * reserve copy-on-grows (`if cap <= 0`)     → mutation auto-promotes too
    //
    // CONTRACT (compiler can't check, like Region's raw pointers): an arena Str
    // that escapes by MOVE (not clone) — stored in a longer-lived var/container —
    // dangles after reset(). Use within the region's lifetime, or clone to keep.
    def str(&!self, &Str src) -> Str {
        int n = src.len()
        *u8 p = self.alloc_bytes(n as i64)
        c.__ls_bytecopy(p, 0, src.as_ptr() as *u8, 0, n)
        Str out = Str { data: p, len: n, cap: 0 - 2 }
        return out
    }

    // Intern a SLICE of `src` — bytes [start, start+len) — into the region; return
    // a region-backed Str (cap == -2 sentinel). The parser/AST interning primitive:
    // carve a token straight out of the source buffer with ZERO malloc — one region
    // bump + one memcpy directly from src's bytes, NO temporary owned Str (unlike
    // `rr.str(src.substr(a, b))`, which mallocs+frees a transient substring first).
    //
    // Lenient clamping (matches Str.substr, str.ls): out-of-range start/len are
    // clipped, never abort — start to [0, len], len to [0, len-start]. A len==0
    // (or fully-clipped) slice yields an empty cap==-2 Str (drop-safe no-op).
    //
    // Same cap==-2 contract as `str` (see header / docs/plan_region_intern_str.md):
    //   * __drop  skips free (region reclaims in bulk)
    //   * __clone deep-copies to malloc → a clone PROMOTES off the region and
    //       safely outlives reset() (copy-out / by-value read escape)
    //   * reserve copy-on-grows (str.ls `<= 0`) → mutation auto-promotes too
    // CONTRACT: an arena Str that escapes by MOVE (not clone) dangles after
    // reset(). Use within the region's lifetime, or clone to keep.
    def str_substr(&!self, &Str src, int start, int len) -> Str {
        int n = src.len()
        int s = start
        if s < 0 { s = 0 }
        if s > n { s = n }
        int l = len
        if l < 0 { l = 0 }
        if s + l > n { l = n - s }
        *u8 p = self.alloc_bytes(l as i64)
        c.__ls_bytecopy(p, 0, src.as_ptr() as *u8, s, l)
        Str out = Str { data: p, len: l, cap: 0 - 2 }
        return out
    }

    // O(1) rewind: reclaim everything, keep the block for reuse. Raw pointers
    // handed out before reset() are now dangling (caller's contract).
    def reset(&!self) { self.off = 0 }

}

methods Region: Destroy {
    // Free the single backing block (elements are POD → no per-object drop).
    def ~(&!self) {
        if self.cap > 0 { std.sys.c.free(self.base) }
    }
}
