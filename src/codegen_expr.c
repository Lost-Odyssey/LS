/* codegen_expr.c
   表达式发射：codegen_expr switch 主体 + lvalue/addr + short-circuit + entry-alloca + slice + 数值 widen + Str helper + print/f-string + errno/from_cstr

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
static int append_text_escaped(char *dst, int len, int cap, const char *src);
static bool cg_build_spec_conv(CodegenContext *ctx, int line, int col, Type *et, const char *spec, char *out, size_t out_sz, bool *out_to_double);
static LLVMValueRef cg_entry_alloca_zeroed(CodegenContext *ctx, LLVMTypeRef ty, const char *name);
static LLVMValueRef cg_fstring_emit_arg(CodegenContext *ctx, AstNode *expr, LLVMValueRef val, const char *user_spec, char *fmt_buf, int *p_fmt_len, int fmt_cap);
static LLVMValueRef cg_make_slice(CodegenContext *ctx, LLVMTypeRef elem_llvm, LLVMValueRef base_ptr, LLVMValueRef start_i64, LLVMValueRef len_i64, Type *slice_type);
static void cg_print_str_value(CodegenContext *ctx, LLVMValueRef val);
static LLVMValueRef cg_str_struct_from_literal(CodegenContext *ctx, const char *text, Type *str_type);
static bool cg_type_is_str(Type *t);
static LLVMValueRef codegen_addr_of(CodegenContext *ctx, AstNode *node);
static LLVMValueRef codegen_errno_call(CodegenContext *ctx);
static LLVMValueRef codegen_format_string(CodegenContext *ctx, AstNode *node);
static LLVMValueRef codegen_from_cstr(CodegenContext *ctx, AstNode *node);
static void codegen_print_array(CodegenContext *ctx, AstNode *arg);
static void codegen_print_struct_value(CodegenContext *ctx, LLVMValueRef val, Type *t);

/* Accept both the @-sigil canonical spelling and the legacy __ spelling during
   the migration window (Phase 2 retires the legacy form). */
static bool cg_is_intrinsic(const char *name, const char *canon, const char *legacy) {
    return name != NULL &&
           (strcmp(name, canon) == 0 || strcmp(name, legacy) == 0);
}
static void codegen_print_enum_value(CodegenContext *ctx, LLVMValueRef val, Type *t);
static void cg_print_one_value(CodegenContext *ctx, LLVMValueRef fval, Type *ftype);
static LLVMValueRef codegen_short_circuit(CodegenContext *ctx, AstNode *node);
static LLVMValueRef emit_printf(CodegenContext *ctx, const char *fmt, LLVMValueRef *extra_args, int extra_count);
static const char *printf_fmt_for_type(Type *t);

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

/* ---- SIMD intrinsic helpers (Phase 1, docs/plan_simd.md) ---- */

/* Coerce a scalar to a target numeric element type (widen OR narrow):
   float<->float, int->float, float->int, int<->int. Used by __simd_splat so a
   literal (e.g. f64 3.0) broadcasts into a Simd of a different element type. */
static LLVMValueRef cg_simd_coerce(CodegenContext *ctx, LLVMValueRef v,
                                   Type *from, Type *to)
{
    if (v == NULL || from == NULL || to == NULL || type_equals(from, to)) return v;
    LLVMTypeRef tt = type_to_llvm(ctx, to);
    bool ff = type_is_float(from), tf = type_is_float(to);
    if (ff && tf) {
        int fb = from->kind == TYPE_F64 ? 64 : from->kind == TYPE_F32 ? 32 : 16;
        int tb = to->kind   == TYPE_F64 ? 64 : to->kind   == TYPE_F32 ? 32 : 16;
        if (tb > fb) return LLVMBuildFPExt(ctx->builder, v, tt, "simd.fpext");
        if (tb < fb) return LLVMBuildFPTrunc(ctx->builder, v, tt, "simd.fptrunc");
        return v;  /* same width (f16<->bf16 not reachable from splat scalars) */
    }
    if (!ff && tf)
        return type_is_unsigned(from)
            ? LLVMBuildUIToFP(ctx->builder, v, tt, "simd.uitofp")
            : LLVMBuildSIToFP(ctx->builder, v, tt, "simd.sitofp");
    if (ff && !tf)
        return type_is_unsigned(to)
            ? LLVMBuildFPToUI(ctx->builder, v, tt, "simd.fptoui")
            : LLVMBuildFPToSI(ctx->builder, v, tt, "simd.fptosi");
    int fb = type_int_bits(from), tb = type_int_bits(to);
    if (tb > fb) return type_is_unsigned(from)
        ? LLVMBuildZExt(ctx->builder, v, tt, "simd.zext")
        : LLVMBuildSExt(ctx->builder, v, tt, "simd.sext");
    if (tb < fb) return LLVMBuildTrunc(ctx->builder, v, tt, "simd.trunc");
    return v;  /* same width (e.g. signedness only) — value bits unchanged */
}

/* LLVM overloaded-intrinsic mangle for a Simd type: "v16f32"/"v8f64"/"v16i32". */
static void cg_simd_mangle(Type *simd, char *buf, size_t n)
{
    Type *e = simd->as.simd.elem;
    const char *ec;
    switch (e->kind) {
    case TYPE_F32: ec = "f32"; break;
    case TYPE_F64: ec = "f64"; break;
    case TYPE_F16: ec = "f16"; break;
    case TYPE_BF16: ec = "bf16"; break;
    case TYPE_I8: case TYPE_U8:  ec = "i8";  break;
    case TYPE_I16: case TYPE_U16: ec = "i16"; break;
    case TYPE_I64: case TYPE_U64: ec = "i64"; break;
    default: ec = "i32"; break;  /* int / i32 / u32 / char */
    }
    snprintf(buf, n, "v%d%s", simd->as.simd.lanes, ec);
}

/* Build a <lanes x i1> mask with the first n lanes set: icmp ult(iota, splat n).
   Hides the i1 vector from the surface — masked load/store take a lane count. */
static LLVMValueRef cg_simd_lane_mask(CodegenContext *ctx, LLVMValueRef n, unsigned lanes)
{
    LLVMTypeRef i32 = LLVMInt32TypeInContext(ctx->context);
    LLVMValueRef ncast = LLVMBuildIntCast2(ctx->builder, n, i32, 0, "simd.n32");
    LLVMValueRef *ic = malloc(sizeof(LLVMValueRef) * lanes);
    for (unsigned j = 0; j < lanes; j++) ic[j] = LLVMConstInt(i32, j, 0);
    LLVMValueRef iota = LLVMConstVector(ic, lanes);
    free(ic);
    LLVMTypeRef i32v = LLVMVectorType(i32, lanes);
    LLVMValueRef undef = LLVMGetUndef(i32v);
    LLVMValueRef ins = LLVMBuildInsertElement(ctx->builder, undef, ncast,
                           LLVMConstInt(i32, 0, 0), "n.ins");
    LLVMValueRef zmask = LLVMConstNull(LLVMVectorType(i32, lanes));
    LLVMValueRef nsplat = LLVMBuildShuffleVector(ctx->builder, ins, undef, zmask, "n.splat");
    return LLVMBuildICmp(ctx->builder, LLVMIntULT, iota, nsplat, "simd.mask");
}

/* Get-or-declare an LLVM intrinsic (or any external) by exact name + signature. */
static LLVMValueRef cg_get_or_declare(LLVMModuleRef mod, const char *name,
                                      LLVMTypeRef ret, LLVMTypeRef *params, unsigned np)
{
    LLVMValueRef fn = LLVMGetNamedFunction(mod, name);
    if (fn) return fn;
    return LLVMAddFunction(mod, name, LLVMFunctionType(ret, params, np, 0));
}

/* Allocate a stack slot in the CURRENT function's ENTRY block, regardless of
   where the builder currently sits. Bug #24/#26 family: a plain
   LLVMBuildAlloca at the current position, if that position is inside a loop
   body, allocates a fresh slot every iteration (LLVM allocas are only released
   on function return) → stack overflow. Entry-block allocas live once per call
   and are reused. Use this for any scratch slot created during expression /
   statement codegen (string method temps, loop indices, etc.). */
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
static LLVMValueRef cg_entry_alloca_zeroed(CodegenContext *ctx, LLVMTypeRef ty,
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

    char fl_name[256];
    snprintf(fl_name, sizeof(fl_name), "%s.__from_list",
             struct_llvm_name(struct_type));
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
static bool cg_type_is_str(Type *t)
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
static LLVMValueRef cg_make_str(CodegenContext *ctx, LLVMTypeRef st,
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
static LLVMValueRef cg_str_data(CodegenContext *ctx, LLVMValueRef str_val)
{
    return LLVMBuildExtractValue(ctx->builder, str_val, 0, "Str.d");
}
static LLVMValueRef cg_str_len(CodegenContext *ctx, LLVMValueRef str_val)
{
    return LLVMBuildExtractValue(ctx->builder, str_val, 1, "Str.l");
}

/* Build a static `Str` struct value {data, len, cap:0} for a string literal
   (docs/plan_string_to_stdlib.md §5.1, P1). `Str { *u8, int, int }` is layout-
   identical to LsString {i8*, i32, i32}; the bytes live in .rodata, so cap 0
   means Str.__drop skips free and Str.__clone shallow-copies. `str_type` is the
   concrete Str struct type the checker resolved (used for the LLVM struct type). */
static LLVMValueRef cg_str_struct_from_literal(CodegenContext *ctx,
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

/* Append src to dst[0..len), escaping '%' as '%%'.  cap is the buffer size.
   Returns the new length (may be clamped to cap-1 if buffer is full). */
static int append_text_escaped(char *dst, int len, int cap, const char *src)
{
    for (; *src; src++) {
        if (*src == '%') {
            if (len + 2 < cap) { dst[len++] = '%'; dst[len++] = '%'; }
        } else {
            if (len + 1 < cap) dst[len++] = *src;
        }
    }
    return len;
}

static const char *printf_fmt_for_type(Type *t)
{
    if (t == NULL)
        return "%p";
    switch (t->kind)
    {
    case TYPE_INT:
    case TYPE_I32:
        return "%d";
    case TYPE_I8:
        return "%d";
    case TYPE_I16:
        return "%d";
    case TYPE_I64:
        return "%lld";
    case TYPE_U8:
        return "%u";
    case TYPE_U16:
        return "%u";
    case TYPE_U32:
        return "%u";
    case TYPE_U64:
        return "%llu";
    case TYPE_F32:
        return "%f";
    case TYPE_F64:
        return "%f";
    case TYPE_F16:
    case TYPE_BF16:
        return "%f";
    case TYPE_BOOL:
        return "%s";
    case TYPE_POINTER:
        return "%p";
    case TYPE_OBJECT:
        return "%p";
    case TYPE_NIL:
        return "nil";
    default:
        return "%p";
    }
}

/* Translate a user f-string format specifier (e.g. ".2f", "03d", ".0f") into a
   printf conversion written to `out`. `*out_to_double` is set when the operand
   must be widened to double (int/f32 operand with a float conversion). Returns
   false (after reporting via cg_error) on an unsupported spec/type combination. */
static bool cg_build_spec_conv(CodegenContext *ctx, int line, int col, Type *et,
                               const char *spec, char *out, size_t out_sz,
                               bool *out_to_double)
{
    *out_to_double = false;
    size_t slen = strlen(spec);

    bool is_float_type = et && (et->kind == TYPE_F32 || et->kind == TYPE_F64);
    bool is_int_type = et && (et->kind == TYPE_INT || et->kind == TYPE_I8 ||
        et->kind == TYPE_I16 || et->kind == TYPE_I32 || et->kind == TYPE_I64 ||
        et->kind == TYPE_U8 || et->kind == TYPE_U16 || et->kind == TYPE_U32 ||
        et->kind == TYPE_U64);
    if (!is_float_type && !is_int_type) {
        cg_error(ctx, line, col, "f-string format specifier ':%s' requires a numeric value", spec);
        return false;
    }
    if (strchr(spec, '%') != NULL) {
        cg_error(ctx, line, col, "f-string format specifier ':%s' must not contain '%%'", spec);
        return false;
    }

    /* Trailing conversion char, if any. */
    char conv_char = 0;
    if (slen > 0 && strchr("fFeEgGdioxXu", spec[slen - 1]) != NULL)
        conv_char = spec[slen - 1];
    size_t body_len = conv_char ? slen - 1 : slen;

    /* Validate the body (flags/width/precision only). */
    for (size_t i = 0; i < body_len; i++) {
        char ch = spec[i];
        if (!(isdigit((unsigned char)ch) || ch == '.' || ch == '+' ||
              ch == '-' || ch == '#' || ch == ' ')) {
            cg_error(ctx, line, col, "invalid character in f-string format specifier ':%s'", spec);
            return false;
        }
    }

    bool want_float = conv_char ? (strchr("fFeEgG", conv_char) != NULL) : is_float_type;
    if (want_float) {
        char cc = conv_char ? conv_char : 'f';
        snprintf(out, out_sz, "%%%.*s%c", (int)body_len, spec, cc);
        if (is_int_type || (et && et->kind == TYPE_F32)) *out_to_double = true;
    } else {
        if (is_float_type) {
            cg_error(ctx, line, col, "integer format specifier ':%s' applied to a float value", spec);
            return false;
        }
        char cc = conv_char ? conv_char : 'd';
        bool is64 = et->kind == TYPE_I64 || et->kind == TYPE_U64;
        if (is64)
            snprintf(out, out_sz, "%%%.*sll%c", (int)body_len, spec, cc);
        else
            snprintf(out, out_sz, "%%%.*s%c", (int)body_len, spec, cc);
    }
    return true;
}

/* Shared by both f-string codegen paths (string-building and the print()
   fast path). Picks the printf conversion (default by type, or from `user_spec`),
   appends it to fmt_buf, and applies the matching value coercion. Returns the
   value to pass to sprintf/printf, or NULL on error. */
static LLVMValueRef cg_fstring_emit_arg(CodegenContext *ctx, AstNode *expr,
                                        LLVMValueRef val, const char *user_spec,
                                        char *fmt_buf, int *p_fmt_len, int fmt_cap)
{
    Type *et = expr->resolved_type;
    char conv[64];

    if (user_spec != NULL) {
        bool to_double = false;
        if (!cg_build_spec_conv(ctx, expr->line, expr->column, et, user_spec,
                                conv, sizeof(conv), &to_double))
            return NULL;
        if (to_double) {
            LLVMTypeRef dty = LLVMDoubleTypeInContext(ctx->context);
            if (et && et->kind == TYPE_F32)
                val = LLVMBuildFPExt(ctx->builder, val, dty, "fstr.f2d");
            else
                val = LLVMBuildSIToFP(ctx->builder, val, dty, "fstr.i2d");
        } else if (et && (et->kind == TYPE_I8 || et->kind == TYPE_I16)) {
            val = LLVMBuildSExt(ctx->builder, val, LLVMInt32TypeInContext(ctx->context), "sext");
        } else if (et && (et->kind == TYPE_U8 || et->kind == TYPE_U16)) {
            val = LLVMBuildZExt(ctx->builder, val, LLVMInt32TypeInContext(ctx->context), "zext");
        }
    } else {
        const char *d = printf_fmt_for_type(et);
        snprintf(conv, sizeof(conv), "%s", d);
        if (et && et->kind == TYPE_BOOL) {
            LLVMValueRef true_str = LLVMBuildGlobalStringPtr(ctx->builder, "true", "true");
            LLVMValueRef false_str = LLVMBuildGlobalStringPtr(ctx->builder, "false", "false");
            val = LLVMBuildSelect(ctx->builder, val, true_str, false_str, "boolstr");
        } else if (et && (et->kind == TYPE_I8 || et->kind == TYPE_I16)) {
            val = LLVMBuildSExt(ctx->builder, val, LLVMInt32TypeInContext(ctx->context), "sext");
        } else if (et && (et->kind == TYPE_U8 || et->kind == TYPE_U16)) {
            val = LLVMBuildZExt(ctx->builder, val, LLVMInt32TypeInContext(ctx->context), "zext");
        } else if (et && (et->kind == TYPE_F32 || et->kind == TYPE_F16 || et->kind == TYPE_BF16)) {
            /* C variadic default promotion: a float passed to printf becomes a
               double, and "%f" expects a double. Without this fpext, @print(f32)
               (and f-string {f32} with no spec) prints garbage. f16/bf16 likewise
               fpext (half/bfloat -> double) for printing. */
            val = LLVMBuildFPExt(ctx->builder, val,
                                 LLVMDoubleTypeInContext(ctx->context), "f2d");
        }
    }

    int slen = (int)strlen(conv);
    if (*p_fmt_len + slen < fmt_cap - 4) {
        memcpy(fmt_buf + *p_fmt_len, conv, (size_t)slen);
        *p_fmt_len += slen;
    }
    return val;
}

/* Helper: emit a print call with the given format string and args. Targets
   __ls_printf (vfprintf to the current sink stream) rather than printf, so
   set_sink redirects all print() output. Default stream is stdout, so output is
   byte-identical to the old direct-printf path. */
static LLVMValueRef emit_printf(CodegenContext *ctx, const char *fmt,
                                LLVMValueRef *extra_args, int extra_count)
{
    LLVMValueRef printf_fn = LLVMGetNamedFunction(ctx->module, "__ls_printf");
    LLVMTypeRef printf_type = LLVMGlobalGetValueType(printf_fn);

    int total = 1 + extra_count;
    LLVMValueRef *args = (LLVMValueRef *)malloc_safe((size_t)total * sizeof(LLVMValueRef));
    /* Dotted hint (".ls.fmt") keeps this internal printf format constant out of the
       user-identifier namespace — see cg_str_struct_from_literal for the full bug. */
    args[0] = LLVMBuildGlobalStringPtr(ctx->builder, fmt, ".ls.fmt");
    for (int i = 0; i < extra_count; i++)
    {
        args[1 + i] = extra_args[i];
    }
    LLVMValueRef result = LLVMBuildCall2(ctx->builder, printf_type, printf_fn,
                                         args, (unsigned)total, "");
    free(args);
    return result;
}

/* Helper: print a fixed-size array as [e0, e1, ...]\n */
static void codegen_print_array(CodegenContext *ctx, AstNode *arg)
{
    Type *arr_type = arg->resolved_type;
    if (arr_type == NULL || arr_type->kind != TYPE_ARRAY)
        return;

    int size = arr_type->as.array.size;
    Type *elem_type = arr_type->as.array.elem;
    const char *elem_fmt = printf_fmt_for_type(elem_type);

    /* Get array pointer */
    LLVMValueRef arr_ptr = NULL;
    if (arg->kind == AST_IDENT)
    {
        CgSymbol *sym = cg_scope_resolve(ctx->current_scope, arg->as.ident.name);
        if (sym)
            arr_ptr = sym->value;
    }
    if (arr_ptr == NULL)
        return;

    LLVMTypeRef arr_llvm = type_to_llvm(ctx, arr_type);
    LLVMTypeRef elem_llvm = type_to_llvm(ctx, elem_type);
    LLVMTypeRef i64_type = LLVMInt64TypeInContext(ctx->context);
    LLVMValueRef zero = LLVMConstInt(i64_type, 0, 0);

    /* Print "[" */
    emit_printf(ctx, "[", NULL, 0);

    for (int i = 0; i < size; i++)
    {
        if (i > 0)
            emit_printf(ctx, ", ", NULL, 0);

        LLVMValueRef idx = LLVMConstInt(i64_type, (uint64_t)i, 0);
        LLVMValueRef indices[2] = {zero, idx};
        LLVMValueRef gep = LLVMBuildGEP2(ctx->builder, arr_llvm, arr_ptr,
                                         indices, 2, "print.idx");
        LLVMValueRef val = LLVMBuildLoad2(ctx->builder, elem_llvm, gep, "print.elem");

        /* Bool: convert to "true"/"false" */
        if (elem_type->kind == TYPE_BOOL)
        {
            LLVMValueRef true_str = LLVMBuildGlobalStringPtr(ctx->builder, "true", "true");
            LLVMValueRef false_str = LLVMBuildGlobalStringPtr(ctx->builder, "false", "false");
            val = LLVMBuildSelect(ctx->builder, val, true_str, false_str, "boolstr");
        }
        /* Small ints: extend to i32 for printf */
        else if (elem_type->kind == TYPE_I8 || elem_type->kind == TYPE_I16)
        {
            val = LLVMBuildSExt(ctx->builder, val,
                                LLVMInt32TypeInContext(ctx->context), "sext");
        }
        else if (elem_type->kind == TYPE_U8 || elem_type->kind == TYPE_U16)
        {
            val = LLVMBuildZExt(ctx->builder, val,
                                LLVMInt32TypeInContext(ctx->context), "zext");
        }

        char elem_fmt_buf[32];
        snprintf(elem_fmt_buf, sizeof(elem_fmt_buf), "%s", elem_fmt);
        emit_printf(ctx, elem_fmt_buf, &val, 1);
    }

    /* Print "]\n" */
    emit_printf(ctx, "]\n", NULL, 0);
}

/* Helper: print a struct aggregate value as StructName{field=val, ...} (no trailing newline) */
/* P3 (docs/plan_string_to_stdlib.md §5.3): print a `Str` value as its raw text.
   Uses printf("%.*s", len, data) — length-bounded because a general Str buffer is
   NOT guaranteed NUL-terminated (from_string/__clone allocate exactly len bytes);
   only static-literal and f-string Strs happen to carry a NUL. `val` is the Str
   struct VALUE; field 0 = *u8 data, field 1 = int len. */
static void cg_print_str_value(CodegenContext *ctx, LLVMValueRef val)
{
    LLVMValueRef data = cg_str_data(ctx, val);
    LLVMValueRef len  = cg_str_len(ctx, val);
    LLVMValueRef args[2] = { len, data };
    emit_printf(ctx, "%.*s", args, 2);
}

/* Print one value of any type, used for struct fields and enum payload fields.
   Str → quoted text; nested struct/enum → recurse; bool → true/false; small
   ints widened to the printf spec; everything else via printf_fmt_for_type. */
static void cg_print_one_value(CodegenContext *ctx, LLVMValueRef fval, Type *ftype)
{
    if (cg_type_is_str(ftype))
    {
        /* D-1: a Str is a struct, but printing it as `Str{data=ptr,len,cap}`
           leaks a pointer (non-deterministic). Render its text, quoted. */
        emit_printf(ctx, "\"", NULL, 0);
        cg_print_str_value(ctx, fval);
        emit_printf(ctx, "\"", NULL, 0);
        return;
    }
    if (ftype->kind == TYPE_STRUCT)
    {
        codegen_print_struct_value(ctx, fval, ftype);
        return;
    }
    if (ftype->kind == TYPE_ENUM)
    {
        codegen_print_enum_value(ctx, fval, ftype);
        return;
    }
    if (ftype->kind == TYPE_BOOL)
    {
        LLVMValueRef true_str = LLVMBuildGlobalStringPtr(ctx->builder, "true", "true");
        LLVMValueRef false_str = LLVMBuildGlobalStringPtr(ctx->builder, "false", "false");
        fval = LLVMBuildSelect(ctx->builder, fval, true_str, false_str, "boolstr");
    }
    else if (ftype->kind == TYPE_I8 || ftype->kind == TYPE_I16)
    {
        fval = LLVMBuildSExt(ctx->builder, fval,
                             LLVMInt32TypeInContext(ctx->context), "sext");
    }
    else if (ftype->kind == TYPE_U8 || ftype->kind == TYPE_U16)
    {
        fval = LLVMBuildZExt(ctx->builder, fval,
                             LLVMInt32TypeInContext(ctx->context), "zext");
    }
    else if (ftype->kind == TYPE_F32 || ftype->kind == TYPE_F16 || ftype->kind == TYPE_BF16)
    {
        /* C variadic default argument promotion: a float passed to printf is
           promoted to double. The "%f" spec expects a double, so emit the fpext
           explicitly (LLVM does not auto-promote a float vararg). f16/bf16 fpext
           (half/bfloat -> double) too. */
        fval = LLVMBuildFPExt(ctx->builder, fval,
                              LLVMDoubleTypeInContext(ctx->context), "f32.promote");
    }
    const char *spec = printf_fmt_for_type(ftype);
    emit_printf(ctx, spec, &fval, 1);
}

static void codegen_print_struct_value(CodegenContext *ctx, LLVMValueRef val, Type *t)
{
    char open_buf[256];
    snprintf(open_buf, sizeof(open_buf), "%s{",
             (t->as.strukt.name ? t->as.strukt.name : "struct"));
    emit_printf(ctx, open_buf, NULL, 0);

    for (int i = 0; i < t->as.strukt.field_count; i++)
    {
        if (i > 0)
            emit_printf(ctx, ", ", NULL, 0);

        const char *fname = t->as.strukt.fields[i].name;
        Type *ftype = t->as.strukt.fields[i].type;

        char field_buf[256];
        snprintf(field_buf, sizeof(field_buf), "%s=", fname);
        emit_printf(ctx, field_buf, NULL, 0);

        LLVMValueRef fval = LLVMBuildExtractValue(ctx->builder, val, (unsigned)i, "sf");
        cg_print_one_value(ctx, fval, ftype);
    }

    emit_printf(ctx, "}", NULL, 0);
}

/* Print an enum value as `Variant` or `Variant(payload0, payload1, …)` —
   switching on the discriminant. Mirrors the match codegen's payload GEP
   (build_variant_payload_struct overlay on the payload slot). Without this,
   print(an enum) fell through to printf_fmt_for_type and rendered the raw
   discriminant/payload bytes (e.g. Option(Str) → `0000000000000001`). */
static void codegen_print_enum_value(CodegenContext *ctx, LLVMValueRef val, Type *t)
{
    LLVMTypeRef enum_llvm = type_to_llvm(ctx, t);
    LLVMTypeRef i8 = LLVMInt8TypeInContext(ctx->context);
    LLVMTypeRef ptr_type = LLVMPointerTypeInContext(ctx->context, 0);

    /* Spill to an alloca so we can GEP into the payload slot. */
    LLVMValueRef ea = cg_entry_alloca(ctx, enum_llvm, "print.enum");
    LLVMBuildStore(ctx->builder, val, ea);

    LLVMValueRef disc_ptr = LLVMBuildStructGEP2(ctx->builder, enum_llvm, ea, 0, "pe.disc.p");
    LLVMValueRef disc = LLVMBuildLoad2(ctx->builder, i8, disc_ptr, "pe.disc");
    LLVMValueRef payload_ptr = LLVMBuildStructGEP2(ctx->builder, enum_llvm, ea, 1, "pe.payload.p");

    LLVMBasicBlockRef merge_bb = LLVMAppendBasicBlockInContext(
        ctx->context, ctx->current_fn, "pe.end");
    LLVMBasicBlockRef default_bb = LLVMAppendBasicBlockInContext(
        ctx->context, ctx->current_fn, "pe.default");

    int vc = t->as.enom.variant_count;
    LLVMValueRef sw = LLVMBuildSwitch(ctx->builder, disc, default_bb, (unsigned)vc);

    for (int v = 0; v < vc; v++)
    {
        LLVMBasicBlockRef bb = LLVMAppendBasicBlockInContext(
            ctx->context, ctx->current_fn, "pe.case");
        LLVMAddCase(sw, LLVMConstInt(i8, (unsigned long long)v, 0), bb);
        LLVMPositionBuilderAtEnd(ctx->builder, bb);

        const char *vname = t->as.enom.variants[v].name
                                ? t->as.enom.variants[v].name : "?";
        emit_printf(ctx, vname, NULL, 0);

        int pc = t->as.enom.variants[v].payload_count;
        if (pc > 0)
        {
            emit_printf(ctx, "(", NULL, 0);
            LLVMTypeRef variant_struct = build_variant_payload_struct(ctx, t, v);
            for (int f = 0; f < pc; f++)
            {
                if (f > 0) emit_printf(ctx, ", ", NULL, 0);
                Type *pt = t->as.enom.variants[v].payload_types[f];
                LLVMTypeRef pllvm = type_to_llvm(ctx, pt);
                LLVMValueRef fp = LLVMBuildStructGEP2(ctx->builder, variant_struct,
                                                      payload_ptr, (unsigned)f, "pe.fld.p");
                LLVMValueRef fv;
                if (pt == t)
                {
                    /* Self-recursive payload: the slot holds an i8* box pointer. */
                    LLVMValueRef box = LLVMBuildLoad2(ctx->builder, ptr_type, fp, "pe.box");
                    fv = LLVMBuildLoad2(ctx->builder, pllvm, box, "pe.boxval");
                }
                else
                {
                    fv = LLVMBuildLoad2(ctx->builder, pllvm, fp, "pe.fld");
                }
                cg_print_one_value(ctx, fv, pt);
            }
            emit_printf(ctx, ")", NULL, 0);
        }
        LLVMBuildBr(ctx->builder, merge_bb);
    }

    LLVMPositionBuilderAtEnd(ctx->builder, default_bb);
    emit_printf(ctx, "<?>", NULL, 0);
    LLVMBuildBr(ctx->builder, merge_bb);

    LLVMPositionBuilderAtEnd(ctx->builder, merge_bb);
}

/* Codegen for print() with any type — generates inline printf */
LLVMValueRef codegen_print_call(CodegenContext *ctx, AstNode *node)
{
    int argc = node->as.call.arg_count;

    /* Build format string and args based on each argument's resolved type */
    char fmt_buf[1024];
    int fmt_len = 0;

    /* Pre-scan to compute the actual number of printf slots needed.
       An f-string argument expands to expr_count individual slots, not 1.
       Allocating only argc*2 is wrong when a single f-string has more than
       2 interpolated expressions — BF-035 (buffer overflow on Linux). */
    int max_printf_args = 0;
    for (int i = 0; i < argc; i++)
    {
        AstNode *arg = node->as.call.args[i];
        if (arg->kind == AST_FORMAT_STRING)
            max_printf_args += arg->as.format_string.expr_count * 2; /* Str -> 2 slots */
        else
            max_printf_args += 2; /* +1 margin for bool select */
    }
    if (max_printf_args < 1) max_printf_args = 1;

    LLVMValueRef *printf_args = (LLVMValueRef *)malloc_safe(
        (size_t)max_printf_args * sizeof(LLVMValueRef));
    int printf_argc = 0;

    for (int i = 0; i < argc; i++)
    {
        AstNode *arg = node->as.call.args[i];

        /* If the argument is an f-string, handle it inline by expanding into the format */
        if (arg->kind == AST_FORMAT_STRING)
        {
            /* Expand format string parts into the printf format */
            for (int j = 0; j < arg->as.format_string.expr_count; j++)
            {
                /* Text part before expression — escape '%' for printf */
                const char *txt = arg->as.format_string.parts[j];
                fmt_len = append_text_escaped(fmt_buf, fmt_len, 1024, txt);

                AstNode *expr = arg->as.format_string.exprs[j];
                const char *uspec = arg->as.format_string.specs
                                        ? arg->as.format_string.specs[j] : NULL;
                /* Str interpolation: "%.*s" with (len, data). Use the VALUE form
                   (not _or_borrow, which could hand back a pointer). */
                if (cg_type_is_str(expr->resolved_type) && uspec == NULL)
                {
                    LLVMValueRef sval = codegen_expr(ctx, expr);
                    if (sval == NULL) { free(printf_args); return NULL; }
                    if (fmt_len + 4 < 1024)
                    {
                        memcpy(fmt_buf + fmt_len, "%.*s", 4);
                        fmt_len += 4;
                    }
                    printf_args[printf_argc++] = cg_str_len(ctx, sval);
                    printf_args[printf_argc++] = cg_str_data(ctx, sval);
                    /* Owned Str rvalue interpolated → register for drop. Besides
                       call/index clones this covers FIELD reads (a terminal
                       has_drop field read clones, e.g. f"{e.color}") and lowered
                       operator chains (f"{a + b}"). Bare ident stays a borrow. */
                    if (expr->kind == AST_CALL || expr->kind == AST_INDEX ||
                        expr->kind == AST_FIELD ||
                        (expr->kind == AST_BINARY && expr->as.binary.lowered != NULL))
                    {
                        LLVMValueRef stmp = cg_entry_alloca(
                            ctx, type_to_llvm(ctx, expr->resolved_type), "fstr.str.drop");
                        LLVMBuildStore(ctx->builder, sval, stmp);
                        cg_push_temp_drop(ctx, stmp, expr->resolved_type);
                    }
                    continue;
                }
                LLVMValueRef val = codegen_expr_or_borrow(ctx, expr);
                if (val == NULL)
                {
                    free(printf_args);
                    return NULL;
                }
                val = cg_fstring_emit_arg(ctx, expr, val, uspec, fmt_buf, &fmt_len, 1024);
                if (val == NULL)
                {
                    free(printf_args);
                    return NULL;
                }
                printf_args[printf_argc++] = val;
            }
            /* Trailing text part — escape '%' for printf */
            if (arg->as.format_string.part_count > arg->as.format_string.expr_count)
            {
                const char *txt = arg->as.format_string.parts[arg->as.format_string.expr_count];
                fmt_len = append_text_escaped(fmt_buf, fmt_len, 1024, txt);
            }
            continue;
        }

        Type *t = arg->resolved_type;

        /* Array: print via special handler */
        if (t && t->kind == TYPE_ARRAY)
        {
            /* Flush current format buffer first */
            if (fmt_len > 0)
            {
                fmt_buf[fmt_len] = '\0';
                emit_printf(ctx, fmt_buf, printf_args, printf_argc);
                fmt_len = 0;
                printf_argc = 0;
            }
            codegen_print_array(ctx, arg);
            continue;
        }

        /* Str value: print its raw text (not the StructName{...} dump). P3. */
        if (t && t->kind == TYPE_STRUCT && t->as.strukt.name &&
            strcmp(t->as.strukt.name, "Str") == 0)
        {
            if (fmt_len > 0)
            {
                fmt_buf[fmt_len] = '\0';
                emit_printf(ctx, fmt_buf, printf_args, printf_argc);
                fmt_len = 0;
                printf_argc = 0;
            }
            if (i > 0)
                emit_printf(ctx, " ", NULL, 0);
            LLVMValueRef sval = codegen_expr(ctx, arg);
            if (sval == NULL) { free(printf_args); return NULL; }
            cg_print_str_value(ctx, sval);
            /* Owned Str rvalue consumed by print → drop it (F3 analog).
               Besides index/call clones this covers terminal FIELD reads
               (a has_drop field read clones, e.g. print(x.first)) and
               lowered operator chains (print(a + b)) — the same whitelist
               as the f-string interpolation site above; a static-Str clone
               allocates nothing, which masked the field-read leak until an
               owned field was printed. Bare ident stays a borrow: skip. */
            if (arg->kind == AST_INDEX || arg->kind == AST_CALL ||
                arg->kind == AST_FIELD ||
                cg_is_owned_combinator_rvalue(arg) ||
                (arg->kind == AST_BINARY && arg->as.binary.lowered != NULL))
            {
                LLVMValueRef stmp = cg_entry_alloca(ctx, type_to_llvm(ctx, t),
                                                    "print.str.drop");
                LLVMBuildStore(ctx->builder, sval, stmp);
                emit_drop_value(ctx, stmp, t);
            }
            continue;
        }

        /* Struct value: print as StructName{field=val, ...} */
        if (t && t->kind == TYPE_STRUCT)
        {
            if (fmt_len > 0)
            {
                fmt_buf[fmt_len] = '\0';
                emit_printf(ctx, fmt_buf, printf_args, printf_argc);
                fmt_len = 0;
                printf_argc = 0;
            }
            if (i > 0)
                emit_printf(ctx, " ", NULL, 0);
            LLVMValueRef sval = codegen_expr(ctx, arg);
            if (sval == NULL)
            {
                free(printf_args);
                return NULL;
            }
            codegen_print_struct_value(ctx, sval, t);
            /* F3 (VR-LIM-008): an owned has_drop struct rvalue passed to print —
               e.g. `print(vp[0])`, where `Vec(T).get`/`__index` deep-clones the
               element — is fully consumed here and bound to nothing, so its owned
               fields (strings/vecs/…) leak. Drop the clone. Restricted to
               owned-rvalue producers (index / call); a bare ident or field read
               of a LIVE binding must NOT be dropped (it's a borrow — dropping
               would corrupt/double-free the source). */
            if (t->as.strukt.has_drop &&
                (arg->kind == AST_INDEX || arg->kind == AST_CALL ||
                 cg_is_owned_combinator_rvalue(arg)))
            {
                LLVMValueRef stmp = cg_entry_alloca(ctx, type_to_llvm(ctx, t),
                                                    "print.drop");
                LLVMBuildStore(ctx->builder, sval, stmp);
                emit_drop_value(ctx, stmp, t);
            }
            continue;
        }

        /* Enum value: print as `Variant` / `Variant(payload, …)` (incl. Option /
           Result). Without this it fell through to printf_fmt_for_type and printed
           the raw discriminant/payload bytes. Drop an owned has_drop enum rvalue
           (same whitelist as the struct branch) so its payload is not leaked. */
        if (t && t->kind == TYPE_ENUM)
        {
            if (fmt_len > 0)
            {
                fmt_buf[fmt_len] = '\0';
                emit_printf(ctx, fmt_buf, printf_args, printf_argc);
                fmt_len = 0;
                printf_argc = 0;
            }
            if (i > 0)
                emit_printf(ctx, " ", NULL, 0);
            LLVMValueRef eval = codegen_expr(ctx, arg);
            if (eval == NULL)
            {
                free(printf_args);
                return NULL;
            }
            codegen_print_enum_value(ctx, eval, t);
            if (t->as.enom.has_drop &&
                (arg->kind == AST_INDEX || arg->kind == AST_CALL ||
                 cg_is_owned_combinator_rvalue(arg)))
            {
                LLVMValueRef etmp = cg_entry_alloca(ctx, type_to_llvm(ctx, t),
                                                    "print.enum.drop");
                LLVMBuildStore(ctx->builder, eval, etmp);
                emit_drop_value(ctx, etmp, t);
            }
            continue;
        }

        LLVMValueRef val = codegen_expr_or_borrow(ctx, arg);
        if (val == NULL)
        {
            free(printf_args);
            return NULL;
        }

        /* Dynamic string temps (upper/lower/concat/f-string/…) are already
           statement end — no separate __argtmp scope registration needed. */

        if (i > 0)
        {
            /* Space separator between multiple args */
            if (fmt_len < 1020)
                fmt_buf[fmt_len++] = ' ';
        }

        const char *spec = printf_fmt_for_type(t);
        int slen = (int)strlen(spec);
        if (fmt_len + slen < 1020)
        {
            memcpy(fmt_buf + fmt_len, spec, (size_t)slen);
            fmt_len += slen;
        }

        /* Bool: convert i1 to "true"/"false" */
        if (t && t->kind == TYPE_BOOL)
        {
            LLVMValueRef true_str = LLVMBuildGlobalStringPtr(ctx->builder, "true", "true");
            LLVMValueRef false_str = LLVMBuildGlobalStringPtr(ctx->builder, "false", "false");
            val = LLVMBuildSelect(ctx->builder, val, true_str, false_str, "boolstr");
        }
        /* Small ints: extend to i32 for printf */
        else if (t && (t->kind == TYPE_I8 || t->kind == TYPE_I16))
        {
            val = LLVMBuildSExt(ctx->builder, val,
                                LLVMInt32TypeInContext(ctx->context), "sext");
        }
        else if (t && (t->kind == TYPE_U8 || t->kind == TYPE_U16))
        {
            val = LLVMBuildZExt(ctx->builder, val,
                                LLVMInt32TypeInContext(ctx->context), "zext");
        }
        /* f32: C variadic default promotion to double ("%f" expects a double).
           LLVM does not auto-promote a float vararg, so @print(f32) printed
           garbage without this explicit fpext. */
        else if (t && (t->kind == TYPE_F32 || t->kind == TYPE_F16 || t->kind == TYPE_BF16))
        {
            val = LLVMBuildFPExt(ctx->builder, val,
                                 LLVMDoubleTypeInContext(ctx->context), "f2d");
        }

        printf_args[printf_argc++] = val;
    }

    /* Append newline */
    if (fmt_len < 1022)
        fmt_buf[fmt_len++] = '\n';
    fmt_buf[fmt_len] = '\0';

    LLVMValueRef result = emit_printf(ctx, fmt_buf, printf_args, printf_argc);
    free(printf_args);
    return result;
}

/* Codegen for f"..." format string — produces an LsString via snprintf+malloc */
static LLVMValueRef codegen_format_string(CodegenContext *ctx, AstNode *node)
{
    /* Build printf format and args, then use snprintf to create the string */
    int expr_count = node->as.format_string.expr_count;
    int part_count = node->as.format_string.part_count;

    /* Build format string */
    char fmt_buf[1024];
    int fmt_len = 0;

    /* A `Str` interpolation needs TWO varargs ("%.*s" -> len, data), so size for
       up to 2 slots per expr. */
    LLVMValueRef *vals = (LLVMValueRef *)malloc_safe(
        (size_t)(expr_count * 2 + 1) * sizeof(LLVMValueRef));
    int val_count = 0;

    for (int i = 0; i < expr_count; i++)
    {
        /* Text part — escape '%' so it is not treated as printf format specifier */
        const char *txt = node->as.format_string.parts[i];
        fmt_len = append_text_escaped(fmt_buf, fmt_len, 1024, txt);

        /* Expression */
        AstNode *expr = node->as.format_string.exprs[i];
        const char *uspec = node->as.format_string.specs
                                ? node->as.format_string.specs[i] : NULL;
        LLVMValueRef val = codegen_expr(ctx, expr);
        if (val == NULL)
        {
            free(vals);
            return NULL;
        }
        /* Str interpolation: "%.*s" with (len, data) — length-bounded since a Str
           buffer is not guaranteed NUL-terminated. (Format specs on Str unsupported.) */
        if (cg_type_is_str(expr->resolved_type) && uspec == NULL)
        {
            if (fmt_len + 4 < 1024)
            {
                memcpy(fmt_buf + fmt_len, "%.*s", 4);
                fmt_len += 4;
            }
            vals[val_count++] = cg_str_len(ctx, val);
            vals[val_count++] = cg_str_data(ctx, val);
            /* Owned Str rvalue interpolated → drop after the result is built
               (statement-end flush runs after the snprintf below). Besides
               call/index clones this covers FIELD reads (a terminal has_drop
               field read CLONES — f"{e.color}" leaked one per evaluation) and
               lowered operator chains (f"{a + b}"). Bare ident is a borrow. */
            if (expr->kind == AST_CALL || expr->kind == AST_INDEX ||
                expr->kind == AST_FIELD ||
                cg_is_owned_combinator_rvalue(expr) ||
                (expr->kind == AST_BINARY && expr->as.binary.lowered != NULL))
            {
                LLVMValueRef stmp = cg_entry_alloca(
                    ctx, type_to_llvm(ctx, expr->resolved_type), "fstr.str.drop");
                LLVMBuildStore(ctx->builder, val, stmp);
                cg_push_temp_drop(ctx, stmp, expr->resolved_type);
            }
            continue;
        }
        val = cg_fstring_emit_arg(ctx, expr, val, uspec, fmt_buf, &fmt_len, 1024);
        if (val == NULL)
        {
            free(vals);
            return NULL;
        }

        vals[val_count++] = val;
    }

    /* Trailing text — escape '%' for printf/sprintf */
    if (part_count > expr_count)
    {
        const char *txt = node->as.format_string.parts[expr_count];
        fmt_len = append_text_escaped(fmt_buf, fmt_len, 1024, txt);
    }
    fmt_buf[fmt_len] = '\0';

    /* If no expressions, return the raw text as a static LsString. Use the
       unescaped parts[0] (not fmt_buf, which has '%' doubled to '%%' for the
       sprintf path) — otherwise a literal f-string like f"100%" would yield
       "100%%". */
    if (expr_count == 0)
    {
        const char *lit = (part_count > 0) ? node->as.format_string.parts[0] : "";
        free(vals);
        /* P5-4 S-3: a no-interpolation f-string is a static literal Str (cap 0). */
        if (node->resolved_type && node->resolved_type->kind == TYPE_STRUCT)
            return cg_str_struct_from_literal(ctx, lit, node->resolved_type);
        cg_error(ctx, node->line, node->column,
                 "internal: f-string not typed as Str");
        return NULL;
    }

    /* Format into a small reused entry-block stack scratch buffer, then copy out
       exactly len+1 bytes onto the heap. snprintf is *bounded* (never overflows
       the scratch) and returns the FULL length the result needs even when it had
       to truncate — so a runtime check takes the fast path (fits in scratch →
       memcpy) or, rarely, reformats straight into an exact-size heap buffer. This
       replaces the old design's fixed 4096-byte (page-sized) heap buffer per
       f-string (cap=4096): when the result outlived the call (e.g. pushed into a
       vec) each one touched a fresh page → ~1 minor page fault apiece (~1.4us).
       Exact sizing lets hundreds of small strings share a page, and there is no
       longer any hard length cap. See benchmarks/alloc/alloc_analysis.md. */
    enum { LS_FSTR_SCRATCH = 256 };
    LLVMTypeRef ptr_t = LLVMPointerTypeInContext(ctx->context, 0);
    LLVMTypeRef i32_t = LLVMInt32TypeInContext(ctx->context);
    LLVMTypeRef i64_t = LLVMInt64TypeInContext(ctx->context);
    LLVMTypeRef i8_t  = LLVMInt8TypeInContext(ctx->context);

    /* Bounded formatter: int __ls_fstr_format(char*, size_t, const char*, ...).
       A runtime wrapper around vsnprintf — see runtime/builtins.c. (snprintf
       itself has no JIT-resolvable symbol on Windows/UCRT, hence the wrapper.) */
    LLVMValueRef snprintf_fn = LLVMGetNamedFunction(ctx->module, "__ls_fstr_format");
    if (snprintf_fn == NULL)
    {
        LLVMTypeRef sp_params[] = {ptr_t, i64_t, ptr_t};
        LLVMTypeRef sp_type0 = LLVMFunctionType(i32_t, sp_params, 3, 1);
        snprintf_fn = LLVMAddFunction(ctx->module, "__ls_fstr_format", sp_type0);
    }
    LLVMTypeRef sp_type = LLVMGlobalGetValueType(snprintf_fn);

    LLVMValueRef fmt_str = LLVMBuildGlobalStringPtr(ctx->builder, fmt_buf, "fstr.fmt");

    /* Small reusable scratch buffer in the function entry block. Constant-size
       alloca → reserved once in the frame, reused across loop iterations (no
       per-iteration stack growth). Only emitted because we are compiling an
       f-string; f-string-free functions get none. */
    LLVMValueRef cur_fn = LLVMGetBasicBlockParent(LLVMGetInsertBlock(ctx->builder));
    LLVMBuilderRef entry_b = LLVMCreateBuilderInContext(ctx->context);
    LLVMBasicBlockRef entry_bb = LLVMGetEntryBasicBlock(cur_fn);
    LLVMValueRef first_instr = LLVMGetFirstInstruction(entry_bb);
    if (first_instr)
        LLVMPositionBuilderBefore(entry_b, first_instr);
    else
        LLVMPositionBuilderAtEnd(entry_b, entry_bb);
    LLVMValueRef tmp_buf = LLVMBuildArrayAlloca(
        entry_b, i8_t, LLVMConstInt(i32_t, LS_FSTR_SCRATCH, 0), "fstr.tmp");
    LLVMDisposeBuilder(entry_b);

    /* n = snprintf(tmp, 256, fmt, vals...) — bounded; returns full needed length. */
    int spn = 3 + val_count;
    LLVMValueRef *sp_args = (LLVMValueRef *)malloc_safe((size_t)spn * sizeof(LLVMValueRef));
    sp_args[0] = tmp_buf;
    sp_args[1] = LLVMConstInt(i64_t, LS_FSTR_SCRATCH, 0);
    sp_args[2] = fmt_str;
    for (int i = 0; i < val_count; i++)
        sp_args[3 + i] = vals[i];
    LLVMValueRef n = LLVMBuildCall2(ctx->builder, sp_type, snprintf_fn,
                                    sp_args, (unsigned)spn, "fstr.n");

    /* cap = n+1; buf = malloc(cap). */
    LLVMValueRef cap = LLVMBuildAdd(ctx->builder, n, LLVMConstInt(i32_t, 1, 0), "fstr.cap");
    LLVMValueRef cap64 = LLVMBuildZExt(ctx->builder, cap, i64_t, "fstr.cap64");
    LLVMValueRef buf = cg_emit_alloc(ctx, cap64, "string.fstring",
                                     node->line, node->column);

    /* if (n < 256) the scratch already holds the full result → memcpy it out;
       else it was truncated → reformat straight into the exact heap buffer. */
    LLVMValueRef fits = LLVMBuildICmp(ctx->builder, LLVMIntULT, n,
                                      LLVMConstInt(i32_t, LS_FSTR_SCRATCH, 0), "fstr.fits");
    LLVMBasicBlockRef fits_bb = LLVMAppendBasicBlockInContext(ctx->context, cur_fn, "fstr.fits");
    LLVMBasicBlockRef big_bb  = LLVMAppendBasicBlockInContext(ctx->context, cur_fn, "fstr.big");
    LLVMBasicBlockRef done_bb = LLVMAppendBasicBlockInContext(ctx->context, cur_fn, "fstr.done");
    LLVMBuildCondBr(ctx->builder, fits, fits_bb, big_bb);

    /* fast path: scratch holds the full result incl NUL (n+1 <= 256 bytes). */
    LLVMPositionBuilderAtEnd(ctx->builder, fits_bb);
    LLVMBuildMemCpy(ctx->builder, buf, 1, tmp_buf, 1, cap64);
    LLVMBuildBr(ctx->builder, done_bb);

    /* fallback: result longer than scratch → reformat directly into buf. */
    LLVMPositionBuilderAtEnd(ctx->builder, big_bb);
    sp_args[0] = buf;
    sp_args[1] = cap64;
    LLVMBuildCall2(ctx->builder, sp_type, snprintf_fn, sp_args, (unsigned)spn, "");
    LLVMBuildBr(ctx->builder, done_bb);

    free(sp_args);
    free(vals);

    LLVMPositionBuilderAtEnd(ctx->builder, done_bb);
    /* P2 (docs/plan_string_to_stdlib.md §5.2): if a `Str` is expected, wrap the
       same heap buffer as an OWNED Str struct {data=buf, len=n, cap=n+1} (cap>0
       → Str.__drop frees it). Zero-copy — reuses the f-string's buffer. Returned
       as a has_drop struct rvalue VALUE; the consumer (var-decl move / discard
       spill+drop / call-arg) routes it through the unified ownership path, exactly
       like a call returning a has_drop struct. */
    if (node->resolved_type && node->resolved_type->kind == TYPE_STRUCT &&
        node->resolved_type->as.strukt.name &&
        strcmp(node->resolved_type->as.strukt.name, "Str") == 0)
    {
        LLVMTypeRef st = type_to_llvm(ctx, node->resolved_type);
        return cg_make_str(ctx, st, buf, n, cap);
    }
    cg_error(ctx, node->line, node->column,
             "internal: f-string not typed as Str");
    return NULL;
}

/* Phase E.3.1: errno() -> int  — read the C runtime's thread-local errno.
   Both libc surfaces expose errno indirectly (it's a macro): on MSVCRT
   `errno` expands to `*_errno()`, on glibc to `*__errno_location()`. We
   emit the deref inline so users can write plain `errno()` in LS. */
static LLVMValueRef codegen_errno_call(CodegenContext *ctx)
{
    LLVMTypeRef i32_t = LLVMInt32TypeInContext(ctx->context);
    LLVMTypeRef ptr_t = LLVMPointerTypeInContext(ctx->context, 0);

#ifdef _WIN32
    const char *fname = "_errno";
#elif defined(__APPLE__)
    const char *fname = "__error";
#else
    const char *fname = "__errno_location";
#endif

    LLVMValueRef fn = LLVMGetNamedFunction(ctx->module, fname);
    if (fn == NULL) {
        LLVMTypeRef ft = LLVMFunctionType(ptr_t, NULL, 0, 0);
        fn = LLVMAddFunction(ctx->module, fname, ft);
        LLVMSetLinkage(fn, LLVMExternalLinkage);
    }
    LLVMTypeRef ft = LLVMGlobalGetValueType(fn);
    LLVMValueRef p = LLVMBuildCall2(ctx->builder, ft, fn, NULL, 0, "errno.p");
    return LLVMBuildLoad2(ctx->builder, i32_t, p, "errno.v");
}

/* Phase E.3.3: from_cstr(object) -> string
   Copies a C-style NUL-terminated string (returned by getenv/strerror/etc
   via FFI) into a managed LsString. Returns an empty owned string when
   the pointer is NULL, so call sites need not branch. */
static LLVMValueRef codegen_from_cstr(CodegenContext *ctx, AstNode *node)
{
    if (node->as.call.arg_count != 1) return NULL;
    LLVMValueRef p = codegen_expr(ctx, node->as.call.args[0]);
    if (p == NULL) return NULL;

    LLVMTypeRef i32_t = LLVMInt32TypeInContext(ctx->context);
    LLVMTypeRef i64_t = LLVMInt64TypeInContext(ctx->context);
    LLVMTypeRef i8_t  = LLVMInt8TypeInContext(ctx->context);
    LLVMTypeRef ptr_t = LLVMPointerTypeInContext(ctx->context, 0);

    /* Coerce input to ptr if it came in as a different pointer-like SSA. */
    if (LLVMGetTypeKind(LLVMTypeOf(p)) != LLVMPointerTypeKind)
        p = LLVMBuildIntToPtr(ctx->builder, p, ptr_t, "fromcstr.cast");

    /* NULL guard: if p == null return empty static string ("") */
    LLVMValueRef cur_fn = ctx->current_fn;
    LLVMBasicBlockRef null_bb = LLVMAppendBasicBlockInContext(ctx->context, cur_fn,
                                                              "fromcstr.null");
    LLVMBasicBlockRef ok_bb   = LLVMAppendBasicBlockInContext(ctx->context, cur_fn,
                                                              "fromcstr.ok");
    LLVMBasicBlockRef cont_bb = LLVMAppendBasicBlockInContext(ctx->context, cur_fn,
                                                              "fromcstr.cont");
    LLVMValueRef null_v = LLVMConstNull(ptr_t);
    LLVMValueRef is_null = LLVMBuildICmp(ctx->builder, LLVMIntEQ, p, null_v,
                                          "fromcstr.isnull");
    LLVMBuildCondBr(ctx->builder, is_null, null_bb, ok_bb);

    /* P5-4 S-2: from_cstr produces a `Str` (layout {*u8 data, int len, int cap},
       same as the old LsString — only the LLVM struct type changes). */
    LLVMTypeRef str_t = type_to_llvm(ctx, node->resolved_type);

    /* null path: empty STATIC Str (cap 0 — drop is a no-op). */
    LLVMPositionBuilderAtEnd(ctx->builder, null_bb);
    LLVMValueRef empty_data = LLVMBuildGlobalStringPtr(ctx->builder, "", "fromcstr.emptylit");
    LLVMValueRef empty_str = cg_make_str(ctx, str_t, empty_data,
                                         LLVMConstInt(i32_t, 0, 0),
                                         LLVMConstInt(i32_t, 0, 0));
    LLVMBuildBr(ctx->builder, cont_bb);

    /* ok path: strlen → malloc(len+1) → memcpy → LsString {buf, len, len+1} */
    LLVMPositionBuilderAtEnd(ctx->builder, ok_bb);

    /* strlen(p) -> i64 */
    LLVMValueRef strlen_fn = LLVMGetNamedFunction(ctx->module, "strlen");
    if (strlen_fn == NULL) {
        LLVMTypeRef strlen_t = LLVMFunctionType(i64_t, &ptr_t, 1, 0);
        strlen_fn = LLVMAddFunction(ctx->module, "strlen", strlen_t);
    }
    LLVMTypeRef strlen_ty = LLVMGlobalGetValueType(strlen_fn);
    LLVMValueRef len64 = LLVMBuildCall2(ctx->builder, strlen_ty, strlen_fn,
                                         &p, 1, "fromcstr.len");
    LLVMValueRef len32 = LLVMBuildTrunc(ctx->builder, len64, i32_t, "fromcstr.len32");

    /* cap = len + 1 (room for terminating NUL) */
    LLVMValueRef cap32 = LLVMBuildAdd(ctx->builder, len32,
                                       LLVMConstInt(i32_t, 1, 0), "fromcstr.cap");
    LLVMValueRef cap64 = LLVMBuildSExt(ctx->builder, cap32, i64_t, "fromcstr.cap64");

    /* buf = malloc(cap) — through memcheck wrapper when enabled */
    LLVMValueRef buf = cg_emit_alloc(ctx, cap64, "from_cstr",
                                      node->line, node->column);

    /* memcpy(buf, p, cap)  — includes the trailing NUL */
    LLVMValueRef memcpy_fn = LLVMGetNamedFunction(ctx->module, "memcpy");
    if (memcpy_fn == NULL) {
        LLVMTypeRef params[3] = { ptr_t, ptr_t, i64_t };
        LLVMTypeRef memcpy_t = LLVMFunctionType(ptr_t, params, 3, 0);
        memcpy_fn = LLVMAddFunction(ctx->module, "memcpy", memcpy_t);
    }
    LLVMTypeRef memcpy_ty = LLVMGlobalGetValueType(memcpy_fn);
    LLVMValueRef mc_args[3] = { buf, p, cap64 };
    LLVMBuildCall2(ctx->builder, memcpy_ty, memcpy_fn, mc_args, 3, "");

    LLVMValueRef ok_str = cg_make_str(ctx, str_t, buf, len32, cap32);
    LLVMBuildBr(ctx->builder, cont_bb);

    /* phi the two paths. The result is an owned has_drop Str rvalue — the
       generic struct rvalue protocol (var-decl transfer / call-arg spill /
       expr-stmt drop) takes it from here. */
    LLVMPositionBuilderAtEnd(ctx->builder, cont_bb);
    LLVMValueRef phi = LLVMBuildPhi(ctx->builder, str_t, "fromcstr.r");
    LLVMValueRef vals[2] = { empty_str, ok_str };
    LLVMBasicBlockRef blks[2] = { null_bb, ok_bb };
    LLVMAddIncoming(phi, vals, blks, 2);
    (void)i8_t;
    return phi;
}

LLVMValueRef codegen_expr_or_borrow(CodegenContext *ctx, AstNode *node)
{
    return codegen_expr(ctx, node);
}

/* ---- Lvalue address helper ----
 * Returns a pointer (GEP or alloca) to the storage of an lvalue node
 * without generating a load. Used to compute `self` for instance method calls.
 * Handles AST_IDENT and AST_FIELD (arbitrarily nested). Returns NULL if the
 * node is not an addressable lvalue. */
static LLVMValueRef codegen_addr_of(CodegenContext *ctx, AstNode *node)
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

        /* Get the array pointer (alloca or global) */
        LLVMValueRef arr_ptr = NULL;
        if (arr_obj->kind == AST_IDENT)
        {
            CgSymbol *sym = cg_scope_resolve(ctx->current_scope, arr_obj->as.ident.name);
            if (sym)
                arr_ptr = sym->value;
        }
        else if (arr_obj->kind == AST_FIELD)
        {
            /* Handle field access like self.array_field */
            arr_ptr = codegen_addr_of(ctx, arr_obj);
        }

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

    /* Owned-rvalue receiver: evaluate it, spill to temp alloca so the caller
       can use it as `self` pointer for a chained method call. Register for
       has_drop cleanup so the temporary's buffer is freed at end-of-scope.
         - AST_CALL          — `vec.map(U)(...).reduce(U)(...)`
         - AST_FORMAT_STRING — `f"...".upper()` (the f-string Str temp would
           otherwise leak: it never binds to a variable and the receiver path
           did not register it for drop). */
    if (node->kind == AST_CALL || node->kind == AST_FORMAT_STRING ||
        cg_is_owned_combinator_rvalue(node))
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
        LLVMTypeRef rllvm = type_to_llvm(ctx, rtype);
        /* Zero-init in entry block: this spill's drop may be reached on a path
           that skipped the store below (chained-op receiver inside a match-arm
           conditional). Stray drop then no-ops instead of freeing stack garbage. */
        LLVMValueRef tmp = cg_entry_alloca_zeroed(ctx, rllvm, "tmp.rval.self");
        LLVMBuildStore(ctx->builder, val, tmp);
        cg_push_temp_drop(ctx, tmp, rtype);
        return tmp;
    }

    return NULL; /* Other lvalue forms not yet handled */
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
           parameter is declared with psym->is_mut_borrow=true and is_borrowed=
           true so scope cleanup skips it — the caller retains ownership. */
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

    case AST_BINARY:
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

    case AST_FORMAT_STRING:
        return codegen_format_string(ctx, node);

    case AST_CLOSURE:
        return codegen_closure_literal(ctx, node);

    case AST_CALL:
    {
        /* Slice builtin `s.len()` — extract field 1 of the {ptr,len} view,
           truncated to i32 (LS `int`). */
        if (node->as.call.callee && node->as.call.callee->kind == AST_FIELD &&
            node->as.call.callee->as.field_access.field &&
            strcmp(node->as.call.callee->as.field_access.field, "len") == 0 &&
            node->as.call.callee->as.field_access.object->resolved_type &&
            node->as.call.callee->as.field_access.object->resolved_type->kind == TYPE_SLICE)
        {
            LLVMValueRef sv = codegen_expr(ctx, node->as.call.callee->as.field_access.object);
            LLVMValueRef len64 = LLVMBuildExtractValue(ctx->builder, sv, 1, "slen");
            return LLVMBuildTrunc(ctx->builder, len64,
                                  LLVMInt32TypeInContext(ctx->context), "slen32");
        }

        /* A-1: canonical-path call to a std.c primitive (std.c.malloc/realloc/
           free/abort). Lower identically to the bare-name builtins. Must come
           before the generic field/method dispatch, which can't resolve the
           `std.c` qualifier. See docs/plan_runtime_primitives.md §5.3. */
        {
            int prim = cg_match_stdc_prim(node->as.call.callee);
            if (prim == 0 || prim == 1) /* malloc(sz) / realloc(p, sz) */
            {
                const char *fn = (prim == 0) ? "malloc" : "realloc";
                int n = node->as.call.arg_count;
                LLVMTypeRef i64t = LLVMInt64TypeInContext(ctx->context);
                /* size arg index: malloc → 0, realloc → 1. Coerce it to i64
                   (LS `int` is i32; old builtin widened at the call site). */
                int size_idx = (prim == 0) ? 0 : 1;
                LLVMValueRef args[2];
                for (int i = 0; i < n && i < 2; i++)
                {
                    LLVMValueRef a = codegen_expr(ctx, node->as.call.args[i]);
                    if (i == size_idx && a != NULL &&
                        LLVMGetTypeKind(LLVMTypeOf(a)) == LLVMIntegerTypeKind &&
                        LLVMGetIntTypeWidth(LLVMTypeOf(a)) < 64)
                        a = LLVMBuildSExt(ctx->builder, a, i64t, "sz.i64");
                    args[i] = a;
                }
                LLVMValueRef f = LLVMGetNamedFunction(ctx->module, fn);
                if (f == NULL)
                {
                    /* Declare on demand: (i64)->i8* / (i8*,i64)->i8* */
                    LLVMTypeRef i8p = LLVMPointerType(LLVMInt8TypeInContext(ctx->context), 0);
                    LLVMTypeRef ps0[2];
                    LLVMTypeRef ft;
                    if (prim == 0) { ps0[0] = LLVMInt64TypeInContext(ctx->context);
                                     ft = LLVMFunctionType(i8p, ps0, 1, 0); }
                    else { ps0[0] = i8p; ps0[1] = LLVMInt64TypeInContext(ctx->context);
                           ft = LLVMFunctionType(i8p, ps0, 2, 0); }
                    f = LLVMAddFunction(ctx->module, fn, ft);
                }
                LLVMTypeRef ft = LLVMGlobalGetValueType(f);
                return LLVMBuildCall2(ctx->builder, ft, f, args, n, "");
            }
            if (prim == 2) /* free(p) — drop struct payload first, then free */
            {
                if (node->as.call.arg_count == 1)
                {
                    LLVMValueRef ptr = codegen_expr(ctx, node->as.call.args[0]);
                    if (ptr == NULL) return NULL;
                    Type *arg_type = node->as.call.args[0]->resolved_type;
                    if (arg_type && arg_type->kind == TYPE_POINTER &&
                        arg_type->as.pointer_to &&
                        arg_type->as.pointer_to->kind == TYPE_STRUCT &&
                        arg_type->as.pointer_to->as.strukt.has_drop)
                    {
                        LLVMTypeRef pt = LLVMTypeOf(ptr);
                        LLVMValueRef isn = LLVMBuildICmp(ctx->builder, LLVMIntEQ,
                                                         ptr, LLVMConstNull(pt), "free.is_null");
                        LLVMValueRef cur = LLVMGetBasicBlockParent(LLVMGetInsertBlock(ctx->builder));
                        LLVMBasicBlockRef skip = LLVMAppendBasicBlockInContext(ctx->context, cur, "free.skip_drop");
                        LLVMBasicBlockRef dod = LLVMAppendBasicBlockInContext(ctx->context, cur, "free.do_drop");
                        LLVMBuildCondBr(ctx->builder, isn, skip, dod);
                        LLVMPositionBuilderAtEnd(ctx->builder, dod);
                        emit_struct_drop(ctx, ptr, arg_type->as.pointer_to);
                        LLVMBuildBr(ctx->builder, skip);
                        LLVMPositionBuilderAtEnd(ctx->builder, skip);
                    }
                    LLVMValueRef free_fn = LLVMGetNamedFunction(ctx->module, "free");
                    LLVMTypeRef free_type = LLVMGlobalGetValueType(free_fn);
                    return LLVMBuildCall2(ctx->builder, free_type, free_fn, &ptr, 1, "");
                }
            }
            if (prim == 3 && node->as.call.arg_count == 0) /* abort() */
            {
                LLVMValueRef exit_fn = LLVMGetNamedFunction(ctx->module, "__ls_proc_exit");
                LLVMTypeRef exit_ty = LLVMFunctionType(
                    LLVMVoidTypeInContext(ctx->context),
                    (LLVMTypeRef[]){ LLVMInt32TypeInContext(ctx->context) }, 1, 0);
                if (exit_fn == NULL)
                    exit_fn = LLVMAddFunction(ctx->module, "__ls_proc_exit", exit_ty);
                LLVMValueRef code = LLVMConstInt(LLVMInt32TypeInContext(ctx->context), 1, 0);
                LLVMBuildCall2(ctx->builder, exit_ty, exit_fn, &code, 1, "");
                return NULL;
            }
        }

        /* Phase B closures: callee is a Block-typed expression (local var or
           an inline `|x| body` literal). Lower as indirect call through the
           {fn_ptr, env_ptr} fat pointer. Must come before the user-fn lookup
           paths, which assume LLVMGetNamedFunction. */
        if (node->as.call.callee->resolved_type &&
            node->as.call.callee->resolved_type->kind == TYPE_BLOCK)
        {
            return codegen_block_call(ctx, node);
        }

        /* Variant ctor short-circuit: callee is an IDENT and the checker
           resolved this CALL to a TYPE_ENUM (which only happens for variant
           constructors).  Skip method/function dispatch entirely. */
        if (node->as.call.callee->kind == AST_IDENT &&
            node->resolved_type && node->resolved_type->kind == TYPE_ENUM)
        {
            Type *et = node->resolved_type;
            const char *vname = node->as.call.callee->as.ident.name;
            for (int v = 0; v < et->as.enom.variant_count; v++)
            {
                if (strcmp(et->as.enom.variants[v].name, vname) == 0)
                    return emit_enum_ctor(ctx, node, et, v,
                                          node->as.call.args, node->as.call.arg_count);
            }
        }

        /* Intercept __move(x) — Phase 4: transparent no-op at codegen.
           The checker has already marked x as moved and rejected subsequent
           uses. At codegen we just forward to the inner expression's value,
           so `v.push(__move(s))` behaves identically to `v.push(s)` in the
           generated IR. Ownership-transfer logic in container ops unwraps
           via ast_unwrap_move() to see the underlying IDENT. */
        if (node->as.call.callee->kind == AST_IDENT &&
            cg_is_intrinsic(node->as.call.callee->as.ident.name, "@move", "__move") &&
            node->as.call.arg_count == 1)
        {
            return codegen_expr(ctx, node->as.call.args[0]);
        }

        /* Intercept @print(...) calls (callee IDENT "@print") — inline printf
           to the current sink stream with type-aware format. */
        if (node->as.call.callee->kind == AST_IDENT && strcmp(node->as.call.callee->as.ident.name, "@print") == 0)
        {
            return codegen_print_call(ctx, node);
        }

        /* Phase E.3.3: intercept from_cstr() — copy C char* into LsString */
        if (node->as.call.callee->kind == AST_IDENT &&
            strcmp(node->as.call.callee->as.ident.name, "from_cstr") == 0)
        {
            return codegen_from_cstr(ctx, node);
        }

        /* Phase E.3.1: intercept errno() — read libc thread-local errno */
        if (node->as.call.callee->kind == AST_IDENT &&
            strcmp(node->as.call.callee->as.ident.name, "errno") == 0)
        {
            return codegen_errno_call(ctx);
        }

        /* Intercept free() calls — call __drop before free for struct pointers */
        if (node->as.call.callee->kind == AST_IDENT && strcmp(node->as.call.callee->as.ident.name, "free") == 0)
        {
            if (node->as.call.arg_count == 1)
            {
                /* Get the argument (pointer to free) */
                LLVMValueRef ptr = codegen_expr(ctx, node->as.call.args[0]);
                if (ptr == NULL)
                    return NULL;

                /* Check if it's a struct pointer and call __drop before free */
                Type *arg_type = node->as.call.args[0]->resolved_type;
                if (arg_type && arg_type->kind == TYPE_POINTER &&
                    arg_type->as.pointer_to &&
                    arg_type->as.pointer_to->kind == TYPE_STRUCT &&
                    arg_type->as.pointer_to->as.strukt.has_drop)
                {
                    /* Guard: only call __drop if ptr != NULL (C standard: free(NULL) is safe) */
                    LLVMTypeRef ptr_type = LLVMTypeOf(ptr);
                    LLVMValueRef null_val = LLVMConstNull(ptr_type);
                    LLVMValueRef is_null = LLVMBuildICmp(ctx->builder, LLVMIntEQ,
                                                         ptr, null_val, "free.is_null");

                    LLVMValueRef cur_fn = LLVMGetBasicBlockParent(
                        LLVMGetInsertBlock(ctx->builder));
                    LLVMBasicBlockRef skip_drop_bb = LLVMAppendBasicBlockInContext(
                        ctx->context, cur_fn, "free.skip_drop");
                    LLVMBasicBlockRef do_drop_bb = LLVMAppendBasicBlockInContext(
                        ctx->context, cur_fn, "free.do_drop");

                    LLVMBuildCondBr(ctx->builder, is_null, skip_drop_bb, do_drop_bb);

                    /* Emit __drop call in do_drop_bb */
                    LLVMPositionBuilderAtEnd(ctx->builder, do_drop_bb);
                    emit_struct_drop(ctx, ptr, arg_type->as.pointer_to);
                    LLVMBuildBr(ctx->builder, skip_drop_bb);

                    /* Emit free() call after drop */
                    LLVMPositionBuilderAtEnd(ctx->builder, skip_drop_bb);
                }

                /* Call free(ptr) — goes through wrapper when memcheck enabled */
                LLVMValueRef free_fn = LLVMGetNamedFunction(ctx->module, "free");
                LLVMTypeRef free_type = LLVMGlobalGetValueType(free_fn);
                return LLVMBuildCall2(ctx->builder, free_type, free_fn, &ptr, 1, "");
            }
        }

        /* Intercept abort() — terminate the process via the runtime helper
           __ls_proc_exit(1). Registered as a global builtin in the checker, so it
           is callable unqualified from anywhere (incl. generic method bodies like
           std.vec's bounds checks) without importing std.c. Returns void. */
        if (node->as.call.callee->kind == AST_IDENT &&
            strcmp(node->as.call.callee->as.ident.name, "abort") == 0 &&
            node->as.call.arg_count == 0)
        {
            LLVMValueRef exit_fn = LLVMGetNamedFunction(ctx->module, "__ls_proc_exit");
            LLVMTypeRef exit_ty = LLVMFunctionType(
                LLVMVoidTypeInContext(ctx->context),
                (LLVMTypeRef[]){ LLVMInt32TypeInContext(ctx->context) }, 1, 0);
            if (exit_fn == NULL)
                exit_fn = LLVMAddFunction(ctx->module, "__ls_proc_exit", exit_ty);
            LLVMValueRef code = LLVMConstInt(LLVMInt32TypeInContext(ctx->context), 1, 0);
            LLVMBuildCall2(ctx->builder, exit_ty, exit_fn, &code, 1, "");
            return NULL; /* void */
        }

        /* Structured concurrency (generic Task(T)):
             __task_spawn(Block()->T, *T box) -> object
             __task_join(object)              -> void
           __task_spawn extracts the closure's {code_fn, env}, synthesises a
           per-T `thunk` that calls the closure and stores its by-value result
           into the `*T box` slot, then hands {thunk, fn, env, box} to the
           worker. It MOVES the env into the worker (suppresses the caller-scope
           env drop — the worker frees it once after the body runs, see
           ls_thread_trampoline). This is the whole point: a Vec move-captured
           into the closure is dropped exactly once, by the worker; the spawning
           scope already marked its source MOVED. The closure returns T by value
           (LLVM handles sret/register ABI), so `*box = closure(env)` is uniform
           over POD and aggregate, and the store IS the move (no clone, no drop).
           The runtime never touches the result bytes — single owner across the
           boundary; join() moves it out via __take. */
        if (node->as.call.callee->kind == AST_IDENT &&
            strcmp(node->as.call.callee->as.ident.name, "__task_spawn") == 0 &&
            node->as.call.arg_count == 2)
        {
            AstNode *blk = node->as.call.args[0];
            AstNode *boxarg = node->as.call.args[1];
            /* T = the Block's return type (checker validated arg0 is a Block). */
            Type *blk_t = blk->resolved_type;
            if (blk_t == NULL || blk_t->kind != TYPE_BLOCK)
            {
                cg_error(ctx, node->line, node->column,
                         "internal: __task_spawn arg0 is not a Block");
                return NULL;
            }
            LLVMTypeRef res_llvm =
                type_to_llvm(ctx, blk_t->as.function.return_type);
            LLVMValueRef closure_val = codegen_expr(ctx, blk);
            if (closure_val == NULL) return NULL;
            LLVMValueRef fn_ptr  = LLVMBuildExtractValue(ctx->builder, closure_val, 0, "task.fn");
            LLVMValueRef env_ptr = LLVMBuildExtractValue(ctx->builder, closure_val, 1, "task.env");
            LLVMValueRef box_ptr = codegen_expr(ctx, boxarg);
            if (box_ptr == NULL) return NULL;
            /* Move the env into the thread (mirror the container-store Block
               handling): a named Block var is nulled; a literal's temp env is
               consumed so the caller scope does not also free it. */
            if (blk->kind == AST_IDENT)
            {
                CgSymbol *bsym = cg_scope_resolve(ctx->current_scope, blk->as.ident.name);
                if (bsym && !bsym->is_borrowed)
                    cg_null_block_env(ctx, bsym->value);
            }
            else if (ctx->temp_block_env_count > 0)
                ctx->temp_block_env_count--;

            LLVMTypeRef ptr_t = LLVMPointerTypeInContext(ctx->context, 0);
            LLVMTypeRef void_t = LLVMVoidTypeInContext(ctx->context);

            /* Synthesise the per-T thunk:
                 void __task_thunk_<id>(ptr fn, ptr env, ptr box):
                     T r = ((T(*)(ptr))fn)(env)
                     store r -> box
                     ret void
               (save/restore builder, mirroring __env_drop_<id>.) */
            int tid = ctx->closure_id_counter++;
            char thunk_name[64];
            snprintf(thunk_name, sizeof(thunk_name), "__task_thunk_%d", tid);
            LLVMTypeRef thunk_param_t[3] = { ptr_t, ptr_t, ptr_t };
            LLVMTypeRef thunk_ty = LLVMFunctionType(void_t, thunk_param_t, 3, 0);
            LLVMValueRef thunk_fn = LLVMAddFunction(ctx->module, thunk_name, thunk_ty);

            LLVMBasicBlockRef t_saved = LLVMGetInsertBlock(ctx->builder);
            LLVMBasicBlockRef t_entry =
                LLVMAppendBasicBlockInContext(ctx->context, thunk_fn, "entry");
            LLVMPositionBuilderAtEnd(ctx->builder, t_entry);
            LLVMValueRef t_fn  = LLVMGetParam(thunk_fn, 0);
            LLVMValueRef t_env = LLVMGetParam(thunk_fn, 1);
            LLVMValueRef t_box = LLVMGetParam(thunk_fn, 2);
            LLVMTypeRef clo_ty = LLVMFunctionType(res_llvm, &ptr_t, 1, 0);
            LLVMValueRef r =
                LLVMBuildCall2(ctx->builder, clo_ty, t_fn, &t_env, 1, "task.r");
            LLVMBuildStore(ctx->builder, r, t_box);   /* store IS the move */
            /* L-015 fix: drop the closure env HERE, in the worker thunk, after
               the body ran. This is the single owner of the env across the
               thread boundary (the spawning scope already suppressed its own
               env drop above). Doing it in LS-emitted code means the free goes
               through the memcheck-tracked free wrapper (ls_mc_free) — the
               runtime trampoline previously freed env with a RAW free(), which
               the tracker never saw, surfacing as a false per-spawn leak.
               The earlier rc=139 was a genuine double-free: the prior attempt
               ADDED this drop while the trampoline STILL freed env. The
               trampoline's drop+free is now removed (os_win32/os_posix). */
            cg_emit_block_env_drop(ctx, t_env);
            LLVMBuildRetVoid(ctx->builder);
            if (t_saved) LLVMPositionBuilderAtEnd(ctx->builder, t_saved);

            LLVMValueRef spawn_fn = LLVMGetNamedFunction(ctx->module, "ls_thread_spawn");
            LLVMTypeRef spawn_ty = LLVMFunctionType(
                ptr_t, (LLVMTypeRef[]){ptr_t, ptr_t, ptr_t, ptr_t}, 4, 0);
            if (spawn_fn == NULL)
                spawn_fn = LLVMAddFunction(ctx->module, "ls_thread_spawn", spawn_ty);
            LLVMValueRef sargs[4] = { thunk_fn, fn_ptr, env_ptr, box_ptr };
            return LLVMBuildCall2(ctx->builder, spawn_ty, spawn_fn, sargs, 4, "task.handle");
        }
        if (node->as.call.callee->kind == AST_IDENT &&
            strcmp(node->as.call.callee->as.ident.name, "__task_join") == 0 &&
            node->as.call.arg_count == 1)
        {
            LLVMValueRef h = codegen_expr(ctx, node->as.call.args[0]);
            if (h == NULL) return NULL;
            LLVMTypeRef ptr_t = LLVMPointerTypeInContext(ctx->context, 0);
            LLVMTypeRef void_t = LLVMVoidTypeInContext(ctx->context);
            LLVMValueRef join_fn = LLVMGetNamedFunction(ctx->module, "ls_thread_join");
            LLVMTypeRef join_ty = LLVMFunctionType(void_t, &ptr_t, 1, 0);
            if (join_fn == NULL)
                join_fn = LLVMAddFunction(ctx->module, "ls_thread_join", join_ty);
            LLVMBuildCall2(ctx->builder, join_ty, join_fn, &h, 1, "");
            return NULL; /* void */
        }

        /* Mutex + spin runtime intrinsics (std.sync). Emit a call to the OS-
           backend runtime function on an opaque handle. These are GLOBAL
           intrinsics (not import aliases), so — like __task_* — they survive
           generic-method instantiation in a consumer module that hasn't imported
           std.c. They know nothing about Mutex(T): an opaque handle in/out is the
           whole interface (the same clean boundary as __atomic_* over scalars). */
        if (node->as.call.callee->kind == AST_IDENT &&
            (strncmp(node->as.call.callee->as.ident.name, "__mutex_", 8) == 0 ||
             strncmp(node->as.call.callee->as.ident.name, "__rwlock_", 9) == 0 ||
             strncmp(node->as.call.callee->as.ident.name, "__cond_", 7) == 0 ||
             strcmp(node->as.call.callee->as.ident.name, "__cpu_relax") == 0 ||
             strcmp(node->as.call.callee->as.ident.name, "__cpu_yield") == 0))
        {
            const char *mname = node->as.call.callee->as.ident.name;
            LLVMTypeRef ptr_t = LLVMPointerTypeInContext(ctx->context, 0);
            LLVMTypeRef void_t = LLVMVoidTypeInContext(ctx->context);
            LLVMTypeRef i32_t = LLVMInt32TypeInContext(ctx->context);

            const char *sym = NULL;
            LLVMTypeRef ret_t = void_t;
            int nargs = 1; /* default: one opaque handle argument */
            if (strcmp(mname, "__mutex_init") == 0)
                { sym = "ls_mutex_init"; ret_t = ptr_t; nargs = 0; }
            else if (strcmp(mname, "__mutex_lock") == 0)
                { sym = "ls_mutex_lock"; ret_t = i32_t; }
            else if (strcmp(mname, "__mutex_trylock") == 0)
                { sym = "ls_mutex_trylock"; ret_t = i32_t; }
            else if (strcmp(mname, "__mutex_unlock") == 0)
                { sym = "ls_mutex_unlock"; ret_t = i32_t; }
            else if (strcmp(mname, "__mutex_destroy") == 0)
                { sym = "ls_mutex_destroy"; ret_t = void_t; }
            else if (strcmp(mname, "__rwlock_init") == 0)
                { sym = "ls_rwlock_init"; ret_t = ptr_t; nargs = 0; }
            else if (strcmp(mname, "__rwlock_rdlock") == 0)
                { sym = "ls_rwlock_rdlock"; ret_t = i32_t; }
            else if (strcmp(mname, "__rwlock_wrlock") == 0)
                { sym = "ls_rwlock_wrlock"; ret_t = i32_t; }
            else if (strcmp(mname, "__rwlock_rdunlock") == 0)
                { sym = "ls_rwlock_rdunlock"; ret_t = i32_t; }
            else if (strcmp(mname, "__rwlock_wrunlock") == 0)
                { sym = "ls_rwlock_wrunlock"; ret_t = i32_t; }
            else if (strcmp(mname, "__rwlock_destroy") == 0)
                { sym = "ls_rwlock_destroy"; ret_t = void_t; }
            /* condition variables (std.chan). __cond_wait is the only 2-arg sync
               intrinsic (cond handle, mutex handle — both opaque pointers). */
            else if (strcmp(mname, "__cond_init") == 0)
                { sym = "ls_cond_init"; ret_t = ptr_t; nargs = 0; }
            else if (strcmp(mname, "__cond_wait") == 0)
                { sym = "ls_cond_wait"; ret_t = void_t; nargs = 2; }
            else if (strcmp(mname, "__cond_signal") == 0)
                { sym = "ls_cond_signal"; ret_t = void_t; }
            else if (strcmp(mname, "__cond_broadcast") == 0)
                { sym = "ls_cond_broadcast"; ret_t = void_t; }
            else if (strcmp(mname, "__cond_destroy") == 0)
                { sym = "ls_cond_destroy"; ret_t = void_t; }
            else if (strcmp(mname, "__cpu_relax") == 0)
                { sym = "ls_cpu_relax"; ret_t = void_t; nargs = 0; }
            else if (strcmp(mname, "__cpu_yield") == 0)
                { sym = "ls_cpu_yield"; ret_t = void_t; nargs = 0; }
            else
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

        /* Intercept __drop_at(place) — run the recursive destructor on the value
           stored at an lvalue place (raw pointer slot p[i], field, *p) WITHOUT
           freeing any backing buffer. No-op for POD. Returns void. The nested
           drop is automatic: emit_drop_value recurses (string free / vec / map /
           struct.__drop / enum.__drop), so __drop_at on a RawVec(RawVec(T)) slot
           dispatches to the inner RawVec's user __drop. */
        if (node->as.call.callee->kind == AST_IDENT &&
            cg_is_intrinsic(node->as.call.callee->as.ident.name, "@dispose", "__drop_at") &&
            node->as.call.arg_count == 1)
        {
            AstNode *place = node->as.call.args[0];
            LLVMValueRef ptr = codegen_lvalue_ptr(ctx, place);
            if (ptr == NULL)
            {
                cg_error(ctx, node->line, node->column,
                         "__drop_at: argument is not an addressable place");
                return NULL;
            }
            emit_drop_value(ctx, ptr, place->resolved_type);
            return NULL; /* void */
        }

        /* Intercept __take(place) — move-OUT: load the value at an lvalue place
           WITHOUT cloning (the raw bit-read), handing ownership to the caller. The
           slot is left holding stale bits; the container excludes it via its len
           (or overwrites it). Counterpart of __drop_at; used to relocate elements
           (pop/remove/insert/swap) without a clone. */
        if (node->as.call.callee->kind == AST_IDENT &&
            cg_is_intrinsic(node->as.call.callee->as.ident.name, "@take", "__take") &&
            node->as.call.arg_count == 1)
        {
            AstNode *place = node->as.call.args[0];
            LLVMValueRef ptr = codegen_lvalue_ptr(ctx, place);
            if (ptr == NULL)
            {
                cg_error(ctx, node->line, node->column,
                         "__take: argument is not an addressable place");
                return NULL;
            }
            Type *et = place->resolved_type;
            LLVMTypeRef elt = type_to_llvm(ctx, et);
            return LLVMBuildLoad2(ctx->builder, elt, ptr, "take");
        }

        /* Intercept __dup(place) — DEEP COPY without consuming: load the value at
           the place and run it through emit_clone_value (POD → the loaded value
           verbatim; has_drop → a deep clone via __clone). The source place is
           untouched (stays live). The clone counterpart of __take; the generic
           value-duplication primitive behind Vec.fill / Map.get_or_insert. */
        if (node->as.call.callee->kind == AST_IDENT &&
            cg_is_intrinsic(node->as.call.callee->as.ident.name, "@dup", "__dup") &&
            node->as.call.arg_count == 1)
        {
            AstNode *place = node->as.call.args[0];
            LLVMValueRef ptr = codegen_lvalue_ptr(ctx, place);
            if (ptr == NULL)
            {
                cg_error(ctx, node->line, node->column,
                         "__dup: argument is not an addressable place");
                return NULL;
            }
            Type *et = place->resolved_type;
            LLVMTypeRef elt = type_to_llvm(ctx, et);
            LLVMValueRef loaded = LLVMBuildLoad2(ctx->builder, elt, ptr, "dup.src");
            return emit_clone_value(ctx, loaded, elt, et);
        }

        /* __rawstr("literal") -> *u8 : emit the literal's baked .rodata pointer
           directly (the same i8* Str's .data would hold), without constructing a
           Str. Used by std.core.reflect_core. */
        if (node->as.call.callee->kind == AST_IDENT &&
            strcmp(node->as.call.callee->as.ident.name, "__rawstr") == 0 &&
            node->as.call.arg_count == 1 &&
            node->as.call.args[0]->kind == AST_STRING_LIT)
        {
            const char *text = node->as.call.args[0]->as.string_lit.value;
            return LLVMBuildGlobalStringPtr(ctx->builder, text ? text : "", ".ls.rawstr");
        }

        /* Atomic intrinsics (std.atomic) — emit a single inline LLVM atomic
           instruction, SequentiallyConsistent (full barrier). arg0 is an lvalue
           place (self.value); the rest are by-value operands. T must be a
           lock-free scalar (≤8 bytes) — larger types get a clean error pointing
           at Mutex. This is the whole point of Atomic: one machine instruction,
           no lock, no call. */
        if (node->as.call.callee->kind == AST_IDENT &&
            strncmp(node->as.call.callee->as.ident.name, "__atomic_", 9) == 0)
        {
            const char *aname = node->as.call.callee->as.ident.name;
            /* Memory order from the name suffix; default SeqCst (full barrier).
               _acquire/_release/_relaxed enable the cheaper orderings the SPSC
               ring fast path uses (acq/rel ~ plain mov on x64 vs SeqCst's locked
               xchg). The 4 ordering suffixes are all 8 chars. */
            LLVMAtomicOrdering ord = LLVMAtomicOrderingSequentiallyConsistent;
            {
                size_t alen = strlen(aname);
                if (alen > 8) {
                    const char *suf = aname + alen - 8;
                    if (strcmp(suf, "_acquire") == 0)      ord = LLVMAtomicOrderingAcquire;
                    else if (strcmp(suf, "_release") == 0) ord = LLVMAtomicOrderingRelease;
                    else if (strcmp(suf, "_relaxed") == 0) ord = LLVMAtomicOrderingMonotonic;
                }
            }

            if (strcmp(aname, "__atomic_fence") == 0)
            {
                LLVMBuildFence(ctx->builder, ord, 0, "");
                return NULL; /* void */
            }

            AstNode *place = node->as.call.args[0];
            LLVMValueRef ptr = codegen_lvalue_ptr(ctx, place);
            if (ptr == NULL)
            {
                cg_error(ctx, node->line, node->column,
                         "%s: argument is not an addressable place", aname);
                return NULL;
            }
            Type *at = place->resolved_type;
            /* Lock-free byte-sized scalars only. bool (i1) is excluded: LLVM
               atomics must be byte-sized — use Atomic(int) for flags. Anything
               larger than a scalar goes through Mutex. */
            bool scalar_ok = at && (at->kind == TYPE_INT || at->kind == TYPE_I8 ||
                at->kind == TYPE_I16 || at->kind == TYPE_I32 || at->kind == TYPE_I64 ||
                at->kind == TYPE_U8 || at->kind == TYPE_U16 || at->kind == TYPE_U32 ||
                at->kind == TYPE_U64 || at->kind == TYPE_F32 || at->kind == TYPE_F64 ||
                at->kind == TYPE_CHAR ||
                at->kind == TYPE_POINTER || at->kind == TYPE_OBJECT);
            if (!scalar_ok)
            {
                cg_error(ctx, node->line, node->column,
                         "atomic requires a lock-free byte-sized scalar "
                         "(int/i64/u64/f64/char/pointer); use Atomic(int) for a "
                         "flag, or Mutex for larger types");
                return NULL;
            }
            LLVMTypeRef elt = type_to_llvm(ctx, at);
            LLVMTargetDataRef td = LLVMGetModuleDataLayout(ctx->module);
            unsigned align = LLVMABIAlignmentOfType(td, elt);
            bool is_float = (at->kind == TYPE_F32 || at->kind == TYPE_F64);

            if (strncmp(aname, "__atomic_load", 13) == 0)
            {
                LLVMValueRef ld = LLVMBuildLoad2(ctx->builder, elt, ptr, "atom.load");
                LLVMSetOrdering(ld, ord);
                LLVMSetAlignment(ld, align);
                return ld;
            }
            if (strncmp(aname, "__atomic_store", 14) == 0)
            {
                LLVMValueRef v = codegen_expr(ctx, node->as.call.args[1]);
                if (v == NULL) return NULL;
                LLVMValueRef st = LLVMBuildStore(ctx->builder, v, ptr);
                LLVMSetOrdering(st, ord);
                LLVMSetAlignment(st, align);
                return NULL; /* void */
            }
            if (strcmp(aname, "__atomic_add") == 0 || strcmp(aname, "__atomic_sub") == 0)
            {
                LLVMValueRef v = codegen_expr(ctx, node->as.call.args[1]);
                if (v == NULL) return NULL;
                LLVMAtomicRMWBinOp op;
                if (strcmp(aname, "__atomic_add") == 0)
                    op = is_float ? LLVMAtomicRMWBinOpFAdd : LLVMAtomicRMWBinOpAdd;
                else
                    op = is_float ? LLVMAtomicRMWBinOpFSub : LLVMAtomicRMWBinOpSub;
                return LLVMBuildAtomicRMW(ctx->builder, op, ptr, v, ord, 0); /* old value */
            }
            if (strcmp(aname, "__atomic_swap") == 0)
            {
                LLVMValueRef v = codegen_expr(ctx, node->as.call.args[1]);
                if (v == NULL) return NULL;
                return LLVMBuildAtomicRMW(ctx->builder, LLVMAtomicRMWBinOpXchg,
                                          ptr, v, ord, 0); /* old value */
            }
            if (strcmp(aname, "__atomic_cas") == 0)
            {
                LLVMValueRef expected = codegen_expr(ctx, node->as.call.args[1]);
                LLVMValueRef desired = codegen_expr(ctx, node->as.call.args[2]);
                if (expected == NULL || desired == NULL) return NULL;
                /* strong CAS; SeqCst on both success and failure paths */
                LLVMValueRef cx = LLVMBuildAtomicCmpXchg(ctx->builder, ptr,
                                          expected, desired, ord, ord, 0);
                return LLVMBuildExtractValue(ctx->builder, cx, 1, "cas.ok"); /* i1 success */
            }
            cg_error(ctx, node->line, node->column,
                     "internal: unknown atomic intrinsic '%s'", aname);
            return NULL;
        }

        /* SIMD intrinsics __simd_* — lower to a single <N x T> IR instruction
           (docs/plan_simd.md §4.2), mirroring the __atomic_* name-dispatch. The
           checker set node->resolved_type (Simd for producers/ops, the element
           type for lane/reduce). */
        if (node->as.call.callee->kind == AST_IDENT &&
            strncmp(node->as.call.callee->as.ident.name, "__simd_", 7) == 0)
        {
            const char *sname = node->as.call.callee->as.ident.name;
            AstNode **sa = node->as.call.args;
            Type *rt = node->resolved_type;

            if (strcmp(sname, "__simd_zero") == 0)
                return LLVMConstNull(type_to_llvm(ctx, rt));

            if (strcmp(sname, "__simd_splat") == 0)
            {
                LLVMTypeRef vt = type_to_llvm(ctx, rt);
                LLVMValueRef s = codegen_expr(ctx, sa[0]);
                if (s == NULL) return NULL;
                s = cg_simd_coerce(ctx, s, sa[0]->resolved_type, rt->as.simd.elem);
                LLVMTypeRef i32 = LLVMInt32TypeInContext(ctx->context);
                LLVMValueRef undef = LLVMGetUndef(vt);
                LLVMValueRef ins = LLVMBuildInsertElement(ctx->builder, undef, s,
                                       LLVMConstInt(i32, 0, 0), "splat.ins");
                LLVMValueRef zmask = LLVMConstNull(
                    LLVMVectorType(i32, (unsigned)rt->as.simd.lanes));
                return LLVMBuildShuffleVector(ctx->builder, ins, undef, zmask, "splat");
            }

            if (strcmp(sname, "__simd_lane") == 0)
            {
                LLVMValueRef v = codegen_expr(ctx, sa[0]);
                LLVMValueRef idx = codegen_expr(ctx, sa[1]);
                if (v == NULL || idx == NULL) return NULL;
                return LLVMBuildExtractElement(ctx->builder, v, idx, "simd.lane");
            }

            if (strcmp(sname, "__simd_fma") == 0)
            {
                LLVMValueRef a = codegen_expr(ctx, sa[0]);
                LLVMValueRef b = codegen_expr(ctx, sa[1]);
                LLVMValueRef cc = codegen_expr(ctx, sa[2]);
                if (a == NULL || b == NULL || cc == NULL) return NULL;
                LLVMTypeRef vt = type_to_llvm(ctx, rt);
                char mg[24], nm[40];
                cg_simd_mangle(rt, mg, sizeof mg);
                snprintf(nm, sizeof nm, "llvm.fma.%s", mg);
                LLVMTypeRef ps[3] = { vt, vt, vt };
                LLVMValueRef fn = cg_get_or_declare(ctx->module, nm, vt, ps, 3);
                LLVMTypeRef fty = LLVMGlobalGetValueType(fn);
                LLVMValueRef av[3] = { a, b, cc };
                return LLVMBuildCall2(ctx->builder, fty, fn, av, 3, "simd.fma");
            }

            if (strcmp(sname, "__simd_max") == 0 || strcmp(sname, "__simd_min") == 0)
            {
                LLVMValueRef a = codegen_expr(ctx, sa[0]);
                LLVMValueRef b = codegen_expr(ctx, sa[1]);
                if (a == NULL || b == NULL) return NULL;
                LLVMTypeRef vt = type_to_llvm(ctx, rt);
                Type *et = rt->as.simd.elem;
                bool is_max = (strcmp(sname, "__simd_max") == 0);
                const char *base = type_is_float(et) ? (is_max ? "maxnum" : "minnum")
                                 : type_is_unsigned(et) ? (is_max ? "umax" : "umin")
                                 : (is_max ? "smax" : "smin");
                char mg[24], nm[48];
                cg_simd_mangle(rt, mg, sizeof mg);
                snprintf(nm, sizeof nm, "llvm.%s.%s", base, mg);
                LLVMTypeRef ps[2] = { vt, vt };
                LLVMValueRef fn = cg_get_or_declare(ctx->module, nm, vt, ps, 2);
                LLVMTypeRef fty = LLVMGlobalGetValueType(fn);
                LLVMValueRef av[2] = { a, b };
                return LLVMBuildCall2(ctx->builder, fty, fn, av, 2, "simd.mm");
            }

            if (strcmp(sname, "__simd_reduce_add") == 0)
            {
                LLVMValueRef v = codegen_expr(ctx, sa[0]);
                if (v == NULL) return NULL;
                Type *st = sa[0]->resolved_type;    /* Simd(T,N) */
                Type *et = st->as.simd.elem;
                LLVMTypeRef etl = type_to_llvm(ctx, et);
                LLVMTypeRef vt = type_to_llvm(ctx, st);
                char mg[24], nm[48];
                cg_simd_mangle(st, mg, sizeof mg);
                if (type_is_float(et)) {
                    /* T @llvm.vector.reduce.fadd.vNfT(T start, <N x T> v) */
                    snprintf(nm, sizeof nm, "llvm.vector.reduce.fadd.%s", mg);
                    LLVMTypeRef ps[2] = { etl, vt };
                    LLVMValueRef fn = cg_get_or_declare(ctx->module, nm, etl, ps, 2);
                    LLVMTypeRef fty = LLVMGlobalGetValueType(fn);
                    LLVMValueRef av[2] = { LLVMConstNull(etl), v };
                    return LLVMBuildCall2(ctx->builder, fty, fn, av, 2, "simd.radd");
                }
                /* T @llvm.vector.reduce.add.vNiT(<N x T> v) */
                snprintf(nm, sizeof nm, "llvm.vector.reduce.add.%s", mg);
                LLVMTypeRef ps[1] = { vt };
                LLVMValueRef fn = cg_get_or_declare(ctx->module, nm, etl, ps, 1);
                LLVMTypeRef fty = LLVMGlobalGetValueType(fn);
                LLVMValueRef av[1] = { v };
                return LLVMBuildCall2(ctx->builder, fty, fn, av, 1, "simd.radd");
            }

            if (strcmp(sname, "__simd_reduce_max") == 0 ||
                strcmp(sname, "__simd_reduce_min") == 0)
            {
                /* Horizontal max/min of <N x T> to a scalar T. The fmax/fmin
                   reduce intrinsics take just the vector (no start value). */
                LLVMValueRef v = codegen_expr(ctx, sa[0]);
                if (v == NULL) return NULL;
                Type *st = sa[0]->resolved_type;    /* Simd(T,N) */
                Type *et = st->as.simd.elem;
                LLVMTypeRef etl = type_to_llvm(ctx, et);
                LLVMTypeRef vt = type_to_llvm(ctx, st);
                bool is_max = (strcmp(sname, "__simd_reduce_max") == 0);
                const char *base = type_is_float(et) ? (is_max ? "fmax" : "fmin")
                                 : type_is_unsigned(et) ? (is_max ? "umax" : "umin")
                                 : (is_max ? "smax" : "smin");
                char mg[24], nm[48];
                cg_simd_mangle(st, mg, sizeof mg);
                snprintf(nm, sizeof nm, "llvm.vector.reduce.%s.%s", base, mg);
                LLVMTypeRef ps[1] = { vt };
                LLVMValueRef fn = cg_get_or_declare(ctx->module, nm, etl, ps, 1);
                LLVMTypeRef fty = LLVMGlobalGetValueType(fn);
                LLVMValueRef av[1] = { v };
                return LLVMBuildCall2(ctx->builder, fty, fn, av, 1, "simd.rmm");
            }

            if (strcmp(sname, "__simd_load") == 0)
            {
                /* Load N contiguous elements starting at ptr[off] as a <N x T>.
                   GEP by element offset, then a vector load (element-aligned =
                   unaligned vector access, safe for any pointer). */
                LLVMValueRef ptr = codegen_expr(ctx, sa[0]);
                LLVMValueRef off = codegen_expr(ctx, sa[1]);
                if (ptr == NULL || off == NULL) return NULL;
                LLVMTypeRef etl = type_to_llvm(ctx, rt->as.simd.elem);
                LLVMTypeRef vt = type_to_llvm(ctx, rt);
                LLVMValueRef idx[1] = { off };
                LLVMValueRef ep = LLVMBuildGEP2(ctx->builder, etl, ptr, idx, 1, "simd.ep");
                LLVMValueRef ld = LLVMBuildLoad2(ctx->builder, vt, ep, "simd.load");
                LLVMTargetDataRef td = LLVMGetModuleDataLayout(ctx->module);
                LLVMSetAlignment(ld, LLVMABIAlignmentOfType(td, etl));
                return ld;
            }

            if (strcmp(sname, "__simd_store") == 0)
            {
                LLVMValueRef ptr = codegen_expr(ctx, sa[0]);
                LLVMValueRef off = codegen_expr(ctx, sa[1]);
                LLVMValueRef v = codegen_expr(ctx, sa[2]);
                if (ptr == NULL || off == NULL || v == NULL) return NULL;
                Type *st = sa[2]->resolved_type;   /* Simd(T,N) */
                LLVMTypeRef etl = type_to_llvm(ctx, st->as.simd.elem);
                LLVMValueRef idx[1] = { off };
                LLVMValueRef ep = LLVMBuildGEP2(ctx->builder, etl, ptr, idx, 1, "simd.ep");
                LLVMValueRef sst = LLVMBuildStore(ctx->builder, v, ep);
                LLVMTargetDataRef td = LLVMGetModuleDataLayout(ctx->module);
                LLVMSetAlignment(sst, LLVMABIAlignmentOfType(td, etl));
                return NULL;
            }

            if (strcmp(sname, "__simd_load_masked") == 0)
            {
                /* Load the first n lanes (rest = 0) via @llvm.masked.load. */
                LLVMValueRef ptr = codegen_expr(ctx, sa[0]);
                LLVMValueRef off = codegen_expr(ctx, sa[1]);
                LLVMValueRef n   = codegen_expr(ctx, sa[2]);
                if (ptr == NULL || off == NULL || n == NULL) return NULL;
                unsigned lanes = (unsigned)rt->as.simd.lanes;
                LLVMTypeRef etl = type_to_llvm(ctx, rt->as.simd.elem);
                LLVMTypeRef vt  = type_to_llvm(ctx, rt);
                LLVMTypeRef i32 = LLVMInt32TypeInContext(ctx->context);
                LLVMTypeRef i1v = LLVMVectorType(LLVMInt1TypeInContext(ctx->context), lanes);
                LLVMValueRef idx[1] = { off };
                LLVMValueRef ep = LLVMBuildGEP2(ctx->builder, etl, ptr, idx, 1, "simd.mep");
                LLVMValueRef mask = cg_simd_lane_mask(ctx, n, lanes);
                LLVMTargetDataRef td = LLVMGetModuleDataLayout(ctx->module);
                LLVMValueRef align = LLVMConstInt(i32, LLVMABIAlignmentOfType(td, etl), 0);
                char mg[24], nm[56];
                cg_simd_mangle(rt, mg, sizeof mg);
                snprintf(nm, sizeof nm, "llvm.masked.load.%s.p0", mg);
                LLVMTypeRef ps[4] = { LLVMTypeOf(ep), i32, i1v, vt };
                LLVMValueRef fn = cg_get_or_declare(ctx->module, nm, vt, ps, 4);
                LLVMTypeRef fty = LLVMGlobalGetValueType(fn);
                LLVMValueRef av[4] = { ep, align, mask, LLVMConstNull(vt) };
                return LLVMBuildCall2(ctx->builder, fty, fn, av, 4, "simd.mload");
            }

            if (strcmp(sname, "__simd_store_masked") == 0)
            {
                /* Store the first n lanes via @llvm.masked.store. */
                LLVMValueRef ptr = codegen_expr(ctx, sa[0]);
                LLVMValueRef off = codegen_expr(ctx, sa[1]);
                LLVMValueRef v   = codegen_expr(ctx, sa[2]);
                LLVMValueRef n   = codegen_expr(ctx, sa[3]);
                if (ptr == NULL || off == NULL || v == NULL || n == NULL) return NULL;
                Type *st = sa[2]->resolved_type;
                unsigned lanes = (unsigned)st->as.simd.lanes;
                LLVMTypeRef etl = type_to_llvm(ctx, st->as.simd.elem);
                LLVMTypeRef vt  = type_to_llvm(ctx, st);
                LLVMTypeRef i32 = LLVMInt32TypeInContext(ctx->context);
                LLVMTypeRef i1v = LLVMVectorType(LLVMInt1TypeInContext(ctx->context), lanes);
                LLVMTypeRef voidty = LLVMVoidTypeInContext(ctx->context);
                LLVMValueRef idx[1] = { off };
                LLVMValueRef ep = LLVMBuildGEP2(ctx->builder, etl, ptr, idx, 1, "simd.mep");
                LLVMValueRef mask = cg_simd_lane_mask(ctx, n, lanes);
                LLVMTargetDataRef td = LLVMGetModuleDataLayout(ctx->module);
                LLVMValueRef align = LLVMConstInt(i32, LLVMABIAlignmentOfType(td, etl), 0);
                char mg[24], nm[56];
                cg_simd_mangle(st, mg, sizeof mg);
                snprintf(nm, sizeof nm, "llvm.masked.store.%s.p0", mg);
                LLVMTypeRef ps[4] = { vt, LLVMTypeOf(ep), i32, i1v };
                LLVMValueRef fn = cg_get_or_declare(ctx->module, nm, voidty, ps, 4);
                LLVMTypeRef fty = LLVMGlobalGetValueType(fn);
                LLVMValueRef av[4] = { v, ep, align, mask };
                LLVMBuildCall2(ctx->builder, fty, fn, av, 4, "");
                return NULL;
            }

            if (strcmp(sname, "__simd_cast") == 0)
            {
                /* Element-wise numeric conversion to <N x U> (same N). */
                LLVMValueRef v = codegen_expr(ctx, sa[0]);
                if (v == NULL) return NULL;
                Type *st = sa[0]->resolved_type;    /* Simd(T,N) */
                Type *se = st->as.simd.elem;
                Type *de = rt->as.simd.elem;
                LLVMTypeRef vt = type_to_llvm(ctx, rt);  /* <N x U> */
                if (se->kind == de->kind) return v;
                bool sf = type_is_float(se), df = type_is_float(de);
                if (sf && df) {
                    int sb = se->kind==TYPE_F64?64:se->kind==TYPE_F32?32:16;
                    int db = de->kind==TYPE_F64?64:de->kind==TYPE_F32?32:16;
                    if (db > sb) return LLVMBuildFPExt(ctx->builder, v, vt, "simd.cast.fpext");
                    if (db < sb) return LLVMBuildFPTrunc(ctx->builder, v, vt, "simd.cast.fptrunc");
                    /* same width, different 16-bit format (f16<->bf16): via f32 */
                    LLVMTypeRef f32v = LLVMVectorType(
                        LLVMFloatTypeInContext(ctx->context), (unsigned)st->as.simd.lanes);
                    LLVMValueRef up = LLVMBuildFPExt(ctx->builder, v, f32v, "simd.cast.up");
                    return LLVMBuildFPTrunc(ctx->builder, up, vt, "simd.cast.dn");
                }
                if (!sf && df)
                    return type_is_unsigned(se)
                        ? LLVMBuildUIToFP(ctx->builder, v, vt, "simd.cast.uitofp")
                        : LLVMBuildSIToFP(ctx->builder, v, vt, "simd.cast.sitofp");
                if (sf && !df)
                    return type_is_unsigned(de)
                        ? LLVMBuildFPToUI(ctx->builder, v, vt, "simd.cast.fptoui")
                        : LLVMBuildFPToSI(ctx->builder, v, vt, "simd.cast.fptosi");
                int sb = type_int_bits(se), db = type_int_bits(de);
                if (db > sb) return type_is_unsigned(se)
                    ? LLVMBuildZExt(ctx->builder, v, vt, "simd.cast.zext")
                    : LLVMBuildSExt(ctx->builder, v, vt, "simd.cast.sext");
                if (db < sb) return LLVMBuildTrunc(ctx->builder, v, vt, "simd.cast.trunc");
                return v;
            }

            if (strcmp(sname, "__simd_floor") == 0)
            {
                LLVMValueRef v = codegen_expr(ctx, sa[0]);
                if (v == NULL) return NULL;
                LLVMTypeRef vt = type_to_llvm(ctx, rt);
                char mg[24], nm[40];
                cg_simd_mangle(rt, mg, sizeof mg);
                snprintf(nm, sizeof nm, "llvm.floor.%s", mg);
                LLVMTypeRef ps[1] = { vt };
                LLVMValueRef fn = cg_get_or_declare(ctx->module, nm, vt, ps, 1);
                LLVMTypeRef fty = LLVMGlobalGetValueType(fn);
                LLVMValueRef av[1] = { v };
                return LLVMBuildCall2(ctx->builder, fty, fn, av, 1, "simd.floor");
            }

            if (strcmp(sname, "__simd_bitcast") == 0)
            {
                /* Reinterpret the lane bits (i32 <-> f32, same total width). */
                LLVMValueRef v = codegen_expr(ctx, sa[0]);
                if (v == NULL) return NULL;
                LLVMTypeRef vt = type_to_llvm(ctx, rt);  /* <N x U> */
                return LLVMBuildBitCast(ctx->builder, v, vt, "simd.bitcast");
            }

            cg_error(ctx, node->line, node->column,
                     "internal: unknown simd intrinsic '%s'", sname);
            return NULL;
        }

        /* Detect struct method calls: obj.method(args) or StructName.method(args).
           The checker has already validated and set resolved_type on the callee. */
        bool cg_is_method_call = false; /* instance method: prepend self */
        if (node->as.call.callee->kind == AST_FIELD)
        {
            AstNode *obj_node = node->as.call.callee->as.field_access.object;
            Type *obj_type = obj_node->resolved_type;
            if (obj_type)
            {
                /* Auto-deref a pointer (*T) or reference (&T / &!T) receiver to
                   its pointee struct/enum so a method call whose receiver is a
                   borrow-returning call result — `v.get_ref(i).eq?(x)` where
                   get_ref returns &T — dispatches as an instance method (self
                   prepended / qualified symbol resolved). Mirrors the checker. */
                Type *deref = obj_type;
                if ((deref->kind == TYPE_POINTER ||
                     deref->kind == TYPE_REFERENCE) && deref->as.pointer_to &&
                    (deref->as.pointer_to->kind == TYPE_STRUCT ||
                     deref->as.pointer_to->kind == TYPE_ENUM))
                {
                    deref = deref->as.pointer_to;
                }
                if (deref->kind == TYPE_STRUCT)
                {
                    /* Check if the callee resolved_type is a function (method) */
                    Type *callee_rt = node->as.call.callee->resolved_type;
                    if (callee_rt && callee_rt->kind == TYPE_FUNCTION)
                    {
                        /* Instance method: first param is *Struct (self) */
                        int nparams = callee_rt->as.function.param_count;
                        if (nparams > 0 && callee_rt->as.function.params &&
                            callee_rt->as.function.params[0]->kind == TYPE_POINTER &&
                            callee_rt->as.function.params[0]->as.pointer_to &&
                            callee_rt->as.function.params[0]->as.pointer_to->kind == TYPE_STRUCT)
                        {
                            cg_is_method_call = true;
                        }
                    }
                }
                else if (deref->kind == TYPE_ENUM)
                {
                    Type *callee_rt = node->as.call.callee->resolved_type;
                    if (callee_rt && callee_rt->kind == TYPE_FUNCTION)
                    {
                        int nparams = callee_rt->as.function.param_count;
                        if (nparams > 0 && callee_rt->as.function.params &&
                            callee_rt->as.function.params[0]->kind == TYPE_POINTER &&
                            callee_rt->as.function.params[0]->as.pointer_to &&
                            callee_rt->as.function.params[0]->as.pointer_to->kind == TYPE_ENUM)
                        {
                            cg_is_method_call = true;
                        }
                    }
                }
                /* Step 11: builtin types (int, f64, ...) with trait methods.
                   Only for known primitive scalar types — not modules, enums, etc.
                   (Includes the sized int / f32 scalars so e.g. an i16 receiver's
                   .show()/.to_value() prepends self like int's does.) */
                else if (deref->kind == TYPE_INT  || deref->kind == TYPE_I64 ||
                         deref->kind == TYPE_F64  || deref->kind == TYPE_BOOL ||
                         deref->kind == TYPE_CHAR ||
                         deref->kind == TYPE_I8   || deref->kind == TYPE_I16 ||
                         deref->kind == TYPE_I32  || deref->kind == TYPE_U8 ||
                         deref->kind == TYPE_U16  || deref->kind == TYPE_U32 ||
                         deref->kind == TYPE_U64  || deref->kind == TYPE_F32)
                {
                    Type *callee_rt = node->as.call.callee->resolved_type;
                    if (callee_rt && callee_rt->kind == TYPE_FUNCTION)
                    {
                        int nparams = callee_rt->as.function.param_count;
                        if (nparams > 0 && callee_rt->as.function.params &&
                            callee_rt->as.function.params[0]->kind == TYPE_POINTER)
                        {
                            cg_is_method_call = true;
                        }
                    }
                }
            }
        }

        LLVMValueRef callee = NULL;
        LLVMTypeRef fn_type = NULL;
        const char *fn_name = "call";

        /* Struct method call (instance or static) — resolve by qualified name */
        if (node->as.call.callee->kind == AST_FIELD &&
            node->as.call.callee->resolved_type &&
            node->as.call.callee->resolved_type->kind == TYPE_FUNCTION)
        {
            AstNode *obj_node = node->as.call.callee->as.field_access.object;
            Type *obj_type = obj_node->resolved_type;
            /* Check if it's a struct instance or struct type method (not module) */
            bool is_struct_method = false;
            const char *struct_name = NULL;
            if (obj_type)
            {
                /* Auto-deref a pointer (*T) or reference (&T / &!T) receiver to
                   its pointee struct/enum so a method call whose receiver is a
                   borrow-returning call result — `v.get_ref(i).eq?(x)` where
                   get_ref returns &T — dispatches as an instance method (self
                   prepended / qualified symbol resolved). Mirrors the checker. */
                Type *deref = obj_type;
                if ((deref->kind == TYPE_POINTER ||
                     deref->kind == TYPE_REFERENCE) && deref->as.pointer_to &&
                    (deref->as.pointer_to->kind == TYPE_STRUCT ||
                     deref->as.pointer_to->kind == TYPE_ENUM))
                {
                    deref = deref->as.pointer_to;
                }
                if (deref->kind == TYPE_STRUCT)
                {
                    is_struct_method = true;
                    /* B-2: use LLVM-prefixed name to find the correct method */
                    struct_name = struct_llvm_name(deref);
                }
                else if (deref->kind == TYPE_ENUM)
                {
                    is_struct_method = true;
                    /* B-2: use LLVM-prefixed name to find the correct enum method */
                    struct_name = enum_llvm_name_of(deref);
                }
                /* Step 11: builtin type method — use type name as prefix */
                else
                {
                    const char *bname = NULL;
                    switch (deref->kind) {
                    case TYPE_INT:   bname = "int";    break;
                    case TYPE_I64:   bname = "i64";    break;
                    case TYPE_F64:   bname = "f64";    break;
                    case TYPE_BOOL:  bname = "bool";   break;
                    case TYPE_CHAR:  bname = "char";   break;
                    case TYPE_I8:    bname = "i8";     break;
                    case TYPE_I16:   bname = "i16";    break;
                    case TYPE_I32:   bname = "i32";    break;
                    case TYPE_U8:    bname = "u8";     break;
                    case TYPE_U16:   bname = "u16";    break;
                    case TYPE_U32:   bname = "u32";    break;
                    case TYPE_U64:   bname = "u64";    break;
                    case TYPE_F32:   bname = "f32";    break;
                    default: break;
                    }
                    if (bname)
                    {
                        is_struct_method = true;
                        struct_name = bname;
                    }
                }
            }
            /* Also detect static call via type name (obj_type may be NULL for bare struct name) */
            if (!is_struct_method && obj_node->kind == AST_IDENT && !obj_type)
            {
                is_struct_method = true;
                const char *bare_sname = obj_node->as.ident.name;
                /* B-2: if the struct was module-defined, its LLVM name is prefixed.
                   Search the registry by bare name (ls_type->as.strukt.name) to find
                   the correct LLVM name for method dispatch. */
                const char *resolved_sname = bare_sname;
                for (int si = 0; si < ctx->struct_type_count; si++)
                {
                    Type *slt = ctx->struct_types[si].ls_type;
                    if (slt && slt->kind == TYPE_STRUCT &&
                        slt->as.strukt.name &&
                        strcmp(slt->as.strukt.name, bare_sname) == 0 &&
                        slt->as.strukt.llvm_name != NULL)
                    {
                        resolved_sname = slt->as.strukt.llvm_name;
                        break;
                    }
                }
                struct_name = resolved_sname;
            }

            if (is_struct_method)
            {
                const char *method_name = node->as.call.callee->as.field_access.field;
                /* Build qualified name: StructName.method_name
                   G3: when call site provides type args (method-level generics
                   like obj.map(string)(...)), append them: StructName.method(type)
                   L-002: an interface-qualified call to a CONTENDED method carries
                   node.qualified_iface — emit `StructName.<Iface>.method` to match
                   the disambiguated symbol from codegen_impl_trait_decl. (Non-
                   contended qualified calls have qualified_iface==NULL → plain.) */
                static char qualified_name[512];
                int npos;
                if (node->as.call.qualified_iface)
                    npos = snprintf(qualified_name, sizeof(qualified_name), "%s.%s.%s",
                                    struct_name ? struct_name : "",
                                    node->as.call.qualified_iface, method_name);
                else
                    npos = snprintf(qualified_name, sizeof(qualified_name), "%s.%s",
                                    struct_name ? struct_name : "", method_name);
                if (node->as.call.resolved_type_args)
                {
                    /* Prefer the checker's resolved method-level type-arg names
                       (concrete, alias-resolved): closure-inferred calls
                       (`v.map(|x| x+1)`) carry no type_args, AND an explicit call
                       with an abstract type param inside a generic body
                       (`self.conv(T)(..)`) needs this too — codegen has no alias
                       context, so re-mangling the raw TypeNode would emit the
                       abstract `Type.conv(T)` instead of `Type.conv(int)`. */
                    npos += snprintf(qualified_name + npos, sizeof(qualified_name) - (size_t)npos,
                                     "(%s)", node->as.call.resolved_type_args);
                }
                else if (node->as.call.type_arg_count > 0)
                {
                    npos += snprintf(qualified_name + npos, sizeof(qualified_name) - (size_t)npos, "(");
                    for (int ti = 0; ti < node->as.call.type_arg_count; ti++)
                    {
                        if (ti > 0)
                            npos += snprintf(qualified_name + npos, sizeof(qualified_name) - (size_t)npos, ",");
                        cg_append_type_node_name(node->as.call.type_args[ti],
                                                 qualified_name, &npos, (int)sizeof(qualified_name));
                    }
                    snprintf(qualified_name + npos, sizeof(qualified_name) - (size_t)npos, ")");
                }
                fn_name = qualified_name;
                callee = LLVMGetNamedFunction(ctx->module, fn_name);
                /* Step 0 (cross-module generics): a generic struct method
                   (e.g. Stack(int).push) can be referenced from a *module*
                   function body emitted in Pass B, before the pending-gm
                   forward-declaration block (~L20815) runs. Resolve it on demand
                   by forward-declaring the matching pending entry — the same
                   fallback the free-function generic path uses (~L11237). The
                   body is still emitted later, guarded against duplication. */
                if (callee == NULL)
                {
                    for (int gi = 0; gi < ctx->pending_gm_count; gi++)
                    {
                        if (strcmp(ctx->pending_generic_methods[gi].mangled_name,
                                   fn_name) != 0)
                            continue;
                        AstNode *cfn = ctx->pending_generic_methods[gi].cloned_fn;
                        if (cfn == NULL || cfn->resolved_type == NULL ||
                            cfn->resolved_type->kind != TYPE_FUNCTION)
                            break;
                        LLVMTypeRef gft = type_to_llvm(ctx, cfn->resolved_type);
                        callee = LLVMAddFunction(ctx->module, fn_name, gft);
                        break;
                    }
                }
                /* Phase 2.5: builtin-type extension methods (e.g. string.split)
                   live in a stdlib module that may be emitted AFTER the caller
                   (transitive import order). Forward-declare from the call site's
                   resolved method type; the body reuses this decl when std.string
                   is processed. Same pattern as the generic-method fallback. */
                if (callee == NULL)
                {
                    Type *crt = node->as.call.callee->resolved_type;
                    if (crt && crt->kind == TYPE_FUNCTION)
                    {
                        LLVMTypeRef bft = type_to_llvm(ctx, crt);
                        callee = LLVMAddFunction(ctx->module, fn_name, bft);
                    }
                }
                if (callee == NULL)
                {
                    cg_error(ctx, node->line, node->column,
                             "undefined method '%s'", fn_name);
                    return NULL;
                }
                fn_type = LLVMGlobalGetValueType(callee);
            }
        }

        if (callee == NULL)
        {
            /* Direct function call by name */
            if (node->as.call.callee->kind == AST_IDENT)
            {
                fn_name = node->as.call.callee->as.ident.name;

                /* G2: generic function call — look up by mangled name.
                   Explicit type args (`identity(int)(42)`) build the suffix from
                   node->type_args; inferred calls (`to_csv(p)`, Gap 1) carry no
                   type_args — the checker stashed the resolved type-arg names in
                   resolved_type_args (same mechanism as method-generic inference). */
                if (node->as.call.type_arg_count > 0 ||
                    node->as.call.resolved_type_args != NULL)
                {
                    static char g2_mangled[512];
                    int pos = snprintf(g2_mangled, sizeof(g2_mangled), "%s(", fn_name);
                    /* Prefer the checker's resolved_type_args (concrete, alias-
                       resolved, type_name-built — byte-identical to the symbol the
                       checker registered). Re-mangling from the raw TypeNodes is the
                       legacy fallback only when resolved_type_args is absent: codegen
                       has no type-alias context, so a `make(T)(..)` call inside a
                       generic body would otherwise emit the abstract `make(T)`
                       instead of the instantiated `make(int)`. */
                    if (node->as.call.resolved_type_args != NULL)
                    {
                        pos += snprintf(g2_mangled + pos, sizeof(g2_mangled) - (size_t)pos,
                                        "%s", node->as.call.resolved_type_args);
                    }
                    else
                    {
                        for (int ti = 0; ti < node->as.call.type_arg_count; ti++)
                        {
                            if (ti > 0) g2_mangled[pos++] = ',';
                            cg_append_type_node_name(node->as.call.type_args[ti],
                                                     g2_mangled, &pos, (int)sizeof(g2_mangled));
                        }
                    }
                    g2_mangled[pos++] = ')';
                    g2_mangled[pos] = '\0';
                    /* A2: when this generic call is emitted inside an imported
                       module, prefix the instantiation symbol with the module
                       (matching the checker's owned_mangled) so two modules'
                       same-named generics resolve to distinct functions. Root
                       module (current_emit_module==NULL) stays unprefixed. */
                    if (ctx->current_emit_module != NULL)
                    {
                        static char g2_mod[640];
                        cg_module_fn_symbol(g2_mod, sizeof(g2_mod),
                                            ctx->current_emit_module, g2_mangled);
                        fn_name = g2_mod;
                    }
                    else
                    {
                        fn_name = g2_mangled;
                    }
                }

                /* L-009: a bare call inside an imported module resolves to that
                   module's own function first (module-prefixed symbol); fall
                   back to the unmangled name (builtins, runtime, root funcs).
                   Skip for generic calls, which carry their own mangled name. */
                if (node->as.call.type_arg_count == 0 &&
                    node->as.call.resolved_type_args == NULL &&
                    ctx->current_emit_module != NULL)
                {
                    char msym[512];
                    cg_module_fn_symbol(msym, sizeof(msym),
                                        ctx->current_emit_module, fn_name);
                    callee = LLVMGetNamedFunction(ctx->module, msym);
                }
                if (callee == NULL)
                    callee = LLVMGetNamedFunction(ctx->module, fn_name);
                /* A1 (module generics): a generic instantiation (e.g.
                   identity(int)) may be referenced from a module function body
                   emitted in Pass B, before the pending-gm forward-declaration
                   block runs. Resolve it on demand: find the matching pending
                   entry by mangled name and forward-declare its signature now.
                   The body is still emitted later (with a dedup guard). */
                if (callee == NULL && (node->as.call.type_arg_count > 0 ||
                                       node->as.call.resolved_type_args != NULL))
                {
                    for (int gi = 0; gi < ctx->pending_gm_count; gi++)
                    {
                        if (strcmp(ctx->pending_generic_methods[gi].mangled_name,
                                   fn_name) != 0)
                            continue;
                        AstNode *cfn = ctx->pending_generic_methods[gi].cloned_fn;
                        if (cfn == NULL || cfn->resolved_type == NULL ||
                            cfn->resolved_type->kind != TYPE_FUNCTION)
                            break;
                        LLVMTypeRef gft = type_to_llvm(ctx, cfn->resolved_type);
                        callee = LLVMAddFunction(ctx->module, fn_name, gft);
                        break;
                    }
                }
                if (callee == NULL)
                {
                    cg_error(ctx, node->line, node->column,
                             "undefined function '%s'", fn_name);
                    return NULL;
                }
                fn_type = LLVMGlobalGetValueType(callee);
            }
            /* Module-qualified function call (e.g., math.add(...)) */
            else if (node->as.call.callee->kind == AST_FIELD &&
                     node->as.call.callee->as.field_access.object->resolved_type &&
                     node->as.call.callee->as.field_access.object->resolved_type->kind == TYPE_MODULE)
            {
                Type *mod_t = node->as.call.callee->as.field_access.object->resolved_type;
                fn_name = node->as.call.callee->as.field_access.field;

                /* Built-in stdlib module: dispatch to the intrinsic emitter,
                   which produces an LLVM intrinsic call (e.g. @llvm.sqrt.f64)
                   or a direct libm call. Bypasses the normal LLVMGetNamedFunction
                   lookup since built-in functions have no AST/IR body. */
                if (mod_t->as.module.is_builtin &&
                    mod_t->as.module.name &&
                    strcmp(mod_t->as.module.name, "std.core.math") == 0)
                {
                    /* Primitive (sqrt/sin/abs/...) → intrinsic/libm emit. The
                       LS-derived helpers merged into math (radians/degrees/...)
                       are NOT in the builtin table, so emit returns NULL — fall
                       through to the normal module-fn symbol path
                       (std_core_math__<fn>), which codegen emitted for the
                       lib/std/core/math.ls module (registered "std.core.math"). */
                    LLVMValueRef mv = builtin_math_emit_call(ctx, fn_name,
                                                  node->as.call.args,
                                                  node->as.call.arg_count);
                    if (mv != NULL) return mv;
                }
                if (mod_t->as.module.is_builtin &&
                    mod_t->as.module.name &&
                    strcmp(mod_t->as.module.name, "perf") == 0)
                {
                    return builtin_perf_emit_call(ctx, fn_name,
                                                  node->as.call.args,
                                                  node->as.call.arg_count);
                }
                /* Phase E.4: io has been migrated to pure-LS stdlib/io.ls.
                   `import io` now goes through the normal user-module path
                   (is_builtin == false). No special dispatch here. */

                /* L-009: the callee lives in module `mod_t->name`; look it up by
                   its module-prefixed symbol. Fall back to the bare name for
                   robustness (e.g. legacy/edge cases). */
                if (mod_t->as.module.name)
                {
                    char msym[512];
                    cg_module_fn_symbol(msym, sizeof(msym),
                                        mod_t->as.module.name, fn_name);
                    callee = LLVMGetNamedFunction(ctx->module, msym);
                }
                if (callee == NULL)
                    callee = LLVMGetNamedFunction(ctx->module, fn_name);
                if (callee == NULL)
                {
                    cg_error(ctx, node->line, node->column,
                             "undefined function '%s' in module", fn_name);
                    return NULL;
                }
                fn_type = LLVMGlobalGetValueType(callee);
            }
            else
            {
                /* Indirect call (function pointer) */
                callee = codegen_expr(ctx, node->as.call.callee);
                if (callee == NULL)
                    return NULL;
                Type *ct = node->as.call.callee->resolved_type;
                if (ct && ct->kind == TYPE_FUNCTION)
                {
                    fn_type = type_to_llvm(ctx, ct);
                }
                else
                {
                    cg_error(ctx, node->line, node->column, "cannot call non-function");
                    return NULL;
                }
            }
        }

        /* Build args */
        int user_argc = node->as.call.arg_count;

        /* Phase E.2: detect sret prepending. If callee is an extern fn
           that returns a large extern struct (LLVM signature returns void
           with a hidden sret pointer first arg), allocate the return slot
           upfront and reserve args[0] for it. arg_offset is bumped so the
           existing struct/string/method fixups naturally use correct LLVM
           parameter indices via LLVMGetParam(callee, slot). */
        Type *_e2_callee_lst = node->as.call.callee
                               ? node->as.call.callee->resolved_type : NULL;
        if (_e2_callee_lst && _e2_callee_lst->kind != TYPE_FUNCTION)
            _e2_callee_lst = NULL;
        bool _e2_needs_sret = false;
        LLVMValueRef sret_slot = NULL;
        if (_e2_callee_lst)
        {
            Type *rt = _e2_callee_lst->as.function.return_type;
            if (rt && rt->kind == TYPE_STRUCT && rt->as.strukt.is_extern_c)
            {
                int sz = extern_struct_size(ctx, rt);
                if (sz > 0 && !extern_struct_fits_in_reg(sz)
                    && LLVMGetTypeKind(LLVMGetReturnType(fn_type)) == LLVMVoidTypeKind)
                {
                    _e2_needs_sret = true;
                }
            }
        }
        int sret_off = _e2_needs_sret ? 1 : 0;

        int total_argc = (cg_is_method_call ? user_argc + 1 : user_argc) + sret_off;
        LLVMValueRef *args = NULL;

        if (total_argc > 0)
        {
            args = (LLVMValueRef *)malloc_safe((size_t)total_argc * sizeof(LLVMValueRef));
            int arg_offset = 0;

            /* Phase E.2: sret slot occupies args[0] before any user args */
            if (_e2_needs_sret)
            {
                LLVMTypeRef st_lt = type_to_llvm(ctx, _e2_callee_lst->as.function.return_type);
                sret_slot = cg_entry_alloca(ctx, st_lt, "sret.slot");
                args[0] = sret_slot;
                arg_offset = 1;
            }

            /* For instance method call, prepend self (pointer to obj) */
            if (cg_is_method_call)
            {
                AstNode *obj_node = node->as.call.callee->as.field_access.object;
                /* codegen_addr_of handles AST_IDENT, AST_FIELD (nested), etc. */
                LLVMValueRef self_ptr = codegen_addr_of(ctx, obj_node);
                if (self_ptr == NULL)
                {
                    /* Phase 2.5: rvalue receiver (string literal, call result, …)
                       for a pointer-self method. Evaluate it and spill to a temp
                       alloca so we can pass its address — needed by `impl string`
                       methods like "a,b".split(","). Only sound for read-only
                       (&self): a writable receiver rvalue is meaningless. */
                    LLVMValueRef self_val = codegen_expr(ctx, obj_node);
                    Type *ort = obj_node->resolved_type;
                    if (self_val != NULL && ort != NULL)
                    {
                        LLVMTypeRef slt = type_to_llvm(ctx, ort);
                        self_ptr = cg_entry_alloca(ctx, slt, "self.spill");
                        LLVMBuildStore(ctx->builder, self_val, self_ptr);
                    }
                }
                if (self_ptr == NULL)
                {
                    cg_error(ctx, node->line, node->column,
                             "cannot take address of object for method call");
                    free(args);
                    return NULL;
                }
                args[arg_offset] = self_ptr;
                arg_offset += 1;
            }

            /* Lookup callee's LS function type so we can widen each arg to its
               declared parameter type when needed. May be NULL for indirect
               calls / FFI / vararg slots. */
            Type *callee_fn_lst = node->as.call.callee
                                  ? node->as.call.callee->resolved_type
                                  : NULL;
            if (callee_fn_lst && callee_fn_lst->kind != TYPE_FUNCTION)
                callee_fn_lst = NULL;

            for (int i = 0; i < user_argc; i++)
            {
                LLVMValueRef arg_val = NULL;
                /* Read-only &T borrow of a stable place (struct field / array or
                   *T element): take its lvalue address directly instead of
                   codegen_expr'ing a by-value CLONE. The §13 amp-strip turned an
                   explicit `&d.field` into the bare place `d.field` (and bare
                   `d.field` against a `&T` param auto-borrows the same way);
                   reading a has_drop struct/enum field by value deep-clones it
                   (emit_struct_clone_val at the AST_FIELD read site), and that
                   clone — registered for drop by the struct/enum arg fixup below —
                   is flushed at the loop-enclosing scope, so in a loop only the
                   last iteration's clone is freed and the earlier ones leak.
                   Borrowing in place via GEP avoids the clone entirely; the callee
                   only reads through the &T. codegen_lvalue_ptr returns NULL for
                   non-lvalue roots (e.g. `make().field`) and Vec `v[i]` (no stable
                   address), falling back to the clone path. AST_IDENT (`&d` whole
                   struct) is already clean — its codegen_expr is a plain load (DCEs)
                   and the fixup substitutes sym->value — so it is left untouched.
                   Mirrors codegen_block_call's &T field/element borrow. */
                bool arg_inplace_borrow = false;
                {
                    int pslot = i + arg_offset - sret_off;
                    Type *pt = (callee_fn_lst && pslot >= 0 &&
                                pslot < callee_fn_lst->as.function.param_count)
                               ? callee_fn_lst->as.function.params[pslot] : NULL;
                    AstNode *an = node->as.call.args[i];
                    if (pt && pt->kind == TYPE_REFERENCE && !pt->is_mut &&
                        pt->as.pointer_to &&
                        (pt->as.pointer_to->kind == TYPE_STRUCT ||
                         pt->as.pointer_to->kind == TYPE_ENUM) &&
                        (an->kind == AST_FIELD || an->kind == AST_INDEX))
                    {
                        LLVMValueRef addr = codegen_lvalue_ptr(ctx, an);
                        if (addr != NULL)
                        {
                            arg_val = addr;
                            arg_inplace_borrow = true;
                        }
                    }
                }
                if (!arg_inplace_borrow)
                    arg_val = codegen_expr(ctx, node->as.call.args[i]);
                if (arg_val == NULL)
                {
                    free(args);
                    return NULL;
                }
                /* In-place borrow already produced the pointer ABI value: skip the
                   numeric widening + by-value ownership-clone policy below (none
                   apply to a borrowed pointer) and the struct/enum fixup pass (the
                   guard there sees args[i] is already a pointer). */
                if (arg_inplace_borrow)
                {
                    args[i + arg_offset] = arg_val;
                    continue;
                }
                /* Implicit numeric widening per Zig-style rules: if param's
                   declared type differs from arg's type AND it is a permitted
                   widening, emit the conversion (sext/zext/sitofp/uitofp/fpext).
                   The checker has already validated assignability. */
                {
                    Type *arg_t = node->as.call.args[i]->resolved_type;
                    /* Phase E.2: callee_fn_lst (LS-side type) does NOT include
                       a sret param, so subtract sret_off from the LLVM slot
                       when indexing into the LS function's params. */
                    int slot = i + arg_offset - sret_off;
                    if (callee_fn_lst && slot >= 0 && slot < callee_fn_lst->as.function.param_count)
                    {
                        Type *param_t = callee_fn_lst->as.function.params[slot];
                        if (arg_t && param_t && type_is_numeric(arg_t) &&
                            type_is_numeric(param_t) && !type_equals(arg_t, param_t))
                        {
                            arg_val = cg_widen(ctx, arg_val, arg_t, param_t);
                        }
                    }
                }
                /* Phase E.1 note: with by-ref vec/map capture semantics, the
                   closure body's captured sym->value IS the outer alloca pointer.
                   Loading from it gives the outer's {data, len, cap}. Passing to
                   a value-ABI fn: callee marks its vec/map param as is_borrowed
                   (Phase C.7.4 fix) so the callee does NOT free the data — the
                   outer's scope cleanup does it. No clone needed; no double-free. */
                /* Argument ownership policy for struct-with-drop:
                     - default (`take(p)`):          deep-clone; caller retains p
                     - explicit (`take(__move(p))`): skip clone, transfer ownership;
                       mark caller's p as moved so its scope cleanup is suppressed
                       (callee drops the shared heap).
                   AST_FIELD / struct-literal args are already cloned at the read site
                   and never reach the move path. */
                Type *arg_type = node->as.call.args[i]->resolved_type;
                if (arg_val && arg_type && arg_type->kind == TYPE_STRUCT &&
                    arg_type->as.strukt.has_drop)
                {
                    /* Phase B: if callee param is pointer ABI (&Struct / &!Struct),
                       skip clone — fixup pass below replaces with sym->value. */
                    bool callee_takes_ptr = false;
                    {
                        unsigned pc2 = LLVMCountParams(callee);
                        unsigned slot = (unsigned)(i + arg_offset);
                        if (slot < pc2) {
                            LLVMTypeRef pt = LLVMTypeOf(LLVMGetParam(callee, slot));
                            if (LLVMGetTypeKind(pt) == LLVMPointerTypeKind)
                                callee_takes_ptr = true;
                        }
                    }
                    if (!callee_takes_ptr)
                    {
                        AstNode *raw = node->as.call.args[i];
                        AstNode *unwrapped = ast_unwrap_move(raw);
                        bool is_move_expr = (raw != unwrapped);
                        if (unwrapped->kind == AST_IDENT)
                        {
                            if (is_move_expr)
                            {
                                /* Suppress caller-side drop — callee now owns the heap. */
                                CgSymbol *argsym = cg_scope_resolve(ctx->current_scope,
                                                                    unwrapped->as.ident.name);
                                if (argsym && argsym->moved_flag)
                                {
                                    LLVMTypeRef i1_t = LLVMInt1TypeInContext(ctx->context);
                                    LLVMBuildStore(ctx->builder,
                                                   LLVMConstInt(i1_t, 1, 0),
                                                   argsym->moved_flag);
                                }
                            }
                            else if (unwrapped->moved_out &&
                                     cg_invalidate_moved_source(ctx, raw, arg_type))
                            {
                                /* A1 clone-elision: the checker's last-use pass
                                   proved this argument is the variable's final
                                   use — transfer the heap instead of cloning.
                                   cg_invalidate_moved_source suppressed the
                                   caller-side scope drop; when it can't (borrow /
                                   no moved_flag) it returns false and the clone
                                   below keeps the old behavior (§3.2 safety net). */
#if CG_DEBUG
                                cg_emit_debug_printf(ctx, "[cg] elide.arg.move struct\n",
                                                     NULL, 0);
#endif
                            }
                            else
                            {
                                LLVMTypeRef llvm_st = type_to_llvm(ctx, arg_type);
                                arg_val = emit_struct_clone_val(ctx, arg_val, llvm_st, arg_type);
                            }
                        }
                    }
                }
                /* Argument ownership policy for enum-with-drop (mirrors struct above):
                     - default: deep-clone so caller retains its copy
                     - __move(e): skip clone, transfer ownership; mark caller's moved_flag */
                else if (arg_val && arg_type && arg_type->kind == TYPE_ENUM &&
                         arg_type->as.enom.has_drop)
                {
                    bool callee_takes_ptr = false;
                    {
                        unsigned pc2 = LLVMCountParams(callee);
                        unsigned slot = (unsigned)(i + arg_offset);
                        if (slot < pc2) {
                            LLVMTypeRef pt = LLVMTypeOf(LLVMGetParam(callee, slot));
                            if (LLVMGetTypeKind(pt) == LLVMPointerTypeKind)
                                callee_takes_ptr = true;
                        }
                    }
                    if (!callee_takes_ptr)
                    {
                        AstNode *raw      = node->as.call.args[i];
                        AstNode *unwrapped = ast_unwrap_move(raw);
                        bool is_move_expr  = (raw != unwrapped);
                        if (unwrapped->kind == AST_IDENT)
                        {
                            if (is_move_expr)
                            {
                                CgSymbol *argsym = cg_scope_resolve(ctx->current_scope,
                                                                    unwrapped->as.ident.name);
                                if (argsym && argsym->moved_flag)
                                {
                                    LLVMTypeRef i1_t = LLVMInt1TypeInContext(ctx->context);
                                    LLVMBuildStore(ctx->builder,
                                                   LLVMConstInt(i1_t, 1, 0),
                                                   argsym->moved_flag);
                                }
                            }
                            else if (unwrapped->moved_out &&
                                     cg_invalidate_moved_source(ctx, raw, arg_type))
                            {
                                /* A1 clone-elision: last use — move, not clone
                                   (mirrors the struct branch above; §3.2 safety
                                   net falls back to the clone when the source
                                   can't be invalidated). */
#if CG_DEBUG
                                cg_emit_debug_printf(ctx, "[cg] elide.arg.move enum\n",
                                                     NULL, 0);
#endif
                            }
                            else
                            {
                                arg_val = emit_enum_clone_val(ctx, arg_val, arg_type);
                            }
                        }
                    }
                }
                /* F5 (VR-LIM-017): a Block moved into a container-STORING method
                   (push/insert/set/__index_set/__from_list/extend) — the
                   container takes ownership of the closure env, so suppress the
                   caller-side free: a closure-literal temp → consume its temp env;
                   a named Block var → null its env_ptr (move). Non-storing methods
                   (each/map/filter that only CALL the block) keep the borrow — the
                   caller still owns and frees its temp. Mirrors map.set's Block
                   handling. Without this the caller frees an env the container now
                   owns → dangling element (UAF on later get; AOT crash). */
                else if (arg_val && arg_type && arg_type->kind == TYPE_BLOCK)
                {
                    const char *mname =
                        (node->as.call.callee->kind == AST_FIELD)
                            ? node->as.call.callee->as.field_access.field : NULL;
                    bool stores = mname &&
                        (strcmp(mname, "push") == 0 || strcmp(mname, "insert") == 0 ||
                         strcmp(mname, "set") == 0 || strcmp(mname, "__index_set") == 0 ||
                         strcmp(mname, "__from_list") == 0 || strcmp(mname, "extend") == 0);
                    /* std.task: `t.run(|| ..)` forwards the closure into the worker
                       thread (via __task_spawn), which frees the env once the body
                       runs — so the caller must NOT also free it. Consume the temp
                       env here, but ONLY for a Task receiver, so an unrelated method
                       named `run` that merely BORROWS a closure is unaffected. */
                    if (!stores && mname && strcmp(mname, "run") == 0)
                    {
                        AstNode *recv = node->as.call.callee->as.field_access.object;
                        Type *rt = recv ? recv->resolved_type : NULL;
                        if (rt && rt->kind == TYPE_STRUCT &&
                            rt->as.strukt.generic_base &&
                            strcmp(rt->as.strukt.generic_base, "Task") == 0)
                            stores = true;
                    }
                    if (stores)
                    {
                        AstNode *raw = node->as.call.args[i];
                        AstNode *uw = ast_unwrap_move(raw);
                        if (uw && uw->kind == AST_IDENT)
                        {
                            CgSymbol *bsym = cg_scope_resolve(ctx->current_scope,
                                                              uw->as.ident.name);
                            if (bsym && !bsym->is_borrowed)
                                cg_null_block_env(ctx, bsym->value);
                        }
                        else if (ctx->temp_block_env_count > 0)
                            ctx->temp_block_env_count--;
                    }
                }
                args[i + arg_offset] = arg_val;
            }

            /* Phase 5.6/5.7/5.8: vec/map/struct arg fixup — when the callee's
               formal parameter is `&T` / `&!T` (pointer ABI) but the argument
               expression produced a by-value aggregate (auto-borrow from an
               owned local), replace with the underlying address. AST_MUT_BORROW
               already returns the pointer directly — its resolved_type is
               TYPE_REFERENCE so it never hits this branch. */
            {
                unsigned pc = LLVMCountParams(callee);
                for (int i = arg_offset; i < total_argc; i++)
                {
                    int user_i = i - arg_offset;
                    Type *arg_type = node->as.call.args[user_i]->resolved_type;
                    if (!(arg_type && (arg_type->kind == TYPE_STRUCT ||
                                       arg_type->kind == TYPE_ENUM))) continue;
                    if ((unsigned)i >= pc) continue;
                    LLVMTypeRef param_llvm = LLVMTypeOf(LLVMGetParam(callee, (unsigned)i));
                    if (LLVMGetTypeKind(param_llvm) != LLVMPointerTypeKind) continue;
                    /* Already a pointer: an in-place &T borrow whose lvalue address
                       was taken in the arg loop above (struct/enum field/element).
                       A by-value struct/enum arg is always an LLVM aggregate value
                       here, never a pointer, so this only skips the pre-lowered
                       borrows — storing the pointer into a struct temp would be a
                       type mismatch. */
                    if (LLVMGetTypeKind(LLVMTypeOf(args[i])) == LLVMPointerTypeKind)
                        continue;
                    /* Need to pass pointer. Prefer the original alloca of an IDENT. */
                    AstNode *a = node->as.call.args[user_i];
                    if (a->kind == AST_IDENT)
                    {
                        CgSymbol *sym = cg_scope_resolve(ctx->current_scope,
                                                         a->as.ident.name);
                        if (sym != NULL)
                        {
                            args[i] = sym->value;
                            continue;
                        }
                    }
                    /* Fallback: materialise a temporary alloca to stabilise addr. */
                    LLVMTypeRef store_t;
                    const char *tmp_name;
                    if (arg_type->kind == TYPE_ENUM) {
                        store_t = type_to_llvm(ctx, arg_type);
                        tmp_name = "enum.borrow.tmp";
                    } else {
                        store_t = type_to_llvm(ctx, arg_type);
                        tmp_name = "struct.borrow.tmp";
                    }
                    LLVMValueRef tmp = cg_entry_alloca(ctx, store_t, tmp_name);
                    LLVMBuildStore(ctx->builder, args[i], tmp);
                    args[i] = tmp;
                    /* Phase B: for owned rvalue (non-IDENT, non-vec[i]) passed as &T,
                       the callee borrows but doesn't own — register the temp for drop
                       so the heap inside is released after the statement. */
                    AstNode *aa = node->as.call.args[i - arg_offset];
                    bool is_rvalue_temp = (aa->kind != AST_IDENT);
                    if (is_rvalue_temp &&
                        ((arg_type->kind == TYPE_ENUM   && arg_type->as.enom.has_drop) ||
                         (arg_type->kind == TYPE_STRUCT && arg_type->as.strukt.has_drop)))
                    {
                        cg_push_temp_drop(ctx, tmp, arg_type);
                    }
                }
            }

        }

        /* ===== Phase E.2: small extern-struct args lowered to iN =====
           Large extern-struct args naturally ride the existing struct→pointer
           fixup pass (LLVM param is pointer for byval). Small ones need an
           explicit struct→iN conversion since LLVM expects an integer. */
        if (_e2_callee_lst && _e2_callee_lst->kind == TYPE_FUNCTION && args)
        {
            unsigned llvm_pc = LLVMCountParams(callee);
            int e2_arg_off = sret_off + (cg_is_method_call ? 1 : 0);
            for (int i = 0; i < user_argc; i++)
            {
                Type *at = node->as.call.args[i]->resolved_type;
                if (!at || at->kind != TYPE_STRUCT || !at->as.strukt.is_extern_c)
                    continue;
                int sz = extern_struct_size(ctx, at);
                if (sz <= 0 || !extern_struct_fits_in_reg(sz)) continue;

                int slot = i + e2_arg_off;
                if ((unsigned)slot >= llvm_pc) continue;
                LLVMTypeRef ptype = LLVMTypeOf(LLVMGetParam(callee, (unsigned)slot));
                if (LLVMGetTypeKind(ptype) != LLVMIntegerTypeKind) continue;

                LLVMTypeRef int_t = extern_struct_reg_int_type(ctx, sz);
                /* args[slot] may be a struct value or already a pointer
                   (struct→ptr fixup may have converted it for some cases).
                   Disambiguate via current LLVM type. */
                LLVMTypeRef cur_t = LLVMTypeOf(args[slot]);
                if (LLVMGetTypeKind(cur_t) == LLVMPointerTypeKind)
                {
                    args[slot] = LLVMBuildLoad2(ctx->builder, int_t, args[slot],
                                                 "ext.arg.int");
                }
                else
                {
                    LLVMTypeRef st_lt = type_to_llvm(ctx, at);
                    LLVMValueRef tmp = cg_entry_alloca(ctx, st_lt,
                                                       "ext.arg.tmp");
                    LLVMBuildStore(ctx->builder, args[slot], tmp);
                    args[slot] = LLVMBuildLoad2(ctx->builder, int_t, tmp,
                                                 "ext.arg.int");
                }
            }
        }

        LLVMValueRef result = LLVMBuildCall2(ctx->builder, fn_type, callee,
                                             args, (unsigned)total_argc, "");

        /* Phase E.2: convert lowered return value back to struct. The sret
           slot prepending happened earlier inside the args build; here we
           only need to load the struct out of it (if sret) or bitcast iN
           back to struct (if small register return). */
        Type *ls_ret_ty = (_e2_callee_lst && _e2_callee_lst->kind == TYPE_FUNCTION)
                         ? _e2_callee_lst->as.function.return_type : NULL;
        bool ret_is_extern = ls_ret_ty && ls_ret_ty->kind == TYPE_STRUCT
                             && ls_ret_ty->as.strukt.is_extern_c;
        if (ret_is_extern)
        {
            LLVMTypeKind rk = LLVMGetTypeKind(LLVMGetReturnType(fn_type));
            int sz = extern_struct_size(ctx, ls_ret_ty);
            if (rk == LLVMVoidTypeKind && sz > 0 && !extern_struct_fits_in_reg(sz)
                && sret_slot != NULL)
            {
                LLVMTypeRef st_lt = type_to_llvm(ctx, ls_ret_ty);
                result = LLVMBuildLoad2(ctx->builder, st_lt, sret_slot,
                                        "ext.ret.sret");
            }
            else if (rk == LLVMIntegerTypeKind && sz > 0
                     && extern_struct_fits_in_reg(sz))
            {
                LLVMTypeRef st_lt = type_to_llvm(ctx, ls_ret_ty);
                LLVMValueRef slot = cg_entry_alloca(ctx, st_lt,
                                                    "ext.ret.slot");
                LLVMBuildStore(ctx->builder, result, slot);
                result = LLVMBuildLoad2(ctx->builder, st_lt, slot,
                                        "ext.ret.struct");
            }
        }
        else if (node->resolved_type && node->resolved_type->kind != TYPE_VOID)
        {
            /* If function returns void, we can't name the result */
            LLVMSetValueName2(result, "call", 4);
        }


        free(args);
        return result;
    }

    case AST_FIELD:
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
                /* Spill struct value to a temp alloca */
                LLVMTypeRef st_llvm = type_to_llvm(ctx, struct_type);
                struct_ptr = cg_entry_alloca(ctx, st_llvm, "tmp.struct");
                LLVMBuildStore(ctx->builder, sub_val, struct_ptr);

                /* M-4.5: when the object is vec[i]/arr[i] of a has_drop struct,
                   sub_val is an owned deep clone (the container keeps its own copy).
                   Field access reads one field; the rest of this temporary struct's
                   owned resources (other string fields, nested drops) would leak.
                   Register the spill slot so the statement-end flush drops it. The
                   accessed field is independently cloned below, so dropping the
                   temporary here does not invalidate the returned value. */
                /* Owned rvalue struct sources whose temp must be dropped after the
                   field read: container index (vec[i]/p[i]), a CALL returning a
                   has_drop struct by value (f().field / obj.method(i).field), AND a
                   nested field read whose own object had no stable lvalue
                   (`v[i].inner.field`): there the intermediate `v[i].inner` is itself
                   an owned struct clone (emit_struct_clone_val), so its other owned
                   fields would leak. Reaching this else-branch at all means obj_node
                   has no backing lvalue (codegen_lvalue_ptr returned NULL above) → the
                   spilled struct value is an owned rvalue → the accessed field is
                   cloned below, so dropping the temp is safe for any has_drop source. */
                AstNode *uobj_src = ast_unwrap_move(obj_node);
                if (struct_type->as.strukt.has_drop &&
                    (uobj_src->kind == AST_INDEX || uobj_src->kind == AST_CALL ||
                     uobj_src->kind == AST_FIELD))
                {
                    cg_push_temp_drop(ctx, struct_ptr, struct_type);
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
           the same string/owned-struct data → double-free. */
        if (field_type && field_type->kind == TYPE_STRUCT && field_type->as.strukt.has_drop)
            field_val = emit_struct_clone_val(ctx, field_val, field_llvm, field_type);
        /* F.3: Block field read — struct retains env ownership; return value directly.
           checker already rejected `Block g = p.step1`; direct call `p.step1(args)`
           is allowed — codegen_block_call will use the loaded value. */
        return field_val;
    }

    case AST_MATCH:
        return codegen_match_expr(ctx, node);

    case AST_TRY:
        return codegen_try_expr(ctx, node);

    case AST_FORCE_UNWRAP:
        return codegen_force_unwrap_expr(ctx, node);

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

    case AST_CAST:
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
    {
        /* Block as expression: value is last expression */
        push_scope(ctx);
        CgScope *block_parent = ctx->current_scope->parent;
        LLVMValueRef last = NULL;
        for (int i = 0; i < node->as.block.stmt_count; i++)
        {
            AstNode *s = node->as.block.stmts[i];
            if (i == node->as.block.stmt_count - 1 && s->kind == AST_EXPR_STMT)
            {
                last = codegen_expr(ctx, s->as.expr_stmt.expr);
            }
            else
            {
                codegen_stmt(ctx, s);
            }
        }
        /* Only clean up variables declared in THIS block, not outer scopes */
        emit_cleanup_to(ctx, block_parent, NULL);
        pop_scope(ctx);
        return last;
    }

    case AST_FFI_CALL:
        return codegen_ffi_call(ctx, node);

    case AST_INDEX:
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
            /* Deep-clone owned element data (string/vec/has_drop struct|enum). */
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
        LLVMValueRef arr_ptr = NULL;
        if (obj->kind == AST_IDENT)
        {
            CgSymbol *sym = cg_scope_resolve(ctx->current_scope, obj->as.ident.name);
            if (sym)
                arr_ptr = sym->value;
        }
        else if (obj->kind == AST_FIELD &&
                 obj->as.field_access.object->resolved_type &&
                 obj->as.field_access.object->resolved_type->kind == TYPE_MODULE)
        {
            /* Module variable: math.PRIMES[0] */
            arr_ptr = LLVMGetNamedGlobal(ctx->module, obj->as.field_access.field);
        }
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
           to give the caller an independent copy (mirrors vec[i] semantics). */
        elem = emit_clone_value(ctx, elem, elem_llvm, elem_type);
        return elem;
    }

    case AST_ARRAY_LIT:
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

        /* Try to build a constant array (all elements are constants) */
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

        /* Not all constant — store element-by-element is handled by VAR_DECL */
        free(elems);
        return NULL;
    }

    case AST_RANGE:
        /* Range expressions are not first-class values; handled by AST_FOR codegen */
        cg_error(ctx, node->line, node->column,
                 "range expression (a..b) can only be used in for-in loops");
        return NULL;

    case AST_NEW_EXPR:
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
            LLVMValueRef val = codegen_expr(ctx, node->as.new_expr.field_inits[i].value);
            if (val == NULL)
                return NULL;

            Type *field_type = struct_type->as.strukt.fields[field_idx].type;
            LLVMValueRef field_ptr = LLVMBuildStructGEP2(ctx->builder, st_llvm,
                                                         storage, (unsigned)field_idx,
                                                         "field_ptr");
            /* cg_store_owned 处理：string clone/move、Block env 转移、
               struct/enum/vec/map move 标记，以及 POD 直接 store */
            cg_store_owned(ctx, field_ptr, val, field_type,
                           node->as.new_expr.field_inits[i].value,
                           CG_XFER_INTO_CONTAINER);
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
            cg_store_owned(ctx, field_ptr, val, field_type, deflt,
                           CG_XFER_INTO_CONTAINER);
        }

        if (on_stack)
        {
            /* Return the loaded struct aggregate value */
            return LLVMBuildLoad2(ctx->builder, st_llvm, storage, "sl.val");
        }
        return storage; /* new: return pointer */
    }

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
