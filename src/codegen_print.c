/* codegen_print.c — @print / f-string rendering family, moved verbatim out of
   codegen_expr.c (Task 7.6, Batch 7). Entry points: codegen_print_call
   (@print intrinsic) and codegen_format_string (f-string lowering), both
   dispatched from codegen_expr. Shares the Str value helpers exported by
   codegen_expr.c via codegen_internal.h. */
#include "codegen.h"
#include "codegen_internal.h"
#include "common.h"

#include <llvm-c/Core.h>

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* family-internal forward declarations (mutual recursion between the
   struct/enum/array printers and cg_print_one_value) */
static int append_text_escaped(char *dst, int len, int cap, const char *src);
static bool cg_build_spec_conv(CodegenContext *ctx, int line, int col, Type *et, const char *spec, char *out, size_t out_sz, bool *out_to_double);
static LLVMValueRef cg_fstring_emit_arg(CodegenContext *ctx, AstNode *expr, LLVMValueRef val, const char *user_spec, char *fmt_buf, int *p_fmt_len, int fmt_cap);
static void cg_print_str_value(CodegenContext *ctx, LLVMValueRef val);
static void codegen_print_array(CodegenContext *ctx, AstNode *arg);
static void codegen_print_struct_value(CodegenContext *ctx, LLVMValueRef val, Type *t);
static void codegen_print_enum_value(CodegenContext *ctx, LLVMValueRef val, Type *t);
static void cg_print_one_value(CodegenContext *ctx, LLVMValueRef fval, Type *ftype);
static LLVMValueRef emit_printf(CodegenContext *ctx, const char *fmt, LLVMValueRef *extra_args, int extra_count);
static const char *printf_fmt_for_type(Type *t);


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

/* Shared by both f-string codegen paths (string-building and the @print
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
   set_sink redirects all @print output. Default stream is stdout, so output is
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

    /* Get array pointer. The old IDENT-only lookup returned SILENTLY for any
       other place — `@print(b.d)` emitted a bare newline instead of the
       elements. cg_array_place_ptr covers field chains, array elements,
       borrowed receivers, nested arrays and rvalues; a NULL now means the
       argument could not be evaluated at all, which deserves a diagnostic
       rather than a missing line. */
    LLVMValueRef arr_ptr = cg_array_place_ptr(ctx, arg);
    if (arr_ptr == NULL)
    {
        cg_error(ctx, arg->line, arg->column,
                 "cannot get address of array to print");
        return;
    }

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
    cg_attach_tag_range(ctx, disc, t->as.enom.variant_count);
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

/* Codegen for @print with any type — generates inline printf */
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
                    /* Owned Str rvalue interpolated → register for drop.
                       Membership rationale lives on cg_expr_yields_owned_rvalue
                       (codegen_internal.h). This site's inline whitelist had
                       drifted: it missed the combinator lowerings
                       (AST_MATCH/AST_FORCE_UNWRAP) and AST_TRY —
                       @print(f"{opt.unwrap_or(s)}") leaked the payload
                       (own_rvalue_sites_test.lls). Bare ident stays a borrow. */
                    if (cg_expr_yields_owned_rvalue(expr, expr->resolved_type))
                        cg_spill_owned_rvalue(ctx, sval, expr->resolved_type,
                                              false, "fstr.str.drop");
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
               Membership rationale lives on cg_expr_yields_owned_rvalue
               (codegen_internal.h); a static-Str clone allocates nothing,
               which masked the field-read leak until an owned field was
               printed. Bare ident stays a borrow: skip. */
            if (cg_expr_yields_owned_rvalue(arg, t))
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
               fields (strings/vecs/…) leak. Drop the clone. Membership rationale
               lives on cg_expr_yields_owned_rvalue (codegen_internal.h). NOTE a
               FIELD read of a has_drop struct IS an owned clone (AST_FIELD read
               site runs emit_struct_clone_val), not a borrow — this site's old
               whitelist missed FIELD (and BINARY.lowered / TRY), so
               @print(obj.inner) leaked the clone (own_rvalue_sites_test.lls).
               Only a bare ident stays a borrow and must not be dropped. */
            if (cg_expr_yields_owned_rvalue(arg, t))
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
            /* Same as the struct branch: a has_drop ENUM field read is an owned
               clone (emit_enum_clone_val, 72c3f9d) — the old whitelist missed
               FIELD/BINARY.lowered/TRY, leaking e.g. @print(h.opt)
               (own_rvalue_sites_test.lls). */
            if (cg_expr_yields_owned_rvalue(arg, t))
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
LLVMValueRef codegen_format_string(CodegenContext *ctx, AstNode *node)
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
               (statement-end flush runs after the snprintf below). Membership
               rationale lives on cg_expr_yields_owned_rvalue
               (codegen_internal.h). Bare ident is a borrow. */
            if (cg_expr_yields_owned_rvalue(expr, expr->resolved_type))
                cg_spill_owned_rvalue(ctx, val, expr->resolved_type,
                                      false, "fstr.str.drop");
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
