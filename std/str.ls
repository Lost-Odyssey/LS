// std/str.ls — pure-LS owning string type `Str` over a raw UTF-8 byte buffer.
//
// Phase P0 of docs/plan_string_to_stdlib.md: this is the SKELETON step. The
// compiler still uses the builtin `string` for literals / f-strings / print;
// `Str` coexists as a manually-usable type so we can validate the unified
// has_drop ownership path (drop / clone / move / temp) BEFORE collapsing the
// builtin string onto it (P1..P5).
//
// Storage (§6.1, locked now): UTF-8 bytes, `len`/`cap` are BYTE counts (not
// char counts). The layout is intentionally minimal `{ *u8, int, int }` — no
// cached char-count; char/codepoint length is always computed O(n) later.
//
// cap three-state (§4), routed through the unified struct ownership path:
//   * cap == 0   static (e.g. a literal pointing at .rodata): __drop skips
//                free, __clone shallow-copies the pointer.
//   * cap >  0   owns a heap buffer: scope-drop frees it, __clone deep-copies.
//   * moved      handled by the unified has_drop moved_flag (NOT the old
//                builtin-string cap == -1 convention).
//
// Reserved-method protocols the compiler will hook in later phases:
//   * __from_static(ptr, len)  P1: string literal -> static Str (cap == 0).
//   * __from_parts             P2: f-string builder appends into an owned Str.
//   * __clone / __drop         the generic has_drop deep-copy / destructor hooks.

import std.c

struct Str { *u8 data; int len; int cap }

impl Str {
    // ---- capacity ----

    // Ensure capacity for at least `need` BYTES (geometric growth). cap/len are
    // byte counts, so element size is 1 — no sizeof(T) scaling like Vec.
    fn reserve(&!self, int need) {
        if need <= self.cap { return }
        int n = self.cap
        if n < 8 { n = 8 }
        while n < need { n = n * 2 }
        self.data = std.c.realloc(self.data as *u8, n) as *u8
        self.cap = n
    }

    // ---- queries ----

    // Byte length, O(1) (§6.3: `.len()` is ALWAYS the byte count, never chars).
    fn len(&self) -> int { return self.len }
    fn cap(&self) -> int { return self.cap }
    fn empty?(&self) -> bool { return self.len == 0 }
    fn as_ptr(&self) -> object { return self.data as object }

    // Byte at index i (§6.3 byte layer). Bounds-checked: out-of-range aborts
    // (matches Vec.get / the safe default). `byte_at!` is the `!`-style
    // unchecked escape hatch (LS convention: `!` = unsafe, cf. Vec.get!/set!).
    fn byte_at(&self, int i) -> int {
        if i < 0 || i >= self.len {
            print("Str byte index out of bounds")
            std.c.abort()
        }
        return self.data[i]
    }
    fn byte_at!(&self, int i) -> int { return self.data[i] }

    // ---- build (byte layer) ----

    // Append a single byte. Used by the constructors below; once the literal /
    // f-string lowerings land (P1/P2) the compiler builds Str through these.
    fn push_byte(&!self, int b) {
        self.reserve(self.len + 1)
        self.data[self.len] = b as u8
        self.len = self.len + 1
    }

    // Append every byte of another Str (owned-concat building block; f-string
    // append chain in P2 routes through here / __from_parts).
    fn push_str(&!self, &Str other) {
        int n = other.len
        self.reserve(self.len + n)
        for (int i = 0; i < n; i = i + 1) {
            self.data[self.len] = other.data[i]
            self.len = self.len + 1
        }
    }

    // ---- reserved construction protocols (compiler hooks, later phases) ----

    // P1: a string literal lowers to `Str { data: <.rodata ptr>, len, cap: 0 }`.
    // Static => cap 0 => __drop skips free, __clone shallow-copies. Bytes are
    // assumed already-valid UTF-8 (no validation; §6.4).
    static fn __from_static(*u8 ptr, int len) -> Str {
        return Str { data: ptr, len: len, cap: 0 }
    }

    // P2: f-string builds an OWNED Str by appending parts. v1 seed = empty owned
    // buffer; callers append via push_str / push_byte. Kept as the named protocol
    // so the f-string lowering has a stable entry point.
    static fn __from_parts() -> Str {
        *u8 z = nil
        return Str { data: z, len: 0, cap: 0 }
    }

    // ---- interop with builtin string (P0 bridge; removed once string is gone) ----

    // Build an OWNED Str by copying the bytes of a builtin string. This is the
    // P0 validation bridge: it lets us exercise the unified ownership path while
    // the builtin string still produces every literal. Disappears in P5.
    static fn from_string(string s) -> Str {
        *u8 z = nil
        Str out = Str { data: z, len: 0, cap: 0 }
        int n = s.length
        out.reserve(n)
        for (int i = 0; i < n; i = i + 1) {
            out.push_byte(s.at_unsafe(i))
        }
        return out
    }

    // Materialise a builtin string from the bytes (P0 read-back bridge, also for
    // printing while @print is not yet wired — P3). Removed in P5.
    fn to_string(&self) -> string {
        string out = ""
        for (int i = 0; i < self.len; i = i + 1) {
            out.append(self.data[i])
        }
        return out
    }

    // ---- equality (byte-wise) ----

    fn eq?(&self, &Str other) -> bool {
        if self.len != other.len { return false }
        for (int i = 0; i < self.len; i = i + 1) {
            if self.data[i] != other.data[i] { return false }
        }
        return true
    }

    // ---- ownership hooks (unified has_drop path) ----

    // Deep-copy hook: emit_clone_value calls this when a Str is cloned (by-value
    // param, nested element read, etc.). cap > 0 => independent heap copy;
    // cap == 0 (static) => shallow pointer copy (the .rodata bytes are shared,
    // never freed).
    fn __clone(&self) -> Str {
        if self.cap == 0 {
            return Str { data: self.data, len: self.len, cap: 0 }
        }
        *u8 z = nil
        Str out = Str { data: z, len: 0, cap: 0 }
        out.reserve(self.len)
        for (int i = 0; i < self.len; i = i + 1) {
            out.data[i] = self.data[i]
        }
        out.len = self.len
        return out
    }

    // Destructor: free the buffer only when we own it (cap > 0). Static strings
    // (cap == 0) point at .rodata and must not be freed.
    fn __drop() {
        if self.cap > 0 { std.c.free(self.data as *u8) }
    }
}
