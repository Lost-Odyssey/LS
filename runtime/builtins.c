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
