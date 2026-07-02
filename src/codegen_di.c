/* codegen_di.c — D1 debug info: DIBuilder line-table skeleton
   (docs/plan_debug_info.md phase 1).

   Gated on `lls compile -g` / `lls emit-ir -g`: without the flag ctx->dib
   stays NULL and every hook below is a no-op, so the default pipeline is
   byte-identical. Phase 1 emits LLVMDWARFEmissionLineTablesOnly — crash
   stacks and profilers get file/line attribution without a DI type graph. */

#include "codegen_internal.h"

#include <string.h>

/* Create the DIBuilder + compile unit + module flags. Called once from
   codegen_compile before any function body is emitted. The source path is
   recovered from the module identifier (codegen_init names the module after
   the input path), so no extra plumbing from main.c is needed. */
void cg_di_init(CodegenContext *ctx)
{
    if (ctx->dib || !ctx->debug_info)
        return;

    size_t id_len = 0;
    const char *path = LLVMGetModuleIdentifier(ctx->module, &id_len);
    if (path == NULL || id_len == 0)
        path = "<unknown>";

    /* Split "C:/dir/app.lls" into basename + directory prefix.
       LLVMDIBuilderCreateFile takes explicit lengths, so the directory can
       point into `path` without a null terminator. */
    const char *base = path;
    for (const char *p = path; *p; p++)
        if (*p == '/' || *p == '\\')
            base = p + 1;
    size_t dir_len = (base > path) ? (size_t)(base - path - 1) : 0;

    ctx->dib = LLVMCreateDIBuilder(ctx->module);
    ctx->di_file = LLVMDIBuilderCreateFile(ctx->dib, base, strlen(base),
                                           path, dir_len);
    ctx->di_cu = LLVMDIBuilderCreateCompileUnit(
        ctx->dib,
        LLVMDWARFSourceLanguageC,  /* borrowed language code until a DWARF
                                      language number is registered for LS */
        ctx->di_file, "lls", 3,
        /*isOptimized*/ ctx->opt.level != LS_OPT_O0,
        "", 0, /*RuntimeVer*/ 0, "", 0,
        LLVMDWARFEmissionLineTablesOnly,
        /*DWOId*/ 0, /*SplitDebugInlining*/ 0,
        /*DebugInfoForProfiling*/ 0, "", 0, "", 0);

    /* Without "Debug Info Version" the backend silently strips all DI. */
    LLVMTypeRef i32 = LLVMInt32TypeInContext(ctx->context);
    LLVMAddModuleFlag(ctx->module, LLVMModuleFlagBehaviorWarning,
                      "Debug Info Version", strlen("Debug Info Version"),
                      LLVMValueAsMetadata(LLVMConstInt(i32, 3, 0)));

    const char *triple = LLVMGetTarget(ctx->module);
    if (triple && strstr(triple, "windows"))
    {
        /* COFF backends only emit CodeView (.debug$S → PDB) with this flag. */
        LLVMAddModuleFlag(ctx->module, LLVMModuleFlagBehaviorWarning,
                          "CodeView", strlen("CodeView"),
                          LLVMValueAsMetadata(LLVMConstInt(i32, 1, 0)));
    }
    else
    {
        LLVMAddModuleFlag(ctx->module, LLVMModuleFlagBehaviorWarning,
                          "Dwarf Version", strlen("Dwarf Version"),
                          LLVMValueAsMetadata(LLVMConstInt(i32, 4, 0)));
    }
}

/* Materialise deferred DI nodes. Must run before the module verifier /
   any pass pipeline sees the module. */
void cg_di_finalize(CodegenContext *ctx)
{
    if (ctx->dib)
        LLVMDIBuilderFinalize(ctx->dib);
}
