/* codegen_stmt.c
   语句发射 + 闭包运行时协作：codegen_stmt 分发器 + cg_stmt_* case helper +
   闭包调用/fn→Block + env clone/drop（闭包字面量本体在 codegen_closure.c）

   Bodies mechanically relocated from codegen.c (docs/plan_codegen_split.md).
   No logic changes. All prototypes live in codegen_internal.h. */
#include "codegen.h"
#include "codegen_internal.h"
#include "mangle.h"
#include "module.h"
#define LS_INCLUDE_CODEGEN 1
#include "builtins_math.h"
#define LS_INCLUDE_CODEGEN 1
#include "builtins_perf.h"
#include "common.h"

#include <llvm-c/Core.h>
#include <llvm-c/Target.h>
#include <llvm-c/TargetMachine.h>
#include <llvm-c/Analysis.h>

#include <stdio.h>
#include <string.h>
#include <ctype.h>
/* File-local helpers (single-TU; re-static'd at codegen split §7). */
static LLVMValueRef cg_resolve_function_for_block(CodegenContext *ctx, AstNode *node, char *name_buf, size_t name_cap);

/* ---- Stage 7 (audit B-3): Block env poison checks -----------------------
   The refcount blinds memcheck to Block double-releases (the second release
   reads a freed rc, sees != 1 after heap reuse, and silently skips the free
   — "memcheck 双盲"). These debug-tier checks make detection STRONGER than
   the pre-refcount model: the rc slot is stamped with a poison value right
   before free(env), and every release/retain first traps on
     rc == POISON  → touching a freed env (use-after-release; both orders of
                     the classic double-release hit this on the second call)
     rc <  1       → over-release (a live env can never sit below 1)
   Gated like the memcheck allocator: emitted only under `--memcheck` (or
   any CG_DEBUG build) — the Release fast path is byte-identical. Poison is
   probabilistic once the freed block is recycled, but "release right after
   the freeing release" — the typical double-free timing — hits reliably.
   (Str/Vec cap sentinels deliberately NOT here: they conflict with the
   enum dead-tag sentinel contract (idempotency semantics unchanged —
   re-drop still routes to the synthesized __drop switch default and
   stays a silent no-op; only the on-disk representation of "already
   dropped" moved from a zeroed slot to tag == variant_count), deferred
   to stage 12 per plan.) */
#define CG_ENV_RC_POISON 0xDEADBEEFDEADBEEFULL

static bool cg_env_poison_on(CodegenContext *ctx)
{
#if CG_DEBUG
    (void)ctx;
    return true;
#else
    return ctx->memcheck_enabled;
#endif
}

/* Branch to a diagnostic+exit block when bad_cond holds; continues emission
   in the ok block. Mirrors cg_emit_bounds_guard's printf/__ls_proc_exit
   shape (runtime diagnostic, not CG_DEBUG tracing). */
static void cg_emit_env_poison_trap(CodegenContext *ctx, LLVMValueRef bad_cond,
                                    LLVMValueRef env_ptr, LLVMValueRef rc,
                                    const char *what)
{
    LLVMValueRef cur_fn = LLVMGetBasicBlockParent(LLVMGetInsertBlock(ctx->builder));
    LLVMBasicBlockRef bad = LLVMAppendBasicBlockInContext(ctx->context, cur_fn, "envp.bad");
    LLVMBasicBlockRef ok  = LLVMAppendBasicBlockInContext(ctx->context, cur_fn, "envp.ok");
    LLVMBuildCondBr(ctx->builder, bad_cond, bad, ok);
    LLVMPositionBuilderAtEnd(ctx->builder, bad);
    LLVMValueRef printf_fn = LLVMGetNamedFunction(ctx->module, "printf");
    if (printf_fn == NULL)
    {
        LLVMTypeRef pty = LLVMFunctionType(LLVMInt32TypeInContext(ctx->context),
            (LLVMTypeRef[]){ LLVMPointerTypeInContext(ctx->context, 0) }, 1, 1);
        printf_fn = LLVMAddFunction(ctx->module, "printf", pty);
    }
    char fmt[160];
    snprintf(fmt, sizeof fmt, "[block-poison] %s: env=%%p rc=%%lld\n", what);
    LLVMValueRef f = LLVMBuildGlobalStringPtr(ctx->builder, fmt, "envp.fmt");
    LLVMValueRef pa[3] = { f, env_ptr, rc };
    LLVMBuildCall2(ctx->builder, LLVMGlobalGetValueType(printf_fn), printf_fn,
                   pa, 3, "");
    LLVMTypeRef i32 = LLVMInt32TypeInContext(ctx->context);
    LLVMValueRef exit_fn = LLVMGetNamedFunction(ctx->module, "__ls_proc_exit");
    LLVMTypeRef exit_ty = LLVMFunctionType(LLVMVoidTypeInContext(ctx->context),
        (LLVMTypeRef[]){ i32 }, 1, 0);
    if (exit_fn == NULL)
        exit_fn = LLVMAddFunction(ctx->module, "__ls_proc_exit", exit_ty);
    LLVMBuildCall2(ctx->builder, exit_ty, exit_fn,
                   (LLVMValueRef[]){ LLVMConstInt(i32, 1, 0) }, 1, "");
    LLVMBuildUnreachable(ctx->builder);
    LLVMPositionBuilderAtEnd(ctx->builder, ok);
}

/* P1 (feat/block-ownership-unify): RELEASE one reference to an env.
   Emits: if (env != NULL) { if (--env->refcount == 0) { drop_fn?(env); free(env) } }
   The name stays cg_emit_block_env_drop so every existing drop site becomes a
   release site automatically. In the current single-owner model refcount is
   always 1 at this point, so "dec → 0 → free" is behaviour-equivalent to the
   old unconditional free (P1 keeps ctest identical); P2+ introduce shared
   references (refcount > 1) that this correctly keeps alive. */
void cg_emit_block_env_drop(CodegenContext *ctx, LLVMValueRef env_ptr)
{
    LLVMBasicBlockRef cur_bb = LLVMGetInsertBlock(ctx->builder);
    if (cur_bb == NULL) return;
    if (LLVMGetBasicBlockTerminator(cur_bb) != NULL) return;
    /* F.6: log block env release (runtime ptr). */
    cg_dbg_block_op(ctx, "env.release", "", env_ptr);
    LLVMValueRef cur_fn = LLVMGetBasicBlockParent(cur_bb);
    LLVMTypeRef ptr_t = LLVMPointerTypeInContext(ctx->context, 0);
    LLVMTypeRef i64_t = LLVMInt64TypeInContext(ctx->context);
    /* env header prefix { drop_fn@0, clone_fn@1, refcount@2 } — the refcount is
       at a fixed offset so this generic code needs no per-closure type. */
    LLVMTypeRef hdr_fields[3] = { ptr_t, ptr_t, i64_t };
    LLVMTypeRef hdr_t = LLVMStructTypeInContext(ctx->context, hdr_fields, 3, 0);

    LLVMValueRef is_nn = LLVMBuildICmp(ctx->builder, LLVMIntNE, env_ptr,
                                       LLVMConstNull(ptr_t), "rel.env.nn");
    LLVMBasicBlockRef dec_bb  = LLVMAppendBasicBlockInContext(ctx->context, cur_fn, "rel.dec");
    LLVMBasicBlockRef call_bb = LLVMAppendBasicBlockInContext(ctx->context, cur_fn, "rel.dropchk");
    LLVMBasicBlockRef df_bb   = LLVMAppendBasicBlockInContext(ctx->context, cur_fn, "rel.dropcall");
    LLVMBasicBlockRef do_bb   = LLVMAppendBasicBlockInContext(ctx->context, cur_fn, "rel.dofree");
    LLVMBasicBlockRef cont_bb = LLVMAppendBasicBlockInContext(ctx->context, cur_fn, "rel.cont");
    LLVMBuildCondBr(ctx->builder, is_nn, dec_bb, cont_bb);

    /* dec_bb: refcount -= 1; free only when it reaches 0. */
    LLVMPositionBuilderAtEnd(ctx->builder, dec_bb);
    LLVMValueRef rc_slot = LLVMBuildStructGEP2(ctx->builder, hdr_t, env_ptr, 2u, "rel.rcslot");
    LLVMValueRef rc  = LLVMBuildLoad2(ctx->builder, i64_t, rc_slot, "rel.rc");
    if (cg_env_poison_on(ctx))
    {
        /* Order matters: poison is a huge negative i64, so test it before
           the generic < 1 over-release check to get the precise message. */
        cg_emit_env_poison_trap(ctx,
            LLVMBuildICmp(ctx->builder, LLVMIntEQ, rc,
                          LLVMConstInt(i64_t, CG_ENV_RC_POISON, 0), "rel.uaf"),
            env_ptr, rc, "block env use-after-release (release)");
        cg_emit_env_poison_trap(ctx,
            LLVMBuildICmp(ctx->builder, LLVMIntSLT, rc,
                          LLVMConstInt(i64_t, 1, 0), "rel.over"),
            env_ptr, rc, "block env over-release");
    }
    LLVMValueRef rc1 = LLVMBuildSub(ctx->builder, rc, LLVMConstInt(i64_t, 1, 0), "rel.rc1");
    LLVMBuildStore(ctx->builder, rc1, rc_slot);
    LLVMValueRef is_zero = LLVMBuildICmp(ctx->builder, LLVMIntEQ, rc1,
                                         LLVMConstInt(i64_t, 0, 0), "rel.zero");
    LLVMBuildCondBr(ctx->builder, is_zero, call_bb, cont_bb);

    /* call_bb: dropped to 0 — run drop_fn (frees captured has_drop) if present. */
    LLVMPositionBuilderAtEnd(ctx->builder, call_bb);
    LLVMValueRef drop_fn_p = LLVMBuildLoad2(ctx->builder, ptr_t, env_ptr, "rel.drop"); /* drop_fn@0 */
    LLVMValueRef has_drop  = LLVMBuildICmp(ctx->builder, LLVMIntNE, drop_fn_p,
                                           LLVMConstNull(ptr_t), "rel.has_drop");
    LLVMBuildCondBr(ctx->builder, has_drop, df_bb, do_bb);
    LLVMPositionBuilderAtEnd(ctx->builder, df_bb);
    {
        LLVMTypeRef dp[1] = { ptr_t };
        LLVMTypeRef dft = LLVMFunctionType(LLVMVoidTypeInContext(ctx->context),
                                           dp, 1, 0);
        LLVMBuildCall2(ctx->builder, dft, drop_fn_p, &env_ptr, 1, "");
    }
    LLVMBuildBr(ctx->builder, do_bb);
    LLVMPositionBuilderAtEnd(ctx->builder, do_bb);
    /* Stage 7: stamp the rc slot BEFORE the free so any later touch of this
       env (release/retain) trips the poison traps above. */
    if (cg_env_poison_on(ctx))
        LLVMBuildStore(ctx->builder,
                       LLVMConstInt(i64_t, CG_ENV_RC_POISON, 0), rc_slot);
    cg_emit_free(ctx, env_ptr, "closure.env.tmp", CG_LINE(ctx), CG_COL(ctx));
    LLVMBuildBr(ctx->builder, cont_bb);
    LLVMPositionBuilderAtEnd(ctx->builder, cont_bb);
}

/* P2 (feat/block-ownership-unify): RETAIN one reference to an env
   (env->refcount += 1). A null env (capture-less closure) is a no-op. Mirror of
   the release in cg_emit_block_env_drop. Used everywhere a Block VALUE is copied
   into a new slot that will later be released — under reference semantics that
   copy shares the env rather than cloning/moving it. */
void cg_emit_block_env_retain(CodegenContext *ctx, LLVMValueRef env_ptr)
{
    LLVMBasicBlockRef cur_bb = LLVMGetInsertBlock(ctx->builder);
    if (cur_bb == NULL || LLVMGetBasicBlockTerminator(cur_bb) != NULL) return;
    cg_dbg_block_op(ctx, "env.retain", "", env_ptr);
    LLVMValueRef cur_fn = LLVMGetBasicBlockParent(cur_bb);
    LLVMTypeRef ptr_t = LLVMPointerTypeInContext(ctx->context, 0);
    LLVMTypeRef i64_t = LLVMInt64TypeInContext(ctx->context);
    LLVMTypeRef hdr_fields[3] = { ptr_t, ptr_t, i64_t };
    LLVMTypeRef hdr_t = LLVMStructTypeInContext(ctx->context, hdr_fields, 3, 0);
    LLVMValueRef is_nn = LLVMBuildICmp(ctx->builder, LLVMIntNE, env_ptr,
                                       LLVMConstNull(ptr_t), "ret.env.nn");
    LLVMBasicBlockRef inc_bb  = LLVMAppendBasicBlockInContext(ctx->context, cur_fn, "ret.inc");
    LLVMBasicBlockRef cont_bb = LLVMAppendBasicBlockInContext(ctx->context, cur_fn, "ret.cont");
    LLVMBuildCondBr(ctx->builder, is_nn, inc_bb, cont_bb);
    LLVMPositionBuilderAtEnd(ctx->builder, inc_bb);
    LLVMValueRef rc_slot = LLVMBuildStructGEP2(ctx->builder, hdr_t, env_ptr, 2u, "ret.rcslot");
    LLVMValueRef rc  = LLVMBuildLoad2(ctx->builder, i64_t, rc_slot, "ret.rc");
    if (cg_env_poison_on(ctx))
        cg_emit_env_poison_trap(ctx,
            LLVMBuildICmp(ctx->builder, LLVMIntEQ, rc,
                          LLVMConstInt(i64_t, CG_ENV_RC_POISON, 0), "ret.uaf"),
            env_ptr, rc, "block env use-after-release (retain)");
    LLVMValueRef rc1 = LLVMBuildAdd(ctx->builder, rc, LLVMConstInt(i64_t, 1, 0), "ret.rc1");
    LLVMBuildStore(ctx->builder, rc1, rc_slot);
    LLVMBuildBr(ctx->builder, cont_bb);
    LLVMPositionBuilderAtEnd(ctx->builder, cont_bb);
}

/* P2: retain the env of a Block VALUE (extract env field, retain). */
void cg_emit_block_retain_val(CodegenContext *ctx, LLVMValueRef block_val)
{
    LLVMBasicBlockRef cur_bb = LLVMGetInsertBlock(ctx->builder);
    if (cur_bb == NULL || LLVMGetBasicBlockTerminator(cur_bb) != NULL) return;
    LLVMValueRef env = LLVMBuildExtractValue(ctx->builder, block_val, 1, "rt.env");
    cg_emit_block_env_retain(ctx, env);
}

/* F.2: Drop the Block env stored at blk_alloca:
   load the Block, extract env_ptr, then call cg_emit_block_env_drop on it. */
void cg_emit_block_drop_at(CodegenContext *ctx, LLVMValueRef blk_alloca)
{
    LLVMBasicBlockRef cur_bb = LLVMGetInsertBlock(ctx->builder);
    if (cur_bb == NULL || LLVMGetBasicBlockTerminator(cur_bb) != NULL) return;
    LLVMTypeRef ptr_t = LLVMPointerTypeInContext(ctx->context, 0);
    LLVMTypeRef fields[2] = { ptr_t, ptr_t };
    LLVMTypeRef blk_t = LLVMStructTypeInContext(ctx->context, fields, 2, 0);
    LLVMValueRef blk_val = LLVMBuildLoad2(ctx->builder, blk_t, blk_alloca, "blk.old.load");
    LLVMValueRef env_ptr = LLVMBuildExtractValue(ctx->builder, blk_val, 1, "blk.old.env");
    cg_emit_block_env_drop(ctx, env_ptr);
}

/* F.2: Zero the env_ptr field (field 1) of the Block stored at blk_alloca.
   Called after moving a Block to another variable so scope cleanup skips this alloca. */
void cg_null_block_env(CodegenContext *ctx, LLVMValueRef blk_alloca)
{
    LLVMBasicBlockRef cur_bb = LLVMGetInsertBlock(ctx->builder);
    if (cur_bb == NULL || LLVMGetBasicBlockTerminator(cur_bb) != NULL) return;
    LLVMTypeRef ptr_t = LLVMPointerTypeInContext(ctx->context, 0);
    LLVMTypeRef fields[2] = { ptr_t, ptr_t };
    LLVMTypeRef blk_t = LLVMStructTypeInContext(ctx->context, fields, 2, 0);
#if CG_DEBUG
    {
        /* F.6: log block.move (source env being zeroed). */
        LLVMValueRef env_field_p = LLVMBuildStructGEP2(ctx->builder, blk_t, blk_alloca, 1, "blk.mv.ep");
        LLVMValueRef old_env = LLVMBuildLoad2(ctx->builder, ptr_t, env_field_p, "blk.mv.oldenv");
        cg_dbg_block_op(ctx, "move", "src->null", old_env);
    }
#endif
    LLVMValueRef env_field = LLVMBuildStructGEP2(ctx->builder, blk_t, blk_alloca, 1, "blk.mv.envf");
    LLVMBuildStore(ctx->builder, LLVMConstNull(ptr_t), env_field);
}

/* Phase G: deep-clone the env of a Block VALUE, producing a new LsBlock that
   owns an independent env. Used at copy-out sites (`Block g = vec[i]` /
   `struct.field` / `map.get(k)`) where the source LsBlock aliases an env still
   owned by the container — without cloning, both the new variable and the
   container element would free the same env on scope exit (double-free).

   The per-closure clone_fn lives at env field 1 (see codegen_closure_literal
   step 9a-clone). A NULL env (closure with no captures) is returned unchanged;
   when env != NULL, clone_fn is guaranteed non-NULL. Runtime shape:
       new_env = env ? ((ptr(*)(ptr))env[1])(env) : NULL
       result  = { blk.fn, new_env }                                       */
LLVMValueRef cg_emit_block_env_clone(CodegenContext *ctx,
                                     LLVMValueRef block_val)
{
    LLVMTypeRef ptr_t = LLVMPointerTypeInContext(ctx->context, 0);
    LLVMTypeRef i64_t = LLVMInt64TypeInContext(ctx->context);
    LLVMValueRef fn_ptr  = LLVMBuildExtractValue(ctx->builder, block_val, 0, "bc.fn");
    LLVMValueRef env_ptr = LLVMBuildExtractValue(ctx->builder, block_val, 1, "bc.env");

    LLVMValueRef cur_fn = LLVMGetBasicBlockParent(LLVMGetInsertBlock(ctx->builder));
    LLVMValueRef is_null = LLVMBuildICmp(ctx->builder, LLVMIntEQ, env_ptr,
                                         LLVMConstNull(ptr_t), "bc.isnull");
    LLVMBasicBlockRef from_bb  = LLVMGetInsertBlock(ctx->builder);
    LLVMBasicBlockRef clone_bb = LLVMAppendBasicBlockInContext(ctx->context, cur_fn, "bc.clone");
    LLVMBasicBlockRef cont_bb  = LLVMAppendBasicBlockInContext(ctx->context, cur_fn, "bc.cont");
    LLVMBuildCondBr(ctx->builder, is_null, cont_bb, clone_bb);

    /* clone_bb: load clone_fn from env[1] and call it. */
    LLVMPositionBuilderAtEnd(ctx->builder, clone_bb);
    LLVMValueRef idx1 = LLVMConstInt(i64_t, 1, 0);
    LLVMValueRef clone_slot = LLVMBuildInBoundsGEP2(ctx->builder, ptr_t, env_ptr,
                                                    &idx1, 1, "bc.cfslot");
    LLVMValueRef clone_fn = LLVMBuildLoad2(ctx->builder, ptr_t, clone_slot, "bc.cf");
    LLVMTypeRef cf_param[1] = { ptr_t };
    LLVMTypeRef cf_ty = LLVMFunctionType(ptr_t, cf_param, 1, 0);
    LLVMValueRef new_env = LLVMBuildCall2(ctx->builder, cf_ty, clone_fn, &env_ptr, 1, "bc.newenv");
    LLVMBasicBlockRef clone_end = LLVMGetInsertBlock(ctx->builder);
    LLVMBuildBr(ctx->builder, cont_bb);

    /* cont_bb: phi NULL (from null path) / new_env (from clone path). */
    LLVMPositionBuilderAtEnd(ctx->builder, cont_bb);
    LLVMValueRef phi = LLVMBuildPhi(ctx->builder, ptr_t, "bc.envphi");
    LLVMValueRef inc_vals[2] = { LLVMConstNull(ptr_t), new_env };
    LLVMBasicBlockRef inc_bbs[2] = { from_bb, clone_end };
    LLVMAddIncoming(phi, inc_vals, inc_bbs, 2);

    LLVMValueRef result = LLVMGetUndef(LLVMTypeOf(block_val));
    result = LLVMBuildInsertValue(ctx->builder, result, fn_ptr, 0, "bc.rfn");
    result = LLVMBuildInsertValue(ctx->builder, result, phi, 1, "bc.renv");
    cg_dbg_block_op(ctx, "clone", "env-deepcopy", phi);
    return result;
}

static LLVMValueRef cg_resolve_function_for_block(CodegenContext *ctx,
                                                  AstNode *node,
                                                  char *name_buf,
                                                  size_t name_cap)
{
    if (!node || !name_buf || name_cap == 0)
        return NULL;
    name_buf[0] = '\0';

    if (node->kind == AST_IDENT)
    {
        snprintf(name_buf, name_cap, "%s", node->as.ident.name);
        if (ctx->current_emit_module != NULL)
        {
            char mod_sym[512];
            cg_module_fn_symbol(mod_sym, sizeof(mod_sym),
                                ctx->current_emit_module, node->as.ident.name);
            LLVMValueRef mod_fn = LLVMGetNamedFunction(ctx->module, mod_sym);
            if (mod_fn)
            {
                snprintf(name_buf, name_cap, "%s", mod_sym);
                return mod_fn;
            }
            snprintf(name_buf, name_cap, "%s", mod_sym);
        }
        LLVMValueRef fn = LLVMGetNamedFunction(ctx->module, node->as.ident.name);
        if (fn)
        {
            snprintf(name_buf, name_cap, "%s", node->as.ident.name);
            return fn;
        }
    }
    else if (node->kind == AST_FIELD &&
             node->as.field_access.object &&
             node->as.field_access.object->resolved_type &&
             node->as.field_access.object->resolved_type->kind == TYPE_MODULE)
    {
        Type *mod_t = node->as.field_access.object->resolved_type;
        const char *field = node->as.field_access.field;
        if (mod_t->as.module.name)
        {
            cg_module_fn_symbol(name_buf, name_cap, mod_t->as.module.name, field);
            LLVMValueRef mod_fn = LLVMGetNamedFunction(ctx->module, name_buf);
            if (mod_fn)
                return mod_fn;
            LLVMValueRef bare_fn = LLVMGetNamedFunction(ctx->module, field);
            if (bare_fn)
            {
                snprintf(name_buf, name_cap, "%s", field);
                return bare_fn;
            }
        }
        else
        {
            snprintf(name_buf, name_cap, "%s", field);
            LLVMValueRef fn = LLVMGetNamedFunction(ctx->module, name_buf);
            if (fn)
                return fn;
        }
    }

    if (node->resolved_type && node->resolved_type->kind == TYPE_FUNCTION &&
        name_buf[0] != '\0')
    {
        LLVMTypeRef fn_type = type_to_llvm(ctx, node->resolved_type);
        return LLVMAddFunction(ctx->module, name_buf, fn_type);
    }
    return NULL;
}

LLVMValueRef codegen_fn_to_block(CodegenContext *ctx, AstNode *node)
{
    Type *block_t = node->coerce_block_type;
    Type *fn_t = node->resolved_type;
    if (!block_t || block_t->kind != TYPE_BLOCK ||
        !fn_t || fn_t->kind != TYPE_FUNCTION)
    {
        cg_error(ctx, node->line, node->column,
                 "internal: fn-to-Block coercion without matching types");
        return NULL;
    }

    char fn_name[512];
    LLVMValueRef target_fn = cg_resolve_function_for_block(ctx, node,
                                                           fn_name, sizeof(fn_name));
    if (!target_fn)
    {
        cg_error(ctx, node->line, node->column,
                 "undefined function '%s'", fn_name[0] ? fn_name : "<fn>");
        return NULL;
    }

    int n = block_t->as.function.param_count;
    LLVMTypeRef ptr_t = LLVMPointerTypeInContext(ctx->context, 0);
    LLVMTypeRef *params = (LLVMTypeRef *)malloc_safe(
        (size_t)(n + 1) * sizeof(LLVMTypeRef));
    params[0] = ptr_t;
    for (int i = 0; i < n; i++)
        params[i + 1] = type_to_llvm(ctx, block_t->as.function.params[i]);
    LLVMTypeRef ret_t = type_to_llvm(ctx, block_t->as.function.return_type);
    LLVMTypeRef thunk_type = LLVMFunctionType(ret_t, params, (unsigned)(n + 1), 0);
    free(params);

    char thunk_name[128];
    snprintf(thunk_name, sizeof(thunk_name), "__fnthunk_%d",
             ctx->closure_id_counter++);
    LLVMValueRef thunk = LLVMAddFunction(ctx->module, thunk_name, thunk_type);

    LLVMBasicBlockRef saved_bb = LLVMGetInsertBlock(ctx->builder);
    LLVMValueRef saved_fn = ctx->current_fn;
    Type *saved_ret = ctx->current_fn_return_type;

    LLVMBasicBlockRef entry =
        LLVMAppendBasicBlockInContext(ctx->context, thunk, "entry");
    LLVMPositionBuilderAtEnd(ctx->builder, entry);
    ctx->current_fn = thunk;
    ctx->current_fn_return_type = block_t->as.function.return_type;

    LLVMValueRef *call_args = NULL;
    if (n > 0)
    {
        call_args = (LLVMValueRef *)malloc_safe((size_t)n * sizeof(LLVMValueRef));
        for (int i = 0; i < n; i++)
            call_args[i] = LLVMGetParam(thunk, (unsigned)(i + 1));
    }

    LLVMTypeRef target_type = LLVMGlobalGetValueType(target_fn);
    bool void_ret = (LLVMGetTypeKind(ret_t) == LLVMVoidTypeKind);
    LLVMValueRef call = LLVMBuildCall2(ctx->builder, target_type, target_fn,
                                       call_args, (unsigned)n,
                                       void_ret ? "" : "fnthunk.call");
    free(call_args);
    if (void_ret)
        LLVMBuildRetVoid(ctx->builder);
    else
        LLVMBuildRet(ctx->builder, call);

    ctx->current_fn = saved_fn;
    ctx->current_fn_return_type = saved_ret;
    if (saved_bb)
        LLVMPositionBuilderAtEnd(ctx->builder, saved_bb);

    LLVMTypeRef block_llvm = type_to_llvm(ctx, block_t);
    LLVMValueRef result = LLVMGetUndef(block_llvm);
    result = LLVMBuildInsertValue(ctx->builder, result, thunk, 0, "f2b.fn");
    result = LLVMBuildInsertValue(ctx->builder, result,
                                  LLVMConstNull(ptr_t), 1, "f2b.env");
    return result;
}

static void cg_stmt_assign(CodegenContext *ctx, AstNode *node)
{

        /* Track temp string slots created during value evaluation. Also snapshot
           the has_drop temp COUNT: the string-count-based mark cannot tell a
           pre-statement registration (e.g. an enclosing match's owned-rvalue
           subject) from an RHS-created spill when no string temps intervene —
           flushing by mark would double-drop the subject. */
        int assign_drop_floor = ctx->temp_drop_count;
        LLVMValueRef val = codegen_expr(ctx, node->as.assign.value);
        if (val == NULL)
            return;

        /* Implicit numeric widening on assignment: rhs may be a smaller
           numeric type that widens to the target's type. */
        {
            Type *as_tgt_t = node->as.assign.target->resolved_type;
            Type *as_rhs_t = node->as.assign.value->resolved_type;
            if (as_tgt_t && as_rhs_t && type_is_numeric(as_tgt_t) &&
                type_is_numeric(as_rhs_t) && !type_equals(as_tgt_t, as_rhs_t) &&
                node->as.assign.op == TOKEN_ASSIGN)
            {
                val = cg_widen(ctx, val, as_rhs_t, as_tgt_t);
            }
        }

        /* Simple variable assignment */
        if (node->as.assign.target->kind == AST_IDENT)
        {
            CgSymbol *sym = cg_scope_resolve(ctx->current_scope,
                                             node->as.assign.target->as.ident.name);
            if (sym == NULL)
            {
                cg_error(ctx, node->line, node->column, "undefined variable in assignment");
                return;
            }

            if (node->as.assign.op == TOKEN_ASSIGN)
            {
                if (sym->type && sym->type->kind == TYPE_STRUCT &&
                         sym->type->as.strukt.has_drop)
                {
                    /* Struct-with-drop assignment:
                       1. Drop the old value currently in sym (frees its owned strings etc.)
                       2. If source is another variable (IDENT), deep-clone it so both dst
                          and src have independently owned copies.
                       3. Store (cloned or fresh) value.
                       If dst is a vec loop var, sym->moved_flag (borrowed_flag) is 1 at
                       runtime, so emit_struct_drop_cond is a no-op for the borrowed data. */

                    /* Step 1: drop old value (runtime-conditional via moved_flag) */
                    emit_struct_drop_cond(ctx, sym->value, sym->type, sym->moved_flag);

                    /* Step 2: clone if source is a shared IDENT. Move-elision (Q4):
                       if the checker tagged the source moved_out (ownership truly
                       transferred, later use rejected) we skip the clone and
                       invalidate the source instead. */
                    if (ast_unwrap_move(node->as.assign.value)->kind == AST_IDENT)
                    {
                        if (ast_unwrap_move(node->as.assign.value)->moved_out &&
                            cg_invalidate_moved_source(ctx, node->as.assign.value, sym->type))
                        {
                            /* moved + invalidated (real owned source) — no clone.
                               Borrow source → false → clone below. */
                        }
                        else
                        {
                            LLVMTypeRef llvm_st = type_to_llvm(ctx, sym->type);
                            val = emit_struct_clone_val(ctx, val, llvm_st, sym->type);
                        }
                    }

                    /* Step 3: store */
                    LLVMBuildStore(ctx->builder, val, sym->value);

                    /* After assignment the variable is alive again — clear moved_flag */
                    if (sym->moved_flag)
                    {
                        LLVMTypeRef i1 = LLVMInt1TypeInContext(ctx->context);
                        LLVMBuildStore(ctx->builder, LLVMConstInt(i1, 0, 0), sym->moved_flag);
                    }
                    /* Drop the has_drop temps the RHS registered (e.g. the f-string
                       Str spilled for `s = s + f"..."`): deferring to scope end
                       leaks all but the last loop iteration (the entry alloca is
                       reused). Use the temp_drop COUNT floor, NOT cg_flush_temps's
                       string-count mark — a mark-based flush also catches an
                       enclosing match's owned-rvalue subject registered before this
                       statement (same mark when no string temps intervene) and
                       double-drops it. The stored result value itself is never
                       temp_drop-registered (var_decl invariant). */
                    for (int ti = assign_drop_floor; ti < ctx->temp_drop_count; ti++)
                    {
                        Type *tt = ctx->temp_drop_types[ti];
                        if (tt->kind == TYPE_STRUCT)
                            emit_struct_drop(ctx, ctx->temp_drop_slots[ti], tt);
                        else if (tt->kind == TYPE_ENUM)
                            emit_enum_drop(ctx, ctx->temp_drop_slots[ti], tt);
                    }
                    ctx->temp_drop_count = assign_drop_floor;
                    /* String temps from the RHS are garbage here (result is a
                       struct): free them as before. */
                }
                else if (sym->type && sym->type->kind == TYPE_ENUM &&
                         sym->type->as.enom.has_drop)
                {
                    /* has_drop enum assignment:
                       1. Drop old value (runtime-conditional via moved_flag)
                       2. Clone if source is an IDENT (shared variable/borrow)
                       3. Store (cloned or fresh) value
                       4. Clear moved_flag so scope cleanup works */
                    if (sym->moved_flag)
                    {
                        LLVMTypeRef i1_t = LLVMInt1TypeInContext(ctx->context);
                        LLVMValueRef cur_fn = LLVMGetBasicBlockParent(
                            LLVMGetInsertBlock(ctx->builder));
                        LLVMBasicBlockRef do_bb = LLVMAppendBasicBlockInContext(
                            ctx->context, cur_fn, "assign.edrop");
                        LLVMBasicBlockRef cont_bb = LLVMAppendBasicBlockInContext(
                            ctx->context, cur_fn, "assign.econt");
                        LLVMValueRef is_moved = LLVMBuildLoad2(ctx->builder,
                            i1_t, sym->moved_flag, "e.moved");
                        LLVMBuildCondBr(ctx->builder, is_moved, cont_bb, do_bb);
                        LLVMPositionBuilderAtEnd(ctx->builder, do_bb);
                        emit_enum_drop(ctx, sym->value, sym->type);
                        LLVMBuildBr(ctx->builder, cont_bb);
                        LLVMPositionBuilderAtEnd(ctx->builder, cont_bb);
                    }
                    else
                    {
                        emit_enum_drop(ctx, sym->value, sym->type);
                    }
                    if (ast_unwrap_move(node->as.assign.value)->kind == AST_IDENT)
                    {
                        /* Move-elision (Q4): skip the clone when the checker
                           confirmed ownership transfer AND the source is a real
                           owned variable (helper returns true). Borrow sources
                           return false → clone. */
                        if (ast_unwrap_move(node->as.assign.value)->moved_out &&
                            cg_invalidate_moved_source(ctx, node->as.assign.value, sym->type))
                        {
                            /* moved + invalidated — no clone */
                        }
                        else
                            val = emit_enum_clone_val(ctx, val, sym->type);
                    }
                    LLVMBuildStore(ctx->builder, val, sym->value);
                    if (sym->moved_flag)
                    {
                        LLVMBuildStore(ctx->builder,
                            LLVMConstInt(LLVMInt1TypeInContext(ctx->context), 0, 0),
                            sym->moved_flag);
                    }
                    /* Same as the has_drop struct branch above: drop RHS-registered
                       has_drop temps by COUNT floor (not mark — see that branch),
                       then free RHS string temps. */
                    for (int ti = assign_drop_floor; ti < ctx->temp_drop_count; ti++)
                    {
                        Type *tt = ctx->temp_drop_types[ti];
                        if (tt->kind == TYPE_STRUCT)
                            emit_struct_drop(ctx, ctx->temp_drop_slots[ti], tt);
                        else if (tt->kind == TYPE_ENUM)
                            emit_enum_drop(ctx, ctx->temp_drop_slots[ti], tt);
                    }
                    ctx->temp_drop_count = assign_drop_floor;
                }
                else if (sym->type && sym->type->kind == TYPE_BLOCK)
                {
                    /* F.2: Block assignment — move semantics.
                       1. Drop old env in destination (if any).
                       2. Store new Block value.
                       3. If source is an owned IDENT, zero its env_ptr. */
                    cg_emit_block_drop_at(ctx, sym->value);
                    AstNode *rhs_node = ast_unwrap_move(node->as.assign.value);
                    if (cg_block_source_is_aliased(rhs_node))
                    {
                        /* Phase G: Block copied out of a vec/struct/map — deep-clone
                           the aliased env before storing so dst owns an independent
                           one (no shared-env double-free). */
                        val = cg_emit_block_env_clone(ctx, val);
                    }
                    LLVMBuildStore(ctx->builder, val, sym->value);
                    if (rhs_node->kind == AST_IDENT)
                    {
                        CgSymbol *src = cg_scope_resolve(ctx->current_scope,
                                                          rhs_node->as.ident.name);
                        if (src && src->no_drop_reason == CG_OWNED)
                            cg_null_block_env(ctx, src->value);
                    }
                    else if (!cg_block_source_is_aliased(rhs_node))
                    {
                        /* Fresh rvalue on RHS — dst takes its single reference, so
                           the statement flush must not also release it. Factory
                           call / force-unwrap (P3) and closure literal (stage 10)
                           are both temp_drop-tracked now: claim the entry. */
                        cg_claim_block_temp_above(ctx, assign_drop_floor);
                    }
                }
                else
                {
                    /* POD / bool / int / float / char / ptr / object target.
                       The store never takes ownership of a heap string, so any
                       string temps created while evaluating the RHS (e.g. a
                       render() call buried in `ok = check(render(x)==..., n)`)
                       are pure garbage and MUST be freed here — not merely
                       discarded by resetting the count, which leaked them.
                       Free ONLY the string temps (not temp-drops / block-envs:
                       those has_drop temporaries are released by the existing
                       statement-boundary machinery, and double-flushing them
                       here would double-free, e.g. by-value struct args in
                       `acc = f(x) && f(x) && acc`). */
                    LLVMBuildStore(ctx->builder, val, sym->value);
                    /* P3 (block-refcount): a Block rvalue discarded in the RHS
                       (`n = n + make()()`) was tracked as a temp_drop. The struct
                       concern above is about STRUCT temps — release only TYPE_BLOCK
                       ones here, at THIS statement's boundary. Per-iteration matters
                       in a loop: the block-temp slot is reused, so deferring to the
                       post-loop flush would release only the last iteration's env
                       and leak the rest.
                       Stage 10 re-derivation (single-ledger world): KEEP. This is
                       the ONLY statement-boundary Block release for a POD ident
                       assign — this AST_ASSIGN path never calls cg_flush_temps
                       (struct/enum temps are deliberately deferred, see above).
                       Unification WIDENED its coverage to closure literals
                       (`n = n + (|| 5)()`), which is a fix, not a regression: the
                       old env-table entry for that shape waited for whatever flush
                       came next, and an AST_IF count-restore could discard it
                       without a release (leak) if the assign closed a branch. */
                    for (int ti = ctx->temp_drop_count - 1; ti >= assign_drop_floor; ti--)
                        if (ctx->temp_drop_types[ti]->kind == TYPE_BLOCK)
                        {
                            cg_emit_block_drop_at(ctx, ctx->temp_drop_slots[ti]);
                            cg_remove_temp_drop(ctx, ctx->temp_drop_slots[ti]);
                        }
                }
            }
            else
            {

                /* Compound assignment: load current, operate, store */
                LLVMTypeRef lt = type_to_llvm(ctx, sym->type);
                LLVMValueRef current = LLVMBuildLoad2(ctx->builder, lt, sym->value, "cur");
                bool is_fp = sym->type && type_is_float(sym->type);
                LLVMValueRef result = NULL;

                switch (node->as.assign.op)
                {
                case TOKEN_PLUS_ASSIGN:
                    result = is_fp ? cg_fp_contract(LLVMBuildFAdd(ctx->builder, current, val, "fadd"))
                                   : LLVMBuildAdd(ctx->builder, current, val, "add");
                    break;
                case TOKEN_MINUS_ASSIGN:
                    result = is_fp ? cg_fp_contract(LLVMBuildFSub(ctx->builder, current, val, "fsub"))
                                   : LLVMBuildSub(ctx->builder, current, val, "sub");
                    break;
                case TOKEN_STAR_ASSIGN:
                    result = is_fp ? cg_fp_contract(LLVMBuildFMul(ctx->builder, current, val, "fmul"))
                                   : LLVMBuildMul(ctx->builder, current, val, "mul");
                    break;
                case TOKEN_SLASH_ASSIGN:
                    result = is_fp ? cg_fp_contract(LLVMBuildFDiv(ctx->builder, current, val, "fdiv"))
                                   : LLVMBuildSDiv(ctx->builder, current, val, "sdiv");
                    break;
                default:
                    break;
                }
                if (result)
                    LLVMBuildStore(ctx->builder, result, sym->value);
            }
        }
        else if (node->as.assign.target->kind == AST_FIELD)
        {
            /* Struct field assignment — use codegen_lvalue_ptr to handle nested access
               (e.g. p1.s.k = expr) which the old IDENT-only path could not reach */
            LLVMValueRef field_ptr = codegen_lvalue_ptr(ctx, node->as.assign.target);
            if (field_ptr != NULL)
            {
                Type *field_type = node->as.assign.target->resolved_type;
                if (cg_type_owns_heap_for_enum(field_type))
                {
                    /* has_drop field (vec/map/has_drop struct|enum): drop the old
                       value, then store an independently-owned value. Clone if the
                       source is a shared IDENT so field and source stay valid;
                       move-elision (Q4) skips the clone when the checker confirmed
                       ownership transfer (moved_out) and invalidates the source. */
                    emit_drop_value(ctx, field_ptr, field_type);
                    if (ast_unwrap_move(node->as.assign.value)->kind == AST_IDENT)
                    {
                        if (ast_unwrap_move(node->as.assign.value)->moved_out &&
                            cg_invalidate_moved_source(ctx, node->as.assign.value, field_type))
                        {
                            /* moved + invalidated (real owned source) — no clone.
                               Borrow source → false → clone below. */
                        }
                        else if (field_type->kind == TYPE_BLOCK)
                        {
                            /* Closure field from a shared IDENT: deep-clone the
                               env so field and source free independently. */
                            val = cg_emit_block_env_clone(ctx, val);
                        }
                        else
                        {
                            LLVMTypeRef flt = type_to_llvm(ctx, field_type);
                            val = emit_clone_value(ctx, val, flt, field_type);
                        }
                    }
                    LLVMBuildStore(ctx->builder, val, field_ptr);
                    cg_flush_temps(ctx);
                }
                else
                {
                    LLVMBuildStore(ctx->builder, val, field_ptr);
                }
            }
            else
            {
            }
        }
        else if (node->as.assign.target->kind == AST_INDEX)
        {
            /* arr[i] = val or v[i] = val */
            AstNode *target = node->as.assign.target;
            AstNode *obj = target->as.index_expr.object;
            Type *obj_type = obj->resolved_type;

            if (obj_type && obj_type->kind == TYPE_SLICE)
            {
                /* s[i] = val on a writable slice — bounds-checked store into the
                   borrowed range (POD elements; has_drop deferred by the checker). */
                LLVMValueRef sv = codegen_expr(ctx, obj);
                LLVMValueRef sptr = LLVMBuildExtractValue(ctx->builder, sv, 0, "ss.ptr");
                LLVMValueRef slen = LLVMBuildExtractValue(ctx->builder, sv, 1, "ss.len");
                LLVMTypeRef i64t = LLVMInt64TypeInContext(ctx->context);
                LLVMValueRef index = codegen_expr(ctx, target->as.index_expr.index);
                if (index == NULL) return;
                if (LLVMTypeOf(index) != i64t)
                    index = LLVMBuildSExtOrBitCast(ctx->builder, index, i64t, "ss.idx");
                LLVMValueRef ge = LLVMBuildICmp(ctx->builder, LLVMIntSGE, index,
                                                LLVMConstInt(i64t, 0, 0), "ss.ge");
                LLVMValueRef lt = LLVMBuildICmp(ctx->builder, LLVMIntSLT, index, slen, "ss.lt");
                cg_emit_bounds_guard(ctx, LLVMBuildAnd(ctx->builder, ge, lt, "ss.ok"),
                                     "Slice index out of bounds", node->line, node->column);
                Type *elem_ty = obj_type->as.array.elem;
                LLVMTypeRef elem_llvm = type_to_llvm(ctx, elem_ty);
                LLVMValueRef gep = LLVMBuildGEP2(ctx->builder, elem_llvm, sptr, &index, 1, "ss.ep");
                /* The slot holds a live element — drop the old value before moving
                   the new one in (has_drop elements), mirroring vec[i]=. POD: the
                   drop is a no-op and cg_store_owned is a plain store. */
                bool elem_has_drop = elem_ty &&
                    ((elem_ty->kind == TYPE_STRUCT && elem_ty->as.strukt.has_drop) ||
                     (elem_ty->kind == TYPE_ENUM && elem_ty->as.enom.has_drop));
                if (elem_has_drop)
                    emit_drop_value(ctx, gep, elem_ty);
                cg_store_owned(ctx, gep, val, elem_ty, node->as.assign.value);
                cg_flush_temps(ctx);
            }
            else if (obj_type && obj_type->kind == TYPE_POINTER && obj_type->as.pointer_to)
            {
                /* p[i] = val on a raw *T pointer - RAW store: typed-GEP the
                   element address and store. Does NOT drop the old slot (it may
                   be uninitialized memory - the unsafe-primitive contract; cf.
                   vec/array which drop the old element). Ownership of `val` is
                   still transferred via cg_store_owned (owned temp marked moved /
                   borrowed cloned) so a has_drop value isn't double-freed. */
                LLVMValueRef ptr_val = codegen_expr(ctx, obj);
                if (ptr_val == NULL)
                    return;
                LLVMTypeRef elem_llvm = type_to_llvm(ctx, obj_type->as.pointer_to);
                LLVMTypeRef i64_type = LLVMInt64TypeInContext(ctx->context);
                LLVMValueRef index = codegen_expr(ctx, target->as.index_expr.index);
                if (index == NULL)
                    return;
                if (LLVMTypeOf(index) != i64_type)
                    index = LLVMBuildSExtOrBitCast(ctx->builder, index, i64_type, "pis.idx");
                LLVMValueRef gep = LLVMBuildGEP2(ctx->builder, elem_llvm, ptr_val,
                                                 &index, 1, "pis.ep");
                cg_store_owned(ctx, gep, val, obj_type->as.pointer_to,
                               node->as.assign.value);
                cg_flush_temps(ctx);
            }
            else
            {
                /* fixed array[i] = val */
                if (obj_type == NULL || obj_type->kind != TYPE_ARRAY)
                    return;

                LLVMValueRef arr_ptr = NULL;
                if (obj->kind == AST_IDENT)
                {
                    CgSymbol *sym = cg_scope_resolve(ctx->current_scope, obj->as.ident.name);
                    if (sym)
                        arr_ptr = sym->value;
                }
                if (arr_ptr == NULL)
                    return;

                LLVMValueRef index = codegen_expr(ctx, target->as.index_expr.index);
                if (index == NULL)
                    return;

                LLVMTypeRef i64_type = LLVMInt64TypeInContext(ctx->context);
                if (LLVMTypeOf(index) != i64_type)
                {
                    index = LLVMBuildSExtOrBitCast(ctx->builder, index, i64_type, "idx.ext");
                }

                LLVMTypeRef arr_llvm = type_to_llvm(ctx, obj_type);
                LLVMValueRef zero = LLVMConstInt(i64_type, 0, 0);
                LLVMValueRef indices[2] = {zero, index};
                LLVMValueRef gep = LLVMBuildGEP2(ctx->builder, arr_llvm, arr_ptr,
                                                 indices, 2, "arr.store");

                LLVMBuildStore(ctx->builder, val, gep);
            }
        }
        else if (node->as.assign.target->kind == AST_UNARY &&
                 node->as.assign.target->as.unary.op == TOKEN_STAR)
        {
            /* *ptr = val — must drop old pointed-to value before overwriting */
            LLVMValueRef ptr = codegen_expr(ctx, node->as.assign.target->as.unary.operand);
            if (ptr == NULL)
                return;

            /* target->resolved_type is T (the pointed-to type, not *T) */
            Type *target_type = node->as.assign.target->resolved_type;

            if (target_type && target_type->kind == TYPE_STRUCT &&
                     target_type->as.strukt.has_drop)
            {
                /* Drop old struct at *ptr (no moved_flag — heap-allocated, no scope cleanup) */
                emit_struct_drop_cond(ctx, ptr, target_type, NULL);
                /* Clone if RHS is a named struct variable (unwrap __move). */
                if (ast_unwrap_move(node->as.assign.value)->kind == AST_IDENT)
                {
                    LLVMTypeRef llvm_st = type_to_llvm(ctx, target_type);
                    val = emit_struct_clone_val(ctx, val, llvm_st, target_type);
                }
                LLVMBuildStore(ctx->builder, val, ptr);
            }
            else
            {
                /* Trivial type: just store */
                LLVMBuildStore(ctx->builder, val, ptr);
            }
        }
        return;
}

static void cg_stmt_var_decl(CodegenContext *ctx, AstNode *node)
{
        Type *var_type = node->resolved_type;
        if (var_type == NULL)
            return;

        /* Phase 1 (borrow extension): a named local borrow `&T r = &x` aliases
           the referent's storage pointer (no alloca, no copy) — exactly like a
           borrow parameter. Register the symbol with that pointer + CG_BORROWED
           so scope cleanup leaves the referent's ownership untouched. The
           checker (check_local_borrow_decl) already validated the source. */
        if (var_type->kind == TYPE_REFERENCE)
        {
            Type *pointee = var_type->as.pointer_to;
            AstNode *init = node->as.var_decl.init;
            LLVMValueRef ptr = NULL;
            /* Phase 2: borrow-returning call init `&T r = obj.get()` — the call
               evaluates to the borrow pointer directly. */
            if (init && init->kind == AST_CALL &&
                init->resolved_type && init->resolved_type->kind == TYPE_REFERENCE)
            {
                ptr = codegen_expr(ctx, init);
            }
            else
            {
                AstNode *src_ident = NULL;
                if (init && init->kind == AST_UNARY && init->as.unary.op == TOKEN_AMP)
                    src_ident = init->as.unary.operand;     /* &x */
                else if (init && init->kind == AST_MUT_BORROW)
                    src_ident = init->as.mut_borrow.operand; /* &!x */
                else if (init && init->kind == AST_IDENT)
                    src_ident = init;                        /* re-borrow r1 */
                if (src_ident == NULL)
                    return;
                ptr = codegen_lvalue_ptr(ctx, src_ident);
            }
            if (ptr == NULL)
                return;
            CgSymbol *bsym = cg_scope_define(ctx->current_scope,
                                             node->as.var_decl.name, ptr,
                                             pointee, NULL);
            if (bsym)
            {
                bsym->no_drop_reason = CG_BORROWED;  /* skip scope cleanup (referent owns) */
                if (var_type->is_mut) bsym->is_mut_borrow = true;
            }
            return;
        }

        LLVMTypeRef llvm_type = type_to_llvm(ctx, var_type);

        /* Alloca at function entry */
        LLVMBasicBlockRef entry = LLVMGetEntryBasicBlock(ctx->current_fn);
        LLVMBuilderRef tmp = LLVMCreateBuilderInContext(ctx->context);
        LLVMValueRef first_inst = LLVMGetFirstInstruction(entry);
        if (first_inst)
            LLVMPositionBuilderBefore(tmp, first_inst);
        else
            LLVMPositionBuilderAtEnd(tmp, entry);
        LLVMValueRef alloca = LLVMBuildAlloca(tmp, llvm_type, node->as.var_decl.name);
        LLVMDisposeBuilder(tmp);

        /* A2: mark this slot's lexical live range so StackColoring can reuse the
           frame slot once the scope exits. Emitted at the var_decl position (not
           the hoisted entry alloca) for the tightest range, BEFORE any zero-init
           store below. Only aggregate AOT locals qualify; the returned flag is
           stamped onto the CgSymbol so scope cleanup emits the paired end. */
        bool var_lifetime_marked = cg_emit_lifetime_start(ctx, alloca, llvm_type);

        /* F5: a Block local initialized from a RAW ptr/array index read
           (`V v = self.vals[idx]` inside a pure-LS container method) holds an
           ALIAS of an env the container owns — the alias-through read protocol
           never clones those. Mark the symbol borrowed (set below) so scope
           cleanup skips it and downstream ownership boundaries (enum ctor
           payload store) know to deep-clone. */
        bool block_alias_init = false;

        /* Allocate moved_flag for struct-with-drop and has_drop enum types.
           F.5: enum with heap payload also needs move tracking so closure
           by-move capture can prevent double-free via scope cleanup. */
        LLVMValueRef moved_flag = NULL;
        if ((var_type->kind == TYPE_STRUCT && var_type->as.strukt.has_drop) ||
            (var_type->kind == TYPE_ENUM   && var_type->as.enom.has_drop))
        {
            LLVMTypeRef i1_type = LLVMInt1TypeInContext(ctx->context);
            moved_flag = cg_entry_alloca(ctx, i1_type, "var.moved");
            LLVMBuildStore(ctx->builder, LLVMConstInt(i1_type, 0, 0), moved_flag);
        }

        /* Zero-initialize arrays that contain strings or droppable structs.
           Without this, unassigned array elements have garbage cap/data values,
           causing emit_cleanup_to to call free() on invalid pointers at scope exit. */
        if (var_type->kind == TYPE_ARRAY && var_type->as.array.elem)
        {
            Type *elem = var_type->as.array.elem;
            bool needs_zero_init =
                (elem->kind == TYPE_STRUCT && elem->as.strukt.has_drop);
            if (needs_zero_init)
            {
                LLVMBuildStore(ctx->builder, LLVMConstNull(llvm_type), alloca);
            }
        }
        /* Zero-initialize structs-with-drop so that any string/nested-struct fields
           start with cap=0/data=NULL. Without this, an uninitialized struct pushed into
           a vec would have garbage cap values → free() on garbage pointer on scope exit. */
        if (var_type->kind == TYPE_STRUCT && var_type->as.strukt.has_drop)
        {
            LLVMBuildStore(ctx->builder, LLVMConstNull(llvm_type), alloca);
        }
        /* Zero-initialize has-drop enum variables (disc=0, payload=zeroed).
           Without this, an uninitialized enum has garbage discriminant + payload,
           causing emit_auto_enum_drop_fn to access a wild pointer on scope exit. */
        if (var_type->kind == TYPE_ENUM && var_type->as.enom.has_drop)
        {
            LLVMBuildStore(ctx->builder, LLVMConstNull(llvm_type), alloca);
        }
        if (node->as.var_decl.init)
        {
            /* Track temp slots created during init expression evaluation */

            /* Collection-literal init for a user container (the `__from_list`
               protocol, checked above): zero-init the struct, then call its
               reserved `__from_list(&!self, E)` method for each element. Matches
               the builtin `vec(T) v = [..]`. */
            if (var_type->kind == TYPE_STRUCT &&
                     node->as.var_decl.init->kind == AST_ARRAY_LIT)
            {
                AstNode *lit = node->as.var_decl.init;
                LLVMBuildStore(ctx->builder, LLVMConstNull(llvm_type), alloca);
                char *fl_name = mangle_method_symbol(
                    struct_llvm_name(var_type), NULL, "__from_list"); /* Task 7.2 */
                LLVMValueRef fl_fn = LLVMGetNamedFunction(ctx->module, fl_name);
                if (fl_fn == NULL)
                {
                    /* F6 (sibling of VR-LIM-016/F1): a local `Vec(T) v = [..]`
                       inside an IMPORTED-module function is emitted before the
                       G1.5 pending-generic pass, so Vec(T).__from_list may not
                       exist yet. Silently skipping the loop left the Vec
                       zero-initialized (len=0, data=null → reads/crashes in the
                       consumer). Forward-declare from the pending queue; the body
                       lands in G1.5. Mirrors emit_user_from_list_value (F1). */
                    fl_fn = cg_declare_pending_generic_method(ctx, fl_name);
                }
                free(fl_name);
                if (fl_fn)
                {
                    LLVMTypeRef fl_ft = LLVMGlobalGetValueType(fl_fn);
                    for (int i = 0; i < lit->as.array_lit.count; i++)
                    {
                        LLVMValueRef ev = codegen_expr(ctx, lit->as.array_lit.elements[i]);
                        if (ev == NULL) continue;
                        LLVMValueRef fl_args[2] = { alloca, ev };
                        LLVMBuildCall2(ctx->builder, fl_ft, fl_fn, fl_args, 2, "");
                    }
                }
            }
            /* M-LIT: key-value literal init for a user container (the
               `__from_pairs` protocol, checked above): zero-init the struct, then
               call `__from_pairs(&!self, K, V)` per pair, moving each key/value in
               (owned-param ABI, mirror __from_list). e.g. `Map(K,V) m = {k: v}`. */
            else if (var_type->kind == TYPE_STRUCT &&
                     node->as.var_decl.init->kind == AST_MAP_LIT &&
                     node->as.var_decl.init->as.map_lit.pair_count > 0)
            {
                AstNode *ml = node->as.var_decl.init;
                LLVMBuildStore(ctx->builder, LLVMConstNull(llvm_type), alloca);
                char *fp_name = mangle_method_symbol(
                    struct_llvm_name(var_type), NULL, "__from_pairs"); /* Task 7.2 */
                LLVMValueRef fp_fn = LLVMGetNamedFunction(ctx->module, fp_name);
                if (fp_fn == NULL)
                    fp_fn = cg_declare_pending_generic_method(ctx, fp_name);
                free(fp_name);
                if (fp_fn)
                {
                    LLVMTypeRef fp_ft = LLVMGlobalGetValueType(fp_fn);
                    for (int i = 0; i < ml->as.map_lit.pair_count; i++)
                    {
                        LLVMValueRef kv = codegen_expr(ctx, ml->as.map_lit.keys[i]);
                        if (kv == NULL) continue;
                        LLVMValueRef vv = codegen_expr(ctx, ml->as.map_lit.vals[i]);
                        if (vv == NULL) continue;
                        LLVMValueRef fp_args[3] = { alloca, kv, vv };
                        LLVMBuildCall2(ctx->builder, fp_ft, fp_fn, fp_args, 3, "");
                    }
                }
            }
            /* Special handling for array literal initialization */
            else if (var_type->kind == TYPE_ARRAY &&
                     node->as.var_decl.init->kind == AST_ARRAY_LIT)
            {
                AstNode *lit = node->as.var_decl.init;
                int count = lit->as.array_lit.count;
                LLVMTypeRef i64_type = LLVMInt64TypeInContext(ctx->context);
                LLVMValueRef zero = LLVMConstInt(i64_type, 0, 0);

                /* Try constant array first */
                LLVMValueRef const_arr = codegen_expr(ctx, lit);
                if (const_arr)
                {
                    LLVMBuildStore(ctx->builder, const_arr, alloca);
                }
                else
                {
                    /* Element-by-element store */
                    for (int i = 0; i < count; i++)
                    {
                        LLVMValueRef elem_val = codegen_expr(ctx, lit->as.array_lit.elements[i]);
                        if (elem_val == NULL)
                            continue;
                        LLVMValueRef idx = LLVMConstInt(i64_type, (uint64_t)i, 0);
                        LLVMValueRef indices[2] = {zero, idx};
                        LLVMValueRef gep = LLVMBuildGEP2(ctx->builder, llvm_type,
                                                         alloca, indices, 2, "arr.init");
                        LLVMBuildStore(ctx->builder, elem_val, gep);
                    }
                }
            }
            else
            {
                int blk_drop_floor = ctx->temp_drop_count;
                LLVMValueRef init = codegen_expr(ctx, node->as.var_decl.init);
                if (init)
                {
                    /* Implicit numeric widening: var_decl init expr's type may
                       differ from var_type (e.g. f64 x = 5_int). The checker
                       allowed this iff type_widens_to(init_t, var_type). */
                    Type *init_t = node->as.var_decl.init->resolved_type;
                    if (init_t && type_is_numeric(init_t) &&
                        type_is_numeric(var_type) && !type_equals(init_t, var_type))
                    {
                        init = cg_widen(ctx, init, init_t, var_type);
                    }
                    if (var_type->kind == TYPE_STRUCT &&
                             var_type->as.strukt.has_drop &&
                             ast_unwrap_move(node->as.var_decl.init)->kind == AST_IDENT)
                    {
                        if (ast_unwrap_move(node->as.var_decl.init)->moved_out &&
                            cg_invalidate_moved_source(ctx, node->as.var_decl.init, var_type))
                        {
                            /* Move-elision (Q4): ownership transferred and source
                               was a real owned variable — moved + invalidated, no
                               clone. Borrow sources return false → fall to clone. */
                        }
                        else
                        {
                            /* Source is another named struct variable — deep-clone so both
                               this variable and the source have independently owned string
                               fields and can be freed without double-free. */
                            LLVMTypeRef llvm_st = type_to_llvm(ctx, var_type);
                            init = emit_struct_clone_val(ctx, init, llvm_st, var_type);
                        }
                    }
                    else if (var_type->kind == TYPE_ENUM &&
                             var_type->as.enom.has_drop &&
                             ast_unwrap_move(node->as.var_decl.init)->kind == AST_IDENT)
                    {
                        if (ast_unwrap_move(node->as.var_decl.init)->moved_out &&
                            cg_invalidate_moved_source(ctx, node->as.var_decl.init, var_type))
                        {
                            /* Move-elision (Q4): ownership transferred and source
                               was a real owned variable — moved + invalidated, no
                               clone. Borrow sources return false → fall to clone. */
                        }
                        else
                        {
                            /* Source is another named has-drop enum variable — deep-clone so
                               both this variable and the source have independently owned heap
                               payloads and can be freed without double-free.
                               e.g. JsonValue b = a  →  b gets its own deep copy of a. */
                            init = emit_enum_clone_val(ctx, init, var_type);
                        }
                    }
                    else if (var_type->kind == TYPE_BLOCK)
                    {
                        AstNode *blk_init = ast_unwrap_move(node->as.var_decl.init);
                        if (blk_init && blk_init->kind == AST_IDENT)
                        {
                            /* F.2: Block variable initialized from another Block
                               variable. Move semantics (P1): zero source's env_ptr
                               so its scope cleanup skips (this var now owns the
                               env). Switching to a shared retain is coupled with
                               checker move-relaxation + discarded-rvalue release —
                               done together in the reference-semantics phase. */
                            CgSymbol *src = cg_scope_resolve(ctx->current_scope,
                                                              blk_init->as.ident.name);
                            if (src && src->no_drop_reason == CG_OWNED)
                                cg_null_block_env(ctx, src->value);
                        }
                        else if (cg_block_source_is_aliased(blk_init))
                        {
                            /* Container read (vec[i]/struct.field/map.get): deep
                               clone the env (P1 behaviour). Switching this to a
                               shared retain is coupled with releasing discarded
                               container-read rvalues — deferred to P3. */
                            init = cg_emit_block_env_clone(ctx, init);
                        }
                        else if (blk_init && blk_init->kind == AST_INDEX)
                        {
                            /* Raw ptr/array index read: bind as a borrow (P1) —
                               no retain, no scope release. Its owner (the
                               container) releases it. Retain-sharing here is
                               coupled with P3 (discarded-rvalue release). */
                            block_alias_init = true;
                        }
                        else
                        {
                            /* Fresh OWNED Block rvalue — factory call / force-unwrap
                               (P3) or closure literal (stage 10, unified into the
                               same temp_drop ledger): the var takes the single
                               reference it was born with — claim the temp so the
                               statement-end flush doesn't release the env the var
                               now owns. No retain, no-op for an untracked rvalue.
                               (Pre-unification this popped the literal's entry from
                               a separate env table FIRST, which mis-claimed the
                               literal ARG's env instead of the factory result for
                               `Block b = make_pair(|| ..)`; topmost-Block claim
                               picks the result — registered last — correctly.) */
                            cg_claim_block_temp_above(ctx, blk_drop_floor);
                        }
                    }

                    LLVMBuildStore(ctx->builder, init, alloca);
                }

                cg_flush_temps(ctx);
            }
        }

        {
            CgSymbol *vsym = cg_scope_define(ctx->current_scope, node->as.var_decl.name,
                                             alloca, var_type, moved_flag);
            if (vsym) vsym->lifetime_marked = var_lifetime_marked;
            if (vsym && block_alias_init) vsym->no_drop_reason = CG_ALIAS;
        }
        return;
}

static void cg_stmt_return(CodegenContext *ctx, AstNode *node)
{
        /* Before returning:
           1. If returning a string/struct variable by name, find its alloca.
           2. Emit all scope cleanups, skipping the returned variable's alloca.
           3. Return the value.

           We use LLVMValueRef (the alloca itself) as the skip key rather than
           a CgSymbol* pointer. This is stable across scope array reallocations
           and correctly handles same-name shadowed variables: each alloca is a
           unique LLVM object, so inner-scope "x" and outer-scope "x" have
           distinct alloca values and are never confused. */
        LLVMValueRef return_alloca = NULL;
        /* Clone-instead-of-transfer flag for an IDENT string return:
           - P1-3/BF-041: a GLOBAL move-type var (global retains ownership, freed
             at exit by __ls_global_cleanup → transferring would double-free).
           - BF-045: a BORROWED string param (cap=-2 alias of the caller's buffer;
             transferring would dangle once the caller frees its temp after the call).
           Both cases must deep-copy so the caller receives an independent owned string. */
        bool ret_global_movetype = false;

        if (node->as.return_stmt.value &&
            node->as.return_stmt.value->kind == AST_IDENT &&
            node->as.return_stmt.value->resolved_type)
        {
            Type *ret_type = node->as.return_stmt.value->resolved_type;
            /* For string/struct/vec/Block/has_drop-enum IDENT returns:
               ownership transfers to caller — skip scope cleanup for this
               variable so we don't free its data/env. */
            if (ret_type->kind == TYPE_STRUCT ||
                ret_type->kind == TYPE_BLOCK  ||
                (ret_type->kind == TYPE_ENUM && ret_type->as.enom.has_drop))
            {
                const char *name = node->as.return_stmt.value->as.ident.name;
                CgSymbol *sym = cg_scope_resolve(ctx->current_scope, name);
                if (sym)
                {
                    bool is_global = LLVMIsAGlobalVariable(sym->value);
                    /* BF-045: a borrowed string param must clone on return.
                       Generalized: a borrowed has_drop struct/enum binder must
                       clone too. A match payload binder of a *borrow* subject
                       (&Enum param OR a closure's by-move capture, which is
                       marked CG_BORROWED so the body's scope cleanup leaves the
                       drop to the env) aliases the subject's payload zero-copy.
                       Returning it by transfer hands the caller an alias the
                       subject's owner (caller / env_drop) will also free →
                       double-free (string survived only because its borrow ABI
                       marks cap=BORROWED so the caller skips the free; Str/Vec/Map
                       carry no such marker). Clone so the caller owns an
                       independent copy. */
                    bool borrowed_heap =
                        sym->no_drop_reason != CG_OWNED &&
                        ((ret_type->kind == TYPE_STRUCT && ret_type->as.strukt.has_drop) ||
                         (ret_type->kind == TYPE_ENUM && ret_type->as.enom.has_drop));
                    if (is_global || borrowed_heap)
                        ret_global_movetype = true; /* clone, don't transfer */
                    else
                        return_alloca = sym->value; /* local: transfer + skip */
                }
            }
        }

        /* Phase 2 (borrow extension): returning a borrow (&T). The LLVM return
           type is a pointer; emit the ADDRESS of the derived place (self /
           self.field) rather than loading its value. No ownership transfer, no
           clone — the checker proved the place derives from a borrow input that
           outlives the call (checker_place_root_symbol). */
        if (node->as.return_stmt.value &&
            ctx->current_fn_return_type &&
            ctx->current_fn_return_type->kind == TYPE_REFERENCE)
        {
            AstNode *rv = node->as.return_stmt.value;
            /* A transitively-chained borrow return `return self.child.get()`:
               the call already evaluates to the borrow pointer. Otherwise the
               return expr is a place (self / self.field) — take its address. */
            LLVMValueRef ptr = (rv->kind == AST_CALL && rv->resolved_type &&
                                rv->resolved_type->kind == TYPE_REFERENCE)
                                   ? codegen_expr(ctx, rv)
                                   : codegen_lvalue_ptr(ctx, rv);
            cg_flush_temps_scope_exit(ctx);
            emit_cleanup_to(ctx, NULL, NULL);
            cg_emit_mc_leave(ctx);
            cg_emit_prof_leave(ctx);
            LLVMBuildRet(ctx->builder, ptr);
            return;
        }

        /* Does the enclosing function return void? `return EXPR` where the
           function is void — e.g. an expression-body closure `|x| print(x)`
           whose expected Block return is void (`v.each(|x| print(x))`), or an
           early `return g()` over a void-returning g — must evaluate EXPR for
           its side effects and then ret void, NOT emit `ret <value>` into a
           void function (invalid IR; was a codegen verification failure). */
        bool fn_is_void = (ctx->current_fn_return_type == NULL ||
                           ctx->current_fn_return_type->kind == TYPE_VOID);
        if (node->as.return_stmt.value && !fn_is_void)
        {
            int ret_drop_floor = ctx->temp_drop_count;
            LLVMValueRef val = codegen_expr(ctx, node->as.return_stmt.value);
            if (val)
            {
                Type *ret_type = node->as.return_stmt.value->resolved_type;
                /* Flush temp strings from return expression evaluation.
                   For string returns: mark the last temp as moved so its cap
                   becomes -1 (ownership transfers to caller via the SSA value).
                   For non-string returns: flush all temps (the enum constructor
                   already cloned any string args). */
                /* Phase C.5: Block return transfers env ownership to the
                   caller — pop the trailing temp env so flush doesn't free
                   it. The caller will store into a Block local (or pass it
                   onward) and own the released env. */
                if (ret_type && ret_type->kind == TYPE_BLOCK) {
                    /* Block return transfers env ownership to the caller — claim
                       the tracked temp (factory / force-unwrap / closure literal,
                       all temp_drop-tracked since stage 10) so the scope-exit
                       flush below does not release the env the caller now owns. */
                    cg_claim_block_temp_above(ctx, ret_drop_floor);
                }
                cg_flush_temps_scope_exit(ctx);
                /* P1-3 fix: returning a GLOBAL string by name shares the global's
                   data pointer. The global is freed at exit by __ls_global_cleanup,
                   so transferring it to the caller (who also frees) double-frees.
                   Clone here so caller owns an independent copy. (Local strings
                   take the move-transfer path above and must NOT clone.) */
                if (ret_global_movetype && ret_type && val)
                {
                    if ((ret_type->kind == TYPE_STRUCT && ret_type->as.strukt.has_drop) ||
                             (ret_type->kind == TYPE_ENUM && ret_type->as.enom.has_drop))
                        /* borrowed has_drop binder (see borrowed_heap above):
                           deep-copy the loaded value so the caller owns it. */
                        val = emit_clone_value(ctx, val, type_to_llvm(ctx, ret_type), ret_type);
                }

                /* Implicit numeric widening to the function's declared return
                   type (e.g. `fn f() -> f64 { return 5 }`). The checker has
                   already validated assignability via type_widens_to. */
                if (ctx->current_fn_return_type && ret_type &&
                    type_is_numeric(ret_type) &&
                    type_is_numeric(ctx->current_fn_return_type) &&
                    !type_equals(ret_type, ctx->current_fn_return_type))
                {
                    val = cg_widen(ctx, val, ret_type, ctx->current_fn_return_type);
                    ret_type = ctx->current_fn_return_type;
                }

                if (ret_type && ret_type->kind == TYPE_ARRAY)
                {
                    /* array with has_drop elements: clone before returning so local
                       array's scope-cleanup doesn't free data the caller will use. */
                    Type *elem = ret_type->as.array.elem;
                    bool needs_clone = elem &&
                                       (elem->kind == TYPE_STRUCT && elem->as.strukt.has_drop);
                    if (needs_clone)
                    {
                        LLVMTypeRef arr_llvm = type_to_llvm(ctx, ret_type);
                        val = emit_array_clone_val(ctx, val, arr_llvm, ret_type);
                    }
                }
                emit_cleanup_to(ctx, NULL, return_alloca);
                cg_emit_mc_leave(ctx);   /* D.1: pop frame */
                cg_emit_prof_leave(ctx);
                LLVMBuildRet(ctx->builder, val);
            }
        }
        else
        {
            /* void return: clean up everything. If a value expression is
               present (void function returning a void-typed expr, e.g. the
               desugared `|x| print(x)` closure body), evaluate it first for
               its side effects, then discard the value and ret void. The
               temp flush runs for the BARE `return` too: a match over an
               owned rvalue subject registers the subject as a base-protected
               temp, and this exiting path must release it like the valued
               return paths do (`match f() { Err(e) => { return } }` leaked
               the subject's payload). */
            if (node->as.return_stmt.value)
                codegen_expr(ctx, node->as.return_stmt.value);
            cg_flush_temps_scope_exit(ctx);
            emit_cleanup_to(ctx, NULL, NULL);
            /* For user-defined __drop: inject string field cleanup before return */
            emit_drop_field_cleanup(ctx);
            if (LLVMGetBasicBlockTerminator(LLVMGetInsertBlock(ctx->builder)) == NULL)
            {
                cg_emit_mc_leave(ctx);   /* D.1: pop frame */
                cg_emit_prof_leave(ctx);
                if (ctx->is_main_void)
                    LLVMBuildRet(ctx->builder,
                        LLVMConstInt(LLVMInt32TypeInContext(ctx->context), 0, 0));
                else
                    LLVMBuildRetVoid(ctx->builder);
            }
        }
        return;
}

static void cg_stmt_for(CodegenContext *ctx, AstNode *node)
{
        /* Iterator-protocol desugaring (for x in <user iterable>): the checker
           rewrote this into an equivalent while/match block. Emit that and stop;
           the builtin range/array/vec paths below never run for it. */
        if (node->as.for_stmt.desugared != NULL)
        {
            codegen_stmt(ctx, node->as.for_stmt.desugared);
            return;
        }

        /* foreach loop: for var in iter { body }
           Supported iterators:
             - Range expression (a..b): iterate var from a to b-1
             - Integer expression (n): iterate var from 0 to n-1
             - Array: iterate over elements
              - User-defined iterables are lowered by the checker before codegen. */
        push_scope(ctx);

        AstNode *iter_node = node->as.for_stmt.iter;
        Type *iter_type = iter_node->resolved_type;
        bool is_array_iter = (iter_type && iter_type->kind == TYPE_ARRAY);
        bool is_slice_iter = (iter_type && iter_type->kind == TYPE_SLICE);
        LLVMValueRef start_val = NULL, end_val = NULL;
        LLVMValueRef arr_ptr = NULL;    /* for fixed-array iter: alloca of array */
        LLVMValueRef slice_ptr = NULL;  /* for slice iter: base *T (SSA, dominates body) */

        if (is_slice_iter)
        {
            /* Evaluate the slice ONCE, keep base ptr + length (i32). */
            LLVMValueRef sv = codegen_expr(ctx, iter_node);
            slice_ptr = LLVMBuildExtractValue(ctx->builder, sv, 0, "it.ptr");
            LLVMValueRef len64 = LLVMBuildExtractValue(ctx->builder, sv, 1, "it.len");
            start_val = LLVMConstInt(LLVMInt32TypeInContext(ctx->context), 0, false);
            end_val = LLVMBuildTrunc(ctx->builder, len64,
                                     LLVMInt32TypeInContext(ctx->context), "it.len32");
        }
        else if (is_array_iter)
        {
            /* Array iteration: index from 0..size */
            start_val = LLVMConstInt(LLVMInt32TypeInContext(ctx->context), 0, false);
            end_val = LLVMConstInt(LLVMInt32TypeInContext(ctx->context),
                                   (unsigned long long)iter_type->as.array.size, false);
            /* Get array pointer */
            if (iter_node->kind == AST_IDENT)
            {
                CgSymbol *sym = cg_scope_resolve(ctx->current_scope, iter_node->as.ident.name);
                if (sym)
                    arr_ptr = sym->value;
            }
            if (arr_ptr == NULL)
            {
                pop_scope(ctx);
                return;
            }
        }
        else if (iter_node->kind == AST_RANGE)
        {
            /* Range: start..end */
            start_val = codegen_expr(ctx, iter_node->as.range.start);
            end_val = codegen_expr(ctx, iter_node->as.range.end);
        }
        else
        {
            /* Single integer expression: iterate 0..n */
            start_val = LLVMConstInt(LLVMInt32TypeInContext(ctx->context), 0, false);
            end_val = codegen_expr(ctx, iter_node);
        }
        if (start_val == NULL || end_val == NULL)
        {
            pop_scope(ctx);
            return;
        }

        LLVMTypeRef i32_ty = LLVMInt32TypeInContext(ctx->context);

        /* The loop counter and the user-visible loop variable `i` are i32 (the
           checker types range loop variables as int). A range / integer bound
           may be a wider integer — e.g. `for i in 0..n` with an i64 `n` — so
           coerce both ends to the i32 counter type before they feed the
           comparison and increment. Without this the cond block emits an
           `icmp i32 %cur, i64 %end` and fails module verification. Mirrors the
           slice path, which already truncates its i64 length to i32. (Bounds
           beyond i32 range are a pre-existing limitation of the i32 loop var,
           not introduced here.) */
        if (LLVMTypeOf(start_val) != i32_ty &&
            LLVMGetTypeKind(LLVMTypeOf(start_val)) == LLVMIntegerTypeKind)
            start_val = LLVMBuildIntCast2(ctx->builder, start_val, i32_ty, 1, "foreach.start32");
        if (LLVMTypeOf(end_val) != i32_ty &&
            LLVMGetTypeKind(LLVMTypeOf(end_val)) == LLVMIntegerTypeKind)
            end_val = LLVMBuildIntCast2(ctx->builder, end_val, i32_ty, 1, "foreach.end32");

        LLVMBasicBlockRef entry = LLVMGetEntryBasicBlock(ctx->current_fn);

        /* Allocate loop index counter and element variable */
        LLVMValueRef idx_var = NULL;  /* internal index counter for array iter */
        LLVMValueRef loop_var = NULL; /* user-visible loop variable */

        {
            LLVMBuilderRef tmp = LLVMCreateBuilderInContext(ctx->context);
            LLVMValueRef first_inst = LLVMGetFirstInstruction(entry);
            if (first_inst)
                LLVMPositionBuilderBefore(tmp, first_inst);
            else
                LLVMPositionBuilderAtEnd(tmp, entry);

            if (is_array_iter || is_slice_iter)
            {
                idx_var = LLVMBuildAlloca(tmp, i32_ty, "foreach.idx");
                Type *elem_type = iter_type->as.array.elem;
                LLVMTypeRef elem_llvm = type_to_llvm(ctx, elem_type);
                loop_var = LLVMBuildAlloca(tmp, elem_llvm, node->as.for_stmt.var);
            }
            else
            {
                loop_var = LLVMBuildAlloca(tmp, i32_ty, node->as.for_stmt.var);
            }
            LLVMDisposeBuilder(tmp);
        }

        if (is_array_iter || is_slice_iter)
        {
            LLVMBuildStore(ctx->builder, start_val, idx_var);
            {
                CgSymbol *lvsym = cg_scope_define(ctx->current_scope, node->as.for_stmt.var,
                                                  loop_var, iter_type->as.array.elem, NULL);
                /* Loop variable is a copy of the element; mark borrowed so scope
                   cleanup doesn't drop it (the container still owns the data). */
                if (lvsym)
                    lvsym->no_drop_reason = CG_BORROWED;
            }
        }
        else
        {
            LLVMBuildStore(ctx->builder, start_val, loop_var);
            cg_scope_define(ctx->current_scope, node->as.for_stmt.var, loop_var, type_int(), NULL);
        }

        /* Create basic blocks */
        LLVMBasicBlockRef cond_bb = LLVMAppendBasicBlockInContext(
            ctx->context, ctx->current_fn, "foreach.cond");
        LLVMBasicBlockRef body_bb = LLVMAppendBasicBlockInContext(
            ctx->context, ctx->current_fn, "foreach.body");
        LLVMBasicBlockRef update_bb = LLVMAppendBasicBlockInContext(
            ctx->context, ctx->current_fn, "foreach.update");
        LLVMBasicBlockRef end_bb = LLVMAppendBasicBlockInContext(
            ctx->context, ctx->current_fn, "foreach.end");

        /* Save and set break/continue targets */
        LLVMBasicBlockRef saved_break = ctx->break_bb;
        LLVMBasicBlockRef saved_continue = ctx->continue_bb;
        CgScope *saved_loop_scope = ctx->loop_scope;
        int saved_loop_temp_drop_floor = ctx->loop_temp_drop_floor;
        ctx->break_bb = end_bb;
        ctx->continue_bb = update_bb;
        ctx->loop_scope = ctx->current_scope;
        ctx->loop_temp_drop_floor = ctx->temp_drop_count;

        /* Branch to condition */
        LLVMBuildBr(ctx->builder, cond_bb);

        /* Condition: idx < end */
        LLVMPositionBuilderAtEnd(ctx->builder, cond_bb);
        LLVMValueRef cmp_var = (is_array_iter || is_slice_iter) ? idx_var : loop_var;
        LLVMValueRef cur = LLVMBuildLoad2(ctx->builder, i32_ty, cmp_var, "cur");
        LLVMValueRef cond = LLVMBuildICmp(ctx->builder, LLVMIntSLT, cur, end_val, "foreach.lt");
        LLVMBuildCondBr(ctx->builder, cond, body_bb, end_bb);

        /* Body */
        LLVMPositionBuilderAtEnd(ctx->builder, body_bb);

        if (is_array_iter)
        {
            /* Load current element into loop variable: loop_var = arr[idx] */
            LLVMValueRef cur_idx = LLVMBuildLoad2(ctx->builder, i32_ty, idx_var, "arr.i");
            LLVMTypeRef i64_type = LLVMInt64TypeInContext(ctx->context);
            LLVMValueRef idx64 = LLVMBuildSExtOrBitCast(ctx->builder, cur_idx, i64_type, "idx64");
            LLVMTypeRef arr_llvm = type_to_llvm(ctx, iter_type);
            LLVMValueRef zero64 = LLVMConstInt(i64_type, 0, 0);
            LLVMValueRef indices[2] = {zero64, idx64};
            LLVMValueRef gep = LLVMBuildGEP2(ctx->builder, arr_llvm, arr_ptr,
                                             indices, 2, "foreach.gep");
            LLVMTypeRef elem_llvm = type_to_llvm(ctx, iter_type->as.array.elem);
            LLVMValueRef elem_val = LLVMBuildLoad2(ctx->builder, elem_llvm, gep, "foreach.elem");
            LLVMBuildStore(ctx->builder, elem_val, loop_var);
        }
        else if (is_slice_iter)
        {
            /* loop_var = slice.ptr[idx] (slice borrows the source; loop var is a
               non-owning copy, mirroring the array path). */
            LLVMValueRef cur_idx = LLVMBuildLoad2(ctx->builder, i32_ty, idx_var, "sl.i");
            LLVMValueRef idx64 = LLVMBuildSExtOrBitCast(ctx->builder, cur_idx,
                LLVMInt64TypeInContext(ctx->context), "sl.i64");
            LLVMTypeRef elem_llvm = type_to_llvm(ctx, iter_type->as.array.elem);
            LLVMValueRef gep = LLVMBuildGEP2(ctx->builder, elem_llvm, slice_ptr,
                                             &idx64, 1, "sl.gep");
            LLVMValueRef elem_val = LLVMBuildLoad2(ctx->builder, elem_llvm, gep, "sl.elem");
            LLVMBuildStore(ctx->builder, elem_val, loop_var);
        }
        codegen_stmt(ctx, node->as.for_stmt.body);
        LLVMBasicBlockRef body_end = LLVMGetInsertBlock(ctx->builder);
        if (body_end && LLVMGetBasicBlockTerminator(body_end) == NULL)
            LLVMBuildBr(ctx->builder, update_bb);

        LLVMPositionBuilderAtEnd(ctx->builder, update_bb);
        LLVMValueRef inc_var = (is_array_iter || is_slice_iter) ? idx_var : loop_var;
        LLVMValueRef cur2 = LLVMBuildLoad2(ctx->builder, i32_ty, inc_var, "cur2");
        LLVMValueRef next = LLVMBuildAdd(ctx->builder, cur2,
                                         LLVMConstInt(i32_ty, 1, false), "next");
        LLVMBuildStore(ctx->builder, next, inc_var);
        LLVMBuildBr(ctx->builder, cond_bb);

        /* Restore break/continue/loop_scope */
        ctx->break_bb = saved_break;
        ctx->continue_bb = saved_continue;
        ctx->loop_scope = saved_loop_scope;
        ctx->loop_temp_drop_floor = saved_loop_temp_drop_floor;

        pop_scope(ctx);
        LLVMPositionBuilderAtEnd(ctx->builder, end_bb);
        return;
}

static void cg_stmt_for_c(CodegenContext *ctx, AstNode *node)
{
        /* C-style for: for (init; cond; update) { body }
           Structure: init → cond_bb → body_bb → update_bb → cond_bb
                                    ↘ end_bb                         */
        push_scope(ctx);

        /* Emit init clause (if any) */
        if (node->as.for_c_stmt.init)
        {
            codegen_stmt(ctx, node->as.for_c_stmt.init);
        }

        LLVMBasicBlockRef cond_bb = LLVMAppendBasicBlockInContext(
            ctx->context, ctx->current_fn, "for.cond");
        LLVMBasicBlockRef body_bb = LLVMAppendBasicBlockInContext(
            ctx->context, ctx->current_fn, "for.body");
        LLVMBasicBlockRef update_bb = LLVMAppendBasicBlockInContext(
            ctx->context, ctx->current_fn, "for.update");
        LLVMBasicBlockRef end_bb = LLVMAppendBasicBlockInContext(
            ctx->context, ctx->current_fn, "for.end");

        /* Save and set break/continue targets */
        LLVMBasicBlockRef saved_break = ctx->break_bb;
        LLVMBasicBlockRef saved_continue = ctx->continue_bb;
        CgScope *saved_loop_scope = ctx->loop_scope;
        int saved_loop_temp_drop_floor = ctx->loop_temp_drop_floor;
        ctx->break_bb = end_bb;
        ctx->continue_bb = update_bb; /* continue jumps to update, not cond */
        ctx->loop_scope = ctx->current_scope;
        ctx->loop_temp_drop_floor = ctx->temp_drop_count;

        /* Branch to condition check */
        LLVMBuildBr(ctx->builder, cond_bb);

        /* Condition block */
        LLVMPositionBuilderAtEnd(ctx->builder, cond_bb);
        if (node->as.for_c_stmt.cond)
        {
            LLVMValueRef cond = codegen_expr(ctx, node->as.for_c_stmt.cond);
            /* Free temporary strings produced by the condition expression. */
            cg_flush_temps(ctx);
            if (cond)
                LLVMBuildCondBr(ctx->builder, cond, body_bb, end_bb);
        }
        else
        {
            /* No condition → infinite loop (like for(;;)) */
            LLVMBuildBr(ctx->builder, body_bb);
        }

        /* Body block */
        LLVMPositionBuilderAtEnd(ctx->builder, body_bb);
        codegen_stmt(ctx, node->as.for_c_stmt.body);
        if (LLVMGetBasicBlockTerminator(LLVMGetInsertBlock(ctx->builder)) == NULL)
        {
            LLVMBuildBr(ctx->builder, update_bb);
        }

        /* Update block */
        LLVMPositionBuilderAtEnd(ctx->builder, update_bb);
        if (node->as.for_c_stmt.update)
        {
            codegen_stmt(ctx, node->as.for_c_stmt.update);
        }
        if (LLVMGetBasicBlockTerminator(LLVMGetInsertBlock(ctx->builder)) == NULL)
        {
            LLVMBuildBr(ctx->builder, cond_bb);
        }

        /* Restore break/continue/loop_scope */
        ctx->break_bb = saved_break;
        ctx->continue_bb = saved_continue;
        ctx->loop_scope = saved_loop_scope;
        ctx->loop_temp_drop_floor = saved_loop_temp_drop_floor;

        pop_scope(ctx);
        LLVMPositionBuilderAtEnd(ctx->builder, end_bb);
        return;
}

static void cg_stmt_while(CodegenContext *ctx, AstNode *node)
{
        LLVMBasicBlockRef cond_bb = LLVMAppendBasicBlockInContext(
            ctx->context, ctx->current_fn, "while.cond");
        LLVMBasicBlockRef body_bb = LLVMAppendBasicBlockInContext(
            ctx->context, ctx->current_fn, "while.body");
        LLVMBasicBlockRef end_bb = LLVMAppendBasicBlockInContext(
            ctx->context, ctx->current_fn, "while.end");

        LLVMBasicBlockRef saved_break = ctx->break_bb;
        LLVMBasicBlockRef saved_continue = ctx->continue_bb;
        CgScope *saved_loop_scope = ctx->loop_scope;
        int saved_loop_temp_drop_floor = ctx->loop_temp_drop_floor;
        ctx->break_bb = end_bb;
        ctx->continue_bb = cond_bb;
        ctx->loop_scope = ctx->current_scope;
        /* Floor for break/continue statement-level temp flush: temps live now
           predate the loop body (e.g. an enclosing match's protected subject);
           break/continue must release only temps created inside the loop. */
        ctx->loop_temp_drop_floor = ctx->temp_drop_count;

        LLVMBuildBr(ctx->builder, cond_bb);

        LLVMPositionBuilderAtEnd(ctx->builder, cond_bb);
        {
            LLVMValueRef cond = codegen_expr(ctx, node->as.while_stmt.cond);
            /* Free temporary strings from the condition before branching.
               They are re-created (and re-freed) on every loop iteration. */
            cg_flush_temps(ctx);
            if (cond)
                LLVMBuildCondBr(ctx->builder, cond, body_bb, end_bb);
        }

        LLVMPositionBuilderAtEnd(ctx->builder, body_bb);
        codegen_stmt(ctx, node->as.while_stmt.body);
        if (LLVMGetBasicBlockTerminator(LLVMGetInsertBlock(ctx->builder)) == NULL)
        {
            LLVMBuildBr(ctx->builder, cond_bb);
        }

        ctx->break_bb = saved_break;
        ctx->continue_bb = saved_continue;
        ctx->loop_scope = saved_loop_scope;
        ctx->loop_temp_drop_floor = saved_loop_temp_drop_floor;

        LLVMPositionBuilderAtEnd(ctx->builder, end_bb);
        return;
}

static void cg_stmt_if(CodegenContext *ctx, AstNode *node)
{
        LLVMValueRef cond = codegen_expr(ctx, node->as.if_stmt.cond);
        if (cond == NULL)
            return;
        /* Free temporary strings produced during condition evaluation
           (e.g. f"..." interpolations, string concatenations used in
           comparisons).  The condition result is an i1 bool already
           materialised, so the strings are no longer needed. */
        cg_flush_temps(ctx);

        /* Snapshot the live-temp bookkeeping after the condition flush. The two
           branches are mutually-exclusive paths but share the single temp_drop
           stack during emission: a branch that `return`s drains it to 0 (via
           cg_flush_temps_scope_exit), which must NOT leak into the sibling branch
           or the merge — both still see exactly the temps live on entry (e.g. an
           enclosing match's protected subject). Restore the snapshot before
           emitting the else and again before the merge. Branch-internal temps are
           pushed ABOVE this floor and flushed at the branch's own statement
           boundaries, so the slots below it stay intact (a plain count restore is
           exact; cg_flush_temps never mutates the slot arrays). */
        int if_saved_drop_count = ctx->temp_drop_count;

        LLVMBasicBlockRef then_bb = LLVMAppendBasicBlockInContext(
            ctx->context, ctx->current_fn, "if.then");
        LLVMBasicBlockRef else_bb = NULL;
        LLVMBasicBlockRef merge_bb = LLVMAppendBasicBlockInContext(
            ctx->context, ctx->current_fn, "if.merge");

        if (node->as.if_stmt.else_block)
        {
            else_bb = LLVMAppendBasicBlockInContext(
                ctx->context, ctx->current_fn, "if.else");
            LLVMBuildCondBr(ctx->builder, cond, then_bb, else_bb);
        }
        else
        {
            LLVMBuildCondBr(ctx->builder, cond, then_bb, merge_bb);
        }

        /* then */
        LLVMPositionBuilderAtEnd(ctx->builder, then_bb);
        codegen_stmt(ctx, node->as.if_stmt.then_block);
        if (LLVMGetBasicBlockTerminator(LLVMGetInsertBlock(ctx->builder)) == NULL)
        {
            LLVMBuildBr(ctx->builder, merge_bb);
        }

        /* else */
        if (else_bb)
        {
            ctx->temp_drop_count = if_saved_drop_count;
            LLVMPositionBuilderAtEnd(ctx->builder, else_bb);
            codegen_stmt(ctx, node->as.if_stmt.else_block);
            if (LLVMGetBasicBlockTerminator(LLVMGetInsertBlock(ctx->builder)) == NULL)
            {
                LLVMBuildBr(ctx->builder, merge_bb);
            }
        }

        ctx->temp_drop_count = if_saved_drop_count;
        LLVMPositionBuilderAtEnd(ctx->builder, merge_bb);
        return;
}

void codegen_stmt(CodegenContext *ctx, AstNode *node)
{
    if (node == NULL)
        return;

    /* D1 (-g): statement-level line info — one location per statement,
       sticky across the expressions it lowers (docs/plan_debug_info.md §3.2). */
    cg_di_stmt_loc(ctx, node);

#if CG_DEBUG_LV2
    printf(">>>>> codegen_stmt, node->kind:%u\n", node->kind);
#endif

    switch (node->kind)
    {
    case AST_VAR_DECL:
        cg_stmt_var_decl(ctx, node);
        break;

    case AST_ASSIGN:
        cg_stmt_assign(ctx, node);
        break;

    case AST_RETURN:
        cg_stmt_return(ctx, node);
        break;

    case AST_IF:
        cg_stmt_if(ctx, node);
        break;

    case AST_WHILE:
        cg_stmt_while(ctx, node);
        break;

    case AST_FOR:
        cg_stmt_for(ctx, node);
        break;

    case AST_FOR_C:
        cg_stmt_for_c(ctx, node);
        break;

    case AST_BLOCK:
    {
        push_scope(ctx);
        CgScope *block_parent = ctx->current_scope->parent;
        for (int i = 0; i < node->as.block.stmt_count; i++)
        {
            codegen_stmt(ctx, node->as.block.stmts[i]);
            /* Stop generating if we've hit a terminator */
            if (LLVMGetBasicBlockTerminator(LLVMGetInsertBlock(ctx->builder)) != NULL)
                break;
        }
        /* Only clean up variables declared in THIS block, not outer scopes */
        emit_cleanup_to(ctx, block_parent, NULL);
        pop_scope(ctx);
        break;
    }

    case AST_EXPR_STMT:
    {
        /* Evaluate the expression, then free any temporary strings it produced.
           The unified temp slot mechanism handles all dynamic strings (method calls,
           concatenation, f-strings) — no need for the old expr_produces_dynamic_string
           heuristic. */
        AstNode *estmt = node->as.expr_stmt.expr;
        LLVMValueRef ev = codegen_expr(ctx, estmt);
        /* F2 (VR-LIM-014): a discarded expression producing an owned has_drop
           rvalue (e.g. `v.pop()` -> Option(string)) leaks its inner buffers —
           nothing binds or drops the rvalue. Spill to a temp and register it
           for drop so the flush below releases it. Membership rationale lives
           on cg_expr_yields_owned_rvalue (codegen_internal.h); this site's old
           CALL+combinator whitelist missed INDEX (fixed-array element clone),
           FIELD (has_drop field clone), FORMAT_STRING (a bare `f"…"` statement
           — codegen_format_string does not self-register), BINARY.lowered
           (`x + y;` on Str) and TRY, all real leaks
           (own_rvalue_sites_test.lls). A bare ident stays a borrow of a live
           binding and must NOT be dropped here. */
        if (ev != NULL &&
            cg_expr_yields_owned_rvalue(estmt, estmt->resolved_type))
            cg_spill_owned_rvalue(ctx, ev, estmt->resolved_type,
                                  false, "discard.drop");
        /* Free all temps produced (none are moved/kept — this is a discarded result) */
        cg_flush_temps(ctx);
        break;
    }

    case AST_BREAK:
        if (ctx->break_bb)
        {
            /* Release statement-level temps created inside the loop before the
               scope-local cleanup. An owned rvalue match subject inside the loop
               body registers itself in the temp_drop table (base-protected for
               the fall-through arms); a break escaping the arm skips the normal
               statement-boundary flush, leaking the subject (same escape class
               as the 29b4fa3 bare-return leak). Flush only [loop_floor, count):
               temps that predate the loop (an enclosing match's subject) are the
               enclosing construct's to drop — mirrors emit_cleanup_to(loop_scope)
               only freeing locals declared inside the loop. */
            cg_flush_temps_from(ctx, ctx->loop_temp_drop_floor);
            /* Free string locals from current scope up to (not including) loop scope */
            emit_cleanup_to(ctx, ctx->loop_scope, NULL);
            LLVMBuildBr(ctx->builder, ctx->break_bb);
        }
        break;

    case AST_CONTINUE:
        if (ctx->continue_bb)
        {
            /* See AST_BREAK: release loop-internal statement temps so a continue
               escaping a match arm doesn't leak an owned rvalue subject. */
            cg_flush_temps_from(ctx, ctx->loop_temp_drop_floor);
            /* Free string locals from current scope up to (not including) loop scope */
            emit_cleanup_to(ctx, ctx->loop_scope, NULL);
            LLVMBuildBr(ctx->builder, ctx->continue_bb);
        }
        break;

    case AST_COMPTIME_CONST:
        /* docs/plan_comptime_consteval.md: a scalar comptime constant produces no
           runtime entity — the checker folded every reference into a literal. The
           decl node itself is a no-op. (Array comptime constants will materialize a
           .rodata global via a dedicated path in Step 4; until then array/block
           forms are rejected in the checker and never reach codegen.) */
        break;

    case AST_COMPTIME_FOR:
    case AST_COMPTIME_IF:
    case AST_COMPTIME_MATCH:
        /* Leak guard: comptime statements are unrolled into ordinary statements by
           the checker at instantiation time. Reaching codegen means the unroll pass
           was skipped — a compiler bug, not user error. */
        cg_error(ctx, node->line, node->column,
                 "internal error: %s survived to codegen (should be unrolled in checker)",
                 ast_kind_name(node->kind));
        break;

    default:
        codegen_decl(ctx, node);
        break;
    }
}

/* Lower `closure(args)` to an indirect call through the fat pointer.
   The callee expression has already been type-checked as TYPE_BLOCK. */
LLVMValueRef codegen_block_call(CodegenContext *ctx, AstNode *node)
{
    Type *block_t = node->as.call.callee->resolved_type;
    if (block_t == NULL ||
        (block_t->kind != TYPE_BLOCK && block_t->kind != TYPE_FUNCTION))
    {
        cg_error(ctx, node->line, node->column,
                 "internal: block call without Block type on callee");
        return NULL;
    }

    LLVMValueRef closure_val = codegen_expr(ctx, node->as.call.callee);
    if (closure_val == NULL) return NULL;

    LLVMValueRef fn_ptr  = LLVMBuildExtractValue(ctx->builder, closure_val, 0,
                                                 "blk.fn");
    LLVMValueRef env_ptr = LLVMBuildExtractValue(ctx->builder, closure_val, 1,
                                                 "blk.env");

    /* Build the actual fn type at the call site: ret(env_ptr, params...). */
    int n = block_t->as.function.param_count;
    LLVMTypeRef ptr_t = LLVMPointerTypeInContext(ctx->context, 0);
    LLVMTypeRef *params_llvm = (LLVMTypeRef *)malloc_safe(
        (size_t)(n + 1) * sizeof(LLVMTypeRef));
    params_llvm[0] = ptr_t;
    for (int i = 0; i < n; i++)
    {
        Type *pt = block_t->as.function.params[i];
        /* `&T` params are pointer ABI — must match the closure definition's
           signature (codegen_closure_literal) and the arg-passing below, NOT the
           by-value-scalar lowering type_to_llvm gives for a read-only &scalar. */
        if (pt && pt->kind == TYPE_REFERENCE)
            params_llvm[i + 1] = ptr_t;
        else
            params_llvm[i + 1] = type_to_llvm(ctx, pt);
    }
    LLVMTypeRef ret_llvm = type_to_llvm(ctx, block_t->as.function.return_type);
    LLVMTypeRef fn_type = LLVMFunctionType(ret_llvm, params_llvm,
                                           (unsigned)(n + 1), 0);
    free(params_llvm);

    /* Build args: env_ptr first, then user args (with widening per param). */
    int argc = node->as.call.arg_count;
    if (argc != n)
    {
        cg_error(ctx, node->line, node->column,
                 "internal: closure call argc mismatch (%d vs %d)", argc, n);
        return NULL;
    }
    LLVMValueRef *args = (LLVMValueRef *)malloc_safe(
        (size_t)(n + 1) * sizeof(LLVMValueRef));
    args[0] = env_ptr;
    for (int i = 0; i < n; i++)
    {
        AstNode *a = node->as.call.args[i];
        Type *src_t = a->resolved_type;
        Type *dst_t = block_t->as.function.params[i];

        /* M5-002: a Block parameter of reference type `&T` expects a POINTER
           (borrow ABI), exactly like a regular function's &T param. Form the
           pointer WITHOUT first materialising the value: IDENT → its slot
           pointer; lvalue field/element (`&self.value`, `v[i]`) → its address
           via codegen_addr_of (zero-copy — NO clone of has_drop fields/elems,
           the read-only `&field` borrow path); `&!v`/forwarded borrow → the
           pointer expr itself; rvalue/other → materialise a temp alloca
           (registered for drop if it owns heap, since the callee only borrows). */
        if (dst_t && dst_t->kind == TYPE_REFERENCE)
        {
            Type *pointee = dst_t->as.pointer_to;
            LLVMValueRef ptr = NULL;
            /* `&!v` (AST_MUT_BORROW) — or any argument that already resolved
               to a reference — evaluates to the caller's slot POINTER itself.
               Pass it through; falling into the rvalue materialisation below
               would store the pointer into a pointee-sized temp and hand the
               closure a garbage borrow (heap corruption on first mutation). */
            if (a->kind == AST_MUT_BORROW ||
                (src_t && src_t->kind == TYPE_REFERENCE))
            {
                ptr = codegen_expr(ctx, a);
                if (ptr == NULL) { free(args); return NULL; }
            }
            else if (a->kind == AST_IDENT)
            {
                CgSymbol *sym = cg_scope_resolve(ctx->current_scope,
                                                 a->as.ident.name);
                if (sym) ptr = sym->value;
            }
            else if (a->kind == AST_FIELD || a->kind == AST_INDEX)
            {
                /* zero-copy borrow of a field / element (read-only &T). */
                ptr = codegen_lvalue_ptr(ctx, a);
            }
            if (ptr == NULL)
            {
                LLVMValueRef av = codegen_expr(ctx, a);
                if (av == NULL) { free(args); return NULL; }
                /* Owned heap in the rvalue: the closure only borrows — spill +
                   register so the statement flush releases it. POD rvalues
                   still need the spill for the borrow ABI (bare alloca). */
                if (pointee &&
                    ((pointee->kind == TYPE_STRUCT && pointee->as.strukt.has_drop) ||
                     (pointee->kind == TYPE_ENUM   && pointee->as.enom.has_drop)))
                    ptr = cg_spill_owned_rvalue(ctx, av, pointee,
                                                false, "blk.borrow.tmp");
                else
                {
                    LLVMTypeRef store_t = type_to_llvm(ctx, pointee ? pointee : src_t);
                    LLVMValueRef tmp = cg_entry_alloca(ctx, store_t, "blk.borrow.tmp");
                    LLVMBuildStore(ctx->builder, av, tmp);
                    ptr = tmp;
                }
            }
            args[i + 1] = ptr;
            continue;
        }

        LLVMValueRef av = codegen_expr(ctx, a);
        if (av == NULL) { free(args); return NULL; }

        av = cg_widen(ctx, av, src_t, dst_t);

        /* Match regular function-call ownership for Block parameters.
           A closure parameter is materialised as a local in the closure body:
           string params use cap to distinguish borrowed vs owned, vec/map/Block
           params are call-borrowed, and has_drop struct/enum params are dropped
           by the closure. Therefore the call site must not pass a raw alias for
           heap-owning named values. */
        Type *arg_type = dst_t ? dst_t : src_t;
        AstNode *raw = node->as.call.args[i];
        AstNode *unwrapped = ast_unwrap_move(raw);
        bool is_move_expr = (raw != unwrapped);

        if (av && arg_type && arg_type->kind == TYPE_STRUCT &&
                 arg_type->as.strukt.has_drop)
        {
            if (unwrapped && unwrapped->kind == AST_IDENT)
            {
                if (is_move_expr)
                {
                    CgSymbol *argsym = cg_scope_resolve(ctx->current_scope,
                                                        unwrapped->as.ident.name);
                    if (argsym && argsym->moved_flag)
                    {
                        LLVMBuildStore(ctx->builder,
                            LLVMConstInt(LLVMInt1TypeInContext(ctx->context), 1, 0),
                            argsym->moved_flag);
                    }
                }
                else
                {
                    LLVMTypeRef st_llvm = type_to_llvm(ctx, arg_type);
                    av = emit_struct_clone_val(ctx, av, st_llvm, arg_type);
                }
            }
        }
        else if (av && arg_type && arg_type->kind == TYPE_ENUM &&
                 arg_type->as.enom.has_drop)
        {
            if (unwrapped && unwrapped->kind == AST_IDENT)
            {
                if (is_move_expr)
                {
                    CgSymbol *argsym = cg_scope_resolve(ctx->current_scope,
                                                        unwrapped->as.ident.name);
                    if (argsym && argsym->moved_flag)
                    {
                        LLVMBuildStore(ctx->builder,
                            LLVMConstInt(LLVMInt1TypeInContext(ctx->context), 1, 0),
                            argsym->moved_flag);
                    }
                }
                else
                {
                    av = emit_enum_clone_val(ctx, av, arg_type);
                }
            }
        }
        args[i + 1] = av;
    }

    bool void_ret = (LLVMGetTypeKind(ret_llvm) == LLVMVoidTypeKind);
    LLVMValueRef result = LLVMBuildCall2(ctx->builder, fn_type, fn_ptr,
                                         args, (unsigned)(n + 1),
                                         void_ret ? "" : "blk.call");
    free(args);
    (void)env_ptr;

    return result;
}
