/* builtins_io_cg.c — Codegen side of the built-in `io` module (Phase 9 v1).
 *
 * Each io.<fn>(...) call lowers to a sequence of libc calls (fopen / fread /
 * fwrite / fclose / fseek / ftell / malloc / free) plus inline construction
 * of the appropriate Result(T, string) / bool / int return value.
 *
 * Both AOT (clang link) and JIT (LLJIT process-symbol resolver) find the
 * libc symbols against the C runtime that ls.exe is already linked against —
 * no FFI, no LoadLibrary, no extra runtime archive.
 *
 * Caveats (v1):
 *   - ftell/fseek use C `long` which is 32-bit on Windows MSVC, capping
 *     read_file at ~2 GB. The IR declares them as i32 here. Linux builds
 *     are not validated.
 *   - File handles (`File.handle`) are not auto-dropped; users must call
 *     `io.close(f)` explicitly. Calling close twice on the same handle is
 *     undefined.
 */
#define LS_INCLUDE_CODEGEN 1
#include "builtins_io.h"
#include "codegen.h"
#include "common.h"

#include <llvm-c/Core.h>
#include <string.h>

/* ---- Small helpers ---- */

static LLVMValueRef get_or_decl(LLVMModuleRef mod, const char *name,
                                LLVMTypeRef ret, LLVMTypeRef *params, unsigned n,
                                int is_vararg) {
    LLVMValueRef fn = LLVMGetNamedFunction(mod, name);
    if (fn) return fn;
    LLVMTypeRef ft = LLVMFunctionType(ret, params, n, is_vararg);
    return LLVMAddFunction(mod, name, ft);
}

/* Build the variant payload struct LLVM type (mirrors codegen.c's static
   build_variant_payload_struct so we don't have to expose it). */
static LLVMTypeRef variant_struct(CodegenContext *ctx, Type *enum_type, int v) {
    int pc = enum_type->as.enom.variants[v].payload_count;
    if (pc == 0)
        return LLVMStructTypeInContext(ctx->context, NULL, 0, 0);
    LLVMTypeRef *fields = (LLVMTypeRef *)malloc_safe((size_t)pc * sizeof(LLVMTypeRef));
    for (int i = 0; i < pc; i++) {
        Type *pt = enum_type->as.enom.variants[v].payload_types[i];
        fields[i] = type_to_llvm(ctx, pt);
    }
    LLVMTypeRef ty = LLVMStructTypeInContext(ctx->context, fields, (unsigned)pc, 0);
    free(fields);
    return ty;
}

/* Hoist an alloca to the entry block to keep stack usage bounded. */
static LLVMValueRef hoist_alloca(CodegenContext *ctx, LLVMTypeRef ty,
                                 const char *name) {
    LLVMBasicBlockRef entry = LLVMGetEntryBasicBlock(ctx->current_fn);
    LLVMBuilderRef tmp_b = LLVMCreateBuilderInContext(ctx->context);
    LLVMValueRef first = LLVMGetFirstInstruction(entry);
    if (first) LLVMPositionBuilderBefore(tmp_b, first);
    else       LLVMPositionBuilderAtEnd(tmp_b, entry);
    LLVMValueRef a = LLVMBuildAlloca(tmp_b, ty, name);
    LLVMDisposeBuilder(tmp_b);
    return a;
}

/* Zero-fill a memory region (count bytes). */
static void zero_mem(CodegenContext *ctx, LLVMValueRef ptr,
                     unsigned long long bytes) {
    LLVMTypeRef i8 = LLVMInt8TypeInContext(ctx->context);
    LLVMTypeRef i32 = LLVMInt32TypeInContext(ctx->context);
    LLVMTypeRef i64 = LLVMInt64TypeInContext(ctx->context);
    LLVMTypeRef ptr_ty = LLVMPointerTypeInContext(ctx->context, 0);
    LLVMTypeRef ms_params[3] = { ptr_ty, i32, i64 };
    LLVMValueRef ms = get_or_decl(ctx->module, "memset", ptr_ty, ms_params, 3, 0);
    LLVMValueRef args[3] = {
        ptr,
        LLVMConstInt(i32, 0, 0),
        LLVMConstInt(i64, bytes, 0)
    };
    LLVMTypeRef ft = LLVMGlobalGetValueType(ms);
    LLVMBuildCall2(ctx->builder, ft, ms, args, 3, "");
    (void)i8;
}

/* Memcpy n bytes from src to dst. */
static void mem_copy(CodegenContext *ctx, LLVMValueRef dst, LLVMValueRef src,
                     LLVMValueRef n_i64) {
    LLVMTypeRef ptr_ty = LLVMPointerTypeInContext(ctx->context, 0);
    LLVMTypeRef i64 = LLVMInt64TypeInContext(ctx->context);
    LLVMTypeRef params[3] = { ptr_ty, ptr_ty, i64 };
    LLVMValueRef mc = get_or_decl(ctx->module, "memcpy", ptr_ty, params, 3, 0);
    LLVMValueRef args[3] = { dst, src, n_i64 };
    LLVMTypeRef ft = LLVMGlobalGetValueType(mc);
    LLVMBuildCall2(ctx->builder, ft, mc, args, 3, "");
}

/* Set the discriminant byte of a Result enum (already alloca'd). */
static void set_disc(CodegenContext *ctx, LLVMValueRef enum_alloca,
                     LLVMTypeRef enum_llvm, int disc) {
    LLVMTypeRef i8 = LLVMInt8TypeInContext(ctx->context);
    LLVMValueRef disc_p = LLVMBuildStructGEP2(ctx->builder, enum_llvm,
                                              enum_alloca, 0, "io.disc.p");
    LLVMBuildStore(ctx->builder,
                   LLVMConstInt(i8, (unsigned long long)disc, 0), disc_p);
}

/* Store a payload field value at variant struct slot `field_idx`.
   Caller has already set the discriminant. */
static void set_payload_field(CodegenContext *ctx, LLVMValueRef enum_alloca,
                              LLVMTypeRef enum_llvm, Type *enum_type,
                              int variant_idx, int field_idx,
                              LLVMValueRef val) {
    LLVMTypeRef vstruct = variant_struct(ctx, enum_type, variant_idx);
    LLVMValueRef pl_p = LLVMBuildStructGEP2(ctx->builder, enum_llvm, enum_alloca,
                                            1, "io.pl.p");
    LLVMValueRef fld_p = LLVMBuildStructGEP2(ctx->builder, vstruct, pl_p,
                                             (unsigned)field_idx, "io.fld.p");
    LLVMBuildStore(ctx->builder, val, fld_p);
}

/* Build an Err(LsString_static_literal) into the result alloca and branch. */
static void emit_err_static(CodegenContext *ctx, LLVMValueRef result_alloca,
                            LLVMTypeRef result_llvm, Type *result_type,
                            const char *msg, int err_variant_idx) {
    set_disc(ctx, result_alloca, result_llvm, err_variant_idx);
    LLVMValueRef err_str = ls_string_from_literal(ctx, msg, "io.err");
    set_payload_field(ctx, result_alloca, result_llvm, result_type,
                      err_variant_idx, 0, err_str);
}

/* Round len+1 up to next pow2 >= 16 (matches ls_str_alloc_cap in codegen.c). */
static LLVMValueRef compute_cap(CodegenContext *ctx, LLVMValueRef len_i32) {
    /* cap = 16; while (cap < len+1) cap *= 2; — emit as loop-free formula via
       intrinsic: ctlz on (len) then 1 << (32 - clz). For simplicity emit a
       small loop in IR. */
    LLVMTypeRef i32 = LLVMInt32TypeInContext(ctx->context);
    LLVMValueRef need = LLVMBuildAdd(ctx->builder, len_i32,
                                     LLVMConstInt(i32, 1, 0), "io.need");
    LLVMValueRef cap_a = hoist_alloca(ctx, i32, "io.cap");
    LLVMBuildStore(ctx->builder, LLVMConstInt(i32, 16, 0), cap_a);

    LLVMBasicBlockRef cond_bb = LLVMAppendBasicBlockInContext(
        ctx->context, ctx->current_fn, "io.cap.cond");
    LLVMBasicBlockRef body_bb = LLVMAppendBasicBlockInContext(
        ctx->context, ctx->current_fn, "io.cap.body");
    LLVMBasicBlockRef done_bb = LLVMAppendBasicBlockInContext(
        ctx->context, ctx->current_fn, "io.cap.done");
    LLVMBuildBr(ctx->builder, cond_bb);

    LLVMPositionBuilderAtEnd(ctx->builder, cond_bb);
    LLVMValueRef cap_v = LLVMBuildLoad2(ctx->builder, i32, cap_a, "io.cap.v");
    LLVMValueRef cmp = LLVMBuildICmp(ctx->builder, LLVMIntSLT, cap_v, need, "io.cap.lt");
    LLVMBuildCondBr(ctx->builder, cmp, body_bb, done_bb);

    LLVMPositionBuilderAtEnd(ctx->builder, body_bb);
    LLVMValueRef cap_v2 = LLVMBuildLoad2(ctx->builder, i32, cap_a, "io.cap.v2");
    LLVMValueRef cap_n = LLVMBuildShl(ctx->builder, cap_v2,
                                      LLVMConstInt(i32, 1, 0), "io.cap.n");
    LLVMBuildStore(ctx->builder, cap_n, cap_a);
    LLVMBuildBr(ctx->builder, cond_bb);

    LLVMPositionBuilderAtEnd(ctx->builder, done_bb);
    return LLVMBuildLoad2(ctx->builder, i32, cap_a, "io.cap.final");
}

/* ---- libc declarations ---- */

static LLVMValueRef decl_fopen(CodegenContext *ctx) {
    LLVMTypeRef ptr_ty = LLVMPointerTypeInContext(ctx->context, 0);
    LLVMTypeRef params[2] = { ptr_ty, ptr_ty };
    return get_or_decl(ctx->module, "fopen", ptr_ty, params, 2, 0);
}

static LLVMValueRef decl_fclose(CodegenContext *ctx) {
    LLVMTypeRef ptr_ty = LLVMPointerTypeInContext(ctx->context, 0);
    LLVMTypeRef i32 = LLVMInt32TypeInContext(ctx->context);
    LLVMTypeRef params[1] = { ptr_ty };
    return get_or_decl(ctx->module, "fclose", i32, params, 1, 0);
}

static LLVMValueRef decl_fread(CodegenContext *ctx) {
    LLVMTypeRef ptr_ty = LLVMPointerTypeInContext(ctx->context, 0);
    LLVMTypeRef i64 = LLVMInt64TypeInContext(ctx->context);
    /* size_t fread(void *buf, size_t sz, size_t n, FILE *fp) */
    LLVMTypeRef params[4] = { ptr_ty, i64, i64, ptr_ty };
    return get_or_decl(ctx->module, "fread", i64, params, 4, 0);
}

static LLVMValueRef decl_fwrite(CodegenContext *ctx) {
    LLVMTypeRef ptr_ty = LLVMPointerTypeInContext(ctx->context, 0);
    LLVMTypeRef i64 = LLVMInt64TypeInContext(ctx->context);
    LLVMTypeRef params[4] = { ptr_ty, i64, i64, ptr_ty };
    return get_or_decl(ctx->module, "fwrite", i64, params, 4, 0);
}

static LLVMValueRef decl_fseek(CodegenContext *ctx) {
    /* int fseek(FILE *, long off, int whence). On Win64 long is 32-bit. */
    LLVMTypeRef ptr_ty = LLVMPointerTypeInContext(ctx->context, 0);
    LLVMTypeRef i32 = LLVMInt32TypeInContext(ctx->context);
    LLVMTypeRef params[3] = { ptr_ty, i32, i32 };
    return get_or_decl(ctx->module, "fseek", i32, params, 3, 0);
}

static LLVMValueRef decl_ftell(CodegenContext *ctx) {
    /* long ftell(FILE *). i32 on Win64. */
    LLVMTypeRef ptr_ty = LLVMPointerTypeInContext(ctx->context, 0);
    LLVMTypeRef i32 = LLVMInt32TypeInContext(ctx->context);
    LLVMTypeRef params[1] = { ptr_ty };
    return get_or_decl(ctx->module, "ftell", i32, params, 1, 0);
}

static LLVMValueRef decl_malloc(CodegenContext *ctx) {
    LLVMTypeRef ptr_ty = LLVMPointerTypeInContext(ctx->context, 0);
    LLVMTypeRef i64 = LLVMInt64TypeInContext(ctx->context);
    LLVMTypeRef params[1] = { i64 };
    return get_or_decl(ctx->module, "malloc", ptr_ty, params, 1, 0);
}

/* 64-bit positioning. Win MSVC has _fseeki64 / _ftelli64; Linux glibc has
   fseeko64 / ftello64. LS primarily targets Windows; the symbol is selected
   at ls.exe compile time so each platform's IR uses its own libc names. */
#ifdef _WIN32
#  define LS_FSEEK64 "_fseeki64"
#  define LS_FTELL64 "_ftelli64"
#else
#  define LS_FSEEK64 "fseeko64"
#  define LS_FTELL64 "ftello64"
#endif

static LLVMValueRef decl_fseek64(CodegenContext *ctx) {
    /* int _fseeki64(FILE*, i64 off, int whence) */
    LLVMTypeRef ptr_ty = LLVMPointerTypeInContext(ctx->context, 0);
    LLVMTypeRef i32 = LLVMInt32TypeInContext(ctx->context);
    LLVMTypeRef i64 = LLVMInt64TypeInContext(ctx->context);
    LLVMTypeRef params[3] = { ptr_ty, i64, i32 };
    return get_or_decl(ctx->module, LS_FSEEK64, i32, params, 3, 0);
}

static LLVMValueRef decl_ftell64(CodegenContext *ctx) {
    /* i64 _ftelli64(FILE*) */
    LLVMTypeRef ptr_ty = LLVMPointerTypeInContext(ctx->context, 0);
    LLVMTypeRef i64 = LLVMInt64TypeInContext(ctx->context);
    LLVMTypeRef params[1] = { ptr_ty };
    return get_or_decl(ctx->module, LS_FTELL64, i64, params, 1, 0);
}

static LLVMValueRef decl_remove(CodegenContext *ctx) {
    /* int remove(const char *path) */
    LLVMTypeRef ptr_ty = LLVMPointerTypeInContext(ctx->context, 0);
    LLVMTypeRef i32 = LLVMInt32TypeInContext(ctx->context);
    LLVMTypeRef params[1] = { ptr_ty };
    return get_or_decl(ctx->module, "remove", i32, params, 1, 0);
}

/* Get the fopen-mode global string array. Built on first use. */
static LLVMValueRef get_open_mode_table(CodegenContext *ctx) {
    LLVMValueRef existing = LLVMGetNamedGlobal(ctx->module, "ls_io_modes");
    if (existing) return existing;

    LLVMTypeRef ptr_ty = LLVMPointerTypeInContext(ctx->context, 0);
    /* Need the C-string pointers. Build them via BuildGlobalStringPtr inside
       a position-anchored builder. We'll create the global as a constant
       initializer of pointers obtained from LLVMAddGlobal + LLVMConstString. */

    /* Build 6 i8 array constants for the mode strings. */
    static const char *MODES[6] = { "r", "w", "a", "rb", "wb", "ab" };
    LLVMValueRef gptrs[6];
    for (int i = 0; i < 6; i++) {
        char name[32];
        snprintf(name, sizeof(name), "ls_io_mode_%d", i);
        LLVMValueRef gv = LLVMGetNamedGlobal(ctx->module, name);
        if (!gv) {
            LLVMValueRef cstr = LLVMConstStringInContext(
                ctx->context, MODES[i], (unsigned)strlen(MODES[i]), 0);
            LLVMTypeRef arr_ty = LLVMTypeOf(cstr);
            gv = LLVMAddGlobal(ctx->module, arr_ty, name);
            LLVMSetInitializer(gv, cstr);
            LLVMSetLinkage(gv, LLVMPrivateLinkage);
            LLVMSetGlobalConstant(gv, 1);
        }
        gptrs[i] = gv;
    }

    LLVMTypeRef arr_ty = LLVMArrayType(ptr_ty, 6);
    LLVMValueRef init = LLVMConstArray(ptr_ty, gptrs, 6);
    LLVMValueRef tbl = LLVMAddGlobal(ctx->module, arr_ty, "ls_io_modes");
    LLVMSetInitializer(tbl, init);
    LLVMSetLinkage(tbl, LLVMPrivateLinkage);
    LLVMSetGlobalConstant(tbl, 1);
    return tbl;
}

/* Pull the C mode string pointer out of an OpenMode enum value (i8 disc). */
static LLVMValueRef extract_mode_str(CodegenContext *ctx,
                                     LLVMValueRef open_mode_val) {
    LLVMTypeRef ptr_ty = LLVMPointerTypeInContext(ctx->context, 0);
    LLVMTypeRef i32 = LLVMInt32TypeInContext(ctx->context);

    /* OpenMode enum layout: { i8 disc, [0 x i8] payload }. ExtractValue(0) → disc. */
    LLVMValueRef disc_i8 = LLVMBuildExtractValue(ctx->builder, open_mode_val,
                                                 0, "io.mode.disc");
    LLVMValueRef disc_i32 = LLVMBuildZExt(ctx->builder, disc_i8, i32, "io.mode.i32");

    LLVMValueRef tbl = get_open_mode_table(ctx);
    LLVMTypeRef arr_ty = LLVMArrayType(ptr_ty, 6);
    LLVMValueRef idxs[2] = {
        LLVMConstInt(i32, 0, 0),
        disc_i32,
    };
    LLVMValueRef slot = LLVMBuildInBoundsGEP2(ctx->builder, arr_ty, tbl, idxs, 2,
                                              "io.mode.slot");
    return LLVMBuildLoad2(ctx->builder, ptr_ty, slot, "io.mode.ptr");
}

/* Get LsString.data (i8*) from an LsString value. */
static LLVMValueRef ls_str_data_v(CodegenContext *ctx, LLVMValueRef str_val) {
    return LLVMBuildExtractValue(ctx->builder, str_val, 0, "io.s.d");
}

/* Get LsString.len (i32) from an LsString value. */
static LLVMValueRef ls_str_len_v(CodegenContext *ctx, LLVMValueRef str_val) {
    return LLVMBuildExtractValue(ctx->builder, str_val, 1, "io.s.l");
}

/* ---- Per-op emitters ---- */

/* Common: open file, slurp all bytes into a freshly-malloc'd buffer,
   construct an LsString and return Result(string,string).
   `fp_val`: i8* FILE handle (already opened). On entry we are at the
   current insert block. line/col are the LS source location used for
   memcheck site labelling. Returns the loaded Result enum value. */
static LLVMValueRef emit_slurp_file(CodegenContext *ctx, LLVMValueRef fp_val,
                                    Type *result_type, bool close_after,
                                    int line, int col) {
    LLVMTypeRef ptr_ty = LLVMPointerTypeInContext(ctx->context, 0);
    LLVMTypeRef i32 = LLVMInt32TypeInContext(ctx->context);
    LLVMTypeRef i64 = LLVMInt64TypeInContext(ctx->context);
    LLVMTypeRef i8  = LLVMInt8TypeInContext(ctx->context);

    LLVMTypeRef result_llvm = type_to_llvm(ctx, result_type);
    LLVMTargetDataRef td = LLVMGetModuleDataLayout(ctx->module);
    unsigned long long result_sz = LLVMABISizeOfType(td, result_llvm);

    LLVMValueRef result_alloca = hoist_alloca(ctx, result_llvm, "io.slurp.res");
    zero_mem(ctx, result_alloca, result_sz);

    /* Branch on fp == NULL */
    LLVMValueRef null_p = LLVMConstPointerNull(ptr_ty);
    LLVMValueRef fp_is_null = LLVMBuildICmp(ctx->builder, LLVMIntEQ, fp_val,
                                            null_p, "io.fp.null");

    LLVMBasicBlockRef ok_bb = LLVMAppendBasicBlockInContext(
        ctx->context, ctx->current_fn, "io.slurp.ok");
    LLVMBasicBlockRef err_bb = LLVMAppendBasicBlockInContext(
        ctx->context, ctx->current_fn, "io.slurp.err");
    LLVMBasicBlockRef done_bb = LLVMAppendBasicBlockInContext(
        ctx->context, ctx->current_fn, "io.slurp.done");
    LLVMBuildCondBr(ctx->builder, fp_is_null, err_bb, ok_bb);

    /* err: open failed (path missing / perms). */
    LLVMPositionBuilderAtEnd(ctx->builder, err_bb);
    emit_err_static(ctx, result_alloca, result_llvm, result_type,
                    "io: open/read failed", 1);
    LLVMBuildBr(ctx->builder, done_bb);

    /* ok: fseek end → ftell → fseek start → malloc → fread → close */
    LLVMPositionBuilderAtEnd(ctx->builder, ok_bb);

    /* Save current position so we can read "from here to EOF" without
       disturbing where the user's file cursor was (matters for read_all on
       a previously-seeked handle). */
    LLVMValueRef fseek = decl_fseek(ctx);
    LLVMTypeRef fseek_ty = LLVMGlobalGetValueType(fseek);
    LLVMValueRef ftell = decl_ftell(ctx);
    LLVMTypeRef ftell_ty = LLVMGlobalGetValueType(ftell);

    LLVMValueRef saved = LLVMBuildCall2(ctx->builder, ftell_ty, ftell, &fp_val, 1,
                                        "io.saved");
    LLVMValueRef fseek_args_end[3] = {
        fp_val, LLVMConstInt(i32, 0, 0), LLVMConstInt(i32, 2, 0) /* SEEK_END */
    };
    LLVMBuildCall2(ctx->builder, fseek_ty, fseek, fseek_args_end, 3, "io.seek.end");
    LLVMValueRef total = LLVMBuildCall2(ctx->builder, ftell_ty, ftell, &fp_val, 1,
                                        "io.total");
    /* sz = total - saved (bytes from current pos to EOF). */
    LLVMValueRef sz_i32 = LLVMBuildSub(ctx->builder, total, saved, "io.size");

    /* Guard: if sz < 0 or saved < 0, ftell errored — treat as Err. */
    LLVMValueRef sz_neg = LLVMBuildICmp(ctx->builder, LLVMIntSLT, sz_i32,
                                        LLVMConstInt(i32, 0, 0), "io.sz.neg");
    LLVMBasicBlockRef sz_ok_bb = LLVMAppendBasicBlockInContext(
        ctx->context, ctx->current_fn, "io.sz.ok");
    LLVMBasicBlockRef sz_err_bb = LLVMAppendBasicBlockInContext(
        ctx->context, ctx->current_fn, "io.sz.err");
    LLVMBuildCondBr(ctx->builder, sz_neg, sz_err_bb, sz_ok_bb);

    LLVMPositionBuilderAtEnd(ctx->builder, sz_err_bb);
    if (close_after) {
        LLVMValueRef fclose = decl_fclose(ctx);
        LLVMTypeRef fclose_ty = LLVMGlobalGetValueType(fclose);
        LLVMBuildCall2(ctx->builder, fclose_ty, fclose, &fp_val, 1, "");
    }
    emit_err_static(ctx, result_alloca, result_llvm, result_type,
                    "io: ftell failed", 1);
    LLVMBuildBr(ctx->builder, done_bb);

    LLVMPositionBuilderAtEnd(ctx->builder, sz_ok_bb);

    /* Restore the saved position so fread starts there. */
    LLVMValueRef fseek_args_restore[3] = {
        fp_val, saved, LLVMConstInt(i32, 0, 0) /* SEEK_SET */
    };
    LLVMBuildCall2(ctx->builder, fseek_ty, fseek, fseek_args_restore, 3, "io.seek.restore");

    /* cap = next_pow2(size+1) ≥ 16 */
    LLVMValueRef cap_i32 = compute_cap(ctx, sz_i32);
    LLVMValueRef cap_i64 = LLVMBuildSExt(ctx->builder, cap_i32, i64, "io.cap.i64");

    /* Use the LS-aware allocator helper so the slurp buffer carries a
       proper memcheck site (kind="io.slurp", LS source line/col). When
       memcheck is off this still goes to plain malloc. */
    LLVMValueRef buf = cg_emit_alloc(ctx, cap_i64, "io.slurp", line, col);
    (void)decl_malloc;  /* still useful as fallback declaration helper */

    /* fread(buf, 1, size, fp) */
    LLVMValueRef sz_i64 = LLVMBuildSExt(ctx->builder, sz_i32, i64, "io.sz.i64");
    LLVMValueRef fread = decl_fread(ctx);
    LLVMTypeRef fread_ty = LLVMGlobalGetValueType(fread);
    LLVMValueRef fread_args[4] = { buf, LLVMConstInt(i64, 1, 0), sz_i64, fp_val };
    LLVMValueRef nread_i64 = LLVMBuildCall2(ctx->builder, fread_ty, fread,
                                            fread_args, 4, "io.nread");
    LLVMValueRef nread_i32 = LLVMBuildTrunc(ctx->builder, nread_i64, i32, "io.nread.i32");

    /* buf[nread] = 0 (null-terminate) */
    LLVMValueRef term_idx[1] = { nread_i64 };
    LLVMValueRef term_p = LLVMBuildInBoundsGEP2(ctx->builder, i8, buf, term_idx, 1,
                                                "io.term.p");
    LLVMBuildStore(ctx->builder, LLVMConstInt(i8, 0, 0), term_p);

    if (close_after) {
        LLVMValueRef fclose = decl_fclose(ctx);
        LLVMTypeRef fclose_ty = LLVMGlobalGetValueType(fclose);
        LLVMBuildCall2(ctx->builder, fclose_ty, fclose, &fp_val, 1, "");
    }

    /* Build LsString { data=buf, len=nread, cap=cap } */
    LLVMValueRef str_v = ls_string_make(ctx, buf, nread_i32, cap_i32);
    set_disc(ctx, result_alloca, result_llvm, 0); /* Ok */
    set_payload_field(ctx, result_alloca, result_llvm, result_type, 0, 0, str_v);
    LLVMBuildBr(ctx->builder, done_bb);

    LLVMPositionBuilderAtEnd(ctx->builder, done_bb);
    return LLVMBuildLoad2(ctx->builder, result_llvm, result_alloca, "io.slurp.v");
}

/* io.read_file(path) -> Result(string, string) */
static LLVMValueRef emit_read_file(CodegenContext *ctx, AstNode **args,
                                   Type *result_type) {
    LLVMValueRef path_v = codegen_expr(ctx, args[0]);
    if (!path_v) return NULL;
    LLVMValueRef path_data = ls_str_data_v(ctx, path_v);

    /* Mode: "rb" — text mode CRLF translation would corrupt sizes. */
    LLVMValueRef mode_str = LLVMBuildGlobalStringPtr(ctx->builder, "rb",
                                                     "io.rb");
    LLVMValueRef fopen_fn = decl_fopen(ctx);
    LLVMTypeRef fopen_ty = LLVMGlobalGetValueType(fopen_fn);
    LLVMValueRef fopen_args[2] = { path_data, mode_str };
    LLVMValueRef fp = LLVMBuildCall2(ctx->builder, fopen_ty, fopen_fn,
                                     fopen_args, 2, "io.fp");

    /* Use the path arg's source location as the call-site label — it sits
       on the same LS line as the io.read_file(...) call. */
    int line = args[0] ? args[0]->line : 0;
    int col  = args[0] ? args[0]->column : 0;
    return emit_slurp_file(ctx, fp, result_type, true, line, col);
}

/* Emit a write of (data, len) bytes to fp; returns nwritten i64. */
static LLVMValueRef emit_fwrite_bytes(CodegenContext *ctx, LLVMValueRef fp,
                                      LLVMValueRef data_ptr,
                                      LLVMValueRef len_i64) {
    LLVMTypeRef i64 = LLVMInt64TypeInContext(ctx->context);
    LLVMValueRef fwrite = decl_fwrite(ctx);
    LLVMTypeRef fwrite_ty = LLVMGlobalGetValueType(fwrite);
    LLVMValueRef args[4] = { data_ptr, LLVMConstInt(i64, 1, 0), len_i64, fp };
    return LLVMBuildCall2(ctx->builder, fwrite_ty, fwrite, args, 4, "io.nwrote");
}

/* Common write: open(path, "wb"), fwrite, fclose; return Result(int, string). */
static LLVMValueRef emit_write_file(CodegenContext *ctx, AstNode **args,
                                    Type *result_type) {
    LLVMTypeRef ptr_ty = LLVMPointerTypeInContext(ctx->context, 0);
    LLVMTypeRef i32 = LLVMInt32TypeInContext(ctx->context);
    LLVMTypeRef i64 = LLVMInt64TypeInContext(ctx->context);

    LLVMValueRef path_v    = codegen_expr(ctx, args[0]);
    LLVMValueRef content_v = codegen_expr(ctx, args[1]);
    if (!path_v || !content_v) return NULL;

    LLVMValueRef path_data    = ls_str_data_v(ctx, path_v);
    LLVMValueRef content_data = ls_str_data_v(ctx, content_v);
    LLVMValueRef content_len  = ls_str_len_v(ctx, content_v);
    LLVMValueRef content_len_i64 = LLVMBuildSExt(ctx->builder, content_len, i64,
                                                 "io.wf.len64");

    LLVMValueRef mode_str = LLVMBuildGlobalStringPtr(ctx->builder, "wb", "io.wb");
    LLVMValueRef fopen_fn = decl_fopen(ctx);
    LLVMTypeRef fopen_ty = LLVMGlobalGetValueType(fopen_fn);
    LLVMValueRef fopen_args[2] = { path_data, mode_str };
    LLVMValueRef fp = LLVMBuildCall2(ctx->builder, fopen_ty, fopen_fn, fopen_args,
                                     2, "io.fp");

    LLVMTypeRef result_llvm = type_to_llvm(ctx, result_type);
    LLVMTargetDataRef td = LLVMGetModuleDataLayout(ctx->module);
    unsigned long long result_sz = LLVMABISizeOfType(td, result_llvm);
    LLVMValueRef result_alloca = hoist_alloca(ctx, result_llvm, "io.wf.res");
    zero_mem(ctx, result_alloca, result_sz);

    LLVMValueRef null_p = LLVMConstPointerNull(ptr_ty);
    LLVMValueRef is_null = LLVMBuildICmp(ctx->builder, LLVMIntEQ, fp, null_p,
                                         "io.wf.null");

    LLVMBasicBlockRef ok_bb = LLVMAppendBasicBlockInContext(
        ctx->context, ctx->current_fn, "io.wf.ok");
    LLVMBasicBlockRef err_bb = LLVMAppendBasicBlockInContext(
        ctx->context, ctx->current_fn, "io.wf.err");
    LLVMBasicBlockRef done_bb = LLVMAppendBasicBlockInContext(
        ctx->context, ctx->current_fn, "io.wf.done");
    LLVMBuildCondBr(ctx->builder, is_null, err_bb, ok_bb);

    LLVMPositionBuilderAtEnd(ctx->builder, err_bb);
    emit_err_static(ctx, result_alloca, result_llvm, result_type,
                    "io: open for write failed", 1);
    LLVMBuildBr(ctx->builder, done_bb);

    LLVMPositionBuilderAtEnd(ctx->builder, ok_bb);
    LLVMValueRef nwrote_i64 = emit_fwrite_bytes(ctx, fp, content_data,
                                                content_len_i64);
    LLVMValueRef fclose = decl_fclose(ctx);
    LLVMTypeRef fclose_ty = LLVMGlobalGetValueType(fclose);
    LLVMBuildCall2(ctx->builder, fclose_ty, fclose, &fp, 1, "");
    LLVMValueRef nwrote_i32 = LLVMBuildTrunc(ctx->builder, nwrote_i64, i32,
                                             "io.wf.n32");

    set_disc(ctx, result_alloca, result_llvm, 0);
    set_payload_field(ctx, result_alloca, result_llvm, result_type, 0, 0,
                      nwrote_i32);
    LLVMBuildBr(ctx->builder, done_bb);

    LLVMPositionBuilderAtEnd(ctx->builder, done_bb);
    return LLVMBuildLoad2(ctx->builder, result_llvm, result_alloca, "io.wf.v");
}

/* io.exists(path) -> bool (true if fopen("rb") succeeds; closes immediately) */
static LLVMValueRef emit_exists(CodegenContext *ctx, AstNode **args) {
    LLVMTypeRef ptr_ty = LLVMPointerTypeInContext(ctx->context, 0);
    LLVMTypeRef i1 = LLVMInt1TypeInContext(ctx->context);

    LLVMValueRef path_v = codegen_expr(ctx, args[0]);
    if (!path_v) return NULL;
    LLVMValueRef path_data = ls_str_data_v(ctx, path_v);

    LLVMValueRef mode_str = LLVMBuildGlobalStringPtr(ctx->builder, "rb",
                                                     "io.ex.rb");
    LLVMValueRef fopen_fn = decl_fopen(ctx);
    LLVMTypeRef fopen_ty = LLVMGlobalGetValueType(fopen_fn);
    LLVMValueRef fopen_args[2] = { path_data, mode_str };
    LLVMValueRef fp = LLVMBuildCall2(ctx->builder, fopen_ty, fopen_fn, fopen_args,
                                     2, "io.ex.fp");

    LLVMValueRef null_p = LLVMConstPointerNull(ptr_ty);
    LLVMValueRef exists = LLVMBuildICmp(ctx->builder, LLVMIntNE, fp, null_p,
                                        "io.ex");

    /* Close if non-null. */
    LLVMBasicBlockRef close_bb = LLVMAppendBasicBlockInContext(
        ctx->context, ctx->current_fn, "io.ex.close");
    LLVMBasicBlockRef done_bb = LLVMAppendBasicBlockInContext(
        ctx->context, ctx->current_fn, "io.ex.done");
    LLVMBuildCondBr(ctx->builder, exists, close_bb, done_bb);

    LLVMPositionBuilderAtEnd(ctx->builder, close_bb);
    LLVMValueRef fclose = decl_fclose(ctx);
    LLVMTypeRef fclose_ty = LLVMGlobalGetValueType(fclose);
    LLVMBuildCall2(ctx->builder, fclose_ty, fclose, &fp, 1, "");
    LLVMBuildBr(ctx->builder, done_bb);

    LLVMPositionBuilderAtEnd(ctx->builder, done_bb);
    /* `exists` was computed in ok_bb, but it's still SSA-valid here as an i1. */
    return exists;
    (void)i1;
}

/* Build a File struct value: { handle, is_binary }. */
static LLVMValueRef build_file_value(CodegenContext *ctx, Type *file_type,
                                     LLVMValueRef handle, bool is_binary) {
    LLVMTypeRef file_llvm = type_to_llvm(ctx, file_type);
    LLVMTypeRef i1 = LLVMInt1TypeInContext(ctx->context);
    LLVMValueRef v = LLVMGetUndef(file_llvm);
    v = LLVMBuildInsertValue(ctx->builder, v, handle, 0, "io.f.h");
    v = LLVMBuildInsertValue(ctx->builder, v,
                             LLVMConstInt(i1, is_binary ? 1 : 0, 0), 1, "io.f.b");
    return v;
}

/* io.open(path, OpenMode) -> Result(File, string) */
static LLVMValueRef emit_open(CodegenContext *ctx, AstNode **args,
                              Type *result_type) {
    LLVMTypeRef ptr_ty = LLVMPointerTypeInContext(ctx->context, 0);
    LLVMTypeRef i8 = LLVMInt8TypeInContext(ctx->context);

    LLVMValueRef path_v = codegen_expr(ctx, args[0]);
    LLVMValueRef mode_v = codegen_expr(ctx, args[1]);
    if (!path_v || !mode_v) return NULL;
    LLVMValueRef path_data = ls_str_data_v(ctx, path_v);
    LLVMValueRef mode_ptr  = extract_mode_str(ctx, mode_v);

    LLVMValueRef fopen_fn = decl_fopen(ctx);
    LLVMTypeRef fopen_ty = LLVMGlobalGetValueType(fopen_fn);
    LLVMValueRef fopen_args[2] = { path_data, mode_ptr };
    LLVMValueRef fp = LLVMBuildCall2(ctx->builder, fopen_ty, fopen_fn, fopen_args,
                                     2, "io.op.fp");

    LLVMTypeRef result_llvm = type_to_llvm(ctx, result_type);
    LLVMTargetDataRef td = LLVMGetModuleDataLayout(ctx->module);
    unsigned long long result_sz = LLVMABISizeOfType(td, result_llvm);
    LLVMValueRef result_alloca = hoist_alloca(ctx, result_llvm, "io.op.res");
    zero_mem(ctx, result_alloca, result_sz);

    LLVMValueRef null_p = LLVMConstPointerNull(ptr_ty);
    LLVMValueRef is_null = LLVMBuildICmp(ctx->builder, LLVMIntEQ, fp, null_p,
                                         "io.op.null");

    LLVMBasicBlockRef ok_bb = LLVMAppendBasicBlockInContext(
        ctx->context, ctx->current_fn, "io.op.ok");
    LLVMBasicBlockRef err_bb = LLVMAppendBasicBlockInContext(
        ctx->context, ctx->current_fn, "io.op.err");
    LLVMBasicBlockRef done_bb = LLVMAppendBasicBlockInContext(
        ctx->context, ctx->current_fn, "io.op.done");
    LLVMBuildCondBr(ctx->builder, is_null, err_bb, ok_bb);

    LLVMPositionBuilderAtEnd(ctx->builder, err_bb);
    emit_err_static(ctx, result_alloca, result_llvm, result_type,
                    "io: open failed", 1);
    LLVMBuildBr(ctx->builder, done_bb);

    LLVMPositionBuilderAtEnd(ctx->builder, ok_bb);
    /* Determine is_binary: disc >= 3 → binary mode (ReadBinary..AppendBinary). */
    LLVMValueRef disc_i8 = LLVMBuildExtractValue(ctx->builder, mode_v, 0,
                                                 "io.op.disc");
    LLVMValueRef is_bin = LLVMBuildICmp(ctx->builder, LLVMIntUGE, disc_i8,
                                        LLVMConstInt(i8, 3, 0), "io.op.bin");

    /* File struct: pick is_binary at runtime via select. */
    Type *file_t = result_type->as.enom.variants[0].payload_types[0];
    LLVMTypeRef file_llvm = type_to_llvm(ctx, file_t);
    LLVMTypeRef i1 = LLVMInt1TypeInContext(ctx->context);
    LLVMValueRef v = LLVMGetUndef(file_llvm);
    v = LLVMBuildInsertValue(ctx->builder, v, fp, 0, "io.op.f.h");
    v = LLVMBuildInsertValue(ctx->builder, v, is_bin, 1, "io.op.f.b");

    set_disc(ctx, result_alloca, result_llvm, 0);
    set_payload_field(ctx, result_alloca, result_llvm, result_type, 0, 0, v);
    LLVMBuildBr(ctx->builder, done_bb);

    LLVMPositionBuilderAtEnd(ctx->builder, done_bb);
    (void)i1; (void)build_file_value;
    return LLVMBuildLoad2(ctx->builder, result_llvm, result_alloca, "io.op.v");
}

/* io.close(File) -> int */
static LLVMValueRef emit_close(CodegenContext *ctx, AstNode **args) {
    LLVMValueRef file_v = codegen_expr(ctx, args[0]);
    if (!file_v) return NULL;

    /* File handle is field 0. */
    LLVMValueRef fp = LLVMBuildExtractValue(ctx->builder, file_v, 0, "io.cl.fp");

    LLVMTypeRef ptr_ty = LLVMPointerTypeInContext(ctx->context, 0);
    LLVMTypeRef i32 = LLVMInt32TypeInContext(ctx->context);
    LLVMValueRef null_p = LLVMConstPointerNull(ptr_ty);
    LLVMValueRef is_null = LLVMBuildICmp(ctx->builder, LLVMIntEQ, fp, null_p,
                                         "io.cl.null");

    LLVMBasicBlockRef do_bb = LLVMAppendBasicBlockInContext(
        ctx->context, ctx->current_fn, "io.cl.do");
    LLVMBasicBlockRef done_bb = LLVMAppendBasicBlockInContext(
        ctx->context, ctx->current_fn, "io.cl.done");
    LLVMValueRef result_alloca = hoist_alloca(ctx, i32, "io.cl.res");
    LLVMBuildStore(ctx->builder, LLVMConstInt(i32, -1, 0), result_alloca);
    LLVMBuildCondBr(ctx->builder, is_null, done_bb, do_bb);

    LLVMPositionBuilderAtEnd(ctx->builder, do_bb);
    LLVMValueRef fclose = decl_fclose(ctx);
    LLVMTypeRef fclose_ty = LLVMGlobalGetValueType(fclose);
    LLVMValueRef rc = LLVMBuildCall2(ctx->builder, fclose_ty, fclose, &fp, 1, "io.cl.rc");
    LLVMBuildStore(ctx->builder, rc, result_alloca);
    LLVMBuildBr(ctx->builder, done_bb);

    LLVMPositionBuilderAtEnd(ctx->builder, done_bb);
    return LLVMBuildLoad2(ctx->builder, i32, result_alloca, "io.cl.v");
}

/* io.read_all(File) -> Result(string, string) */
static LLVMValueRef emit_read_all(CodegenContext *ctx, AstNode **args,
                                  Type *result_type) {
    LLVMValueRef file_v = codegen_expr(ctx, args[0]);
    if (!file_v) return NULL;
    LLVMValueRef fp = LLVMBuildExtractValue(ctx->builder, file_v, 0, "io.ra.fp");
    int line = args[0] ? args[0]->line : 0;
    int col  = args[0] ? args[0]->column : 0;
    return emit_slurp_file(ctx, fp, result_type, false, line, col);
}

/* io.write(File, string) -> Result(int, string) */
static LLVMValueRef emit_write(CodegenContext *ctx, AstNode **args,
                               Type *result_type) {
    LLVMTypeRef ptr_ty = LLVMPointerTypeInContext(ctx->context, 0);
    LLVMTypeRef i32 = LLVMInt32TypeInContext(ctx->context);
    LLVMTypeRef i64 = LLVMInt64TypeInContext(ctx->context);

    LLVMValueRef file_v    = codegen_expr(ctx, args[0]);
    LLVMValueRef content_v = codegen_expr(ctx, args[1]);
    if (!file_v || !content_v) return NULL;

    LLVMValueRef fp = LLVMBuildExtractValue(ctx->builder, file_v, 0, "io.w.fp");
    LLVMValueRef content_data = ls_str_data_v(ctx, content_v);
    LLVMValueRef content_len  = ls_str_len_v(ctx, content_v);
    LLVMValueRef content_len_i64 = LLVMBuildSExt(ctx->builder, content_len, i64,
                                                 "io.w.len64");

    LLVMTypeRef result_llvm = type_to_llvm(ctx, result_type);
    LLVMTargetDataRef td = LLVMGetModuleDataLayout(ctx->module);
    unsigned long long result_sz = LLVMABISizeOfType(td, result_llvm);
    LLVMValueRef result_alloca = hoist_alloca(ctx, result_llvm, "io.w.res");
    zero_mem(ctx, result_alloca, result_sz);

    LLVMValueRef null_p = LLVMConstPointerNull(ptr_ty);
    LLVMValueRef is_null = LLVMBuildICmp(ctx->builder, LLVMIntEQ, fp, null_p,
                                         "io.w.null");

    LLVMBasicBlockRef ok_bb = LLVMAppendBasicBlockInContext(
        ctx->context, ctx->current_fn, "io.w.ok");
    LLVMBasicBlockRef err_bb = LLVMAppendBasicBlockInContext(
        ctx->context, ctx->current_fn, "io.w.err");
    LLVMBasicBlockRef done_bb = LLVMAppendBasicBlockInContext(
        ctx->context, ctx->current_fn, "io.w.done");
    LLVMBuildCondBr(ctx->builder, is_null, err_bb, ok_bb);

    LLVMPositionBuilderAtEnd(ctx->builder, err_bb);
    emit_err_static(ctx, result_alloca, result_llvm, result_type,
                    "io: write to closed/null handle", 1);
    LLVMBuildBr(ctx->builder, done_bb);

    LLVMPositionBuilderAtEnd(ctx->builder, ok_bb);
    LLVMValueRef nwrote_i64 = emit_fwrite_bytes(ctx, fp, content_data,
                                                content_len_i64);
    LLVMValueRef nwrote_i32 = LLVMBuildTrunc(ctx->builder, nwrote_i64, i32,
                                             "io.w.n32");
    set_disc(ctx, result_alloca, result_llvm, 0);
    set_payload_field(ctx, result_alloca, result_llvm, result_type, 0, 0,
                      nwrote_i32);
    LLVMBuildBr(ctx->builder, done_bb);

    LLVMPositionBuilderAtEnd(ctx->builder, done_bb);
    return LLVMBuildLoad2(ctx->builder, result_llvm, result_alloca, "io.w.v");
}

/* Extract is_binary (i1) from a File struct value. File layout: { ptr, i1 }. */
static LLVMValueRef file_is_binary(CodegenContext *ctx, LLVMValueRef file_v) {
    return LLVMBuildExtractValue(ctx->builder, file_v, 1, "io.f.bin");
}

/* Extract handle (i8*) from a File struct value. */
static LLVMValueRef file_handle(CodegenContext *ctx, LLVMValueRef file_v) {
    return LLVMBuildExtractValue(ctx->builder, file_v, 0, "io.f.h");
}

/* Allocate a Result enum + memset to zero. Returns the alloca pointer. */
static LLVMValueRef alloc_result(CodegenContext *ctx, LLVMTypeRef result_llvm,
                                 const char *name) {
    LLVMTargetDataRef td = LLVMGetModuleDataLayout(ctx->module);
    unsigned long long sz = LLVMABISizeOfType(td, result_llvm);
    LLVMValueRef a = hoist_alloca(ctx, result_llvm, name);
    zero_mem(ctx, a, sz);
    return a;
}

/* Common preamble for File-positioning ops: extract handle, branch on
   binary/null gate. On the binary-and-non-null path we continue at the
   returned `ok_bb`; on the bad path we set Err and branch to `done_bb`.

   Caller must ultimately position the builder at done_bb and load the
   result. */
typedef struct {
    LLVMValueRef fp;
    LLVMBasicBlockRef ok_bb;
    LLVMBasicBlockRef done_bb;
    LLVMValueRef result_alloca;
    LLVMTypeRef  result_llvm;
    Type        *result_type;
} PosCtx;

static PosCtx pos_open(CodegenContext *ctx, AstNode *file_arg, Type *result_type,
                       const char *prefix) {
    PosCtx pc;
    pc.result_type = result_type;
    pc.result_llvm = type_to_llvm(ctx, result_type);

    LLVMValueRef file_v = codegen_expr(ctx, file_arg);
    pc.fp = file_handle(ctx, file_v);
    LLVMValueRef is_bin = file_is_binary(ctx, file_v);

    pc.result_alloca = alloc_result(ctx, pc.result_llvm, prefix);

    LLVMTypeRef ptr_ty = LLVMPointerTypeInContext(ctx->context, 0);
    LLVMValueRef null_p = LLVMConstPointerNull(ptr_ty);
    LLVMValueRef fp_nz = LLVMBuildICmp(ctx->builder, LLVMIntNE, pc.fp, null_p,
                                       "io.fp.nz");
    LLVMValueRef gate = LLVMBuildAnd(ctx->builder, is_bin, fp_nz, "io.gate");

    pc.ok_bb = LLVMAppendBasicBlockInContext(ctx->context, ctx->current_fn,
                                             "io.pos.ok");
    LLVMBasicBlockRef err_bb = LLVMAppendBasicBlockInContext(
        ctx->context, ctx->current_fn, "io.pos.err");
    pc.done_bb = LLVMAppendBasicBlockInContext(ctx->context, ctx->current_fn,
                                                "io.pos.done");
    LLVMBuildCondBr(ctx->builder, gate, pc.ok_bb, err_bb);

    LLVMPositionBuilderAtEnd(ctx->builder, err_bb);
    emit_err_static(ctx, pc.result_alloca, pc.result_llvm, pc.result_type,
                    "io: file is text-mode or closed (positioning requires binary)",
                    1);
    LLVMBuildBr(ctx->builder, pc.done_bb);

    LLVMPositionBuilderAtEnd(ctx->builder, pc.ok_bb);
    return pc;
}

/* io.seek(File, i64, SeekFrom) -> Result(i64, string) */
static LLVMValueRef emit_seek(CodegenContext *ctx, AstNode **args,
                              Type *result_type) {
    LLVMTypeRef i32 = LLVMInt32TypeInContext(ctx->context);
    LLVMTypeRef i64 = LLVMInt64TypeInContext(ctx->context);

    PosCtx pc = pos_open(ctx, args[0], result_type, "io.sk.res");

    LLVMValueRef off = codegen_expr(ctx, args[1]);
    if (!off) return NULL;
    /* Widen int (i32) → i64 if necessary so _fseeki64's signature matches. */
    LLVMTypeRef off_ty = LLVMTypeOf(off);
    if (LLVMGetTypeKind(off_ty) == LLVMIntegerTypeKind &&
        LLVMGetIntTypeWidth(off_ty) < 64) {
        off = LLVMBuildSExt(ctx->builder, off, i64, "io.sk.off64");
    }
    LLVMValueRef whence_v = codegen_expr(ctx, args[2]);
    if (!whence_v) return NULL;
    /* SeekFrom enum disc (0/1/2) maps directly to SEEK_SET/CUR/END. */
    LLVMValueRef whence_i8 = LLVMBuildExtractValue(ctx->builder, whence_v, 0,
                                                   "io.sk.disc");
    LLVMValueRef whence_i32 = LLVMBuildZExt(ctx->builder, whence_i8, i32,
                                            "io.sk.whence");

    LLVMValueRef fseek = decl_fseek64(ctx);
    LLVMTypeRef fseek_ty = LLVMGlobalGetValueType(fseek);
    LLVMValueRef fseek_args[3] = { pc.fp, off, whence_i32 };
    LLVMValueRef rc = LLVMBuildCall2(ctx->builder, fseek_ty, fseek, fseek_args,
                                     3, "io.sk.rc");
    LLVMValueRef rc_ok = LLVMBuildICmp(ctx->builder, LLVMIntEQ, rc,
                                       LLVMConstInt(i32, 0, 0), "io.sk.ok");

    LLVMBasicBlockRef tell_bb = LLVMAppendBasicBlockInContext(
        ctx->context, ctx->current_fn, "io.sk.tell");
    LLVMBasicBlockRef bad_bb = LLVMAppendBasicBlockInContext(
        ctx->context, ctx->current_fn, "io.sk.bad");
    LLVMBuildCondBr(ctx->builder, rc_ok, tell_bb, bad_bb);

    LLVMPositionBuilderAtEnd(ctx->builder, bad_bb);
    emit_err_static(ctx, pc.result_alloca, pc.result_llvm, pc.result_type,
                    "io: seek out of range or unseekable", 1);
    LLVMBuildBr(ctx->builder, pc.done_bb);

    LLVMPositionBuilderAtEnd(ctx->builder, tell_bb);
    LLVMValueRef ftell = decl_ftell64(ctx);
    LLVMTypeRef ftell_ty = LLVMGlobalGetValueType(ftell);
    LLVMValueRef pos = LLVMBuildCall2(ctx->builder, ftell_ty, ftell, &pc.fp, 1,
                                      "io.sk.pos");
    set_disc(ctx, pc.result_alloca, pc.result_llvm, 0);
    set_payload_field(ctx, pc.result_alloca, pc.result_llvm, pc.result_type,
                      0, 0, pos);
    LLVMBuildBr(ctx->builder, pc.done_bb);

    LLVMPositionBuilderAtEnd(ctx->builder, pc.done_bb);
    (void)i64;
    return LLVMBuildLoad2(ctx->builder, pc.result_llvm, pc.result_alloca, "io.sk.v");
}

/* io.tell(File) -> Result(i64, string) */
static LLVMValueRef emit_tell(CodegenContext *ctx, AstNode **args,
                              Type *result_type) {
    LLVMTypeRef i64 = LLVMInt64TypeInContext(ctx->context);

    PosCtx pc = pos_open(ctx, args[0], result_type, "io.tl.res");

    LLVMValueRef ftell = decl_ftell64(ctx);
    LLVMTypeRef ftell_ty = LLVMGlobalGetValueType(ftell);
    LLVMValueRef pos = LLVMBuildCall2(ctx->builder, ftell_ty, ftell, &pc.fp, 1,
                                      "io.tl.pos");
    LLVMValueRef neg = LLVMBuildICmp(ctx->builder, LLVMIntSLT, pos,
                                     LLVMConstInt(i64, 0, 0), "io.tl.neg");

    LLVMBasicBlockRef ok_bb = LLVMAppendBasicBlockInContext(
        ctx->context, ctx->current_fn, "io.tl.ok");
    LLVMBasicBlockRef err_bb = LLVMAppendBasicBlockInContext(
        ctx->context, ctx->current_fn, "io.tl.err");
    LLVMBuildCondBr(ctx->builder, neg, err_bb, ok_bb);

    LLVMPositionBuilderAtEnd(ctx->builder, err_bb);
    emit_err_static(ctx, pc.result_alloca, pc.result_llvm, pc.result_type,
                    "io: tell failed", 1);
    LLVMBuildBr(ctx->builder, pc.done_bb);

    LLVMPositionBuilderAtEnd(ctx->builder, ok_bb);
    set_disc(ctx, pc.result_alloca, pc.result_llvm, 0);
    set_payload_field(ctx, pc.result_alloca, pc.result_llvm, pc.result_type,
                      0, 0, pos);
    LLVMBuildBr(ctx->builder, pc.done_bb);

    LLVMPositionBuilderAtEnd(ctx->builder, pc.done_bb);
    return LLVMBuildLoad2(ctx->builder, pc.result_llvm, pc.result_alloca, "io.tl.v");
}

/* io.size(File) -> Result(i64, string)
   Save current pos, seek to end, ftell, seek back. */
static LLVMValueRef emit_size(CodegenContext *ctx, AstNode **args,
                              Type *result_type) {
    LLVMTypeRef i32 = LLVMInt32TypeInContext(ctx->context);
    LLVMTypeRef i64 = LLVMInt64TypeInContext(ctx->context);

    PosCtx pc = pos_open(ctx, args[0], result_type, "io.sz.res");

    LLVMValueRef ftell = decl_ftell64(ctx);
    LLVMTypeRef ftell_ty = LLVMGlobalGetValueType(ftell);
    LLVMValueRef fseek = decl_fseek64(ctx);
    LLVMTypeRef fseek_ty = LLVMGlobalGetValueType(fseek);

    LLVMValueRef saved = LLVMBuildCall2(ctx->builder, ftell_ty, ftell, &pc.fp, 1,
                                        "io.sz.saved");

    LLVMValueRef end_args[3] = {
        pc.fp, LLVMConstInt(i64, 0, 0), LLVMConstInt(i32, 2, 0)
    };
    LLVMBuildCall2(ctx->builder, fseek_ty, fseek, end_args, 3, "io.sz.end");
    LLVMValueRef sz = LLVMBuildCall2(ctx->builder, ftell_ty, ftell, &pc.fp, 1,
                                     "io.sz.sz");
    LLVMValueRef restore_args[3] = { pc.fp, saved, LLVMConstInt(i32, 0, 0) };
    LLVMBuildCall2(ctx->builder, fseek_ty, fseek, restore_args, 3, "io.sz.restore");

    LLVMValueRef neg = LLVMBuildICmp(ctx->builder, LLVMIntSLT, sz,
                                     LLVMConstInt(i64, 0, 0), "io.sz.neg");
    LLVMBasicBlockRef ok_bb = LLVMAppendBasicBlockInContext(
        ctx->context, ctx->current_fn, "io.sz.ok");
    LLVMBasicBlockRef err_bb = LLVMAppendBasicBlockInContext(
        ctx->context, ctx->current_fn, "io.sz.err");
    LLVMBuildCondBr(ctx->builder, neg, err_bb, ok_bb);

    LLVMPositionBuilderAtEnd(ctx->builder, err_bb);
    emit_err_static(ctx, pc.result_alloca, pc.result_llvm, pc.result_type,
                    "io: size query failed", 1);
    LLVMBuildBr(ctx->builder, pc.done_bb);

    LLVMPositionBuilderAtEnd(ctx->builder, ok_bb);
    set_disc(ctx, pc.result_alloca, pc.result_llvm, 0);
    set_payload_field(ctx, pc.result_alloca, pc.result_llvm, pc.result_type,
                      0, 0, sz);
    LLVMBuildBr(ctx->builder, pc.done_bb);

    LLVMPositionBuilderAtEnd(ctx->builder, pc.done_bb);
    return LLVMBuildLoad2(ctx->builder, pc.result_llvm, pc.result_alloca, "io.sz.v");
}

/* io.rewind(File) -> Result(int, string) — equivalent to seek(0, Start). */
static LLVMValueRef emit_rewind(CodegenContext *ctx, AstNode **args,
                                Type *result_type) {
    LLVMTypeRef i32 = LLVMInt32TypeInContext(ctx->context);
    LLVMTypeRef i64 = LLVMInt64TypeInContext(ctx->context);

    PosCtx pc = pos_open(ctx, args[0], result_type, "io.rw.res");

    LLVMValueRef fseek = decl_fseek64(ctx);
    LLVMTypeRef fseek_ty = LLVMGlobalGetValueType(fseek);
    LLVMValueRef rw_args[3] = {
        pc.fp, LLVMConstInt(i64, 0, 0), LLVMConstInt(i32, 0, 0)
    };
    LLVMValueRef rc = LLVMBuildCall2(ctx->builder, fseek_ty, fseek, rw_args, 3,
                                     "io.rw.rc");
    LLVMValueRef ok = LLVMBuildICmp(ctx->builder, LLVMIntEQ, rc,
                                    LLVMConstInt(i32, 0, 0), "io.rw.ok");

    LLVMBasicBlockRef ok_bb = LLVMAppendBasicBlockInContext(
        ctx->context, ctx->current_fn, "io.rw.ok2");
    LLVMBasicBlockRef err_bb = LLVMAppendBasicBlockInContext(
        ctx->context, ctx->current_fn, "io.rw.err");
    LLVMBuildCondBr(ctx->builder, ok, ok_bb, err_bb);

    LLVMPositionBuilderAtEnd(ctx->builder, err_bb);
    emit_err_static(ctx, pc.result_alloca, pc.result_llvm, pc.result_type,
                    "io: rewind failed", 1);
    LLVMBuildBr(ctx->builder, pc.done_bb);

    LLVMPositionBuilderAtEnd(ctx->builder, ok_bb);
    set_disc(ctx, pc.result_alloca, pc.result_llvm, 0);
    set_payload_field(ctx, pc.result_alloca, pc.result_llvm, pc.result_type,
                      0, 0, LLVMConstInt(i32, 0, 0));
    LLVMBuildBr(ctx->builder, pc.done_bb);

    LLVMPositionBuilderAtEnd(ctx->builder, pc.done_bb);
    return LLVMBuildLoad2(ctx->builder, pc.result_llvm, pc.result_alloca, "io.rw.v");
}

/* io.append_file(string, string) -> Result(int, string) — same shape as
   write_file but opens with "ab". */
static LLVMValueRef emit_append_file(CodegenContext *ctx, AstNode **args,
                                     Type *result_type) {
    LLVMTypeRef ptr_ty = LLVMPointerTypeInContext(ctx->context, 0);
    LLVMTypeRef i32 = LLVMInt32TypeInContext(ctx->context);
    LLVMTypeRef i64 = LLVMInt64TypeInContext(ctx->context);

    LLVMValueRef path_v    = codegen_expr(ctx, args[0]);
    LLVMValueRef content_v = codegen_expr(ctx, args[1]);
    if (!path_v || !content_v) return NULL;

    LLVMValueRef path_data    = ls_str_data_v(ctx, path_v);
    LLVMValueRef content_data = ls_str_data_v(ctx, content_v);
    LLVMValueRef content_len  = ls_str_len_v(ctx, content_v);
    LLVMValueRef content_len_i64 = LLVMBuildSExt(ctx->builder, content_len, i64,
                                                 "io.af.len64");

    LLVMValueRef mode_str = LLVMBuildGlobalStringPtr(ctx->builder, "ab", "io.ab");
    LLVMValueRef fopen_fn = decl_fopen(ctx);
    LLVMTypeRef fopen_ty = LLVMGlobalGetValueType(fopen_fn);
    LLVMValueRef fopen_args[2] = { path_data, mode_str };
    LLVMValueRef fp = LLVMBuildCall2(ctx->builder, fopen_ty, fopen_fn, fopen_args,
                                     2, "io.af.fp");

    LLVMTypeRef result_llvm = type_to_llvm(ctx, result_type);
    LLVMValueRef result_alloca = alloc_result(ctx, result_llvm, "io.af.res");

    LLVMValueRef null_p = LLVMConstPointerNull(ptr_ty);
    LLVMValueRef is_null = LLVMBuildICmp(ctx->builder, LLVMIntEQ, fp, null_p,
                                         "io.af.null");

    LLVMBasicBlockRef ok_bb = LLVMAppendBasicBlockInContext(
        ctx->context, ctx->current_fn, "io.af.ok");
    LLVMBasicBlockRef err_bb = LLVMAppendBasicBlockInContext(
        ctx->context, ctx->current_fn, "io.af.err");
    LLVMBasicBlockRef done_bb = LLVMAppendBasicBlockInContext(
        ctx->context, ctx->current_fn, "io.af.done");
    LLVMBuildCondBr(ctx->builder, is_null, err_bb, ok_bb);

    LLVMPositionBuilderAtEnd(ctx->builder, err_bb);
    emit_err_static(ctx, result_alloca, result_llvm, result_type,
                    "io: open for append failed", 1);
    LLVMBuildBr(ctx->builder, done_bb);

    LLVMPositionBuilderAtEnd(ctx->builder, ok_bb);
    LLVMValueRef nwrote_i64 = emit_fwrite_bytes(ctx, fp, content_data,
                                                content_len_i64);
    LLVMValueRef fclose = decl_fclose(ctx);
    LLVMTypeRef fclose_ty = LLVMGlobalGetValueType(fclose);
    LLVMBuildCall2(ctx->builder, fclose_ty, fclose, &fp, 1, "");
    LLVMValueRef nwrote_i32 = LLVMBuildTrunc(ctx->builder, nwrote_i64, i32,
                                             "io.af.n32");
    set_disc(ctx, result_alloca, result_llvm, 0);
    set_payload_field(ctx, result_alloca, result_llvm, result_type, 0, 0,
                      nwrote_i32);
    LLVMBuildBr(ctx->builder, done_bb);

    LLVMPositionBuilderAtEnd(ctx->builder, done_bb);
    return LLVMBuildLoad2(ctx->builder, result_llvm, result_alloca, "io.af.v");
}

/* io.remove(string) -> Result(int, string) */
static LLVMValueRef emit_remove(CodegenContext *ctx, AstNode **args,
                                Type *result_type) {
    LLVMTypeRef i32 = LLVMInt32TypeInContext(ctx->context);

    LLVMValueRef path_v = codegen_expr(ctx, args[0]);
    if (!path_v) return NULL;
    LLVMValueRef path_data = ls_str_data_v(ctx, path_v);

    LLVMValueRef rm_fn = decl_remove(ctx);
    LLVMTypeRef rm_ty = LLVMGlobalGetValueType(rm_fn);
    LLVMValueRef rc = LLVMBuildCall2(ctx->builder, rm_ty, rm_fn, &path_data, 1,
                                     "io.rm.rc");
    LLVMValueRef ok = LLVMBuildICmp(ctx->builder, LLVMIntEQ, rc,
                                    LLVMConstInt(i32, 0, 0), "io.rm.ok");

    LLVMTypeRef result_llvm = type_to_llvm(ctx, result_type);
    LLVMValueRef result_alloca = alloc_result(ctx, result_llvm, "io.rm.res");

    LLVMBasicBlockRef ok_bb = LLVMAppendBasicBlockInContext(
        ctx->context, ctx->current_fn, "io.rm.ok2");
    LLVMBasicBlockRef err_bb = LLVMAppendBasicBlockInContext(
        ctx->context, ctx->current_fn, "io.rm.err");
    LLVMBasicBlockRef done_bb = LLVMAppendBasicBlockInContext(
        ctx->context, ctx->current_fn, "io.rm.done");
    LLVMBuildCondBr(ctx->builder, ok, ok_bb, err_bb);

    LLVMPositionBuilderAtEnd(ctx->builder, err_bb);
    emit_err_static(ctx, result_alloca, result_llvm, result_type,
                    "io: remove failed", 1);
    LLVMBuildBr(ctx->builder, done_bb);

    LLVMPositionBuilderAtEnd(ctx->builder, ok_bb);
    set_disc(ctx, result_alloca, result_llvm, 0);
    set_payload_field(ctx, result_alloca, result_llvm, result_type, 0, 0,
                      LLVMConstInt(i32, 0, 0));
    LLVMBuildBr(ctx->builder, done_bb);

    LLVMPositionBuilderAtEnd(ctx->builder, done_bb);
    return LLVMBuildLoad2(ctx->builder, result_llvm, result_alloca, "io.rm.v");
}

/* ---- Public dispatch ---- */

LLVMValueRef builtin_io_emit_call(CodegenContext *ctx, const char *fn_name,
                                  AstNode **args, int arg_count,
                                  Type *result_type) {
    if (strcmp(fn_name, "read_file") == 0 && arg_count == 1)
        return emit_read_file(ctx, args, result_type);
    if (strcmp(fn_name, "write_file") == 0 && arg_count == 2)
        return emit_write_file(ctx, args, result_type);
    if (strcmp(fn_name, "exists") == 0 && arg_count == 1)
        return emit_exists(ctx, args);
    if (strcmp(fn_name, "open") == 0 && arg_count == 2)
        return emit_open(ctx, args, result_type);
    if (strcmp(fn_name, "close") == 0 && arg_count == 1)
        return emit_close(ctx, args);
    if (strcmp(fn_name, "read_all") == 0 && arg_count == 1)
        return emit_read_all(ctx, args, result_type);
    if (strcmp(fn_name, "write") == 0 && arg_count == 2)
        return emit_write(ctx, args, result_type);
    if (strcmp(fn_name, "seek") == 0 && arg_count == 3)
        return emit_seek(ctx, args, result_type);
    if (strcmp(fn_name, "tell") == 0 && arg_count == 1)
        return emit_tell(ctx, args, result_type);
    if (strcmp(fn_name, "size") == 0 && arg_count == 1)
        return emit_size(ctx, args, result_type);
    if (strcmp(fn_name, "rewind") == 0 && arg_count == 1)
        return emit_rewind(ctx, args, result_type);
    if (strcmp(fn_name, "append_file") == 0 && arg_count == 2)
        return emit_append_file(ctx, args, result_type);
    if (strcmp(fn_name, "remove") == 0 && arg_count == 1)
        return emit_remove(ctx, args, result_type);
    return NULL;
}
