/* leakcheck.h — opt-in compiler self heap-leak tracker (Windows-native).
 *
 * The runtime memcheck (`ls run --memcheck`) instruments the *generated LS
 * program*. This tracks the *compiler's own* allocations (scanner / parser /
 * checker / codegen). MSVC's _CrtDumpMemoryLeaks needs a debug CRT (/MDd),
 * which cannot link against the release-CRT static LLVM (crt_mismatch_bug.md);
 * so instead common.h macro-redirects malloc/calloc/realloc/free/strdup in the
 * compiler TUs to the trackers below, which sit on top of the real CRT heap and
 * dump unfreed blocks (grouped by site) at exit. LLVM (precompiled) is never
 * macro-affected. Enabled only with -DLS_LEAKCHECK=ON; a no-op header otherwise.
 *
 * Measure with `ls parse` / `ls check` (no codegen) to isolate frontend leaks
 * from LLVM. Single-threaded use (the compiler driver).
 */
#ifndef LS_LEAKCHECK_H
#define LS_LEAKCHECK_H
#ifdef LS_LEAKCHECK

#include <stddef.h>

void  ls_lc_init(void);   /* registers the exit report; call once from main */
void  ls_lc_report(void); /* dump live blocks to stderr (also runs via atexit) */

void *ls_lc_malloc(size_t size, const char *file, int line);
void *ls_lc_calloc(size_t n, size_t size, const char *file, int line);
void *ls_lc_realloc(void *p, size_t size, const char *file, int line);
char *ls_lc_strdup(const char *s, const char *file, int line);
void  ls_lc_free(void *p);

/* malloc_safe / realloc_safe replacements: same OOM-exit semantics, but
   tracked with the caller's site (common.h macro-redirects the wrappers). */
void *ls_lc_xmalloc(size_t size, const char *file, int line);
void *ls_lc_xrealloc(void *p, size_t size, const char *file, int line);

#endif /* LS_LEAKCHECK */
#endif /* LS_LEAKCHECK_H */
