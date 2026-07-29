/* codegen_expr.c
   表达式发射：codegen_expr switch 主体 + lvalue/addr + short-circuit + entry-alloca + slice + 数值 widen + Str helper + print/f-string + errno/from_cstr

   Bodies mechanically relocated from codegen.c (docs/plan_codegen_split.md).
   No logic changes. All prototypes live in codegen_internal.h. */
#include "codegen.h"
#include "codegen_internal.h"
#include "mangle.h"
#include "block_protocol.h"
#include "module.h"
#define LS_INCLUDE_CODEGEN 1
#include "builtins_math.h"
#define LS_INCLUDE_CODEGEN 1
#include "builtins_perf.h"
#include "builtins_intrinsic_cg.h"
#include "common.h"

#include <llvm-c/Core.h>
#include <llvm-c/Target.h>
#include <llvm-c/TargetMachine.h>
#include <llvm-c/Analysis.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

/* A (docs/plan_fma_coldpath.md): allow FMA contraction on an FP arithmetic
   instruction. `contract` lets LLVM fuse `a*b + c` into a single `fma` — fewer
   instructions AND more accurate (one rounding instead of two). It is the one
   fast-math flag that doesn't change results for the worse, so it is on by
   default; LS_NO_FMA=1 is the escape hatch. Returns `inst` so it can wrap an
   LLVMBuildF* call inline. Integer ops never reach here. */
LLVMValueRef cg_fp_contract(LLVMValueRef inst)
{
    static int enabled = -1;
    if (enabled < 0)
        enabled = (getenv("LS_NO_FMA") == NULL) ? 1 : 0;
    if (enabled && inst && LLVMCanValueUseFastMathFlags(inst))
        LLVMSetFastMathFlags(inst, LLVMFastMathAllowContract);
    return inst;
}

/* A3 (docs/plan_opt_enum_range.md): an enum tag load is always in
   [0, variant_count) — the checker's exhaustiveness guarantee (Phase 8) plus
   tag-only construction make any other value memory-unsafety. Attach !range
   so LLVM can drop the switch bounds compare and tighten the lowering.
   Tags are i8, so a count that doesn't fit (or covers the full i8 domain)
   carries no information — skip it. LS_NO_ENUM_RANGE=1 disables all
   emission (FFI / @take escape hatch). */
void cg_attach_tag_range(CodegenContext *ctx, LLVMValueRef load, int variant_count)
{
    static int disabled = -1;
    if (disabled < 0)
        disabled = (getenv("LS_NO_ENUM_RANGE") != NULL) ? 1 : 0;
    if (disabled || load == NULL || variant_count <= 0 || variant_count > 255)
        return;
    LLVMTypeRef i8 = LLVMInt8TypeInContext(ctx->context);
    LLVMMetadataRef pair[2] = {
        LLVMValueAsMetadata(LLVMConstInt(i8, 0, 0)),
        LLVMValueAsMetadata(LLVMConstInt(i8, (unsigned long long)variant_count, 0)),
    };
    LLVMMetadataRef md = LLVMMDNodeInContext2(ctx->context, pair, 2);
    LLVMSetMetadata(load, LLVMGetMDKindIDInContext(ctx->context, "range", 5),
                    LLVMMetadataAsValue(ctx->context, md));
}

/* B (docs/plan_fma_coldpath.md): mark a never-returning runtime sink (the
   __ls_proc_exit family that backs abort / bounds-check / unwrap failures) as
   `noreturn cold`. LLVM then propagates coldness to every block that leads to
   it — so the ubiquitous `if oob { abort }` / `None => abort` branches get laid
   out off the hot path and predicted untaken, with no per-site llvm.expect. */
void cg_mark_noreturn_cold(CodegenContext *ctx, LLVMValueRef fn)
{
    if (fn == NULL) return;
    unsigned nr = LLVMGetEnumAttributeKindForName("noreturn", 8);
    unsigned cd = LLVMGetEnumAttributeKindForName("cold", 4);
    if (nr)
        LLVMAddAttributeAtIndex(fn, (LLVMAttributeIndex)LLVMAttributeFunctionIndex,
                                LLVMCreateEnumAttribute(ctx->context, nr, 0));
    if (cd)
        LLVMAddAttributeAtIndex(fn, (LLVMAttributeIndex)LLVMAttributeFunctionIndex,
                                LLVMCreateEnumAttribute(ctx->context, cd, 0));
}

/* File-local helpers (single-TU; re-static'd at codegen split §7). */
static LLVMValueRef cg_make_slice(CodegenContext *ctx, LLVMTypeRef elem_llvm, LLVMValueRef base_ptr, LLVMValueRef start_i64, LLVMValueRef len_i64, Type *slice_type);
LLVMValueRef cg_str_struct_from_literal(CodegenContext *ctx, const char *text, Type *str_type);
bool cg_type_is_str(Type *t);

/* Accept both the @-sigil canonical spelling and the legacy __ spelling during
   the migration window (Phase 2 retires the legacy form). */
static LLVMValueRef codegen_short_circuit(CodegenContext *ctx, AstNode *node);

/* ---- Numeric widening (Zig-style implicit conversions) ----
   Inserts the appropriate LLVM extension/conversion when an LS expression
   of type `from` is used in a context expecting type `to`. Returns `val`
   unchanged if the types match or are non-numeric (e.g. struct fields).
   The checker already validated that the conversion is permitted via
   type_widens_to(); this helper just emits the right LLVM op:
     iN → iM   : sext (signed) / zext (unsigned)
     iN/uN → fM: sitofp / uitofp
     f32 → f64 : fpext
*/
LLVMValueRef cg_widen(CodegenContext *ctx, LLVMValueRef val,
                             Type *from, Type *to)
{
    if (val == NULL || from == NULL || to == NULL) return val;
    if (type_equals(from, to)) return val;
    if (!type_is_numeric(from) || !type_is_numeric(to)) return val;
    if (!type_widens_to(from, to)) return val;  /* defensive */

    LLVMTypeRef dst_llvm = type_to_llvm(ctx, to);

    if (type_is_integer(from) && type_is_integer(to))
    {
        if (type_is_signed(from))
            return LLVMBuildSExt(ctx->builder, val, dst_llvm, "widen.sext");
        return LLVMBuildZExt(ctx->builder, val, dst_llvm, "widen.zext");
    }
    if (type_is_integer(from) && type_is_float(to))
    {
        if (type_is_signed(from))
            return LLVMBuildSIToFP(ctx->builder, val, dst_llvm, "widen.sitofp");
        return LLVMBuildUIToFP(ctx->builder, val, dst_llvm, "widen.uitofp");
    }
    if (type_is_float(from) && type_is_float(to))
    {
        return LLVMBuildFPExt(ctx->builder, val, dst_llvm, "widen.fpext");
    }
    return val;  /* shouldn't reach */
}

/* SIMD intrinsic helpers moved to builtins_intrinsic_cg.c (S2 P1 3/4). */

/* Allocate a stack slot in the CURRENT function's ENTRY block, regardless of
   where the builder currently sits. Bug #24/#26 family: a plain
   LLVMBuildAlloca at the current position, if that position is inside a loop
   body, allocates a fresh slot every iteration (LLVM allocas are only released
   on function return) → stack overflow. Entry-block allocas live once per call
   and are reused. Use this for any scratch slot created during expression /
   statement codegen (string method temps, loop indices, etc.). */
/* __ls_bytecopy lowering moved to builtins_intrinsic_cg.c (S2 P1 4/4). */

LLVMValueRef cg_entry_alloca(CodegenContext *ctx, LLVMTypeRef ty, const char *name)
{
    LLVMBasicBlockRef cur = LLVMGetInsertBlock(ctx->builder);
    LLVMValueRef fn = LLVMGetBasicBlockParent(cur);
    LLVMBasicBlockRef entry_bb = LLVMGetEntryBasicBlock(fn);
    LLVMBuilderRef eb = LLVMCreateBuilderInContext(ctx->context);
    LLVMValueRef first = LLVMGetFirstInstruction(entry_bb);
    if (first) LLVMPositionBuilderBefore(eb, first);
    else       LLVMPositionBuilderAtEnd(eb, entry_bb);
    LLVMValueRef slot = LLVMBuildAlloca(eb, ty, name);
    LLVMDisposeBuilder(eb);
    return slot;
}

/* Like cg_entry_alloca, but also zero-initialises the slot in the entry block.
   Use for has_drop temporaries whose initialising store happens in a CONDITIONAL
   block but whose drop may be reached on a fall-through path that skipped the
   store (e.g. a chained-operator receiver spill inside a match-arm `if` body).
   The entry-block zeroinit makes such a stray drop a safe no-op (cap=0/data=NULL),
   the same defense the match result_alloca uses. */
LLVMValueRef cg_entry_alloca_zeroed(CodegenContext *ctx, LLVMTypeRef ty,
                                    const char *name)
{
    LLVMValueRef slot = cg_entry_alloca(ctx, ty, name);
    /* Insert the zero store in the entry block, right after the alloca, so it
       dominates every use and never lands in a conditional block. */
    LLVMBuilderRef eb = LLVMCreateBuilderInContext(ctx->context);
    LLVMValueRef nexti = LLVMGetNextInstruction(slot);
    if (nexti) LLVMPositionBuilderBefore(eb, nexti);
    else {
        LLVMBasicBlockRef abb = LLVMGetInstructionParent(slot);
        LLVMPositionBuilderAtEnd(eb, abb);
    }
    LLVMBuildStore(eb, LLVMConstNull(ty), slot);
    LLVMDisposeBuilder(eb);
    return slot;
}

/* Emit a user-container list literal: `StructWithFromList v = [a, b]`.
   The checker guarantees `lit->resolved_type == struct_type` and that the
   struct has `__from_list(&!self, E)`. */
LLVMValueRef emit_user_from_list_value(CodegenContext *ctx, Type *struct_type,
                                              AstNode *lit)
{
    if (ctx == NULL || struct_type == NULL || lit == NULL ||
        struct_type->kind != TYPE_STRUCT || lit->kind != AST_ARRAY_LIT)
        return NULL;

    LLVMTypeRef st_llvm = type_to_llvm(ctx, struct_type);
    LLVMValueRef tmp = cg_entry_alloca(ctx, st_llvm, "ufl.tmp");
    LLVMBuildStore(ctx->builder, LLVMConstNull(st_llvm), tmp);

    char *fl_name = mangle_method_symbol(struct_llvm_name(struct_type),
                                         NULL, "__from_list"); /* Task 7.2 */
    LLVMValueRef fl_fn = LLVMGetNamedFunction(ctx->module, fl_name);
    if (fl_fn == NULL)
    {
        /* VR-LIM-016: global `Vec(T) v = [..]` init is emitted (in
           __ls_global_stmts) BEFORE the G1.5 pending-generic-method pass, so the
           monomorphized `Vec(T).__from_list` body doesn't exist yet. Forward-
           declare it from the checker's pending queue; the body lands later in
           G1.5. Mirrors the local var-decl path and other generic call sites. */
        fl_fn = cg_declare_pending_generic_method(ctx, fl_name);
    }
    free(fl_name);
    if (fl_fn == NULL)
    {
        cg_error(ctx, lit->line, lit->column,
                 "missing __from_list method for '%s'",
                 struct_type->as.strukt.name);
        return NULL;
    }

    LLVMTypeRef fl_ft = LLVMGlobalGetValueType(fl_fn);
    for (int i = 0; i < lit->as.array_lit.count; i++)
    {
        AstNode *elem = lit->as.array_lit.elements[i];
        LLVMValueRef ev = codegen_expr(ctx, elem);
        if (ev == NULL)
            continue;


        LLVMValueRef fl_args[2] = { tmp, ev };
        LLVMBuildCall2(ctx->builder, fl_ft, fl_fn, fl_args, 2, "");
    }

    return LLVMBuildLoad2(ctx->builder, st_llvm, tmp, "ufl.val");
}

/* True iff `t` is the pure-LS `Str` struct (recognized by name, like Vec/Map). */
bool cg_type_is_str(Type *t)
{
    return t != NULL && t->kind == TYPE_STRUCT && t->as.strukt.name != NULL &&
           strcmp(t->as.strukt.name, "Str") == 0;
}

/* ---- Str layout chokepoints (docs/spike_sso.md §4) ---------------------------
   THE single place codegen knows Str's 3-field {*u8 data, i32 len, i32 cap}
   layout. Every codegen site that BUILDS or READS a Str routes through these, so
   a future SSO (small-string optimization) layout change touches just these
   helpers + type_to_llvm(Str), not the (formerly scattered) construction/read
   sites. Pure refactor: behavior identical to the inlined InsertValue/Extract
   it replaces. `st` is the Str LLVM struct type (type_to_llvm on a resolved Str
   Type); `data` is the *u8 buffer; `len`/`cap` are i32. */
LLVMValueRef cg_make_str(CodegenContext *ctx, LLVMTypeRef st,
                                LLVMValueRef data, LLVMValueRef len,
                                LLVMValueRef cap)
{
    LLVMValueRef v = LLVMGetUndef(st);
    v = LLVMBuildInsertValue(ctx->builder, v, data, 0, "Str.d");
    v = LLVMBuildInsertValue(ctx->builder, v, len, 1, "Str.l");
    v = LLVMBuildInsertValue(ctx->builder, v, cap, 2, "Str.c");
    return v;
}

/* Read a Str value's data pointer (field 0) / byte length (field 1). */
LLVMValueRef cg_str_data(CodegenContext *ctx, LLVMValueRef str_val)
{
    return LLVMBuildExtractValue(ctx->builder, str_val, 0, "Str.d");
}
LLVMValueRef cg_str_len(CodegenContext *ctx, LLVMValueRef str_val)
{
    return LLVMBuildExtractValue(ctx->builder, str_val, 1, "Str.l");
}

/* Build a static `Str` struct value {data, len, cap:0} for a string literal
   (docs/plan_string_to_stdlib.md §5.1, P1). `Str { *u8, int, int }` is layout-
   identical to LsString {i8*, i32, i32}; the bytes live in .rodata, so cap 0
   means Str.__drop skips free and Str.__clone shallow-copies. `str_type` is the
   concrete Str struct type the checker resolved (used for the LLVM struct type). */
LLVMValueRef cg_str_struct_from_literal(CodegenContext *ctx,
                                               const char *text, Type *str_type)
{
    LLVMTypeRef st = type_to_llvm(ctx, str_type);
    /* Name hint is dotted (".ls.*") so this compiler-internal private constant
       can NEVER collide with a user global variable's name. LS identifiers cannot
       contain '.', so the internal-global namespace is disjoint from user names.
       Before this, a bare hint like "Strlit"/"fmt" would squat the user's chosen
       global name; LLVM then auto-renamed the *user* global (e.g. fmt -> fmt.126)
       while the name-based global lookup (emit_global_var_init / cleanup) still
       resolved the bare name to THIS constant — storing a Str into / __drop-ing a
       .rodata format string => heap corruption + "invalid free". */
    LLVMValueRef data = LLVMBuildGlobalStringPtr(ctx->builder, text, ".ls.strlit");
    LLVMTypeRef i32 = LLVMInt32TypeInContext(ctx->context);
    LLVMValueRef len = LLVMConstInt(i32, (unsigned long long)strlen(text), 0);
    LLVMValueRef cap = LLVMConstInt(i32, 0, 0);
    return cg_make_str(ctx, st, data, len, cap);
}

/* Unified element/value deep-clone dispatcher. Returns an independently-owned
   copy for heap-owning types (string / vec / has_drop struct / has_drop enum);
   returns the value unchanged for POD (and map/array, which keep their current
   shallow behavior). Centralizes the clone logic that was inlined at many vec
   element-read sites, where a vec element previously fell through to a shallow
   copy → double-free on nested vec(vec(...)). */
/* Slice bounds guard: if `ok_cond` (i1) is false, print `msg at L:C` and
   exit(1). Leaves the builder positioned in the ok-continuation block. Mirrors
   the force-unwrap abort path (printf + __ls_proc_exit + unreachable). */
void cg_emit_bounds_guard(CodegenContext *ctx, LLVMValueRef ok_cond,
                                 const char *msg, int line, int col)
{
    LLVMBasicBlockRef bad = LLVMAppendBasicBlockInContext(ctx->context,
                                                          ctx->current_fn, "sl.oob");
    LLVMBasicBlockRef ok = LLVMAppendBasicBlockInContext(ctx->context,
                                                         ctx->current_fn, "sl.ok");
    LLVMBuildCondBr(ctx->builder, ok_cond, ok, bad);
    LLVMPositionBuilderAtEnd(ctx->builder, bad);
    LLVMValueRef printf_fn = LLVMGetNamedFunction(ctx->module, "printf");
    if (printf_fn == NULL)
    {
        LLVMTypeRef pty = LLVMFunctionType(LLVMInt32TypeInContext(ctx->context),
            (LLVMTypeRef[]){ LLVMPointerTypeInContext(ctx->context, 0) }, 1, 1);
        printf_fn = LLVMAddFunction(ctx->module, "printf", pty);
    }
    LLVMTypeRef printf_ty = LLVMGlobalGetValueType(printf_fn);
    char fmt[160];
    snprintf(fmt, sizeof fmt, "%s at %%d:%%d\n", msg);
    LLVMValueRef f = LLVMBuildGlobalStringPtr(ctx->builder, fmt, "sl.fmt");
    LLVMTypeRef i32 = LLVMInt32TypeInContext(ctx->context);
    LLVMValueRef pa[3] = { f, LLVMConstInt(i32, (unsigned)line, 0),
                              LLVMConstInt(i32, (unsigned)col, 0) };
    LLVMBuildCall2(ctx->builder, printf_ty, printf_fn, pa, 3, "");
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

/* Phase: build a borrowed slice value {ptr, len} from a base *T pointer, a
   start index, and a length (both i64). */
static LLVMValueRef cg_make_slice(CodegenContext *ctx, LLVMTypeRef elem_llvm,
                                  LLVMValueRef base_ptr, LLVMValueRef start_i64,
                                  LLVMValueRef len_i64, Type *slice_type)
{
    LLVMValueRef sptr = LLVMBuildGEP2(ctx->builder, elem_llvm, base_ptr,
                                      &start_i64, 1, "slice.base");
    LLVMTypeRef slice_llvm = type_to_llvm(ctx, slice_type);
    LLVMValueRef sv = LLVMGetUndef(slice_llvm);
    sv = LLVMBuildInsertValue(ctx->builder, sv, sptr, 0, "slice.ptr");
    sv = LLVMBuildInsertValue(ctx->builder, sv, len_i64, 1, "slice.len");
    return sv;
}

/* Returns an LLVM pointer (alloca or GEP) for the given lvalue node without
   loading it. Handles nested field access (p1.s.k), array index, and pointer
   dereference. Returns NULL if the node is not a valid lvalue. */
LLVMValueRef codegen_lvalue_ptr(CodegenContext *ctx, AstNode *node)
{
    if (node->kind == AST_IDENT)
    {
        CgSymbol *sym = cg_scope_resolve(ctx->current_scope, node->as.ident.name);
        if (sym) return sym->value;
        /* Module-level global: a lifted closure body's scope does not chain to
           the global scope, so a global named inside a closure (e.g. a shared
           Atomic/Mutex) resolves to its global variable address here. */
        return LLVMGetNamedGlobal(ctx->module, node->as.ident.name);
    }

    if (node->kind == AST_FIELD)
    {
        AstNode *obj_node = node->as.field_access.object;
        Type *obj_type = obj_node->resolved_type;
        const char *fname = node->as.field_access.field;

        /* Module-qualified global (`pmod.COUNT = 1`). The object is a MODULE,
           not a struct, so the field walk below cannot GEP it and used to fall
           out at the "not a struct" bail with NULL -- and the assign path guards
           on `if (ptr != NULL)` with no else, so every cross-module store was
           dropped on the floor. Reads worked, writes vanished, exit code 0, no
           diagnostic. P1-1 gives module globals a `<mod>__name` symbol; try that
           before the bare name, the same way cg_array_place_ptr already does for
           `math.PRIMES[0]`. This also fixes the nested place `pmod.MG.n = 7`,
           whose base is resolved through here. */
        if (obj_type != NULL && obj_type->kind == TYPE_MODULE)
        {
            const char *mod = obj_type->as.module.name;
            if (mod == NULL)
                return LLVMGetNamedGlobal(ctx->module, fname);
            char gv_sym[512];
            cg_module_fn_symbol(gv_sym, sizeof(gv_sym), mod, fname);
            LLVMValueRef gv = LLVMGetNamedGlobal(ctx->module, gv_sym);
            if (gv == NULL)
                gv = LLVMGetNamedGlobal(ctx->module, fname);
            return gv;
        }

        /* Auto-dereference pointer-to-struct and reference-to-struct (&Doc / &!Doc).
           Both are lowered to a pointer ABI; the alloca holds the pointer value. */
        bool is_ptr = false;
        Type *stype = obj_type;
        if (stype && (stype->kind == TYPE_POINTER || stype->kind == TYPE_REFERENCE) &&
            stype->as.pointer_to && stype->as.pointer_to->kind == TYPE_STRUCT)
        {
            stype = stype->as.pointer_to;
            is_ptr = true;
        }
        if (stype == NULL || stype->kind != TYPE_STRUCT)
            return NULL;

        int field_idx = -1;
        for (int i = 0; i < stype->as.strukt.field_count; i++)
        {
            if (strcmp(stype->as.strukt.fields[i].name, fname) == 0)
            {
                field_idx = i;
                break;
            }
        }
        if (field_idx < 0)
            return NULL;

        LLVMValueRef struct_ptr = NULL;
        if (is_ptr)
        {
            /* obj is *Struct: get the alloca, then load the pointer */
            LLVMValueRef ptr_alloca = codegen_lvalue_ptr(ctx, obj_node);
            if (ptr_alloca == NULL)
                return NULL;
            LLVMTypeRef ptr_llvm = LLVMPointerTypeInContext(ctx->context, 0);
            struct_ptr = LLVMBuildLoad2(ctx->builder, ptr_llvm, ptr_alloca, "ptr.deref");
        }
        else
        {
            /* obj is Struct: recursively get the alloca/GEP for the struct */
            struct_ptr = codegen_lvalue_ptr(ctx, obj_node);
        }
        if (struct_ptr == NULL)
            return NULL;

        LLVMTypeRef struct_llvm = find_struct_llvm(ctx, stype->as.strukt.name);
        if (struct_llvm == NULL)
            struct_llvm = type_to_llvm(ctx, stype);

        return LLVMBuildStructGEP2(ctx->builder, struct_llvm,
                                   struct_ptr, (unsigned)field_idx, "field.ptr");
    }

    if (node->kind == AST_INDEX)
    {
        AstNode *obj = node->as.index_expr.object;
        Type *obj_type = obj->resolved_type;
        LLVMTypeRef i64_type = LLVMInt64TypeInContext(ctx->context);

        if (obj_type && obj_type->kind == TYPE_ARRAY)
        {
            LLVMValueRef arr_ptr = codegen_lvalue_ptr(ctx, obj);
            if (arr_ptr == NULL)
                return NULL;
            LLVMValueRef index = codegen_expr(ctx, node->as.index_expr.index);
            if (index == NULL)
                return NULL;
            if (LLVMTypeOf(index) != i64_type)
                index = LLVMBuildSExtOrBitCast(ctx->builder, index, i64_type, "idx.ext");
            LLVMTypeRef arr_llvm = type_to_llvm(ctx, obj_type);
            LLVMValueRef zero = LLVMConstInt(i64_type, 0, 0);
            LLVMValueRef indices[2] = {zero, index};
            return LLVMBuildGEP2(ctx->builder, arr_llvm, arr_ptr, indices, 2, "arr.elem.ptr");
        }

        /* p[i] place on a raw *T pointer: load the pointer value, typed-GEP the
           element address. Valid until the buffer is realloc'd (caller's
           responsibility — same escape constraint as vec). */
        if (obj_type && obj_type->kind == TYPE_POINTER && obj_type->as.pointer_to)
        {
            LLVMValueRef ptr_val = codegen_expr(ctx, obj);
            if (ptr_val == NULL)
                return NULL;
            LLVMValueRef index = codegen_expr(ctx, node->as.index_expr.index);
            if (index == NULL)
                return NULL;
            if (LLVMTypeOf(index) != i64_type)
                index = LLVMBuildSExtOrBitCast(ctx->builder, index, i64_type, "lp.idx");
            LLVMTypeRef elem_llvm = type_to_llvm(ctx, obj_type->as.pointer_to);
            return LLVMBuildGEP2(ctx->builder, elem_llvm, ptr_val, &index, 1, "ptr.elem.ptr");
        }

        return NULL;
    }

    if (node->kind == AST_UNARY && node->as.unary.op == TOKEN_STAR)
    {
        /* *ptr — the lvalue pointer is the pointer value itself */
        return codegen_expr(ctx, node->as.unary.operand);
    }

    return NULL;
}

/* ---- Lvalue address helper ----
 * Returns a pointer (GEP or alloca) to the storage of an lvalue node
 * without generating a load. Used to compute `self` for instance method calls.
 * Handles AST_IDENT and AST_FIELD (arbitrarily nested). Returns NULL if the
 * node is not an addressable lvalue. */
LLVMValueRef codegen_addr_of(CodegenContext *ctx, AstNode *node)
{
    if (node == NULL)
        return NULL;

    if (node->kind == AST_IDENT)
    {
        CgSymbol *sym = cg_scope_resolve(ctx->current_scope, node->as.ident.name);
        LLVMValueRef storage = sym ? sym->value : NULL;
        if (!storage)
        {
            /* Module-level global: a lifted closure body's scope does not chain
               to the global scope, so the address of a global named inside a
               closure (e.g. a shared Atomic/Mutex method receiver) resolves to
               its global variable here. Without this the method-call path fell
               through to the rvalue-self spill and mutated a private COPY — the
               shared global stayed untouched across worker threads. */
            storage = LLVMGetNamedGlobal(ctx->module, node->as.ident.name);
            if (!storage)
                return NULL;
        }
        Type *rtype = node->resolved_type;
        /* *Struct variable: alloca holds a pointer value; load it to get the heap address */
        if (rtype && rtype->kind == TYPE_POINTER &&
            rtype->as.pointer_to && rtype->as.pointer_to->kind == TYPE_STRUCT)
        {
            LLVMTypeRef ptr_llvm = LLVMPointerTypeInContext(ctx->context, 0);
            return LLVMBuildLoad2(ctx->builder, ptr_llvm, storage, "self.deref");
        }
        /* Stack struct: alloca IS the struct storage */
        return storage;
    }

    if (node->kind == AST_UNARY && node->as.unary.op == TOKEN_STAR)
    {
        /* (*p) — the address of the pointee IS the pointer value (no load). Lets
           a `*Struct` deref be the receiver of a &self/&!self method, e.g. a
           ChanIter holding `*Chan` calling `(*self.ch).recv()`. Without this the
           method-call path fell through to the rvalue-self spill below and
           mutated a private COPY (the pointed-to value stayed untouched).
           Mirrors codegen_lvalue_ptr's deref case. */
        return codegen_expr(ctx, node->as.unary.operand);
    }

    if (node->kind == AST_INDEX)
    {
        /* arr[index] — get pointer to array element */
        AstNode *arr_obj = node->as.index_expr.object;
        Type *arr_type = arr_obj->resolved_type;

        /* Raw *T pointer index: borrow the element in place via a typed GEP (the
           same address codegen_lvalue_ptr computes). Without this, a &self/&T
           method receiver or operator operand on a `*T` slot — e.g. Map's
           `self.keys[idx] == k` probe compare — fell through to the rvalue-self
           spill in the method-call path and DEEP-CLONED the has_drop slot on
           every probe (the alloc benchmark's dominant Str churn). Mirrors the
           array case below; same realloc escape constraint as vec. */
        if (arr_type && arr_type->kind == TYPE_POINTER && arr_type->as.pointer_to)
            return codegen_lvalue_ptr(ctx, node);

        if (arr_type == NULL || arr_type->kind != TYPE_ARRAY)
        {
            return NULL;
        }

        /* Get the array pointer. Recursing through the general place engine
           (rather than the old IDENT / FIELD pair) also covers a nested array
           (`M[0][1]`), a `*p` deref, and an rvalue array that needs a spill. */
        LLVMValueRef arr_ptr = cg_array_place_ptr(ctx, arr_obj);
        if (arr_ptr == NULL)
        {
            return NULL;
        }

        /* Compute element index */
        LLVMValueRef index = codegen_expr(ctx, node->as.index_expr.index);
        if (index == NULL)
            return NULL;

        LLVMTypeRef i64_type = LLVMInt64TypeInContext(ctx->context);
        if (LLVMTypeOf(index) != i64_type)
        {
            index = LLVMBuildSExtOrBitCast(ctx->builder, index, i64_type, "idx.ext");
        }

        LLVMTypeRef arr_llvm = type_to_llvm(ctx, arr_type);
        LLVMValueRef zero = LLVMConstInt(i64_type, 0, 0);
        LLVMValueRef indices[2] = {zero, index};
        return LLVMBuildGEP2(ctx->builder, arr_llvm, arr_ptr,
                             indices, 2, "arr.elem.addr");
    }

    if (node->kind == AST_FIELD)
    {
        AstNode *sub_obj = node->as.field_access.object;
        Type *sub_type = sub_obj->resolved_type;

        bool is_ptr = sub_type && sub_type->kind == TYPE_POINTER &&
                      sub_type->as.pointer_to &&
                      sub_type->as.pointer_to->kind == TYPE_STRUCT;
        Type *struct_type = is_ptr ? sub_type->as.pointer_to : sub_type;
        if (!struct_type || struct_type->kind != TYPE_STRUCT)
            return NULL;

        /* Get pointer to the parent struct recursively */
        LLVMValueRef struct_ptr = codegen_addr_of(ctx, sub_obj);
        if (!struct_ptr)
        {
            /* Sub-expression is not a simple lvalue — evaluate and spill to temp */
            LLVMValueRef sub_val = codegen_expr(ctx, sub_obj);
            if (!sub_val)
                return NULL;
            if (is_ptr)
            {
                struct_ptr = sub_val;
            }
            else
            {
                LLVMTypeRef st_llvm = type_to_llvm(ctx, struct_type);
                struct_ptr = cg_entry_alloca(ctx, st_llvm, "tmp.struct");
                LLVMBuildStore(ctx->builder, sub_val, struct_ptr);
            }
        }

        /* Find field index */
        const char *fname = node->as.field_access.field;
        int fidx = -1;
        for (int i = 0; i < struct_type->as.strukt.field_count; i++)
        {
            if (strcmp(struct_type->as.strukt.fields[i].name, fname) == 0)
            {
                fidx = i;
                break;
            }
        }
        if (fidx < 0)
            return NULL;

        LLVMTypeRef struct_llvm = find_struct_llvm(ctx, struct_type->as.strukt.name);
        if (!struct_llvm)
            struct_llvm = type_to_llvm(ctx, struct_type);

        return LLVMBuildStructGEP2(ctx->builder, struct_llvm,
                                   struct_ptr, (unsigned)fidx, "field.addr");
    }

    /* Operator-overload chain receiver: `(a + b) + c` lowers the inner binary
       to a synthesized method call; the OUTER call's receiver is still the
       AST_BINARY node. Route to the lowered call so the rvalue-receiver spill
       below registers the intermediate has_drop result for cleanup (without
       this it fell into the Phase-2.5 no-drop spill → leaked, e.g. chained
       Str `+`). */
    if (node->kind == AST_BINARY && node->as.binary.lowered != NULL)
        return codegen_addr_of(ctx, node->as.binary.lowered);

    /* Fresh-rvalue receiver: evaluate it, spill to temp alloca so the caller
       can use it as `self` pointer for a chained method call. Register for
       has_drop cleanup so the temporary's buffer is freed at end-of-scope
       (cg_push_temp_drop self-filters: a POD spill is not registered, but it
       still needs the slot to be addressable — hence the KIND-layer predicate
       here, not the type-aware one; see codegen_internal.h). Examples:
         - AST_CALL          — `vec.map(U)(...).reduce(U)(...)`
         - AST_FORMAT_STRING — `f"...".upper()`
         - AST_TRY           — `(try f()).upper()` (payload moved out, nobody
           registered it: the old whitelist missed TRY and leaked it).
       INDEX / FIELD / BINARY.lowered members are unreachable here: they were
       already handled by the lvalue/GEP/reroute branches above. */
    if (cg_expr_is_fresh_rvalue_kind(node))
    {
        Type *rtype = node->resolved_type;
        if (rtype == NULL)
            return NULL;
        LLVMValueRef val = codegen_expr(ctx, node);
        if (val == NULL)
            return NULL;
        /* A borrow-returning call (e.g. Vec.get_ref(i) -> &T) yields a
           reference whose VALUE already IS the pointee's address. Use it
           directly as the self pointer for a chained method call
           (`v.get_ref(i).eq?(x)`); spilling it to a temp would add a level of
           indirection (ptr-to-ptr) and a borrow owns nothing, so there is
           nothing to drop. Mirrors the AST_UNARY(*) case above — a reference
           value is an address. */
        if (rtype->kind == TYPE_REFERENCE)
            return val;
        /* Zero-init in entry block (zeroed=true): this spill's drop may be
           reached on a path that skipped the store (chained-op receiver inside
           a match-arm conditional). Stray drop then no-ops instead of freeing
           stack garbage. */
        return cg_spill_owned_rvalue(ctx, val, rtype, true, "tmp.rval.self");
    }

    return NULL; /* Other lvalue forms not yet handled */
}

/* ---- Fixed-array place: SINGLE AUTHORITY for "address of an array(T,N)" ----
 * A fixed array is represented by the ADDRESS of its storage, never by the
 * loaded aggregate, so every consumer (element GEP, whole-array print, for-in)
 * needs a pointer. Five sites used to hand-roll that address from an IDENT
 * symbol lookup, which silently failed for every other place:
 *   b.d / o.inner.d / bs[k].d / (&Buf b).d / M[0][1] / mk().d
 * Symptoms were split between a "cannot get address of array" codegen error
 * (element read) and — worse — SILENT wrong behaviour: a blank line from
 * @print, a for-in body that never ran, a dropped element store.
 *
 * Resolution is delegated to codegen_addr_of, the general place engine
 * (IDENT local+global, FIELD chains with &T / *T auto-deref, nested INDEX,
 * *p deref, and a temp spill for a genuine rvalue such as `mk().d`).
 *
 * NOT for assignment targets: codegen_addr_of may hand back a spilled
 * temporary, and a store through that is silently discarded. Store sites must
 * use codegen_lvalue_ptr (strict place, no spill) and reject a NULL.
 *
 * Returns NULL only when the node has no place and cannot be evaluated; the
 * caller is responsible for the diagnostic. */
LLVMValueRef cg_array_place_ptr(CodegenContext *ctx, AstNode *node)
{
    if (node == NULL)
        return NULL;

    /* Module-qualified global array (`math.PRIMES[0]`): the object is a MODULE,
       not a struct, so the general engine cannot GEP it. P1-1 gives module
       globals a `<mod>__name` symbol; try that before the bare name, mirroring
       the module field-read path (cg_expr_field). The old open-coded version
       here looked up the BARE name only and so missed prefixed globals. */
    if (node->kind == AST_FIELD &&
        node->as.field_access.object->resolved_type &&
        node->as.field_access.object->resolved_type->kind == TYPE_MODULE)
    {
        const char *mod = node->as.field_access.object->resolved_type->as.module.name;
        const char *fld = node->as.field_access.field;
        char gv_sym[512];
        cg_module_fn_symbol(gv_sym, sizeof(gv_sym), mod, fld);
        LLVMValueRef gv = LLVMGetNamedGlobal(ctx->module, gv_sym);
        if (gv == NULL)
            gv = LLVMGetNamedGlobal(ctx->module, fld);
        return gv;
    }

    return codegen_addr_of(ctx, node);
}

static LLVMValueRef cg_expr_field(CodegenContext *ctx, AstNode *node)
{
    if (node->coerce_fn_to_block)
        return codegen_fn_to_block(ctx, node);

    AstNode *obj_node = node->as.field_access.object;
    Type *obj_type = obj_node->resolved_type;

    /* Module-qualified access (e.g., math.add or math.PI) */
    if (obj_type && obj_type->kind == TYPE_MODULE)
    {
        const char *name = node->as.field_access.field;

        /* Built-in stdlib module: emit constant inline (PI/E/INF/NAN/...).
           Functions reached as bare field-access (without a call) are
           unsupported for now — taking math.sqrt as a function pointer
           would need a wrapper since we have no IR-level body. */
        if (obj_type->as.module.is_builtin &&
            obj_type->as.module.name &&
            strcmp(obj_type->as.module.name, "std.core.math") == 0)
        {
            LLVMValueRef cst = builtin_math_emit_const(ctx, name);
            if (cst) return cst;
            cg_error(ctx, node->line, node->column,
                     "math.%s is not a constant; use it in a call expression",
                     name);
            return NULL;
        }

        /* Try function first: module-prefixed (L-009), then bare. */
        char fn_sym[512];
        cg_module_fn_symbol(fn_sym, sizeof(fn_sym),
                            obj_type->as.module.name, name);
        LLVMValueRef fn = LLVMGetNamedFunction(ctx->module, fn_sym);
        if (!fn)
            fn = LLVMGetNamedFunction(ctx->module, name);
        if (fn)
            return fn;
        /* Try global variable: P1-1 module globals use prefixed name. */
        char gv_sym[512];
        cg_module_fn_symbol(gv_sym, sizeof(gv_sym),
                            obj_type->as.module.name, name);
        LLVMValueRef gv = LLVMGetNamedGlobal(ctx->module, gv_sym);
        if (!gv)
            gv = LLVMGetNamedGlobal(ctx->module, name);
        if (gv)
        {
            Type *rt = node->resolved_type;
            if (rt && rt->kind == TYPE_ARRAY)
            {
                /* For arrays, return the pointer to the global array directly */
                return gv;
            }
            LLVMTypeRef load_type = type_to_llvm(ctx, rt);
            return LLVMBuildLoad2(ctx->builder, load_type, gv, name);
        }
        cg_error(ctx, node->line, node->column,
                 "undefined symbol '%s' in module '%s'",
                 name, obj_type->as.module.name ? obj_type->as.module.name : "?");
        return NULL;
    }

    /* Array .length — compile-time constant */
    if (obj_type && obj_type->kind == TYPE_ARRAY)
    {
        if (strcmp(node->as.field_access.field, "length") == 0)
        {
            return LLVMConstInt(LLVMInt32TypeInContext(ctx->context),
                                (unsigned long long)obj_type->as.array.size, 0);
        }
        cg_error(ctx, node->line, node->column,
                 "array has no field '%s'", node->as.field_access.field);
        return NULL;
    }


    /* Auto-dereference pointer-to-struct for field access (self.x where self is *Struct) */
    bool is_ptr_deref = false;
    /* Phase 2 (borrow extension): obj is a borrow result (&Struct), e.g.
       `obj.get_ref().field`. The evaluated value IS the struct pointer (the
       pointer ABI of &T), so GEP it directly — no alloca spill. */
    bool is_ref_value = false;
    Type *struct_type = obj_type;
    if (obj_type && obj_type->kind == TYPE_POINTER && obj_type->as.pointer_to &&
        obj_type->as.pointer_to->kind == TYPE_STRUCT)
    {
        struct_type = obj_type->as.pointer_to;
        is_ptr_deref = true;
    }
    else if (obj_type && obj_type->kind == TYPE_REFERENCE && obj_type->as.pointer_to &&
             obj_type->as.pointer_to->kind == TYPE_STRUCT)
    {
        struct_type = obj_type->as.pointer_to;
        is_ref_value = true;
    }

    /* obj.field — struct field access */
    if (struct_type == NULL || struct_type->kind != TYPE_STRUCT)
    {
        cg_error(ctx, node->line, node->column, "field access on non-struct");
        return NULL;
    }

    const char *field_name = node->as.field_access.field;
    int field_idx = -1;
    for (int i = 0; i < struct_type->as.strukt.field_count; i++)
    {
        if (strcmp(struct_type->as.strukt.fields[i].name, field_name) == 0)
        {
            field_idx = i;
            break;
        }
    }
    if (field_idx < 0)
    {
        cg_error(ctx, node->line, node->column,
                 "struct '%s' has no field '%s'",
                 struct_type->as.strukt.name, field_name);
        return NULL;
    }

    /* Get the pointer to the struct for GEP */
    LLVMValueRef struct_ptr = NULL;
    if (is_ref_value)
    {
        /* Phase 2: the borrow result evaluates to the struct pointer. */
        struct_ptr = codegen_expr(ctx, obj_node);
    }
    else if (obj_node->kind == AST_IDENT)
    {
        CgSymbol *sym = cg_scope_resolve(ctx->current_scope, obj_node->as.ident.name);
        if (sym)
        {
            if (is_ptr_deref)
            {
                /* self is *Struct: alloca holds a pointer, load it to get the actual struct ptr */
                LLVMTypeRef ptr_llvm = LLVMPointerTypeInContext(ctx->context, 0);
                struct_ptr = LLVMBuildLoad2(ctx->builder, ptr_llvm, sym->value, "self.deref");
            }
            else
            {
                /* obj is a struct value: alloca IS the struct pointer */
                struct_ptr = sym->value;
            }
        }
    }
    /* BF-040: array element field read (arr[i].field). The element lives
       in-place inside the array alloca, so take its lvalue address via GEP
       and read the field directly — instead of cloning the whole has_drop
       struct (the M-4.5 clone+temp_drop path below). Cloning would invoke
       the user __drop on a transient clone, double-firing side effects
       (drop_count doubled per read). Only arrays expose a stable element
       lvalue here; vec[i] returns NULL from codegen_lvalue_ptr and keeps
       the clone path (its heap data may realloc, so no stable address). */
    if (struct_ptr == NULL && !is_ptr_deref)
    {
        AstNode *uobj = ast_unwrap_move(obj_node);
        if (uobj->kind == AST_INDEX &&
            uobj->as.index_expr.object->resolved_type &&
            uobj->as.index_expr.object->resolved_type->kind == TYPE_ARRAY)
        {
            struct_ptr = codegen_lvalue_ptr(ctx, uobj);
        }
        /* Transient read-through of a chained struct field: `a.b.c` where the
           object `a.b` is itself a struct field rooted in stable named storage
           (or an array element). Borrow `&a.b` via GEP instead of deep-cloning
           the whole intermediate has_drop struct. The clone path below (11873)
           produces an owned temporary that is never registered for drop → it
           leaks its vec/map/string/nested heap, and re-fires user __drop side
           effects. Reading through the borrow is safe — only the finally
           accessed field is cloned below. When `a.b` is a terminal value
           binding (`Box x = a.b`), the AST_FIELD object is an IDENT and is
           handled above (struct_ptr from the symbol), so the clone is retained
           there and correctly consumed by the binding. Mirrors the BF-040
           array-element borrow; codegen_lvalue_ptr returns NULL for non-lvalue
           roots (e.g. `make_box().inner`), falling through to the clone path. */
        else if (uobj->kind == AST_FIELD)
        {
            struct_ptr = codegen_lvalue_ptr(ctx, uobj);
        }
    }
    if (struct_ptr == NULL)
    {
        /* obj_node is not a simple identifier (e.g. px.color.r — chained field access).
           Evaluate the sub-expression to get a struct value, then spill to a temp alloca
           so we can use GEP to read the field. */
        LLVMValueRef sub_val = codegen_expr(ctx, obj_node);
        if (sub_val == NULL)
            return NULL;
        /* If sub_val is already a pointer to the struct (is_ptr_deref), use directly */
        if (is_ptr_deref)
        {
            struct_ptr = sub_val;
        }
        else
        {
            /* M-4.5: when the object is vec[i]/arr[i] of a has_drop struct,
               sub_val is an owned deep clone (the container keeps its own copy).
               Field access reads one field; the rest of this temporary struct's
               owned resources (other string fields, nested drops) would leak.
               Spill + register so the statement-end flush drops it. The
               accessed field is independently cloned below, so dropping the
               temporary here does not invalidate the returned value. */
            /* Owned rvalue struct sources whose temp must be dropped after the
               field read: container index (vec[i]/p[i]), a CALL returning a
               has_drop struct by value (f().field / obj.method(i).field), a
               nested field read whose own object had no stable lvalue
               (`v[i].inner.field`), and the other fresh-rvalue producers
               (`(match …).field` / `(try f()).field` / lowered-operator
               results) — the old INDEX/CALL/FIELD list missed those, leaking
               the spilled struct's OTHER owned fields
               (own_rvalue_sites_test.lls). Reaching this else-branch at all
               means obj_node has no backing lvalue (codegen_lvalue_ptr
               returned NULL above) → the spilled struct value is an owned
               rvalue → the accessed field is cloned below, so dropping the
               temp is safe for any has_drop source. Membership rationale
               lives on cg_expr_yields_owned_rvalue (codegen_internal.h).
               A POD source still needs the spill for the GEP below — bare
               alloca+store, nothing to register. */
            AstNode *uobj_src = ast_unwrap_move(obj_node);
            if (cg_expr_yields_owned_rvalue(uobj_src, struct_type))
                struct_ptr = cg_spill_owned_rvalue(ctx, sub_val, struct_type,
                                                   false, "tmp.struct");
            else
            {
                struct_ptr = cg_entry_alloca(
                    ctx, type_to_llvm(ctx, struct_type), "tmp.struct");
                LLVMBuildStore(ctx->builder, sub_val, struct_ptr);
            }
        }
    }

    LLVMTypeRef struct_llvm = find_struct_llvm(ctx, struct_type->as.strukt.name);
    if (struct_llvm == NULL)
    {
        struct_llvm = type_to_llvm(ctx, struct_type);
    }

    LLVMValueRef gep = LLVMBuildStructGEP2(ctx->builder, struct_llvm,
                                           struct_ptr, (unsigned)field_idx, "field");
    Type *field_type = struct_type->as.strukt.fields[field_idx].type;
    LLVMTypeRef field_llvm = type_to_llvm(ctx, field_type);
    LLVMValueRef field_val = LLVMBuildLoad2(ctx->builder, field_llvm, gep, field_name);
    /* Struct field access is a READ — the struct retains ownership of its fields.
       Clone owned data so the caller gets an independent copy.
       Without this, both the caller's variable and the struct would try to free
       the same string/owned-struct data → double-free.
       CONTRACT: this clone behavior is one half of the table on
       cg_match_subject_is_owned_rvalue (codegen_match.c) — an AST_FIELD match
       subject is judged owned and dropped at merge, which is only sound
       because the read here yields an independent copy. Change one side,
       re-check the other. */
    if (field_type && field_type->kind == TYPE_STRUCT && field_type->as.strukt.has_drop)
        field_val = emit_struct_clone_val(ctx, field_val, field_llvm, field_type);
    /* Symmetric to the struct branch: reading a has_drop ENUM field
       (Option(Str), a user enum with a Str/owned payload, ...) is also a READ —
       the struct keeps ownership of its payload. Without a clone the loaded enum
       aliases the struct's payload heap; a consumer that treats it as owned
       (e.g. a `match` subject — a non-IDENT AST_FIELD is an owned rvalue temp,
       dropped at merge, see cg_match_subject_is_owned_rvalue in
       codegen_match.c) then double-frees that payload against the struct's own
       scope-exit drop. Mirrors AST_INDEX enum reads, which already clone.
       (memcheck-found 2026-07-04; field_enum_subject_test.lls = BUG-1.) */
    else if (field_type && field_type->kind == TYPE_ENUM && field_type->as.enom.has_drop)
        field_val = emit_enum_clone_val(ctx, field_val, field_type);
    /* Same reasoning one level down for a FIXED-ARRAY field whose ELEMENTS own
       heap (`array(Str,2) d`): the load copies the aggregate bit-for-bit, so
       every element handle would be shared with the struct's own field and both
       sides would drop it. Reading a place clones — exactly like the struct and
       enum branches above; the IDENT-to-IDENT array bind clones in var_decl
       instead, mirroring how struct binds are split. (L-023 site ④.)
       Same CONTRACT as the two branches above: the AST_FIELD row of the table
       on cg_match_subject_is_owned_rvalue (codegen_match.c) counts on this
       read yielding an independent copy. */
    else if (field_type && field_type->kind == TYPE_ARRAY &&
             type_array_elem_owns_heap(field_type))
        field_val = emit_array_clone_val(ctx, field_val, field_llvm, field_type);
    /* F.3: Block field read — the struct retains env ownership, so the loaded
       LsBlock is a shallow ALIAS; return it directly (do NOT clone here).
       Phase G removed the old `Block g = p.step1` rejection: binding a Block
       out of a field now deep-clones the env at the BIND site
       (check_stmt_var_decl in checker_stmt.c documents the checker half;
       cg_emit_block_env_clone does the work), and a direct call
       `p.step1(args)` just uses the aliased value. (When the WHOLE struct is
       cloned by value, emit_struct_clone_val deep-clones its Block fields —
       a separate path.) */
    return field_val;
}

static LLVMValueRef cg_expr_index(CodegenContext *ctx, AstNode *node)
{
    AstNode *obj = node->as.index_expr.object;
    AstNode *idx_node = node->as.index_expr.index;
    Type *obj_type = obj->resolved_type;
    LLVMTypeRef i64_t0 = LLVMInt64TypeInContext(ctx->context);
    LLVMValueRef zero64 = LLVMConstInt(i64_t0, 0, 0);

    /* Slice creation `v[a..b]` — build a {ptr,len} view over a Vec(T) (or a
       sub-slice of a slice), bounds-checked: 0 <= a <= b <= len. */
    if (node->resolved_type && node->resolved_type->kind == TYPE_SLICE &&
        idx_node && idx_node->kind == AST_RANGE)
    {
        Type *slice_t = node->resolved_type;
        LLVMTypeRef elem_llvm = type_to_llvm(ctx, slice_t->as.array.elem);
        LLVMValueRef base_ptr = NULL, src_len = NULL;
        if (obj_type && obj_type->kind == TYPE_SLICE)
        {
            LLVMValueRef sv = codegen_expr(ctx, obj);
            base_ptr = LLVMBuildExtractValue(ctx->builder, sv, 0, "src.ptr");
            src_len  = LLVMBuildExtractValue(ctx->builder, sv, 1, "src.len");
        }
        else
        {
            /* Vec(T): field 0 = *T data, field 1 = i32 len. */
            LLVMValueRef vec_ptr = codegen_lvalue_ptr(ctx, obj);
            if (vec_ptr == NULL) { cg_error(ctx, node->line, node->column,
                "cannot take address of slice source"); return NULL; }
            LLVMTypeRef vec_llvm = type_to_llvm(ctx, obj_type);
            LLVMValueRef dgep = LLVMBuildStructGEP2(ctx->builder, vec_llvm, vec_ptr, 0, "v.data.p");
            base_ptr = LLVMBuildLoad2(ctx->builder,
                LLVMPointerTypeInContext(ctx->context, 0), dgep, "v.data");
            LLVMValueRef lgep = LLVMBuildStructGEP2(ctx->builder, vec_llvm, vec_ptr, 1, "v.len.p");
            LLVMValueRef len32 = LLVMBuildLoad2(ctx->builder,
                LLVMInt32TypeInContext(ctx->context), lgep, "v.len");
            src_len = LLVMBuildSExt(ctx->builder, len32, i64_t0, "v.len64");
        }
        AstNode *rng = idx_node;
        LLVMValueRef lo = rng->as.range.start ? codegen_expr(ctx, rng->as.range.start) : zero64;
        LLVMValueRef hi = rng->as.range.end   ? codegen_expr(ctx, rng->as.range.end)   : src_len;
        if (LLVMTypeOf(lo) != i64_t0) lo = LLVMBuildSExtOrBitCast(ctx->builder, lo, i64_t0, "lo64");
        if (LLVMTypeOf(hi) != i64_t0) hi = LLVMBuildSExtOrBitCast(ctx->builder, hi, i64_t0, "hi64");
        /* 0 <= lo && lo <= hi && hi <= len */
        LLVMValueRef c1 = LLVMBuildICmp(ctx->builder, LLVMIntSGE, lo, zero64, "c1");
        LLVMValueRef c2 = LLVMBuildICmp(ctx->builder, LLVMIntSLE, lo, hi, "c2");
        LLVMValueRef c3 = LLVMBuildICmp(ctx->builder, LLVMIntSLE, hi, src_len, "c3");
        LLVMValueRef ok = LLVMBuildAnd(ctx->builder,
            LLVMBuildAnd(ctx->builder, c1, c2, "ok12"), c3, "ok");
        cg_emit_bounds_guard(ctx, ok, "Slice range out of bounds", node->line, node->column);
        LLVMValueRef slen = LLVMBuildSub(ctx->builder, hi, lo, "slice.length");
        return cg_make_slice(ctx, elem_llvm, base_ptr, lo, slen, slice_t);
    }

    /* `slice[i]` — bounds-checked element read of a borrowed slice. */
    if (obj_type && obj_type->kind == TYPE_SLICE)
    {
        LLVMValueRef sv = codegen_expr(ctx, obj);
        LLVMValueRef sptr = LLVMBuildExtractValue(ctx->builder, sv, 0, "s.ptr");
        LLVMValueRef slen = LLVMBuildExtractValue(ctx->builder, sv, 1, "s.len");
        LLVMValueRef index = codegen_expr(ctx, idx_node);
        if (LLVMTypeOf(index) != i64_t0)
            index = LLVMBuildSExtOrBitCast(ctx->builder, index, i64_t0, "si.idx");
        LLVMValueRef ge = LLVMBuildICmp(ctx->builder, LLVMIntSGE, index, zero64, "sge");
        LLVMValueRef lt = LLVMBuildICmp(ctx->builder, LLVMIntSLT, index, slen, "slt");
        LLVMValueRef ok = LLVMBuildAnd(ctx->builder, ge, lt, "sok");
        cg_emit_bounds_guard(ctx, ok, "Slice index out of bounds", node->line, node->column);
        LLVMTypeRef elem_llvm = type_to_llvm(ctx, obj_type->as.array.elem);
        LLVMValueRef gep = LLVMBuildGEP2(ctx->builder, elem_llvm, sptr, &index, 1, "s.elem.p");
        LLVMValueRef elem = LLVMBuildLoad2(ctx->builder, elem_llvm, gep, "s.elem");
        /* READ = independent copy; contract with
           cg_match_subject_is_owned_rvalue (codegen_match.c). */
        return emit_clone_value(ctx, elem, elem_llvm, obj_type->as.array.elem);
    }

    /* p[i] on a raw *T pointer — load the pointer value, typed-GEP element,
       load it, then DEEP-CLONE owned element data — matching vec[i]/array[i]
       read semantics exactly (a read yields an independent copy; the slot
       keeps its own, so both can be dropped without double-free). POD /
       non-has_drop is returned as-is (emit_clone_value is a no-op). The GEP
       stride comes from the SAME DataLayout as sizeof(T), so struct padding
       is handled automatically. No bounds check (unsafe layer).
       NOTE: zero-copy move-out is NOT done here (would alias); a container
       that wants move-out reads + __drop_at(slot) (clone + drop original). */
    if (obj_type && obj_type->kind == TYPE_POINTER && obj_type->as.pointer_to)
    {
        LLVMValueRef ptr_val = codegen_expr(ctx, obj);
        if (ptr_val == NULL)
            return NULL;
        LLVMValueRef index = codegen_expr(ctx, idx_node);
        if (index == NULL)
            return NULL;
        LLVMTypeRef i64_t = LLVMInt64TypeInContext(ctx->context);
        if (LLVMTypeOf(index) != i64_t)
            index = LLVMBuildSExtOrBitCast(ctx->builder, index, i64_t, "pi.idx");
        Type *elem_type = obj_type->as.pointer_to;
        LLVMTypeRef elem_llvm = type_to_llvm(ctx, elem_type);
        LLVMValueRef gep = LLVMBuildGEP2(ctx->builder, elem_llvm, ptr_val,
                                         &index, 1, "ptr.idx");
        LLVMValueRef elem = LLVMBuildLoad2(ctx->builder, elem_llvm, gep, "ptr.elem");
        /* Deep-clone owned element data (string/vec/has_drop struct|enum).
           Contract with cg_match_subject_is_owned_rvalue (codegen_match.c). */
        elem = emit_clone_value(ctx, elem, elem_llvm, elem_type);
        return elem;
    }

    /* arr[index] — GEP into fixed array + load element */
    if (obj_type == NULL || obj_type->kind != TYPE_ARRAY)
    {
        cg_error(ctx, node->line, node->column, "index on non-array/non-vec");
        return NULL;
    }

    /* Get the alloca/global pointer for the array (not a load) */
    LLVMValueRef arr_ptr = cg_array_place_ptr(ctx, obj);
    if (arr_ptr == NULL)
    {
        cg_error(ctx, node->line, node->column, "cannot get address of array");
        return NULL;
    }

    LLVMValueRef index = codegen_expr(ctx, idx_node);
    if (index == NULL)
        return NULL;

    /* Ensure index is i64 for GEP */
    LLVMTypeRef i64_type = LLVMInt64TypeInContext(ctx->context);
    if (LLVMTypeOf(index) != i64_type)
    {
        index = LLVMBuildSExtOrBitCast(ctx->builder, index, i64_type, "idx.ext");
    }

    LLVMTypeRef arr_llvm = type_to_llvm(ctx, obj_type);
    LLVMValueRef zero = LLVMConstInt(i64_type, 0, 0);
    LLVMValueRef indices[2] = {zero, index};
    LLVMValueRef gep = LLVMBuildGEP2(ctx->builder, arr_llvm, arr_ptr,
                                     indices, 2, "arr.idx");
    Type *elem_type = obj_type->as.array.elem;
    LLVMTypeRef elem_llvm = type_to_llvm(ctx, elem_type);
    LLVMValueRef elem = LLVMBuildLoad2(ctx->builder, elem_llvm, gep, "arr.elem");
    /* array[i] is a READ — the array retains ownership.  Clone owned data
       to give the caller an independent copy (mirrors vec[i] semantics).
       CONTRACT with cg_match_subject_is_owned_rvalue (codegen_match.c):
       an AST_INDEX match subject is dropped at merge as an owned temp —
       sound only while this read clones. */
    elem = emit_clone_value(ctx, elem, elem_llvm, elem_type);
    return elem;
}

/* cg_store_array_lit_elements — materialise an array literal element by element
   into an array slot that already exists.

   CONTRACT (cg_expr_array_lit): an array literal whose elements are not all pure
   literals emits NOTHING and returns NULL, because the CALLER is expected to
   store the elements itself. var_decl has always honoured that (its sibling loop
   in codegen_stmt.c); the struct-literal field path had no such fallback and
   silently gave up, leaving the field as uninitialised stack garbage with rc=0
   and no diagnostic — `S { d: [mk(), mk()] }` then produced garbage element
   values, segfaulted when printing an element, and hit an invalid free at
   scope exit. (L-023 site ④'s real cause, value+memcheck probes 2026-07-25.)

   Ownership goes through cg_store_owned, the same protocol the var_decl sibling
   and struct-literal fields use: a named owned source moves in (and is marked
   moved), a borrowed source is deep-copied, a fresh rvalue is taken as is
   (L-023 sites ① and ④). The destination is a freshly zero-initialised struct
   field, so there is no old value to drop. A NULL element means that element
   already reported its own diagnostic; skip it and let compilation fail,
   mirroring the sibling. */
static void cg_store_array_lit_elements(CodegenContext *ctx, LLVMValueRef slot,
                                        Type *arr_type, AstNode *lit)
{
    LLVMTypeRef arr_llvm = type_to_llvm(ctx, arr_type);
    LLVMTypeRef i64_t = LLVMInt64TypeInContext(ctx->context);
    LLVMValueRef zero = LLVMConstInt(i64_t, 0, 0);
    Type *elem_ty = arr_type->as.array.elem;
    int count = lit->as.array_lit.count;

    for (int i = 0; i < count; i++)
    {
        AstNode *el = lit->as.array_lit.elements[i];
        LLVMValueRef ev = codegen_expr(ctx, el);
        if (ev == NULL)
            continue;
        LLVMValueRef idx = LLVMConstInt(i64_t, (uint64_t)i, 0);
        LLVMValueRef indices[2] = {zero, idx};
        LLVMValueRef gep = LLVMBuildGEP2(ctx->builder, arr_llvm, slot,
                                         indices, 2, "fld.arr.init");
        cg_store_owned(ctx, gep, ev, elem_ty, el);
    }
}

static LLVMValueRef cg_expr_new_expr(CodegenContext *ctx, AstNode *node)
{
    bool on_stack = node->as.new_expr.on_stack;

    /* Resolve struct type */
    Type *struct_type;
    if (on_stack)
    {
        /* StructName{...} — value literal, resolved_type is TYPE_STRUCT */
        struct_type = node->resolved_type;
    }
    else
    {
        /* new StructName{...} — heap, resolved_type is *TYPE_STRUCT */
        Type *ptr_type = node->resolved_type;
        if (!ptr_type || ptr_type->kind != TYPE_POINTER || !ptr_type->as.pointer_to)
        {
            cg_error(ctx, node->line, node->column, "new_expr: bad resolved type");
            return NULL;
        }
        struct_type = ptr_type->as.pointer_to;
    }
    if (!struct_type || struct_type->kind != TYPE_STRUCT)
    {
        cg_error(ctx, node->line, node->column, "new_expr: not a struct type");
        return NULL;
    }

    LLVMTypeRef st_llvm = type_to_llvm(ctx, struct_type);

    /* Allocate storage: stack alloca for value literal, malloc for new.
       Bug #24: the alloca MUST be in the function entry block, not at the
       current builder position. If this struct literal is inside a loop
       body, a per-iteration alloca grows the stack without bound (LLVM
       alloca is only freed on function return) → stack overflow in JIT
       (default 1 MB stack; 100k × 16B = 1.6 MB). AOT hides it because
       the O2 mem2reg pass promotes the alloca to a register. */
    LLVMValueRef storage;
    if (on_stack)
    {
        LLVMValueRef cur_fn = LLVMGetBasicBlockParent(
            LLVMGetInsertBlock(ctx->builder));
        LLVMBasicBlockRef entry_bb = LLVMGetEntryBasicBlock(cur_fn);
        LLVMBuilderRef entry_b = LLVMCreateBuilderInContext(ctx->context);
        LLVMValueRef first_instr = LLVMGetFirstInstruction(entry_bb);
        if (first_instr)
            LLVMPositionBuilderBefore(entry_b, first_instr);
        else
            LLVMPositionBuilderAtEnd(entry_b, entry_bb);
        storage = LLVMBuildAlloca(entry_b, st_llvm, "sl.tmp");
        LLVMDisposeBuilder(entry_b);
    }
    else
    {
        LLVMValueRef size_val = LLVMSizeOf(st_llvm);
        LLVMValueRef malloc_fn = LLVMGetNamedFunction(ctx->module, "malloc");
        LLVMTypeRef malloc_type = LLVMGlobalGetValueType(malloc_fn);
        storage = LLVMBuildCall2(ctx->builder, malloc_type, malloc_fn,
                                 &size_val, 1, "new_raw");
    }

    /* Zero-initialize */
    LLVMBuildStore(ctx->builder, LLVMConstNull(st_llvm), storage);

    /* Apply field initializers — M-3: 统一所有权转移 */
    int ninits = node->as.new_expr.field_init_count;
    for (int i = 0; i < ninits; i++)
    {
        const char *fname = node->as.new_expr.field_inits[i].name;
        int field_idx = -1;
        for (int j = 0; j < struct_type->as.strukt.field_count; j++)
        {
            if (strcmp(struct_type->as.strukt.fields[j].name, fname) == 0)
            {
                field_idx = j;
                break;
            }
        }
        if (field_idx < 0)
            continue;

        /* 记录本字段求值前的 temp mark，供 cg_store_owned 的 rvalue pop 使用 */
        AstNode *init_expr = node->as.new_expr.field_inits[i].value;
        LLVMValueRef val = codegen_expr(ctx, init_expr);
        Type *field_type = struct_type->as.strukt.fields[field_idx].type;
        if (val == NULL)
        {
            /* Array-literal field with non-constant elements: nothing was
               emitted and the caller owes the element stores (see
               cg_store_array_lit_elements). */
            if (field_type && field_type->kind == TYPE_ARRAY &&
                init_expr->kind == AST_ARRAY_LIT)
            {
                LLVMValueRef arr_ptr = LLVMBuildStructGEP2(ctx->builder, st_llvm,
                                                           storage,
                                                           (unsigned)field_idx,
                                                           "field_arr_ptr");
                cg_store_array_lit_elements(ctx, arr_ptr, field_type, init_expr);
                continue;
            }
            return NULL;
        }

        LLVMValueRef field_ptr = LLVMBuildStructGEP2(ctx->builder, st_llvm,
                                                     storage, (unsigned)field_idx,
                                                     "field_ptr");
        /* cg_store_owned 处理：string clone/move、Block env 转移、
           struct/enum/vec/map move 标记，以及 POD 直接 store */
        cg_store_owned(ctx, field_ptr, val, field_type,
                       node->as.new_expr.field_inits[i].value);
    }

    /* Fill any field not explicitly initialized with its declared default
       (struct field default, v1). Defaults are evaluated here, at the
       construction site — same ownership path as an explicit initializer. */
    for (int j = 0; j < struct_type->as.strukt.field_count; j++)
    {
        bool provided = false;
        for (int i = 0; i < ninits; i++)
        {
            if (strcmp(node->as.new_expr.field_inits[i].name,
                       struct_type->as.strukt.fields[j].name) == 0)
            {
                provided = true;
                break;
            }
        }
        if (provided)
            continue;
        AstNode *deflt = (AstNode *)struct_type->as.strukt.fields[j].default_expr;
        if (deflt == NULL)
            continue; /* checker already errored; leave zero-init */

        Type *field_type = struct_type->as.strukt.fields[j].type;
        LLVMValueRef field_ptr = LLVMBuildStructGEP2(ctx->builder, st_llvm,
                                                     storage, (unsigned)j,
                                                     "field_ptr_def");

        /* v2: vec(T) field with an array-literal default — build the vec in
           place (codegen_expr on an array literal yields a fixed array, not
           a vec). The field is already zero-initialized (cap=0/len=0/NULL),
           so we grow + push each element directly into field_ptr. Empty []
           leaves the valid zero vec. */
        LLVMValueRef val = codegen_expr(ctx, deflt);
        if (val == NULL)
            return NULL;
        cg_store_owned(ctx, field_ptr, val, field_type, deflt);
    }

    if (on_stack)
    {
        /* Return the loaded struct aggregate value */
        return LLVMBuildLoad2(ctx->builder, st_llvm, storage, "sl.val");
    }
    return storage; /* new: return pointer */
}

static LLVMValueRef cg_expr_binary(CodegenContext *ctx, AstNode *node)
{
    /* Operator overloading: the checker lowered `a OP b` to a synthesized
       method-call (or derived) expression. Emit that instead; it reuses the
       full instance-method-call codegen (self borrow, sret, drop, etc.). */
    if (node->as.binary.lowered)
        return codegen_expr(ctx, node->as.binary.lowered);

    /* Short-circuit for logical && and || */
    if (node->as.binary.op == TOKEN_AND || node->as.binary.op == TOKEN_OR)
    {
        return codegen_short_circuit(ctx, node);
    }

    LLVMValueRef left = codegen_expr_or_borrow(ctx, node->as.binary.left);
    LLVMValueRef right = codegen_expr_or_borrow(ctx, node->as.binary.right);
    if (left == NULL || right == NULL)
        return NULL;

    Type *lt = node->as.binary.left->resolved_type;
    Type *rt = node->as.binary.right->resolved_type;

    /* Implicit numeric widening: if the operands have different numeric
       types but the checker accepted them, the result type is the common
       wider type. Promote each operand to that common type so the
       subsequent op (add/sub/cmp/...) sees uniform LLVM types. */
    Type *common = NULL;
    if (lt && rt &&
        type_is_numeric(lt) && type_is_numeric(rt))
    {
        common = type_numeric_common(lt, rt);
        if (common != NULL)
        {
            left = cg_widen(ctx, left, lt, common);
            right = cg_widen(ctx, right, rt, common);
            lt = common;  /* drive is_fp / is_signed off the common type */
        }
    }

    /* For Simd(T,N) operands the LLVM op is a vector op (LLVMBuildFAdd on a
       <N x float> is element-wise vector fadd); pick float vs int by the
       ELEMENT type, since type_is_float(Simd) is false. */
    Type *op_t = (lt && lt->kind == TYPE_SIMD) ? lt->as.simd.elem : lt;
    bool is_fp = op_t && type_is_float(op_t);
    bool is_signed_int = op_t && type_is_signed(op_t);

    switch (node->as.binary.op)
    {
    /* Signed integer arithmetic is emitted with `nsw` (no-signed-wrap):
       signed overflow is undefined (C semantics), which lets LLVM's
       IndVarSimplify widen i32 loop induction vars to i64 and LSR
       strength-reduce affine array addressing into pointer-walking —
       without nsw the i32 IV can't be widened, forcing a sext (movslq)
       and explicit offset math (leal) on every indexed access. Measured
       ~+25% on the packed sgemm micro-kernel. Unsigned keeps wrapping. */
    case TOKEN_PLUS:
        if (is_fp)
            return cg_fp_contract(LLVMBuildFAdd(ctx->builder, left, right, "fadd"));
        if (is_signed_int)
            return LLVMBuildNSWAdd(ctx->builder, left, right, "add");
        return LLVMBuildAdd(ctx->builder, left, right, "add");
    case TOKEN_MINUS:
        if (is_fp)
            return cg_fp_contract(LLVMBuildFSub(ctx->builder, left, right, "fsub"));
        if (is_signed_int)
            return LLVMBuildNSWSub(ctx->builder, left, right, "sub");
        return LLVMBuildSub(ctx->builder, left, right, "sub");
    case TOKEN_STAR:
        if (is_fp)
            return cg_fp_contract(LLVMBuildFMul(ctx->builder, left, right, "fmul"));
        if (is_signed_int)
            return LLVMBuildNSWMul(ctx->builder, left, right, "mul");
        return LLVMBuildMul(ctx->builder, left, right, "mul");
    case TOKEN_SLASH:
        if (is_fp)
            return cg_fp_contract(LLVMBuildFDiv(ctx->builder, left, right, "fdiv"));
        if (is_signed_int)
            return LLVMBuildSDiv(ctx->builder, left, right, "sdiv");
        return LLVMBuildUDiv(ctx->builder, left, right, "udiv");
    case TOKEN_PERCENT:
        if (is_signed_int)
            return LLVMBuildSRem(ctx->builder, left, right, "srem");
        return LLVMBuildURem(ctx->builder, left, right, "urem");

    /* Bitwise */
    case TOKEN_AMP:
        return LLVMBuildAnd(ctx->builder, left, right, "and");
    case TOKEN_PIPE:
        return LLVMBuildOr(ctx->builder, left, right, "or");
    case TOKEN_CARET:
        return LLVMBuildXor(ctx->builder, left, right, "xor");
    case TOKEN_LSHIFT:
        return LLVMBuildShl(ctx->builder, left, right, "shl");
    case TOKEN_RSHIFT:
        if (is_signed_int)
            return LLVMBuildAShr(ctx->builder, left, right, "ashr");
        return LLVMBuildLShr(ctx->builder, left, right, "lshr");

    /* Comparison */
    case TOKEN_EQ:
        if (is_fp)
            return LLVMBuildFCmp(ctx->builder, LLVMRealOEQ, left, right, "feq");
        return LLVMBuildICmp(ctx->builder, LLVMIntEQ, left, right, "eq");
    case TOKEN_NEQ:
        if (is_fp)
            return LLVMBuildFCmp(ctx->builder, LLVMRealONE, left, right, "fne");
        return LLVMBuildICmp(ctx->builder, LLVMIntNE, left, right, "ne");
    case TOKEN_LT:
        if (is_fp)
            return LLVMBuildFCmp(ctx->builder, LLVMRealOLT, left, right, "flt");
        if (is_signed_int)
            return LLVMBuildICmp(ctx->builder, LLVMIntSLT, left, right, "slt");
        return LLVMBuildICmp(ctx->builder, LLVMIntULT, left, right, "ult");
    case TOKEN_GT:
        if (is_fp)
            return LLVMBuildFCmp(ctx->builder, LLVMRealOGT, left, right, "fgt");
        if (is_signed_int)
            return LLVMBuildICmp(ctx->builder, LLVMIntSGT, left, right, "sgt");
        return LLVMBuildICmp(ctx->builder, LLVMIntUGT, left, right, "ugt");
    case TOKEN_LEQ:
        if (is_fp)
            return LLVMBuildFCmp(ctx->builder, LLVMRealOLE, left, right, "fle");
        if (is_signed_int)
            return LLVMBuildICmp(ctx->builder, LLVMIntSLE, left, right, "sle");
        return LLVMBuildICmp(ctx->builder, LLVMIntULE, left, right, "ule");
    case TOKEN_GEQ:
        if (is_fp)
            return LLVMBuildFCmp(ctx->builder, LLVMRealOGE, left, right, "fge");
        if (is_signed_int)
            return LLVMBuildICmp(ctx->builder, LLVMIntSGE, left, right, "sge");
        return LLVMBuildICmp(ctx->builder, LLVMIntUGE, left, right, "uge");

    default:
        cg_error(ctx, node->line, node->column, "unsupported binary operator");
        return NULL;
    }
}

static LLVMValueRef cg_expr_array_lit(CodegenContext *ctx, AstNode *node)
{
    /* Array literal — build constant array if possible, else return NULL
       (caller VAR_DECL handles element-by-element store) */
    Type *arr_type = node->resolved_type;
    if (arr_type && arr_type->kind == TYPE_STRUCT)
        return emit_user_from_list_value(ctx, arr_type, node);
    if (arr_type == NULL || arr_type->kind != TYPE_ARRAY)
        return NULL;

    int count = node->as.array_lit.count;
    LLVMTypeRef elem_llvm = type_to_llvm(ctx, arr_type->as.array.elem);

    /* Constant-fold PRE-SCAN on the AST — decide WITHOUT emitting.
       The old probe emitted every element first and tested
       LLVMIsConstant on the results: on a non-constant element it
       returned NULL, but the already-emitted element code stayed in
       the IR, so the caller's element-by-element fallback evaluated
       every element a SECOND time. Consequences (2026-07-04, found
       via own-rvalue corpus): element side effects ran twice, and the
       first emission's owned results (e.g. `[heaped(..), ..]` Str
       returns) were bound to nothing and leaked. Only pure literals
       qualify for folding; anything else defers to the caller's
       element-by-element path — the single emission. */
    bool all_lit = true;
    for (int i = 0; i < count && all_lit; i++)
    {
        AstNode *el = node->as.array_lit.elements[i];
        switch (el->kind)
        {
        case AST_INT_LIT:
        case AST_FLOAT_LIT:
        case AST_BOOL_LIT:
        case AST_NIL_LIT:
        case AST_STRING_LIT: /* static Str struct — no heap, no drop */
            break;
        case AST_UNARY:
            /* [-1, 2.5]: negation over a numeric literal const-folds. */
            if (el->as.unary.op == TOKEN_MINUS && el->as.unary.operand &&
                (el->as.unary.operand->kind == AST_INT_LIT ||
                 el->as.unary.operand->kind == AST_FLOAT_LIT))
                break;
            all_lit = false;
            break;
        default:
            all_lit = false;
            break;
        }
    }
    if (!all_lit)
        return NULL; /* caller stores element-by-element; nothing emitted */

    /* All pure literals: emit them (constant expressions only — no
       instruction-stream side effects) and build the constant array.
       The LLVMIsConstant check stays as a defensive backstop; literal
       emission produces no instructions, so a surprise non-constant
       here still cannot double-evaluate or leak. */
    LLVMValueRef *elems = (LLVMValueRef *)malloc_safe(
        (size_t)count * sizeof(LLVMValueRef));
    bool all_const = true;
    for (int i = 0; i < count; i++)
    {
        elems[i] = codegen_expr(ctx, node->as.array_lit.elements[i]);
        if (elems[i] == NULL)
        {
            free(elems);
            return NULL;
        }
        if (!LLVMIsConstant(elems[i]))
            all_const = false;
    }

    if (all_const)
    {
        LLVMValueRef result = LLVMConstArray2(elem_llvm, elems, (uint64_t)count);
        free(elems);
        return result;
    }

    free(elems);
    return NULL;
}

static LLVMValueRef cg_expr_at_bench(CodegenContext *ctx, AstNode *node)
{
    LLVMTypeRef i64_t = LLVMInt64TypeInContext(ctx->context);
    LLVMTypeRef i32_t = LLVMInt32TypeInContext(ctx->context);
    LLVMTypeRef f64_t = LLVMDoubleTypeInContext(ctx->context);
    int iterations = node->as.at_bench.iterations;

    LLVMValueRef now_fn = cg_get_perf_now(ctx);
    LLVMTypeRef now_fn_ty = LLVMGlobalGetValueType(now_fn);

    /* total_ns alloca */
    LLVMBasicBlockRef entry_bb = LLVMGetEntryBasicBlock(ctx->current_fn);
    LLVMBuilderRef tmp_b = LLVMCreateBuilderInContext(ctx->context);
    LLVMValueRef first_inst = LLVMGetFirstInstruction(entry_bb);
    if (first_inst) LLVMPositionBuilderBefore(tmp_b, first_inst);
    else            LLVMPositionBuilderAtEnd(tmp_b, entry_bb);
    LLVMValueRef total_alloca = LLVMBuildAlloca(tmp_b, i64_t, "bench.total");
    LLVMValueRef i_alloca = LLVMBuildAlloca(tmp_b, i32_t, "bench.i");
    LLVMDisposeBuilder(tmp_b);

    /* total = 0; i = 0 */
    LLVMBuildStore(ctx->builder, LLVMConstInt(i64_t, 0, 0), total_alloca);
    LLVMBuildStore(ctx->builder, LLVMConstInt(i32_t, 0, 0), i_alloca);

    /* Loop: for (i = 0; i < N; i++) */
    LLVMBasicBlockRef cond_bb = LLVMAppendBasicBlockInContext(
        ctx->context, ctx->current_fn, "bench.cond");
    LLVMBasicBlockRef body_bb = LLVMAppendBasicBlockInContext(
        ctx->context, ctx->current_fn, "bench.body");
    LLVMBasicBlockRef done_bb = LLVMAppendBasicBlockInContext(
        ctx->context, ctx->current_fn, "bench.done");

    LLVMBuildBr(ctx->builder, cond_bb);

    /* cond: i < N */
    LLVMPositionBuilderAtEnd(ctx->builder, cond_bb);
    LLVMValueRef i_val = LLVMBuildLoad2(ctx->builder, i32_t, i_alloca, "bench.i.v");
    LLVMValueRef cmp = LLVMBuildICmp(ctx->builder, LLVMIntSLT, i_val,
                                      LLVMConstInt(i32_t, (unsigned long long)iterations, 0),
                                      "bench.cmp");
    LLVMBuildCondBr(ctx->builder, cmp, body_bb, done_bb);

    /* body: t0 = now(); expr; t1 = now(); total += (t1-t0); i++ */
    LLVMPositionBuilderAtEnd(ctx->builder, body_bb);
    LLVMValueRef t0 = LLVMBuildCall2(ctx->builder, now_fn_ty, now_fn,
                                      NULL, 0, "bench.t0");
    codegen_expr(ctx, node->as.at_bench.expr);
    LLVMValueRef t1 = LLVMBuildCall2(ctx->builder, now_fn_ty, now_fn,
                                      NULL, 0, "bench.t1");
    LLVMValueRef diff = LLVMBuildSub(ctx->builder, t1, t0, "bench.diff");
    LLVMValueRef old_total = LLVMBuildLoad2(ctx->builder, i64_t, total_alloca, "bench.old");
    LLVMValueRef new_total = LLVMBuildAdd(ctx->builder, old_total, diff, "bench.new");
    LLVMBuildStore(ctx->builder, new_total, total_alloca);
    LLVMValueRef i_next = LLVMBuildAdd(ctx->builder, i_val,
                                        LLVMConstInt(i32_t, 1, 0), "bench.i.next");
    LLVMBuildStore(ctx->builder, i_next, i_alloca);
    LLVMBuildBr(ctx->builder, cond_bb);

    /* done: mean_ns = (f64)total / (f64)N */
    LLVMPositionBuilderAtEnd(ctx->builder, done_bb);
    LLVMValueRef final_total = LLVMBuildLoad2(ctx->builder, i64_t, total_alloca, "bench.ft");
    LLVMValueRef total_f = LLVMBuildSIToFP(ctx->builder, final_total, f64_t, "bench.tf");
    LLVMValueRef n_f = LLVMConstReal(f64_t, (double)iterations);
    LLVMValueRef mean_ns = LLVMBuildFDiv(ctx->builder, total_f, n_f, "bench.mean");

    /* printf("[@bench] %.1f ns (N=%d)\n", mean_ns, N) */
    LLVMValueRef printf_fn = LLVMGetNamedFunction(ctx->module, "printf");
    if (printf_fn) {
        LLVMTypeRef printf_ty = LLVMGlobalGetValueType(printf_fn);
        LLVMValueRef fmt = LLVMBuildGlobalStringPtr(ctx->builder,
            "[@bench] mean %.1f ns (%d iterations)\n", "bench.fmt");
        LLVMValueRef pargs[3] = { fmt, mean_ns, LLVMConstInt(i32_t, (unsigned long long)iterations, 0) };
        LLVMBuildCall2(ctx->builder, printf_ty, printf_fn, pargs, 3, "");
    }

    return mean_ns;
}

static LLVMValueRef cg_expr_block(CodegenContext *ctx, AstNode *node)
{
    /* Block as expression: value is last expression */
    push_scope(ctx);
    CgScope *block_parent = ctx->current_scope->parent;
    LLVMValueRef last = NULL;
    AstNode *tail_expr = NULL;
    for (int i = 0; i < node->as.block.stmt_count; i++)
    {
        AstNode *s = node->as.block.stmts[i];
        if (i == node->as.block.stmt_count - 1 && s->kind == AST_EXPR_STMT)
        {
            tail_expr = s->as.expr_stmt.expr;
            last = codegen_expr(ctx, tail_expr);
        }
        else
        {
            codegen_stmt(ctx, s);
        }
    }
    /* A tail IDENT naming a has_drop local of THIS block would be dropped
       by the cleanup below while its loaded value escapes with `last` —
       the yielded value would alias freed heap (double-free at the
       consumer, memcheck-confirmed via a match arm `=> { Str b = ..; b }`).
       Transfer ownership out instead: skip the scope drop and hand the
       slot to the statement-level temp table — a value-consuming match
       arm transfers it into the result (cg_match_arm_encapsulate), a
       discarding consumer's statement-end flush drops it. An OUTER local
       resolves outside this block's scopes and stays untouched (its
       consumer clones, e.g. cg_match_arm_own_tail). */
    if (last && tail_expr && tail_expr->kind == AST_IDENT)
    {
        for (CgScope *sc = ctx->current_scope;
             sc != NULL && sc != block_parent; sc = sc->parent)
        {
            CgSymbol *ts = NULL;
            for (int si = sc->count - 1; si >= 0; si--)
            {
                if (sc->symbols[si].name &&
                    strcmp(sc->symbols[si].name,
                           tail_expr->as.ident.name) == 0)
                {
                    ts = &sc->symbols[si];
                    break;
                }
            }
            if (ts == NULL)
                continue;
            if (ts->no_drop_reason == CG_OWNED && !ts->is_mut_borrow && ts->value &&
                ts->type &&
                ((ts->type->kind == TYPE_STRUCT &&
                  ts->type->as.strukt.has_drop) ||
                 (ts->type->kind == TYPE_ENUM &&
                  ts->type->as.enom.has_drop) ||
                 /* Block local: the slot owns a heap closure env — same
                    transfer, or the scope cleanup frees the env the
                    yielded fat pointer still aliases (double-free). */
                 ts->type->kind == TYPE_BLOCK))
            {
                ts->no_drop_reason = CG_MOVED_OUT; /* skip drop: moved out */
                /* The slot is read again by the temp-table drop AFTER the
                   block ends — an end marker here would be premature. */
                ts->lifetime_marked = false;
                /* Bare cg_push_temp_drop, NOT cg_spill_owned_rvalue: the
                   local's slot already exists — this is a registration-only
                   ownership transfer, there is nothing to spill. */
                cg_push_temp_drop(ctx, ts->value, ts->type);
            }
            break; /* innermost (shadowing) symbol decides */
        }
    }
    /* Only clean up variables declared in THIS block, not outer scopes */
    emit_cleanup_to(ctx, block_parent, NULL);
    pop_scope(ctx);
    return last;
}

static LLVMValueRef cg_expr_cast(CodegenContext *ctx, AstNode *node)
{
    LLVMValueRef val = codegen_expr(ctx, node->as.cast.expr);
    if (val == NULL)
        return NULL;

    Type *from = node->as.cast.expr->resolved_type;
    Type *to = node->resolved_type;
    if (from == NULL || to == NULL)
        return val;

    LLVMTypeRef to_llvm = type_to_llvm(ctx, to);

    if (type_is_integer(from) && type_is_integer(to))
    {
        unsigned from_bits = LLVMGetIntTypeWidth(LLVMTypeOf(val));
        unsigned to_bits = LLVMGetIntTypeWidth(to_llvm);
        if (from_bits < to_bits)
        {
            if (type_is_signed(from))
                return LLVMBuildSExt(ctx->builder, val, to_llvm, "sext");
            return LLVMBuildZExt(ctx->builder, val, to_llvm, "zext");
        }
        else if (from_bits > to_bits)
        {
            return LLVMBuildTrunc(ctx->builder, val, to_llvm, "trunc");
        }
        return val;
    }
    if (type_is_integer(from) && type_is_float(to))
    {
        if (type_is_signed(from))
            return LLVMBuildSIToFP(ctx->builder, val, to_llvm, "sitofp");
        return LLVMBuildUIToFP(ctx->builder, val, to_llvm, "uitofp");
    }
    if (type_is_float(from) && type_is_integer(to))
    {
        if (type_is_signed(to))
            return LLVMBuildFPToSI(ctx->builder, val, to_llvm, "fptosi");
        return LLVMBuildFPToUI(ctx->builder, val, to_llvm, "fptoui");
    }
    if (type_is_float(from) && type_is_float(to))
    {
        /* Choose fpext (widen) vs fptrunc (narrow) by bit width; f16/bf16 are
           16-bit. f16<->bf16 (same width, different format) goes via f32. */
        int fb = from->kind==TYPE_F64?64 : from->kind==TYPE_F32?32 : 16;
        int tb = to->kind  ==TYPE_F64?64 : to->kind  ==TYPE_F32?32 : 16;
        if (tb > fb) return LLVMBuildFPExt(ctx->builder, val, to_llvm, "fpext");
        if (tb < fb) return LLVMBuildFPTrunc(ctx->builder, val, to_llvm, "fptrunc");
        /* Same bit width: identical type (f64->f64, f32->f32, f16->f16,
           bf16->bf16) is a no-op; only differing 16-bit formats
           (f16<->bf16) need a round-trip via f32. */
        if (from->kind == to->kind) return val;
        LLVMValueRef up = LLVMBuildFPExt(ctx->builder, val,
                              LLVMFloatTypeInContext(ctx->context), "fpext.up");
        return LLVMBuildFPTrunc(ctx->builder, up, to_llvm, "fptrunc.dn");
    }
    /* Pointer/object <-> integer casts */
    if ((from->kind == TYPE_POINTER || from->kind == TYPE_OBJECT) && type_is_integer(to))
    {
        return LLVMBuildPtrToInt(ctx->builder, val, to_llvm, "ptrtoint");
    }
    if (type_is_integer(from) && (to->kind == TYPE_POINTER || to->kind == TYPE_OBJECT))
    {
        return LLVMBuildIntToPtr(ctx->builder, val, to_llvm, "inttoptr");
    }
    /* Pointer/object <-> pointer/object casts (all opaque ptrs in LLVM) */
    return val;
}

static LLVMValueRef cg_expr_unary(CodegenContext *ctx, AstNode *node)
{
    /* Address-of must NOT evaluate the operand as a value (that would load
       the pointed-to element, not take its address). Handle it before the
       eager value-eval below: &ident is the alloca itself; any other lvalue
       — struct field, or a pointer/array element like &self.data[off] (used
       to get a *T base for a sub-block / view) — goes through
       codegen_lvalue_ptr, which handles pointer-index and &self/&!self
       reference auto-deref. */
    if (node->as.unary.op == TOKEN_AMP)
    {
        AstNode *opd = node->as.unary.operand;
        if (opd->kind == AST_IDENT)
        {
            CgSymbol *sym = cg_scope_resolve(ctx->current_scope, opd->as.ident.name);
            if (sym)
                return sym->value; /* alloca IS the address */
            cg_error(ctx, node->line, node->column, "cannot take address of expression");
            return NULL;
        }
        LLVMValueRef addr = codegen_lvalue_ptr(ctx, opd);
        if (addr == NULL)
        {
            cg_error(ctx, node->line, node->column, "cannot take address of expression");
            return NULL;
        }
        return addr;
    }

    LLVMValueRef operand = codegen_expr(ctx, node->as.unary.operand);
    if (operand == NULL)
        return NULL;

    switch (node->as.unary.op)
    {
    case TOKEN_MINUS:
        if (node->resolved_type && type_is_float(node->resolved_type))
            return LLVMBuildFNeg(ctx->builder, operand, "fneg");
        return LLVMBuildNeg(ctx->builder, operand, "neg");

    case TOKEN_BANG:
        return LLVMBuildNot(ctx->builder, operand, "not");

    case TOKEN_TILDE:
        return LLVMBuildNot(ctx->builder, operand, "bitnot");

    case TOKEN_STAR:
    {
        /* *ptr — dereference */
        Type *res = node->resolved_type;
        LLVMTypeRef load_type = res ? type_to_llvm(ctx, res)
                                    : LLVMInt32TypeInContext(ctx->context);
        return LLVMBuildLoad2(ctx->builder, load_type, operand, "deref");
    }

    default:
        cg_error(ctx, node->line, node->column, "unsupported unary operator");
        return NULL;
    }
}

LLVMValueRef codegen_expr(CodegenContext *ctx, AstNode *node)
{
    if (node == NULL)
        return NULL;

    /* Track which AST node we're currently lowering — helpers (clone, vec
       grow, scope cleanup …) read this for memcheck site labelling. We
       don't bother with save/restore: a fresh assignment runs on every
       codegen_expr call; the value is only meaningful at the moment a
       helper consumes it. */
    ctx->current_node = node;

    switch (node->kind)
    {
    case AST_INT_LIT:
    {
        /* Emit i64 when the checker typed this literal i64/u64 (value didn't fit
           i32); otherwise i32 as usual. Without this, a literal like 9000000000
           would be truncated to i32 here even in an i64 context. */
        Type *rt = node->resolved_type;
        LLVMTypeRef ity = (rt && (rt->kind == TYPE_I64 || rt->kind == TYPE_U64))
                              ? LLVMInt64TypeInContext(ctx->context)
                              : LLVMInt32TypeInContext(ctx->context);
        return LLVMConstInt(ity, (unsigned long long)node->as.int_lit.value, 1);
    }

    case AST_FLOAT_LIT:
        return LLVMConstReal(LLVMDoubleTypeInContext(ctx->context),
                             node->as.float_lit.value);

    case AST_BOOL_LIT:
        return LLVMConstInt(LLVMInt1TypeInContext(ctx->context),
                            node->as.bool_lit.value ? 1 : 0, 0);

    case AST_STRING_LIT:
        /* P5-4 S-3: every string literal is a static Str struct value. */
        if (node->resolved_type && node->resolved_type->kind == TYPE_STRUCT)
            return cg_str_struct_from_literal(ctx, node->as.string_lit.value,
                                              node->resolved_type);
        cg_error(ctx, node->line, node->column,
                 "internal: string literal not typed as Str");
        return NULL;

    case AST_NIL_LIT:
        return LLVMConstNull(LLVMPointerTypeInContext(ctx->context, 0));

    case AST_IDENT:
    {
        if (node->coerce_fn_to_block)
            return codegen_fn_to_block(ctx, node);

        /* Variant ctor with no payload (e.g. `Red`, `None`).  The checker
           has set resolved_type to the enum and validated the variant name. */
        if (node->resolved_type && node->resolved_type->kind == TYPE_ENUM)
        {
            Type *et = node->resolved_type;
            for (int v = 0; v < et->as.enom.variant_count; v++)
            {
                if (strcmp(et->as.enom.variants[v].name, node->as.ident.name) == 0)
                    return emit_enum_ctor(ctx, node, et, v, NULL, 0);
            }
        }

        CgSymbol *sym = cg_scope_resolve(ctx->current_scope, node->as.ident.name);
        if (sym == NULL)
        {
            /* Try as a function reference */
            LLVMValueRef fn = LLVMGetNamedFunction(ctx->module, node->as.ident.name);
            if (fn)
                return fn;
            /* Try a module-level global variable. A lifted closure body's scope
               does not chain to the global scope, so a global referenced inside
               a closure (now NOT captured — see capture_walk) resolves here.
               Arrays return the global pointer directly; scalars load. */
            LLVMValueRef gv = LLVMGetNamedGlobal(ctx->module, node->as.ident.name);
            if (gv)
            {
                Type *rt = node->resolved_type;
                if (rt && rt->kind == TYPE_ARRAY)
                    return gv;
                LLVMTypeRef gload = type_to_llvm(ctx, rt);
                return LLVMBuildLoad2(ctx->builder, gload, gv, node->as.ident.name);
            }
            cg_error(ctx, node->line, node->column,
                     "undefined variable '%s'", node->as.ident.name);
            return NULL;
        }
        /* Load from alloca */
        LLVMTypeRef load_type = type_to_llvm(ctx, sym->type);
        return LLVMBuildLoad2(ctx->builder, load_type, sym->value, node->as.ident.name);
    }

    case AST_MUT_BORROW:
    {
        /* Phase 5.5 Step 5: &!x ABI is a raw LsString* pointing at the caller's
           alloca. The checker guarantees operand is an IDENT bound to a local
           owned string (not a borrow, not moved, not static). So we just look
           up the symbol and hand its alloca address to the callee. The callee
           parameter is declared with psym->is_mut_borrow=true and
           no_drop_reason=CG_BORROWED so scope cleanup skips it — the caller retains ownership. */
        AstNode *op = node->as.mut_borrow.operand;
        if (op != NULL && op->kind == AST_FIELD)
        {
            /* &!base.field — writable borrow of a struct field. GEP the field
               address (same lvalue path field-assignment uses) and forward it
               to the Block(&!T) parameter exactly like the IDENT case. */
            return codegen_lvalue_ptr(ctx, op);
        }
        if (op == NULL || op->kind != AST_IDENT)
        {
            cg_error(ctx, node->line, node->column,
                     "&! operand must be an identifier or field access");
            return NULL;
        }
        CgSymbol *sym = cg_scope_resolve(ctx->current_scope, op->as.ident.name);
        if (sym == NULL)
        {
            cg_error(ctx, node->line, node->column,
                     "undefined variable '%s' in &!", op->as.ident.name);
            return NULL;
        }
        /* sym->value is already LsString* (either an alloca or, for a mut-borrow
           param forwarded further, the incoming pointer). */
        return sym->value;
    }

    case AST_UNARY:
        return cg_expr_unary(ctx, node);

    case AST_BINARY:
        return cg_expr_binary(ctx, node);

    case AST_FORMAT_STRING:
        return codegen_format_string(ctx, node);

    case AST_CLOSURE:
        return codegen_closure_literal(ctx, node);

    case AST_CALL:
        return cg_expr_call(ctx, node);

    case AST_FIELD:
        return cg_expr_field(ctx, node);

    case AST_MATCH:
        return codegen_match_expr(ctx, node);

    case AST_TRY:
        return codegen_try_expr(ctx, node);

    case AST_FORCE_UNWRAP:
    {
        LLVMValueRef fuw = codegen_force_unwrap_expr(ctx, node);
        /* P3 (block-refcount): `opt!` on Option/Result(Block) yields an OWNED
           Block — the container-get cloned the payload env. Track it so a
           discarded `v.get(i)!()` releases at flush and a binding claims it. */
        cg_track_block_rvalue(ctx, fuw, node->resolved_type);
        return fuw;
    }

    case AST_AT_TIME:
    {
        LLVMTypeRef f64_t = LLVMDoubleTypeInContext(ctx->context);

        LLVMValueRef now_fn = cg_get_perf_now(ctx);
        LLVMTypeRef now_fn_ty = LLVMGlobalGetValueType(now_fn);

        /* t0 = perf.now() */
        LLVMValueRef t0 = LLVMBuildCall2(ctx->builder, now_fn_ty, now_fn,
                                          NULL, 0, "time.t0");

        /* Evaluate the inner expression */
        LLVMValueRef expr_val = codegen_expr(ctx, node->as.at_time.expr);

        /* t1 = perf.now() */
        LLVMValueRef t1 = LLVMBuildCall2(ctx->builder, now_fn_ty, now_fn,
                                          NULL, 0, "time.t1");

        /* elapsed_ns = t1 - t0 */
        LLVMValueRef elapsed = LLVMBuildSub(ctx->builder, t1, t0, "time.elapsed");

        /* Convert to f64 milliseconds for display */
        LLVMValueRef elapsed_f = LLVMBuildSIToFP(ctx->builder, elapsed, f64_t, "time.ms.f");
        LLVMValueRef divisor = LLVMConstReal(f64_t, 1000000.0);
        LLVMValueRef ms_val = LLVMBuildFDiv(ctx->builder, elapsed_f, divisor, "time.ms");

        /* printf("[@time] %.3f ms\n", ms_val) */
        LLVMValueRef printf_fn = LLVMGetNamedFunction(ctx->module, "printf");
        if (printf_fn) {
            LLVMTypeRef printf_ty = LLVMGlobalGetValueType(printf_fn);
            LLVMValueRef fmt = LLVMBuildGlobalStringPtr(ctx->builder,
                                                        "[@time] %.3f ms\n", "time.fmt");
            LLVMValueRef pargs[2] = { fmt, ms_val };
            LLVMBuildCall2(ctx->builder, printf_ty, printf_fn, pargs, 2, "");
        }

        return expr_val;
    }

    case AST_AT_BENCH:
        return cg_expr_at_bench(ctx, node);

    case AST_CAST:
        return cg_expr_cast(ctx, node);

    case AST_SIZEOF:
    {
        /* sizeof(Type) -> i64 compile-time constant via LLVMSizeOf. The checker
           resolved the operand to a concrete type (type-param T already
           substituted per monomorphization). */
        Type *st = node->as.sizeof_expr.sized_type;
        if (st == NULL)
            return LLVMConstInt(LLVMInt64TypeInContext(ctx->context), 0, 0);
        LLVMTypeRef llt = type_to_llvm(ctx, st);
        return LLVMSizeOf(llt); /* i64 constant */
    }

    case AST_TYPENAME:
    {
        /* __type_name(Type) -> static Str. The checker resolved the operand to a
           concrete type (type-param T already substituted per monomorphization);
           emit its display name as a static (cap 0) Str from .rodata. */
        Type *nt = node->as.typename_expr.named_type;
        const char *nm = nt ? type_name(nt) : "void";
        Type *str_t = node->resolved_type;  /* the Str struct type (set by checker) */
        if (str_t == NULL) str_t = find_struct_ls_type(ctx, "Str");
        return cg_str_struct_from_literal(ctx, nm, str_t);
    }

    case AST_BLOCK:
        return cg_expr_block(ctx, node);

    case AST_FFI_CALL:
        return codegen_ffi_call(ctx, node);

    case AST_INDEX:
        return cg_expr_index(ctx, node);

    case AST_ARRAY_LIT:
        return cg_expr_array_lit(ctx, node);

    case AST_RANGE:
        /* Range expressions are not first-class values; handled by AST_FOR codegen */
        cg_error(ctx, node->line, node->column,
                 "range expression (a..b) can only be used in for-in loops");
        return NULL;

    case AST_NEW_EXPR:
        return cg_expr_new_expr(ctx, node);

    case AST_COMPTIME_FIELD:
        /* Leak guard: `v.(f)` is lowered to a concrete field access during comptime
           unroll (checker). Reaching codegen means the unroll pass was skipped. */
        cg_error(ctx, node->line, node->column,
                 "internal error: COMPTIME_FIELD survived to codegen (should be unrolled in checker)");
        return NULL;

    case AST_COMPTIME_BLOCK:
        /* Leak guard: a comptime block is folded to a constant by the checker's
           compile-time evaluator. Reaching codegen means evaluation was skipped. */
        cg_error(ctx, node->line, node->column,
                 "internal error: COMPTIME_BLOCK survived to codegen (should be evaluated in checker)");
        return NULL;

    default:
        cg_error(ctx, node->line, node->column,
                 "unsupported expression node: %s", ast_kind_name(node->kind));
        return NULL;
    }
}

static LLVMValueRef codegen_short_circuit(CodegenContext *ctx, AstNode *node)
{
    LLVMValueRef left = codegen_expr(ctx, node->as.binary.left);
    if (left == NULL)
        return NULL;

    LLVMBasicBlockRef rhs_bb = LLVMAppendBasicBlockInContext(
        ctx->context, ctx->current_fn, "sc.rhs");
    LLVMBasicBlockRef merge_bb = LLVMAppendBasicBlockInContext(
        ctx->context, ctx->current_fn, "sc.merge");
    LLVMBasicBlockRef entry_bb = LLVMGetInsertBlock(ctx->builder);

    if (node->as.binary.op == TOKEN_AND)
    {
        LLVMBuildCondBr(ctx->builder, left, rhs_bb, merge_bb);
    }
    else
    {
        LLVMBuildCondBr(ctx->builder, left, merge_bb, rhs_bb);
    }

    LLVMPositionBuilderAtEnd(ctx->builder, rhs_bb);
    /* BF-044: temps produced while evaluating the RHS (e.g. a spilled has_drop
       struct clone from `vec[i].field`) live in the conditionally-executed RHS
       block. They MUST be flushed here, inside the RHS block, before branching to
       merge — otherwise their drop is emitted at the enclosing statement boundary
       (a block not dominated by rhs_bb), giving "Instruction does not dominate all
       uses". The RHS result is a bool (i1), so it owns nothing; flushing all RHS
       temps is safe. LHS temps were created in the entry block (which dominates
       everything) and correctly flush at the outer statement boundary. */
    LLVMValueRef right = codegen_expr(ctx, node->as.binary.right);
    cg_flush_temps(ctx);
    LLVMBasicBlockRef rhs_end = LLVMGetInsertBlock(ctx->builder);
    LLVMBuildBr(ctx->builder, merge_bb);

    LLVMPositionBuilderAtEnd(ctx->builder, merge_bb);
    LLVMValueRef phi = LLVMBuildPhi(ctx->builder, LLVMInt1TypeInContext(ctx->context), "sc");
    LLVMValueRef incoming_vals[2] = {left, right};
    LLVMBasicBlockRef incoming_bbs[2] = {entry_bb, rhs_end};
    LLVMAddIncoming(phi, incoming_vals, incoming_bbs, 2);
    return phi;
}
