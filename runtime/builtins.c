/* builtins.c — Runtime built-in function implementations.
   Linked into ls.exe (for JIT AbsoluteSymbols) and into AOT-compiled programs. */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

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
