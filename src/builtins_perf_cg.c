/* builtins_perf_cg.c — Codegen side of the built-in `perf` module.
 *
 * Emits LLVM IR for perf.<fn>(...) calls:
 *
 *   perf.now()        → call i64 @ls_os_perf_now()
 *                       Platform-specific (QueryPerformanceCounter/clock_gettime)
 *                       implemented in runtime/os_win32.c or os_posix.c.
 *
 *   perf.rdtsc()      → call i64 @llvm.x86.rdtsc()
 *   perf.rdtscp()     → alloca i32; {i64,i32} = call @llvm.x86.rdtscp(ptr);
 *                       extractvalue 0  (the TSC i64, aux i32 discarded)
 *
 *   perf.elapsed_ns(t0) → %t1 = call @ls_os_perf_now(); sub i64 %t1, %t0
 *   perf.elapsed_ms(t0) → same sub; sitofp to f64; fdiv 1_000_000.0
 *   perf.elapsed_s(t0)  → same sub; sitofp to f64; fdiv 1_000_000_000.0
 *
 * All emits are inlined at the call site — no wrapper functions are created
 * in the module, keeping the generated IR clean and allowing LLVM to
 * constant-fold elapsed_* when t0 is a compile-time constant.
 */
#define LS_INCLUDE_CODEGEN 1
#include "builtins_perf.h"
#include <string.h>
#include <llvm-c/Core.h>

/* ---- Internal helpers ---- */

/* Get-or-declare an external function with the given signature. */
static LLVMValueRef get_or_declare_fn(LLVMModuleRef mod, const char *name,
                                      LLVMTypeRef ret,
                                      LLVMTypeRef *params, unsigned nparams) {
    LLVMValueRef fn = LLVMGetNamedFunction(mod, name);
    if (fn) return fn;
    LLVMTypeRef fn_ty = LLVMFunctionType(ret, params, nparams, 0);
    return LLVMAddFunction(mod, name, fn_ty);
}

/* Emit a call to ls_os_perf_now() → i64. */
static LLVMValueRef emit_now(CodegenContext *ctx) {
    LLVMTypeRef i64_t = LLVMInt64TypeInContext(ctx->context);
    LLVMValueRef fn = get_or_declare_fn(ctx->module, "ls_os_perf_now",
                                        i64_t, NULL, 0);
    LLVMTypeRef fn_ty = LLVMGlobalGetValueType(fn);
    return LLVMBuildCall2(ctx->builder, fn_ty, fn, NULL, 0, "perf.now");
}

/* Emit @llvm.x86.rdtsc() → i64. */
static LLVMValueRef emit_rdtsc(CodegenContext *ctx) {
    LLVMTypeRef i64_t = LLVMInt64TypeInContext(ctx->context);
    LLVMValueRef fn = get_or_declare_fn(ctx->module, "llvm.x86.rdtsc",
                                        i64_t, NULL, 0);
    LLVMTypeRef fn_ty = LLVMGlobalGetValueType(fn);
    return LLVMBuildCall2(ctx->builder, fn_ty, fn, NULL, 0, "perf.rdtsc");
}

/* Emit a call to ls_os_perf_rdtscp() → i64.
 * The serialising RDTSCP instruction's signature varies across LLVM versions
 * (the llvm.x86.rdtscp intrinsic returns different types in different releases),
 * so we delegate to a C function in the OS backend which uses the compiler
 * built-in directly (__rdtscp on MSVC / __builtin_ia32_rdtscp on GCC/Clang). */
static LLVMValueRef emit_rdtscp(CodegenContext *ctx) {
    LLVMTypeRef  i64_t = LLVMInt64TypeInContext(ctx->context);
    LLVMValueRef fn    = get_or_declare_fn(ctx->module, "ls_os_perf_rdtscp",
                                           i64_t, NULL, 0);
    LLVMTypeRef  fn_ty = LLVMGlobalGetValueType(fn);
    return LLVMBuildCall2(ctx->builder, fn_ty, fn, NULL, 0, "perf.rdtscp");
}

/* Emit elapsed_* family. divisor_f64 = 1.0 → NS (i64), 1e6 → MS, 1e9 → S. */
static LLVMValueRef emit_elapsed(CodegenContext *ctx, AstNode **args,
                                 PerfEmitKind kind) {
    LLVMTypeRef i64_t = LLVMInt64TypeInContext(ctx->context);
    LLVMTypeRef f64_t = LLVMDoubleTypeInContext(ctx->context);

    LLVMValueRef t0 = codegen_expr(ctx, args[0]);
    if (t0 == NULL) return NULL;

    /* Widen t0 to i64 if it came in as a narrower int (defensive). */
    LLVMTypeRef t0_ty = LLVMTypeOf(t0);
    if (t0_ty != i64_t && LLVMGetTypeKind(t0_ty) == LLVMIntegerTypeKind)
        t0 = LLVMBuildSExt(ctx->builder, t0, i64_t, "t0.sext");

    LLVMValueRef t1 = emit_now(ctx);
    LLVMValueRef dt = LLVMBuildSub(ctx->builder, t1, t0, "perf.dt_ns");

    if (kind == PERF_EMIT_ELAPSED_NS)
        return dt;                               /* already i64 nanoseconds */

    double divisor = (kind == PERF_EMIT_ELAPSED_MS) ? 1.0e6 : 1.0e9;
    LLVMValueRef dt_f  = LLVMBuildSIToFP(ctx->builder, dt, f64_t, "perf.dt_f");
    LLVMValueRef div_c = LLVMConstReal(f64_t, divisor);
    return LLVMBuildFDiv(ctx->builder, dt_f, div_c,
                         (kind == PERF_EMIT_ELAPSED_MS) ? "perf.ms" : "perf.s");
}

/* ---- Public API ---- */

LLVMValueRef builtin_perf_emit_call(CodegenContext *ctx, const char *fn_name,
                                    AstNode **args, int arg_count) {
    int          arity = 0;
    PerfEmitKind kind  = PERF_EMIT_NOW;
    if (!builtin_perf_lookup_fn(fn_name, &arity, &kind)) return NULL;
    if (arg_count != arity) return NULL;

    switch (kind) {
        case PERF_EMIT_NOW:        return emit_now(ctx);
        case PERF_EMIT_RDTSC:      return emit_rdtsc(ctx);
        case PERF_EMIT_RDTSCP:     return emit_rdtscp(ctx);
        case PERF_EMIT_ELAPSED_NS:
        case PERF_EMIT_ELAPSED_MS:
        case PERF_EMIT_ELAPSED_S:  return emit_elapsed(ctx, args, kind);
    }
    return NULL;
}
