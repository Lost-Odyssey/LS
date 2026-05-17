/* profiler.c — LS instrumented function-level profiler.
 *
 * Activation: ls run --profile / ls compile --profile
 * Injected enter/leave calls surround every user function.
 * At program exit (or explicit ls_prof_report call), prints a sorted table.
 *
 * Design mirrors memcheck.c:
 *  - ls_prof_enter(fn_name, file, line) — called at function entry
 *  - ls_prof_leave()                    — called before every ret
 *  - ls_prof_report()                   — print sorted profile
 *
 * Timing uses ls_os_perf_now() (QueryPerformanceCounter / clock_gettime).
 * NOT thread-safe (LS has no threads in v1).
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

/* Windows .exe symbols need explicit export so the JIT's process-symbol
   resolver (GetProcAddress) can find them. */
#ifdef _WIN32
#  define LS_PROF_EXPORT __declspec(dllexport)
#else
#  define LS_PROF_EXPORT __attribute__((visibility("default")))
#endif

/* Forward-declare ls_os_perf_now — implemented in os_win32.c / os_posix.c. */
extern long long ls_os_perf_now(void);

/* =========================================================================
 * Per-function stats table (open-addressing hash, key = fn_name string ptr)
 * ========================================================================= */

typedef struct {
    const char *fn_name;  /* NULL = empty slot */
    const char *file;
    int         line;
    uint64_t    total_ns;     /* total wall time including callees */
    uint64_t    self_ns;      /* total_ns minus time in direct callees */
    uint64_t    call_count;
} LsProfEntry;

#define PROF_TABLE_INIT_CAP 128
#define PROF_TOMBSTONE      ((const char *)1)

static LsProfEntry *g_table   = NULL;
static size_t       g_cap     = 0;
static size_t       g_count   = 0;   /* live entries */
static int          g_initialized = 0;
static int          g_reported = 0;

/* Hash by fn_name pointer address (stable per compiled module). */
static size_t prof_hash(const char *p) {
    uintptr_t v = (uintptr_t)p;
    v ^= v >> 16;
    v *= 0x45d9f3bULL;
    v ^= v >> 16;
    return (size_t)v;
}

static LsProfEntry *prof_find_or_insert(const char *fn_name, const char *file, int line) {
    if (g_count * 2 >= g_cap) {
        /* Resize */
        size_t new_cap = (g_cap == 0) ? PROF_TABLE_INIT_CAP : g_cap * 2;
        LsProfEntry *new_t = (LsProfEntry *)calloc(new_cap, sizeof(LsProfEntry));
        if (!new_t) return NULL;
        for (size_t i = 0; i < g_cap; i++) {
            LsProfEntry *e = &g_table[i];
            if (!e->fn_name || e->fn_name == PROF_TOMBSTONE) continue;
            size_t idx = prof_hash(e->fn_name) & (new_cap - 1);
            while (new_t[idx].fn_name && new_t[idx].fn_name != PROF_TOMBSTONE)
                idx = (idx + 1) & (new_cap - 1);
            new_t[idx] = *e;
        }
        free(g_table);
        g_table = new_t;
        g_cap = new_cap;
    }
    size_t idx = prof_hash(fn_name) & (g_cap - 1);
    while (g_table[idx].fn_name && g_table[idx].fn_name != PROF_TOMBSTONE &&
           g_table[idx].fn_name != fn_name) {
        /* Different pointer — check if same string (e.g. JIT recompile) */
        if (strcmp(g_table[idx].fn_name, fn_name) == 0)
            return &g_table[idx];
        idx = (idx + 1) & (g_cap - 1);
    }
    if (!g_table[idx].fn_name || g_table[idx].fn_name == PROF_TOMBSTONE) {
        /* New entry */
        g_table[idx].fn_name   = fn_name;
        g_table[idx].file      = file;
        g_table[idx].line      = line;
        g_table[idx].total_ns  = 0;
        g_table[idx].self_ns   = 0;
        g_table[idx].call_count = 0;
        g_count++;
    }
    return &g_table[idx];
}

/* =========================================================================
 * Call-stack for self_ns computation
 * ========================================================================= */

#define PROF_FRAME_MAX 512

typedef struct {
    const char   *fn_name;
    const char   *file;
    int           line;
    long long     enter_ns;   /* ls_os_perf_now() at entry */
    long long     child_ns;   /* accumulated time spent in direct callees */
} LsProfFrame;

static LsProfFrame g_frames[PROF_FRAME_MAX];
static int         g_frame_top = 0;   /* index of current top + 1 */

LS_PROF_EXPORT void ls_prof_report(void);   /* forward */

static void prof_init(void) {
    if (g_initialized) return;
    g_initialized = 1;
    g_cap   = PROF_TABLE_INIT_CAP;
    g_table = (LsProfEntry *)calloc(g_cap, sizeof(LsProfEntry));
    atexit(ls_prof_report);   /* AOT: automatically print at process exit */
}

/* =========================================================================
 * Public API — called from instrumented IR
 * ========================================================================= */

LS_PROF_EXPORT void ls_prof_enter(const char *fn_name, const char *file, int line) {
    if (!g_initialized) prof_init();

    if (g_frame_top < PROF_FRAME_MAX) {
        LsProfFrame *f = &g_frames[g_frame_top];
        f->fn_name  = fn_name;
        f->file     = file;
        f->line     = line;
        f->enter_ns = ls_os_perf_now();
        f->child_ns = 0;
    }
    g_frame_top++;
}

LS_PROF_EXPORT void ls_prof_leave(void) {
    if (g_frame_top <= 0) return;
    g_frame_top--;

    if (g_frame_top >= PROF_FRAME_MAX) return;   /* was overflowed */
    LsProfFrame *f = &g_frames[g_frame_top];

    long long now = ls_os_perf_now();
    long long elapsed = now - f->enter_ns;
    if (elapsed < 0) elapsed = 0;

    LsProfEntry *e = prof_find_or_insert(f->fn_name, f->file, f->line);
    if (e) {
        e->total_ns  += (uint64_t)elapsed;
        /* self = elapsed - time spent in direct callees */
        long long self = elapsed - f->child_ns;
        if (self < 0) self = 0;
        e->self_ns   += (uint64_t)self;
        e->call_count++;
    }

    /* Attribute elapsed to parent's child_ns */
    if (g_frame_top > 0 && g_frame_top - 1 < PROF_FRAME_MAX)
        g_frames[g_frame_top - 1].child_ns += elapsed;
}

/* =========================================================================
 * Report — qsort comparator + output
 * ========================================================================= */

static int prof_cmp_total(const void *a, const void *b) {
    const LsProfEntry *ea = (const LsProfEntry *)a;
    const LsProfEntry *eb = (const LsProfEntry *)b;
    if (eb->total_ns > ea->total_ns) return  1;
    if (eb->total_ns < ea->total_ns) return -1;
    return 0;
}

LS_PROF_EXPORT void ls_prof_report(void) {
    if (g_reported) return;
    g_reported = 1;

    fflush(stdout);

    if (!g_table || g_count == 0) {
        fprintf(stderr, "\n=== LS Profile Report ===\n");
        fprintf(stderr, "  (no functions profiled)\n");
        return;
    }

    /* Collect live entries into a temp array for sorting */
    LsProfEntry *arr = (LsProfEntry *)malloc(g_count * sizeof(LsProfEntry));
    if (!arr) return;
    size_t n = 0;
    for (size_t i = 0; i < g_cap; i++) {
        if (g_table[i].fn_name && g_table[i].fn_name != PROF_TOMBSTONE)
            arr[n++] = g_table[i];
    }
    qsort(arr, n, sizeof(LsProfEntry), prof_cmp_total);

    fprintf(stderr, "\n=== LS Profile Report ===\n");
    fprintf(stderr, "  %8s  %12s  %10s  %6s  %s\n",
            "calls", "total ms", "self ms", "self%", "function");
    fprintf(stderr, "  %8s  %12s  %10s  %6s  %s\n",
            "------", "--------", "-------", "-----", "--------");

    size_t show = (n > 20) ? 20 : n;
    uint64_t total_all = 0;
    for (size_t i = 0; i < n; i++) total_all += arr[i].self_ns;

    for (size_t i = 0; i < show; i++) {
        LsProfEntry *e = &arr[i];
        double total_ms = (double)e->total_ns / 1.0e6;
        double self_ms  = (double)e->self_ns  / 1.0e6;
        double pct = (total_all > 0)
                   ? (100.0 * (double)e->self_ns / (double)total_all)
                   : 0.0;
        const char *file = e->file ? e->file : "?";
        fprintf(stderr, "  %8llu  %12.3f  %10.3f  %5.1f%%  %s  (%s:%d)\n",
                (unsigned long long)e->call_count,
                total_ms, self_ms, pct,
                e->fn_name, file, e->line);
    }
    if (n > 20)
        fprintf(stderr, "  ... %zu more (use ls_prof_report_all for full list)\n",
                n - 20);
    fprintf(stderr, "=== end profile ===\n");
    free(arr);
}
