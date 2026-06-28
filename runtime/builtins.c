/* builtins.c — Runtime built-in function implementations.
   Linked into ls.exe (for JIT AbsoluteSymbols) and into AOT-compiled programs. */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <stdarg.h>

/* ---- f-string formatter (used by codegen) ----
   __ls_fstr_format(buf, size, fmt, ...) has snprintf semantics: writes at most
   size-1 chars + NUL, returns the FULL length the result needs (even if
   truncated). Codegen calls this rather than snprintf directly because on
   Windows/UCRT `snprintf` is a header inline with no JIT-resolvable symbol; this
   is a real exported symbol (AOT-linked via ls_os_backend, JIT-registered).

   Fast path: when the format contains only the integer/string conversions LS
   actually emits for plain interpolations (%d %i %u %lld %llu %s %c %%) — no
   float, width, precision or flags — we assemble the result with a hand-rolled
   itoa + memcpy, bypassing the CRT printf state machine (locale handling, format
   re-parsing). Anything else (%f, %.2f, %x, %p, padding, …) falls back to
   vsnprintf so behaviour is unchanged. */

/* Bound-aware append of `len` bytes; advances *total by len (snprintf counting,
   even past the buffer). Reserves buf[size-1] for the NUL. */
static void ls_fmt_put(char *buf, size_t size, size_t *total,
                       const char *src, size_t len) {
    for (size_t i = 0; i < len; i++) {
        size_t idx = *total + i;
        if (idx + 1 < size) buf[idx] = src[i];
    }
    *total += len;
}

/* unsigned -> decimal, written to out (no NUL); returns digit count. */
static int ls_u64_dec(unsigned long long v, char *out) {
    char tmp[20];
    int n = 0;
    if (v == 0) { out[0] = '0'; return 1; }
    while (v) { tmp[n++] = (char)('0' + (int)(v % 10)); v /= 10; }
    for (int i = 0; i < n; i++) out[i] = tmp[n - 1 - i];
    return n;
}

/* signed -> decimal (handles INT64_MIN safely); returns char count. */
static int ls_i64_dec(long long v, char *out) {
    if (v < 0) {
        out[0] = '-';
        unsigned long long uv = (unsigned long long)(-(v + 1)) + 1ULL;
        return 1 + ls_u64_dec(uv, out + 1);
    }
    return ls_u64_dec((unsigned long long)v, out);
}

/* Returns 1 if every conversion in fmt is one this fast path can handle. Must
   stay exactly in sync with the switch in ls_fast_vformat below. */
static int ls_fmt_is_simple(const char *fmt) {
    for (const char *p = fmt; *p; p++) {
        if (*p != '%') continue;
        char c = p[1];
        if (c == '%' || c == 'd' || c == 'i' || c == 'u' || c == 's' || c == 'c') {
            p++;  /* consume the conversion char */
        } else if (c == 'l' && p[2] == 'l' && (p[3] == 'd' || p[3] == 'u')) {
            p += 3;
        } else {
            return 0;  /* float / hex / pointer / width / precision / flags */
        }
    }
    return 1;
}

static int ls_fast_vformat(char *buf, size_t size, const char *fmt, va_list ap) {
    size_t total = 0;
    char num[24];
    const char *p = fmt;
    while (*p) {
        if (*p != '%') {
            const char *start = p;
            while (*p && *p != '%') p++;
            ls_fmt_put(buf, size, &total, start, (size_t)(p - start));
            continue;
        }
        /* *p == '%' */
        char c = p[1];
        if (c == '%') {
            ls_fmt_put(buf, size, &total, "%", 1);
            p += 2;
        } else if (c == 'd' || c == 'i') {
            int v = va_arg(ap, int);
            ls_fmt_put(buf, size, &total, num, (size_t)ls_i64_dec(v, num));
            p += 2;
        } else if (c == 'u') {
            unsigned v = va_arg(ap, unsigned);
            ls_fmt_put(buf, size, &total, num, (size_t)ls_u64_dec(v, num));
            p += 2;
        } else if (c == 's') {
            const char *s = va_arg(ap, const char *);
            if (s == NULL) s = "(null)";
            ls_fmt_put(buf, size, &total, s, strlen(s));
            p += 2;
        } else if (c == 'c') {
            int ch = va_arg(ap, int);
            char cc = (char)ch;
            ls_fmt_put(buf, size, &total, &cc, 1);
            p += 2;
        } else { /* c == 'l': %lld / %llu (guaranteed by ls_fmt_is_simple) */
            if (p[3] == 'd') {
                long long v = va_arg(ap, long long);
                ls_fmt_put(buf, size, &total, num, (size_t)ls_i64_dec(v, num));
            } else {
                unsigned long long v = va_arg(ap, unsigned long long);
                ls_fmt_put(buf, size, &total, num, (size_t)ls_u64_dec(v, num));
            }
            p += 4;
        }
    }
    if (size > 0) buf[total < size ? total : size - 1] = '\0';
    return (int)total;
}

int __ls_fstr_format(char *buf, size_t size, const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    int n;
    if (ls_fmt_is_simple(fmt))
        n = ls_fast_vformat(buf, size, fmt, ap);
    else
        n = vsnprintf(buf, size, fmt, ap);
    va_end(ap);
    return n;
}

/* __ls_str_skip_ws(data, len, start) -> int
   Scan forward from `start`, skipping ' ' '\t' '\n' '\r', return first
   non-ws position (or len if all ws). Single tight C loop — replaces the
   LS-level _skip_ws that called at()/at_unsafe() per character. */
int __ls_str_skip_ws(const char *data, int len, int start) {
    int i = start;
    while (i < len) {
        char c = data[i];
        if (c == ' ' || c == '\t' || c == '\n' || c == '\r')
            i++;
        else
            break;
    }
    return i;
}

/* __ls_str_scan_plain(data, len, start) -> int
   Scan forward from `start` until '"' or '\\' or end, return the position.
   Used by JSON string parser to bulk-skip unescaped chars. */
int __ls_str_scan_plain(const char *data, int len, int start) {
    int i = start;
    while (i < len) {
        char c = data[i];
        if (c == '"' || c == '\\') break;
        i++;
    }
    return i;
}

/* __ls_str_scan_digits(data, len, start) -> int
   Scan forward from `start` while chars are '0'-'9', return first non-digit pos. */
int __ls_str_scan_digits(const char *data, int len, int start) {
    int i = start;
    while (i < len && data[i] >= '0' && data[i] <= '9') i++;
    return i;
}

/* __ls_str_find(hay, hlen, needle, nlen, start) -> int
   Byte offset of the first occurrence of `needle` in `hay` at or after `start`,
   or -1 if absent. Empty needle -> `start` (clamped to hlen). Works on raw
   ptr+len buffers (NO NUL termination assumed, embedded zeros allowed), so it is
   safe to call from a read-only `&self` / `&Str` without c_str(). Backs std.str's
   find / contains? / count / replace / split sep-scan.

   Strategy is size-adaptive (mirrors glibc / CPython fastsearch — the right
   algorithm depends on the input, no single one wins everywhere):

     * nlen == 1            -> memchr (SIMD, unbeatable for a single byte).
     * hlen < SHORT_HAY     -> memchr first-byte locate + memcmp verify, NO
                               preprocessing. For the common case of many
                               searches over small strings, building a 256-entry
                               skip table every call would cost more than it saves
                               (this is glibc's short-needle strategy).
     * otherwise            -> Boyer-Moore-Horspool / Sunday Quick-Search: memchr
                               still does the SIMD first-byte locate, but on a
                               verify miss we skip by the Sunday bad-character
                               shift (the byte one past the window) instead of +1.
                               On long / repetitive / common-first-byte text this
                               avoids the O(n*m) blow-up that plain memchr+memcmp
                               degrades to. */
#define LS_STRFIND_SHORT_HAY 256

int __ls_str_find(const char *hay, int hlen, const char *needle, int nlen, int start) {
    if (start < 0) start = 0;
    if (nlen == 0) return start <= hlen ? start : -1;
    if (nlen > hlen) return -1;
    unsigned char first = (unsigned char)needle[0];
    if (nlen == 1) {
        const char *p = memchr(hay + start, first, (size_t)(hlen - start));
        return p ? (int)(p - hay) : -1;
    }
    int last = hlen - nlen;  /* greatest valid start index */

    /* Short haystack: SIMD locate + verify, no skip-table preprocessing. */
    if (hlen < LS_STRFIND_SHORT_HAY) {
        int i = start;
        while (i <= last) {
            const char *p = memchr(hay + i, first, (size_t)(last - i + 1));
            if (!p) return -1;
            i = (int)(p - hay);
            if (memcmp(hay + i + 1, needle + 1, (size_t)(nlen - 1)) == 0) return i;
            i++;
        }
        return -1;
    }

    /* Long haystack: Sunday Quick-Search bad-character table. qs[c] = shift to
       apply, keyed on the byte one PAST the current window; a byte absent from
       the needle skips the whole window + 1. Shifts are in [1, nlen+1], so `i`
       strictly increases — no infinite loop. Combining the table with a
       memchr-forward locate is safe: memchr only moves to the next first-byte
       candidate, and the Sunday shift provably skips no real match. */
    int qs[256];
    for (int k = 0; k < 256; k++) qs[k] = nlen + 1;
    for (int k = 0; k < nlen; k++) qs[(unsigned char)needle[k]] = nlen - k;

    int i = start;
    while (i <= last) {
        const char *p = memchr(hay + i, first, (size_t)(last - i + 1));
        if (!p) return -1;
        i = (int)(p - hay);
        if (memcmp(hay + i + 1, needle + 1, (size_t)(nlen - 1)) == 0) return i;
        int nxt = i + nlen;             /* Sunday character (one past window) */
        if (nxt >= hlen) return -1;     /* window already at the tail */
        i += qs[(unsigned char)hay[nxt]];
    }
    return -1;
}

/* __ls_bytecopy(dst, doff, src, soff, n) — copy `n` bytes from src+soff to
   dst+doff. A single memcpy (SIMD-accelerated in the CRT) replacing std.str's
   per-byte copy loops (substr / concat / __clone / reserve copy-on-grow /
   push_str). Takes byte offsets because LS has no pointer arithmetic, so callers
   can't form `&buf[off]` themselves. n <= 0 is a no-op (src/dst may be nil). */
void __ls_bytecopy(void *dst, int doff, const void *src, int soff, int n) {
    if (n <= 0) return;
    memcpy((char *)dst + doff, (const char *)src + soff, (size_t)n);
}

/* __ls_ptr_at(base, off) -> base + off (byte offset). The pointer-arithmetic
   primitive LS lacks: a region/arena sub-allocates a typed `*T` slice from one
   `*u8` block by computing `__ls_ptr_at(base, off) as *T`. Pure address math —
   no read/write, no bounds check (the arena tracks its own offset). off may be
   any byte offset the caller has reserved; base must be non-nil. */
void *__ls_ptr_at(void *base, long long off) {
    return (char *)base + off;
}


/* __ls_fxhash_bytes(data, len) -> u64  — byte-wise FxHash over a raw ptr+len
   buffer. Bit-identical to std.str's `impl Hash for Str` (the LS loop
   h = rotate_left(h ^ byte, 5) * 0x517cc1b727220a95, one byte per word): this
   replaces 2.5M+ per-byte LS `fx_mix` calls in the Map(Str,_) hot path with a
   single C call. SAME hash values by construction, so Map bucket layout /
   iteration order are unchanged. SEED is rustc FxHasher's multiplier. No NUL
   assumed; len <= 0 returns 0 (data may be nil for an empty Str). */
unsigned long long __ls_fxhash_bytes(const char *data, int len) {
    unsigned long long h = 0;
    const unsigned long long SEED = 0x517cc1b727220a95ULL;
    for (int i = 0; i < len; i++) {
        unsigned long long x = h ^ (unsigned long long)(unsigned char)data[i];
        unsigned long long r = (x << 5) | (x >> 59);
        h = r * SEED;
    }
    return h;
}

/* Byte-buffer integer loads for std.bytes (V2 bit-pattern parsing). Assemble an
   N-byte big/little-endian integer from p+off via byte shifts — this is HOST-
   ENDIAN INDEPENDENT (no `*(uint32_t*)` cast, no conditional bswap, no alignment
   requirement): the same code is correct on LE and BE hosts. All return uint64_t
   (the value zero-extended) so the FFI return ABI is uniform; the LS wrapper casts
   down to u16/u32 as needed. Bounds checking is the caller's job (std.bytes.Reader
   validates pos+N <= len before calling these). */
unsigned long long __ls_load_u8(const unsigned char *p, long long off) {
    return (unsigned long long)p[off];
}
unsigned long long __ls_load_be_u16(const unsigned char *p, long long off) {
    return ((unsigned long long)p[off] << 8) | (unsigned long long)p[off + 1];
}
unsigned long long __ls_load_be_u32(const unsigned char *p, long long off) {
    return ((unsigned long long)p[off]     << 24) | ((unsigned long long)p[off + 1] << 16) |
           ((unsigned long long)p[off + 2] <<  8) |  (unsigned long long)p[off + 3];
}
unsigned long long __ls_load_be_u64(const unsigned char *p, long long off) {
    return ((unsigned long long)p[off]     << 56) | ((unsigned long long)p[off + 1] << 48) |
           ((unsigned long long)p[off + 2] << 40) | ((unsigned long long)p[off + 3] << 32) |
           ((unsigned long long)p[off + 4] << 24) | ((unsigned long long)p[off + 5] << 16) |
           ((unsigned long long)p[off + 6] <<  8) |  (unsigned long long)p[off + 7];
}
unsigned long long __ls_load_le_u16(const unsigned char *p, long long off) {
    return (unsigned long long)p[off] | ((unsigned long long)p[off + 1] << 8);
}
unsigned long long __ls_load_le_u32(const unsigned char *p, long long off) {
    return  (unsigned long long)p[off]            | ((unsigned long long)p[off + 1] <<  8) |
           ((unsigned long long)p[off + 2] << 16) | ((unsigned long long)p[off + 3] << 24);
}
unsigned long long __ls_load_le_u64(const unsigned char *p, long long off) {
    return  (unsigned long long)p[off]            | ((unsigned long long)p[off + 1] <<  8) |
           ((unsigned long long)p[off + 2] << 16) | ((unsigned long long)p[off + 3] << 24) |
           ((unsigned long long)p[off + 4] << 32) | ((unsigned long long)p[off + 5] << 40) |
           ((unsigned long long)p[off + 6] << 48) | ((unsigned long long)p[off + 7] << 56);
}

/* Flush all CRT output streams. Codegen injects a call to this before every
   `ret` in main so buffered stdout/stderr is written WHILE this translation unit's
   CRT is still live — not left to the process-teardown path. On Windows the AOT
   exe links both msvcrt.dll and ucrtbase.dll (see docs/crt_mismatch_bug.md); when
   stdout is redirected to a file/pipe it is fully buffered, and the CRT that runs
   the exit-time flush can differ from the one holding printf/puts' buffer, so ~15%
   of runs lost ALL output (rc=0, empty stdout) intermittently. fflush lives in the
   same TU as ls_print/printf, so it always targets the buffer those writes filled. */
void __ls_flush_out(void) {
    fflush(NULL);
}

/* Expose the CRT's stdout/stderr FILE* to LS (std.core.sink redirect). Returned
   as void* (LS `object`) and handed straight back to c.fwrite. Same-TU CRT as
   ls_print/printf, so the FILE* matches the streams those writes target. */
void *__ls_stdout(void) {
    return (void *)stdout;
}

void *__ls_stderr(void) {
    return (void *)stderr;
}

/* ---- print() redirect (std.core.sink) ----
   g_sink_stream is the destination print() writes to (NULL == stdout). set_sink
   pokes it via __ls_sink_set; print()'s codegen emits __ls_printf, which forwards
   to it. Default NULL => stdout, so unredirected print is byte-identical. */
static FILE *g_sink_stream = NULL;   /* current destination (NULL = stdout) */
static FILE *g_sink_owned  = NULL;   /* fclose on the next switch (a redirect file) */

FILE *__ls_sink_stream(void) {
    return g_sink_stream ? g_sink_stream : stdout;
}

/* Set the print destination. `owned` => this is a file we must fclose on the
   next switch (and the CRT closes at exit). Closes the previously-owned file
   first (the single fclose of that handle — no double close, the source File's
   destructor was nil'd by io.file). */
void __ls_sink_set(void *fp, int owned) {
    if (g_sink_owned != NULL && g_sink_owned != (FILE *)fp) {
        fclose(g_sink_owned);
    }
    g_sink_stream = (FILE *)fp;
    g_sink_owned  = owned ? (FILE *)fp : NULL;
}

/* print()'s output primitive: vfprintf to the current sink stream. */
int __ls_printf(const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    int n = vfprintf(g_sink_stream ? g_sink_stream : stdout, fmt, ap);
    va_end(ap);
    return n;
}

/* print(string) — print a string followed by newline */
void ls_print(const char *s) {
    if (s) {
        puts(s);
    } else {
        puts("(nil)");
    }
}

/* print_int(int) — print an integer */
void ls_print_int(int value) {
    printf("%d\n", value);
}

/* print_f64(double) — print a float */
void ls_print_f64(double value) {
    printf("%g\n", value);
}

/* print_bool(int) — print a boolean */
void ls_print_bool(int value) {
    printf("%s\n", value ? "true" : "false");
}

/* ---- process args (set by `ls run` before calling the script's main) ---- */

static int    g_argc = 0;
static char **g_argv = NULL;

void __ls_set_args(int argc, char **argv) {
    g_argc = argc;
    g_argv = argv;
}

int __ls_get_argc(void) {
    return g_argc;
}

void *__ls_get_argv(int i) {
    if (i < 0 || i >= g_argc || g_argv == NULL) return NULL;
    return (void *)g_argv[i];
}

void __ls_proc_exit(int code) {
    /* Flush BEFORE exit: the panic/abort path (vec OOB, unwrap None, abort())
       diverges and never reaches main's ret, so the codegen-injected
       __ls_flush_out there is bypassed. exit()'s own atexit stdio flush proved
       unreliable here (~10% of runs lost buffered stdout — including the panic
       diagnostic itself), so flush explicitly while this TU's CRT is live. */
    fflush(NULL);
    exit(code);
}

/* ---- stdin readline ---- */

static char  *g_readline_buf = NULL;
static size_t g_readline_len = 0;
static int    g_readline_ok  = 0;

void __ls_readline_exec(void) {
    g_readline_ok = 0;
    g_readline_len = 0;
    if (g_readline_buf) { free(g_readline_buf); g_readline_buf = NULL; }
    size_t cap = 256;
    g_readline_buf = (char *)malloc(cap);
    if (!g_readline_buf) return;
    size_t pos = 0;
    int c;
    while ((c = getchar()) != EOF && c != '\n') {
        if (pos + 1 >= cap) {
            cap *= 2;
            char *nb = (char *)realloc(g_readline_buf, cap);
            if (!nb) { free(g_readline_buf); g_readline_buf = NULL; return; }
            g_readline_buf = nb;
        }
        g_readline_buf[pos++] = (char)c;
    }
    if (c == EOF && pos == 0) { free(g_readline_buf); g_readline_buf = NULL; return; }
    g_readline_buf[pos] = '\0';
    g_readline_len = pos;
    g_readline_ok = 1;
}

int __ls_readline_ok(void)       { return g_readline_ok; }
long long __ls_readline_len(void) { return (long long)g_readline_len; }

/* Non-owning peek: the buffer stays owned by the runtime (freed by the next
   __ls_readline_exec). Callers that copy (io.read_line via from_cstr) use this
   instead of _take so the LS side never frees a runtime-malloc'd pointer —
   under --memcheck such a free is untracked and reports INVALID FREE. */
void *__ls_readline_ptr(void) { return g_readline_buf; }

void *__ls_readline_take(void) {
    void *p = g_readline_buf;
    g_readline_buf = NULL;
    g_readline_ok  = 0;
    g_readline_len = 0;
    return p;
}

/* ---- float_fixed helpers (std/strconv.ls: float_fixed) ---- */
/* Formats a double with N decimal places into a global static buffer.
   Not thread-safe; suitable for single-threaded LS runtime. */

static char g_ffix_buf[64];
static int  g_ffix_len = 0;

void __ls_float_fixed_exec(double val, int digits) {
    if (digits < 0)  digits = 0;
    if (digits > 20) digits = 20;
    g_ffix_len = snprintf(g_ffix_buf, sizeof(g_ffix_buf), "%.*f", digits, val);
    if (g_ffix_len < 0 || g_ffix_len >= (int)sizeof(g_ffix_buf))
        g_ffix_len = 0;
}

/* Returns a pointer to the static buffer (NOT heap-allocated, caller must NOT free).
   Caller should copy the contents (e.g. via LS from_cstr) before calling again. */
void *__ls_float_fixed_ptr(void) { return (void *)g_ffix_buf; }
