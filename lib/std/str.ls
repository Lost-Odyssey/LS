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

import std.c as c
import std.vec
import std.hash as _hash

struct Str { *u8 data; int len; int cap }

// A borrowed view into a Str: byte offset + length, NO owned buffer (POD, not
// has_drop). Produced by `split_view` to defer the per-part heap copy that
// `split` pays eagerly — materialize on demand with `Str.slice_str(view)`.
struct StrSlice { int off; int len }

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
            *u8 nd = c.malloc(n) as *u8
            c.__ls_bytecopy(nd, 0, self.data, 0, self.len)
            self.data = nd
            self.cap = n
            return
        }
        self.data = c.realloc(self.data as *u8, n) as *u8
        self.cap = n
    }

    // ---- queries ----

    // Byte length, O(1) (§6.3: `.len()` is ALWAYS the byte count, never chars).
    fn len(&self) -> int { return self.len }
    fn cap(&self) -> int { return self.cap }
    fn empty?(&self) -> bool { return self.len == 0 }
    fn as_ptr(&self) -> object { return self.data as object }

    // FFI marshalling (P5-0, docs/plan_p5_remove_builtin_string.md §4):
    // NUL-terminated view of the buffer for C `char*` consumers.
    //   * cap == 0 && len > 0 — static/shared C string (a compiler-emitted
    //     literal via LLVMBuildGlobalStringPtr, or a bridge view of a static
    //     builtin string): already NUL-terminated, returned as-is (zero cost).
    //   * otherwise (owned buffer, empty literal, or zero-init nil data) —
    //     reserve one spare byte and write data[len] = 0 (len unchanged;
    //     reserve handles nil data by mallocing a fresh buffer).
    // NOTE: bytes after an embedded 0 are invisible to C (C-string semantics).
    fn c_str(&!self) -> *u8 {
        if self.cap == 0 && self.len > 0 { return self.data }
        self.reserve(self.len + 1)
        self.data[self.len] = 0 as u8
        return self.data
    }

    // Byte at index i (§6.3 byte layer). Bounds-checked: out-of-range aborts
    // (matches Vec.get / the safe default). `byte_at!` is the `!`-style
    // unchecked escape hatch (LS convention: `!` = unsafe, cf. Vec.get!/set!).
    fn byte_at(&self, int i) -> int {
        if i < 0 || i >= self.len {
            print("Str byte index out of bounds")
            c.abort()
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
        c.__ls_bytecopy(self.data, self.len, other.data, 0, n)
        self.len = self.len + n
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
    // Delegates to the runtime memchr+memcmp scan (SIMD-accelerated in the CRT)
    // over raw ptr+len — no NUL needed, so the read-only &self borrow is safe.
    fn find(&self, &Str needle) -> int {
        return c.__ls_str_find(self.data, self.len, needle.data, needle.len, 0)
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
        c.__ls_bytecopy(out.data, 0, self.data, s, l)
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
        c.__ls_bytecopy(out.data, 0, self.data, 0, a)
        c.__ls_bytecopy(out.data, a, other.data, 0, b)
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
        for (int t = 0; t < times; t = t + 1) {
            c.__ls_bytecopy(out.data, t * n, self.data, 0, n)
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
        while true {
            int p = c.__ls_str_find(self.data, n, needle.data, m, i)
            if p < 0 { break }
            total = total + 1
            i = p + m
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
        int m = old.len
        int n = self.len
        // Empty `old`: copy self unchanged.
        if m == 0 { return self.copy() }
        // Single-byte equal-length fast-path (cf. Rust's 1:1 byte replace): clone
        // self, then memchr-scan and overwrite each match IN PLACE — no rebuild,
        // no per-byte push. Result has the same length as self.
        if m == 1 && rep.len == 1 {
            Str out = self.copy()
            int rb = rep.data[0]
            int i = 0
            while true {
                int p = c.__ls_str_find(out.data, n, old.data, 1, i)
                if p < 0 { break }
                out.data[p] = rb as u8
                i = p + 1
            }
            return out
        }
        // General path: locate each match with the runtime scan, append the
        // run before it, then `rep`.
        *u8 z = nil
        Str out = Str { data: z, len: 0, cap: 0 }
        int i = 0
        while i < n {
            int p = c.__ls_str_find(self.data, n, old.data, m, i)
            if p < 0 {
                for (int k = i; k < n; k = k + 1) { out.push_byte(self.data[k]) }
                break
            }
            for (int k = i; k < p; k = k + 1) { out.push_byte(self.data[k]) }
            for (int j = 0; j < rep.len; j = j + 1) { out.push_byte(rep.data[j]) }
            i = p + m
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
        while true {
            int p = c.__ls_str_find(self.data, n, sep.data, sn, i)
            if p < 0 { break }
            out.push(self.substr(start, p - start))
            i = p + sn
            start = i
        }
        out.push(self.substr(start, n - start))
        return out
    }

    // Zero-copy split: return byte-offset views (StrSlice) instead of owned Str
    // parts, so a split of K pieces does NOT do K heap allocations — only the
    // Vec(StrSlice) buffer grows. Materialize the parts you actually need with
    // `slice_str`. Same boundary semantics as `split` (empty sep -> whole string;
    // trailing sep -> trailing empty view).
    fn split_view(&self, &Str sep) -> Vec(StrSlice) {
        Vec(StrSlice) out = {}
        int sn = sep.len
        int n = self.len
        if sn == 0 {
            out.push(StrSlice { off: 0, len: n })
            return out
        }
        int start = 0
        int i = 0
        while true {
            int p = c.__ls_str_find(self.data, n, sep.data, sn, i)
            if p < 0 { break }
            out.push(StrSlice { off: start, len: p - start })
            i = p + sn
            start = i
        }
        out.push(StrSlice { off: start, len: n - start })
        return out
    }

    // Materialize a view into an owned Str (the deferred copy). `s` is POD so it
    // is passed by value. Offsets are clamped by substr.
    fn slice_str(&self, StrSlice s) -> Str {
        return self.substr(s.off, s.len)
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
        // hex prefix (0x / 0X), parity with the old builtin
        if i + 1 < n && self.data[i] == 48 && (self.data[i + 1] == 120 || self.data[i + 1] == 88) {
            i = i + 2
            if i >= n { return Err("no hex digits") }
            int hv = 0
            while i < n {
                int hd = self.data[i]
                int dig = 0
                if hd >= 48 && hd <= 57 { dig = hd - 48 }
                else if hd >= 97 && hd <= 102 { dig = hd - 97 + 10 }
                else if hd >= 65 && hd <= 70 { dig = hd - 65 + 10 }
                else { return Err("invalid hex digit") }
                hv = hv * 16 + dig
                i = i + 1
            }
            if neg { hv = 0 - hv }
            return Ok(hv)
        }
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
        // hex prefix (0x / 0X), parity with the old builtin
        if i + 1 < n && self.data[i] == 48 && (self.data[i + 1] == 120 || self.data[i + 1] == 88) {
            i = i + 2
            if i >= n { return Err("no hex digits") }
            i64 hv = 0
            while i < n {
                int hd = self.data[i]
                int dig = 0
                if hd >= 48 && hd <= 57 { dig = hd - 48 }
                else if hd >= 97 && hd <= 102 { dig = hd - 97 + 10 }
                else if hd >= 65 && hd <= 70 { dig = hd - 65 + 10 }
                else { return Err("invalid hex digit") }
                hv = hv * 16 + (dig as i64)
                i = i + 1
            }
            if neg { hv = 0 - hv }
            return Ok(hv)
        }
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

    // Parse a decimal float: sign, integer part, optional '.fraction', optional
    // 'e'/'E' exponent (parity with the old builtin — JSON needs `1e6`).
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
            if d == 101 || d == 69 { break }      // 'e' | 'E'
            if d < 48 || d > 57 { return Err("invalid digit") }
            val = val * 10.0 + ((d - 48) as f64)
            any = true
            i = i + 1
        }
        if i < n && self.data[i] == 46 {
            i = i + 1
            f64 scale = 0.1
            while i < n {
                int d = self.data[i]
                if d == 101 || d == 69 { break }  // 'e' | 'E'
                if d < 48 || d > 57 { return Err("invalid digit") }
                val = val + ((d - 48) as f64) * scale
                scale = scale * 0.1
                any = true
                i = i + 1
            }
        }
        if !any { return Err("no digits") }
        if i < n {
            int ec = self.data[i]
            if ec != 101 && ec != 69 { return Err("invalid digit") }
            i = i + 1
            bool eneg = false
            if i < n {
                int sc = self.data[i]
                if sc == 45 { eneg = true  i = i + 1 }
                else if sc == 43 { i = i + 1 }
            }
            if i >= n { return Err("no digits") }
            int ev = 0
            while i < n {
                int d = self.data[i]
                if d < 48 || d > 57 { return Err("invalid digit") }
                ev = ev * 10 + (d - 48)
                i = i + 1
            }
            f64 p10 = 1.0
            for (int t = 0; t < ev; t = t + 1) { p10 = p10 * 10.0 }
            if eneg { val = val / p10 } else { val = val * p10 }
        }
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
        c.__ls_bytecopy(out.data, 0, self.data, 0, self.len)
        out.len = self.len
        return out
    }

    // Destructor: free the buffer only when we own it (cap > 0). Static strings
    // (cap == 0) point at .rodata and must not be freed.
    fn __drop() {
        if self.cap > 0 { c.free(self.data as *u8) }
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
        // One C call (runtime/builtins.c) over the raw buffer instead of a
        // per-byte LS fx_mix loop — same FxHash values, but the Map(Str,_) hot
        // path no longer pays 2.5M+ LS call frames. See alloc_analysis.md.
        return c.__ls_fxhash_bytes(self.data, self.len)
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
        c.__ls_bytecopy(out.data, 0, self.data, 0, self.len)
        c.__ls_bytecopy(out.data, self.len, rhs.data, 0, rhs.len)
        out.len = self.len + rhs.len
        return out
    }
}

// Operator `<` (trait Ord; `>`, `<=`, `>=` derive from `<` / `==`). Lexicographic
// byte order — delegates to `compare` (defined earlier in the main impl block).
// Lets `"a" < "b"` and `min_ord(Str)(...)` / `Vec(Str).sort()` work once P5-2
// flips the literal default to Str.
impl Ord for Str {
    fn <(&self, &Str rhs) -> bool {
        return self.compare(rhs) < 0
    }
}
