/* leakcheck.c — compiler self heap-leak tracker (see leakcheck.h).
 *
 * Standalone: does NOT include common.h, so the malloc/free macros are not in
 * scope here and this file uses the real CRT heap directly. A small open-
 * addressing table maps live pointer -> (size, site). At exit, unfreed blocks
 * are aggregated by allocation site and printed to stderr.
 */
#ifdef LS_LEAKCHECK

#include "leakcheck.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <stdint.h>

typedef struct {
    void       *ptr;        /* NULL = empty, (void*)1 = tombstone */
    size_t      size;
    const char *file;
    int         line;
} LcEntry;

#define LC_TOMB ((void *)1)

static LcEntry *g_tab = NULL;
static size_t   g_cap = 0;      /* power of two */
static size_t   g_live = 0;     /* occupied (non-empty, non-tomb) */
static size_t   g_used = 0;     /* occupied + tombstones */
static size_t   g_untracked_free = 0;
static int      g_inited = 0;
static int      g_reporting = 0;

static size_t hash_ptr(void *p) {
    uintptr_t x = (uintptr_t)p;
    x ^= x >> 33; x *= (uintptr_t)0xff51afd7ed558ccdULL; x ^= x >> 33;
    return (size_t)x;
}

static void lc_grow(void) {
    size_t newcap = g_cap ? g_cap * 2 : 4096;
    LcEntry *nt = (LcEntry *)calloc(newcap, sizeof(LcEntry));
    if (!nt) { fprintf(stderr, "[leakcheck] OOM growing table\n"); return; }
    for (size_t i = 0; i < g_cap; i++) {
        void *p = g_tab[i].ptr;
        if (p && p != LC_TOMB) {
            size_t m = newcap - 1, j = hash_ptr(p) & m;
            while (nt[j].ptr) j = (j + 1) & m;
            nt[j] = g_tab[i];
        }
    }
    free(g_tab);
    g_tab = nt; g_cap = newcap; g_used = g_live;
}

static void lc_insert(void *p, size_t size, const char *file, int line) {
    if (!p) return;
    if (g_used * 4 >= g_cap * 3) lc_grow();
    if (!g_cap) return;
    size_t m = g_cap - 1, j = hash_ptr(p) & m, first_tomb = (size_t)-1;
    while (g_tab[j].ptr) {
        if (g_tab[j].ptr == LC_TOMB) { if (first_tomb == (size_t)-1) first_tomb = j; }
        else if (g_tab[j].ptr == p) { g_tab[j].size = size; g_tab[j].file = file; g_tab[j].line = line; return; }
        j = (j + 1) & m;
    }
    if (first_tomb != (size_t)-1) j = first_tomb; else g_used++;
    g_tab[j].ptr = p; g_tab[j].size = size; g_tab[j].file = file; g_tab[j].line = line;
    g_live++;
}

static int lc_remove(void *p) {   /* 1 if found+removed */
    if (!p || !g_cap) return 0;
    size_t m = g_cap - 1, j = hash_ptr(p) & m;
    while (g_tab[j].ptr) {
        if (g_tab[j].ptr == p) { g_tab[j].ptr = LC_TOMB; g_live--; return 1; }
        j = (j + 1) & m;
    }
    return 0;
}

void ls_lc_init(void) {
    if (g_inited) return;
    g_inited = 1;
    atexit(ls_lc_report);
}

void *ls_lc_malloc(size_t size, const char *file, int line) {
    void *p = malloc(size);
    if (p) lc_insert(p, size, file, line);
    return p;
}

void *ls_lc_calloc(size_t n, size_t size, const char *file, int line) {
    void *p = calloc(n, size);
    if (p) lc_insert(p, n * size, file, line);
    return p;
}

void *ls_lc_realloc(void *old, size_t size, const char *file, int line) {
    if (old) lc_remove(old);
    void *p = realloc(old, size);
    if (p) lc_insert(p, size, file, line);
    else if (size == 0) { /* realloc(p,0) freed it */ }
    return p;
}

char *ls_lc_strdup(const char *s, const char *file, int line) {
    if (!s) return NULL;
    size_t n = strlen(s) + 1;
    char *p = (char *)malloc(n);
    if (p) { memcpy(p, s, n); lc_insert(p, n, file, line); }
    return p;
}

void ls_lc_free(void *p) {
    if (!p) return;
    if (!lc_remove(p)) g_untracked_free++;
    free(p);
}

void *ls_lc_xmalloc(size_t size, const char *file, int line) {
    void *p = malloc(size);
    if (p == NULL && size != 0) { fprintf(stderr, "fatal: out of memory\n"); exit(1); }
    if (p) lc_insert(p, size, file, line);
    return p;
}

void *ls_lc_xrealloc(void *old, size_t size, const char *file, int line) {
    if (old) lc_remove(old);
    void *p = realloc(old, size);
    if (p == NULL && size != 0) { fprintf(stderr, "fatal: out of memory\n"); exit(1); }
    if (p) lc_insert(p, size, file, line);
    return p;
}

/* ---- exit report: aggregate live blocks by (file,line) ---- */
typedef struct { const char *file; int line; size_t count; size_t bytes; } LcSite;

void ls_lc_report(void) {
    if (g_reporting) return;
    g_reporting = 1;

    size_t total_bytes = 0, total_blocks = g_live;
    LcSite *sites = NULL; size_t nsites = 0, capsites = 0;
    for (size_t i = 0; i < g_cap; i++) {
        void *p = g_tab ? g_tab[i].ptr : NULL;
        if (!p || p == LC_TOMB) continue;
        total_bytes += g_tab[i].size;
        size_t k;
        for (k = 0; k < nsites; k++)
            if (sites[k].line == g_tab[i].line && sites[k].file == g_tab[i].file) break;
        if (k == nsites) {
            if (nsites == capsites) {
                capsites = capsites ? capsites * 2 : 64;
                sites = (LcSite *)realloc(sites, capsites * sizeof(LcSite));
            }
            sites[k].file = g_tab[i].file; sites[k].line = g_tab[i].line;
            sites[k].count = 0; sites[k].bytes = 0; nsites++;
        }
        sites[k].count++; sites[k].bytes += g_tab[i].size;
    }

    fprintf(stderr, "\n[leakcheck] === compiler heap leak report ===\n");
    if (total_blocks == 0) {
        fprintf(stderr, "[leakcheck] OK: 0 leaked blocks\n");
    } else {
        fprintf(stderr, "[leakcheck] LEAK: %zu block(s), %zu bytes, %zu site(s)\n",
                total_blocks, total_bytes, nsites);
        /* simple selection sort by bytes desc (nsites is small) */
        for (size_t a = 0; a < nsites; a++) {
            size_t best = a;
            for (size_t b = a + 1; b < nsites; b++)
                if (sites[b].bytes > sites[best].bytes) best = b;
            LcSite t = sites[a]; sites[a] = sites[best]; sites[best] = t;
            fprintf(stderr, "[leakcheck]   %6zu blk  %9zu B   %s:%d\n",
                    sites[a].count, sites[a].bytes,
                    sites[a].file ? sites[a].file : "?", sites[a].line);
        }
    }
    if (g_untracked_free)
        fprintf(stderr, "[leakcheck] note: %zu free() of untracked pointers\n",
                g_untracked_free);
    fprintf(stderr, "[leakcheck] === end ===\n");
    free(sites);
}

#else
/* Avoid a C4206 empty-translation-unit warning when leak-check is disabled. */
typedef int ls_leakcheck_disabled_tu;
#endif /* LS_LEAKCHECK */
