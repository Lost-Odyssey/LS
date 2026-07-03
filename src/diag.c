/* diag.c — Diagnostic sink: text renderer (default) with source snippet,
   caret line, and VT colors (C2-1, docs/plan_diagnostics_v2.md §3.2). */
#include "diag.h"
#include "common.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>

#ifdef _WIN32
#include <io.h>
#include <windows.h>
#define ls_isatty _isatty
#define ls_fileno _fileno
#else
#include <unistd.h>
#define ls_isatty isatty
#define ls_fileno fileno
#endif

/* ---- VT color support ----
   Colors are used only when stderr is a terminal. On Windows 10+ the console
   needs ENABLE_VIRTUAL_TERMINAL_PROCESSING switched on once; if that fails
   (old console) we stay colorless. Redirected stderr (!isatty) is colorless. */

static int g_color_state = -1; /* -1 = not probed, 0 = off, 1 = on */

static bool diag_color_enabled(void)
{
    if (g_color_state >= 0)
        return g_color_state == 1;
    g_color_state = 0;
    if (!ls_isatty(ls_fileno(stderr)))
        return false;
#ifdef _WIN32
    HANDLE h = GetStdHandle(STD_ERROR_HANDLE);
    DWORD mode = 0;
    if (h == INVALID_HANDLE_VALUE || !GetConsoleMode(h, &mode))
        return false;
    if (!(mode & ENABLE_VIRTUAL_TERMINAL_PROCESSING) &&
        !SetConsoleMode(h, mode | ENABLE_VIRTUAL_TERMINAL_PROCESSING))
        return false;
#endif
    g_color_state = 1;
    return true;
}

/* ---- Source line cache ----
   Lazily reads the diagnosed file once and indexes line starts. Single slot:
   diagnostics arrive grouped by file, and this is the error path — perf is
   irrelevant. Unreadable files (REPL synthetic sources) are remembered as
   failed so we silently fall back to the one-line format without re-probing. */

typedef struct {
    char *path;     /* owned; NULL = empty slot */
    char *src;      /* owned file contents (NUL-terminated) */
    long *line_off; /* owned; byte offset of each line start */
    int   line_count;
    bool  failed;   /* path known unreadable */
} SrcCache;

static SrcCache g_src;

static void src_cache_load(const char *path)
{
    free(g_src.path);
    free(g_src.src);
    free(g_src.line_off);
    memset(&g_src, 0, sizeof(g_src));

    g_src.path = (char *)malloc_safe(strlen(path) + 1);
    strcpy(g_src.path, path);

    FILE *f = fopen(path, "rb");
    if (f == NULL) { g_src.failed = true; return; }
    fseek(f, 0, SEEK_END);
    long size = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (size < 0) { fclose(f); g_src.failed = true; return; }
    g_src.src = (char *)malloc_safe((size_t)size + 1);
    size_t got = fread(g_src.src, 1, (size_t)size, f);
    fclose(f);
    g_src.src[got] = '\0';

    /* Index line starts. */
    int cap = 128, n = 0;
    long *off = (long *)malloc_safe(sizeof(long) * (size_t)cap);
    off[n++] = 0;
    for (size_t i = 0; i < got; i++) {
        if (g_src.src[i] == '\n') {
            if (n == cap) {
                cap *= 2;
                off = (long *)realloc_safe(off, sizeof(long) * (size_t)cap);
            }
            off[n++] = (long)i + 1;
        }
    }
    g_src.line_off = off;
    g_src.line_count = n;
}

/* Returns a pointer to the start of 1-based `line` and its length (without
   the trailing newline), or NULL when the file/line is unavailable. */
static const char *diag_source_line(const char *path, int line, int *out_len)
{
    if (path == NULL || line < 1)
        return NULL;
    if (g_src.path == NULL || strcmp(g_src.path, path) != 0)
        src_cache_load(path);
    if (g_src.failed || line > g_src.line_count)
        return NULL;
    const char *s = g_src.src + g_src.line_off[line - 1];
    const char *e = strchr(s, '\n');
    size_t len = e ? (size_t)(e - s) : strlen(s);
    while (len > 0 && s[len - 1] == '\r')
        len--;
    *out_len = (int)len;
    return s;
}

/* ---- did-you-mean (C2-2) ---- */

#define DIAG_SUGG_MAX_NAME 64

/* Optimal-string-alignment Damerau-Levenshtein distance between a and b,
   early-outing to max+1 when the distance provably exceeds `max`. */
static int diag_osa_distance(const char *a, const char *b, int max)
{
    int la = (int)strlen(a), lb = (int)strlen(b);
    int diff = la > lb ? la - lb : lb - la;
    if (diff > max)
        return max + 1;
    if (la > DIAG_SUGG_MAX_NAME || lb > DIAG_SUGG_MAX_NAME)
        return max + 1;

    /* Rolling three rows (OSA needs row i-2 for transpositions). */
    int rows[3][DIAG_SUGG_MAX_NAME + 1];
    int *prev2 = rows[0], *prev = rows[1], *cur = rows[2];
    for (int j = 0; j <= lb; j++)
        prev[j] = j;
    for (int i = 1; i <= la; i++) {
        cur[0] = i;
        int row_min = cur[0];
        for (int j = 1; j <= lb; j++) {
            int cost = (a[i - 1] == b[j - 1]) ? 0 : 1;
            int d = prev[j] + 1;                     /* deletion */
            if (cur[j - 1] + 1 < d) d = cur[j - 1] + 1;      /* insertion */
            if (prev[j - 1] + cost < d) d = prev[j - 1] + cost; /* subst */
            if (i > 1 && j > 1 &&
                a[i - 1] == b[j - 2] && a[i - 2] == b[j - 1] &&
                prev2[j - 2] + 1 < d)
                d = prev2[j - 2] + 1;                /* transposition */
            cur[j] = d;
            if (d < row_min) row_min = d;
        }
        if (row_min > max)
            return max + 1;
        int *t = prev2; prev2 = prev; prev = cur; cur = t;
    }
    return prev[lb];
}

const char *diag_suggest(const char *bad, DiagCandidateFn next, void *ctx)
{
    int blen = (int)strlen(bad);
    int limit = blen / 3;
    if (limit > 2) limit = 2;
    if (limit <= 0)
        return NULL; /* names under 3 chars: any suggestion is a coin flip */

    const char *best = NULL;
    int best_d = limit + 1;
    bool tie = false;
    const char *cand;
    while ((cand = next(ctx)) != NULL) {
        if (cand[0] == '\0' || strcmp(cand, bad) == 0)
            continue;
        int d = diag_osa_distance(bad, cand, limit);
        if (d > limit)
            continue;
        if (d < best_d) {
            best_d = d;
            best = cand;
            tie = false;
        } else if (d == best_d && strcmp(cand, best) != 0) {
            tie = true;
        }
    }
    return tie ? NULL : best;
}

/* ---- Text renderer (default sink) ---- */

static const char *diag_prefix(DiagKind kind)
{
    switch (kind) {
    case DIAG_TYPE_ERROR:  return "[type error]";
    case DIAG_MOVE_ERROR:  return "[move error]";
    case DIAG_WARNING:     return "[warning]";
    case DIAG_PARSE_ERROR:
    case DIAG_SCAN_ERROR:  return "[error]";
    }
    return "[error]";
}

static void text_sink_emit(DiagSink *self, const Diagnostic *d)
{
    (void)self;
    bool color = diag_color_enabled();
    const char *c_pre   = !color ? ""
                        : d->kind == DIAG_WARNING ? "\x1b[33m" : "\x1b[31m";
    const char *c_caret = color ? "\x1b[36m" : "";
    const char *c_rst   = color ? "\x1b[0m"  : "";

    fprintf(stderr, "%s%s%s %s:%d:%d: %s\n",
            c_pre, diag_prefix(d->kind), c_rst,
            d->file ? d->file : "<unknown>",
            d->line, d->col, d->message);

    int slen = 0;
    const char *sline = diag_source_line(d->file, d->line, &slen);
    if (sline && d->col >= 1) {
        char numbuf[16];
        int w = snprintf(numbuf, sizeof(numbuf), "%5d", d->line);
        fprintf(stderr, "%s | %.*s\n", numbuf, slen, sline);
        fprintf(stderr, "%*s | ", w, "");
        /* Re-emit the prefix chars as whitespace, preserving tabs so the
           caret stays aligned under tab-indented code. */
        for (int i = 0; i < d->col - 1 && i < slen; i++)
            fputc(sline[i] == '\t' ? '\t' : ' ', stderr);
        int squiggle = d->len - 1;
        int remain = slen - d->col; /* line chars after the caret column */
        if (squiggle > remain) squiggle = remain > 0 ? remain : 0;
        fprintf(stderr, "%s^", c_caret);
        for (int i = 0; i < squiggle; i++)
            fputc('~', stderr);
        fprintf(stderr, "%s\n", c_rst);
    }
    if (d->help[0])
        fprintf(stderr, "   help: %s\n", d->help);
}

static DiagSink g_text_sink = { text_sink_emit, NULL };
static DiagSink *g_current_sink = &g_text_sink;

/* ---- Sink plumbing ---- */

DiagSink *diag_current_sink(void)
{
    return g_current_sink;
}

void diag_set_sink(DiagSink *sink)
{
    g_current_sink = sink ? sink : &g_text_sink;
}

void diag_emit(DiagSink *sink, const Diagnostic *d)
{
    if (sink == NULL) sink = g_current_sink;
    sink->emit(sink, d);
}

void diag_vemitf(DiagKind kind, const char *file, int line, int col, int len,
                 const char *help, const char *fmt, va_list args)
{
    Diagnostic d;
    d.kind = kind;
    d.file = file;
    d.line = line;
    d.col = col;
    d.len = len > 0 ? len : 1;
    vsnprintf(d.message, sizeof(d.message), fmt, args);
    if (help && help[0])
        snprintf(d.help, sizeof(d.help), "%s", help);
    else
        d.help[0] = '\0';
    diag_emit(g_current_sink, &d);
}

void diag_emitf(DiagKind kind, const char *file, int line, int col, int len,
                const char *help, const char *fmt, ...)
{
    va_list args;
    va_start(args, fmt);
    diag_vemitf(kind, file, line, col, len, help, fmt, args);
    va_end(args);
}
