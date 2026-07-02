/* codegen.c — AST to LLVM IR code generation */
#include "codegen.h"
#include "codegen_internal.h"
#include "module.h"
#define LS_INCLUDE_CODEGEN 1
#include "builtins_math.h"
#define LS_INCLUDE_CODEGEN 1
#include "builtins_perf.h"
/* Phase E.4: builtins_io.h removed — io is now pure-LS stdlib/io.ls. */
#include "common.h"

#include <llvm-c/Core.h>
#include <llvm-c/Target.h>
#include <llvm-c/TargetMachine.h>
#include <llvm-c/Analysis.h>
#include <llvm-c/BitWriter.h>
#include <llvm-c/Transforms/PassBuilder.h>

#include <stdio.h>
#include <string.h>
#include <ctype.h>

/* File-local helpers (single-TU; re-static'd at codegen split §7). */
static void cg_install_memcheck_wrappers(CodegenContext *ctx);
static LLVMValueRef cg_mc_enter_fn(CodegenContext *ctx);
static LLVMValueRef cg_mc_leave_fn(CodegenContext *ctx);
static LLVMTypeRef cg_mc_site_type(CodegenContext *ctx);
static LLVMValueRef cg_module_cstr(CodegenContext *ctx, const char *s, const char *gv_name);
static LLVMValueRef cg_prof_enter_fn(CodegenContext *ctx);
static LLVMValueRef cg_prof_leave_fn(CodegenContext *ctx);
static void declare_builtins(CodegenContext *ctx);
static void emit_global_var_init(CodegenContext *ctx, AstNode *decl);
static void emit_str_replace_helper(CodegenContext *ctx);

/* CG_DEBUG is defined in common.h (default 0).
   Set to 1 via -DCG_DEBUG=1 at build time to inject runtime printf traces
   at every compiler-managed memory alloc/clone/free/drop point. */

/* Global counter for unique basic block names */
static int g_block_counter = 0;

/* L-009: authoritative function-name mangler. The single source of truth used
   by BOTH definition sites and call sites — they must produce identical strings.
   Scheme: "<modpath>__<fn>" with '.' in the module path replaced by '_'
   (e.g. module "std.json", fn "parse" -> "std_json__parse").
   When module_path is NULL or empty, the name is copied verbatim (root/main
   file functions stay unmangled, preserving `main` and existing behaviour). */
void cg_module_fn_symbol(char *out, size_t cap,
                                const char *module_path, const char *fn)
{
    if (module_path == NULL || module_path[0] == '\0')
    {
        snprintf(out, cap, "%s", fn);
        return;
    }
    size_t pos = 0;
    for (const char *p = module_path; *p && pos + 1 < cap; p++)
        out[pos++] = (*p == '.') ? '_' : *p;
    /* separator "__" then fn */
    if (pos + 2 < cap) { out[pos++] = '_'; out[pos++] = '_'; }
    snprintf(out + pos, cap - pos, "%s", fn);
}


/* Forward declarations for Phase E.2 ABI lowering helpers (definitions live
   near extern_fn_type lower in this file but are referenced from AST_CALL). */

/* Phase 4 (__move): unwrap `__move(x)` call wrappers so downstream AST-shape
   checks (e.g. vec.push testing `arg->kind == AST_IDENT` for ownership
   transfer) can see through the explicit-move annotation.
   `__move` is a type-preserving no-op at codegen; the checker has already
   marked the source variable as moved and rejected any later use. */
AstNode *ast_unwrap_move(AstNode *n)
{
    while (n && n->kind == AST_CALL &&
           n->as.call.callee && n->as.call.callee->kind == AST_IDENT &&
           (strcmp(n->as.call.callee->as.ident.name, "@move") == 0 ||
            strcmp(n->as.call.callee->as.ident.name, "__move") == 0) &&
           n->as.call.arg_count == 1)
    {
        n = n->as.call.args[0];
    }
    return n;
}

/* ---- Error reporting ---- */

void cg_error(CodegenContext *ctx, int line, int col, const char *fmt, ...)
{
    ctx->had_error = true;
    fprintf(stderr, "[codegen] %d:%d: ", line, col);
    va_list args;
    va_start(args, fmt);
    vfprintf(stderr, fmt, args);
    va_end(args);
    fprintf(stderr, "\n");
}

/* ---- CG_DEBUG runtime trace helper ---- */

#if CG_DEBUG
/* Emit a runtime printf call into the generated IR for memory-management tracing.
   fmt_cstr: a C string literal (format, can use %d %p %s etc.)
   args / nargs: LLVMValueRef arguments matching the format specifiers.
   Safe to call from any context where ctx->builder is positioned in a valid block. */
void cg_emit_debug_printf(CodegenContext *ctx,
                                 const char *fmt_cstr,
                                 LLVMValueRef *args, int nargs)
{
    LLVMBasicBlockRef cur_bb = LLVMGetInsertBlock(ctx->builder);
    if (cur_bb == NULL)
        return;
    if (LLVMGetBasicBlockTerminator(cur_bb) != NULL)
        return;

    LLVMValueRef printf_fn = LLVMGetNamedFunction(ctx->module, "printf");
    if (printf_fn == NULL)
        return;
    LLVMTypeRef printf_type = LLVMGlobalGetValueType(printf_fn);

    /* Build the format string as a global string constant */
    LLVMValueRef fmt_str = LLVMBuildGlobalStringPtr(ctx->builder, fmt_cstr, "dbg.fmt");

    /* Assemble argument list: fmt_str + user args */
    LLVMValueRef *all_args = (LLVMValueRef *)malloc_safe(
        (size_t)(nargs + 1) * sizeof(LLVMValueRef));
    all_args[0] = fmt_str;
    for (int i = 0; i < nargs; i++)
        all_args[i + 1] = args[i];

    LLVMBuildCall2(ctx->builder, printf_type, printf_fn, all_args, (unsigned)(nargs + 1), "");
    free(all_args);
}
#else
/* No-op when CG_DEBUG is disabled — the compiler optimises all calls away. */
void cg_emit_debug_printf(CodegenContext *ctx,
                                 const char *fmt_cstr,
                                 LLVMValueRef *args, int nargs)
{
    (void)ctx;
    (void)fmt_cstr;
    (void)args;
    (void)nargs;
}
#endif /* CG_DEBUG */

/* ---- F.6: CG_DEBUG helper wrappers for closure operations ----
   All helpers are no-ops when CG_DEBUG==0; the compiler eliminates them.
   When CG_DEBUG==1 they emit a runtime printf via cg_emit_debug_printf.   */

/* Log a capture decision (one line per captured variable). */
void cg_dbg_capture(CodegenContext *ctx,
                           const char *name, Type *t, const char *kind)
{
#if CG_DEBUG
    char fmt[256];
    snprintf(fmt, sizeof(fmt),
             "[cg] cap.%-7s name='%-12s' type='%s'\n",
             kind, name ? name : "?", t ? type_name(t) : "?");
    cg_emit_debug_printf(ctx, fmt, NULL, 0);
#else
    (void)ctx; (void)name; (void)t; (void)kind;
#endif
}

/* Log an outer-variable move marker (cap=-1 or moved_flag). */
void cg_dbg_outer_mark(CodegenContext *ctx,
                              const char *name, const char *marker)
{
#if CG_DEBUG
    char fmt[256];
    snprintf(fmt, sizeof(fmt),
             "[cg] outer.mark name='%-12s' state='MOVED'  marker='%s'\n",
             name ? name : "?", marker ? marker : "?");
    cg_emit_debug_printf(ctx, fmt, NULL, 0);
#else
    (void)ctx; (void)name; (void)marker;
#endif
}

/* Log env.alloc with a runtime pointer value. */
void cg_dbg_env_alloc(CodegenContext *ctx, int closure_id,
                             unsigned long long size, LLVMValueRef env_ptr)
{
#if CG_DEBUG
    char fmt[256];
    snprintf(fmt, sizeof(fmt),
             "[cg] env.alloc  closure_id=%-4d size=%-6llu ptr=%%p\n",
             closure_id, size);
    LLVMValueRef args[1] = { env_ptr };
    cg_emit_debug_printf(ctx, fmt, args, 1);
#else
    (void)ctx; (void)closure_id; (void)size; (void)env_ptr;
#endif
}

/* Log a block operation (drop / move / assign) with a runtime env pointer. */
void cg_dbg_block_op(CodegenContext *ctx,
                            const char *op, const char *label,
                            LLVMValueRef env_ptr)
{
#if CG_DEBUG
    char fmt[256];
    snprintf(fmt, sizeof(fmt),
             "[cg] block.%-8s %-18s env=%%p\n",
             op, label ? label : "");
    LLVMValueRef args[1] = { env_ptr };
    cg_emit_debug_printf(ctx, fmt, args, 1);
#else
    (void)ctx; (void)op; (void)label; (void)env_ptr;
#endif
}

/* ---- CgScope management ---- */






/* Forward declarations for the ownership engine, closure lowering, and the
   CgTransferKind ownership-transfer API moved to codegen_internal.h (Step 1 of
   the codegen.c split, docs/plan_codegen_split.md). */






/* Whether marking the outer-as-moved is done via the cap idiom.
   Only string uses this (cap=-1). Struct and enum use moved_flag (i1 alloca).
   vec/map are now by-ref and never mark the outer moved. */



/* ---- Struct type registry ---- */





/* ---- Enum LLVM type registry ---- */





/* ---- Memcheck integration ---------------------------------------------
   When ctx->memcheck_enabled, every malloc/free in LS-emitted IR routes
   through ls_mc_alloc / ls_mc_free, with a SiteInfo describing the LS
   source location and operation kind. SiteInfo globals are deduplicated
   per (kind, file, line, col) to keep IR size sane. */

/* Get-or-build the LsMcSite LLVM struct type. Layout: { ptr, i32, i32, ptr } */
static LLVMTypeRef cg_mc_site_type(CodegenContext *ctx) {
    LLVMTypeRef existing = LLVMGetTypeByName2(ctx->context, "LsMcSite");
    if (existing) return existing;
    LLVMTypeRef ptr = LLVMPointerTypeInContext(ctx->context, 0);
    LLVMTypeRef i32 = LLVMInt32TypeInContext(ctx->context);
    LLVMTypeRef fields[4] = { ptr, i32, i32, ptr };
    LLVMTypeRef st = LLVMStructCreateNamed(ctx->context, "LsMcSite");
    LLVMStructSetBody(st, fields, 4, 0);
    return st;
}

/* Get-or-declare ls_mc_alloc and ls_mc_free externally. */
/* Get-or-emit ls_os_perf_now().  In JIT mode the symbol is resolved via
   AbsoluteSymbols; in AOT mode we emit a module-internal definition that
   calls QueryPerformanceCounter (Windows) or clock_gettime (POSIX). */
LLVMValueRef cg_get_perf_now(CodegenContext *ctx) {
    LLVMValueRef fn = LLVMGetNamedFunction(ctx->module, "ls_os_perf_now");
    if (fn) return fn;

    LLVMTypeRef i64_t = LLVMInt64TypeInContext(ctx->context);
    LLVMTypeRef i32_t = LLVMInt32TypeInContext(ctx->context);
    LLVMTypeRef f64_t = LLVMDoubleTypeInContext(ctx->context);
    LLVMTypeRef ptr_t = LLVMPointerTypeInContext(ctx->context, 0);

    LLVMTypeRef fn_ty = LLVMFunctionType(i64_t, NULL, 0, 0);
    fn = LLVMAddFunction(ctx->module, "ls_os_perf_now", fn_ty);

    /* In JIT user-module mode (extern_builtins == true), the symbol is resolved
       via AbsoluteSymbols — leave as extern declaration.
       In AOT mode (extern_builtins == false), emit inline body. */
    if (ctx->extern_builtins) return fn;

    /* Emit body: calls QPC (Windows) or clock_gettime (POSIX) */
    LLVMBasicBlockRef saved_bb = LLVMGetInsertBlock(ctx->builder);
    LLVMValueRef saved_fn_val = ctx->current_fn;

    LLVMBasicBlockRef entry = LLVMAppendBasicBlockInContext(ctx->context, fn, "entry");
    LLVMPositionBuilderAtEnd(ctx->builder, entry);

#ifdef _WIN32
    /* Declare QueryPerformanceCounter(i64*) -> i32
       and QueryPerformanceFrequency(i64*) -> i32 */
    LLVMValueRef qpc_fn = LLVMGetNamedFunction(ctx->module, "QueryPerformanceCounter");
    if (!qpc_fn) {
        LLVMTypeRef qpc_params[1] = { ptr_t };
        LLVMTypeRef qpc_ty = LLVMFunctionType(i32_t, qpc_params, 1, 0);
        qpc_fn = LLVMAddFunction(ctx->module, "QueryPerformanceCounter", qpc_ty);
    }
    LLVMValueRef qpf_fn = LLVMGetNamedFunction(ctx->module, "QueryPerformanceFrequency");
    if (!qpf_fn) {
        LLVMTypeRef qpf_params[1] = { ptr_t };
        LLVMTypeRef qpf_ty = LLVMFunctionType(i32_t, qpf_params, 1, 0);
        qpf_fn = LLVMAddFunction(ctx->module, "QueryPerformanceFrequency", qpf_ty);
    }

    /* Global freq variable (lazy init via flag) */
    LLVMValueRef freq_gv = LLVMGetNamedGlobal(ctx->module, "__perf_freq");
    if (!freq_gv) {
        freq_gv = LLVMAddGlobal(ctx->module, i64_t, "__perf_freq");
        LLVMSetInitializer(freq_gv, LLVMConstInt(i64_t, 0, 0));
        LLVMSetLinkage(freq_gv, LLVMInternalLinkage);
    }

    /* if (freq == 0) QueryPerformanceFrequency(&freq) */
    LLVMValueRef freq_val = LLVMBuildLoad2(ctx->builder, i64_t, freq_gv, "freq");
    LLVMValueRef is_zero = LLVMBuildICmp(ctx->builder, LLVMIntEQ, freq_val,
                                          LLVMConstInt(i64_t, 0, 0), "freq.z");
    LLVMBasicBlockRef init_bb = LLVMAppendBasicBlockInContext(ctx->context, fn, "freq.init");
    LLVMBasicBlockRef cont_bb = LLVMAppendBasicBlockInContext(ctx->context, fn, "freq.cont");
    LLVMBuildCondBr(ctx->builder, is_zero, init_bb, cont_bb);

    LLVMPositionBuilderAtEnd(ctx->builder, init_bb);
    LLVMValueRef qpf_args[1] = { freq_gv };
    LLVMBuildCall2(ctx->builder, LLVMGlobalGetValueType(qpf_fn), qpf_fn, qpf_args, 1, "");
    LLVMBuildBr(ctx->builder, cont_bb);

    LLVMPositionBuilderAtEnd(ctx->builder, cont_bb);
    /* counter alloca + QPC call */
    LLVMValueRef counter_alloca = cg_entry_alloca(ctx, i64_t, "counter");
    LLVMValueRef qpc_args[1] = { counter_alloca };
    LLVMBuildCall2(ctx->builder, LLVMGlobalGetValueType(qpc_fn), qpc_fn, qpc_args, 1, "");
    LLVMValueRef counter = LLVMBuildLoad2(ctx->builder, i64_t, counter_alloca, "cnt");
    LLVMValueRef freq2 = LLVMBuildLoad2(ctx->builder, i64_t, freq_gv, "frq");

    /* ns = (double)counter * 1e9 / (double)freq */
    LLVMValueRef cnt_f = LLVMBuildSIToFP(ctx->builder, counter, f64_t, "cnt.f");
    LLVMValueRef frq_f = LLVMBuildSIToFP(ctx->builder, freq2, f64_t, "frq.f");
    LLVMValueRef mul = LLVMBuildFMul(ctx->builder, cnt_f,
                                      LLVMConstReal(f64_t, 1.0e9), "mul");
    LLVMValueRef ns_f = LLVMBuildFDiv(ctx->builder, mul, frq_f, "ns.f");
    LLVMValueRef ns = LLVMBuildFPToSI(ctx->builder, ns_f, i64_t, "ns");
    LLVMBuildRet(ctx->builder, ns);
#else
    /* POSIX: declare clock_gettime(i32 clk_id, ptr tp) -> i32 */
    LLVMValueRef cgt_fn = LLVMGetNamedFunction(ctx->module, "clock_gettime");
    if (!cgt_fn) {
        LLVMTypeRef cgt_params[2] = { i32_t, ptr_t };
        LLVMTypeRef cgt_ty = LLVMFunctionType(i32_t, cgt_params, 2, 0);
        cgt_fn = LLVMAddFunction(ctx->module, "clock_gettime", cgt_ty);
    }
    /* struct timespec { i64 tv_sec; i64 tv_nsec; } */
    LLVMTypeRef ts_fields[2] = { i64_t, i64_t };
    LLVMTypeRef ts_ty = LLVMStructTypeInContext(ctx->context, ts_fields, 2, 0);
    LLVMValueRef ts_alloca = cg_entry_alloca(ctx, ts_ty, "ts");
    /* CLOCK_MONOTONIC = 1 on Linux */
    LLVMValueRef cgt_args[2] = { LLVMConstInt(i32_t, 1, 0), ts_alloca };
    LLVMBuildCall2(ctx->builder, LLVMGlobalGetValueType(cgt_fn), cgt_fn, cgt_args, 2, "");
    LLVMValueRef sec_ptr = LLVMBuildStructGEP2(ctx->builder, ts_ty, ts_alloca, 0, "sec.p");
    LLVMValueRef nsec_ptr = LLVMBuildStructGEP2(ctx->builder, ts_ty, ts_alloca, 1, "nsec.p");
    LLVMValueRef sec = LLVMBuildLoad2(ctx->builder, i64_t, sec_ptr, "sec");
    LLVMValueRef nsec = LLVMBuildLoad2(ctx->builder, i64_t, nsec_ptr, "nsec");
    LLVMValueRef sec_ns = LLVMBuildMul(ctx->builder, sec,
                                        LLVMConstInt(i64_t, 1000000000ULL, 0), "sec.ns");
    LLVMValueRef total = LLVMBuildAdd(ctx->builder, sec_ns, nsec, "total");
    LLVMBuildRet(ctx->builder, total);
#endif

    /* Restore builder position */
    if (saved_bb)
        LLVMPositionBuilderAtEnd(ctx->builder, saved_bb);
    ctx->current_fn = saved_fn_val;
    return fn;
}

LLVMValueRef cg_mc_alloc_fn(CodegenContext *ctx) {
    LLVMValueRef fn = LLVMGetNamedFunction(ctx->module, "ls_mc_alloc");
    if (fn) return fn;
    LLVMTypeRef ptr = LLVMPointerTypeInContext(ctx->context, 0);
    LLVMTypeRef i64 = LLVMInt64TypeInContext(ctx->context);
    LLVMTypeRef params[2] = { i64, ptr };
    LLVMTypeRef ft = LLVMFunctionType(ptr, params, 2, 0);
    return LLVMAddFunction(ctx->module, "ls_mc_alloc", ft);
}

LLVMValueRef cg_mc_free_fn(CodegenContext *ctx) {
    LLVMValueRef fn = LLVMGetNamedFunction(ctx->module, "ls_mc_free");
    if (fn) return fn;
    LLVMTypeRef ptr = LLVMPointerTypeInContext(ctx->context, 0);
    LLVMTypeRef params[2] = { ptr, ptr };
    LLVMTypeRef ft = LLVMFunctionType(LLVMVoidTypeInContext(ctx->context),
                                      params, 2, 0);
    return LLVMAddFunction(ctx->module, "ls_mc_free", ft);
}

/* Forward decl — defined later in the file. Needed by cg_emit_mc_enter
   which sits above cg_module_cstr in source order. */

/* D.1 — get-or-declare ls_mc_enter(const char *fn, const char *file, int line). */
static LLVMValueRef cg_mc_enter_fn(CodegenContext *ctx) {
    LLVMValueRef fn = LLVMGetNamedFunction(ctx->module, "ls_mc_enter");
    if (fn) return fn;
    LLVMTypeRef ptr = LLVMPointerTypeInContext(ctx->context, 0);
    LLVMTypeRef i32 = LLVMInt32TypeInContext(ctx->context);
    LLVMTypeRef params[3] = { ptr, ptr, i32 };
    LLVMTypeRef ft = LLVMFunctionType(LLVMVoidTypeInContext(ctx->context),
                                      params, 3, 0);
    return LLVMAddFunction(ctx->module, "ls_mc_enter", ft);
}

/* D.1 — get-or-declare ls_mc_leave(). */
static LLVMValueRef cg_mc_leave_fn(CodegenContext *ctx) {
    LLVMValueRef fn = LLVMGetNamedFunction(ctx->module, "ls_mc_leave");
    if (fn) return fn;
    LLVMTypeRef ft = LLVMFunctionType(LLVMVoidTypeInContext(ctx->context),
                                      NULL, 0, 0);
    return LLVMAddFunction(ctx->module, "ls_mc_leave", ft);
}

/* D.1 — emit "call void @ls_mc_enter(fn_name, file, line)" at the current
   builder position. No-op when memcheck is disabled. fn_name + file are
   reused via the same private string cache as cg_make_site to keep the IR
   compact across many functions. */
void cg_emit_mc_enter(CodegenContext *ctx, const char *fn_name,
                              const char *file, int line)
{
    if (!ctx->memcheck_enabled) return;
    if (!fn_name) fn_name = "?";
    if (!file)    file = "?";

    /* Allocate stable globals for fn_name and file. We could dedup against
       a cache, but per-function strings are tiny relative to overall IR. */
    static int seq = 0;
    char fbuf[64], gbuf[64];
    snprintf(fbuf, sizeof(fbuf), "ls_mc_fn_%d", seq);
    snprintf(gbuf, sizeof(gbuf), "ls_mc_fn_file_%d", seq);
    seq++;
    LLVMValueRef fn_str = cg_module_cstr(ctx, fn_name, fbuf);
    LLVMValueRef file_str = cg_module_cstr(ctx, file, gbuf);

    LLVMTypeRef i32 = LLVMInt32TypeInContext(ctx->context);
    LLVMTypeRef ptr = LLVMPointerTypeInContext(ctx->context, 0);
    LLVMValueRef args[3] = {
        fn_str, file_str, LLVMConstInt(i32, (unsigned long long)line, 1)
    };
    LLVMTypeRef params[3] = { ptr, ptr, i32 };
    LLVMTypeRef ft = LLVMFunctionType(LLVMVoidTypeInContext(ctx->context),
                                      params, 3, 0);
    LLVMBuildCall2(ctx->builder, ft, cg_mc_enter_fn(ctx), args, 3, "");
}

/* D.1 — emit "call void @ls_mc_leave()" at the current builder position.
   No-op when memcheck is disabled. Must be inserted before every ret/retvoid
   in user-written functions (compiler-synthesized helpers like __drop bodies
   and runtime intrinsics are not balanced and must be skipped). */
void cg_emit_mc_leave(CodegenContext *ctx)
{
    if (!ctx->memcheck_enabled) return;
    LLVMTypeRef ft = LLVMFunctionType(LLVMVoidTypeInContext(ctx->context),
                                      NULL, 0, 0);
    LLVMBuildCall2(ctx->builder, ft, cg_mc_leave_fn(ctx), NULL, 0, "");
}

/* --profile: emit ls_prof_enter/ls_prof_leave at function boundaries. */
static LLVMValueRef cg_prof_enter_fn(CodegenContext *ctx) {
    LLVMValueRef fn = LLVMGetNamedFunction(ctx->module, "ls_prof_enter");
    if (fn) return fn;
    LLVMTypeRef ptr = LLVMPointerTypeInContext(ctx->context, 0);
    LLVMTypeRef i32 = LLVMInt32TypeInContext(ctx->context);
    LLVMTypeRef params[3] = { ptr, ptr, i32 };
    LLVMTypeRef ft = LLVMFunctionType(LLVMVoidTypeInContext(ctx->context),
                                      params, 3, 0);
    return LLVMAddFunction(ctx->module, "ls_prof_enter", ft);
}

static LLVMValueRef cg_prof_leave_fn(CodegenContext *ctx) {
    LLVMValueRef fn = LLVMGetNamedFunction(ctx->module, "ls_prof_leave");
    if (fn) return fn;
    LLVMTypeRef ft = LLVMFunctionType(LLVMVoidTypeInContext(ctx->context),
                                      NULL, 0, 0);
    return LLVMAddFunction(ctx->module, "ls_prof_leave", ft);
}

void cg_emit_prof_enter(CodegenContext *ctx, const char *fn_name,
                               const char *file, int line)
{
    if (!ctx->profile_enabled) return;
    if (!fn_name) fn_name = "?";
    if (!file)    file = "?";
    static int seq = 0;
    char fbuf[64], gbuf[64];
    snprintf(fbuf, sizeof(fbuf), "ls_prof_fn_%d", seq);
    snprintf(gbuf, sizeof(gbuf), "ls_prof_file_%d", seq);
    seq++;
    LLVMValueRef fn_str = cg_module_cstr(ctx, fn_name, fbuf);
    LLVMValueRef file_str = cg_module_cstr(ctx, file, gbuf);
    LLVMTypeRef i32 = LLVMInt32TypeInContext(ctx->context);
    LLVMTypeRef ptr = LLVMPointerTypeInContext(ctx->context, 0);
    LLVMValueRef args[3] = {
        fn_str, file_str, LLVMConstInt(i32, (unsigned long long)line, 1)
    };
    LLVMTypeRef params[3] = { ptr, ptr, i32 };
    LLVMTypeRef ft = LLVMFunctionType(LLVMVoidTypeInContext(ctx->context),
                                      params, 3, 0);
    LLVMBuildCall2(ctx->builder, ft, cg_prof_enter_fn(ctx), args, 3, "");
}

void cg_emit_prof_leave(CodegenContext *ctx)
{
    if (!ctx->profile_enabled) return;
    LLVMTypeRef ft = LLVMFunctionType(LLVMVoidTypeInContext(ctx->context),
                                      NULL, 0, 0);
    LLVMBuildCall2(ctx->builder, ft, cg_prof_leave_fn(ctx), NULL, 0, "");
}

/* Add a private constant i8 array global holding `s` + null terminator,
   returning a pointer to its first byte (i8*). Builder-independent — works
   even before any function exists. */
static LLVMValueRef cg_module_cstr(CodegenContext *ctx, const char *s,
                                   const char *gv_name) {
    LLVMValueRef cstr = LLVMConstStringInContext(ctx->context, s,
                                                 (unsigned)strlen(s), 0);
    LLVMTypeRef arr_ty = LLVMTypeOf(cstr);
    LLVMValueRef gv = LLVMAddGlobal(ctx->module, arr_ty, gv_name);
    LLVMSetInitializer(gv, cstr);
    LLVMSetLinkage(gv, LLVMPrivateLinkage);
    LLVMSetGlobalConstant(gv, 1);
    LLVMSetUnnamedAddr(gv, 1);
    /* Decay to i8* via constant GEP — safe at module level (no builder). */
    LLVMTypeRef i32 = LLVMInt32TypeInContext(ctx->context);
    LLVMValueRef zero = LLVMConstInt(i32, 0, 0);
    LLVMValueRef idxs[2] = { zero, zero };
    return LLVMConstInBoundsGEP2(arr_ty, gv, idxs, 2);
}

/* Build (or reuse) a private LsMcSite global. The cache key combines
   kind/file/line/col so identical sites share one global. Builder-independent. */
LLVMValueRef cg_make_site(CodegenContext *ctx, const char *kind,
                                 int line, int col) {
    const char *file = "<unknown>";
    if (ctx->module) {
        size_t mn_len = 0;
        const char *mn = LLVMGetModuleIdentifier(ctx->module, &mn_len);
        if (mn && mn_len > 0) file = mn;
    }
    if (!kind) kind = "unknown";

    /* Cache lookup */
    char keybuf[256];
    snprintf(keybuf, sizeof(keybuf), "%s|%s|%d|%d", kind, file, line, col);
    for (int i = 0; i < ctx->mc_site_count; i++) {
        if (strcmp(ctx->mc_sites[i].key, keybuf) == 0)
            return ctx->mc_sites[i].site_gv;
    }

    /* Build globals: file string, kind string, then the LsMcSite struct */
    char fname[64], kname[64];
    snprintf(fname, sizeof(fname), "ls_mc_file_%d", ctx->mc_site_count);
    snprintf(kname, sizeof(kname), "ls_mc_kind_%d", ctx->mc_site_count);
    LLVMValueRef file_ptr = cg_module_cstr(ctx, file, fname);
    LLVMValueRef kind_ptr = cg_module_cstr(ctx, kind, kname);

    LLVMTypeRef i32 = LLVMInt32TypeInContext(ctx->context);
    LLVMTypeRef st = cg_mc_site_type(ctx);
    LLVMValueRef fields[4] = {
        file_ptr,
        LLVMConstInt(i32, (unsigned long long)line, 1),
        LLVMConstInt(i32, (unsigned long long)col, 1),
        kind_ptr,
    };
    LLVMValueRef init = LLVMConstNamedStruct(st, fields, 4);
    char gv_name[64];
    snprintf(gv_name, sizeof(gv_name), "ls_mc_site_%d", ctx->mc_site_count);
    LLVMValueRef gv = LLVMAddGlobal(ctx->module, st, gv_name);
    LLVMSetInitializer(gv, init);
    LLVMSetLinkage(gv, LLVMPrivateLinkage);
    LLVMSetGlobalConstant(gv, 1);

    /* Insert into cache */
    if (ctx->mc_site_count >= ctx->mc_site_cap) {
        ctx->mc_site_cap = GROW_CAPACITY(ctx->mc_site_cap);
        ctx->mc_sites = realloc_safe(ctx->mc_sites,
            (size_t)ctx->mc_site_cap * sizeof(ctx->mc_sites[0]));
    }
    size_t klen = strlen(keybuf);
    char *kdup = (char *)malloc_safe(klen + 1);
    memcpy(kdup, keybuf, klen + 1);
    ctx->mc_sites[ctx->mc_site_count].key = kdup;
    ctx->mc_sites[ctx->mc_site_count].site_gv = gv;
    ctx->mc_site_count++;
    return gv;
}

/* When memcheck is enabled, install internal wrappers `@malloc` / `@free` in
   the LLVM module that forward to `ls_mc_alloc` / `ls_mc_free` with a
   generic site. Existing codegen call sites (LLVMGetNamedFunction("malloc")
   …) resolve to these wrappers transparently. This makes Phase A a drop-in
   activation: no per-call-site changes required. The downside is all leaks
   report kind="unknown"; Phase A.5 introduces cg_emit_alloc with specific
   kinds for major paths (string.upper, vec.grow, …). */
static void cg_install_memcheck_wrappers(CodegenContext *ctx) {
    if (!ctx->memcheck_enabled) return;
    if (LLVMGetNamedFunction(ctx->module, "ls_mc_malloc_wrap")) return;

    LLVMTypeRef ptr = LLVMPointerTypeInContext(ctx->context, 0);
    LLVMTypeRef i64 = LLVMInt64TypeInContext(ctx->context);
    LLVMTypeRef voidt = LLVMVoidTypeInContext(ctx->context);

    /* Make sure ls_mc_alloc / ls_mc_free / ls_mc_realloc are declared (extern). */
    LLVMValueRef mc_alloc = cg_mc_alloc_fn(ctx);
    LLVMValueRef mc_free  = cg_mc_free_fn(ctx);
    LLVMValueRef mc_realloc;
    {
        mc_realloc = LLVMGetNamedFunction(ctx->module, "ls_mc_realloc");
        if (!mc_realloc) {
            LLVMTypeRef rparams[3] = { ptr, i64, ptr };
            LLVMTypeRef rft = LLVMFunctionType(ptr, rparams, 3, 0);
            mc_realloc = LLVMAddFunction(ctx->module, "ls_mc_realloc", rft);
        }
    }

    /* A single shared default LsMcSite for the wrapper paths. */
    LLVMValueRef default_site = cg_make_site(ctx, "unknown", 0, 0);

    /* Save current insert point — we'll generate function bodies and need
       to restore the builder afterwards. */
    LLVMBasicBlockRef saved_bb = LLVMGetInsertBlock(ctx->builder);

    /* --- @malloc(i64) -> ptr  (replaces or wraps user malloc references) --- */
    LLVMValueRef existing_malloc = LLVMGetNamedFunction(ctx->module, "malloc");
    if (existing_malloc) {
        /* Already declared as extern. Set its name aside and rename so we can
           reuse "malloc" for our internal wrapper. */
        LLVMSetValueName2(existing_malloc, "ls_mc_real_malloc",
                          (size_t)strlen("ls_mc_real_malloc"));
    }
    LLVMTypeRef m_params[1] = { i64 };
    LLVMTypeRef m_ft = LLVMFunctionType(ptr, m_params, 1, 0);
    LLVMValueRef m_fn = LLVMAddFunction(ctx->module, "malloc", m_ft);
    LLVMSetLinkage(m_fn, LLVMInternalLinkage);
    LLVMBasicBlockRef m_entry = LLVMAppendBasicBlockInContext(
        ctx->context, m_fn, "entry");
    LLVMPositionBuilderAtEnd(ctx->builder, m_entry);
    LLVMValueRef sz_arg = LLVMGetParam(m_fn, 0);
    LLVMValueRef m_args[2] = { sz_arg, default_site };
    LLVMTypeRef mc_alloc_ty = LLVMGlobalGetValueType(mc_alloc);
    LLVMValueRef m_result = LLVMBuildCall2(ctx->builder, mc_alloc_ty, mc_alloc,
                                           m_args, 2, "p");
    LLVMBuildRet(ctx->builder, m_result);

    /* --- @realloc(ptr, i64) -> ptr  (wraps existing extern realloc) --- */
    LLVMValueRef existing_realloc = LLVMGetNamedFunction(ctx->module, "realloc");
    if (existing_realloc) {
        LLVMSetValueName2(existing_realloc, "ls_mc_real_realloc",
                          (size_t)strlen("ls_mc_real_realloc"));
    }
    LLVMTypeRef r_params[2] = { ptr, i64 };
    LLVMTypeRef r_ft = LLVMFunctionType(ptr, r_params, 2, 0);
    LLVMValueRef r_fn = LLVMAddFunction(ctx->module, "realloc", r_ft);
    LLVMSetLinkage(r_fn, LLVMInternalLinkage);
    LLVMBasicBlockRef r_entry = LLVMAppendBasicBlockInContext(
        ctx->context, r_fn, "entry");
    LLVMPositionBuilderAtEnd(ctx->builder, r_entry);
    LLVMValueRef r_old = LLVMGetParam(r_fn, 0);
    LLVMValueRef r_sz = LLVMGetParam(r_fn, 1);
    LLVMValueRef r_args[3] = { r_old, r_sz, default_site };
    LLVMTypeRef mc_realloc_ty = LLVMGlobalGetValueType(mc_realloc);
    LLVMValueRef r_result = LLVMBuildCall2(ctx->builder, mc_realloc_ty, mc_realloc,
                                           r_args, 3, "p");
    LLVMBuildRet(ctx->builder, r_result);

    /* --- @free(ptr) -> void --- */
    LLVMValueRef existing_free = LLVMGetNamedFunction(ctx->module, "free");
    if (existing_free) {
        LLVMSetValueName2(existing_free, "ls_mc_real_free",
                          (size_t)strlen("ls_mc_real_free"));
    }
    LLVMTypeRef f_params[1] = { ptr };
    LLVMTypeRef f_ft = LLVMFunctionType(voidt, f_params, 1, 0);
    LLVMValueRef f_fn = LLVMAddFunction(ctx->module, "free", f_ft);
    LLVMSetLinkage(f_fn, LLVMInternalLinkage);
    LLVMBasicBlockRef f_entry = LLVMAppendBasicBlockInContext(
        ctx->context, f_fn, "entry");
    LLVMPositionBuilderAtEnd(ctx->builder, f_entry);
    LLVMValueRef p_arg = LLVMGetParam(f_fn, 0);
    LLVMValueRef f_args[2] = { p_arg, default_site };
    LLVMTypeRef mc_free_ty = LLVMGlobalGetValueType(mc_free);
    LLVMBuildCall2(ctx->builder, mc_free_ty, mc_free, f_args, 2, "");
    LLVMBuildRet(ctx->builder, NULL);

    /* --- @calloc(i64, i64) -> ptr ---
       Wraps calloc: multiply nmemb * size, call ls_mc_alloc, then zero memory. */
    LLVMValueRef existing_calloc = LLVMGetNamedFunction(ctx->module, "calloc");
    if (existing_calloc) {
        LLVMSetValueName2(existing_calloc, "ls_mc_real_calloc",
                          (size_t)strlen("ls_mc_real_calloc"));
    }
    LLVMTypeRef c_params[2] = { i64, i64 };
    LLVMTypeRef c_ft = LLVMFunctionType(ptr, c_params, 2, 0);
    LLVMValueRef c_fn = LLVMAddFunction(ctx->module, "calloc", c_ft);
    LLVMSetLinkage(c_fn, LLVMInternalLinkage);
    LLVMBasicBlockRef c_entry = LLVMAppendBasicBlockInContext(
        ctx->context, c_fn, "entry");
    LLVMPositionBuilderAtEnd(ctx->builder, c_entry);
    LLVMValueRef nm = LLVMGetParam(c_fn, 0);
    LLVMValueRef sz = LLVMGetParam(c_fn, 1);
    LLVMValueRef total = LLVMBuildMul(ctx->builder, nm, sz, "total");
    LLVMValueRef c_args[2] = { total, default_site };
    LLVMTypeRef mc_alloc_ty2 = LLVMGlobalGetValueType(mc_alloc);
    LLVMValueRef c_result = LLVMBuildCall2(ctx->builder, mc_alloc_ty2, mc_alloc,
                                           c_args, 2, "cp");
    /* Zero the memory */
    LLVMValueRef memset_fn = LLVMGetNamedFunction(ctx->module, "memset");
    if (!memset_fn) {
        LLVMTypeRef ms_params[3] = { ptr, LLVMInt32TypeInContext(ctx->context), i64 };
        memset_fn = LLVMAddFunction(ctx->module, "memset",
                                     LLVMFunctionType(ptr, ms_params, 3, 0));
    }
    LLVMValueRef ms_args[3] = { c_result,
                                 LLVMConstInt(LLVMInt32TypeInContext(ctx->context), 0, 0),
                                 total };
    LLVMTypeRef ms_ty = LLVMGlobalGetValueType(memset_fn);
    LLVMBuildCall2(ctx->builder, ms_ty, memset_fn, ms_args, 3, "");
    LLVMBuildRet(ctx->builder, c_result);

    /* Restore builder to its previous insert block (if any). */
    if (saved_bb)
        LLVMPositionBuilderAtEnd(ctx->builder, saved_bb);

    /* Marker so we don't install twice — give it a trivial body so the
       verifier doesn't complain about an internal-linkage extern. */
    LLVMValueRef marker = LLVMAddFunction(ctx->module, "ls_mc_malloc_wrap",
                                          LLVMFunctionType(voidt, NULL, 0, 0));
    LLVMSetLinkage(marker, LLVMInternalLinkage);
    LLVMBasicBlockRef mk_bb = LLVMAppendBasicBlockInContext(
        ctx->context, marker, "entry");
    LLVMPositionBuilderAtEnd(ctx->builder, mk_bb);
    LLVMBuildRetVoid(ctx->builder);
    if (saved_bb)
        LLVMPositionBuilderAtEnd(ctx->builder, saved_bb);
}

/* CG_LINE / CG_COL moved to codegen_internal.h (shared across codegen TUs). */






/* ---- LsString LLVM type: { i8*, i32, i32 } = { data, len, cap } ---- */

/* Get or create the LsString LLVM struct type.
   cap == 0 means static literal (data points to global constant, don't free).
   cap > 0 means heap-allocated (caller must free data). */

/* Build an LsString constant struct value from components */

/* Build a static LsString from a global string pointer (cap=0 = static) */


















/* ---- Deep clone helpers for vec[i] reads ----
   When reading an element from a vec, the loaded struct/string value shares
   its data pointers with the vec's internal buffer.  To give the caller an
   independently owned copy (so both the caller variable and the vec element
   can be freed without double-free), we deep-clone any owned string fields.

   Semantics summary:
     v.push(x)   — MOVE: x is consumed, vec owns the data
     y = v[i]    — CLONE: vec retains ownership, y gets an independent copy
     v.pop()     — MOVE out: vec loses the element, caller owns it
*/









/* ---- Temporary string slot management ---- */

/* Register a dynamic string value in the temp slot list.
   Creates an alloca in the function entry block, stores str_val there,
   and registers the alloca for statement-level cleanup.
   Returns str_val unchanged (the SSA value, not a reload). */

/* Mark the last temp slot (if any created since mark) as moved (cap = -1).
   Used after a dynamic string is stored into a variable: prevents double-free
   when both the temp slot and the variable's alloca are cleaned up. */
























/* Forward declaration for struct drop */



/* ---- Struct destructor (RAII) ---- */





/* ---- Type mapping: LS Type -> LLVMTypeRef ---- */



/* ---- Forward declarations (codegen_expr_or_borrow / short_circuit /
   ffi_call / stmt / decl) moved to codegen_internal.h (Step 1 split). ---- */

/* ---- Lvalue pointer resolution ---- */


/* ---- Printf format specifier for a given type ---- */









#undef VEC_MAX_PRINT_ELEMS







/* ---- Match OR-pattern helpers ---- */



/* ---- Expression codegen ---- */

/* A-1 (docs/plan_runtime_primitives.md): structural match of a canonical-path
   call to a std.sys.c primitive — `std.sys.c.malloc/realloc/free/abort`. Mirrors the
   checker's match_stdc_prim. Returns 0=malloc 1=realloc 2=free 3=abort, else -1.
   These lower to exactly the same CRT/runtime calls the bare builtins emitted. */
int cg_match_stdc_prim(AstNode *callee)
{
    if (callee == NULL || callee->kind != AST_FIELD)
        return -1;
    AstNode *mid = callee->as.field_access.object;   /* std.sys.c */
    if (mid == NULL || mid->kind != AST_FIELD)
        return -1;
    if (strcmp(mid->as.field_access.field, "c") != 0)
        return -1;
    AstNode *sysn = mid->as.field_access.object;      /* std.sys */
    if (sysn == NULL || sysn->kind != AST_FIELD)
        return -1;
    if (strcmp(sysn->as.field_access.field, "sys") != 0)
        return -1;
    AstNode *head = sysn->as.field_access.object;     /* std */
    if (head == NULL || head->kind != AST_IDENT)
        return -1;
    if (strcmp(head->as.ident.name, "std") != 0)
        return -1;
    const char *f = callee->as.field_access.field;
    if (strcmp(f, "malloc") == 0)  return 0;
    if (strcmp(f, "realloc") == 0) return 1;
    if (strcmp(f, "free") == 0)    return 2;
    if (strcmp(f, "abort") == 0)   return 3;
    return -1;
}







/* ---- Short-circuit for && and || ---- */


/* ---- Statement codegen ---- */


/* ---- Declaration codegen ---- */




















/* ===== Phase E.2: Windows x64 ABI lowering helpers for extern struct ===== */














/* ---- Declare built-in functions ---- */

static void declare_builtins(CodegenContext *ctx)
{
    LLVMTypeRef ptr_type = LLVMPointerTypeInContext(ctx->context, 0);
    LLVMTypeRef i32_type = LLVMInt32TypeInContext(ctx->context);
    LLVMTypeRef void_type = LLVMVoidTypeInContext(ctx->context);

    /* printf — used by print() builtin */
    LLVMTypeRef printf_args[] = {ptr_type};
    LLVMTypeRef printf_type = LLVMFunctionType(i32_type, printf_args, 1, 1);
    LLVMAddFunction(ctx->module, "printf", printf_type);

    /* __ls_printf — vfprintf to the current sink stream (std.core.sink redirect).
       print()'s emit_printf targets THIS, not printf, so set_sink redirects all
       print output to a file/stderr. Default stream is stdout → byte-identical. */
    LLVMAddFunction(ctx->module, "__ls_printf", printf_type);

    /* puts */
    LLVMTypeRef puts_args[] = {ptr_type};
    LLVMTypeRef puts_type = LLVMFunctionType(i32_type, puts_args, 1, 0);
    LLVMAddFunction(ctx->module, "puts", puts_type);

    /* print() is handled as a compiler intrinsic in codegen_print_call().
       No LLVM function is needed — calls are expanded to printf inline. */

    /* malloc */
    LLVMTypeRef malloc_args[] = {LLVMInt64TypeInContext(ctx->context)};
    LLVMTypeRef malloc_type = LLVMFunctionType(ptr_type, malloc_args, 1, 0);
    LLVMAddFunction(ctx->module, "malloc", malloc_type);

    /* free */
    LLVMTypeRef free_args[] = {ptr_type};
    LLVMTypeRef free_type = LLVMFunctionType(void_type, free_args, 1, 0);
    LLVMAddFunction(ctx->module, "free", free_type);

    /* realloc — used by vec(T) push to grow the data buffer */
    {
        LLVMTypeRef i64_t = LLVMInt64TypeInContext(ctx->context);
        LLVMTypeRef ra_args[] = {ptr_type, i64_t};
        LLVMTypeRef ra_type = LLVMFunctionType(ptr_type, ra_args, 2, 0);
        if (!LLVMGetNamedFunction(ctx->module, "realloc"))
            LLVMAddFunction(ctx->module, "realloc", ra_type);
    }

    /* calloc — used by map(K,V) to allocate zero-initialized bucket arrays */
    {
        LLVMTypeRef i64_t = LLVMInt64TypeInContext(ctx->context);
        LLVMTypeRef ca_args[] = {i64_t, i64_t};
        LLVMTypeRef ca_type = LLVMFunctionType(ptr_type, ca_args, 2, 0);
        if (!LLVMGetNamedFunction(ctx->module, "calloc"))
            LLVMAddFunction(ctx->module, "calloc", ca_type);
    }

    /* sqrt (from math library) */
    LLVMTypeRef sqrt_args[] = {LLVMDoubleTypeInContext(ctx->context)};
    LLVMTypeRef sqrt_type = LLVMFunctionType(
        LLVMDoubleTypeInContext(ctx->context), sqrt_args, 1, 0);
    LLVMAddFunction(ctx->module, "sqrt", sqrt_type);

    /* sprintf — used for f-string value generation */
    LLVMTypeRef sprintf_args[] = {ptr_type, ptr_type};
    LLVMTypeRef sprintf_type = LLVMFunctionType(i32_type, sprintf_args, 2, 1);
    if (!LLVMGetNamedFunction(ctx->module, "sprintf"))
        LLVMAddFunction(ctx->module, "sprintf", sprintf_type);

    /* strlen — used for string operations */
    LLVMTypeRef i64_type = LLVMInt64TypeInContext(ctx->context);
    LLVMTypeRef strlen_args[] = {ptr_type};
    LLVMTypeRef strlen_type = LLVMFunctionType(i64_type, strlen_args, 1, 0);
    if (!LLVMGetNamedFunction(ctx->module, "strlen"))
        LLVMAddFunction(ctx->module, "strlen", strlen_type);

    /* memcpy — used for string concatenation */
    LLVMTypeRef memcpy_args[] = {ptr_type, ptr_type, i64_type};
    LLVMTypeRef memcpy_type = LLVMFunctionType(ptr_type, memcpy_args, 3, 0);
    if (!LLVMGetNamedFunction(ctx->module, "memcpy"))
        LLVMAddFunction(ctx->module, "memcpy", memcpy_type);

    /* memmove — used for overlapping-safe element shifts in vec.remove() */
    LLVMTypeRef memmove_args[] = {ptr_type, ptr_type, i64_type};
    LLVMTypeRef memmove_type = LLVMFunctionType(ptr_type, memmove_args, 3, 0);
    if (!LLVMGetNamedFunction(ctx->module, "memmove"))
        LLVMAddFunction(ctx->module, "memmove", memmove_type);

    /* qsort — used by vec.sort() / vec.sort_by(); signature: qsort(base, nmemb, size, cmp) */
    {
        LLVMTypeRef qsort_params[] = {ptr_type, i64_type, i64_type, ptr_type};
        LLVMTypeRef qsort_type = LLVMFunctionType(void_type, qsort_params, 4, 0);
        if (!LLVMGetNamedFunction(ctx->module, "qsort"))
            LLVMAddFunction(ctx->module, "qsort", qsort_type);
    }

    /* strcmp — used for string comparison */
    LLVMTypeRef strcmp_args[] = {ptr_type, ptr_type};
    LLVMTypeRef strcmp_type = LLVMFunctionType(i32_type, strcmp_args, 2, 0);
    if (!LLVMGetNamedFunction(ctx->module, "strcmp"))
        LLVMAddFunction(ctx->module, "strcmp", strcmp_type);

    /* strstr — used for string.find() / string.contains() */
    LLVMTypeRef strstr_args[] = {ptr_type, ptr_type};
    LLVMTypeRef strstr_type = LLVMFunctionType(ptr_type, strstr_args, 2, 0);
    if (!LLVMGetNamedFunction(ctx->module, "strstr"))
        LLVMAddFunction(ctx->module, "strstr", strstr_type);

    /* strncmp — used for string.starts_with() */
    LLVMTypeRef strncmp_args[] = {ptr_type, ptr_type, i64_type};
    LLVMTypeRef strncmp_type = LLVMFunctionType(i32_type, strncmp_args, 3, 0);
    if (!LLVMGetNamedFunction(ctx->module, "strncmp"))
        LLVMAddFunction(ctx->module, "strncmp", strncmp_type);

    /* __ls_str_replace — runtime helper for string.replace() */
    {
        LLVMTypeRef rp_params[] = {
            ptr_type, i32_type, ptr_type, i32_type,
            ptr_type, i32_type, ptr_type /* out_len ptr */
        };
        LLVMTypeRef rp_type = LLVMFunctionType(ptr_type, rp_params, 7, 0);
        if (!LLVMGetNamedFunction(ctx->module, "__ls_str_replace"))
            LLVMAddFunction(ctx->module, "__ls_str_replace", rp_type);
    }


    /* strcmp — for string.to_bool() */
    {
        LLVMTypeRef sc_params[] = {ptr_type, ptr_type};
        LLVMTypeRef sc_type = LLVMFunctionType(i32_type, sc_params, 2, 0);
        if (!LLVMGetNamedFunction(ctx->module, "strcmp"))
            LLVMAddFunction(ctx->module, "strcmp", sc_type);
    }

    /* ---- FFI platform API declarations ---- */
#ifdef _WIN32
    /* LoadLibraryA(path) -> HMODULE (ptr) */
    LLVMTypeRef lla_args[] = {ptr_type};
    LLVMTypeRef lla_type = LLVMFunctionType(ptr_type, lla_args, 1, 0);
    LLVMAddFunction(ctx->module, "LoadLibraryA", lla_type);

    /* GetProcAddress(hModule, name) -> FARPROC (ptr) */
    LLVMTypeRef gpa_args[] = {ptr_type, ptr_type};
    LLVMTypeRef gpa_type = LLVMFunctionType(ptr_type, gpa_args, 2, 0);
    LLVMAddFunction(ctx->module, "GetProcAddress", gpa_type);

    /* FreeLibrary(hModule) -> BOOL (i32) */
    LLVMTypeRef fl_args[] = {ptr_type};
    LLVMTypeRef fl_type = LLVMFunctionType(i32_type, fl_args, 1, 0);
    LLVMAddFunction(ctx->module, "FreeLibrary", fl_type);
#else
    /* dlopen(path, flags) -> void* */
    LLVMTypeRef dlo_args[] = {ptr_type, i32_type};
    LLVMTypeRef dlo_type = LLVMFunctionType(ptr_type, dlo_args, 2, 0);
    LLVMAddFunction(ctx->module, "dlopen", dlo_type);

    /* dlsym(handle, name) -> void* */
    LLVMTypeRef dls_args[] = {ptr_type, ptr_type};
    LLVMTypeRef dls_type = LLVMFunctionType(ptr_type, dls_args, 2, 0);
    LLVMAddFunction(ctx->module, "dlsym", dls_type);

    /* dlclose(handle) -> int */
    LLVMTypeRef dlc_args[] = {ptr_type};
    LLVMTypeRef dlc_type = LLVMFunctionType(i32_type, dlc_args, 1, 0);
    LLVMAddFunction(ctx->module, "dlclose", dlc_type);
#endif
}

/* Emit __ls_str_replace(s, s_len, old, old_len, new, new_len, out_len_ptr) -> ptr
   Replaces all occurrences of 'old' in 's' with 'new', returns malloc'd buffer. */
static void emit_str_replace_helper(CodegenContext *ctx)
{
    /* In JIT extern_builtins mode, the helper is already defined in __builtins module;
       only declare it (no body) so it resolves via symbol search.
       Exception: when memcheck is enabled, emit locally so malloc calls are tracked
       by ls_mc_alloc instead of the untracked real malloc in the builtins module. */
    if (ctx->extern_builtins && !ctx->memcheck_enabled)
        return;

    LLVMValueRef fn = LLVMGetNamedFunction(ctx->module, "__ls_str_replace");
    if (fn == NULL || LLVMCountBasicBlocks(fn) > 0)
        return;
    if (ctx->extern_builtins && ctx->memcheck_enabled)
        LLVMSetLinkage(fn, LLVMInternalLinkage);

    LLVMTypeRef i8_t = LLVMInt8TypeInContext(ctx->context);
    LLVMTypeRef i32_t = LLVMInt32TypeInContext(ctx->context);
    LLVMTypeRef i64_t = LLVMInt64TypeInContext(ctx->context);
    LLVMTypeRef ptr_t = LLVMPointerTypeInContext(ctx->context, 0);

    /* Save current builder position */
    LLVMBasicBlockRef saved_bb = LLVMGetInsertBlock(ctx->builder);

    /* Parameters: s_data, s_len, old_data, old_len, new_data, new_len, out_len_ptr */
    LLVMValueRef s_data = LLVMGetParam(fn, 0);
    LLVMValueRef s_len = LLVMGetParam(fn, 1);
    LLVMValueRef old_data = LLVMGetParam(fn, 2);
    LLVMValueRef old_len = LLVMGetParam(fn, 3);
    LLVMValueRef new_data = LLVMGetParam(fn, 4);
    LLVMValueRef new_len = LLVMGetParam(fn, 5);
    LLVMValueRef out_len_p = LLVMGetParam(fn, 6);

    LLVMValueRef zero = LLVMConstInt(i32_t, 0, 0);
    LLVMValueRef one = LLVMConstInt(i32_t, 1, 0);

    /* entry */
    LLVMBasicBlockRef entry_bb = LLVMAppendBasicBlockInContext(ctx->context, fn, "entry");
    LLVMPositionBuilderAtEnd(ctx->builder, entry_bb);

    /* Allocas */
    LLVMValueRef count_ptr = cg_entry_alloca(ctx, i32_t, "count");
    LLVMValueRef p_ptr = cg_entry_alloca(ctx, ptr_t, "p");
    LLVMValueRef src_ptr = cg_entry_alloca(ctx, ptr_t, "src");
    LLVMValueRef dst_ptr = cg_entry_alloca(ctx, ptr_t, "dst");

    LLVMBuildStore(ctx->builder, zero, count_ptr);
    LLVMBuildStore(ctx->builder, s_data, p_ptr);

    LLVMValueRef strstr_fn = LLVMGetNamedFunction(ctx->module, "strstr");
    LLVMTypeRef strstr_type = LLVMGlobalGetValueType(strstr_fn);
    LLVMValueRef memcpy_fn = LLVMGetNamedFunction(ctx->module, "memcpy");
    LLVMTypeRef mc_type = LLVMGlobalGetValueType(memcpy_fn);
    LLVMValueRef malloc_fn = LLVMGetNamedFunction(ctx->module, "malloc");
    LLVMTypeRef malloc_type = LLVMGlobalGetValueType(malloc_fn);

    /* Phase 1: count occurrences */
    LLVMBasicBlockRef cnt_cond = LLVMAppendBasicBlockInContext(ctx->context, fn, "cnt.cond");
    LLVMBasicBlockRef cnt_body = LLVMAppendBasicBlockInContext(ctx->context, fn, "cnt.body");
    LLVMBasicBlockRef cnt_end = LLVMAppendBasicBlockInContext(ctx->context, fn, "cnt.end");

    LLVMBuildBr(ctx->builder, cnt_cond);
    LLVMPositionBuilderAtEnd(ctx->builder, cnt_cond);
    LLVMValueRef p_val = LLVMBuildLoad2(ctx->builder, ptr_t, p_ptr, "p.val");
    LLVMValueRef ss_args[] = {p_val, old_data};
    LLVMValueRef found = LLVMBuildCall2(ctx->builder, strstr_type, strstr_fn,
                                        ss_args, 2, "found");
    LLVMValueRef null_ptr = LLVMConstNull(ptr_t);
    LLVMValueRef not_null = LLVMBuildICmp(ctx->builder, LLVMIntNE, found,
                                          null_ptr, "notnull");
    LLVMBuildCondBr(ctx->builder, not_null, cnt_body, cnt_end);

    LLVMPositionBuilderAtEnd(ctx->builder, cnt_body);
    LLVMValueRef cnt = LLVMBuildLoad2(ctx->builder, i32_t, count_ptr, "cnt");
    LLVMBuildStore(ctx->builder, LLVMBuildAdd(ctx->builder, cnt, one, "cnt.inc"),
                   count_ptr);
    /* p = found + old_len */
    LLVMValueRef adv = LLVMBuildGEP2(ctx->builder, i8_t, found, &old_len, 1, "adv");
    LLVMBuildStore(ctx->builder, adv, p_ptr);
    LLVMBuildBr(ctx->builder, cnt_cond);

    LLVMPositionBuilderAtEnd(ctx->builder, cnt_end);

    /* result_len = s_len + count * (new_len - old_len) */
    LLVMValueRef final_count = LLVMBuildLoad2(ctx->builder, i32_t, count_ptr, "fcnt");
    LLVMValueRef diff = LLVMBuildSub(ctx->builder, new_len, old_len, "diff");
    LLVMValueRef delta = LLVMBuildMul(ctx->builder, final_count, diff, "delta");
    LLVMValueRef result_len = LLVMBuildAdd(ctx->builder, s_len, delta, "rlen");

    /* Store out_len */
    LLVMBuildStore(ctx->builder, result_len, out_len_p);

    /* malloc(result_len + 1) */
    LLVMValueRef alloc_sz = LLVMBuildAdd(ctx->builder, result_len, one, "asz");
    LLVMValueRef alloc64 = LLVMBuildZExt(ctx->builder, alloc_sz, i64_t, "asz64");
    LLVMValueRef buf = LLVMBuildCall2(ctx->builder, malloc_type, malloc_fn,
                                      &alloc64, 1, "buf");

    LLVMBuildStore(ctx->builder, s_data, src_ptr);
    LLVMBuildStore(ctx->builder, buf, dst_ptr);

    /* Phase 2: copy with replacements */
    LLVMBasicBlockRef cp_cond = LLVMAppendBasicBlockInContext(ctx->context, fn, "cp.cond");
    LLVMBasicBlockRef cp_body = LLVMAppendBasicBlockInContext(ctx->context, fn, "cp.body");
    LLVMBasicBlockRef cp_end = LLVMAppendBasicBlockInContext(ctx->context, fn, "cp.end");

    LLVMBuildBr(ctx->builder, cp_cond);
    LLVMPositionBuilderAtEnd(ctx->builder, cp_cond);
    LLVMValueRef src_v = LLVMBuildLoad2(ctx->builder, ptr_t, src_ptr, "src.v");
    LLVMValueRef ss2_args[] = {src_v, old_data};
    LLVMValueRef found2 = LLVMBuildCall2(ctx->builder, strstr_type, strstr_fn,
                                         ss2_args, 2, "found2");
    LLVMValueRef not_null2 = LLVMBuildICmp(ctx->builder, LLVMIntNE, found2,
                                           null_ptr, "nn2");
    LLVMBuildCondBr(ctx->builder, not_null2, cp_body, cp_end);

    LLVMPositionBuilderAtEnd(ctx->builder, cp_body);
    /* seg_len = found2 - src */
    LLVMValueRef src2 = LLVMBuildLoad2(ctx->builder, ptr_t, src_ptr, "src2");
    LLVMValueRef dst2 = LLVMBuildLoad2(ctx->builder, ptr_t, dst_ptr, "dst2");
    LLVMValueRef f_int = LLVMBuildPtrToInt(ctx->builder, found2, i64_t, "fint");
    LLVMValueRef s_int = LLVMBuildPtrToInt(ctx->builder, src2, i64_t, "sint");
    LLVMValueRef seg64 = LLVMBuildSub(ctx->builder, f_int, s_int, "seg64");

    /* memcpy(dst, src, seg_len) */
    LLVMValueRef mc1_args[] = {dst2, src2, seg64};
    LLVMBuildCall2(ctx->builder, mc_type, memcpy_fn, mc1_args, 3, "");

    /* dst += seg_len */
    LLVMValueRef seg32 = LLVMBuildTrunc(ctx->builder, seg64, i32_t, "seg32");
    LLVMValueRef dst_adv = LLVMBuildGEP2(ctx->builder, i8_t, dst2, &seg32, 1, "dadv");

    /* memcpy(dst, new_data, new_len) */
    LLVMValueRef new_len64 = LLVMBuildZExt(ctx->builder, new_len, i64_t, "nl64");
    LLVMValueRef mc2_args[] = {dst_adv, new_data, new_len64};
    LLVMBuildCall2(ctx->builder, mc_type, memcpy_fn, mc2_args, 3, "");

    /* dst += new_len */
    LLVMValueRef dst_adv2 = LLVMBuildGEP2(ctx->builder, i8_t, dst_adv,
                                          &new_len, 1, "dadv2");
    LLVMBuildStore(ctx->builder, dst_adv2, dst_ptr);

    /* src = found2 + old_len */
    LLVMValueRef src_adv = LLVMBuildGEP2(ctx->builder, i8_t, found2,
                                         &old_len, 1, "sadv");
    LLVMBuildStore(ctx->builder, src_adv, src_ptr);
    LLVMBuildBr(ctx->builder, cp_cond);

    LLVMPositionBuilderAtEnd(ctx->builder, cp_end);

    /* Copy remainder: remain = s_data + s_len - src */
    LLVMValueRef final_src = LLVMBuildLoad2(ctx->builder, ptr_t, src_ptr, "fsrc");
    LLVMValueRef final_dst = LLVMBuildLoad2(ctx->builder, ptr_t, dst_ptr, "fdst");
    LLVMValueRef s_end_ptr = LLVMBuildGEP2(ctx->builder, i8_t, s_data,
                                           &s_len, 1, "send");
    LLVMValueRef se_int = LLVMBuildPtrToInt(ctx->builder, s_end_ptr, i64_t, "seint");
    LLVMValueRef fs_int = LLVMBuildPtrToInt(ctx->builder, final_src, i64_t, "fsint");
    LLVMValueRef remain = LLVMBuildSub(ctx->builder, se_int, fs_int, "remain");
    /* +1 for null terminator */
    LLVMValueRef remain_plus1 = LLVMBuildAdd(ctx->builder, remain,
                                             LLVMConstInt(i64_t, 1, 0), "rem1");
    LLVMValueRef mc3_args[] = {final_dst, final_src, remain_plus1};
    LLVMBuildCall2(ctx->builder, mc_type, memcpy_fn, mc3_args, 3, "");

    LLVMBuildRet(ctx->builder, buf);

    /* Restore builder position */
    if (saved_bb)
        LLVMPositionBuilderAtEnd(ctx->builder, saved_bb);
}


/* ---- Public API ---- */

void codegen_init(CodegenContext *ctx, const char *module_name)
{
    memset(ctx, 0, sizeof(CodegenContext));

    /* Initialize LLVM targets */
    LLVMInitializeNativeTarget();
    LLVMInitializeNativeAsmPrinter();
    LLVMInitializeNativeAsmParser();

    ctx->context = LLVMContextCreate();
    ctx->module = LLVMModuleCreateWithNameInContext(module_name, ctx->context);
    ctx->builder = LLVMCreateBuilderInContext(ctx->context);

    /* Setup target */
    char *triple = LLVMGetDefaultTargetTriple();
    LLVMSetTarget(ctx->module, triple);

    LLVMTargetRef target;
    char *error = NULL;
    if (LLVMGetTargetFromTriple(triple, &target, &error))
    {
        fprintf(stderr, "error: %s\n", error);
        LLVMDisposeMessage(error);
        LLVMDisposeMessage(triple);
        return;
    }

    ctx->target_machine = LLVMCreateTargetMachine(
        target, triple, "generic", "",
        LLVMCodeGenLevelDefault, LLVMRelocDefault, LLVMCodeModelDefault);

    LLVMTargetDataRef data_layout = LLVMCreateTargetDataLayout(ctx->target_machine);
    LLVMSetModuleDataLayout(ctx->module, data_layout);
    LLVMDisposeTargetData(data_layout);

    LLVMDisposeMessage(triple);

    /* Default optimization config for AOT: O2 + generic (portable). Honours
       LS_OPT / LS_NATIVE / LS_VERIFY_EACH env vars; the CLI overrides it before
       codegen_emit_object runs. (JIT does not call codegen_init, so this only
       affects the AOT path.) */
    ctx->opt = ls_opt_default_aot();

    ctx->current_scope = cg_scope_new(NULL);
}

void codegen_destroy(CodegenContext *ctx)
{
    /* Free scope chain */
    while (ctx->current_scope)
    {
        CgScope *old = ctx->current_scope;
        ctx->current_scope = old->parent;
        cg_scope_free(old);
    }
    free(ctx->struct_types);

    /* M-4.5: release temp has_drop slot arrays */
    free(ctx->temp_drop_slots);
    free(ctx->temp_drop_types);
    ctx->temp_drop_slots = NULL;
    ctx->temp_drop_types = NULL;
    ctx->temp_drop_count = 0;
    ctx->temp_drop_cap = 0;

    /* Free memcheck site cache (keys were malloc'd) */
    for (int i = 0; i < ctx->mc_site_count; i++)
        free(ctx->mc_sites[i].key);
    free(ctx->mc_sites);
    ctx->mc_sites = NULL;
    ctx->mc_site_count = 0;
    ctx->mc_site_cap = 0;

    if (ctx->dib)
        LLVMDisposeDIBuilder(ctx->dib);
    free(ctx->di_files);
    ctx->di_files = NULL;
    ctx->di_file_count = 0;
    ctx->di_file_cap = 0;
    if (ctx->builder)
        LLVMDisposeBuilder(ctx->builder);
    if (ctx->target_machine)
        LLVMDisposeTargetMachine(ctx->target_machine);
    if (ctx->module)
        LLVMDisposeModule(ctx->module);
    if (ctx->context)
        LLVMContextDispose(ctx->context);
}

/* Emit initialization code for a single global VAR_DECL into the current builder
   position.  The global variable must already exist in the LLVM module (created in
   Pass 1).  Only stores an initializer — does NOT create a new alloca or add the
   symbol to the scope (that was done in Pass 1). */
static void emit_global_var_init(CodegenContext *ctx, AstNode *decl)
{
    if (decl == NULL || decl->kind != AST_VAR_DECL || decl->as.var_decl.init == NULL)
        return;
    Type *var_type = decl->resolved_type;
    if (var_type == NULL)
        return;
    /* P1-1: module globals have prefixed LLVM names; root globals use bare names. */
    const char *gname = decl->as.var_decl.name;
    char mod_sym[512];
    if (ctx->current_emit_module && ctx->current_emit_module[0])
    {
        cg_module_fn_symbol(mod_sym, sizeof(mod_sym),
                            ctx->current_emit_module, gname);
        gname = mod_sym;
    }
    LLVMValueRef global = LLVMGetNamedGlobal(ctx->module, gname);
    if (global == NULL)
        return;

    if (var_type->kind == TYPE_ARRAY &&
        decl->as.var_decl.init->kind == AST_ARRAY_LIT)
    {
        LLVMValueRef const_arr = codegen_expr(ctx, decl->as.var_decl.init);
        if (const_arr)
        {
            LLVMSetInitializer(global, const_arr);
        }
        else
        {
            AstNode *lit = decl->as.var_decl.init;
            int count = lit->as.array_lit.count;
            LLVMTypeRef i64_type = LLVMInt64TypeInContext(ctx->context);
            LLVMValueRef zero = LLVMConstInt(i64_type, 0, 0);
            LLVMTypeRef arr_llvm = type_to_llvm(ctx, var_type);
            for (int j = 0; j < count; j++)
            {
                LLVMValueRef elem_val = codegen_expr(ctx, lit->as.array_lit.elements[j]);
                if (elem_val == NULL)
                    continue;
                LLVMValueRef idx = LLVMConstInt(i64_type, (uint64_t)j, 0);
                LLVMValueRef indices[2] = {zero, idx};
                LLVMValueRef gep = LLVMBuildGEP2(ctx->builder, arr_llvm,
                                                 global, indices, 2, "g.init");
                LLVMBuildStore(ctx->builder, elem_val, gep);
            }
        }
    }
    else
    {
        LLVMValueRef init = codegen_expr(ctx, decl->as.var_decl.init);
        if (init && LLVMIsConstant(init))
        {
            LLVMSetInitializer(global, init);
        }
        else if (init)
        {
            LLVMBuildStore(ctx->builder, init, global);
        }
    }
}

int codegen_compile(CodegenContext *ctx, AstNode *ast,
                    struct ModuleRegistry *registry)
{
    if (ast == NULL || ast->kind != AST_PROGRAM)
        return -1;

    /* Ensure we have a base scope (JIT path may not call codegen_init) */
    if (ctx->current_scope == NULL)
    {
        ctx->current_scope = cg_scope_new(NULL);
    }

    /* D1: create the DIBuilder skeleton (-g only) before any body is emitted. */
    cg_di_init(ctx);

    declare_builtins(ctx);

    /* Memcheck: install internal @malloc/@free wrappers BEFORE any helper
       fn body is emitted, so all subsequent calls route through the tracker.
       declare_builtins above declared malloc/free as externs; the wrapper
       installer will rename those externs and shadow them with internals. */
    cg_install_memcheck_wrappers(ctx);

    emit_str_replace_helper(ctx);

    /* Process imported modules in two separate passes so that transitive
       dependencies (e.g. std.time importing std.os) have all symbols
       forward-declared before any function body is generated.

       Pass A (all modules): forward-declare structs, externs, fn signatures,
                             global variable slots.
       Pass B (all modules): generate function / impl bodies. */
    if (registry)
    {
        /* Pass A — forward-declare everything across all modules */
        for (int m = 0; m < registry->count; m++)
        {
            AstNode *mod_ast = registry->modules[m].ast;
            if (mod_ast == NULL || mod_ast->kind != AST_PROGRAM)
                continue;

            /* L-009: functions in this module get module-prefixed symbols. */
            ctx->current_emit_module = registry->modules[m].name;

            for (int i = 0; i < mod_ast->as.program.decl_count; i++)
            {
                AstNode *decl = mod_ast->as.program.decls[i];
                if (decl->kind == AST_STRUCT_DECL)
                {
                    codegen_struct_decl(ctx, decl);
                }
                else if (decl->kind == AST_EXTERN_STRUCT_DECL)
                {
                    codegen_extern_struct_decl(ctx, decl);
                }
                else if (decl->kind == AST_EXTERN_BLOCK)
                {
                    codegen_extern_block(ctx, decl);
                }
                else if (decl->kind == AST_EXTERN_FN)
                {
                    codegen_extern_fn(ctx, decl);
                }
                else if (decl->kind == AST_ENUM_DECL)
                {
                    codegen_enum_decl(ctx, decl);
                }
                else if (decl->kind == AST_FN_DECL)
                {
                    Type *fn_type_ml = decl->resolved_type;
                    if (fn_type_ml && fn_type_ml->kind == TYPE_FUNCTION &&
                        decl->as.fn_decl.type_param_count == 0)
                    {
                        LLVMTypeRef fn_type = type_to_llvm(ctx, fn_type_ml);
                        /* L-009: module-prefixed symbol name. */
                        char sym[512];
                        cg_module_fn_symbol(sym, sizeof(sym),
                            ctx->current_emit_module, decl->as.fn_decl.name);
                        if (!LLVMGetNamedFunction(ctx->module, sym))
                        {
                            LLVMValueRef fn_g = LLVMAddFunction(ctx->module,
                                sym, fn_type);
                            /* Phase E.4: imported user-module functions get
                               internal linkage so their symbols don't collide
                               with libc/runtime names at AOT link time. */
                            LLVMSetLinkage(fn_g, LLVMInternalLinkage);
                        }
                    }
                }
                else if (decl->kind == AST_VAR_DECL && decl->resolved_type)
                {
                    /* P1-1: module global — prefix LLVM symbol name to avoid
                       cross-module collisions (same scheme as function mangling). */
                    Type *var_type = decl->resolved_type;
                    LLVMTypeRef llvm_type = type_to_llvm(ctx, var_type);
                    char mod_sym[512];
                    cg_module_fn_symbol(mod_sym, sizeof(mod_sym),
                                        ctx->current_emit_module,
                                        decl->as.var_decl.name);
                    if (!LLVMGetNamedGlobal(ctx->module, mod_sym))
                    {
                        LLVMValueRef gv = LLVMAddGlobal(ctx->module, llvm_type, mod_sym);
                        LLVMSetLinkage(gv, LLVMInternalLinkage);
                        LLVMSetInitializer(gv, LLVMConstNull(llvm_type));
                    }
                }
            }
        }

        /* Pass A done — clear module context before root-file declarations. */
        ctx->current_emit_module = NULL;

        /* Pass B — generate function bodies (all symbols now visible) */
        for (int m = 0; m < registry->count; m++)
        {
            AstNode *mod_ast = registry->modules[m].ast;
            if (mod_ast == NULL || mod_ast->kind != AST_PROGRAM)
                continue;

            /* L-009: functions in this module get module-prefixed symbols. */
            ctx->current_emit_module = registry->modules[m].name;
            /* D1: their subprograms map lines into the module's own file. */
            ctx->current_emit_file = registry->modules[m].file_path;

            /* P1-1: push a temporary scope layer so module functions can
               resolve this module's global variables by their bare names.
               Each global is stored under its bare name but points to the
               prefixed LLVM global created in Pass A. */
            push_scope(ctx);
            for (int i = 0; i < mod_ast->as.program.decl_count; i++)
            {
                AstNode *d = mod_ast->as.program.decls[i];
                if (d->kind == AST_VAR_DECL && d->resolved_type)
                {
                    char msym[512];
                    cg_module_fn_symbol(msym, sizeof(msym),
                                        ctx->current_emit_module,
                                        d->as.var_decl.name);
                    LLVMValueRef gv = LLVMGetNamedGlobal(ctx->module, msym);
                    if (gv)
                        cg_scope_define(ctx->current_scope,
                                        d->as.var_decl.name, gv,
                                        d->resolved_type, NULL);
                }
            }

            for (int i = 0; i < mod_ast->as.program.decl_count; i++)
            {
                AstNode *decl = mod_ast->as.program.decls[i];
                if (decl->kind == AST_FN_DECL)
                {
                    codegen_fn_decl(ctx, decl);
                }
                else if (decl->kind == AST_IMPL_DECL)
                {
                    codegen_impl_decl(ctx, decl);
                }
                else if (decl->kind == AST_IMPL_TRAIT_DECL)
                {
                    codegen_impl_trait_decl(ctx, decl);
                }
            }

            /* Pop the module-globals scope; the next module must not see them. */
            pop_scope(ctx);
        }
        /* Root/main-file functions follow — unmangled. */
        ctx->current_emit_module = NULL;
        ctx->current_emit_file = NULL;
    }

    /* Phase E.2: ensure all extern struct LLVM types are emitted with their
       bodies set BEFORE any extern fn declaration runs through extern_fn_type
       (which calls LLVMABISizeOfType and requires non-opaque struct). */
    cg_predeclare_extern_structs(ctx, ast);

    /* Pass 1: Declare all structs, function signatures, and FFI lib globals */
    for (int i = 0; i < ast->as.program.decl_count; i++)
    {
        AstNode *decl = ast->as.program.decls[i];
        if (decl->kind == AST_STRUCT_DECL)
        {
            codegen_struct_decl(ctx, decl);
        }
        else if (decl->kind == AST_ENUM_DECL)
        {
            codegen_enum_decl(ctx, decl);
        }
        else if (decl->kind == AST_FN_DECL)
        {
            /* Forward-declare function */
            Type *fn_type_ml = decl->resolved_type;
            if (fn_type_ml && fn_type_ml->kind == TYPE_FUNCTION)
            {
                LLVMTypeRef fn_type = type_to_llvm(ctx, fn_type_ml);
                /* AOT: fn main() (void) → i32 main() for CRT compatibility */
                bool fwd_main = (strcmp(decl->as.fn_decl.name, "main") == 0 &&
                                 decl->as.fn_decl.param_count == 0);
                bool fwd_main_void = (fwd_main &&
                                      fn_type_ml->as.function.return_type->kind == TYPE_VOID);
                /* bug #22: AOT entry main gets C sig int main(argc,argv). Must match
                   the signature codegen_fn_decl builds, or the body reuses this
                   forward decl and argv never reaches __ls_set_args. */
                bool fwd_main_entry = (fwd_main && ctx->aot_entry &&
                                       ctx->current_emit_module == NULL);
                if (fwd_main_void || fwd_main_entry)
                {
                    LLVMTypeRef i32_t = LLVMInt32TypeInContext(ctx->context);
                    if (fwd_main_entry)
                    {
                        LLVMTypeRef pt = LLVMPointerType(LLVMInt8TypeInContext(ctx->context), 0);
                        LLVMTypeRef ps[] = { i32_t, pt };
                        fn_type = LLVMFunctionType(i32_t, ps, 2, 0);
                    }
                    else
                    {
                        fn_type = LLVMFunctionType(i32_t, NULL, 0, 0);
                    }
                }
                LLVMValueRef fn = LLVMGetNamedFunction(ctx->module, decl->as.fn_decl.name);
                if (fn == NULL)
                {
                    LLVMAddFunction(ctx->module, decl->as.fn_decl.name, fn_type);
                }
            }
        }
        else if (decl->kind == AST_EXTERN_FN)
        {
            codegen_extern_fn(ctx, decl);
        }
        else if (decl->kind == AST_EXTERN_STRUCT_DECL)
        {
            codegen_extern_struct_decl(ctx, decl);
        }
        else if (decl->kind == AST_EXTERN_BLOCK)
        {
            codegen_extern_block(ctx, decl);
        }
        else if (decl->kind == AST_LOAD_LIB)
        {
            codegen_load_lib(ctx, decl);
        }
        else if (decl->kind == AST_VAR_DECL && decl->resolved_type)
        {
            /* Global variable declaration */
            Type *var_type = decl->resolved_type;
            LLVMTypeRef llvm_type = type_to_llvm(ctx, var_type);
            LLVMValueRef global = LLVMAddGlobal(ctx->module, llvm_type,
                                                decl->as.var_decl.name);
            LLVMSetLinkage(global, LLVMExternalLinkage);
            LLVMSetInitializer(global, LLVMConstNull(llvm_type));
            cg_scope_define(ctx->current_scope, decl->as.var_decl.name,
                            global, var_type, NULL);
        }
    }

    /* Generate __ls_ffi_init if there are lib declarations */
    codegen_ffi_init(ctx, ast);

    /* Generate __ls_global_stmts: initialise global variables AND execute top-level
       statements in **source order**.  This replaces the old __ls_global_init and the
       previous "inject stmts into main" approach.  The function is called once at the
       start of main() (after __ls_ffi_init) so that FFI libraries are available to
       top-level expressions. */
    {
        /* Determine whether there is anything to do.
           We need the function if:
           (a) any imported module has a global VAR_DECL with an initializer, OR
           (b) the main module has any VAR_DECL with an initializer, OR
           (c) the main module has any top-level non-declaration statement. */
        bool has_global_stmts = false;

        if (registry)
        {
            for (int m = 0; m < registry->count && !has_global_stmts; m++)
            {
                AstNode *mod_ast = registry->modules[m].ast;
                if (mod_ast == NULL || mod_ast->kind != AST_PROGRAM)
                    continue;
                for (int i = 0; i < mod_ast->as.program.decl_count && !has_global_stmts; i++)
                {
                    AstNode *d = mod_ast->as.program.decls[i];
                    if (d->kind == AST_VAR_DECL && d->as.var_decl.init)
                        has_global_stmts = true;
                }
            }
        }
        for (int i = 0; i < ast->as.program.decl_count && !has_global_stmts; i++)
        {
            AstNode *d = ast->as.program.decls[i];
            if (d->kind == AST_VAR_DECL && d->as.var_decl.init)
            {
                has_global_stmts = true;
            }
            else
            {
                switch (d->kind)
                {
                case AST_FN_DECL:
                case AST_STRUCT_DECL:
                case AST_ENUM_DECL:
                case AST_IMPL_DECL:
                case AST_TRAIT_DECL:
                case AST_IMPL_TRAIT_DECL:
                case AST_EXTERN_FN:
                case AST_EXTERN_STRUCT_DECL:
                case AST_EXTERN_BLOCK:
                case AST_LOAD_LIB:
                case AST_MODULE_DECL:
                case AST_IMPORT_DECL:
                    break;
                default:
                    has_global_stmts = true;
                    break;
                }
            }
        }

        if (has_global_stmts)
        {
            LLVMTypeRef void_type = LLVMVoidTypeInContext(ctx->context);
            LLVMTypeRef gs_fn_type = LLVMFunctionType(void_type, NULL, 0, 0);
            LLVMValueRef gs_fn = LLVMAddFunction(ctx->module, "__ls_global_stmts",
                                                 gs_fn_type);
            LLVMBasicBlockRef gs_entry = LLVMAppendBasicBlockInContext(
                ctx->context, gs_fn, "entry");
            LLVMPositionBuilderAtEnd(ctx->builder, gs_entry);

            LLVMValueRef saved_fn = ctx->current_fn;
            ctx->current_fn = gs_fn;

            /* Push a local scope so any variables declared inside top-level statements
               live in their own scope and get cleaned up on return.
               Global variables were already defined in ctx->current_scope (outer) during
               Pass 1 and remain accessible through the scope chain. */
            push_scope(ctx);

            /* 1. Initialise imported module globals first (preserve import order).
                  P1-1: set current_emit_module so emit_global_var_init resolves
                  the prefixed LLVM global name for each module. */
            if (registry)
            {
                for (int m = 0; m < registry->count; m++)
                {
                    AstNode *mod_ast = registry->modules[m].ast;
                    if (mod_ast == NULL || mod_ast->kind != AST_PROGRAM)
                        continue;
                    ctx->current_emit_module = registry->modules[m].name;
                    for (int i = 0; i < mod_ast->as.program.decl_count; i++)
                    {
                        AstNode *d = mod_ast->as.program.decls[i];
                        if (d->kind == AST_VAR_DECL && d->as.var_decl.init)
                            emit_global_var_init(ctx, d);
                    }
                    ctx->current_emit_module = NULL;
                }
            }

            /* 2. Process main module top-level items in source order:
                  - VAR_DECL with init  → emit_global_var_init (store to LLVM global)
                  - other statements    → codegen_stmt (execute in place)
                  - declarations        → skip (handled in other passes) */
            for (int i = 0; i < ast->as.program.decl_count; i++)
            {
                /* Stop if the current block already has a terminator
                   (e.g. a top-level return/break, though rare). */
                LLVMBasicBlockRef cur_bb = LLVMGetInsertBlock(ctx->builder);
                if (cur_bb == NULL || LLVMGetBasicBlockTerminator(cur_bb) != NULL)
                    break;

                AstNode *d = ast->as.program.decls[i];
                switch (d->kind)
                {
                case AST_FN_DECL:
                case AST_STRUCT_DECL:
                case AST_ENUM_DECL:
                case AST_IMPL_DECL:
                case AST_TRAIT_DECL:
                case AST_IMPL_TRAIT_DECL:
                case AST_EXTERN_FN:
                case AST_EXTERN_STRUCT_DECL:
                case AST_EXTERN_BLOCK:
                case AST_LOAD_LIB:
                case AST_MODULE_DECL:
                case AST_IMPORT_DECL:
                    break; /* handled elsewhere */
                case AST_VAR_DECL:
                    /* Only emit runtime initializer — the global was already declared
                       in Pass 1.  Do NOT call codegen_stmt here, which would create a
                       local alloca that shadows the global. */
                    if (d->as.var_decl.init)
                        emit_global_var_init(ctx, d);
                    break;
                default:
                    /* Top-level statement: compile inline */
                    codegen_stmt(ctx, d);
                    break;
                }
            }

            /* Cleanup any locals created inside top-level statements, then return. */
            LLVMBasicBlockRef cur_bb = LLVMGetInsertBlock(ctx->builder);
            if (cur_bb != NULL && LLVMGetBasicBlockTerminator(cur_bb) == NULL)
            {
                emit_cleanup_to(ctx, NULL, NULL);
                LLVMBuildRetVoid(ctx->builder);
            }

            pop_scope(ctx);
            ctx->current_fn = saved_fn;
        }
    }

    /* Generate __ls_global_cleanup for globals that own heap data.
       Called just before main() returns so global values don't leak at program exit. */
    {
        /* Collect all global VAR_DECLs that need cleanup. */
        bool has_global_cleanup = false;

#define DECL_NEEDS_GLOBAL_CLEANUP(d)                                      \
    ((d)->kind == AST_VAR_DECL && (d)->resolved_type &&                   \
     ((d)->resolved_type->kind == TYPE_STRUCT ||                          \
      ((d)->resolved_type->kind == TYPE_ENUM &&                           \
       (d)->resolved_type->as.enom.has_drop)))

        for (int i = 0; i < ast->as.program.decl_count && !has_global_cleanup; i++)
        {
            if (DECL_NEEDS_GLOBAL_CLEANUP(ast->as.program.decls[i]))
                has_global_cleanup = true;
        }
        if (!has_global_cleanup && registry)
        {
            for (int m = 0; m < registry->count && !has_global_cleanup; m++)
            {
                AstNode *mod_ast = registry->modules[m].ast;
                if (mod_ast == NULL || mod_ast->kind != AST_PROGRAM)
                    continue;
                for (int i = 0; i < mod_ast->as.program.decl_count && !has_global_cleanup; i++)
                {
                    if (DECL_NEEDS_GLOBAL_CLEANUP(mod_ast->as.program.decls[i]))
                        has_global_cleanup = true;
                }
            }
        }

        if (has_global_cleanup)
        {
            LLVMTypeRef void_type = LLVMVoidTypeInContext(ctx->context);
            LLVMTypeRef cleanup_fn_type = LLVMFunctionType(void_type, NULL, 0, 0);
            LLVMValueRef cleanup_fn = LLVMAddFunction(ctx->module, "__ls_global_cleanup",
                                                      cleanup_fn_type);
            LLVMBasicBlockRef cleanup_entry = LLVMAppendBasicBlockInContext(
                ctx->context, cleanup_fn, "entry");
            LLVMPositionBuilderAtEnd(ctx->builder, cleanup_entry);

            LLVMValueRef saved_fn2 = ctx->current_fn;
            ctx->current_fn = cleanup_fn;
/* Helper macro: emit cleanup for one global var decl.
   P1-3: gname is the LLVM global name (prefixed for module globals, bare for root). */
#define EMIT_GLOBAL_CLEANUP(decl, gname)                                                                 \
    do                                                                                                   \
    {                                                                                                    \
        if ((decl)->kind != AST_VAR_DECL || !(decl)->resolved_type)                                      \
            break;                                                                                       \
        LLVMValueRef gv = LLVMGetNamedGlobal(ctx->module, (gname));                                      \
        if (!gv)                                                                                         \
            break;                                                                                       \
        if ((decl)->resolved_type->kind == TYPE_STRUCT)                                                  \
        {                                                                                                \
            Type *gst = (decl)->resolved_type;                                                           \
            char gdrop_name[256];                                                                        \
            snprintf(gdrop_name, sizeof(gdrop_name), "%s.__drop", struct_llvm_name(gst));                \
            LLVMValueRef gdrop = LLVMGetNamedFunction(ctx->module, gdrop_name);                          \
            if (gdrop == NULL)                                                                           \
                gdrop = cg_declare_pending_generic_method(ctx, gdrop_name);                              \
            if (gdrop != NULL)                                                                           \
            {                                                                                            \
                LLVMTypeRef gdft = LLVMGlobalGetValueType(gdrop);                                        \
                LLVMBuildCall2(ctx->builder, gdft, gdrop, &gv, 1, "");                                  \
            }                                                                                            \
        }                                                                                                \
    } while (0)

            /* P1-3: Free imported module globals (prefixed names). */
            if (registry)
            {
                for (int m = 0; m < registry->count; m++)
                {
                    AstNode *mod_ast = registry->modules[m].ast;
                    if (mod_ast == NULL || mod_ast->kind != AST_PROGRAM)
                        continue;
                    const char *mname = registry->modules[m].name;
                    for (int i = 0; i < mod_ast->as.program.decl_count; i++)
                    {
                        AstNode *d = mod_ast->as.program.decls[i];
                        if (d->kind != AST_VAR_DECL || !d->resolved_type)
                            continue;
                        char msym2[512];
                        cg_module_fn_symbol(msym2, sizeof(msym2),
                                            mname, d->as.var_decl.name);
                        EMIT_GLOBAL_CLEANUP(d, msym2);
                    }
                }
            }
            /* Free main module globals (bare names). */
            for (int i = 0; i < ast->as.program.decl_count; i++)
            {
                AstNode *d = ast->as.program.decls[i];
                if (d->kind == AST_VAR_DECL && d->resolved_type)
                    EMIT_GLOBAL_CLEANUP(d, d->as.var_decl.name);
            }

#undef EMIT_GLOBAL_CLEANUP
#undef DECL_NEEDS_GLOBAL_CLEANUP

            LLVMBuildRetVoid(ctx->builder);
            ctx->current_fn = saved_fn2;
        }
    }

    /* Pass 2a: Process all IMPL declarations first (sets drop_fn for structs) */
    for (int i = 0; i < ast->as.program.decl_count; i++)
    {
        AstNode *decl = ast->as.program.decls[i];
        if (decl->kind == AST_IMPL_DECL)
        {
            codegen_impl_decl(ctx, decl);
        }
        else if (decl->kind == AST_IMPL_TRAIT_DECL)
        {
            codegen_impl_trait_decl(ctx, decl);
        }
    }

    /* G1.5: Emit pending generic method instantiations (from checker).
       Each entry has a cloned+type-checked fn_decl and a mangled function name.
       We forward-declare all of them first, then emit bodies, then free the ASTs. */
    if (ctx->pending_gm_count > 0)
    {
        /* Forward-declare all pending generic methods */
        for (int i = 0; i < ctx->pending_gm_count; i++)
        {
            AstNode *cfn = ctx->pending_generic_methods[i].cloned_fn;
            const char *mname = ctx->pending_generic_methods[i].mangled_name;
            Type *fn_type_ml = cfn->resolved_type;
            if (fn_type_ml == NULL || fn_type_ml->kind != TYPE_FUNCTION)
                continue;
            LLVMTypeRef fn_type = type_to_llvm(ctx, fn_type_ml);
            if (!LLVMGetNamedFunction(ctx->module, mname))
                LLVMAddFunction(ctx->module, mname, fn_type);
        }

        /* Emit bodies */
        for (int i = 0; i < ctx->pending_gm_count; i++)
        {
            AstNode *cfn = ctx->pending_generic_methods[i].cloned_fn;
            const char *mname = ctx->pending_generic_methods[i].mangled_name;
            if (cfn == NULL || cfn->resolved_type == NULL)
                continue;

            /* A1 (module generics): the same instantiation (e.g. identity(int))
               can be queued by more than one module/checker. Their bodies are
               structurally identical, so emit only the first; skip any whose
               LLVM function already has a body (basic blocks), else we'd append
               a second entry block to it and produce invalid IR. */
            LLVMValueRef existing = LLVMGetNamedFunction(ctx->module, mname);
            if (existing && LLVMCountBasicBlocks(existing) > 0)
                continue;

            /* D1: the cloned template AST keeps the defining module's line
               numbers — point the instance's DIFile at that module's source
               (falls back to the root file when the stamp/lookup misses). */
            ctx->current_emit_file = NULL;
            Type *gst = ctx->pending_generic_methods[i].struct_type;
            if (registry && gst && gst->kind == TYPE_STRUCT &&
                gst->as.strukt.generic_module)
            {
                ModuleInfo *gmod = module_find(registry,
                                               gst->as.strukt.generic_module);
                if (gmod)
                    ctx->current_emit_file = gmod->file_path;
            }

            /* codegen_fn_decl uses node->as.fn_decl.name as the LLVM function name.
               Temporarily set it to the mangled name (e.g. "Pair(int,string).get_first"). */
            const char *orig_name = cfn->as.fn_decl.name;
            cfn->as.fn_decl.name = (char *)mname;
            codegen_fn_decl(ctx, cfn);
            cfn->as.fn_decl.name = (char *)orig_name;
            ctx->current_emit_file = NULL;
        }

        /* Free cloned AST nodes and mangled names */
        for (int i = 0; i < ctx->pending_gm_count; i++)
        {
            if (ctx->pending_generic_methods[i].cloned_fn)
                ast_free(ctx->pending_generic_methods[i].cloned_fn);
            free(ctx->pending_generic_methods[i].mangled_name);
        }
        free(ctx->pending_generic_methods);
        ctx->pending_generic_methods = NULL;
        ctx->pending_gm_count = 0;
    }

    /* Pass 2.5: Generate auto-drop functions for structs that have has_drop=true but
       no user-defined __drop (drop_fn==NULL after Pass 2a).
       We iterate the struct registry multiple times so that member structs get their
       drop functions generated before container structs that depend on them. */
    {
        bool progress = true;
        while (progress)
        {
            progress = false;
            for (int i = 0; i < ctx->struct_type_count; i++)
            {
                Type *st = ctx->struct_types[i].ls_type;
                if (st == NULL || st->kind != TYPE_STRUCT)
                    continue;
                if (!st->as.strukt.has_drop)
                    continue;
                if (st->as.strukt.drop_fn != NULL)
                    continue; /* user-defined or already generated */

                /* Check that all member structs with has_drop already have their drop_fn set */
                bool deps_ready = true;
                for (int fi = 0; fi < st->as.strukt.field_count; fi++)
                {
                    Type *ft = st->as.strukt.fields[fi].type;
                    if (ft == NULL)
                        continue;
                    if (ft->kind == TYPE_STRUCT && ft->as.strukt.has_drop && ft->as.strukt.drop_fn == NULL)
                    {
                        deps_ready = false;
                        break;
                    }
                }
                if (!deps_ready)
                    continue;

                emit_auto_drop_fn(ctx, st);
                progress = true;
            }
        }
    }

    /* Pre-Pass 2b: If there is global setup work (__ls_global_stmts or __ls_ffi_init)
       and no user-defined main(), create a minimal synthetic main() { ret 0 }.
       The injection step below will prepend the setup calls before the ret.
       We only create synthetic main when there is actually something to run;
       this avoids polluting library/builtins modules with an unwanted main symbol. */
    {
        bool has_setup = (LLVMGetNamedFunction(ctx->module, "__ls_global_stmts") != NULL ||
                          LLVMGetNamedFunction(ctx->module, "__ls_ffi_init") != NULL);
        bool has_user_main = (LLVMGetNamedFunction(ctx->module, "main") != NULL);
        if (has_setup && !has_user_main)
        {
            LLVMTypeRef i32_t = LLVMInt32TypeInContext(ctx->context);
            LLVMTypeRef main_ft;
            /* bug #22: AOT synthetic main also gets C sig int main(argc,argv) so
               proc.args() works in top-level (mainless) programs too. */
            if (ctx->aot_entry)
            {
                LLVMTypeRef pt = LLVMPointerType(LLVMInt8TypeInContext(ctx->context), 0);
                LLVMTypeRef ps[] = { i32_t, pt };
                main_ft = LLVMFunctionType(i32_t, ps, 2, 0);
            }
            else
            {
                main_ft = LLVMFunctionType(i32_t, NULL, 0, 0);
            }
            LLVMValueRef syn_main = LLVMAddFunction(ctx->module, "main", main_ft);
            LLVMBasicBlockRef entry = LLVMAppendBasicBlockInContext(
                ctx->context, syn_main, "entry");
            LLVMPositionBuilderAtEnd(ctx->builder, entry);
            LLVMBuildRet(ctx->builder, LLVMConstInt(i32_t, 0, 0));
            ctx->current_fn = NULL;
        }
    }

    /* Pass 2b: Generate all function bodies and other decls */
    for (int i = 0; i < ast->as.program.decl_count; i++)
    {
        AstNode *decl = ast->as.program.decls[i];
        switch (decl->kind)
        {
        case AST_STRUCT_DECL:
        case AST_ENUM_DECL:
            /* Already handled */
            break;
        case AST_FN_DECL:
            codegen_fn_decl(ctx, decl);
            break;
        case AST_IMPL_DECL:
        case AST_IMPL_TRAIT_DECL:
            /* Already handled in Pass 2a */
            break;
        case AST_EXTERN_FN:
        case AST_EXTERN_STRUCT_DECL:
        case AST_EXTERN_BLOCK:
        case AST_LOAD_LIB:
        case AST_VAR_DECL:
            /* Handled in Pass 1 (__ls_global_stmts for VAR_DECL init) */
            break;
        default:
            /* Top-level statements are compiled inside __ls_global_stmts above */
            break;
        }
    }

    /* Inject calls to __ls_ffi_init and __ls_global_stmts at the start of main() */
    {
        LLVMValueRef main_fn = LLVMGetNamedFunction(ctx->module, "main");
        LLVMValueRef ffi_init = LLVMGetNamedFunction(ctx->module, "__ls_ffi_init");
        LLVMValueRef global_stmts = LLVMGetNamedFunction(ctx->module, "__ls_global_stmts");

        if (main_fn && LLVMCountBasicBlocks(main_fn) > 0)
        {
            LLVMBasicBlockRef entry = LLVMGetEntryBasicBlock(main_fn);
            LLVMValueRef first = LLVMGetFirstInstruction(entry);
            if (first)
            {
                LLVMBuilderRef tmp = LLVMCreateBuilderInContext(ctx->context);
                LLVMPositionBuilderBefore(tmp, first);
                LLVMTypeRef init_type = LLVMFunctionType(
                    LLVMVoidTypeInContext(ctx->context), NULL, 0, 0);
                /* bug #22: forward argc/argv to __ls_set_args before any setup
                   runs, so proc.args() works in AOT. main has the C signature
                   (argc, argv) only when ctx->aot_entry gave it params. */
                if (ctx->aot_entry && LLVMCountParams(main_fn) >= 2)
                {
                    LLVMTypeRef i32_t = LLVMInt32TypeInContext(ctx->context);
                    LLVMTypeRef pt = LLVMPointerType(LLVMInt8TypeInContext(ctx->context), 0);
                    LLVMTypeRef sa_params[] = { i32_t, pt };
                    LLVMTypeRef sa_type = LLVMFunctionType(
                        LLVMVoidTypeInContext(ctx->context), sa_params, 2, 0);
                    LLVMValueRef sa_fn = LLVMGetNamedFunction(ctx->module, "__ls_set_args");
                    if (!sa_fn)
                        sa_fn = LLVMAddFunction(ctx->module, "__ls_set_args", sa_type);
                    LLVMValueRef sa_args[] = {
                        LLVMGetParam(main_fn, 0), LLVMGetParam(main_fn, 1) };
                    LLVMBuildCall2(tmp, sa_type, sa_fn, sa_args, 2, "");
                }
                if (ctx->memcheck_enabled)
                {
                    LLVMValueRef mc_ensure = LLVMGetNamedFunction(ctx->module,
                                                                  "ls_mc_ensure_report");
                    if (!mc_ensure) {
                        mc_ensure = LLVMAddFunction(ctx->module, "ls_mc_ensure_report",
                                                    init_type);
                    }
                    LLVMBuildCall2(tmp, init_type, mc_ensure, NULL, 0, "");
                }
                if (ffi_init)
                    LLVMBuildCall2(tmp, init_type, ffi_init, NULL, 0, "");
                if (global_stmts)
                    LLVMBuildCall2(tmp, init_type, global_stmts, NULL, 0, "");
                LLVMDisposeBuilder(tmp);
            }
        }
    }

    /* Inject __ls_global_cleanup call before every ret in main() (BUG #1 fix) */
    {
        LLVMValueRef main_fn = LLVMGetNamedFunction(ctx->module, "main");
        LLVMValueRef cleanup_fn = LLVMGetNamedFunction(ctx->module, "__ls_global_cleanup");
        if (main_fn && cleanup_fn)
        {
            LLVMTypeRef cleanup_type = LLVMGlobalGetValueType(cleanup_fn);
            for (LLVMBasicBlockRef bb = LLVMGetFirstBasicBlock(main_fn);
                 bb != NULL; bb = LLVMGetNextBasicBlock(bb))
            {
                LLVMValueRef term = LLVMGetBasicBlockTerminator(bb);
                if (term && LLVMGetInstructionOpcode(term) == LLVMRet)
                {
                    LLVMBuilderRef tmp = LLVMCreateBuilderInContext(ctx->context);
                    LLVMPositionBuilderBefore(tmp, term);
                    LLVMBuildCall2(tmp, cleanup_type, cleanup_fn, NULL, 0, "");
                    LLVMDisposeBuilder(tmp);
                }
            }
        }
    }

    /* Flush CRT output streams before every ret in the AOT entry main, AFTER
       global cleanup. The AOT exe links msvcrt + ucrt (docs/crt_mismatch_bug.md);
       with redirected (fully buffered) stdout the exit-time flush could run on a
       different CRT than printf/puts' buffer → ~15% of runs lost ALL output
       (rc=0, empty stdout) intermittently. __ls_flush_out (runtime/builtins.c, same
       CRT as the prints) calls fflush(NULL) so output is written while this CRT is
       live. AOT-only: JIT resolves runtime symbols via a fixed AbsoluteSymbols list
       and ls.exe uses a single /MD CRT that flushes correctly, so JIT never flaked. */
    if (ctx->aot_entry)
    {
        LLVMValueRef main_fn = LLVMGetNamedFunction(ctx->module, "main");
        if (main_fn && LLVMCountBasicBlocks(main_fn) > 0)
        {
            LLVMTypeRef flush_type = LLVMFunctionType(
                LLVMVoidTypeInContext(ctx->context), NULL, 0, 0);
            LLVMValueRef flush_fn = LLVMGetNamedFunction(ctx->module, "__ls_flush_out");
            if (!flush_fn)
                flush_fn = LLVMAddFunction(ctx->module, "__ls_flush_out", flush_type);
            for (LLVMBasicBlockRef bb = LLVMGetFirstBasicBlock(main_fn);
                 bb != NULL; bb = LLVMGetNextBasicBlock(bb))
            {
                LLVMValueRef term = LLVMGetBasicBlockTerminator(bb);
                if (term && LLVMGetInstructionOpcode(term) == LLVMRet)
                {
                    LLVMBuilderRef tmp = LLVMCreateBuilderInContext(ctx->context);
                    LLVMPositionBuilderBefore(tmp, term);
                    LLVMBuildCall2(tmp, flush_type, flush_fn, NULL, 0, "");
                    LLVMDisposeBuilder(tmp);
                }
            }
        }
    }

    /* B (docs/plan_fma_coldpath.md): mark the process-exit sink noreturn+cold in
       ONE place, regardless of which of the many abort / bounds-check / unwrap
       codegen paths first declared it. LLVM then propagates coldness to every
       block that branches to it, laying the failure paths off the hot path. */
    {
        LLVMValueRef pe = LLVMGetNamedFunction(ctx->module, "__ls_proc_exit");
        if (pe) cg_mark_noreturn_cold(ctx, pe);
    }

    /* D1: materialise deferred DI nodes before the verifier / pass pipeline. */
    cg_di_finalize(ctx);

    /* Verify module */
    char *error = NULL;
    if (LLVMVerifyModule(ctx->module, LLVMReturnStatusAction, &error))
    {
        fprintf(stderr, "[codegen] module verification failed:\n%s\n", error);
        LLVMDisposeMessage(error);
        return -1;
    }
    LLVMDisposeMessage(error);

    return ctx->had_error ? -1 : 0;
}

int codegen_emit_object(CodegenContext *ctx, const char *output_path)
{
    /* Build a TargetMachine configured for the requested opt level + CPU
       (host when ctx->opt.native, else generic). The init-time TM is kept only
       for the module data layout, which is invariant across CPU features on a
       given triple. Both the IR pass pipeline and the backend codegen run on
       this configured TM. */
    char *triple = LLVMGetDefaultTargetTriple();
    LLVMTargetMachineRef tm = ls_opt_create_target_machine(triple, &ctx->opt);
    LLVMDisposeMessage(triple);
    if (tm == NULL) tm = ctx->target_machine;  /* fallback: keep building */

    ls_opt_run_passes(ctx->module, tm, &ctx->opt);

    char *error = NULL;
    if (LLVMTargetMachineEmitToFile(tm, ctx->module,
                                    (char *)output_path,
                                    LLVMObjectFile, &error))
    {
        fprintf(stderr, "error emitting object: %s\n", error);
        LLVMDisposeMessage(error);
        if (tm != ctx->target_machine) LLVMDisposeTargetMachine(tm);
        return -1;
    }
    if (tm != ctx->target_machine) LLVMDisposeTargetMachine(tm);
    return 0;
}

void codegen_dump_ir(CodegenContext *ctx)
{
    LLVMDumpModule(ctx->module);
}

char *codegen_get_ir(CodegenContext *ctx)
{
    return LLVMPrintModuleToString(ctx->module);
}
