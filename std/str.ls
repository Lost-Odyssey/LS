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
import std.vec
import std.hash as _hash

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
        if self.cap == 0 {
            // cap == 0 is STATIC (data points at .rodata / a shared buffer) or
            // empty (data == nil). Either way the pointer is NOT ours to realloc —
            // malloc a fresh buffer and copy the existing bytes (copy-on-grow).
            // This makes a static Str safely mutable without touching .rodata.
            *u8 nd = std.c.malloc(n) as *u8
            for (int i = 0; i < self.len; i = i + 1) { nd[i] = self.data[i] }
            self.data = nd
            self.cap = n
            return
        }
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

    // ---- search (byte layer; needle borrowed) ----

    // Index of the first occurrence of `needle`, or -1. Empty needle -> 0.
    fn find(&self, &Str needle) -> int {
        int n = self.len
        int m = needle.len
        if m == 0 { return 0 }
        if m > n { return -1 }
        int last = n - m
        for (int i = 0; i <= last; i = i + 1) {
            bool hit = true
            for (int j = 0; j < m; j = j + 1) {
                if self.data[i + j] != needle.data[j] { hit = false  break }
            }
            if hit { return i }
        }
        return -1
    }

    fn contains?(&self, &Str needle) -> bool { return self.find(needle) >= 0 }

    fn starts_with?(&self, &Str prefix) -> bool {
        int m = prefix.len
        if m > self.len { return false }
        for (int j = 0; j < m; j = j + 1) {
            if self.data[j] != prefix.data[j] { return false }
        }
        return true
    }

    fn ends_with?(&self, &Str suffix) -> bool {
        int m = suffix.len
        int n = self.len
        if m > n { return false }
        int off = n - m
        for (int j = 0; j < m; j = j + 1) {
            if self.data[off + j] != suffix.data[j] { return false }
        }
        return true
    }

    // ---- slice / transform (owned Str results) ----

    // Substring of `len` bytes starting at `start`. Lenient clamping (matches the
    // old builtin): out-of-range start/len are clipped, never abort.
    fn substr(&self, int start, int len) -> Str {
        int n = self.len
        int s = start
        if s < 0 { s = 0 }
        if s > n { s = n }
        int l = len
        if l < 0 { l = 0 }
        if s + l > n { l = n - s }
        *u8 z = nil
        Str out = Str { data: z, len: 0, cap: 0 }
        out.reserve(l)
        for (int i = 0; i < l; i = i + 1) { out.data[i] = self.data[s + i] }
        out.len = l
        return out
    }

    // ASCII upper/lower (byte layer; non-ASCII bytes pass through unchanged —
    // Unicode case folding is out of scope, §6.5).
    fn upper(&self) -> Str {
        int n = self.len
        *u8 z = nil
        Str out = Str { data: z, len: 0, cap: 0 }
        out.reserve(n)
        for (int i = 0; i < n; i = i + 1) {
            int b = self.data[i]
            if b >= 97 && b <= 122 { b = b - 32 }
            out.data[i] = b as u8
        }
        out.len = n
        return out
    }

    fn lower(&self) -> Str {
        int n = self.len
        *u8 z = nil
        Str out = Str { data: z, len: 0, cap: 0 }
        out.reserve(n)
        for (int i = 0; i < n; i = i + 1) {
            int b = self.data[i]
            if b >= 65 && b <= 90 { b = b + 32 }
            out.data[i] = b as u8
        }
        out.len = n
        return out
    }

    // Trim leading/trailing ASCII whitespace (space, tab, LF, CR).
    fn trim(&self) -> Str {
        int n = self.len
        int s = 0
        while s < n {
            int b = self.data[s]
            if b == 32 || b == 9 || b == 10 || b == 13 { s = s + 1 } else { break }
        }
        int e = n
        while e > s {
            int b = self.data[e - 1]
            if b == 32 || b == 9 || b == 10 || b == 13 { e = e - 1 } else { break }
        }
        return self.substr(s, e - s)
    }

    // Concatenate self ++ other into a fresh owned Str.
    fn concat(&self, &Str other) -> Str {
        int a = self.len
        int b = other.len
        *u8 z = nil
        Str out = Str { data: z, len: 0, cap: 0 }
        out.reserve(a + b)
        for (int i = 0; i < a; i = i + 1) { out.data[i] = self.data[i] }
        for (int i = 0; i < b; i = i + 1) { out.data[a + i] = other.data[i] }
        out.len = a + b
        return out
    }

    // Repeat self `times` times (times <= 0 -> empty).
    fn repeat(&self, int times) -> Str {
        *u8 z = nil
        Str out = Str { data: z, len: 0, cap: 0 }
        if times <= 0 { return out }
        int n = self.len
        out.reserve(n * times)
        int k = 0
        for (int t = 0; t < times; t = t + 1) {
            for (int i = 0; i < n; i = i + 1) {
                out.data[k] = self.data[i]
                k = k + 1
            }
        }
        out.len = n * times
        return out
    }

    // Explicit deep copy (mirrors the builtin string's .copy()). Static (cap 0)
    // sources stay shared-static, owned sources get an independent heap buffer.
    // Useful where an explicit independent value is wanted regardless of the
    // call-site's clone/move policy (e.g. returning a borrowed match binder).
    // NOTE: defined after substr — impl method bodies resolve sibling calls
    // only to methods declared earlier in the block.
    fn copy(&self) -> Str {
        return self.substr(0, self.len)
    }

    // ---- more search (byte layer) ----

    // Index of the LAST occurrence of `needle`, or -1. Empty needle -> len.
    fn rfind(&self, &Str needle) -> int {
        int n = self.len
        int m = needle.len
        if m == 0 { return n }
        if m > n { return -1 }
        for (int i = n - m; i >= 0; i = i - 1) {
            bool hit = true
            for (int j = 0; j < m; j = j + 1) {
                if self.data[i + j] != needle.data[j] { hit = false  break }
            }
            if hit { return i }
        }
        return -1
    }

    // Number of non-overlapping occurrences of `needle`.
    fn count(&self, &Str needle) -> int {
        int m = needle.len
        if m == 0 { return 0 }
        int n = self.len
        int total = 0
        int i = 0
        while i + m <= n {
            bool hit = true
            for (int j = 0; j < m; j = j + 1) {
                if self.data[i + j] != needle.data[j] { hit = false  break }
            }
            if hit { total = total + 1  i = i + m } else { i = i + 1 }
        }
        return total
    }

    // Lexicographic byte comparison: -1 if self < other, 1 if >, 0 if equal.
    fn compare(&self, &Str other) -> int {
        int a = self.len
        int b = other.len
        int n = a
        if b < n { n = b }
        for (int i = 0; i < n; i = i + 1) {
            int x = self.data[i]
            int y = other.data[i]
            if x < y { return -1 }
            if x > y { return 1 }
        }
        if a < b { return -1 }
        if a > b { return 1 }
        return 0
    }

    // ---- replace / pad (owned Str results) ----

    // Replace every non-overlapping occurrence of `old` with `rep`. Empty `old`
    // copies self unchanged.
    fn replace(&self, &Str old, &Str rep) -> Str {
        *u8 z = nil
        Str out = Str { data: z, len: 0, cap: 0 }
        int m = old.len
        int n = self.len
        if m == 0 {
            out.reserve(n)
            for (int i = 0; i < n; i = i + 1) { out.data[i] = self.data[i] }
            out.len = n
            return out
        }
        int i = 0
        while i < n {
            bool hit = false
            if i + m <= n {
                hit = true
                for (int j = 0; j < m; j = j + 1) {
                    if self.data[i + j] != old.data[j] { hit = false  break }
                }
            }
            if hit {
                for (int j = 0; j < rep.len; j = j + 1) { out.push_byte(rep.data[j]) }
                i = i + m
            } else {
                out.push_byte(self.data[i])
                i = i + 1
            }
        }
        return out
    }

    // Left/right pad with `fill` byte until at least `width` bytes wide.
    fn pad_left(&self, int width, int fill) -> Str {
        int n = self.len
        *u8 z = nil
        Str out = Str { data: z, len: 0, cap: 0 }
        if width > n {
            int pad = width - n
            for (int i = 0; i < pad; i = i + 1) { out.push_byte(fill) }
        }
        for (int i = 0; i < n; i = i + 1) { out.push_byte(self.data[i]) }
        return out
    }

    fn pad_right(&self, int width, int fill) -> Str {
        int n = self.len
        *u8 z = nil
        Str out = Str { data: z, len: 0, cap: 0 }
        for (int i = 0; i < n; i = i + 1) { out.push_byte(self.data[i]) }
        if width > n {
            int pad = width - n
            for (int i = 0; i < pad; i = i + 1) { out.push_byte(fill) }
        }
        return out
    }

    // ---- collections (byte/substring layer; return Vec) ----

    // Every byte as an int (0..255). §6.3: byte layer — the name `chars()` is
    // reserved for a future codepoint-layer iterator (Unicode).
    fn bytes(&self) -> Vec(int) {
        Vec(int) out = {}
        for (int i = 0; i < self.len; i = i + 1) { out.push(self.data[i]) }
        return out
    }

    // Split on every non-overlapping `sep`. Empty sep yields one element (the
    // whole string). A trailing sep yields a trailing empty element.
    fn split(&self, &Str sep) -> Vec(Str) {
        Vec(Str) out = {}
        int sn = sep.len
        int n = self.len
        if sn == 0 {
            out.push(self.substr(0, n))
            return out
        }
        int start = 0
        int i = 0
        while i + sn <= n {
            bool hit = true
            for (int j = 0; j < sn; j = j + 1) {
                if self.data[i + j] != sep.data[j] { hit = false  break }
            }
            if hit {
                out.push(self.substr(start, i - start))
                i = i + sn
                start = i
            } else {
                i = i + 1
            }
        }
        out.push(self.substr(start, n - start))
        return out
    }

    // Split into lines on '\n', stripping a preceding '\r' (CRLF). A trailing
    // newline does NOT yield a final empty element.
    fn lines(&self) -> Vec(Str) {
        Vec(Str) out = {}
        int n = self.len
        if n == 0 { return out }
        int start = 0
        int i = 0
        while i < n {
            int ch = self.data[i]
            if ch == 10 {
                int cut = i
                if cut > start {
                    int prev = self.data[cut - 1]
                    if prev == 13 { cut = cut - 1 }
                }
                out.push(self.substr(start, cut - start))
                start = i + 1
            }
            i = i + 1
        }
        if start < n { out.push(self.substr(start, n - start)) }
        return out
    }

    // ---- parsing (Result(T, Str); Err payload is a diagnostic Str) ----

    // Parse a signed decimal integer. Lenient: no overflow check (matches the
    // old builtin). Err on empty / sign-only / non-digit.
    fn to_int(&self) -> Result(int, Str) {
        int n = self.len
        if n == 0 { return Err("empty string") }
        int i = 0
        bool neg = false
        int first = self.data[0]
        if first == 45 { neg = true  i = 1 }
        else if first == 43 { i = 1 }
        if i >= n { return Err("no digits") }
        int val = 0
        while i < n {
            int d = self.data[i]
            if d < 48 || d > 57 { return Err("invalid digit") }
            val = val * 10 + (d - 48)
            i = i + 1
        }
        if neg { val = 0 - val }
        return Ok(val)
    }

    fn to_i64(&self) -> Result(i64, Str) {
        int n = self.len
        if n == 0 { return Err("empty string") }
        int i = 0
        bool neg = false
        int first = self.data[0]
        if first == 45 { neg = true  i = 1 }
        else if first == 43 { i = 1 }
        if i >= n { return Err("no digits") }
        i64 val = 0
        while i < n {
            int d = self.data[i]
            if d < 48 || d > 57 { return Err("invalid digit") }
            val = val * 10 + ((d - 48) as i64)
            i = i + 1
        }
        if neg { val = 0 - val }
        return Ok(val)
    }

    // Parse a decimal float (sign, integer part, optional '.fraction'; no
    // exponent — keep it simple, §6.5 defers full numeric parsing).
    fn to_float(&self) -> Result(f64, Str) {
        int n = self.len
        if n == 0 { return Err("empty string") }
        int i = 0
        bool neg = false
        int first = self.data[0]
        if first == 45 { neg = true  i = 1 }
        else if first == 43 { i = 1 }
        f64 val = 0.0
        bool any = false
        while i < n {
            int d = self.data[i]
            if d == 46 { break }
            if d < 48 || d > 57 { return Err("invalid digit") }
            val = val * 10.0 + ((d - 48) as f64)
            any = true
            i = i + 1
        }
        if i < n {
            i = i + 1
            f64 scale = 0.1
            while i < n {
                int d = self.data[i]
                if d < 48 || d > 57 { return Err("invalid digit") }
                val = val + ((d - 48) as f64) * scale
                scale = scale * 0.1
                any = true
                i = i + 1
            }
        }
        if !any { return Err("no digits") }
        if neg { val = 0.0 - val }
        return Ok(val)
    }

    // Parse "true"/"false". Err otherwise.
    fn to_bool(&self) -> Result(bool, Str) {
        Str t = "true"
        Str f = "false"
        if self.eq?(t) { return Ok(true) }
        if self.eq?(f) { return Ok(false) }
        return Err("invalid bool")
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

// Operator `==` (trait Eq; `!=` derives). Needed e.g. for Str as a Map key
// (`where K: Hash + Eq`). Operator method names are only legal inside
// `impl Trait for Type` blocks, hence the separate block.
impl Eq for Str {
    fn ==(&self, &Str rhs) -> bool {
        if self.len != rhs.len { return false }
        for (int i = 0; i < self.len; i = i + 1) {
            if self.data[i] != rhs.data[i] { return false }
        }
        return true
    }
}

// Byte-wise FxHash (same algorithm as std.hash's builtin-string impl, which
// P5 deletes). Lives here — not in std/hash.ls — so the method symbol carries
// this module's type prefix (std_str__Str.hash); see the note in std/hash.ls.
impl Hash for Str {
    fn hash(&self) -> u64 {
        u64 h = 0 as u64
        int n = self.len
        int i = 0
        while i < n {
            u64 b = self.data[i] as u64
            h = _hash.fx_mix(h, b)
            i = i + 1
        }
        return h
    }
}

// Operator `+` (trait Add): byte-wise concatenation producing a new owned Str.
// Makes `a + b + "lit"` work for Str (literals coerce to &Str at the rhs),
// which keeps the +-heavy std modules (strconv/plotfmt/plot/plottl) mechanical
// to migrate. Bodies are inlined byte loops — no borrow-of-borrow forwarding.
impl Add for Str {
    fn +(&self, &Str rhs) -> Str {
        Str out = ""
        out.reserve(self.len + rhs.len)
        for (int i = 0; i < self.len; i = i + 1) { out.push_byte(self.data[i]) }
        for (int i = 0; i < rhs.len; i = i + 1) { out.push_byte(rhs.data[i]) }
        return out
    }
}
