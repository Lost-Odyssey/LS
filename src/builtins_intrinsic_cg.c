/* builtins_intrinsic_cg.c — registry + emitters for the AST_CALL compiler
 * intrinsics (sync / atomic / simd / bytecopy). See the header for the
 * "adding an intrinsic" how-to and the dispatch contract.
 *
 * ZERO-BEHAVIOR TRANSPLANT (S2 Phase 1): every emit body below is moved
 * verbatim from codegen_expr.c — instruction order, result-label strings and
 * error texts are load-bearing (emit-ir byte-diff against the pre-refactor
 * baselines is the acceptance oracle). Do not "clean up" emission code here
 * without re-running the intrinsic smoke corpora diffs.
 */
#include "builtins_intrinsic_cg.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <llvm-c/Core.h>

#include "codegen_internal.h"

/* ============================== lookup ================================== */

/* Exact-name rows (sync family). Prefix families (atomic/simd) are handled
   as branches in builtin_intrinsic_lookup below. */
static const struct {
    const char *name;
    IntrinKind kind;
} sync_names[] = {
    { "__mutex_init",      INTRIN_MUTEX_INIT },
    { "__mutex_lock",      INTRIN_MUTEX_LOCK },
    { "__mutex_trylock",   INTRIN_MUTEX_TRYLOCK },
    { "__mutex_unlock",    INTRIN_MUTEX_UNLOCK },
    { "__mutex_destroy",   INTRIN_MUTEX_DESTROY },
    { "__rwlock_init",     INTRIN_RWLOCK_INIT },
    { "__rwlock_rdlock",   INTRIN_RWLOCK_RDLOCK },
    { "__rwlock_wrlock",   INTRIN_RWLOCK_WRLOCK },
    { "__rwlock_rdunlock", INTRIN_RWLOCK_RDUNLOCK },
    { "__rwlock_wrunlock", INTRIN_RWLOCK_WRUNLOCK },
    { "__rwlock_destroy",  INTRIN_RWLOCK_DESTROY },
    { "__cond_init",       INTRIN_COND_INIT },
    { "__cond_wait",       INTRIN_COND_WAIT },
    { "__cond_signal",     INTRIN_COND_SIGNAL },
    { "__cond_broadcast",  INTRIN_COND_BROADCAST },
    { "__cond_destroy",    INTRIN_COND_DESTROY },
    { "__cpu_relax",       INTRIN_CPU_RELAX },
    { "__cpu_yield",       INTRIN_CPU_YIELD },
};

IntrinKind builtin_intrinsic_lookup(const char *name)
{
    if (name == NULL || name[0] != '_')
        return INTRIN_NONE;

    /* sync family: exact names under three prefixes + two bare cpu hints.
       The prefix check decides family membership (an unknown __mutex_xyz is
       consumed as INTRIN_SYNC_UNKNOWN, same as the old inline chain). */
    if (strncmp(name, "__mutex_", 8) == 0 ||
        strncmp(name, "__rwlock_", 9) == 0 ||
        strncmp(name, "__cond_", 7) == 0 ||
        strcmp(name, "__cpu_relax") == 0 ||
        strcmp(name, "__cpu_yield") == 0)
    {
        for (size_t i = 0; i < sizeof(sync_names) / sizeof(sync_names[0]); i++)
            if (strcmp(name, sync_names[i].name) == 0)
                return sync_names[i].kind;
        return INTRIN_SYNC_UNKNOWN;
    }

    return INTRIN_NONE;
}

bool builtin_intrinsic_is_global(IntrinKind kind)
{
    return kind != INTRIN_NONE;
}

/* ============================ sync family =============================== */

/* One row per runtime call: OS-backend symbol, return type, opaque-pointer
   arg count. Mirrors the retired if-chain in codegen_expr.c exactly. */
typedef enum { SYNC_RET_VOID, SYNC_RET_PTR, SYNC_RET_I32 } SyncRet;

static const struct {
    IntrinKind kind;
    const char *sym;
    SyncRet ret;
    int nargs;
} sync_rows[] = {
    { INTRIN_MUTEX_INIT,      "ls_mutex_init",      SYNC_RET_PTR,  0 },
    { INTRIN_MUTEX_LOCK,      "ls_mutex_lock",      SYNC_RET_I32,  1 },
    { INTRIN_MUTEX_TRYLOCK,   "ls_mutex_trylock",   SYNC_RET_I32,  1 },
    { INTRIN_MUTEX_UNLOCK,    "ls_mutex_unlock",    SYNC_RET_I32,  1 },
    { INTRIN_MUTEX_DESTROY,   "ls_mutex_destroy",   SYNC_RET_VOID, 1 },
    { INTRIN_RWLOCK_INIT,     "ls_rwlock_init",     SYNC_RET_PTR,  0 },
    { INTRIN_RWLOCK_RDLOCK,   "ls_rwlock_rdlock",   SYNC_RET_I32,  1 },
    { INTRIN_RWLOCK_WRLOCK,   "ls_rwlock_wrlock",   SYNC_RET_I32,  1 },
    { INTRIN_RWLOCK_RDUNLOCK, "ls_rwlock_rdunlock", SYNC_RET_I32,  1 },
    { INTRIN_RWLOCK_WRUNLOCK, "ls_rwlock_wrunlock", SYNC_RET_I32,  1 },
    { INTRIN_RWLOCK_DESTROY,  "ls_rwlock_destroy",  SYNC_RET_VOID, 1 },
    /* condition variables (std.chan). __cond_wait is the only 2-arg sync
       intrinsic (cond handle, mutex handle — both opaque pointers). */
    { INTRIN_COND_INIT,       "ls_cond_init",       SYNC_RET_PTR,  0 },
    { INTRIN_COND_WAIT,       "ls_cond_wait",       SYNC_RET_VOID, 2 },
    { INTRIN_COND_SIGNAL,     "ls_cond_signal",     SYNC_RET_VOID, 1 },
    { INTRIN_COND_BROADCAST,  "ls_cond_broadcast",  SYNC_RET_VOID, 1 },
    { INTRIN_COND_DESTROY,    "ls_cond_destroy",    SYNC_RET_VOID, 1 },
    { INTRIN_CPU_RELAX,       "ls_cpu_relax",       SYNC_RET_VOID, 0 },
    { INTRIN_CPU_YIELD,       "ls_cpu_yield",       SYNC_RET_VOID, 0 },
};

/* Mutex + spin runtime intrinsics (std.sync). Emit a call to the OS-backend
   runtime function on an opaque handle. These are GLOBAL intrinsics (not
   import aliases), so — like __task_* — they survive generic-method
   instantiation in a consumer module that hasn't imported std.c. They know
   nothing about Mutex(T): an opaque handle in/out is the whole interface
   (the same clean boundary as __atomic_* over scalars). */
static LLVMValueRef intrin_sync_emit(CodegenContext *ctx, const char *mname,
                                     IntrinKind kind, AstNode *node)
{
    LLVMTypeRef ptr_t = LLVMPointerTypeInContext(ctx->context, 0);
    LLVMTypeRef void_t = LLVMVoidTypeInContext(ctx->context);
    LLVMTypeRef i32_t = LLVMInt32TypeInContext(ctx->context);

    const char *sym = NULL;
    LLVMTypeRef ret_t = void_t;
    int nargs = 1; /* default: one opaque handle argument */
    for (size_t i = 0; i < sizeof(sync_rows) / sizeof(sync_rows[0]); i++)
    {
        if (sync_rows[i].kind != kind)
            continue;
        sym = sync_rows[i].sym;
        ret_t = sync_rows[i].ret == SYNC_RET_PTR ? ptr_t
              : sync_rows[i].ret == SYNC_RET_I32 ? i32_t : void_t;
        nargs = sync_rows[i].nargs;
        break;
    }
    if (sym == NULL)
    {
        cg_error(ctx, node->line, node->column,
                 "internal: unknown sync intrinsic '%s'", mname);
        return NULL;
    }

    /* Build 0, 1, or 2 opaque-pointer arguments. */
    LLVMValueRef call_args[2];
    LLVMTypeRef  param_tys[2] = { ptr_t, ptr_t };
    for (int i = 0; i < nargs; i++)
    {
        LLVMValueRef a = codegen_expr(ctx, node->as.call.args[i]);
        if (a == NULL) return NULL;
        call_args[i] = a;
    }
    LLVMTypeRef fn_ty = LLVMFunctionType(ret_t, nargs ? param_tys : NULL,
                                         (unsigned)nargs, 0);

    LLVMValueRef fn = LLVMGetNamedFunction(ctx->module, sym);
    if (fn == NULL) fn = LLVMAddFunction(ctx->module, sym, fn_ty);
    bool is_void = (ret_t == void_t);
    LLVMValueRef rv = LLVMBuildCall2(ctx->builder, fn_ty, fn,
                                     nargs ? call_args : NULL, (unsigned)nargs,
                                     is_void ? "" : mname);
    return is_void ? NULL : rv;
}

/* ============================== dispatch ================================ */

LLVMValueRef builtin_intrinsic_emit_call(CodegenContext *ctx, const char *name,
                                         AstNode *node)
{
    IntrinKind kind = builtin_intrinsic_lookup(name);
    switch (kind)
    {
    case INTRIN_NONE:
        return NULL;
    default:
        return intrin_sync_emit(ctx, name, kind, node);
    }
}
