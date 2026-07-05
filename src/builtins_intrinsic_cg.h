/* builtins_intrinsic_cg.h — name-keyed registry for the compiler intrinsics
 * that AST_CALL used to dispatch through three hard-coded strcmp chains in
 * codegen_expr.c (S2, docs/plan_arch_cleanup.md):
 *
 *   - sync    __mutex_* / __rwlock_* / __cond_* / __cpu_relax / __cpu_yield
 *             (std.sync / std.chan; opaque-handle calls into the OS runtime)
 *   - atomic  __atomic_*   (std.atomic; one inline LLVM atomic instruction)
 *   - simd    __simd_*     (Simd(T,N); one <N x T> IR instruction)
 *   - bytecopy std.sys.c.__ls_bytecopy -> @llvm.memcpy (form-(3) primitive;
 *             dispatched from the MODULE-call site, never from a bare ident —
 *             see builtin_intrinsic_is_global)
 *
 * Architecture mirrors builtins_math_cg.c: enum Kind + name->Kind lookup +
 * emit functions, one registry TU.
 *
 * ============================ ADDING AN INTRINSIC ============================
 * 1. Add an INTRIN_* value to IntrinKind below (inside its family block).
 * 2. Teach builtin_intrinsic_lookup (builtins_intrinsic_cg.c) the name — one
 *    table row for exact names (sync family), or one branch for prefix-matched
 *    names (atomic load/store ordering suffixes).
 * 3. Emit: sync family -> one row in the sync_rows[] table (runtime symbol,
 *    return type, arg count); atomic/simd -> one case in the family's emit
 *    switch. Keep result-label strings stable — emit-ir diffs are the
 *    regression oracle for this whole TU.
 * 4. The checker has its own name list (checker.c ~3323/3371/3435) — a new
 *    intrinsic needs BOTH sides; codegen alone will never see the call.
 * 5. Cover it in the matching tests/samples/intrinsic_*_smoke.lls corpus so
 *    the emit-ir baseline exercises the new row.
 * =============================================================================
 *
 * Dispatch contract (preserved from the old inline chains): a name that
 * matches a family PREFIX is always consumed by the registry — an unknown
 * member (e.g. __atomic_nand) produces the family's "internal: unknown ...
 * intrinsic" error, it does NOT fall through to user-call resolution.
 */
#ifndef LS_BUILTINS_INTRINSIC_CG_H
#define LS_BUILTINS_INTRINSIC_CG_H

#include <stdbool.h>

#include "ast.h"
#include "codegen.h"

typedef enum {
    INTRIN_NONE = 0,

    /* ---- sync family (exact names; opaque-handle OS runtime calls) ---- */
    INTRIN_MUTEX_INIT,
    INTRIN_MUTEX_LOCK,
    INTRIN_MUTEX_TRYLOCK,
    INTRIN_MUTEX_UNLOCK,
    INTRIN_MUTEX_DESTROY,
    INTRIN_RWLOCK_INIT,
    INTRIN_RWLOCK_RDLOCK,
    INTRIN_RWLOCK_WRLOCK,
    INTRIN_RWLOCK_RDUNLOCK,
    INTRIN_RWLOCK_WRUNLOCK,
    INTRIN_RWLOCK_DESTROY,
    INTRIN_COND_INIT,
    INTRIN_COND_WAIT,
    INTRIN_COND_SIGNAL,
    INTRIN_COND_BROADCAST,
    INTRIN_COND_DESTROY,
    INTRIN_CPU_RELAX,
    INTRIN_CPU_YIELD,
    INTRIN_SYNC_UNKNOWN,    /* __mutex_/__rwlock_/__cond_ prefixed, unknown member */

    /* ---- atomic family (__atomic_*; one inline LLVM atomic instruction).
       Load/store are PREFIX rows: the ordering suffix (_acquire/_release/
       _relaxed) is parsed off the callee name, so __atomic_load_acquire and
       friends all map to INTRIN_ATOMIC_LOAD/STORE. The other rows are exact
       names — a suffixed __atomic_add_relaxed is INTRIN_ATOMIC_UNKNOWN,
       exactly like the retired strcmp chain (checker rejects it anyway). ---- */
    INTRIN_ATOMIC_FENCE,
    INTRIN_ATOMIC_LOAD,     /* prefix "__atomic_load"  (+ optional ordering suffix) */
    INTRIN_ATOMIC_STORE,    /* prefix "__atomic_store" (+ optional ordering suffix) */
    INTRIN_ATOMIC_ADD,
    INTRIN_ATOMIC_SUB,
    INTRIN_ATOMIC_SWAP,
    INTRIN_ATOMIC_CAS,
    INTRIN_ATOMIC_UNKNOWN,  /* __atomic_ prefixed, unknown member */

    /* ---- simd family (__simd_*; one <N x T> IR instruction each, exact
       names — docs/plan_simd.md §4.2). The checker set node->resolved_type
       (Simd for producers/ops, the element type for lane/reduce). ---- */
    INTRIN_SIMD_ZERO,
    INTRIN_SIMD_SPLAT,
    INTRIN_SIMD_LANE,
    INTRIN_SIMD_FMA,
    INTRIN_SIMD_MAX,
    INTRIN_SIMD_MIN,
    INTRIN_SIMD_REDUCE_ADD,
    INTRIN_SIMD_REDUCE_MAX,
    INTRIN_SIMD_REDUCE_MIN,
    INTRIN_SIMD_LOAD,
    INTRIN_SIMD_STORE,
    INTRIN_SIMD_LOAD_MASKED,
    INTRIN_SIMD_STORE_MASKED,
    INTRIN_SIMD_CAST,
    INTRIN_SIMD_FLOOR,
    INTRIN_SIMD_BITCAST,
    INTRIN_SIMD_UNKNOWN,    /* __simd_ prefixed, unknown member */

    INTRIN_KIND_COUNT
} IntrinKind;

/* Classify a callee name. INTRIN_NONE = not an intrinsic (fall through to the
   normal call paths). Pure name lookup — no AST/type inspection. */
IntrinKind builtin_intrinsic_lookup(const char *name);

/* True when `kind` is dispatched at the BARE-IDENT call position in AST_CALL
   (global intrinsics that survive generic instantiation without imports).
   False for INTRIN_NONE and for module-qualified rows (bytecopy). */
bool builtin_intrinsic_is_global(IntrinKind kind);

/* Emit IR for an intrinsic call. `name` is the callee name (bare ident, or
   the field name at the module-call site); `node` is the AST_CALL node.
   Returns the call's LLVM value, or NULL for void intrinsics AND on error
   (matching the old inline chains — callers treat both identically). */
LLVMValueRef builtin_intrinsic_emit_call(CodegenContext *ctx, const char *name,
                                         AstNode *node);

#endif /* LS_BUILTINS_INTRINSIC_CG_H */
