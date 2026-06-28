/* codegen_match.c
   match / try / force-unwrap 下沉 + match-arm 所有权 helper + int-switch + bit-pattern。见 docs/match_codegen_guide.md（6 臂体存储点 / 三 helper）

   Bodies mechanically relocated from codegen.c (docs/plan_codegen_split.md).
   No logic changes. All prototypes live in codegen_internal.h. */
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
static LLVMValueRef cg_emit_bit_pattern_seq(CodegenContext *ctx, AstNode *seq, LLVMValueRef subject, LLVMTypeRef subj_llvm, bool bind);
static void cg_match_arm_encapsulate(CodegenContext *ctx, int drop_floor, Type *result_type);
static LLVMValueRef cg_match_arm_own_tail(CodegenContext *ctx, AstNode *tail, LLVMValueRef body_val, LLVMTypeRef res_llvm, Type *result_type, int drop_floor, bool did_move_out_binder);
static AstNode *cg_match_arm_tail(AstNode *arm_body);
static bool cg_pattern_has_bit_seq(const AstNode *pat);
static int match_collect_int_vals(AstNode *pat, long long *out, int max);
static bool match_pattern_all_int_const(AstNode *pat);

/* L-013: unwrap a match-arm body to its tail expression (the value the arm yields).
   For a block body `=> { ...; E }` the tail is the last statement's expression;
   for a bare `=> E` the tail is E itself. Returns NULL if there is no value tail. */
static AstNode *cg_match_arm_tail(AstNode *arm_body)
{
    AstNode *tail = arm_body;
    if (tail && tail->kind == AST_BLOCK && tail->as.block.stmt_count > 0)
    {
        AstNode *last_s = tail->as.block.stmts[tail->as.block.stmt_count - 1];
        tail = (last_s && last_s->kind == AST_EXPR_STMT)
                   ? last_s->as.expr_stmt.expr
                   : NULL;
    }
    return tail;
}

/* L-013 (step 2+3): ensure the value an arm stores into result_alloca is owned
   INDEPENDENTLY by the result, returning the value to store.
   Clone is needed exactly when the tail aliases storage owned elsewhere with no
   fresh owned temp to transfer: an outer local or a borrowed payload binder. A
   freshly-produced rvalue temp (count grew) is transferred (not cloned); a binder
   we just moved out already owns its independent B2 clone (not cloned); a static
   literal / POD tail aliases no heap (stored as-is).
   `did_move_out_binder` = the arm's move-out optimization marked a payload binder
   borrowed this arm (enum path only; always false for non-enum patterns). */
static LLVMValueRef cg_match_arm_own_tail(CodegenContext *ctx, AstNode *tail,
                                          LLVMValueRef body_val, LLVMTypeRef res_llvm,
                                          Type *result_type, int drop_floor,
                                          bool did_move_out_binder)
{
    if (body_val == NULL || result_type == NULL)
        return body_val;
    bool owned_heap =
        (result_type->kind == TYPE_STRUCT && result_type->as.strukt.has_drop) ||
        (result_type->kind == TYPE_ENUM   && result_type->as.enom.has_drop);
    if (!owned_heap)
        return body_val;
    /* Fresh owned temp produced by this body → an rvalue we will transfer (no clone). */
    if (ctx->temp_drop_count > drop_floor)
        return body_val;
    /* A binder we just moved out already owns an independent clone (no clone). */
    if (did_move_out_binder)
        return body_val;
    /* Tail aliasing an owning IDENT (outer local, or borrowed binder) → clone so the
       result owns independently of the real owner. Static/POD tails alias nothing. */
    if (tail && tail->kind == AST_IDENT)
    {
        CgSymbol *s = cg_scope_resolve(ctx->current_scope, tail->as.ident.name);
        if (s && s->value && s->type &&
            ((s->type->kind == TYPE_STRUCT && s->type->as.strukt.has_drop) ||
             (s->type->kind == TYPE_ENUM   && s->type->as.enom.has_drop)))
            return emit_clone_value(ctx, body_val, res_llvm, result_type);
    }
    return body_val;
}

/* L-013 (step 2+3): encapsulate one match arm's statement-level temporaries after
   its body_val has been stored into result_alloca. The single owned tail value the
   arm yields is transferred to the result (which is registered as the lone result
   temp at the merge block); every OTHER arm-body temp is freed/dropped here so it
   does not leak into the outer statement temp tables.
   - drop_floor = temp_drop_count captured just before the body was evaluated.
     Pre-body temps (the subject drop at index < drop_floor) are untouched.
   - The tail temp matching result_type is neutralized (removed from the drop
     list) — the result owns its buffer. */
static void cg_match_arm_encapsulate(CodegenContext *ctx, int drop_floor,
                                     Type *result_type)
{
    bool res_is_drop =
        result_type &&
        ((result_type->kind == TYPE_STRUCT && result_type->as.strukt.has_drop) ||
         (result_type->kind == TYPE_ENUM   && result_type->as.enom.has_drop));

    LLVMBasicBlockRef cur = LLVMGetInsertBlock(ctx->builder);
    bool terminated = cur && LLVMGetBasicBlockTerminator(cur) != NULL;

    /* Transfer the tail has_drop temp into the result: remove the last body-registered
       drop entry without emitting its drop (the result owns that buffer). */
    if (res_is_drop && ctx->temp_drop_count > drop_floor)
        ctx->temp_drop_count--;
    /* Drop the remaining arm-body has_drop temps in [drop_floor, count). */
    if (!terminated)
        for (int i = drop_floor; i < ctx->temp_drop_count; i++)
        {
            Type *t = ctx->temp_drop_types[i];
            if (t->kind == TYPE_STRUCT)    emit_struct_drop(ctx, ctx->temp_drop_slots[i], t);
            else if (t->kind == TYPE_ENUM) emit_enum_drop(ctx, ctx->temp_drop_slots[i], t);
        }
    ctx->temp_drop_count = drop_floor;
}

/* Return true if 'pat' (possibly an OR-pattern tree) consists entirely of
   integer-literal leaves.  Wildcards are checked separately and skipped. */
static bool match_pattern_all_int_const(AstNode *pat)
{
    if (pat->kind == AST_MATCH_OR_PATTERN)
        return match_pattern_all_int_const(pat->as.or_pattern.left) &&
               match_pattern_all_int_const(pat->as.or_pattern.right);
    return pat->kind == AST_INT_LIT;
}

/* Flatten OR-pattern tree into an array of long-long integer values.
   Returns the number of values written (≤ max).  Non-INT_LIT leaves are
   silently skipped (should not happen when called after the int-const check). */
static int match_collect_int_vals(AstNode *pat, long long *out, int max)
{
    if (max <= 0) return 0;
    if (pat->kind == AST_MATCH_OR_PATTERN) {
        int n  = match_collect_int_vals(pat->as.or_pattern.left,  out,     max);
        int n2 = match_collect_int_vals(pat->as.or_pattern.right, out + n, max - n);
        return n + n2;
    }
    if (pat->kind == AST_INT_LIT) {
        out[0] = pat->as.int_lit.value;
        return 1;
    }
    return 0;
}

/* True if a match-arm pattern is a bit pattern (a seq, or an OR-tree of seqs). */
static bool cg_pattern_has_bit_seq(const AstNode *pat)
{
    if (pat == NULL) return false;
    if (pat->kind == AST_MATCH_BIT_PATTERN_SEQ) return true;
    if (pat->kind == AST_MATCH_OR_PATTERN)
        return cg_pattern_has_bit_seq(pat->as.or_pattern.left) ||
               cg_pattern_has_bit_seq(pat->as.or_pattern.right);
    return false;
}

/* V1 bit-pattern: emit field extraction for one bits[...] sequence on `subject`
   (an integer SSA value of type subj_llvm). Stores happen in the current insert
   block. When `bind`, materialise binder allocas (int / i64 / bool) and define
   them in the current scope so the arm body can read them. Returns the AND-combined
   match-value condition (i1), or NULL when the sequence has no match-value
   constraints (an unconditional match). */
static LLVMValueRef cg_emit_bit_pattern_seq(CodegenContext *ctx, AstNode *seq,
                                            LLVMValueRef subject, LLVMTypeRef subj_llvm,
                                            bool bind)
{
    LLVMValueRef cond = NULL;
    for (int i = 0; i < seq->as.bit_pattern_seq.count; i++)
    {
        AstNode *item = seq->as.bit_pattern_seq.items[i];
        int width = item->as.bit_pattern.width;
        int shift = item->as.bit_pattern.lsb_shift;
        unsigned long long maskv = (width >= 64) ? ~0ULL : ((1ULL << width) - 1ULL);
        LLVMValueRef mask = LLVMConstInt(subj_llvm, maskv, 0);

        LLVMValueRef shifted = (shift != 0)
            ? LLVMBuildLShr(ctx->builder, subject,
                            LLVMConstInt(subj_llvm, (unsigned long long)shift, 0), "bit.shr")
            : subject;
        LLVMValueRef field = LLVMBuildAnd(ctx->builder, shifted, mask, "bit.val");

        /* match-value constraint: (field == match_val) */
        if (item->as.bit_pattern.match_value_set)
        {
            LLVMValueRef want = LLVMConstInt(subj_llvm,
                (unsigned long long)item->as.bit_pattern.match_val, 0);
            LLVMValueRef cmp = LLVMBuildICmp(ctx->builder, LLVMIntEQ, field, want, "bit.cmp");
            cond = cond ? LLVMBuildAnd(ctx->builder, cond, cmp, "bit.and") : cmp;
        }

        if (bind && item->as.bit_pattern.name != NULL)
        {
            /* Binder type chosen so the zero-extended field is always non-negative:
               width 1 → bool; 2..31 → int (i32, top bit is bit30 at most, sign bit
               unreachable); 32..63 → i64 (a 32-bit field would fill i32's sign bit and
               read negative, so widen to i64). A genuine 64-bit field should be read
               whole via be_u64, never bit-matched (it can't be split anyway). */
            Type *bt = (width == 1) ? type_bool()
                                    : (width < 32 ? type_int() : type_i64());
            LLVMTypeRef slot_ty = type_to_llvm(ctx, bt);
            unsigned dw = LLVMGetIntTypeWidth(slot_ty);
            LLVMValueRef stored;
            if (width == 1)
            {
                /* bool binder: field != 0, sized to bool's storage type */
                LLVMValueRef b = LLVMBuildICmp(ctx->builder, LLVMIntNE, field,
                                               LLVMConstInt(subj_llvm, 0, 0), "bit.bool");
                stored = (dw == 1) ? b
                                   : LLVMBuildZExt(ctx->builder, b, slot_ty, "bit.boolz");
            }
            else
            {
                /* resize the masked field from subj width to the binder width */
                unsigned sw = LLVMGetIntTypeWidth(subj_llvm);
                if (sw == dw)      stored = field;
                else if (sw > dw)  stored = LLVMBuildTrunc(ctx->builder, field, slot_ty, "bit.trunc");
                else               stored = LLVMBuildZExt(ctx->builder, field, slot_ty, "bit.zext");
            }
            LLVMValueRef slot = cg_entry_alloca(ctx, slot_ty, item->as.bit_pattern.name);
            LLVMBuildStore(ctx->builder, stored, slot);
            cg_scope_define(ctx->current_scope, item->as.bit_pattern.name, slot, bt, NULL);
        }
    }
    return cond;
}

/* Extracted from codegen_expr's switch (codegen.c split Step 4): the
   AST_MATCH case body, verbatim. Behavior unchanged — ctx->current_node is
   already set by codegen_expr before dispatch. */
LLVMValueRef codegen_match_expr(CodegenContext *ctx, AstNode *node)
{
        /* Compile match as cascading if-else.
           Subject is only read (compared against patterns), so borrow vec[i] strings. */
        LLVMValueRef subject = codegen_expr_or_borrow(ctx, node->as.match.subject);
        if (subject == NULL)
            return NULL;

        Type *result_type = node->resolved_type;
        LLVMTypeRef res_llvm = result_type ? type_to_llvm(ctx, result_type)
                                           : LLVMInt32TypeInContext(ctx->context);

        LLVMBasicBlockRef merge_bb = LLVMAppendBasicBlockInContext(
            ctx->context, ctx->current_fn, "match.end");

        /* Protect the subject temp(s) from arm-internal statement flushes: raise
           the flush floor to the current live-temp count after the subject is
           pushed (set below), restore on the enum path's exits. Without this an
           `@print`/`if`/`while` inside an arm resets temp_drop_count to 0,
           collapsing below the arm's drop_floor — later borrow-arg temps then
           reuse the freed low slots, and a not-taken branch leaves one
           uninitialised, re-exposed by the arm encapsulate (invalid free). */
        int saved_drop_base = ctx->temp_drop_base;

        /* Alloca for result */
        LLVMValueRef result_alloca = NULL;
        if (result_type && result_type->kind != TYPE_VOID)
        {
            LLVMBasicBlockRef entry = LLVMGetEntryBasicBlock(ctx->current_fn);
            LLVMBuilderRef tmp = LLVMCreateBuilderInContext(ctx->context);
            LLVMValueRef first_inst = LLVMGetFirstInstruction(entry);
            if (first_inst)
                LLVMPositionBuilderBefore(tmp, first_inst);
            else
                LLVMPositionBuilderAtEnd(tmp, entry);
            result_alloca = LLVMBuildAlloca(tmp, res_llvm, "match.res");
            /* L-013: zero-initialize so a path that reaches merge without storing
               (e.g. a non-exhaustive integer match's default branch) leaves the
               registered result temp with cap=0 / empty → its free/drop is skipped. */
            LLVMBuildStore(tmp, LLVMConstNull(res_llvm), result_alloca);
            LLVMDisposeBuilder(tmp);
        }

        Type *subj_type = node->as.match.subject->resolved_type;
        bool is_fp = subj_type && type_is_float(subj_type);

        /* L-012: an OWNED rvalue-temp scrutinee (a call/index/field/ctor result,
           not a named var or borrow) has no other owner, so the match itself must
           drop it. For such subjects we (a) clone every has_drop binder so it is
           independent of the subject, and (b) register the subject for drop. A
           named-var / &self subject keeps the existing borrow behavior (its owner
           drops it; binders alias read-only). */
        bool subj_owned_temp =
            subj_type && subj_type->kind == TYPE_ENUM &&
            subj_type->as.enom.has_drop &&
            node->as.match.subject->kind != AST_IDENT &&
            node->as.match.subject->kind != AST_UNARY &&
            node->as.match.subject->kind != AST_MUT_BORROW;

        /* ---- Enum subject: switch on discriminant + binder extraction ---- */
        if (subj_type && subj_type->kind == TYPE_ENUM)
        {
            LLVMTypeRef enum_llvm = type_to_llvm(ctx, subj_type);
            LLVMTypeRef i8 = LLVMInt8TypeInContext(ctx->context);
            LLVMTypeRef ptr_type = LLVMPointerTypeInContext(ctx->context, 0);

            /* Phase 9: detect borrow subject — AST_IDENT with is_borrowed=true.
               For &Enum params, sym->value IS the pointer; skip alloca+store copy
               and GEP directly through the pointer (zero-copy borrow match). */
            bool subj_is_enum_borrow = false;
            LLVMValueRef subj_ptr_val = NULL; /* pointer to the enum, borrow path */
            {
                AstNode *sn = node->as.match.subject;
                if (sn->kind == AST_IDENT) {
                    CgSymbol *bsym = cg_scope_resolve(ctx->current_scope,
                                                      sn->as.ident.name);
                    if (bsym && bsym->is_borrowed) {
                        subj_is_enum_borrow = true;
                        subj_ptr_val = bsym->value;
                    }
                }
            }

            LLVMValueRef subj_alloca;  /* pointer (alloca or incoming ptr) used for GEP */
            LLVMBasicBlockRef entry = LLVMGetEntryBasicBlock(ctx->current_fn);
            if (subj_is_enum_borrow) {
                /* Borrow path: use the incoming pointer directly — no copy. */
                subj_alloca = subj_ptr_val;
            } else {
                /* Owned path: stash subject in an alloca so we can GEP into the payload. */
                LLVMBuilderRef tmp_b = LLVMCreateBuilderInContext(ctx->context);
                LLVMValueRef first_inst = LLVMGetFirstInstruction(entry);
                if (first_inst) LLVMPositionBuilderBefore(tmp_b, first_inst);
                else            LLVMPositionBuilderAtEnd(tmp_b, entry);
                subj_alloca = LLVMBuildAlloca(tmp_b, enum_llvm, "match.subj");
                LLVMDisposeBuilder(tmp_b);
                LLVMBuildStore(ctx->builder, subject, subj_alloca);

                /* L-012: own the temp scrutinee — drop it at statement end / on return
                   (binders below are cloned, so this never double-frees). */
                if (subj_owned_temp)
                    cg_push_temp_drop(ctx, subj_alloca, subj_type);
            }

            /* Subject (and any enclosing temps) are now live below the arm bodies;
               keep arm-internal flushes from collapsing past them. */
            ctx->temp_drop_base = ctx->temp_drop_count;

            LLVMValueRef disc_ptr = LLVMBuildStructGEP2(ctx->builder, enum_llvm, subj_alloca, 0, "disc.p");
            LLVMValueRef disc = LLVMBuildLoad2(ctx->builder, i8, disc_ptr, "disc");
            LLVMValueRef payload_ptr = LLVMBuildStructGEP2(ctx->builder, enum_llvm, subj_alloca, 1, "payload.p");

            /* Default block: holds wildcard arm (if any) or unreachable. */
            LLVMBasicBlockRef default_bb = LLVMAppendBasicBlockInContext(
                ctx->context, ctx->current_fn, "match.default");

            /* Count concrete (non-wildcard) arms */
            int concrete_arms = 0;
            for (int i = 0; i < node->as.match.arm_count; i++)
            {
                AstNode *pat = node->as.match.arms[i].pattern;
                bool is_wild = pat->kind == AST_IDENT && strcmp(pat->as.ident.name, "_") == 0;
                if (!is_wild) concrete_arms++;
            }

            LLVMValueRef switch_inst = LLVMBuildSwitch(ctx->builder, disc,
                                                       default_bb, (unsigned)concrete_arms);

            bool default_used = false;
            for (int i = 0; i < node->as.match.arm_count; i++)
            {
                MatchArm *arm = &node->as.match.arms[i];
                AstNode *pat = arm->pattern;
                bool is_wild = pat->kind == AST_IDENT && strcmp(pat->as.ident.name, "_") == 0;

                if (is_wild)
                {
                    /* Wildcard → fill in the default block */
                    LLVMPositionBuilderAtEnd(ctx->builder, default_bb);
                    default_used = true;
                    int arm_drop_floor = ctx->temp_drop_count;
                    LLVMValueRef body_val = codegen_expr(ctx, arm->body);
                    if (result_alloca && body_val)
                    {
                        body_val = cg_match_arm_own_tail(
                            ctx, cg_match_arm_tail(arm->body), body_val, res_llvm,
                            result_type, arm_drop_floor,
                            /*did_move_out_binder=*/false);
                        LLVMBuildStore(ctx->builder, body_val, result_alloca);
                    }
                    cg_match_arm_encapsulate(ctx, arm_drop_floor, result_type);
                    if (LLVMGetBasicBlockTerminator(LLVMGetInsertBlock(ctx->builder)) == NULL)
                        LLVMBuildBr(ctx->builder, merge_bb);
                    continue;
                }

                /* Resolve variant name + binder list */
                const char *vname = NULL;
                AstNode **binders = NULL;
                int binder_count = 0;
                if (pat->kind == AST_IDENT) {
                    vname = pat->as.ident.name;
                } else if (pat->kind == AST_CALL && pat->as.call.callee->kind == AST_IDENT) {
                    vname = pat->as.call.callee->as.ident.name;
                    binders = pat->as.call.args;
                    binder_count = pat->as.call.arg_count;
                }
                if (vname == NULL) continue;

                int variant_idx = -1;
                for (int v = 0; v < subj_type->as.enom.variant_count; v++) {
                    if (strcmp(subj_type->as.enom.variants[v].name, vname) == 0) {
                        variant_idx = v; break;
                    }
                }
                if (variant_idx < 0) continue;  /* checker should have caught this */

                /* Add a case block for this variant */
                LLVMBasicBlockRef case_bb = LLVMAppendBasicBlockInContext(
                    ctx->context, ctx->current_fn, "match.case");
                LLVMAddCase(switch_inst,
                            LLVMConstInt(i8, (unsigned long long)variant_idx, 0),
                            case_bb);

                LLVMPositionBuilderAtEnd(ctx->builder, case_bb);

                /* Bind each payload field into a fresh scope */
                push_scope(ctx);
                if (binder_count > 0) {
                    LLVMTypeRef variant_struct = build_variant_payload_struct(ctx, subj_type, variant_idx);
                    /* payload_ptr aliases the [N x i8] storage in the enum struct.
                       We GEP into it as the variant's struct layout. */
                    for (int b = 0; b < binder_count; b++) {
                        AstNode *bnode = binders[b];
                        if (bnode->kind != AST_IDENT) continue;
                        const char *bname = bnode->as.ident.name;
                        if (strcmp(bname, "_") == 0) continue;

                        Type *pt = subj_type->as.enom.variants[variant_idx].payload_types[b];
                        LLVMValueRef field_ptr = LLVMBuildStructGEP2(
                            ctx->builder, variant_struct, payload_ptr, (unsigned)b, "binder.p");

                        LLVMTypeRef field_llvm = type_to_llvm(ctx, pt);

                        /* Phase 9 / Phase B: borrow subject — owned payload bindings
                           are borrows: zero-copy for vec/map/struct/nested-enum via
                           direct field pointer; cap-marked borrow for string. */
                        if (subj_is_enum_borrow) {
                            /* Phase A: self-recursive (box) payload. */
                            if (pt == subj_type) {
                                LLVMValueRef box = LLVMBuildLoad2(ctx->builder, ptr_type,
                                                                  field_ptr, "box");
                                CgSymbol *sym = cg_scope_define(ctx->current_scope, bname,
                                                                box, pt, NULL);
                                if (sym) sym->is_borrowed = true;
                                continue;
                            }

                            /* Phase B: vec/map/struct/nested-has_drop-enum payload.
                               Use field_ptr directly as sym->value (pointer into the
                               enum payload storage) — zero-copy, same ABI as &T params. */
                            if (pt->kind == TYPE_STRUCT ||
                                (pt->kind == TYPE_ENUM && pt->as.enom.has_drop)) {
                                CgSymbol *sym = cg_scope_define(ctx->current_scope, bname,
                                                                field_ptr, pt, NULL);
                                if (sym) sym->is_borrowed = true;
                                continue;
                            }

                            /* Scalars (int/f64/bool/char) and other non-owned types:
                               fall through to the normal load-into-alloca path below. */
                        }

                        LLVMValueRef val;
                        if (pt == subj_type) {
                            /* Self-recursive payload (owned subject): payload slot stores
                               an i8* pointing at a heap-boxed enum.  Load the box
                               pointer, then load the enum value through it. */
                            LLVMValueRef box = LLVMBuildLoad2(ctx->builder, ptr_type,
                                                              field_ptr, "box");
                            val = LLVMBuildLoad2(ctx->builder, field_llvm, box, bname);
                        } else {
                            val = LLVMBuildLoad2(ctx->builder, field_llvm, field_ptr, bname);
                        }

                        /* Determine whether the binder needs an independent owned copy.
                           Without cloning, a string binder shares the enum's data pointer.
                           If the binder escapes the arm (via `return s`), both the caller
                           and the enum's drop (env_drop or scope cleanup) would free the
                           same allocation → double-free.
                           Fix: clone string payloads so each binder independently owns
                           its data.  With independent ownership, is_borrowed=false and
                           scope cleanup frees the binder's copy when the arm exits
                           (unless the binder is being returned, in which case the
                           return_alloca skip list suppresses the scope drop). */
                        bool binder_owns = false;
                        if (subj_owned_temp && pt && cg_type_owns_heap_for_enum(pt)) {
                            if (cg_struct_is_move_only(pt)) {
                                /* Move-only payload (Destroy + raw ptr/object field,
                                   no Clone — e.g. a File handle): it cannot be cloned.
                                   MOVE it out instead — keep `val` as the binder's
                                   value and zero the subject's payload slot so the
                                   subject's wholesale drop (cg_push_temp_drop below)
                                   sees an empty field and the destructor's nil guard
                                   no-ops on it. The binder then solely owns the
                                   resource and drops once at arm exit. Enables
                                   `match open(p) { Ok(f) => ... }` for move-only
                                   resource types (previously a compile error). */
                                LLVMBuildStore(ctx->builder,
                                               LLVMConstNull(field_llvm), field_ptr);
                            } else {
                                /* Owned-temp subject: clone every has_drop binder so
                                   it is independent of the subject (which we drop). */
                                val = emit_clone_value(ctx, val, field_llvm, pt);
                            }
                            binder_owns = true;
                        }

                        /* Materialise an alloca so existing IDENT-load paths work,
                           then bind in current scope.  Non-string (or non-cloned)
                           binders are marked borrowed so scope cleanup leaves heap
                           ownership with the enum subject. */
                        LLVMBuilderRef bb_tmp = LLVMCreateBuilderInContext(ctx->context);
                        LLVMValueRef first_i = LLVMGetFirstInstruction(entry);
                        if (first_i) LLVMPositionBuilderBefore(bb_tmp, first_i);
                        else         LLVMPositionBuilderAtEnd(bb_tmp, entry);
                        LLVMValueRef bind_alloca = LLVMBuildAlloca(bb_tmp, field_llvm, bname);
                        LLVMDisposeBuilder(bb_tmp);
                        LLVMBuildStore(ctx->builder, val, bind_alloca);
                        /* An owned has_drop binder gets a moved_flag so a move that
                           CONSUMES it mid-arm — e.g. `Ok(v) => { return Ok(v) }`,
                           where v is passed by value into a ctor and returned out —
                           suppresses the arm-scope drop via cg_invalidate_moved_source,
                           exactly like a plain owned local. Without it the binder was
                           dropped here AND owned by the returned value → double-free
                           (0xC0000374), masked for cap-0/POD/empty payloads; an owned
                           Vec/Str payload corrupts the heap. The `=> v` tail-yield
                           move-out is handled separately below (is_borrowed). */
                        LLVMValueRef binder_moved_flag = NULL;
                        if (binder_owns) {
                            LLVMTypeRef i1t = LLVMInt1TypeInContext(ctx->context);
                            binder_moved_flag = cg_entry_alloca(ctx, i1t, "binder.moved");
                            LLVMBuildStore(ctx->builder,
                                           LLVMConstInt(i1t, 0, 0), binder_moved_flag);
                        }
                        CgSymbol *sym = cg_scope_define(ctx->current_scope, bname,
                                                        bind_alloca, pt, binder_moved_flag);
                        if (sym) sym->is_borrowed = !binder_owns;
                    }
                }

                int arm_drop_floor = ctx->temp_drop_count;
                LLVMValueRef body_val = codegen_expr(ctx, arm->body);
                bool did_move_out_binder = false;
                AstNode *tail = cg_match_arm_tail(arm->body);
                /* BF-026 / BF-029 / VR-LIM-020: a match arm clones every owned
                   has_drop payload binder (binder_owns above), so the arm scope
                   normally frees that clone on exit to avoid leaks.
                   EXCEPTION — "move out": when the arm's RESULT value is exactly one
                   of those binders, the value is transferred to the match result (and
                   on to whoever owns it), so freeing it here would double-free. The
                   tail value is the binder both for `=> binder` and for the block form
                   `=> { ...; binder }`. Suppress the binder's scope-cleanup drop by
                   marking it borrowed — uniform across string / has_drop struct·enum /
                   map (body_val, the already-loaded SSA, is what the caller receives). */
                if (body_val && tail && tail->kind == AST_IDENT)
                {
                    /* Resolve ONLY in the arm scope (the payload binders), not in
                       outer scopes — we must not silently move out outer locals. */
                    for (int si = ctx->current_scope->count - 1; si >= 0; si--)
                    {
                        CgSymbol *bs = &ctx->current_scope->symbols[si];
                        if (!bs->name ||
                            strcmp(bs->name, tail->as.ident.name) != 0)
                            continue;
                        Type *bt = bs->type;
                        bool owns_heap =
                            bt && !bs->is_borrowed && bs->value &&
                            ((bt->kind == TYPE_STRUCT && bt->as.strukt.has_drop) ||
                             (bt->kind == TYPE_ENUM && bt->as.enom.has_drop));
                        if (owns_heap)
                        {
                            bs->is_borrowed = true; /* skip drop: moved out */
                            did_move_out_binder = true;
                        }
                        break;
                    }
                }
                /* L-013: make the result OWN its value independently. Clone a tail that
                   aliases an outer local or a borrowed binder (must run while the arm
                   scope is still alive so the tail IDENT resolves to the binder). */
                if (result_alloca && body_val)
                    body_val = cg_match_arm_own_tail(ctx, tail, body_val, res_llvm,
                                                     result_type,
                                                     arm_drop_floor, did_move_out_binder);
                if (result_alloca && body_val)
                    LLVMBuildStore(ctx->builder, body_val, result_alloca);
                emit_scope_cleanup(ctx);
                pop_scope(ctx);
                /* L-013: encapsulate arm-body temps (transfer the tail temp into the
                   result, free the rest). Subject drop (index < arm_drop_floor) and
                   outer temps are preserved. */
                cg_match_arm_encapsulate(ctx, arm_drop_floor, result_type);
                /* Guard: arm body may end with 'return', which already terminates
                   the block.  Only emit the merge-branch when the block is still
                   open (no terminator yet). */
                if (LLVMGetBasicBlockTerminator(LLVMGetInsertBlock(ctx->builder)) == NULL)
                    LLVMBuildBr(ctx->builder, merge_bb);
            }

            /* Fill in the default block with unreachable when no wildcard arm. */
            if (!default_used) {
                LLVMPositionBuilderAtEnd(ctx->builder, default_bb);
                LLVMBuildUnreachable(ctx->builder);
            }

            LLVMPositionBuilderAtEnd(ctx->builder, merge_bb);

            /* Drop the match subject if it's an rvalue temp (e.g. function-call
               result like `match io.read_file(p) { ... }`). The enum's payload
               might own heap data (Ok(string)/Err(string) etc.) and binders are
               borrowed (is_borrowed=true above), so without this drop the heap
               buffer leaks. Skip the drop when the subject is a named scope
               variable — its own scope cleanup will handle it. */
            if (subj_type->as.enom.has_drop) {
                bool subject_owned_by_scope = false;
                AstNode *subj_node = node->as.match.subject;
                if (subj_node->kind == AST_IDENT) {
                    CgSymbol *sym = cg_scope_resolve(ctx->current_scope,
                                                     subj_node->as.ident.name);
                    if (sym && !sym->is_borrowed && !sym->is_mut_borrow)
                        subject_owned_by_scope = true;
                    /* Borrowed self-recursive enum identifiers (e.g. sum_tree's
                       parameter `t`) share heap boxes with the caller. The
                       match.subj copy aliases those boxes, so dropping it
                       recursively would double-free with the caller. Skip drop. */
                    if (sym && sym->is_borrowed)
                        subject_owned_by_scope = true;
                }
                /* Self-recursive enums (Tree { Node(int, Tree, Tree) }) don't
                   benefit from a match.subj drop in general: the subject is
                   either a named owned variable (already covered) or a value
                   from a recursive call — in both cases the box hierarchy is
                   shared and recursive drop here causes double-free with the
                   real owner's later cleanup. */
                bool is_self_recursive = false;
                for (int v = 0; v < subj_type->as.enom.variant_count && !is_self_recursive; v++)
                {
                    int pc = subj_type->as.enom.variants[v].payload_count;
                    for (int j = 0; j < pc; j++)
                    {
                        if (subj_type->as.enom.variants[v].payload_types[j] == subj_type)
                        { is_self_recursive = true; break; }
                    }
                }
                if (!subject_owned_by_scope && !is_self_recursive) {
                    emit_enum_drop(ctx, subj_alloca, subj_type);
                    /* The owned-temp subject is ALSO on the temp-drop list (L-012,
                       which covers early-return arms this merge-block drop misses).
                       Having just dropped it here on the fall-through path, remove
                       it from that list so the statement-end flush does not drop it
                       a SECOND time. Without this, the double drop is masked for
                       idempotent string free (cap zeroed) but double-frees user
                       structs/containers whose __drop doesn't zero cap (Vec/Map). */
                    cg_remove_temp_drop(ctx, subj_alloca);
                }
            }

            /* L-013: the match result is now an owned-rvalue funneled through
               result_alloca (each arm transferred/cloned its owned tail into it).
               Register it as the single statement-level result temp so the consumer
               (var_decl/assign/return/call-arg) transfers it via the existing
               "last temp moved" protocol — exactly one drop, no leak / no double-free.
               Non-owned (static/POD) results are no-ops here. */
            ctx->temp_drop_base = saved_drop_base;  /* leave arm scope: drop the protection */

            if (result_alloca)
                return LLVMBuildLoad2(ctx->builder, res_llvm, result_alloca, "match.val");
            return NULL;
        }

        /* ---- Non-enum subject ----
           Two sub-paths:
           (A) Integer switch: subject is integer-typed AND every non-wildcard
               pattern leaf is an AST_INT_LIT.  We emit a single LLVM switch
               instruction (like the enum path above), supporting OR-patterns
               that map multiple constants to one arm body.
           (B) CondBr chain: string, float, or patterns that contain variables.
               OR-patterns are supported by flattening the tree into leaves and
               OR-ing the comparisons together before the CondBr.
           (C) Bit-pattern chain (V1): arms use `bits[...]`. Each arm extracts
               fields (shift/and), binds variables, and AND/OR-combines any
               match-value constraints into a CondBr. */

        bool cg_has_bit = false;
        for (int i = 0; i < node->as.match.arm_count; i++)
            if (cg_pattern_has_bit_seq(node->as.match.arms[i].pattern)) { cg_has_bit = true; break; }

        if (cg_has_bit)
        {
            LLVMTypeRef subj_llvm = type_to_llvm(ctx, subj_type);
            LLVMTypeRef i1_ty = LLVMInt1TypeInContext(ctx->context);

            for (int i = 0; i < node->as.match.arm_count; i++)
            {
                MatchArm *arm = &node->as.match.arms[i];
                AstNode  *pat = arm->pattern;
                bool is_wild = pat->kind == AST_IDENT &&
                               strcmp(pat->as.ident.name, "_") == 0;

                if (is_wild)
                {
                    /* Wildcard runs in the current (last next_bb) block. */
                    int arm_drop_floor = ctx->temp_drop_count;
                    LLVMValueRef body_val = codegen_expr(ctx, arm->body);
                    if (result_alloca && body_val)
                    {
                        body_val = cg_match_arm_own_tail(
                            ctx, cg_match_arm_tail(arm->body), body_val, res_llvm,
                            result_type, arm_drop_floor, false);
                        LLVMBuildStore(ctx->builder, body_val, result_alloca);
                    }
                    cg_match_arm_encapsulate(ctx, arm_drop_floor, result_type);
                    if (LLVMGetBasicBlockTerminator(LLVMGetInsertBlock(ctx->builder)) == NULL)
                        LLVMBuildBr(ctx->builder, merge_bb);
                    continue;
                }

                LLVMBasicBlockRef then_bb = LLVMAppendBasicBlockInContext(
                    ctx->context, ctx->current_fn, "match.then");
                LLVMBasicBlockRef next_bb = LLVMAppendBasicBlockInContext(
                    ctx->context, ctx->current_fn, "match.next");

                /* Collect OR-tree leaves left-to-right (each is a bit seq). */
                AstNode *leaves[64]; int nleaves = 0;
                {
                    AstNode *stk[64]; int sp = 0; stk[sp++] = pat;
                    while (sp > 0 && nleaves < 64)
                    {
                        AstNode *cur = stk[--sp];
                        if (cur->kind == AST_MATCH_OR_PATTERN)
                        {
                            if (sp + 2 <= 64) {
                                stk[sp++] = cur->as.or_pattern.right;
                                stk[sp++] = cur->as.or_pattern.left;
                            }
                        }
                        else
                            leaves[nleaves++] = cur;
                    }
                }

                /* Extract + bind (first leaf binds) in the current block, build the
                   OR-combined arm condition. A leaf with no match-value is an
                   unconditional match → contributes constant-true. */
                push_scope(ctx);
                LLVMValueRef combined = NULL;
                for (int L = 0; L < nleaves; L++)
                {
                    LLVMValueRef leaf_cond = cg_emit_bit_pattern_seq(
                        ctx, leaves[L], subject, subj_llvm, /*bind=*/L == 0);
                    if (leaf_cond == NULL)
                        leaf_cond = LLVMConstInt(i1_ty, 1, 0);
                    combined = combined
                        ? LLVMBuildOr(ctx->builder, combined, leaf_cond, "bit.orleaf")
                        : leaf_cond;
                }
                if (combined == NULL) combined = LLVMConstInt(i1_ty, 1, 0);
                LLVMBuildCondBr(ctx->builder, combined, then_bb, next_bb);

                LLVMPositionBuilderAtEnd(ctx->builder, then_bb);
                int arm_drop_floor = ctx->temp_drop_count;
                LLVMValueRef body_val = codegen_expr(ctx, arm->body);
                if (result_alloca && body_val)
                {
                    body_val = cg_match_arm_own_tail(
                        ctx, cg_match_arm_tail(arm->body), body_val, res_llvm,
                        result_type, arm_drop_floor, false);
                    LLVMBuildStore(ctx->builder, body_val, result_alloca);
                }
                cg_match_arm_encapsulate(ctx, arm_drop_floor, result_type);
                pop_scope(ctx);
                if (LLVMGetBasicBlockTerminator(LLVMGetInsertBlock(ctx->builder)) == NULL)
                    LLVMBuildBr(ctx->builder, merge_bb);

                LLVMPositionBuilderAtEnd(ctx->builder, next_bb);
            }

            /* Last next_bb (or wildcard block) falls through to merge. */
            if (LLVMGetBasicBlockTerminator(LLVMGetInsertBlock(ctx->builder)) == NULL)
                LLVMBuildBr(ctx->builder, merge_bb);

            LLVMPositionBuilderAtEnd(ctx->builder, merge_bb);
            if (result_alloca)
                return LLVMBuildLoad2(ctx->builder, res_llvm, result_alloca, "match.val");
            return NULL;
        }

        bool use_int_switch = false;
        if (subj_type && !is_fp)
        {
            /* All non-wildcard patterns must be integer constants. */
            use_int_switch = true;
            for (int i = 0; i < node->as.match.arm_count; i++)
            {
                AstNode *pat = node->as.match.arms[i].pattern;
                bool is_wild = pat->kind == AST_IDENT &&
                               strcmp(pat->as.ident.name, "_") == 0;
                if (is_wild) continue;
                if (!match_pattern_all_int_const(pat))
                {
                    use_int_switch = false;
                    break;
                }
            }
        }

        if (use_int_switch)
        {
            /* ---- (A) LLVM switch instruction for integer subjects ---- */
            LLVMTypeRef subj_llvm = type_to_llvm(ctx, subj_type);

            LLVMBasicBlockRef default_bb = LLVMAppendBasicBlockInContext(
                ctx->context, ctx->current_fn, "match.default");

            /* Count total switch cases across all OR-pattern leaves. */
            int total_cases = 0;
            for (int i = 0; i < node->as.match.arm_count; i++)
            {
                AstNode *pat = node->as.match.arms[i].pattern;
                bool is_wild = pat->kind == AST_IDENT &&
                               strcmp(pat->as.ident.name, "_") == 0;
                if (!is_wild)
                {
                    long long tmp[64];
                    total_cases += match_collect_int_vals(pat, tmp, 64);
                }
            }

            LLVMValueRef switch_inst = LLVMBuildSwitch(ctx->builder, subject,
                                                       default_bb, (unsigned)total_cases);
            bool default_used = false;

            for (int i = 0; i < node->as.match.arm_count; i++)
            {
                MatchArm *arm = &node->as.match.arms[i];
                AstNode  *pat = arm->pattern;
                bool is_wild  = pat->kind == AST_IDENT &&
                                strcmp(pat->as.ident.name, "_") == 0;

                if (is_wild)
                {
                    /* Wildcard → default block */
                    LLVMPositionBuilderAtEnd(ctx->builder, default_bb);
                    default_used = true;
                    int arm_drop_floor = ctx->temp_drop_count;
                    LLVMValueRef body_val = codegen_expr(ctx, arm->body);
                    if (result_alloca && body_val)
                    {
                        body_val = cg_match_arm_own_tail(
                            ctx, cg_match_arm_tail(arm->body), body_val, res_llvm,
                            result_type, arm_drop_floor, false);
                        LLVMBuildStore(ctx->builder, body_val, result_alloca);
                    }
                    cg_match_arm_encapsulate(ctx, arm_drop_floor, result_type);
                    if (LLVMGetBasicBlockTerminator(LLVMGetInsertBlock(ctx->builder)) == NULL)
                        LLVMBuildBr(ctx->builder, merge_bb);
                }
                else
                {
                    /* Create one body block; add all OR-pattern constants as cases. */
                    LLVMBasicBlockRef body_bb = LLVMAppendBasicBlockInContext(
                        ctx->context, ctx->current_fn, "match.case");

                    long long vals[64];
                    int nvals = match_collect_int_vals(pat, vals, 64);
                    for (int j = 0; j < nvals; j++)
                    {
                        LLVMValueRef case_val = LLVMConstInt(subj_llvm,
                                                             (unsigned long long)vals[j],
                                                             /*sign_extend=*/1);
                        LLVMAddCase(switch_inst, case_val, body_bb);
                    }

                    LLVMPositionBuilderAtEnd(ctx->builder, body_bb);
                    int arm_drop_floor = ctx->temp_drop_count;
                    LLVMValueRef body_val = codegen_expr(ctx, arm->body);
                    if (result_alloca && body_val)
                    {
                        body_val = cg_match_arm_own_tail(
                            ctx, cg_match_arm_tail(arm->body), body_val, res_llvm,
                            result_type, arm_drop_floor, false);
                        LLVMBuildStore(ctx->builder, body_val, result_alloca);
                    }
                    cg_match_arm_encapsulate(ctx, arm_drop_floor, result_type);
                    if (LLVMGetBasicBlockTerminator(LLVMGetInsertBlock(ctx->builder)) == NULL)
                        LLVMBuildBr(ctx->builder, merge_bb);
                }
            }

            if (!default_used)
            {
                /* No wildcard arm — default block falls through to merge. */
                LLVMPositionBuilderAtEnd(ctx->builder, default_bb);
                LLVMBuildBr(ctx->builder, merge_bb);
            }
        }
        else
        {
            /* ---- (B) CondBr chain (string / float / non-const patterns) ---- */
            for (int i = 0; i < node->as.match.arm_count; i++)
            {
                MatchArm *arm = &node->as.match.arms[i];
                bool is_wildcard = arm->pattern->kind == AST_IDENT &&
                                   strcmp(arm->pattern->as.ident.name, "_") == 0;

                if (is_wildcard)
                {
                    int arm_drop_floor = ctx->temp_drop_count;
                    LLVMValueRef body_val = codegen_expr(ctx, arm->body);
                    if (result_alloca && body_val)
                    {
                        body_val = cg_match_arm_own_tail(
                            ctx, cg_match_arm_tail(arm->body), body_val, res_llvm,
                            result_type, arm_drop_floor, false);
                        LLVMBuildStore(ctx->builder, body_val, result_alloca);
                    }
                    cg_match_arm_encapsulate(ctx, arm_drop_floor, result_type);
                    if (LLVMGetBasicBlockTerminator(LLVMGetInsertBlock(ctx->builder)) == NULL)
                        LLVMBuildBr(ctx->builder, merge_bb);
                }
                else
                {
                    /* Flatten OR-pattern tree into leaf array. */
                    AstNode *leaves[64];
                    int nleaves = 0;
                    {
                        AstNode *stk[64]; int sp = 0;
                        stk[sp++] = arm->pattern;
                        while (sp > 0 && nleaves < 64)
                        {
                            AstNode *cur = stk[--sp];
                            if (cur->kind == AST_MATCH_OR_PATTERN)
                            {
                                if (sp + 2 <= 64) {
                                    stk[sp++] = cur->as.or_pattern.right;
                                    stk[sp++] = cur->as.or_pattern.left;
                                }
                            }
                            else
                                leaves[nleaves++] = cur;
                        }
                    }

                    LLVMBasicBlockRef then_bb = LLVMAppendBasicBlockInContext(
                        ctx->context, ctx->current_fn, "match.then");
                    LLVMBasicBlockRef next_bb = LLVMAppendBasicBlockInContext(
                        ctx->context, ctx->current_fn, "match.next");

                    /* Build comparison for each leaf, OR results together. */
                    LLVMValueRef combined_cmp = NULL;
                    for (int j = 0; j < nleaves; j++)
                    {
                        LLVMValueRef pattern = codegen_expr(ctx, leaves[j]);
                        if (pattern == NULL) continue;

                        LLVMValueRef cmp;
                        if (is_fp)
                            cmp = LLVMBuildFCmp(ctx->builder, LLVMRealOEQ, subject, pattern, "match.cmp");
                        else
                            cmp = LLVMBuildICmp(ctx->builder, LLVMIntEQ, subject, pattern, "match.cmp");

                        combined_cmp = (combined_cmp == NULL)
                            ? cmp
                            : LLVMBuildOr(ctx->builder, combined_cmp, cmp, "match.or");
                    }

                    if (combined_cmp == NULL)
                    {
                        /* Degenerate: no leaf patterns — skip arm. */
                        LLVMDeleteBasicBlock(then_bb);
                        LLVMDeleteBasicBlock(next_bb);
                        continue;
                    }

                    LLVMBuildCondBr(ctx->builder, combined_cmp, then_bb, next_bb);

                    LLVMPositionBuilderAtEnd(ctx->builder, then_bb);
                    int arm_drop_floor = ctx->temp_drop_count;
                    LLVMValueRef body_val = codegen_expr(ctx, arm->body);
                    if (result_alloca && body_val)
                    {
                        body_val = cg_match_arm_own_tail(
                            ctx, cg_match_arm_tail(arm->body), body_val, res_llvm,
                            result_type, arm_drop_floor, false);
                        LLVMBuildStore(ctx->builder, body_val, result_alloca);
                    }
                    cg_match_arm_encapsulate(ctx, arm_drop_floor, result_type);
                    /* Guard: arm body may end with 'return'. */
                    if (LLVMGetBasicBlockTerminator(LLVMGetInsertBlock(ctx->builder)) == NULL)
                        LLVMBuildBr(ctx->builder, merge_bb);

                    LLVMPositionBuilderAtEnd(ctx->builder, next_bb);
                }
            }

            /* If last arm wasn't wildcard, fall through to merge. */
            if (node->as.match.arm_count > 0)
            {
                MatchArm *last = &node->as.match.arms[node->as.match.arm_count - 1];
                bool last_is_wildcard = last->pattern->kind == AST_IDENT &&
                                        strcmp(last->pattern->as.ident.name, "_") == 0;
                if (!last_is_wildcard)
                {
                    if (LLVMGetBasicBlockTerminator(LLVMGetInsertBlock(ctx->builder)) == NULL)
                        LLVMBuildBr(ctx->builder, merge_bb);
                }
            }
        }

        LLVMPositionBuilderAtEnd(ctx->builder, merge_bb);
        /* L-013: register the funneled owned result as the single result temp
           (mirrors the enum path); no-op for static/POD results. */
        if (result_alloca)
        if (result_alloca)
            return LLVMBuildLoad2(ctx->builder, res_llvm, result_alloca, "match.val");
        return NULL;
    }

/* Extracted from codegen_expr's switch (codegen.c split Step 4): the
   AST_TRY case body, verbatim. Behavior unchanged — ctx->current_node is
   already set by codegen_expr before dispatch. */
LLVMValueRef codegen_try_expr(CodegenContext *ctx, AstNode *node)
{
        /* try expr — Zig-style early return for Result/Option.
           Lowering: extract inner enum's discriminant; on the success variant
           (Ok / Some) yield the unwrapped T; on the failure variant (Err / None)
           build a fresh enum value of the enclosing function's return type and
           return it after running scope-cleanup.
           We move-by-bytes (memcpy) the Err payload so the heap stays single-
           owner; no clone is needed. */
        AstNode *inner_expr = node->as.try_expr.expr;
        Type *inner_type = inner_expr->resolved_type;
        Type *fn_ret_type = node->as.try_expr.fn_return_type;
        if (inner_type == NULL || fn_ret_type == NULL ||
            inner_type->kind != TYPE_ENUM || fn_ret_type->kind != TYPE_ENUM)
            return NULL;

        bool is_result = (strncmp(inner_type->as.enom.name, "Result(", 7) == 0);
        int success_idx = is_result ? 0 : 1;   /* Ok=0 / Some=1 */
        int failure_idx = is_result ? 1 : 0;   /* Err=1 / None=0 */

        /* Save temp mark: inner_expr eval may create temps (f-string, concat,
           upper, etc.) that aren't consumed by the try's payload extraction. */
        LLVMValueRef inner_val = codegen_expr(ctx, inner_expr);
        if (inner_val == NULL) return NULL;

        LLVMTypeRef inner_llvm = type_to_llvm(ctx, inner_type);
        LLVMTypeRef ret_llvm   = type_to_llvm(ctx, fn_ret_type);
        LLVMTypeRef i8 = LLVMInt8TypeInContext(ctx->context);
        LLVMTypeRef i32 = LLVMInt32TypeInContext(ctx->context);
        LLVMTypeRef i64 = LLVMInt64TypeInContext(ctx->context);

        /* Hoist allocas to the entry block */
        LLVMBasicBlockRef entry = LLVMGetEntryBasicBlock(ctx->current_fn);
        LLVMBuilderRef tmp_b = LLVMCreateBuilderInContext(ctx->context);
        LLVMValueRef first_inst = LLVMGetFirstInstruction(entry);
        if (first_inst) LLVMPositionBuilderBefore(tmp_b, first_inst);
        else            LLVMPositionBuilderAtEnd(tmp_b, entry);
        LLVMValueRef inner_alloca = LLVMBuildAlloca(tmp_b, inner_llvm, "try.inner");
        LLVMValueRef ret_alloca   = LLVMBuildAlloca(tmp_b, ret_llvm,   "try.ret");
        Type *success_t = node->resolved_type;
        LLVMValueRef result_alloca = NULL;
        LLVMTypeRef success_llvm = NULL;
        if (success_t != NULL && success_t->kind != TYPE_VOID) {
            success_llvm = type_to_llvm(ctx, success_t);
            result_alloca = LLVMBuildAlloca(tmp_b, success_llvm, "try.unwrapped");
        }
        LLVMDisposeBuilder(tmp_b);

        LLVMBuildStore(ctx->builder, inner_val, inner_alloca);

        LLVMValueRef disc_ptr = LLVMBuildStructGEP2(ctx->builder, inner_llvm,
                                                    inner_alloca, 0, "try.disc.p");
        LLVMValueRef disc = LLVMBuildLoad2(ctx->builder, i8, disc_ptr, "try.disc");
        LLVMValueRef cmp = LLVMBuildICmp(ctx->builder, LLVMIntEQ, disc,
                                         LLVMConstInt(i8, (unsigned long long)success_idx, 0),
                                         "try.is_ok");

        LLVMBasicBlockRef ok_bb    = LLVMAppendBasicBlockInContext(
            ctx->context, ctx->current_fn, "try.ok");
        LLVMBasicBlockRef err_bb   = LLVMAppendBasicBlockInContext(
            ctx->context, ctx->current_fn, "try.err");
        LLVMBasicBlockRef merge_bb = LLVMAppendBasicBlockInContext(
            ctx->context, ctx->current_fn, "try.merge");
        LLVMBuildCondBr(ctx->builder, cmp, ok_bb, err_bb);

        /* ---- Success path: extract T from Ok/Some payload ---- */
        LLVMPositionBuilderAtEnd(ctx->builder, ok_bb);
        if (result_alloca && success_llvm) {
            LLVMTypeRef variant_struct = build_variant_payload_struct(
                ctx, inner_type, success_idx);
            LLVMValueRef in_payload = LLVMBuildStructGEP2(
                ctx->builder, inner_llvm, inner_alloca, 1, "try.in.payload");
            LLVMValueRef field_ptr = LLVMBuildStructGEP2(
                ctx->builder, variant_struct, in_payload, 0, "try.ok.field");
            LLVMValueRef ok_val = LLVMBuildLoad2(
                ctx->builder, success_llvm, field_ptr, "try.ok.val");
            LLVMBuildStore(ctx->builder, ok_val, result_alloca);
        }
        LLVMBuildBr(ctx->builder, merge_bb);

        /* ---- Failure path: build return enum, run cleanup, ret ---- */
        LLVMPositionBuilderAtEnd(ctx->builder, err_bb);
        LLVMTargetDataRef td = LLVMGetModuleDataLayout(ctx->module);
        unsigned long long ret_sz = LLVMABISizeOfType(td, ret_llvm);

        /* Zero ret_alloca */
        LLVMValueRef memset_fn = LLVMGetNamedFunction(ctx->module, "memset");
        if (memset_fn) {
            LLVMValueRef ms_args[3] = {
                ret_alloca,
                LLVMConstInt(i32, 0, 0),
                LLVMConstInt(i64, ret_sz, 0)
            };
            LLVMTypeRef ms_type = LLVMGlobalGetValueType(memset_fn);
            LLVMBuildCall2(ctx->builder, ms_type, memset_fn, ms_args, 3, "");
        }

        /* Set failure discriminant */
        LLVMValueRef ret_disc_ptr = LLVMBuildStructGEP2(
            ctx->builder, ret_llvm, ret_alloca, 0, "try.ret.disc.p");
        LLVMBuildStore(ctx->builder,
                       LLVMConstInt(i8, (unsigned long long)failure_idx, 0),
                       ret_disc_ptr);

        /* For Result, copy Err payload bytes from inner to return.
           Err variant struct has the same single field of type E in both
           inner_type and fn_ret_type, so byte-copy is safe and transfers
           ownership without aliasing. Option(T)::None has no payload. */
        if (is_result) {
            LLVMTypeRef err_struct = build_variant_payload_struct(
                ctx, inner_type, failure_idx);
            unsigned long long err_sz = LLVMABISizeOfType(td, err_struct);
            LLVMValueRef in_payload = LLVMBuildStructGEP2(
                ctx->builder, inner_llvm, inner_alloca, 1, "try.err.in.payload");
            LLVMValueRef out_payload = LLVMBuildStructGEP2(
                ctx->builder, ret_llvm, ret_alloca, 1, "try.err.out.payload");
            LLVMValueRef memcpy_fn = LLVMGetNamedFunction(ctx->module, "memcpy");
            if (memcpy_fn) {
                LLVMValueRef mc_args[3] = {
                    out_payload,
                    in_payload,
                    LLVMConstInt(i64, err_sz, 0)
                };
                LLVMTypeRef mc_type = LLVMGlobalGetValueType(memcpy_fn);
                LLVMBuildCall2(ctx->builder, mc_type, memcpy_fn, mc_args, 3, "");
            }
        }

        /* Flush temp strings from the inner expression before scope cleanup.
           This is a scope-exit (try propagates Err via `return`): flush every
           temp, including a match subject the base protects (see
           cg_flush_temps_scope_exit). */
        cg_flush_temps_scope_exit(ctx);
        /* RAII: drop all owned variables in scope before returning */
        emit_cleanup_to(ctx, NULL, NULL);

        LLVMValueRef ret_val = LLVMBuildLoad2(ctx->builder, ret_llvm, ret_alloca,
                                              "try.ret.val");
        LLVMBuildRet(ctx->builder, ret_val);

        /* ---- Merge: yield unwrapped value ---- */
        LLVMPositionBuilderAtEnd(ctx->builder, merge_bb);
        /* Flush temp strings from inner expression before yielding the
           unwrapped value. These temps (e.g. f-string buffers, concat results)
           have been cloned into the Result payload and are safe to free. */
        cg_flush_temps(ctx);
        if (result_alloca && success_llvm)
            return LLVMBuildLoad2(ctx->builder, success_llvm, result_alloca, "try.val");
        return NULL;
    }

/* Extracted from codegen_expr's switch (codegen.c split Step 4): the
   AST_FORCE_UNWRAP case body, verbatim. Behavior unchanged — ctx->current_node is
   already set by codegen_expr before dispatch. */
LLVMValueRef codegen_force_unwrap_expr(CodegenContext *ctx, AstNode *node)
{
        /* expr! — force-unwrap Option/Result, abort on None/Err.
           Lowered to a match-like branch:
             - Ok/Some => extract payload, yield T
             - Err/None => call abort() (does not return) */
        AstNode *inner_expr = node->as.force_unwrap.expr;
        Type *inner_type = inner_expr->resolved_type;
        if (inner_type == NULL || inner_type->kind != TYPE_ENUM)
            return NULL;

        bool is_result = (strncmp(inner_type->as.enom.name, "Result(", 7) == 0);
        /* Discriminant order is fixed by the builtin Option/Result templates
           (None=0/Some=1, Ok=0/Err=1) — same convention the AST_TRY handler
           above relies on. Reordering those templates would break both sites
           and is caught immediately by the test suite. */
        int success_idx = is_result ? 0 : 1;   /* Ok=0 / Some=1 */

        LLVMValueRef inner_val = codegen_expr(ctx, inner_expr);
        if (inner_val == NULL) return NULL;

        LLVMTypeRef inner_llvm = type_to_llvm(ctx, inner_type);
        LLVMTypeRef i8 = LLVMInt8TypeInContext(ctx->context);

        /* Hoist alloca to entry block */
        LLVMBasicBlockRef entry = LLVMGetEntryBasicBlock(ctx->current_fn);
        LLVMBuilderRef tmp_b = LLVMCreateBuilderInContext(ctx->context);
        LLVMValueRef first_inst = LLVMGetFirstInstruction(entry);
        if (first_inst) LLVMPositionBuilderBefore(tmp_b, first_inst);
        else            LLVMPositionBuilderAtEnd(tmp_b, entry);
        LLVMValueRef inner_alloca = LLVMBuildAlloca(tmp_b, inner_llvm, "fuw.inner");
        Type *success_t = node->resolved_type;
        LLVMValueRef result_alloca = NULL;
        LLVMTypeRef success_llvm = NULL;
        if (success_t != NULL && success_t->kind != TYPE_VOID) {
            success_llvm = type_to_llvm(ctx, success_t);
            result_alloca = LLVMBuildAlloca(tmp_b, success_llvm, "fuw.val");
        }
        LLVMDisposeBuilder(tmp_b);

        LLVMBuildStore(ctx->builder, inner_val, inner_alloca);

        /* Load discriminant and branch */
        LLVMValueRef disc_ptr = LLVMBuildStructGEP2(ctx->builder, inner_llvm,
                                                    inner_alloca, 0, "fuw.disc.p");
        LLVMValueRef disc = LLVMBuildLoad2(ctx->builder, i8, disc_ptr, "fuw.disc");
        LLVMValueRef cmp = LLVMBuildICmp(ctx->builder, LLVMIntEQ, disc,
                                         LLVMConstInt(i8, (unsigned long long)success_idx, 0),
                                         "fuw.is_ok");

        LLVMBasicBlockRef ok_bb  = LLVMAppendBasicBlockInContext(
            ctx->context, ctx->current_fn, "fuw.ok");
        LLVMBasicBlockRef err_bb = LLVMAppendBasicBlockInContext(
            ctx->context, ctx->current_fn, "fuw.err");
        LLVMBasicBlockRef merge_bb = LLVMAppendBasicBlockInContext(
            ctx->context, ctx->current_fn, "fuw.merge");
        LLVMBuildCondBr(ctx->builder, cmp, ok_bb, err_bb);

        /* ---- Success path: extract payload ---- */
        LLVMPositionBuilderAtEnd(ctx->builder, ok_bb);
        if (result_alloca && success_llvm) {
            LLVMTypeRef variant_struct = build_variant_payload_struct(
                ctx, inner_type, success_idx);
            LLVMValueRef in_payload = LLVMBuildStructGEP2(
                ctx->builder, inner_llvm, inner_alloca, 1, "fuw.in.payload");
            LLVMValueRef field_ptr = LLVMBuildStructGEP2(
                ctx->builder, variant_struct, in_payload, 0, "fuw.ok.field");
            LLVMValueRef ok_val = LLVMBuildLoad2(
                ctx->builder, success_llvm, field_ptr, "fuw.ok.val");
            LLVMBuildStore(ctx->builder, ok_val, result_alloca);
            /* Move-elision (Q4): the success payload's ownership is transferred
               to `result_alloca`. When the operand is a named owned variable the
               checker tagged moved_out — invalidate the SOURCE enum so its scope
               cleanup skips dropping the now-moved payload (else double-free).
               Covers string / Vec / Map / struct / has_drop-enum uniformly via
               the source enum's moved_flag; POD payloads are no-ops. rvalue
               operands (calls) have no named source and are left untouched. */
            if (inner_expr->moved_out)
                cg_invalidate_moved_source(ctx, inner_expr, inner_type);
        }
        LLVMBuildBr(ctx->builder, merge_bb);

        /* ---- Failure path: print diagnostic + abort ---- */
        LLVMPositionBuilderAtEnd(ctx->builder, err_bb);
        {
            LLVMValueRef printf_fn = LLVMGetNamedFunction(ctx->module, "printf");
            /* C1: `.expect(msg)` lowers to a force-unwrap carrying a message expr.
               On the failure path print the user's message; bare `!` / `.unwrap()`
               (message == NULL) print the default diagnostic. The message string is
               evaluated only here (panic path) — any owned temp leaks are moot since
               the process is exiting. */
            AstNode *msg_node = node->as.force_unwrap.message;
            if (printf_fn && msg_node) {
                LLVMTypeRef printf_ty = LLVMGlobalGetValueType(printf_fn);
                LLVMValueRef msg_val = codegen_expr(ctx, msg_node);
                /* message is a Str struct value; field 0 is the (static,
                   NUL-terminated .rodata) data pointer. */
                LLVMValueRef msg_data = msg_val
                    ? LLVMBuildExtractValue(ctx->builder, msg_val, 0, "fuw.emsgd")
                    : NULL;
                LLVMValueRef fmt = LLVMBuildGlobalStringPtr(
                    ctx->builder, "[expect] %d:%d: %s\n", "fuw.efmt");
                LLVMValueRef line_val = LLVMConstInt(LLVMInt32TypeInContext(ctx->context),
                                                     (unsigned long long)node->line, 0);
                LLVMValueRef col_val  = LLVMConstInt(LLVMInt32TypeInContext(ctx->context),
                                                     (unsigned long long)node->column, 0);
                if (msg_data == NULL)
                    msg_data = LLVMBuildGlobalStringPtr(ctx->builder, "(expect)", "fuw.emsg0");
                LLVMValueRef pargs4[4] = { fmt, line_val, col_val, msg_data };
                LLVMBuildCall2(ctx->builder, printf_ty, printf_fn, pargs4, 4, "");
            }
            else if (printf_fn) {
                LLVMTypeRef printf_ty = LLVMGlobalGetValueType(printf_fn);
                const char *fail_variant = is_result ? "Err" : "None";
                const char *ok_variant   = is_result ? "Ok" : "Some";
                const char *type_str = inner_type->as.enom.name;
                /* Use a short fixed format to avoid dynamic string building */
                char fmt_buf[256];
                snprintf(fmt_buf, sizeof(fmt_buf),
                    "[unwrap] %%d:%%d: unwrap failed: expected %s, got %s (type: %s)\n",
                    ok_variant, fail_variant, type_str);
                LLVMValueRef fmt = LLVMBuildGlobalStringPtr(ctx->builder, fmt_buf, "fuw.fmt");
                LLVMValueRef line_val = LLVMConstInt(LLVMInt32TypeInContext(ctx->context),
                                                     (unsigned long long)node->line, 0);
                LLVMValueRef col_val  = LLVMConstInt(LLVMInt32TypeInContext(ctx->context),
                                                     (unsigned long long)node->column, 0);
                LLVMValueRef pargs[3] = { fmt, line_val, col_val };
                LLVMBuildCall2(ctx->builder, printf_ty, printf_fn, pargs, 3, "");
            }
            LLVMValueRef exit_fn = LLVMGetNamedFunction(ctx->module, "__ls_proc_exit");
            LLVMTypeRef exit_ty = LLVMFunctionType(
                LLVMVoidTypeInContext(ctx->context),
                (LLVMTypeRef[]){ LLVMInt32TypeInContext(ctx->context) }, 1, 0);
            if (exit_fn == NULL)
                exit_fn = LLVMAddFunction(ctx->module, "__ls_proc_exit", exit_ty);
            LLVMValueRef code = LLVMConstInt(LLVMInt32TypeInContext(ctx->context), 1, 0);
            LLVMBuildCall2(ctx->builder, exit_ty, exit_fn, &code, 1, "");
            LLVMBuildUnreachable(ctx->builder);
        }

        /* ---- Merge: yield unwrapped value ---- */
        LLVMPositionBuilderAtEnd(ctx->builder, merge_bb);
        if (result_alloca && success_llvm)
            return LLVMBuildLoad2(ctx->builder, success_llvm, result_alloca, "fuw.val");
        return NULL;
    }
