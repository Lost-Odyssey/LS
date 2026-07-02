/* codegen_lifetime.c — A2: llvm.lifetime.start/end markers for scoped stack
 * slots. (docs/plan_opt_lifetime_markers.md)
 *
 * WHY. codegen emits every alloca in the entry block and never tells LLVM when
 * a slot's useful life ends. Without `llvm.lifetime.end` the StackColoring pass
 * cannot prove two allocas have disjoint lifetimes, so it never overlaps their
 * frame slots. match-dense recursive-descent parsers (std.json/html/md) spend a
 * distinct slot on every arm temporary / spill / binder and their stack frames
 * balloon. Bracketing a slot with lifetime.start/end lets StackColoring reuse
 * one frame slot for lexically-disjoint aggregates.
 *
 * SCOPE (v1, deliberately conservative — a wrong lifetime interval is a
 * miscompile, so this only marks slots whose end placement is provably safe):
 *   - AOT only. JIT / REPL incremental modules keep slots live across snippet
 *     boundaries, so the whole file gates on `!ctx->extern_builtins`.
 *   - Only AGGREGATE-typed locals (LLVM struct / array kind): Str, Vec, Map,
 *     user structs, enums ({i8 disc,[N x i8]}), fixed arrays — the stack heavy
 *     hitters. Scalars (int/float/bool/ptr) are skipped: tiny, and this filter
 *     also excludes borrow bindings (`&T r = &x` — a pointer scalar) for free.
 *
 * PAIRING. cg_emit_lifetime_start returns whether it actually emitted, and the
 * caller stamps CgSymbol.lifetime_marked. Every end site emits ONLY for marked
 * slots, so start/end are always paired — there is never an end without a start.
 * The asymmetry also fails safe: if a slot is marked at var_decl but a later
 * pass turns it borrowed / the end site is never reached (early return through a
 * path we do not cover), the missing end merely forfeits the optimization.
 * LLVM treats a lifetime.start with no matching end as "live to function end";
 * only a lifetime.end BEFORE a real use is UB, and every end here is emitted
 * strictly after the slot's scope-exit drop and before the block leaves.
 *
 * Switch: LS_NO_LIFETIME=1 suppresses all emission. With it set, cg_emit_*
 * short-circuit before touching the IR, so the marked flag is never set and the
 * cleanup end-passes see nothing → the emitted module is byte-identical to the
 * pre-A2 baseline.
 */
#include "codegen_internal.h"

#include <stdlib.h>
#include <string.h>

#include <llvm-c/Core.h>

/* -1 unknown, 0 enabled, 1 disabled — LS_NO_LIFETIME checked once. */
static int lifetime_env_disabled = -1;

static bool cg_lifetime_enabled(CodegenContext *ctx)
{
    if (ctx == NULL || ctx->extern_builtins)
        return false;                 /* AOT-only; JIT/REPL slots span snippets */
    if (lifetime_env_disabled < 0)
        lifetime_env_disabled = getenv("LS_NO_LIFETIME") ? 1 : 0;
    return lifetime_env_disabled == 0;
}

/* Emit `call void @llvm.lifetime.{start,end}.p0(i64 size, ptr slot)` at the
   current builder position. No-op if the current block is already terminated
   (a lifetime marker must sit inside a live block). */
static void emit_lifetime_call(CodegenContext *ctx, bool is_start,
                               LLVMValueRef slot, LLVMTypeRef ty)
{
    if (slot == NULL || ty == NULL)
        return;
    LLVMBasicBlockRef bb = LLVMGetInsertBlock(ctx->builder);
    if (bb == NULL || LLVMGetBasicBlockTerminator(bb) != NULL)
        return;

    LLVMTargetDataRef td = LLVMGetModuleDataLayout(ctx->module);
    unsigned long long sz = LLVMABISizeOfType(td, ty);
    if (sz == 0)
        return;

    LLVMTypeRef ptr_t = LLVMPointerTypeInContext(ctx->context, 0);
    const char *name = is_start ? "llvm.lifetime.start" : "llvm.lifetime.end";
    unsigned id = LLVMLookupIntrinsicID(name, strlen(name));
    /* lifetime.{start,end} are overloaded on the pointer operand type (.p0). */
    LLVMTypeRef overload[1] = { ptr_t };
    LLVMValueRef fn = LLVMGetIntrinsicDeclaration(ctx->module, id, overload, 1);
    LLVMTypeRef fnty = LLVMIntrinsicGetType(ctx->context, id, overload, 1);
    if (fn == NULL || fnty == NULL)
        return;

    LLVMValueRef args[2] = {
        LLVMConstInt(LLVMInt64TypeInContext(ctx->context), sz, 0),
        slot,
    };
    LLVMBuildCall2(ctx->builder, fnty, fn, args, 2, "");
}

/* True aggregate kinds only — the stack heavy hitters. Also naturally excludes
   scalar borrow-binding locals (pointers). */
static bool ty_is_marked_aggregate(LLVMTypeRef ty)
{
    if (ty == NULL)
        return false;
    LLVMTypeKind k = LLVMGetTypeKind(ty);
    return k == LLVMStructTypeKind || k == LLVMArrayTypeKind;
}

bool cg_emit_lifetime_start(CodegenContext *ctx, LLVMValueRef slot, LLVMTypeRef ty)
{
    if (!cg_lifetime_enabled(ctx) || !ty_is_marked_aggregate(ty))
        return false;
    emit_lifetime_call(ctx, /*is_start=*/true, slot, ty);
    return true;
}

void cg_emit_lifetime_end(CodegenContext *ctx, LLVMValueRef slot, LLVMTypeRef ty)
{
    if (!cg_lifetime_enabled(ctx))
        return;
    emit_lifetime_call(ctx, /*is_start=*/false, slot, ty);
}
