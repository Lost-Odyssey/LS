/* builtins_math_cg.c — Codegen side of the built-in `math` module.
 *
 * Emits LLVM IR for math.<fn>(...) and math.<CONST>:
 *   - INTRINSIC entries → `call double @llvm.<name>.f64(...)`. LLVM lowers
 *     these to a single hardware instruction when available (vsqrtsd /
 *     vroundsd / etc.) or falls back to libm transparently.
 *   - LIBM entries → `call double @<name>(...)`. The linker (AOT) or
 *     LLJIT (run mode) resolves them against the C runtime that ls.exe
 *     is already linked against — no LoadLibrary, no FFI overhead.
 */
#define LS_INCLUDE_CODEGEN 1
#include "builtins_math.h"
#include <string.h>

#include <llvm-c/Core.h>

/* Get-or-declare a function with given signature. */
static LLVMValueRef get_or_declare_fn(LLVMModuleRef mod, const char *name,
                                      LLVMTypeRef ret, LLVMTypeRef *params,
                                      unsigned param_count) {
    LLVMValueRef fn = LLVMGetNamedFunction(mod, name);
    if (fn) return fn;
    LLVMTypeRef fn_ty = LLVMFunctionType(ret, params, param_count, 0);
    return LLVMAddFunction(mod, name, fn_ty);
}

/* Float-only emit path: widen all args to f64 and call the f64 intrinsic/libm. */
static LLVMValueRef emit_float_call(CodegenContext *ctx, const char *emit_name,
                                    AstNode **args, int arity,
                                    const char *result_label) {
    LLVMTypeRef f64 = LLVMDoubleTypeInContext(ctx->context);
    LLVMValueRef argv[2] = { NULL, NULL };
    for (int i = 0; i < arity; i++) {
        LLVMValueRef v = codegen_expr(ctx, args[i]);
        if (v == NULL) return NULL;
        LLVMTypeRef vt = LLVMTypeOf(v);
        if (vt != f64) {
            LLVMTypeKind k = LLVMGetTypeKind(vt);
            if (k == LLVMIntegerTypeKind)
                v = LLVMBuildSIToFP(ctx->builder, v, f64, "math.sitofp");
            else if (k == LLVMFloatTypeKind)
                v = LLVMBuildFPExt(ctx->builder, v, f64, "math.fpext");
        }
        argv[i] = v;
    }
    LLVMTypeRef params[2] = { f64, f64 };
    LLVMValueRef fn = get_or_declare_fn(ctx->module, emit_name, f64, params,
                                        (unsigned)arity);
    LLVMTypeRef fn_ty = LLVMGlobalGetValueType(fn);
    return LLVMBuildCall2(ctx->builder, fn_ty, fn, argv, (unsigned)arity,
                          result_label);
}

/* Dispatch a polymorphic math.abs/min/max call to integer LLVM intrinsics
   when all args are integer (no float widening). The arg types are taken
   from the AST — the checker has already promoted them to the common type
   via numeric widening rules. */
static LLVMValueRef emit_int_poly_call(CodegenContext *ctx, const char *fn_name,
                                       const char *int_prefix,
                                       AstNode **args, int arity,
                                       Type *int_t) {
    int bits = type_int_bits(int_t);
    bool is_signed = type_is_signed(int_t);

    /* min/max: pick signed vs unsigned variant by mangling the prefix. The
       table stores the signed prefix (".smin." / ".smax."). For unsigned,
       replace 's' with 'u'. abs only has the signed/no-sign-distinction form. */
    char intrinsic[64];
    if (strcmp(fn_name, "abs") == 0) {
        if (!is_signed) {
            /* abs of unsigned is identity — no-op, just return the value. */
            return codegen_expr(ctx, args[0]);
        }
        snprintf(intrinsic, sizeof(intrinsic), "%s%d", int_prefix, bits);
    } else {
        /* min/max */
        char buf[64];
        snprintf(buf, sizeof(buf), "%s", int_prefix);
        if (!is_signed) {
            /* int_prefix is "llvm.smin.i" or "llvm.smax.i"; flip the 's' */
            char *dot = strchr(buf, '.');
            if (dot) {
                char *s = strchr(dot + 1, '.');
                if (s && s[-1] == 's' || s == NULL) { /* defensive */ }
                /* simpler: locate ".s" segment */
                for (char *p = buf; *p; p++) {
                    if (p[0] == '.' && p[1] == 's' &&
                        (p[2] == 'm') && (p[3] == 'i' || p[3] == 'a')) {
                        p[1] = 'u';
                        break;
                    }
                }
            }
        }
        snprintf(intrinsic, sizeof(intrinsic), "%s%d", buf, bits);
    }

    /* Lower args, widening narrower ints to the common int type. */
    LLVMTypeRef int_llvm = type_to_llvm(ctx, int_t);
    LLVMValueRef argv[2] = { NULL, NULL };
    for (int i = 0; i < arity; i++) {
        LLVMValueRef v = codegen_expr(ctx, args[i]);
        if (v == NULL) return NULL;
        Type *at = args[i]->resolved_type;
        if (at && !type_equals(at, int_t)) {
            LLVMTypeRef vt = LLVMTypeOf(v);
            if (LLVMGetTypeKind(vt) == LLVMIntegerTypeKind) {
                if (type_is_signed(at))
                    v = LLVMBuildSExt(ctx->builder, v, int_llvm, "math.sext");
                else
                    v = LLVMBuildZExt(ctx->builder, v, int_llvm, "math.zext");
            }
        }
        argv[i] = v;
    }

    if (strcmp(fn_name, "abs") == 0) {
        /* llvm.abs.iN signature: (iN, i1 is_int_min_poison) -> iN.
           Pass i1=false: INT_MIN's abs returns INT_MIN (defined behavior). */
        LLVMTypeRef i1 = LLVMInt1TypeInContext(ctx->context);
        LLVMTypeRef params[2] = { int_llvm, i1 };
        LLVMValueRef fn = get_or_declare_fn(ctx->module, intrinsic, int_llvm,
                                            params, 2);
        LLVMTypeRef fn_ty = LLVMGlobalGetValueType(fn);
        LLVMValueRef call_args[2] = { argv[0], LLVMConstInt(i1, 0, 0) };
        return LLVMBuildCall2(ctx->builder, fn_ty, fn, call_args, 2, fn_name);
    }
    /* min/max: (iN, iN) -> iN */
    LLVMTypeRef params[2] = { int_llvm, int_llvm };
    LLVMValueRef fn = get_or_declare_fn(ctx->module, intrinsic, int_llvm, params,
                                        (unsigned)arity);
    LLVMTypeRef fn_ty = LLVMGlobalGetValueType(fn);
    return LLVMBuildCall2(ctx->builder, fn_ty, fn, argv, (unsigned)arity, fn_name);
}

LLVMValueRef builtin_math_emit_call(CodegenContext *ctx, const char *fn_name,
                                    AstNode **args, int arg_count) {
    int arity = 0;
    MathEmitKind kind = MATH_EMIT_INTRINSIC;
    const char *emit_name = NULL;
    MathPolyKind poly = MATH_POLY_NONE;
    const char *int_prefix = NULL;
    if (!builtin_math_lookup_fn(fn_name, &arity, &kind, &emit_name, &poly,
                                &int_prefix))
        return NULL;
    if (arg_count != arity) return NULL;

    /* Polymorphic dispatch: when all args are integer, call the integer
       intrinsic. The checker has set the call node's resolved_type and
       canonicalised arg widening for us. */
    if (poly == MATH_POLY_INT_OR_FLOAT && int_prefix != NULL) {
        bool all_int = true;
        Type *common = NULL;
        for (int i = 0; i < arity; i++) {
            Type *at = args[i]->resolved_type;
            if (at == NULL || !type_is_integer(at)) { all_int = false; break; }
            if (common == NULL) common = at;
            else {
                Type *c2 = type_numeric_common(common, at);
                if (c2 == NULL) { all_int = false; break; }
                common = c2;
            }
        }
        if (all_int && common != NULL)
            return emit_int_poly_call(ctx, fn_name, int_prefix, args, arity, common);
        /* fall through to float path */
    }

    return emit_float_call(ctx, emit_name, args, arity, fn_name);
}

LLVMValueRef builtin_math_emit_const(CodegenContext *ctx, const char *name) {
    double v = 0;
    if (!builtin_math_lookup_const(name, &v)) return NULL;
    LLVMTypeRef f64 = LLVMDoubleTypeInContext(ctx->context);
    return LLVMConstReal(f64, v);
}
