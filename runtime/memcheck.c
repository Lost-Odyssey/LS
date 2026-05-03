/* memcheck.c — LS-aware memory leak / double-free detector.
 *
 * Linked into ls.exe so JIT-mode programs see ls_mc_alloc / ls_mc_free
 * via process symbol resolution. AOT support (Phase C) will install this
 * as a separate static library.
 *
 * Activation: codegen replaces every malloc/free with ls_mc_alloc / ls_mc_free
 * when ctx->memcheck_enabled == true. ls_mc_report runs at program exit
 * (registered via atexit on first alloc).
 *
 * NOT thread-safe (LS has no threads in v1).
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

/* On Windows, .exe symbols aren't visible to GetProcAddress unless exported.
   The JIT's process symbol generator uses GetProcAddress, so we must export
   ls_mc_alloc / ls_mc_free explicitly. */
#ifdef _WIN32
#  define LS_MC_EXPORT __declspec(dllexport)
#else
#  define LS_MC_EXPORT __attribute__((visibility("default")))
#endif

/* Public site descriptor — codegen emits a private constant of this layout
   for every alloc/free call site. */
typedef struct {
    const char *file;
    int         line;
    int         col;
    const char *kind;   /* e.g. "string.upper" / "vec.grow" / "io.slurp" */
} LsMcSite;

/* Phase D.1 — call-stack frame.
   Each user function in --memcheck mode emits an ls_mc_enter at entry and
   ls_mc_leave before each ret. Frames live on a global stack (LS is single-
   threaded in v1; if/when threading lands this becomes TLS). The frame
   stack snapshot is captured at every alloc so leak reports can show the
   chain of LS function calls that led to the allocation. */
typedef struct {
    const char *fn_name;     /* e.g. "build_tree" / "string.upper" */
    const char *file;        /* file of the function definition */
    int         call_line;   /* line in the function where the alloc/call lives */
} LsMcFrame;

#define LS_MC_FRAME_STACK_MAX 256   /* max LS recursion depth tracked */
#define LS_MC_BACKTRACE_MAX   8     /* frames captured per allocation */

typedef struct {
    void           *ptr;        /* NULL = empty slot; (void*)1 = tombstone */
    size_t          size;
    const LsMcSite *alloc_site;
    const LsMcSite *free_site;  /* set after free, for double-free diagnostics */
    int             freed;      /* 0 = live, 1 = freed */
    /* D.1 — innermost LS_MC_BACKTRACE_MAX frames at alloc time, deepest
       first. backtrace_depth records actual stored count (may be < MAX). */
    LsMcFrame       backtrace[LS_MC_BACKTRACE_MAX];
    int             backtrace_depth;
} LsMcEntry;

#define LS_MC_INITIAL_CAP 256
#define LS_MC_TOMBSTONE   ((void *)1)

static LsMcEntry *g_table = NULL;
static size_t     g_cap = 0;
static size_t     g_live = 0;        /* live entries (alloc'd, not yet freed) */
static size_t     g_total = 0;       /* live + freed (still in table for diag) */
static int        g_invalid_frees = 0;
static int        g_double_frees = 0;
static int        g_initialized = 0;
static int        g_reported = 0;     /* idempotent: only report once */

/* Phase D.2 — verbose / trace mode.
   Activated by env var LS_MEMCHECK_VERBOSE=1 (read once at first alloc).
   When on, every alloc/free/realloc/double-free/invalid-free emits a one-line
   trace to stderr so the user can see the alloc/free sequence as it happens.
   Costs a single int comparison + (when on) one fprintf per call. */
static int        g_verbose = -1;     /* -1 = unprobed; 0 = off; 1 = on */

/* Phase D.3 — strict mode.
   Activated by env var LS_MEMCHECK_STRICT=1. When on, ls_mc_report calls
   _exit(2) at the end if any leak / double-free / invalid-free was seen.
   Used to fail CI builds on any memory violation. */
static int        g_strict = -1;      /* -1 = unprobed; 0 = off; 1 = on */

/* Phase D.1 — call-stack tracking.
   Codegen-injected ls_mc_enter/ls_mc_leave maintain this stack. Overflow
   is tolerated: g_frame_top keeps incrementing past LS_MC_FRAME_STACK_MAX
   so leaves still match enters; alloc captures only the deepest MAX frames. */
static LsMcFrame  g_frame_stack[LS_MC_FRAME_STACK_MAX];
static int        g_frame_top = 0;
static int        g_frame_overflow_warned = 0;

LS_MC_EXPORT void ls_mc_report(void);   /* forward */

/* Read an env var as a 0/1 flag, treating any non-empty non-"0" value as 1. */
static int env_flag(const char *name) {
    const char *v = getenv(name);
    if (v == NULL || v[0] == '\0') return 0;
    if (v[0] == '0' && v[1] == '\0') return 0;
    return 1;
}

static void probe_modes_once(void) {
    if (g_verbose < 0) g_verbose = env_flag("LS_MEMCHECK_VERBOSE");
    if (g_strict  < 0) g_strict  = env_flag("LS_MEMCHECK_STRICT");
}

/* Bit-mixing hash for pointers — OS allocators like to return addresses
   with similar high bits, simple `mod` clusters terribly. */
static size_t hash_ptr(void *p) {
    uintptr_t x = (uintptr_t)p;
    x ^= x >> 33;
    x *= 0xff51afd7ed558ccdULL;
    x ^= x >> 33;
    x *= 0xc4ceb9fe1a85ec53ULL;
    x ^= x >> 33;
    return (size_t)x;
}

/* Find an entry. for_insert=1: returns first empty/tombstone slot OR the
   matching ptr; for_insert=0: returns matching ptr or NULL. */
static LsMcEntry *find_slot(void *p, int for_insert) {
    if (g_cap == 0) return NULL;
    size_t mask = g_cap - 1;
    size_t i = hash_ptr(p) & mask;
    LsMcEntry *first_tomb = NULL;
    for (size_t probes = 0; probes < g_cap; probes++) {
        LsMcEntry *e = &g_table[i];
        if (e->ptr == NULL) {
            if (for_insert)
                return first_tomb ? first_tomb : e;
            return NULL;
        }
        if (e->ptr == LS_MC_TOMBSTONE) {
            if (for_insert && first_tomb == NULL) first_tomb = e;
        } else if (e->ptr == p) {
            return e;
        }
        i = (i + 1) & mask;
    }
    return first_tomb;  /* table full of tombstones — shouldn't happen post-grow */
}

static void table_grow(void) {
    size_t old_cap = g_cap;
    LsMcEntry *old_table = g_table;
    size_t new_cap = old_cap == 0 ? LS_MC_INITIAL_CAP : old_cap * 2;
    LsMcEntry *new_table = (LsMcEntry *)calloc(new_cap, sizeof(LsMcEntry));
    if (!new_table) {
        fprintf(stderr, "[memcheck] OOM growing tracker table\n");
        return;
    }
    g_table = new_table;
    g_cap = new_cap;
    g_total = 0;
    /* Rehash all live + freed (we keep freed for double-free diagnostics) */
    for (size_t i = 0; i < old_cap; i++) {
        LsMcEntry *e = &old_table[i];
        if (e->ptr == NULL || e->ptr == LS_MC_TOMBSTONE) continue;
        LsMcEntry *slot = find_slot(e->ptr, 1);
        *slot = *e;
        g_total++;
    }
    free(old_table);
}

static void mc_init(void) {
    if (g_initialized) return;
    g_initialized = 1;
    g_table = (LsMcEntry *)calloc(LS_MC_INITIAL_CAP, sizeof(LsMcEntry));
    g_cap = LS_MC_INITIAL_CAP;
    probe_modes_once();
    if (g_verbose) {
        fprintf(stderr, "[memcheck] verbose mode enabled (LS_MEMCHECK_VERBOSE=1)\n");
    }
    if (g_strict) {
        fprintf(stderr, "[memcheck] strict mode enabled (LS_MEMCHECK_STRICT=1)\n");
    }
    atexit(ls_mc_report);
}

/* Phase D.1 — push a frame on entry to a user function.
   Tolerates overflow (top keeps counting so leave still balances correctly). */
LS_MC_EXPORT void ls_mc_enter(const char *fn, const char *file, int call_line) {
    if (g_frame_top < LS_MC_FRAME_STACK_MAX) {
        g_frame_stack[g_frame_top].fn_name = fn ? fn : "?";
        g_frame_stack[g_frame_top].file = file ? file : "?";
        g_frame_stack[g_frame_top].call_line = call_line;
    } else if (!g_frame_overflow_warned) {
        fprintf(stderr,
                "[memcheck] frame stack overflow (>%d) — backtraces may be truncated\n",
                LS_MC_FRAME_STACK_MAX);
        g_frame_overflow_warned = 1;
    }
    g_frame_top++;
}

/* Pop on function exit. Underflow can happen on broken codegen; we clamp at 0. */
LS_MC_EXPORT void ls_mc_leave(void) {
    if (g_frame_top > 0) g_frame_top--;
}

/* Snapshot the deepest LS_MC_BACKTRACE_MAX frames into the entry. Called
   under each alloc — cost is a small memcpy capped at MAX * sizeof(LsMcFrame). */
static void capture_backtrace(LsMcEntry *e) {
    int top = g_frame_top;
    if (top > LS_MC_FRAME_STACK_MAX) top = LS_MC_FRAME_STACK_MAX;
    int n = top;
    if (n > LS_MC_BACKTRACE_MAX) n = LS_MC_BACKTRACE_MAX;
    /* Copy the innermost n frames (highest indices in the stack), deepest first. */
    for (int i = 0; i < n; i++) {
        e->backtrace[i] = g_frame_stack[top - 1 - i];
    }
    e->backtrace_depth = n;
}

/* One-line trace helper for verbose mode. NULL site is tolerated (no site
   info available in deep wrapper paths). */
static void trace_op(const char *op, void *p, size_t sz, const LsMcSite *site) {
    if (!g_verbose) return;
    const char *file = (site && site->file) ? site->file : "?";
    int line = site ? site->line : 0;
    int col  = site ? site->col  : 0;
    const char *kind = (site && site->kind) ? site->kind : "unknown";
    fprintf(stderr, "[mc] %-7s ptr=%p size=%zu  %s @ %s:%d:%d\n",
            op, p, sz, kind, file, line, col);
}

LS_MC_EXPORT void *ls_mc_alloc(size_t sz, const LsMcSite *site) {
    if (!g_initialized) mc_init();
    /* Grow when half-full (counting freed entries — they still occupy slots). */
    if ((g_total + 1) * 2 >= g_cap) table_grow();

    void *p = malloc(sz);
    if (!p) return NULL;

    LsMcEntry *slot = find_slot(p, 1);
    if (slot == NULL) {
        /* Fallback — should not happen */
        return p;
    }
    /* If slot already had a freed entry for this ptr (rare: malloc reused
       a previously-freed address), we just overwrite — the old freed record
       is no longer needed since the address is now live again. */
    if (slot->ptr != NULL && slot->ptr != LS_MC_TOMBSTONE && slot->freed) {
        /* Overwrite: this freed entry is being recycled; not in g_total
           bookkeeping any more (we already counted on first insert), so
           don't bump g_total again. */
    } else {
        g_total++;
    }
    slot->ptr = p;
    slot->size = sz;
    slot->alloc_site = site;
    slot->free_site = NULL;
    slot->freed = 0;
    capture_backtrace(slot);
    g_live++;
    trace_op("+alloc", p, sz, site);
    return p;
}

/* realloc wrapper: untrack the old pointer (treating the realloc as an
   implicit free of the old block), call libc realloc, track the new
   pointer at `site`. The standard library guarantees the old pointer is
   invalid after realloc returns (whether moved or in-place). */
LS_MC_EXPORT void *ls_mc_realloc(void *old_p, size_t new_sz, const LsMcSite *site) {
    if (!g_initialized) mc_init();
    if (old_p != NULL) {
        LsMcEntry *e = find_slot(old_p, 0);
        if (e != NULL && !e->freed) {
            /* Mark the old entry freed via this site (so a later double-free
               at the same address is still detectable). Don't decrement g_live
               here because we'll either re-track the new pointer below (net 0)
               or fail and the entry stays freed. */
            e->freed = 1;
            e->free_site = site;
            g_live--;
        }
    }
    if ((g_total + 1) * 2 >= g_cap) table_grow();
    void *new_p = realloc(old_p, new_sz);
    if (!new_p) return NULL;
    LsMcEntry *slot = find_slot(new_p, 1);
    if (slot != NULL) {
        if (slot->ptr != NULL && slot->ptr != LS_MC_TOMBSTONE && slot->freed) {
            /* recycled freed slot — don't double-count in g_total */
        } else {
            g_total++;
        }
        slot->ptr = new_p;
        slot->size = new_sz;
        slot->alloc_site = site;
        slot->free_site = NULL;
        slot->freed = 0;
        capture_backtrace(slot);
        g_live++;
    }
    trace_op("realloc", new_p, new_sz, site);
    return new_p;
}

LS_MC_EXPORT void ls_mc_free(void *p, const LsMcSite *site) {
    if (p == NULL) return;            /* free(NULL) is a no-op (libc semantics) */
    if (!g_initialized) {
        /* Code path freeing without a matching alloc — could be a static
           buffer or pre-init alloc. Just pass through silently. */
        free(p);
        return;
    }
    LsMcEntry *e = find_slot(p, 0);
    if (e == NULL) {
        fprintf(stderr,
                "[memcheck] INVALID FREE  %s:%d:%d  (%s)  ptr=%p — never allocated by LS\n",
                site->file, site->line, site->col, site->kind, p);
        g_invalid_frees++;
        return;
    }
    if (e->freed) {
        fprintf(stderr,
                "[memcheck] DOUBLE FREE   %s:%d:%d  (%s)  ptr=%p\n",
                site->file, site->line, site->col, site->kind, p);
        if (e->alloc_site) {
            fprintf(stderr,
                    "[memcheck]   originally allocated at %s:%d:%d (%s)\n",
                    e->alloc_site->file, e->alloc_site->line,
                    e->alloc_site->col, e->alloc_site->kind);
        }
        if (e->free_site) {
            fprintf(stderr,
                    "[memcheck]   first freed at         %s:%d:%d (%s)\n",
                    e->free_site->file, e->free_site->line,
                    e->free_site->col, e->free_site->kind);
        }
        g_double_frees++;
        return;
    }
    free(p);
    e->freed = 1;
    e->free_site = site;
    g_live--;
    trace_op("-free", p, e->size, site);
    /* Keep the freed entry in the table so a subsequent free of the same
       ptr can be identified as DOUBLE FREE. The entry is recycled if a
       new alloc happens to receive the same address (handled in ls_mc_alloc). */
}

LS_MC_EXPORT void ls_mc_report(void) {
    if (!g_initialized) return;
    if (g_reported) return;             /* idempotent: explicit + atexit safe */
    g_reported = 1;
    int leaks = 0;
    size_t leaked_bytes = 0;

    /* Drain stdout (where the user program writes) before emitting the
       stderr report, so AOT runs print "program output -> report" rather
       than the report appearing before any printf() output the CRT had
       buffered. JIT was unaffected because reports go through the same
       process at a clear point in time. */
    fflush(stdout);

    fprintf(stderr, "\n=== LS memcheck report ===\n");
    for (size_t i = 0; i < g_cap; i++) {
        LsMcEntry *e = &g_table[i];
        if (e->ptr == NULL || e->ptr == LS_MC_TOMBSTONE) continue;
        if (e->freed) continue;
        const char *file = e->alloc_site ? e->alloc_site->file : "?";
        int line = e->alloc_site ? e->alloc_site->line : 0;
        int col  = e->alloc_site ? e->alloc_site->col  : 0;
        const char *kind = e->alloc_site ? e->alloc_site->kind : "unknown";
        fprintf(stderr, "[memcheck] LEAK %5zu bytes  %s:%d:%d  (%s)\n",
                e->size, file, line, col, kind);
        /* D.1 — print captured call-stack backtrace (deepest first). */
        for (int b = 0; b < e->backtrace_depth; b++) {
            const LsMcFrame *f = &e->backtrace[b];
            fprintf(stderr, "[memcheck]   at %-24s %s:%d\n",
                    f->fn_name ? f->fn_name : "?",
                    f->file ? f->file : "?",
                    f->call_line);
        }
        leaks++;
        leaked_bytes += e->size;
    }
    fprintf(stderr,
            "[memcheck] SUMMARY: %d leak(s) (%zu bytes), %d double-free, %d invalid free\n",
            leaks, leaked_bytes, g_double_frees, g_invalid_frees);
    int violations = leaks + g_double_frees + g_invalid_frees;
    if (violations == 0)
        fprintf(stderr, "[memcheck] OK clean\n");
    fprintf(stderr, "=== end ===\n");
    fflush(stderr);

    /* Phase D.3 — strict mode: any violation forces a non-zero exit. _exit(2)
       (rather than exit) because we're already inside atexit handling and
       calling exit() recursively is undefined. The "2" is conventional for
       "memcheck violation" — distinct from typical 1 = generic failure. */
    if (g_strict && violations > 0) {
        fprintf(stderr, "[memcheck] strict mode: failing with exit code 2\n");
        fflush(stderr);
        /* _Exit (C99) — does not run atexit handlers or flush stdio buffers,
           which is what we want here: we're already inside an atexit handler,
           and calling exit() would be undefined. We've manually flushed stderr
           above so the report is durable. */
        _Exit(2);
    }
}
