/* codegen_closure.c
   闭包字面量下沉：codegen_closure_literal + 专用捕获谓词 helper。

   Bodies mechanically relocated from codegen_stmt.c (Batch 3 Task 3.2,
   docs/plan_codegen_split.md). No logic changes. All prototypes live in
   codegen_internal.h. */
#include "codegen.h"
#include "codegen_internal.h"
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
static bool capture_type_is_by_move_cg(const Type *t);
static bool capture_type_is_by_ref_cg(const Type *t);

/* Phase C.5/C.7: capture types that need release work in env_drop.
   Currently:
     (historical) string — env_drop freed data when cap > 0 (cap 0 when the
                   capture aliases a caller-owned string via the by-value
                   param-borrow ABI; non-zero when cloned/owned).
     TYPE_STRUCT(has_drop) — env_drop calls Struct.__drop on the slot. */
static bool capture_type_is_by_move_cg(const Type *t) {
    if (t == NULL) return false;
    switch (t->kind) {
    case TYPE_STRUCT: return t->as.strukt.has_drop;
    case TYPE_ENUM:   return t->as.enom.has_drop;  /* F.5: has_drop enum → by-move */
    /* Closure-foundation Phase A: a captured Block is by-CLONE at the source
       (the outer Block stays live, see checker capture_type_supported), but the
       env field holds an OWNED clone that must be dropped. This predicate governs
       env ownership/drop (counts toward has_drop_n, gets an env_drop entry, env
       field stores a value not a by-ref pointer) — so it returns true here even
       though the checker's capture_type_is_by_move stays false. The two predicates
       are DELIBERATELY ASYMMETRIC: checker = source-move semantics (Block: no),
       codegen = env-ownership/drop semantics (Block: yes). Do not "unify" them.
       See docs/plan_closure_foundation.md §2.4. */
    case TYPE_BLOCK:  return true;
    default:          return false;
    }
}

/* True for by-ref captures: env stores a pointer to the outer alloca.
   Only the removed builtin map used this; now always false. */
static bool capture_type_is_by_ref_cg(const Type *t) {
    (void)t;
    return false;
}

/* ---- Phase B/C closure codegen ----
   Lifts a `|x| body` literal into a synthesised top-level LLVM function
   `__closure_<N>(env_ptr, params...)` and returns an LsBlock fat-pointer
   value `{ fn_ptr, env_ptr }`.

   Phase B: closures with no captures used env=NULL.
   Phase C: when the checker recorded captures on the AST node, this routine:
     1) builds an LLVM struct type matching the capture list,
     2) heap-allocates one (`cg_emit_alloc(... "closure.env" ...)`),
     3) stores the live outer values into the env at construction time,
     4) inside the synthesised body, copies each capture out of env_ptr into
        a fresh local alloca that the body sees by name (CgSymbol). POD-only
        in v1 — string/vec/map captures are rejected at the checker. */
LLVMValueRef codegen_closure_literal(CodegenContext *ctx, AstNode *node)
{
    Type *block_t = node->resolved_type;
    if (block_t == NULL ||
        (block_t->kind != TYPE_BLOCK && block_t->kind != TYPE_FUNCTION))
    {
        cg_error(ctx, node->line, node->column,
                 "internal: closure literal has no resolved Block type");
        return NULL;
    }

    int n = block_t->as.function.param_count;
    LLVMTypeRef ptr_t = LLVMPointerTypeInContext(ctx->context, 0);

    int cap_n = node->as.closure.capture_count;

    /* Phase C.5 env layout:
         field 0: ptr drop_fn   (NULL when no has_drop captures)
         field 1..N: captures in declaration order
       The drop_fn slot lets RAII free heap captures (string/.. in v1) by
       calling a per-closure synthesised __env_drop_<id> before freeing the
       env block itself. POD-only envs store NULL there and skip the call.

       has_drop_n counts captures whose env-side ownership requires a drop:
       string + struct(has_drop): always by-move */
    int has_drop_n = 0;
    for (int i = 0; i < cap_n; i++) {
        Type *ct_i = node->as.closure.captures[i].type;
        if (capture_type_is_by_move_cg(ct_i))
            has_drop_n++;
    }

	/* 0) Snapshot outer alloca pointers (for the post-capture cap=-1 mark
	   on by-move strings) AND load each current value into a register. We
	   have to do this BEFORE detaching the scope chain, since the closure
	   body runs in a fresh isolated scope. */
    LLVMValueRef *cap_outer_vals    = NULL;
    LLVMValueRef *cap_outer_allocas = NULL;
    if (cap_n > 0) {
        cap_outer_vals    = (LLVMValueRef*)malloc_safe(
            (size_t)cap_n * sizeof(LLVMValueRef));
        cap_outer_allocas = (LLVMValueRef*)malloc_safe(
            (size_t)cap_n * sizeof(LLVMValueRef));
        for (int i = 0; i < cap_n; i++) {
            const char *name = node->as.closure.captures[i].name;
            bool explicit_move = node->as.closure.captures[i].is_explicit_move;
            CgSymbol *sym = cg_scope_resolve(ctx->current_scope, name);
            if (sym == NULL) {
                cg_error(ctx, node->line, node->column,
                         "internal: capture '%s' not in scope at codegen",
                         name);
                free(cap_outer_vals);
                free(cap_outer_allocas);
                return NULL;
            }
            cap_outer_allocas[i] = sym->value;
            Type *ct = node->as.closure.captures[i].type;
            bool is_default_by_ref = capture_type_is_by_ref_cg(ct) && !explicit_move;
            if (is_default_by_ref) {
                /* By-ref (default map): store outer alloca pointer.
                   Mutations to the outer variable are visible inside. */
                cap_outer_vals[i] = sym->value;
                cg_dbg_capture(ctx, name, ct, "borrow");
            } else if (capture_type_is_by_move_cg(ct)) {
                LLVMTypeRef ct_llvm = type_to_llvm(ctx, ct);
                cap_outer_vals[i] = LLVMBuildLoad2(ctx->builder, ct_llvm,
                                                   sym->value, "cap.load");
                /* Distinguish auto by-move vs. explicit [move] enum/string */
                cg_dbg_capture(ctx, name, ct, explicit_move ? "move-expl" : "move");
            } else {
                /* by-copy (POD / array / non-has_drop enum) */
                LLVMTypeRef ct_llvm = type_to_llvm(ctx, ct);
                cap_outer_vals[i] = LLVMBuildLoad2(ctx->builder, ct_llvm,
                                                   sym->value, "cap.load");
                cg_dbg_capture(ctx, name, ct, "copy");
            }
        }
    }

    /* 1) Build env struct LLVM type. Field 0 is the drop_fn pointer slot
       (always present so the cleanup logic stays uniform); user captures
       follow at fields 1..N.
       - by-move captures (string/struct/[move map]): value type
       - by-ref captures (default map without [move]): ptr to outer alloca
       When cap_n == 0 we still skip env entirely and pass NULL. */
    /* Phase G env layout:
         field 0: ptr drop_fn   (NULL when no has_drop captures)
         field 1: ptr clone_fn  (Phase G: per-closure __env_clone_<id>, lets a
                  `Block g = vec[i]` copy-out site deep-clone the env without
                  statically knowing its struct type; NULL only when cap_n==0,
                  in which case env itself is NULL and nothing reads this)
         field 2..N+1: captures in declaration order
       drop_fn stays at offset 0 so scope cleanup's raw offset-0 load is
       unaffected by the inserted clone_fn slot. */
    LLVMTypeRef env_struct_t = NULL;
    if (cap_n > 0) {
        /* P1 (feat/block-ownership-unify): field 2 is an i64 refcount — the
           number of live Block values pointing at this env. env is freed only
           when it drops to 0 (see cg_emit_block_env_release). Header stays
           drop_fn@0 / clone_fn@1 (raw offset-0 drop_fn load unchanged); captures
           shift to fields 3..N+2. */
        LLVMTypeRef *fields = (LLVMTypeRef*)malloc_safe(
            (size_t)(cap_n + 3) * sizeof(LLVMTypeRef));
        fields[0] = ptr_t; /* drop_fn slot */
        fields[1] = ptr_t; /* clone_fn slot (Phase G) */
        fields[2] = LLVMInt64TypeInContext(ctx->context); /* refcount (P1) */
        for (int i = 0; i < cap_n; i++) {
            Type *ct = node->as.closure.captures[i].type;
            bool explicit_move = node->as.closure.captures[i].is_explicit_move;
            bool is_default_by_ref = capture_type_is_by_ref_cg(ct) && !explicit_move;
            if (is_default_by_ref) {
                fields[i + 3] = ptr_t; /* pointer to outer alloca */
            } else {
                fields[i + 3] = type_to_llvm(ctx, ct);
            }
        }
        env_struct_t = LLVMStructTypeInContext(ctx->context, fields,
                                               (unsigned)(cap_n + 3), 0);
        free(fields);
    }

    /* 2) Build LLVM signature: ret(env_ptr, params...) */
    LLVMTypeRef *params_llvm = (LLVMTypeRef *)malloc_safe(
        (size_t)(n + 1) * sizeof(LLVMTypeRef));
    params_llvm[0] = ptr_t; /* env */
    for (int i = 0; i < n; i++)
    {
        Type *pt = block_t->as.function.params[i];
        /* A `&T` closure param uses POINTER ABI (borrow) — must match the body
           param handling (5b, which registers the LLVM param as a pointer) AND
           the call site (codegen_block_call passes a pointer). type_to_llvm
           would lower a read-only `&scalar` to the pointee VALUE type (the
           by-value-scalar optimisation for regular `&K` params), which mismatches
           the body's pointer use → "Load operand must be a pointer". Force ptr. */
        if (pt && pt->kind == TYPE_REFERENCE)
            params_llvm[i + 1] = ptr_t;
        else
            params_llvm[i + 1] = type_to_llvm(ctx, pt);
    }
    Type *ret_lst = block_t->as.function.return_type;
    LLVMTypeRef ret_llvm = type_to_llvm(ctx, ret_lst);
    LLVMTypeRef fn_type_llvm = LLVMFunctionType(ret_llvm, params_llvm,
                                                (unsigned)(n + 1), 0);
    free(params_llvm);

    /* 3) Create the function under a unique name. */
    char fn_name[64];
    int id = ctx->closure_id_counter++;
    snprintf(fn_name, sizeof(fn_name), "__closure_%d", id);
    LLVMValueRef fn = LLVMAddFunction(ctx->module, fn_name, fn_type_llvm);

    /* 4) Save outer codegen state and switch to the new function's body. */
    LLVMBasicBlockRef saved_block = LLVMGetInsertBlock(ctx->builder);
    /* D1: the closure gets its own subprogram, so the sticky debug location
       switches scope with it. Snapshot the outer statement's location and
       reinstate it after the mid-statement body emission — otherwise the rest
       of the enclosing statement carries closure-scoped (broken) locations. */
    LLVMMetadataRef saved_di_loc =
        ctx->dib ? LLVMGetCurrentDebugLocation2(ctx->builder) : NULL;
    LLVMValueRef saved_fn = ctx->current_fn;
    Type *saved_fn_ret = ctx->current_fn_return_type;
    /* The closure is its own function, never `main`: a void closure must ret
       void, not `ret i32 0`. Clear is_main_void while compiling the body so a
       void return inside it (e.g. the desugared `|x| print(x)`) doesn't inherit
       main's int-0 return convention. */
    bool saved_is_main_void = ctx->is_main_void;
    ctx->is_main_void = false;
    CgScope *saved_scope = ctx->current_scope;
    /* Isolate the body's statement-level temp stacks from the parent's. The
       closure body is a separate function: its own rvalue temporaries (has_drop
       struct/enum/vec drops + closure-env drops) must not be drained by — and
       must not leak into — the outer function. Without this, a temp registered
       in the parent before this closure literal (e.g. the rvalue receiver of a
       chained method call `v.map(U)(...).reduce(U)(...)`) would be flushed
       INSIDE the closure body, referencing an alloca from another function
       (LLVM "instruction does not dominate all uses"). */
    int saved_temp_drop_count = ctx->temp_drop_count;
    int saved_temp_drop_base  = ctx->temp_drop_base;

    LLVMBasicBlockRef entry =
        LLVMAppendBasicBlockInContext(ctx->context, fn, "entry");
    LLVMPositionBuilderAtEnd(ctx->builder, entry);
    /* D1 (-g): closures are real user code with real source lines. */
    cg_di_fn_begin(ctx, fn, node);
    ctx->current_fn = fn;
    ctx->current_fn_return_type = ret_lst;
    ctx->temp_drop_count = 0;
    ctx->temp_drop_base = 0;  /* independent body: no enclosing protected floor */

    /* Detach from outer scope chain — only params + captures should be
       visible inside the closure body. */
    ctx->current_scope = NULL;
    push_scope(ctx);

    /* 5a) Materialise captures inside the body. Field 0 is the drop_fn
       slot, so user captures live at indices 1..N.

       Two strategies depending on capture kind:
       - by-move (string/struct): load value from env slot → alloca →
         cg_scope_define with CG_BORROWED (env is sole owner of heap).
       - by-ref (map): env slot holds a pointer to the OUTER alloca.
         Load the pointer from env, use it directly as sym->value. Body
         reads/writes go straight to the outer variable, so mutations are
         visible bidirectionally. Mark CG_BORROWED so scope cleanup
         doesn't call drop on what it doesn't own. */
    if (cap_n > 0) {
        LLVMValueRef env_param = LLVMGetParam(fn, 0);
        for (int i = 0; i < cap_n; i++) {
            Type *ct = node->as.closure.captures[i].type;
            const char *cap_name = node->as.closure.captures[i].name;
            bool explicit_move = node->as.closure.captures[i].is_explicit_move;
            bool is_default_by_ref = capture_type_is_by_ref_cg(ct) && !explicit_move;
            LLVMValueRef field_ptr = LLVMBuildStructGEP2(
                ctx->builder, env_struct_t, env_param,
                (unsigned)(i + 3), "cap.gep");  /* P1: +refcount@2 → caps at i+3 */

            if (is_default_by_ref) {
                /* By-ref (default map): load the outer alloca pointer.
                   sym->value = that pointer = the outer alloca itself.
                   Body accesses the outer map in-place. CG_BORROWED
                   prevents scope cleanup from dropping what it doesn't own. */
                LLVMValueRef outer_ptr = LLVMBuildLoad2(
                    ctx->builder, ptr_t, field_ptr, "cap.refptr");
                CgSymbol *cs = cg_scope_define(ctx->current_scope,
                                cap_name, outer_ptr, ct, NULL);
                if (cs) cs->no_drop_reason = CG_BORROWED;
            } else {
                /* By-move (or POD or explicit [move] map): load value,
                   alloca, store, register. */
                LLVMTypeRef ct_llvm = type_to_llvm(ctx, ct);
                LLVMValueRef field_val = LLVMBuildLoad2(
                    ctx->builder, ct_llvm, field_ptr, "cap.fromenv");
                LLVMValueRef alloca = LLVMBuildAlloca(
                    ctx->builder, ct_llvm, cap_name);
                LLVMBuildStore(ctx->builder, field_val, alloca);
                CgSymbol *cs = cg_scope_define(ctx->current_scope,
                                cap_name, alloca, ct, NULL);
                /* Body must not drop env-owned heap.
                   For [move] map: env owns the buckets; mark borrowed
                   so scope cleanup doesn't free it (env_drop handles it). */
                bool needs_borrow = capture_type_is_by_move_cg(ct);
                if (cs && needs_borrow)
                    cs->no_drop_reason = CG_BORROWED;
            }
        }
    }

    /* 5b) Define each user parameter as alloca + store. The LLVM param at
       slot (i+1) skips the env at slot 0.
       map/Block params are marked CG_BORROWED — the caller owns
       the underlying heap (bucket array / env block), so the
       closure body's scope cleanup must not free it (matches the behaviour
       of regular fn params, codegen_fn_decl line ~12117). */
    for (int i = 0; i < n; i++)
    {
        Type *pt = block_t->as.function.params[i];
        /* M5-002: a `&T` closure param uses pointer ABI (borrow), exactly like a
           regular function's &T param (codegen_fn_decl ~13201). The LLVM param IS
           the pointer; register the symbol with that pointer as its value and the
           UNWRAPPED pointee type, no_drop_reason so the body GEPs through it and
           scope cleanup leaves ownership with the caller. (The call site passes a
           pointer for &T params — see codegen_block_call.) */
        if (pt && pt->kind == TYPE_REFERENCE)
        {
            Type *pointee = pt->as.pointer_to;
            LLVMValueRef ptr = LLVMGetParam(fn, (unsigned)(i + 1));
            CgSymbol *psym = cg_scope_define(ctx->current_scope,
                            node->as.closure.param_names[i], ptr, pointee, NULL);
            if (psym)
            {
                psym->no_drop_reason = CG_BORROWED;
                if (pt->is_mut) psym->is_mut_borrow = true;
            }
            continue;
        }
        LLVMTypeRef pt_llvm = type_to_llvm(ctx, pt);
        LLVMValueRef param_val = LLVMGetParam(fn, (unsigned)(i + 1));
        LLVMValueRef alloca = cg_entry_alloca(ctx, pt_llvm,
                                              node->as.closure.param_names[i]);
        LLVMBuildStore(ctx->builder, param_val, alloca);
        CgSymbol *psym = cg_scope_define(ctx->current_scope,
                        node->as.closure.param_names[i],
                        alloca, pt, NULL);
        if (psym && pt &&
            pt->kind == TYPE_BLOCK)
            psym->no_drop_reason = CG_BORROWED;
    }

    /* 6) Compile the body. */
    codegen_stmt(ctx, node->as.closure.body);

    /* A4-closure (own-audit): the body ran with zeroed, isolated ledgers; on
       its open tail (before the implicit ret below) they must be drained —
       the restore at step 8 would silently discard any leftover, exactly the
       cross-function leak shape the isolation comment above describes.
       Terminated tails skipped as in fn_end/A4. */
    if (cg_own_audit_enabled())
    {
        LLVMBasicBlockRef cl_tail = LLVMGetInsertBlock(ctx->builder);
        if (cl_tail && LLVMGetBasicBlockTerminator(cl_tail) == NULL)
        {
            CG_OWN_AUDIT(ctx, ctx->temp_drop_count == 0, "closure_end/A4",
                         "temp_drop_count %d != 0 on open closure tail",
                         ctx->temp_drop_count);
        }
    }

    /* 7) Ensure the entry block (and any continuation block) has a terminator.
       If the user body has no explicit return, default to ret 0 / ret void. */
    LLVMBasicBlockRef cur = LLVMGetInsertBlock(ctx->builder);
    if (cur && LLVMGetBasicBlockTerminator(cur) == NULL)
    {
        if (LLVMGetTypeKind(ret_llvm) == LLVMVoidTypeKind)
        {
            LLVMBuildRetVoid(ctx->builder);
        }
        else
        {
            LLVMBuildRet(ctx->builder, LLVMConstNull(ret_llvm));
        }
    }

    pop_scope(ctx);

    /* 8) Restore outer state. */
    ctx->current_scope = saved_scope;
    ctx->current_fn = saved_fn;
    ctx->current_fn_return_type = saved_fn_ret;
    ctx->is_main_void = saved_is_main_void;
    ctx->temp_drop_count = saved_temp_drop_count;
    ctx->temp_drop_base  = saved_temp_drop_base;
    if (saved_block) LLVMPositionBuilderAtEnd(ctx->builder, saved_block);
    if (ctx->dib) LLVMSetCurrentDebugLocation2(ctx->builder, saved_di_loc);

    /* 9a) If any capture needs heap drop (string in v1) synthesise a per-
       closure __env_drop_<id> and remember its address — stored into env
       field 0 so RAII can call it without knowing the env's static type. */
    LLVMValueRef env_drop_fn = LLVMConstNull(ptr_t);
    if (has_drop_n > 0) {
        LLVMTypeRef drop_param_t[1] = { ptr_t };
        LLVMTypeRef drop_fn_ty = LLVMFunctionType(
            LLVMVoidTypeInContext(ctx->context), drop_param_t, 1, 0);
        char drop_name[80];
        snprintf(drop_name, sizeof(drop_name), "__env_drop_%d", id);
        LLVMValueRef drop_fn = LLVMAddFunction(ctx->module, drop_name,
                                               drop_fn_ty);

        /* Save outer state (we re-use the same save vars conceptually,
           but at this point saved_block / saved_fn already point at the
           outer post-restore state — i.e. caller's BB. We therefore
           snapshot anew for this nested emission.) */
        LLVMBasicBlockRef d_saved_block = LLVMGetInsertBlock(ctx->builder);
        LLVMBasicBlockRef d_entry = LLVMAppendBasicBlockInContext(
            ctx->context, drop_fn, "entry");
        LLVMPositionBuilderAtEnd(ctx->builder, d_entry);
        LLVMValueRef d_env = LLVMGetParam(drop_fn, 0);

        /* For each by-move capture, dispatch on type:
              string : cap > 0 → free(data)                       (C.5)
              struct : call Struct.__drop(slot_ptr)                (C.7) */
        for (int i = 0; i < cap_n; i++) {
            Type *ct = node->as.closure.captures[i].type;
            /* Drop this slot if it's a by-move type. */
            bool needs_drop = capture_type_is_by_move_cg(ct);
            if (!needs_drop) continue;
            LLVMValueRef slot = LLVMBuildStructGEP2(
                ctx->builder, env_struct_t, d_env,
                (unsigned)(i + 3), "cap.slot");  /* P1: caps at i+3 */

            if (ct->kind == TYPE_STRUCT && ct->as.strukt.has_drop) {
                /* Call the struct's auto/user-defined __drop on its slot. */
                LLVMValueRef sdfn = (LLVMValueRef)ct->as.strukt.drop_fn;
                if (sdfn == NULL) {
                    /* Defensive: if the struct's __drop hasn't been emitted
                       yet, force-emit it now. */
                    emit_auto_drop_fn(ctx, ct);
                    sdfn = (LLVMValueRef)ct->as.strukt.drop_fn;
                }
                if (sdfn) {
                    LLVMTypeRef sft = LLVMGlobalGetValueType(sdfn);
                    LLVMBuildCall2(ctx->builder, sft, sdfn, &slot, 1, "");
                }
            }
            else if (ct->kind == TYPE_ENUM && ct->as.enom.has_drop) {
                /* F.5: Call the enum's auto-generated __drop on its slot. */
                LLVMValueRef edfn = (LLVMValueRef)ct->as.enom.drop_fn;
                if (edfn == NULL) {
                    emit_auto_enum_drop_fn(ctx, ct);
                    edfn = (LLVMValueRef)ct->as.enom.drop_fn;
                }
                if (edfn) {
                    LLVMTypeRef eft = LLVMGlobalGetValueType(edfn);
                    LLVMBuildCall2(ctx->builder, eft, edfn, &slot, 1, "");
                }
            }
            else if (ct->kind == TYPE_BLOCK) {
                /* Closure-foundation Phase A: the env owns a cloned Block. Load
                   the Block value, extract its env_ptr (field 1), and free it via
                   the shared helper (drop_fn + free env). NULL-env safe. */
                LLVMTypeRef bptr_t = LLVMPointerTypeInContext(ctx->context, 0);
                LLVMTypeRef bfields[2] = { bptr_t, bptr_t };
                LLVMTypeRef blk_t = LLVMStructTypeInContext(ctx->context, bfields, 2, 0);
                LLVMValueRef blk = LLVMBuildLoad2(ctx->builder, blk_t, slot, "cap.blk");
                LLVMValueRef benv = LLVMBuildExtractValue(ctx->builder, blk, 1, "cap.blk.env");
                cg_emit_block_env_drop(ctx, benv);
            }
        }
        LLVMBuildRetVoid(ctx->builder);

        if (d_saved_block) LLVMPositionBuilderAtEnd(ctx->builder, d_saved_block);
        env_drop_fn = drop_fn;
    }

    /* 9a-clone) Phase G: synthesise a per-closure
         ptr __env_clone_<id>(ptr src_env)
       that deep-copies the env so a copy-out site (`Block g = vec[i]`) can own an
       INDEPENDENT env. Emitted whenever the closure has captures — even POD-only —
       because the env block itself is heap-allocated and two Block values sharing
       one env pointer would double-free it on scope exit.

       Per-capture policy:
          by-ref map (default): copy the outer-alloca pointer shallowly. The
            env does not own it (not in has_drop set), so the clone safely shares
            the by-ref just like the original — no double-free.
          string/map/struct/enum (by-move or [move]): deep clone via the
            existing emit_*_clone_val helpers.
         POD / array(POD): plain value copy (emit_clone_value default). */
    LLVMValueRef env_clone_fn = LLVMConstNull(ptr_t);
    if (cap_n > 0) {
        LLVMTypeRef clone_param_t[1] = { ptr_t };
        LLVMTypeRef clone_fn_ty = LLVMFunctionType(ptr_t, clone_param_t, 1, 0);
        char clone_name[80];
        snprintf(clone_name, sizeof(clone_name), "__env_clone_%d", id);
        LLVMValueRef clone_fn = LLVMAddFunction(ctx->module, clone_name,
                                                clone_fn_ty);

        LLVMBasicBlockRef c_saved_block = LLVMGetInsertBlock(ctx->builder);
        LLVMValueRef c_saved_fn = ctx->current_fn;
        LLVMBasicBlockRef c_entry = LLVMAppendBasicBlockInContext(
            ctx->context, clone_fn, "entry");
        LLVMPositionBuilderAtEnd(ctx->builder, c_entry);
        ctx->current_fn = clone_fn;
        LLVMValueRef c_src = LLVMGetParam(clone_fn, 0);

        /* malloc a fresh env of identical size. */
        unsigned long long esz = LLVMABISizeOfType(
            LLVMGetModuleDataLayout(ctx->module), env_struct_t);
        LLVMValueRef csz = LLVMConstInt(LLVMInt64TypeInContext(ctx->context),
                                        esz, 0);
        LLVMValueRef c_dst = cg_emit_alloc(ctx, csz, "closure.env.clone",
                                           node->line, node->column);

        /* Copy field 0 (drop_fn) and field 1 (clone_fn) verbatim. */
        for (unsigned f = 0; f < 2; f++) {
            LLVMValueRef sp = LLVMBuildStructGEP2(ctx->builder, env_struct_t,
                                                  c_src, f, "cl.shdr");
            LLVMValueRef dp = LLVMBuildStructGEP2(ctx->builder, env_struct_t,
                                                  c_dst, f, "cl.dhdr");
            LLVMValueRef hv = LLVMBuildLoad2(ctx->builder, ptr_t, sp, "cl.hdr");
            LLVMBuildStore(ctx->builder, hv, dp);
        }
        /* P1: the deep-cloned env is an INDEPENDENT allocation — start its
           refcount at 1 (do NOT copy src's count). */
        {
            LLVMValueRef c_rc_slot = LLVMBuildStructGEP2(ctx->builder,
                                        env_struct_t, c_dst, 2u, "cl.rcslot");
            LLVMBuildStore(ctx->builder,
                LLVMConstInt(LLVMInt64TypeInContext(ctx->context), 1, 0),
                c_rc_slot);
        }

        /* Per-capture deep copy. */
        for (int i = 0; i < cap_n; i++) {
            Type *ct = node->as.closure.captures[i].type;
            bool explicit_move_i = node->as.closure.captures[i].is_explicit_move;
            bool is_default_by_ref_i =
                capture_type_is_by_ref_cg(ct) && !explicit_move_i;
            LLVMValueRef s_slot = LLVMBuildStructGEP2(
                ctx->builder, env_struct_t, c_src, (unsigned)(i + 3), "cl.sslot");
            LLVMValueRef d_slot = LLVMBuildStructGEP2(
                ctx->builder, env_struct_t, c_dst, (unsigned)(i + 3), "cl.dslot");
            if (is_default_by_ref_i) {
                LLVMValueRef p = LLVMBuildLoad2(ctx->builder, ptr_t, s_slot,
                                                "cl.refp");
                LLVMBuildStore(ctx->builder, p, d_slot);
            } else if (ct->kind == TYPE_BLOCK) {
                /* Closure-foundation Phase A: when this whole env is copied-out
                   (outer closure becomes a value elsewhere), the Block nested in
                   it must also deep-clone its env one more layer, else both envs
                   would free the same inner env. emit_clone_value falls through to
                   a shallow copy for Block, so handle it explicitly here. */
                LLVMTypeRef ct_llvm = type_to_llvm(ctx, ct);
                LLVMValueRef sv = LLVMBuildLoad2(ctx->builder, ct_llvm, s_slot,
                                                 "cl.sv.blk");
                LLVMValueRef cv = cg_emit_block_env_clone(ctx, sv);
                LLVMBuildStore(ctx->builder, cv, d_slot);
            } else {
                LLVMTypeRef ct_llvm = type_to_llvm(ctx, ct);
                LLVMValueRef sv = LLVMBuildLoad2(ctx->builder, ct_llvm, s_slot,
                                                 "cl.sv");
                LLVMValueRef cv = emit_clone_value(ctx, sv, ct_llvm, ct);
                LLVMBuildStore(ctx->builder, cv, d_slot);
            }
        }
        LLVMBuildRet(ctx->builder, c_dst);

        ctx->current_fn = c_saved_fn;
        if (c_saved_block) LLVMPositionBuilderAtEnd(ctx->builder, c_saved_block);
        env_clone_fn = clone_fn;
    }

    /* 9b) Construct the env value (heap) and store each capture into it.
       Field 0 = drop_fn slot; user captures live at fields 1..N. For
       by-move (string) captures we additionally write cap=-1 to the OUTER
       alloca so the outer scope's cleanup safely skips the now-transferred
       heap data. The runtime guard `cap > 0` keeps static strings (cap=0)
       from being mis-marked. For zero captures we leave env=NULL and skip
       all of this. */
    LLVMValueRef env_val = LLVMConstNull(ptr_t);
    if (cap_n > 0 && cap_outer_vals) {
        unsigned long long env_size_const =
            LLVMABISizeOfType(LLVMGetModuleDataLayout(ctx->module),
                              env_struct_t);
        LLVMValueRef sz_v = LLVMConstInt(LLVMInt64TypeInContext(ctx->context),
                                         env_size_const, 0);
        env_val = cg_emit_alloc(ctx, sz_v, "closure.env",
                                node->line, node->column);
        /* F.6: log env allocation (closure id + size + runtime ptr). */
        cg_dbg_env_alloc(ctx, id, env_size_const, env_val);

        /* drop_fn slot first (NULL for POD-only envs). */
        LLVMValueRef drop_slot = LLVMBuildStructGEP2(
            ctx->builder, env_struct_t, env_val, 0u, "env.dropslot");
        LLVMBuildStore(ctx->builder, env_drop_fn, drop_slot);

        /* Phase G: clone_fn slot at field 1. */
        LLVMValueRef clone_slot = LLVMBuildStructGEP2(
            ctx->builder, env_struct_t, env_val, 1u, "env.cloneslot");
        LLVMBuildStore(ctx->builder, env_clone_fn, clone_slot);

        /* P1: refcount slot at field 2 — a freshly-built env has exactly one
           live Block value (the one being returned here), so start at 1. */
        LLVMValueRef rc_slot = LLVMBuildStructGEP2(
            ctx->builder, env_struct_t, env_val, 2u, "env.rcslot");
        LLVMBuildStore(ctx->builder,
            LLVMConstInt(LLVMInt64TypeInContext(ctx->context), 1, 0), rc_slot);

        for (int i = 0; i < cap_n; i++) {
            Type *ct = node->as.closure.captures[i].type;
            LLVMValueRef field_ptr = LLVMBuildStructGEP2(
                ctx->builder, env_struct_t, env_val,
                (unsigned)(i + 3), "cap.slot");

            /* Store capture into env:
               - by-ref (map): cap_outer_vals[i] IS the outer alloca ptr,
                  so we're storing a pointer-to-alloca into the ptr-typed slot.
                 No ownership transfer; outer remains live.
               - by-move (string/struct/POD): cap_outer_vals[i] is a loaded
                 value; env takes ownership of the heap data.
               - by-clone (Block): deep-copy the captured Block's env so the
                 source Block stays live (par_for captures a Block param P
                 times); the env owns this independent clone and env_drop frees
                 it. See docs/plan_closure_foundation.md §2.4. */
            LLVMValueRef store_val = cap_outer_vals[i];
            if (ct->kind == TYPE_BLOCK)
                store_val = cg_emit_block_env_clone(ctx, cap_outer_vals[i]);
            LLVMBuildStore(ctx->builder, store_val, field_ptr);

            /* By-move marker on the outer alloca:
                 string: cap field gets -1 when currently > 0 (skip .rodata).
                 struct: moved_flag i1 alloca set to true.
                  [move] map: zero out outer map's cap field → __drop skips.
               Default by-ref map: outer is NOT marked at all. */
            bool explicit_move_i = node->as.closure.captures[i].is_explicit_move;
            bool is_default_by_ref_i = capture_type_is_by_ref_cg(ct) && !explicit_move_i;
            if (is_default_by_ref_i) {
                /* By-ref: outer is still live; do nothing. */
            } else if (capture_type_is_by_move_cg(ct) && cap_outer_allocas[i]) {
                if (ct->kind == TYPE_STRUCT || ct->kind == TYPE_ENUM) {
                    /* F.5: enum uses the same moved_flag mechanism as struct. */
                    const char *cap_name = node->as.closure.captures[i].name;
                    CgSymbol *outer_sym =
                        cg_scope_resolve(saved_scope, cap_name);
                    if (outer_sym && outer_sym->moved_flag) {
                        LLVMTypeRef i1_t = LLVMInt1TypeInContext(ctx->context);
                        LLVMBuildStore(ctx->builder,
                                       LLVMConstInt(i1_t, 1, 0),
                                       outer_sym->moved_flag);
                        /* F.6: log moved_flag=1 mark. */
                        cg_dbg_outer_mark(ctx, cap_name, "moved_flag=1");
                    }
                }
            }
        }
    }
    free(cap_outer_vals);
    free(cap_outer_allocas);

    /* 10) Materialise the LsBlock value: { fn_ptr, env_ptr }. */
    LLVMTypeRef block_llvm = type_to_llvm(ctx, block_t);
    LLVMValueRef val = LLVMGetUndef(block_llvm);
    val = LLVMBuildInsertValue(ctx->builder, val, fn, 0, "blk.fn");
    val = LLVMBuildInsertValue(ctx->builder, val, env_val, 1, "blk.env");

    /* 11) Stage 10 (B-4 unification): register the literal like every other
       fresh owned Block rvalue — spill the fat value to an entry slot and
       enter it in the temp_drop ledger (this replaced the separate
       temp_block_env SSA-value table). A consuming var_decl / return /
       store claims the entry (cg_claim_block_temp_above); an unconsumed
       rvalue (arg to a call that only borrowed it) is released by the
       statement-end flush via the slot. zeroed=true: the drop can be
       reached on a path that skipped a conditionally-emitted literal
       (same defense as tmp.rval.self); a zero slot releases env=NULL,
       a runtime no-op — which also covers the captureless case
       (env_val is ConstNull, release branches past it). */
    cg_spill_owned_rvalue(ctx, val, block_t, true, "blk.lit.tmp");
    return val;
}
