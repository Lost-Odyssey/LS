/* builtins_perf.h — Compiler-level built-in `perf` module.
 *
 * Exposes a stdlib `perf` module with high-resolution timing primitives:
 *   perf.now()         — monotonic wall clock in nanoseconds (i64)
 *   perf.rdtsc()       — CPU cycle counter via RDTSC x86 intrinsic (i64)
 *   perf.rdtscp()      — serialising RDTSCP x86 intrinsic (i64)
 *   perf.elapsed_ns(t) — nanoseconds since t (i64)
 *   perf.elapsed_ms(t) — milliseconds since t (f64)
 *   perf.elapsed_s(t)  — seconds since t (f64)
 *
 * perf.now() delegates to ls_os_perf_now() declared in runtime/os_backend.h
 * and implemented in os_win32.c / os_posix.c.
 * rdtsc / rdtscp / elapsed_* are fully inlined at the LLVM IR level.
 *
 * User-priority shadowing: if perf.ls exists next to the importing file,
 * that file wins; this built-in is only used when no user file is present.
 */
#ifndef LS_BUILTINS_PERF_H
#define LS_BUILTINS_PERF_H

#include "ast.h"
#include "types.h"

typedef struct Checker Checker;

/* ---- Type / lookup API (no LLVM deps) ---- */

typedef enum {
    PERF_EMIT_NOW,          /* calls ls_os_perf_now() → i64 nanoseconds  */
    PERF_EMIT_RDTSC,        /* llvm.x86.rdtsc intrinsic → i64 cycles      */
    PERF_EMIT_RDTSCP,       /* llvm.x86.rdtscp intrinsic → i64 cycles     */
    PERF_EMIT_ELAPSED_NS,   /* (now() - t0) → i64 nanoseconds             */
    PERF_EMIT_ELAPSED_MS,   /* (now() - t0) / 1e6 → f64 milliseconds      */
    PERF_EMIT_ELAPSED_S,    /* (now() - t0) / 1e9 → f64 seconds           */
} PerfEmitKind;

/* Lookup info for a perf function.  Returns true on hit; NULL out-params
   are tolerated. */
bool builtin_perf_lookup_fn(const char *name, int *out_arity,
                            PerfEmitKind *out_kind);

/* Build the TYPE_MODULE for the `perf` built-in module. */
Type *builtin_perf_make_type(void);

/* ---- Codegen API (LLVM-dependent; compiled separately) ---- */
#ifdef LS_INCLUDE_CODEGEN
#include "codegen.h"

/* Emit IR for a call perf.<fn_name>(args...).  Returns the LLVM value
   produced (i64 or f64 depending on the function), or NULL on error. */
LLVMValueRef builtin_perf_emit_call(CodegenContext *ctx, const char *fn_name,
                                    AstNode **args, int arg_count);
#endif

#endif /* LS_BUILTINS_PERF_H */
