/* codegen_di.c — D1 debug info: DIBuilder line-table skeleton
   (docs/plan_debug_info.md phase 1).

   Gated on `lls compile -g` / `lls emit-ir -g`: without the flag ctx->dib
   stays NULL and every hook below is a no-op, so the default pipeline is
   byte-identical. Phase 1 emits LLVMDWARFEmissionLineTablesOnly — crash
   stacks and profilers get file/line attribution without a DI type graph. */

#include "codegen_internal.h"

#include <stdlib.h>
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

/* DIFile for an arbitrary source path, cached per path (imported modules
   all emit into the one LLVM module but must keep their own file/line
   mapping). NULL path = the root source file. */
static LLVMMetadataRef cg_di_file_for(CodegenContext *ctx, const char *path)
{
    if (path == NULL)
        return ctx->di_file;
    for (int i = 0; i < ctx->di_file_count; i++)
        if (strcmp(ctx->di_files[i].path, path) == 0)
            return ctx->di_files[i].file;

    const char *base = path;
    for (const char *p = path; *p; p++)
        if (*p == '/' || *p == '\\')
            base = p + 1;
    size_t dir_len = (base > path) ? (size_t)(base - path - 1) : 0;
    LLVMMetadataRef file = LLVMDIBuilderCreateFile(ctx->dib, base, strlen(base),
                                                   path, dir_len);

    if (ctx->di_file_count >= ctx->di_file_cap)
    {
        ctx->di_file_cap = ctx->di_file_cap ? ctx->di_file_cap * 2 : 8;
        ctx->di_files = realloc(ctx->di_files,
                                (size_t)ctx->di_file_cap * sizeof(ctx->di_files[0]));
        if (ctx->di_files == NULL)
        {
            ctx->di_file_cap = ctx->di_file_count = 0;
            return file;  /* cache full/unavailable — still return the DIFile */
        }
    }
    ctx->di_files[ctx->di_file_count].path = path;
    ctx->di_files[ctx->di_file_count].file = file;
    ctx->di_file_count++;
    return file;
}

/* Attach a subprogram to a function whose body is about to be emitted
   (user fn_decls incl. generic instantiations, and __closure_N literals).
   LineTablesOnly: an empty subroutine type placeholder is legal — no DI
   type graph is built. Also resets the builder's sticky debug location so
   the new body's prologue doesn't inherit the previous function's lines. */
void cg_di_fn_begin(CodegenContext *ctx, LLVMValueRef fn, AstNode *node)
{
    if (ctx->dib == NULL || fn == NULL)
        return;

    LLVMSetCurrentDebugLocation2(ctx->builder, NULL);

    if (LLVMGetSubprogram(fn) != NULL)
        return;

    LLVMMetadataRef file = cg_di_file_for(ctx, ctx->current_emit_file);
    LLVMMetadataRef fn_ty = LLVMDIBuilderCreateSubroutineType(
        ctx->dib, file, NULL, 0, LLVMDIFlagZero);

    size_t name_len = 0;
    const char *name = LLVMGetValueName2(fn, &name_len);
    unsigned line = (node && node->line > 0) ? (unsigned)node->line : 1;

    LLVMMetadataRef sp = LLVMDIBuilderCreateFunction(
        ctx->dib, file, name, name_len, name, name_len, file, line, fn_ty,
        /*IsLocalToUnit*/ 0, /*IsDefinition*/ 1, /*ScopeLine*/ line,
        LLVMDIFlagZero, /*IsOptimized*/ ctx->opt.level != LS_OPT_O0);
    LLVMSetSubprogram(fn, sp);

    /* Discipline (§3.2): set a location on function entry and NEVER leave the
       builder loc-less inside a subprogram — a call to another subprogram
       function without !dbg is a hard verifier error ("inlinable function
       call in a function with debug info must have a !dbg location"). */
    LLVMSetCurrentDebugLocation2(ctx->builder,
        LLVMDIBuilderCreateDebugLocation(ctx->context, line, 0, sp, NULL));
}

/* Statement-level location hook (codegen_stmt entry). The location scope is
   the insert-point function's own subprogram; when the builder currently
   sits in a subprogram-less function (synthesised helpers, __ls_global_stmts,
   closures before fn_begin) the location is cleared instead — a stale outer
   location there would be broken DI (scope from a different function). */
void cg_di_stmt_loc(CodegenContext *ctx, AstNode *node)
{
    if (ctx->dib == NULL)
        return;

    LLVMBasicBlockRef bb = LLVMGetInsertBlock(ctx->builder);
    LLVMValueRef fn = bb ? LLVMGetBasicBlockParent(bb) : NULL;
    LLVMMetadataRef sp = fn ? LLVMGetSubprogram(fn) : NULL;
    if (sp == NULL)
    {
        LLVMSetCurrentDebugLocation2(ctx->builder, NULL);
        return;
    }
    /* Checker-lowered synthesised statements carry line 0 — keep the previous
       statement's location rather than going loc-less (see fn_begin note). */
    if (node == NULL || node->line <= 0)
        return;
    LLVMMetadataRef loc = LLVMDIBuilderCreateDebugLocation(
        ctx->context, (unsigned)node->line,
        node->column > 0 ? (unsigned)node->column : 0, sp, NULL);
    LLVMSetCurrentDebugLocation2(ctx->builder, loc);
}

/* Safety net: the builder's debug location is sticky, so a helper function
   emitted lazily in the middle of a statement (auto-drop bodies, env clones,
   fn→Block thunks, ...) picks up the enclosing statement's location — whose
   scope belongs to a DIFFERENT function. The verifier treats that as broken
   DI and strips ALL debug info. Rather than chase every helper-emission
   site, drop any location whose scope isn't the containing function's own
   subprogram. */
static void cg_di_strip_mismatched_locs(CodegenContext *ctx)
{
    unsigned dbg_kind = LLVMGetMDKindIDInContext(ctx->context, "dbg", 3);
    for (LLVMValueRef fn = LLVMGetFirstFunction(ctx->module); fn;
         fn = LLVMGetNextFunction(fn))
    {
        LLVMMetadataRef fsp = LLVMGetSubprogram(fn);
        for (LLVMBasicBlockRef bb = LLVMGetFirstBasicBlock(fn); bb;
             bb = LLVMGetNextBasicBlock(bb))
        {
            for (LLVMValueRef inst = LLVMGetFirstInstruction(bb); inst;
                 inst = LLVMGetNextInstruction(inst))
            {
                LLVMValueRef md = LLVMGetMetadata(inst, dbg_kind);
                if (md == NULL)
                    continue;
                if (fsp == NULL ||
                    LLVMDILocationGetScope(LLVMValueAsMetadata(md)) != fsp)
                    LLVMSetMetadata(inst, dbg_kind, NULL);
            }
        }
    }
}

/* Materialise deferred DI nodes. Must run before the module verifier /
   any pass pipeline sees the module. */
void cg_di_finalize(CodegenContext *ctx)
{
    if (ctx->dib == NULL)
        return;
    cg_di_strip_mismatched_locs(ctx);
    LLVMDIBuilderFinalize(ctx->dib);
}
