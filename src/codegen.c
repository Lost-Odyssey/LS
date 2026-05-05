/* codegen.c — AST to LLVM IR code generation */
#include "codegen.h"
#include "module.h"
#define LS_INCLUDE_CODEGEN 1
#include "builtins_math.h"
#define LS_INCLUDE_CODEGEN 1
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

/* CG_DEBUG is defined in common.h (default 0).
   Set to 1 via -DCG_DEBUG=1 at build time to inject runtime printf traces
   at every compiler-managed memory alloc/clone/free/drop point. */

/* Global counter for unique basic block names */
static int g_block_counter = 0;

/* Forward declarations for Phase E.2 ABI lowering helpers (definitions live
   near extern_fn_type lower in this file but are referenced from AST_CALL). */
static int extern_struct_size(CodegenContext *ctx, Type *t);
static bool extern_struct_fits_in_reg(int sz);
static LLVMTypeRef extern_struct_reg_int_type(CodegenContext *ctx, int sz);

/* Phase 4 (__move): unwrap `__move(x)` call wrappers so downstream AST-shape
   checks (e.g. vec.push testing `arg->kind == AST_IDENT` for ownership
   transfer) can see through the explicit-move annotation.
   `__move` is a type-preserving no-op at codegen; the checker has already
   marked the source variable as moved and rejected any later use. */
static AstNode *ast_unwrap_move(AstNode *n)
{
    while (n && n->kind == AST_CALL &&
           n->as.call.callee && n->as.call.callee->kind == AST_IDENT &&
           strcmp(n->as.call.callee->as.ident.name, "__move") == 0 &&
           n->as.call.arg_count == 1)
    {
        n = n->as.call.args[0];
    }
    return n;
}

/* ---- Error reporting ---- */

static void cg_error(CodegenContext *ctx, int line, int col, const char *fmt, ...)
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
static void cg_emit_debug_printf(CodegenContext *ctx,
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
static void cg_emit_debug_printf(CodegenContext *ctx,
                                 const char *fmt_cstr,
                                 LLVMValueRef *args, int nargs)
{
    (void)ctx;
    (void)fmt_cstr;
    (void)args;
    (void)nargs;
}
#endif /* CG_DEBUG */

/* ---- CgScope management ---- */

static CgScope *cg_scope_new(CgScope *parent)
{
    CgScope *s = (CgScope *)malloc_safe(sizeof(CgScope));
    s->symbols = NULL;
    s->count = 0;
    s->capacity = 0;
    s->parent = parent;
    return s;
}

static void cg_scope_free(CgScope *s)
{
    if (s == NULL)
        return;
    free(s->symbols);
    free(s);
}

static CgSymbol *cg_scope_define(CgScope *s, const char *name, LLVMValueRef val, Type *type,
                                 LLVMValueRef moved_flag)
{
    if (s->count >= s->capacity)
    {
        s->capacity = GROW_CAPACITY(s->capacity);
        s->symbols = GROW_ARRAY(CgSymbol, s->symbols, s->capacity);
    }
    s->symbols[s->count].name = name;
    s->symbols[s->count].value = val;
    s->symbols[s->count].type = type;
    s->symbols[s->count].moved_flag = moved_flag;
    s->symbols[s->count].is_borrowed = false;
    s->symbols[s->count].is_mut_borrow = false;
    return &s->symbols[s->count++];
}

static CgSymbol *cg_scope_resolve(CgScope *s, const char *name)
{
    for (CgScope *cur = s; cur != NULL; cur = cur->parent)
    {
        for (int i = 0; i < cur->count; i++)
        {
            if (strcmp(cur->symbols[i].name, name) == 0)
            {
                return &cur->symbols[i];
            }
        }
    }
    return NULL;
}

static void push_scope(CodegenContext *ctx)
{
    ctx->current_scope = cg_scope_new(ctx->current_scope);
}

/* Forward declarations for string cleanup and cloning (defined after LsString helpers).
   kind + line/col are memcheck free-site labels — pass NULL/0/0 for "unknown" fallback. */
static void emit_string_free_with_cont(CodegenContext *ctx, LLVMValueRef str_alloca,
                                       LLVMBasicBlockRef *out_cont, const char *var_name,
                                       const char *free_kind, int free_line, int free_col);
static void emit_string_free_kind(CodegenContext *ctx, LLVMValueRef str_alloca,
                                  const char *free_kind, int free_line, int free_col);
static void emit_string_free(CodegenContext *ctx, LLVMValueRef str_alloca);
static void emit_string_free_named(CodegenContext *ctx, LLVMValueRef str_alloca,
                                   const char *var_name);
static LLVMBasicBlockRef emit_string_free_separate(CodegenContext *ctx, LLVMValueRef str_alloca,
                                                    const char *free_kind, int free_line, int free_col);
/* Clone a string VALUE (not alloca): if cap>0 → malloc+memcpy owned copy; else keep as-is */
static LLVMValueRef emit_string_clone_val(CodegenContext *ctx, LLVMValueRef str_val);
/* Clone a struct VALUE: deep-copy all owned string fields, returns new struct value */
static LLVMValueRef emit_struct_clone_val(CodegenContext *ctx, LLVMValueRef struct_val,
                                          LLVMTypeRef llvm_struct_type, Type *struct_type);
/* Clone an array VALUE: deep-copy each has_drop element, returns new array value */
static LLVMValueRef emit_array_clone_val(CodegenContext *ctx, LLVMValueRef arr_val,
                                         LLVMTypeRef llvm_arr_type, Type *arr_type);
/* Clone a vec VALUE (struct {data*, len, cap}): malloc new data, copy+clone elements */
static LLVMValueRef emit_vec_clone_val(CodegenContext *ctx, LLVMValueRef vec_val,
                                       Type *elem_type);
static void emit_scope_cleanup(CodegenContext *ctx);
static void emit_cleanup_to(CodegenContext *ctx, CgScope *stop, LLVMValueRef skip_alloca);
static void emit_struct_drop(CodegenContext *ctx, LLVMValueRef drop_ptr, Type *struct_type);
static void emit_struct_drop_cond(CodegenContext *ctx, LLVMValueRef drop_ptr,
                                  Type *struct_type, LLVMValueRef moved_flag);
static LLVMBasicBlockRef emit_struct_drop_separate(CodegenContext *ctx, LLVMValueRef drop_ptr,
                                                   Type *struct_type, LLVMValueRef moved_flag);
static void emit_drop_field_cleanup(CodegenContext *ctx);
LLVMTypeRef type_to_llvm(CodegenContext *ctx, Type *t);
static LLVMTypeRef build_variant_payload_struct(CodegenContext *ctx, Type *enum_type, int variant_idx);
static LLVMValueRef emit_enum_ctor(CodegenContext *ctx, AstNode *node,
                                   Type *enum_type, int variant_idx,
                                   AstNode **args, int arg_count);
static void emit_auto_enum_drop_fn(CodegenContext *ctx, Type *enum_type);
static void emit_enum_drop(CodegenContext *ctx, LLVMValueRef enum_ptr, Type *enum_type);
static void emit_map_helpers_for(CodegenContext *ctx, Type *key_type, Type *val_type);
static LLVMValueRef codegen_map_method(CodegenContext *ctx, AstNode *call_node, Type *map_type);
static LLVMBasicBlockRef emit_vec_elem_drop_at(CodegenContext *ctx, LLVMValueRef elem_ptr,
                                               Type *elem_type, int idx_suffix,
                                               const char *label);

static void pop_scope(CodegenContext *ctx)
{
    CgScope *old = ctx->current_scope;
    ctx->current_scope = old->parent;
    cg_scope_free(old);
}

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
static LLVMValueRef cg_widen(CodegenContext *ctx, LLVMValueRef val,
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

/* ---- Struct type registry ---- */

static void register_struct_llvm(CodegenContext *ctx, const char *name,
                                 LLVMTypeRef llvm_type, Type *ls_type)
{
    if (ctx->struct_type_count >= ctx->struct_type_cap)
    {
        ctx->struct_type_cap = GROW_CAPACITY(ctx->struct_type_cap);
        ctx->struct_types = realloc_safe(ctx->struct_types,
                                         (size_t)ctx->struct_type_cap * sizeof(ctx->struct_types[0]));
    }
    ctx->struct_types[ctx->struct_type_count].name = name;
    ctx->struct_types[ctx->struct_type_count].llvm_type = llvm_type;
    ctx->struct_types[ctx->struct_type_count].ls_type = ls_type;
    ctx->struct_type_count++;
}

static LLVMTypeRef find_struct_llvm(CodegenContext *ctx, const char *name)
{
    for (int i = 0; i < ctx->struct_type_count; i++)
    {
        if (strcmp(ctx->struct_types[i].name, name) == 0)
        {
            return ctx->struct_types[i].llvm_type;
        }
    }
    return NULL;
}

static Type *find_struct_ls_type(CodegenContext *ctx, const char *name)
{
    for (int i = 0; i < ctx->struct_type_count; i++)
    {
        if (strcmp(ctx->struct_types[i].name, name) == 0)
        {
            return ctx->struct_types[i].ls_type;
        }
    }
    return NULL;
}

static Type *find_struct_ls(CodegenContext *ctx, const char *name)
{
    for (int i = 0; i < ctx->struct_type_count; i++)
    {
        if (strcmp(ctx->struct_types[i].name, name) == 0)
        {
            return ctx->struct_types[i].ls_type;
        }
    }
    return NULL;
}

/* ---- Enum LLVM type registry ---- */

static int find_enum_idx(CodegenContext *ctx, const char *name)
{
    for (int i = 0; i < ctx->enum_type_count; i++)
    {
        if (strcmp(ctx->enum_types[i].name, name) == 0)
            return i;
    }
    return -1;
}

static LLVMTypeRef find_enum_llvm(CodegenContext *ctx, const char *name)
{
    int i = find_enum_idx(ctx, name);
    return (i >= 0) ? ctx->enum_types[i].llvm_type : NULL;
}

static int enum_payload_bytes(CodegenContext *ctx, const char *name)
{
    int i = find_enum_idx(ctx, name);
    return (i >= 0) ? ctx->enum_types[i].payload_bytes : 0;
}

static void register_enum_llvm(CodegenContext *ctx, const char *name,
                               LLVMTypeRef llvm_type, Type *ls_type, int payload_bytes)
{
    if (ctx->enum_type_count >= ctx->enum_type_cap)
    {
        ctx->enum_type_cap = GROW_CAPACITY(ctx->enum_type_cap);
        ctx->enum_types = realloc_safe(ctx->enum_types,
            (size_t)ctx->enum_type_cap * sizeof(ctx->enum_types[0]));
    }
    ctx->enum_types[ctx->enum_type_count].name = name;
    ctx->enum_types[ctx->enum_type_count].llvm_type = llvm_type;
    ctx->enum_types[ctx->enum_type_count].ls_type = ls_type;
    ctx->enum_types[ctx->enum_type_count].payload_bytes = payload_bytes;
    ctx->enum_type_count++;
}

/* ---- LsVec LLVM type: { ptr, i32, i32 } = { data, len, cap } ---- */

/* Get or create the LsVec LLVM struct type (shared across all vec(T) specialisations,
   since the layout is the same — data is an opaque ptr regardless of element type).
   cap == 0 means empty/unallocated (data may be NULL, nothing to free).
   cap >  0 means heap-allocated (caller must free data). */
static LLVMTypeRef ls_vec_type(CodegenContext *ctx)
{
    LLVMTypeRef existing = LLVMGetTypeByName2(ctx->context, "LsVec");
    if (existing)
        return existing;
    LLVMTypeRef fields[3] = {
        LLVMPointerTypeInContext(ctx->context, 0), /* ptr  data */
        LLVMInt32TypeInContext(ctx->context),      /* i32  len  */
        LLVMInt32TypeInContext(ctx->context),      /* i32  cap  */
    };
    LLVMTypeRef st = LLVMStructCreateNamed(ctx->context, "LsVec");
    LLVMStructSetBody(st, fields, 3, 0);
    return st;
}

/* ---- LsMap LLVM type: { ptr, i32, i32 } = { buckets, len, cap } ---- */

static LLVMTypeRef ls_map_type(CodegenContext *ctx)
{
    LLVMTypeRef existing = LLVMGetTypeByName2(ctx->context, "LsMap");
    if (existing)
        return existing;
    LLVMTypeRef fields[3] = {
        LLVMPointerTypeInContext(ctx->context, 0),
        LLVMInt32TypeInContext(ctx->context),
        LLVMInt32TypeInContext(ctx->context),
    };
    LLVMTypeRef st = LLVMStructCreateNamed(ctx->context, "LsMap");
    LLVMStructSetBody(st, fields, 3, 0);
    return st;
}

/* Build a short suffix for the (K, V) type pair. */
static void map_type_id(Type *key_type, Type *val_type, char *buf, int sz)
{
    const char *k = (key_type && key_type->kind == TYPE_STRING) ? "s" : "i";
    const char *v = "i";
    if (val_type)
    {
        if (val_type->kind == TYPE_STRING)
            v = "s";
        else if (val_type->kind == TYPE_BOOL)
            v = "b";
        else if (val_type->kind == TYPE_F32 || val_type->kind == TYPE_F64)
            v = "f";
        else if (val_type->kind == TYPE_STRUCT && val_type->as.strukt.name)
        {
            snprintf(buf, sz, "%s_%s", k, val_type->as.strukt.name);
            return;
        }
    }
    snprintf(buf, sz, "%s_%s", k, v);
}

/* Get or create the per-(K,V) LsMapNode LLVM struct type.
   Layout: { i64 hash, K key, V val, ptr next } */
static LLVMTypeRef ls_map_node_type(CodegenContext *ctx, Type *key_type, Type *val_type)
{
    char suffix[64];
    map_type_id(key_type, val_type, suffix, sizeof(suffix));
    char name[96];
    snprintf(name, sizeof(name), "LsMapNode_%s", suffix);
    LLVMTypeRef existing = LLVMGetTypeByName2(ctx->context, name);
    if (existing)
        return existing;
    LLVMTypeRef fields[4] = {
        LLVMInt64TypeInContext(ctx->context),
        type_to_llvm(ctx, key_type),
        type_to_llvm(ctx, val_type),
        LLVMPointerTypeInContext(ctx->context, 0),
    };
    LLVMTypeRef st = LLVMStructCreateNamed(ctx->context, name);
    LLVMStructSetBody(st, fields, 4, 0);
    return st;
}

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
static LLVMValueRef cg_mc_alloc_fn(CodegenContext *ctx) {
    LLVMValueRef fn = LLVMGetNamedFunction(ctx->module, "ls_mc_alloc");
    if (fn) return fn;
    LLVMTypeRef ptr = LLVMPointerTypeInContext(ctx->context, 0);
    LLVMTypeRef i64 = LLVMInt64TypeInContext(ctx->context);
    LLVMTypeRef params[2] = { i64, ptr };
    LLVMTypeRef ft = LLVMFunctionType(ptr, params, 2, 0);
    return LLVMAddFunction(ctx->module, "ls_mc_alloc", ft);
}

static LLVMValueRef cg_mc_free_fn(CodegenContext *ctx) {
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
static LLVMValueRef cg_module_cstr(CodegenContext *ctx, const char *s,
                                   const char *gv_name);

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
static void cg_emit_mc_enter(CodegenContext *ctx, const char *fn_name,
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
static void cg_emit_mc_leave(CodegenContext *ctx)
{
    if (!ctx->memcheck_enabled) return;
    LLVMTypeRef ft = LLVMFunctionType(LLVMVoidTypeInContext(ctx->context),
                                      NULL, 0, 0);
    LLVMBuildCall2(ctx->builder, ft, cg_mc_leave_fn(ctx), NULL, 0, "");
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
static LLVMValueRef cg_make_site(CodegenContext *ctx, const char *kind,
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

/* Convenience accessors for the current expr's source location. Used by
   helpers that don't have direct AST access to avoid threading nodes
   through. Falls back to 0/0 when no current node is set. */
#define CG_LINE(ctx) ((ctx)->current_node ? (ctx)->current_node->line   : 0)
#define CG_COL(ctx)  ((ctx)->current_node ? (ctx)->current_node->column : 0)

/* Emit an allocation. When memcheck is on, calls ls_mc_alloc(size, site).
   When off, calls plain malloc(size). Returns the pointer value.
   Exposed via codegen.h so built-in stdlib codegen (io / fs / ...) can
   tag their allocations with a meaningful kind + LS source location. */
LLVMValueRef cg_emit_alloc(CodegenContext *ctx, LLVMValueRef size,
                           const char *kind, int line, int col) {
    if (ctx->memcheck_enabled) {
        LLVMValueRef fn = cg_mc_alloc_fn(ctx);
        LLVMValueRef site = cg_make_site(ctx, kind, line, col);
        LLVMValueRef args[2] = { size, site };
        LLVMTypeRef ft = LLVMGlobalGetValueType(fn);
        return LLVMBuildCall2(ctx->builder, ft, fn, args, 2, "mc.p");
    }
    /* Fallback: vanilla malloc */
    LLVMValueRef malloc_fn = LLVMGetNamedFunction(ctx->module, "malloc");
    if (!malloc_fn) {
        LLVMTypeRef ptr = LLVMPointerTypeInContext(ctx->context, 0);
        LLVMTypeRef i64 = LLVMInt64TypeInContext(ctx->context);
        LLVMTypeRef params[1] = { i64 };
        LLVMTypeRef ft = LLVMFunctionType(ptr, params, 1, 0);
        malloc_fn = LLVMAddFunction(ctx->module, "malloc", ft);
    }
    LLVMTypeRef ft = LLVMGlobalGetValueType(malloc_fn);
    return LLVMBuildCall2(ctx->builder, ft, malloc_fn, &size, 1, "p");
}

/* Emit a free. Memcheck on → ls_mc_free(ptr, site). Off → free(ptr). */
static void cg_emit_free(CodegenContext *ctx, LLVMValueRef ptr,
                         const char *kind, int line, int col) {
    if (ctx->memcheck_enabled) {
        LLVMValueRef fn = cg_mc_free_fn(ctx);
        LLVMValueRef site = cg_make_site(ctx, kind, line, col);
        LLVMValueRef args[2] = { ptr, site };
        LLVMTypeRef ft = LLVMGlobalGetValueType(fn);
        LLVMBuildCall2(ctx->builder, ft, fn, args, 2, "");
        return;
    }
    LLVMValueRef free_fn = LLVMGetNamedFunction(ctx->module, "free");
    if (!free_fn) {
        LLVMTypeRef ptr_ty = LLVMPointerTypeInContext(ctx->context, 0);
        LLVMTypeRef params[1] = { ptr_ty };
        LLVMTypeRef ft = LLVMFunctionType(LLVMVoidTypeInContext(ctx->context),
                                          params, 1, 0);
        free_fn = LLVMAddFunction(ctx->module, "free", ft);
    }
    LLVMTypeRef ft = LLVMGlobalGetValueType(free_fn);
    LLVMBuildCall2(ctx->builder, ft, free_fn, &ptr, 1, "");
}

/* ---- LsString LLVM type: { i8*, i32, i32 } = { data, len, cap } ---- */

/* Get or create the LsString LLVM struct type.
   cap == 0 means static literal (data points to global constant, don't free).
   cap > 0 means heap-allocated (caller must free data). */
LLVMTypeRef ls_string_type(CodegenContext *ctx)
{
    /* Cache: use a named struct so we only create it once per module */
    LLVMTypeRef existing = LLVMGetTypeByName2(ctx->context, "LsString");
    if (existing)
        return existing;

    LLVMTypeRef fields[3] = {
        LLVMPointerTypeInContext(ctx->context, 0), /* i8* data */
        LLVMInt32TypeInContext(ctx->context),      /* i32 len  */
        LLVMInt32TypeInContext(ctx->context),      /* i32 cap  */
    };
    LLVMTypeRef st = LLVMStructCreateNamed(ctx->context, "LsString");
    LLVMStructSetBody(st, fields, 3, 0);
    return st;
}

/* Build an LsString constant struct value from components */
LLVMValueRef ls_string_make(CodegenContext *ctx, LLVMValueRef data,
                            LLVMValueRef len, LLVMValueRef cap)
{
    LLVMTypeRef st = ls_string_type(ctx);
    LLVMValueRef undef = LLVMGetUndef(st);
    LLVMValueRef v = LLVMBuildInsertValue(ctx->builder, undef, data, 0, "str.d");
    v = LLVMBuildInsertValue(ctx->builder, v, len, 1, "str.l");
    v = LLVMBuildInsertValue(ctx->builder, v, cap, 2, "str.c");
    return v;
}

/* Build a static LsString from a global string pointer (cap=0 = static) */
LLVMValueRef ls_string_from_literal(CodegenContext *ctx,
                                    const char *text, const char *name)
{
    LLVMValueRef data = LLVMBuildGlobalStringPtr(ctx->builder, text, name);
    int slen = (int)strlen(text);
    LLVMValueRef len = LLVMConstInt(LLVMInt32TypeInContext(ctx->context),
                                    (unsigned long long)slen, 0);
    LLVMValueRef cap = LLVMConstInt(LLVMInt32TypeInContext(ctx->context), 0, 0);
    return ls_string_make(ctx, data, len, cap);
}

/* Build a constant (compile-time) LsString struct for global initializers */
static LLVMValueRef ls_string_const(CodegenContext *ctx, LLVMValueRef data,
                                    int slen, int scap)
{
    LLVMTypeRef i32 = LLVMInt32TypeInContext(ctx->context);
    LLVMValueRef fields[3] = {
        data,
        LLVMConstInt(i32, (unsigned long long)slen, 0),
        LLVMConstInt(i32, (unsigned long long)scap, 0),
    };
    return LLVMConstStructInContext(ctx->context, fields, 3, 0);
}

/* Extract the .data (i8*) field from an LsString value */
static LLVMValueRef ls_string_data(CodegenContext *ctx, LLVMValueRef str_val)
{
    return LLVMBuildExtractValue(ctx->builder, str_val, 0, "str.data");
}

/* Extract the .len (i32) field from an LsString value */
static LLVMValueRef ls_string_len(CodegenContext *ctx, LLVMValueRef str_val)
{
    return LLVMBuildExtractValue(ctx->builder, str_val, 1, "str.len");
}

/* Compute the allocation capacity for a given string length.
   Returns max(LS_MIN_STR_CAP, next_power_of_2(need)) where need = len + 1. */
static int ls_str_alloc_cap(int slen)
{
    int need = slen + 1; /* +1 for null terminator */
    if (need <= LS_MIN_STR_CAP)
        return LS_MIN_STR_CAP;
    /* Round up to next power of 2 */
    int cap = LS_MIN_STR_CAP;
    while (cap < need)
        cap *= 2;
    return cap;
}

/* ---- String auto-free: scope cleanup for dynamic strings ---- */

/* String capacity semantics:
   cap = -1: MOVED — already moved to another variable, skip cleanup
   cap =  0: STATIC — static literal (data in .rodata), skip cleanup
   cap >  0: OWNED — heap-allocated, must call free(data) on cleanup */

/* Check if a string value needs cleanup: returns true if cap > 0.
   str_alloca: the alloca pointer to the LsString struct. */
static bool needs_string_cleanup(CodegenContext *ctx, LLVMValueRef str_alloca)
{
    LLVMTypeRef str_type = ls_string_type(ctx);
    LLVMValueRef str_val = LLVMBuildLoad2(ctx->builder, str_type, str_alloca, "nsc.val");
    LLVMValueRef cap = LLVMBuildExtractValue(ctx->builder, str_val, 2, "nsc.cap");
    LLVMValueRef zero = LLVMConstInt(LLVMInt32TypeInContext(ctx->context), 0, 0);
    return LLVMBuildICmp(ctx->builder, LLVMIntSGT, cap, zero, "nsc.is_owned") != NULL;
}

/* Mark a string as moved by setting cap = -1, but ONLY if cap > 0 (heap-owned).
   Static strings (cap == 0) never need a moved marker: the vec/map cleanup sees cap == 0
   and skips free, and so does scope cleanup — no double-free risk, no mark needed.
   Already-moved strings (cap == -1) are left unchanged.
   reason: short C string for CG_DEBUG output. */
static void mark_string_moved(CodegenContext *ctx, LLVMValueRef str_alloca,
                              const char *reason)
{
    LLVMTypeRef str_type = ls_string_type(ctx);
    LLVMTypeRef i32_type = LLVMInt32TypeInContext(ctx->context);

    LLVMBasicBlockRef cur_bb = LLVMGetInsertBlock(ctx->builder);
    LLVMValueRef cur_fn = LLVMGetBasicBlockParent(cur_bb);

    /* Load current cap to decide whether a mark is needed at all */
    LLVMValueRef str_val = LLVMBuildLoad2(ctx->builder, str_type, str_alloca, "msm.val");
    LLVMValueRef cap = LLVMBuildExtractValue(ctx->builder, str_val, 2, "msm.cap");
    LLVMValueRef zero32 = LLVMConstInt(i32_type, 0, 0);

    /* Only mark if cap > 0 (heap-owned); skip static (cap==0) and already-moved (cap==-1) */
    LLVMValueRef is_owned = LLVMBuildICmp(ctx->builder, LLVMIntSGT, cap, zero32, "msm.owned");

    LLVMBasicBlockRef do_bb = LLVMAppendBasicBlockInContext(ctx->context, cur_fn, "msm.do");
    LLVMBasicBlockRef skip_bb = LLVMAppendBasicBlockInContext(ctx->context, cur_fn, "msm.skip");
    LLVMBuildCondBr(ctx->builder, is_owned, do_bb, skip_bb);

    /* --- mark path: heap-owned, set cap = -1 --- */
    LLVMPositionBuilderAtEnd(ctx->builder, do_bb);
    LLVMValueRef data = LLVMBuildExtractValue(ctx->builder, str_val, 0, "msm.data");
    LLVMValueRef len = LLVMBuildExtractValue(ctx->builder, str_val, 1, "msm.len");
    LLVMValueRef neg1 = LLVMConstInt(i32_type, (unsigned long long)-1, 1);
    LLVMValueRef undef = LLVMGetUndef(str_type);
    LLVMValueRef new_str = LLVMBuildInsertValue(ctx->builder, undef, data, 0, "msm.d");
    new_str = LLVMBuildInsertValue(ctx->builder, new_str, len, 1, "msm.l");
    new_str = LLVMBuildInsertValue(ctx->builder, new_str, neg1, 2, "msm.c");
    LLVMBuildStore(ctx->builder, new_str, str_alloca);
#if CG_DEBUG
    {
        char fmt[128];
        snprintf(fmt, sizeof(fmt), "[cg] str.moved  cap=-1  (%s)\n",
                 reason ? reason : "?");
        cg_emit_debug_printf(ctx, fmt, NULL, 0);
    }
#endif
    LLVMBuildBr(ctx->builder, skip_bb);

    /* --- skip path: static or already-moved, nothing to do --- */
    LLVMPositionBuilderAtEnd(ctx->builder, skip_bb);
    /* builder left here for callers to continue emitting IR */
}

/* Emit string cleanup - creates a complete cleanup block with its own continuation.
   var_name: optional variable name for CG_DEBUG output (may be NULL).
   free_kind / free_line / free_col: memcheck free-site label (NULL/0/0 = "unknown").
   Handles three states: cap==0 (static, skip), cap>0 (owned, free), cap==-1 (moved, skip).
   Leaves the builder at the continuation block so callers can chain more instructions. */
static void emit_string_free_with_cont(CodegenContext *ctx, LLVMValueRef str_alloca,
                                       LLVMBasicBlockRef *out_cont, const char *var_name,
                                       const char *free_kind, int free_line, int free_col)
{
    LLVMTypeRef str_type = ls_string_type(ctx);
    LLVMTypeRef i32_type = LLVMInt32TypeInContext(ctx->context);

    LLVMBasicBlockRef cur_bb = LLVMGetInsertBlock(ctx->builder);
    LLVMValueRef cur_fn = LLVMGetBasicBlockParent(cur_bb);

    int id = g_block_counter++;
    char cleanup_name[32], skip_name[32], free_name[32], cont_name[32];
    snprintf(cleanup_name, sizeof(cleanup_name), "sf.cleanup%d", id);
    snprintf(skip_name, sizeof(skip_name), "sf.skip%d", id);
    snprintf(free_name, sizeof(free_name), "sf.free%d", id);
    snprintf(cont_name, sizeof(cont_name), "sf.cont%d", id);
    LLVMBasicBlockRef cleanup_bb = LLVMAppendBasicBlockInContext(ctx->context, cur_fn, cleanup_name);
    LLVMBasicBlockRef skip_bb = LLVMAppendBasicBlockInContext(ctx->context, cur_fn, skip_name);
    LLVMBasicBlockRef free_bb = LLVMAppendBasicBlockInContext(ctx->context, cur_fn, free_name);
    LLVMBasicBlockRef cont_bb = LLVMAppendBasicBlockInContext(ctx->context, cur_fn, cont_name);

    /* Branch from current to cleanup */
    LLVMBuildBr(ctx->builder, cleanup_bb);

    /* Build cleanup block: load cap, branch on cap > 0 */
    LLVMPositionBuilderAtEnd(ctx->builder, cleanup_bb);
    LLVMValueRef str_val = LLVMBuildLoad2(ctx->builder, str_type, str_alloca, "sf.str");
    LLVMValueRef cap = LLVMBuildExtractValue(ctx->builder, str_val, 2, "sf.cap");
    LLVMValueRef zero = LLVMConstInt(i32_type, 0, 0);
    LLVMValueRef is_owned = LLVMBuildICmp(ctx->builder, LLVMIntSGT, cap, zero, "sf.owned");
    LLVMBuildCondBr(ctx->builder, is_owned, free_bb, skip_bb);

    /* skip path: cap <= 0 (static cap==0 or moved cap==-1) — nothing to free */
    LLVMPositionBuilderAtEnd(ctx->builder, skip_bb);
#if CG_DEBUG
    {
        char dbg_fmt[128];
        snprintf(dbg_fmt, sizeof(dbg_fmt),
                 "[cg] str.skip   var=%-12s cap=%%d  (static or moved)\n",
                 var_name ? var_name : "?");
        LLVMValueRef skip_da[1] = {cap};
        cg_emit_debug_printf(ctx, dbg_fmt, skip_da, 1);
    }
#endif
    LLVMBuildBr(ctx->builder, cont_bb);

    /* free path: cap > 0 (owned heap string) — call free(data) via memcheck */
    LLVMPositionBuilderAtEnd(ctx->builder, free_bb);
    LLVMValueRef data = LLVMBuildExtractValue(ctx->builder, str_val, 0, "sf.data");
#if CG_DEBUG
    {
        LLVMValueRef len_val = LLVMBuildExtractValue(ctx->builder, str_val, 1, "sf.len");
        char dbg_fmt[160];
        snprintf(dbg_fmt, sizeof(dbg_fmt),
                 "[cg] str.free   var=%-12s cap=%%d len=%%d ptr=%%p \"%%.*s\"\n",
                 var_name ? var_name : "?");
        LLVMValueRef dbg_args[5] = {cap, len_val, data, len_val, data};
        cg_emit_debug_printf(ctx, dbg_fmt, dbg_args, 5);
    }
#endif
    cg_emit_free(ctx, data, free_kind ? free_kind : "unknown",
                 free_line, free_col);
    LLVMBuildBr(ctx->builder, cont_bb);

    LLVMPositionBuilderAtEnd(ctx->builder, cont_bb);

    if (out_cont)
        *out_cont = cont_bb;
}

/* Emit string free with memcheck kind labels. Use emit_string_free_kind directly
   from callers that have context; the bare emit_string_free() is kept for
   backward compat and passes "unknown"/0/0. */
static void emit_string_free_kind(CodegenContext *ctx, LLVMValueRef str_alloca,
                                  const char *free_kind, int free_line, int free_col)
{
    emit_string_free_with_cont(ctx, str_alloca, NULL, NULL,
                               free_kind, free_line, free_col);
}

/* Emit string free (unnamed, no memcheck kind — "unknown" fallback). */
static void emit_string_free(CodegenContext *ctx, LLVMValueRef str_alloca)
{
    emit_string_free_with_cont(ctx, str_alloca, NULL, NULL,
                               "unknown", 0, 0);
}

/* Emit string free with a known variable name (for CG_DEBUG clarity)
   and memcheck kind labels. */
static void emit_string_free_named_kind(CodegenContext *ctx, LLVMValueRef str_alloca,
                                        const char *var_name,
                                        const char *free_kind, int free_line, int free_col)
{
    emit_string_free_with_cont(ctx, str_alloca, NULL, var_name,
                               free_kind, free_line, free_col);
}

/* Emit string free with a known variable name (for CG_DEBUG clarity).
   Memcheck kind defaults to "unknown". */
static void emit_string_free_named(CodegenContext *ctx, LLVMValueRef str_alloca,
                                   const char *var_name)
{
    emit_string_free_with_cont(ctx, str_alloca, NULL, var_name,
                               "unknown", 0, 0);
}

/* Emit string cleanup in a separate block (for chaining multiple cleanups).
   free_kind / free_line / free_col: memcheck site info. */
static LLVMBasicBlockRef emit_string_free_separate(CodegenContext *ctx, LLVMValueRef str_alloca,
                                                    const char *free_kind, int free_line, int free_col)
{
    LLVMBasicBlockRef cont;
    emit_string_free_with_cont(ctx, str_alloca, &cont, NULL,
                               free_kind, free_line, free_col);
    return cont;
}

/* Emit cleanup for a temporary string result.
   This is used when an expression returns a dynamic string that isn't
   assigned to a variable (e.g., "hello".upper() as a statement).
   str_val: the LsString struct value (not an alloca). */
static void emit_temp_string_cleanup(CodegenContext *ctx, LLVMValueRef str_val)
{
    LLVMTypeRef i32_type = LLVMInt32TypeInContext(ctx->context);

    /* Extract cap (field 2) */
    LLVMValueRef cap = LLVMBuildExtractValue(ctx->builder, str_val, 2, "tsc.cap");

    /* Check if cap > 0 (owned) */
    LLVMValueRef zero = LLVMConstInt(i32_type, 0, 0);
    LLVMValueRef is_owned = LLVMBuildICmp(ctx->builder, LLVMIntSGT, cap,
                                          zero, "tsc.owned");

    LLVMValueRef cur_fn = LLVMGetBasicBlockParent(LLVMGetInsertBlock(ctx->builder));
    LLVMBasicBlockRef skip_bb = LLVMAppendBasicBlockInContext(
        ctx->context, cur_fn, "tsc.skip");
    LLVMBasicBlockRef free_bb = LLVMAppendBasicBlockInContext(
        ctx->context, cur_fn, "tsc.free");
    LLVMBasicBlockRef cont_bb = LLVMAppendBasicBlockInContext(
        ctx->context, cur_fn, "tsc.cont");

    LLVMBuildCondBr(ctx->builder, is_owned, free_bb, skip_bb);

    LLVMPositionBuilderAtEnd(ctx->builder, skip_bb);
    LLVMBuildBr(ctx->builder, cont_bb);

    LLVMPositionBuilderAtEnd(ctx->builder, free_bb);
    LLVMValueRef data = LLVMBuildExtractValue(ctx->builder, str_val, 0, "tsc.data");
    cg_emit_free(ctx, data, "string.temp", CG_LINE(ctx), CG_COL(ctx));
    LLVMBuildBr(ctx->builder, cont_bb);

    LLVMPositionBuilderAtEnd(ctx->builder, cont_bb);
}

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

/* Append `suffix_data` (i8*, length `suffix_len` i32) to the string stored at
   `str_alloca` (an alloca holding an LsString struct), mutating it in-place.
   Grows the buffer when needed:
     cap == 0 (static literal) → malloc new buffer, copy old content + suffix
     cap > 0  (owned)          → realloc if capacity insufficient
   After the call the builder is positioned at the merge/continuation block. */
static void emit_string_append_inline(CodegenContext *ctx,
                                      LLVMValueRef str_alloca,
                                      LLVMValueRef suffix_data,
                                      LLVMValueRef suffix_len)
{
    LLVMTypeRef str_type = ls_string_type(ctx);
    LLVMTypeRef i8_type = LLVMInt8TypeInContext(ctx->context);
    LLVMTypeRef i32_type = LLVMInt32TypeInContext(ctx->context);
    LLVMTypeRef i64_type = LLVMInt64TypeInContext(ctx->context);
    LLVMTypeRef ptr_type = LLVMPointerTypeInContext(ctx->context, 0);
    LLVMValueRef cur_fn = LLVMGetBasicBlockParent(LLVMGetInsertBlock(ctx->builder));

    /* Load current string fields */
    LLVMValueRef str_val = LLVMBuildLoad2(ctx->builder, str_type, str_alloca, "sa.v");
    LLVMValueRef s_data = LLVMBuildExtractValue(ctx->builder, str_val, 0, "sa.data");
    LLVMValueRef s_len = LLVMBuildExtractValue(ctx->builder, str_val, 1, "sa.len");
    LLVMValueRef s_cap = LLVMBuildExtractValue(ctx->builder, str_val, 2, "sa.cap");

    /* new_len = s_len + suffix_len (i32) */
    LLVMValueRef new_len = LLVMBuildAdd(ctx->builder, s_len, suffix_len, "sa.nlen");

    /* need_grow = (s_cap == 0) || (new_len + 1 > s_cap) */
    LLVMValueRef zero32 = LLVMConstInt(i32_type, 0, 0);
    LLVMValueRef one32 = LLVMConstInt(i32_type, 1, 0);
    LLVMValueRef is_static = LLVMBuildICmp(ctx->builder, LLVMIntEQ, s_cap, zero32, "sa.isst");
    LLVMValueRef need_plus1 = LLVMBuildAdd(ctx->builder, new_len, one32, "sa.np1");
    LLVMValueRef no_space = LLVMBuildICmp(ctx->builder, LLVMIntSGT, need_plus1, s_cap, "sa.nospc");
    LLVMValueRef need_grow = LLVMBuildOr(ctx->builder, is_static, no_space, "sa.grow");

    int id = g_block_counter++;
    char grow_name[32], static_name[32], owned_name[32], mg_name[32], cont_name[32];
    snprintf(grow_name, sizeof(grow_name), "sa.grow%d", id);
    snprintf(static_name, sizeof(static_name), "sa.static%d", id);
    snprintf(owned_name, sizeof(owned_name), "sa.owned%d", id);
    snprintf(mg_name, sizeof(mg_name), "sa.mg%d", id);
    snprintf(cont_name, sizeof(cont_name), "sa.cont%d", id);

    LLVMBasicBlockRef grow_bb = LLVMAppendBasicBlockInContext(ctx->context, cur_fn, grow_name);
    LLVMBasicBlockRef static_bb = LLVMAppendBasicBlockInContext(ctx->context, cur_fn, static_name);
    LLVMBasicBlockRef owned_bb = LLVMAppendBasicBlockInContext(ctx->context, cur_fn, owned_name);
    LLVMBasicBlockRef mg_bb = LLVMAppendBasicBlockInContext(ctx->context, cur_fn, mg_name);
    LLVMBasicBlockRef cont_bb = LLVMAppendBasicBlockInContext(ctx->context, cur_fn, cont_name);

    LLVMBuildCondBr(ctx->builder, need_grow, grow_bb, cont_bb);

    /* ---- grow_bb: compute new capacity ---- */
    LLVMPositionBuilderAtEnd(ctx->builder, grow_bb);
    {
        /* new_cap = max(new_len + 1, max(s_cap * 2, LS_MIN_STR_CAP)) */
        LLVMValueRef two32 = LLVMConstInt(i32_type, 2, 0);
        LLVMValueRef min_cap32 = LLVMConstInt(i32_type, LS_MIN_STR_CAP, 0);
        LLVMValueRef dbl_cap = LLVMBuildMul(ctx->builder, s_cap, two32, "sa.dbl");
        LLVMValueRef use_min = LLVMBuildICmp(ctx->builder, LLVMIntULT, dbl_cap, min_cap32, "sa.um");
        LLVMValueRef floor_cap = LLVMBuildSelect(ctx->builder, use_min, min_cap32, dbl_cap, "sa.flr");
        LLVMValueRef use_need = LLVMBuildICmp(ctx->builder, LLVMIntSGT, need_plus1, floor_cap, "sa.un");
        LLVMValueRef new_cap = LLVMBuildSelect(ctx->builder, use_need, need_plus1, floor_cap, "sa.nc");

        LLVMBuildCondBr(ctx->builder, is_static, static_bb, owned_bb);

        /* static_bb: malloc(new_cap) then memcpy old content */
        LLVMPositionBuilderAtEnd(ctx->builder, static_bb);
        {
            LLVMValueRef bytes_s = LLVMBuildZExt(ctx->builder, new_cap, i64_type, "sa.bs");
            LLVMValueRef nd_s = cg_emit_alloc(ctx, bytes_s, "string.append",
                                               CG_LINE(ctx), CG_COL(ctx));
            /* memcpy old content (len bytes — no null yet) */
            LLVMValueRef old_len64 = LLVMBuildZExt(ctx->builder, s_len, i64_type, "sa.ol64");
            LLVMValueRef memcpy_fn = LLVMGetNamedFunction(ctx->module, "memcpy");
            LLVMTypeRef memcpy_ft = LLVMGlobalGetValueType(memcpy_fn);
            LLVMValueRef mc_args[3] = {nd_s, s_data, old_len64};
            LLVMBuildCall2(ctx->builder, memcpy_ft, memcpy_fn, mc_args, 3, "");
            /* Update str_alloca: {nd_s, s_len, new_cap} */
            LLVMValueRef upd = LLVMBuildLoad2(ctx->builder, str_type, str_alloca, "sa.us");
            upd = LLVMBuildInsertValue(ctx->builder, upd, nd_s, 0, "sa.us0");
            upd = LLVMBuildInsertValue(ctx->builder, upd, new_cap, 2, "sa.us2");
            LLVMBuildStore(ctx->builder, upd, str_alloca);
#if CG_DEBUG
            {
                LLVMValueRef da[3] = {s_cap, new_cap, new_len};
                cg_emit_debug_printf(ctx,
                                     "[cg] str.append static -> owned  old_cap=%d new_cap=%d new_len=%d\n", da, 3);
            }
#endif
        }
        LLVMBuildBr(ctx->builder, mg_bb);

        /* owned_bb: realloc(data, new_cap) */
        LLVMPositionBuilderAtEnd(ctx->builder, owned_bb);
        {
            LLVMValueRef bytes_o = LLVMBuildZExt(ctx->builder, new_cap, i64_type, "sa.bo");
            LLVMValueRef realloc_fn = LLVMGetNamedFunction(ctx->module, "realloc");
            LLVMTypeRef realloc_ft = LLVMGlobalGetValueType(realloc_fn);
            LLVMValueRef ra_args[2] = {s_data, bytes_o};
            LLVMValueRef nd_o = LLVMBuildCall2(ctx->builder, realloc_ft, realloc_fn,
                                               ra_args, 2, "sa.ndo");
            LLVMValueRef upd = LLVMBuildLoad2(ctx->builder, str_type, str_alloca, "sa.uo");
            upd = LLVMBuildInsertValue(ctx->builder, upd, nd_o, 0, "sa.uo0");
            upd = LLVMBuildInsertValue(ctx->builder, upd, new_cap, 2, "sa.uo2");
            LLVMBuildStore(ctx->builder, upd, str_alloca);
#if CG_DEBUG
            {
                LLVMValueRef da[3] = {s_cap, new_cap, new_len};
                cg_emit_debug_printf(ctx,
                                     "[cg] str.append realloc  old_cap=%d new_cap=%d new_len=%d\n", da, 3);
            }
#endif
        }
        LLVMBuildBr(ctx->builder, mg_bb);
    }

    /* mg_bb: merge grow paths → fall through to cont_bb */
    LLVMPositionBuilderAtEnd(ctx->builder, mg_bb);
    LLVMBuildBr(ctx->builder, cont_bb);

    /* cont_bb: perform the actual append */
    LLVMPositionBuilderAtEnd(ctx->builder, cont_bb);
    {
        /* Reload after potential realloc */
        LLVMValueRef cur = LLVMBuildLoad2(ctx->builder, str_type, str_alloca, "sa.cur");
        LLVMValueRef cur_data = LLVMBuildExtractValue(ctx->builder, cur, 0, "sa.cd");
        LLVMValueRef cur_len = LLVMBuildExtractValue(ctx->builder, cur, 1, "sa.cl");

        /* memcpy(cur_data + cur_len, suffix_data, suffix_len) */
        LLVMValueRef off64 = LLVMBuildZExt(ctx->builder, cur_len, i64_type, "sa.off");
        LLVMValueRef dst = LLVMBuildGEP2(ctx->builder, i8_type, cur_data, &off64, 1, "sa.dst");
        LLVMValueRef suf64 = LLVMBuildZExt(ctx->builder, suffix_len, i64_type, "sa.sl64");
        LLVMValueRef memcpy_fn = LLVMGetNamedFunction(ctx->module, "memcpy");
        LLVMTypeRef memcpy_ft = LLVMGlobalGetValueType(memcpy_fn);
        LLVMValueRef mc2[3] = {dst, suffix_data, suf64};
        LLVMBuildCall2(ctx->builder, memcpy_ft, memcpy_fn, mc2, 3, "");

        /* cur_data[cur_len + suffix_len] = '\0' */
        LLVMValueRef new_len32 = LLVMBuildAdd(ctx->builder, cur_len, suffix_len, "sa.nl2");
        LLVMValueRef nl64 = LLVMBuildZExt(ctx->builder, new_len32, i64_type, "sa.nl64");
        LLVMValueRef null_ptr = LLVMBuildGEP2(ctx->builder, i8_type, cur_data, &nl64, 1, "sa.np");
        LLVMBuildStore(ctx->builder, LLVMConstInt(i8_type, 0, 0), null_ptr);

        /* Update len in str_alloca */
        LLVMValueRef upd = LLVMBuildLoad2(ctx->builder, str_type, str_alloca, "sa.ul");
        upd = LLVMBuildInsertValue(ctx->builder, upd, new_len32, 1, "sa.ul1");
        LLVMBuildStore(ctx->builder, upd, str_alloca);

        (void)ptr_type; /* suppress unused warning */
    }
}

/* Clone a single LsString VALUE (not alloca).
   If cap > 0 (owned): malloc + memcpy → returns new LsString with independent data.
   If cap == 0 (static literal): returned as-is (safe — we never free static data).
   If cap == -1 (moved marker): returned as-is (shouldn't happen in a healthy vec).
   After this call the builder is at a valid continuation block. */
static LLVMValueRef emit_string_clone_val(CodegenContext *ctx, LLVMValueRef str_val)
{
    LLVMTypeRef str_type = ls_string_type(ctx);
    LLVMTypeRef i32_type = LLVMInt32TypeInContext(ctx->context);
    LLVMTypeRef i64_type = LLVMInt64TypeInContext(ctx->context);
    LLVMValueRef cur_fn = LLVMGetBasicBlockParent(LLVMGetInsertBlock(ctx->builder));

    LLVMValueRef data = LLVMBuildExtractValue(ctx->builder, str_val, 0, "sc.data");
    LLVMValueRef len = LLVMBuildExtractValue(ctx->builder, str_val, 1, "sc.len");
    LLVMValueRef cap = LLVMBuildExtractValue(ctx->builder, str_val, 2, "sc.cap");

    LLVMValueRef zero32 = LLVMConstInt(i32_type, 0, 0);
    LLVMValueRef is_owned = LLVMBuildICmp(ctx->builder, LLVMIntSGT, cap, zero32, "sc.owned");

    /* Allocate a result slot in the function entry block */
    LLVMBuilderRef tb = LLVMCreateBuilderInContext(ctx->context);
    LLVMBasicBlockRef entry_bb = LLVMGetEntryBasicBlock(cur_fn);
    LLVMValueRef fi = LLVMGetFirstInstruction(entry_bb);
    if (fi)
        LLVMPositionBuilderBefore(tb, fi);
    else
        LLVMPositionBuilderAtEnd(tb, entry_bb);
    LLVMValueRef res_alloca = LLVMBuildAlloca(tb, str_type, "sc.res");
    LLVMDisposeBuilder(tb);

    /* Default: keep original (handles static / moved strings) */
    LLVMBuildStore(ctx->builder, str_val, res_alloca);

    int id = g_block_counter++;
    char copy_name[32], skip_name[32], merge_name[32];
    snprintf(copy_name, sizeof(copy_name), "sc.copy%d", id);
    snprintf(skip_name, sizeof(skip_name), "sc.skip%d", id);
    snprintf(merge_name, sizeof(merge_name), "sc.merge%d", id);
    LLVMBasicBlockRef copy_bb = LLVMAppendBasicBlockInContext(ctx->context, cur_fn, copy_name);
    LLVMBasicBlockRef skip_bb = LLVMAppendBasicBlockInContext(ctx->context, cur_fn, skip_name);
    LLVMBasicBlockRef merge_bb = LLVMAppendBasicBlockInContext(ctx->context, cur_fn, merge_name);

    LLVMBuildCondBr(ctx->builder, is_owned, copy_bb, skip_bb);

    /* copy_bb: malloc(max(len+1, LS_MIN_STR_CAP)) + memcpy */
    LLVMPositionBuilderAtEnd(ctx->builder, copy_bb);
    {
        LLVMValueRef one32 = LLVMConstInt(i32_type, 1, 0);
        LLVMValueRef need = LLVMBuildAdd(ctx->builder, len, one32, "sc.need");
        LLVMValueRef min_cap = LLVMConstInt(i32_type, LS_MIN_STR_CAP, 0);
        LLVMValueRef use_min = LLVMBuildICmp(ctx->builder, LLVMIntULT, need, min_cap, "sc.usem");
        LLVMValueRef new_cap = LLVMBuildSelect(ctx->builder, use_min, min_cap, need, "sc.nc");
        LLVMValueRef bytes = LLVMBuildZExt(ctx->builder, new_cap, i64_type, "sc.bytes");

        LLVMValueRef new_data = cg_emit_alloc(ctx, bytes, "string.clone",
                                              CG_LINE(ctx), CG_COL(ctx));

        LLVMValueRef copy_len = LLVMBuildZExt(ctx->builder, need, i64_type, "sc.clen");
        LLVMValueRef memcpy_fn = LLVMGetNamedFunction(ctx->module, "memcpy");
        LLVMTypeRef memcpy_ft = LLVMGlobalGetValueType(memcpy_fn);
        LLVMValueRef mc_args[3] = {new_data, data, copy_len};
        LLVMBuildCall2(ctx->builder, memcpy_ft, memcpy_fn, mc_args, 3, "");

        /* Build new LsString{new_data, len, new_cap} */
        LLVMValueRef undef = LLVMGetUndef(str_type);
        LLVMValueRef ns = LLVMBuildInsertValue(ctx->builder, undef, new_data, 0, "sc.ns0");
        ns = LLVMBuildInsertValue(ctx->builder, ns, len, 1, "sc.ns1");
        ns = LLVMBuildInsertValue(ctx->builder, ns, new_cap, 2, "sc.ns2");
        LLVMBuildStore(ctx->builder, ns, res_alloca);
#if CG_DEBUG
        {
            LLVMValueRef dbg_args[3] = {new_cap, len, new_data};
            cg_emit_debug_printf(ctx,
                                 "[cg] str.clone  cap=%d len=%d ptr=%p\n", dbg_args, 3);
        }
#endif
    }
    LLVMBuildBr(ctx->builder, merge_bb);

    /* skip_bb: static string, no copy needed */
    LLVMPositionBuilderAtEnd(ctx->builder, skip_bb);
    LLVMBuildBr(ctx->builder, merge_bb);

    LLVMPositionBuilderAtEnd(ctx->builder, merge_bb);
    return LLVMBuildLoad2(ctx->builder, str_type, res_alloca, "sc.r");
}

/* Clone a struct VALUE by deep-copying all owned string fields.
   Non-string fields are copied by value (cheap, correct for int/bool/f64/pointer).
   Nested struct-with-drop fields are recursively cloned.
   Returns a new struct value with independently owned data. */
static LLVMValueRef emit_struct_clone_val(CodegenContext *ctx,
                                          LLVMValueRef struct_val,
                                          LLVMTypeRef llvm_struct_type,
                                          Type *struct_type)
{
    if (struct_type == NULL || struct_type->kind != TYPE_STRUCT)
        return struct_val;
    if (!struct_type->as.strukt.has_drop)
        return struct_val; /* no owned resources — plain value copy is fine */

#if CG_DEBUG
    {
        char dbg_fmt[128];
        snprintf(dbg_fmt, sizeof(dbg_fmt),
                 "[cg] struct.clone  type=%s\n",
                 struct_type->as.strukt.name ? struct_type->as.strukt.name : "?");
        cg_emit_debug_printf(ctx, dbg_fmt, NULL, 0);
    }
#endif

    LLVMValueRef result = struct_val; /* start as shallow copy */

    for (int fi = 0; fi < struct_type->as.strukt.field_count; fi++)
    {
        Type *ft = struct_type->as.strukt.fields[fi].type;
        if (ft == NULL)
            continue;

        if (ft->kind == TYPE_STRING)
        {
            LLVMValueRef field_val = LLVMBuildExtractValue(ctx->builder, result,
                                                           (unsigned)fi, "sc.fld");
            LLVMValueRef cloned_str = emit_string_clone_val(ctx, field_val);
            result = LLVMBuildInsertValue(ctx->builder, result, cloned_str,
                                          (unsigned)fi, "sc.ins");
        }
        else if (ft->kind == TYPE_STRUCT && ft->as.strukt.has_drop)
        {
            LLVMTypeRef ft_llvm = type_to_llvm(ctx, ft);
            LLVMValueRef fld_val = LLVMBuildExtractValue(ctx->builder, result,
                                                         (unsigned)fi, "sc.sfld");
            LLVMValueRef cloned_s = emit_struct_clone_val(ctx, fld_val, ft_llvm, ft);
            result = LLVMBuildInsertValue(ctx->builder, result, cloned_s,
                                          (unsigned)fi, "sc.sins");
        }
    }

    (void)llvm_struct_type; /* used implicitly through extractValue field indices */
    return result;
}

/* emit_array_clone_val — deep-copy each element that owns heap data.
   For arrays with trivial element types, the value is returned as-is (no alloc).
   For arrays with string/has_drop struct elements, each element is cloned via
   ExtractValue + clone + InsertValue. */
static LLVMValueRef emit_array_clone_val(CodegenContext *ctx, LLVMValueRef arr_val,
                                         LLVMTypeRef llvm_arr_type, Type *arr_type)
{
    if (arr_type == NULL || arr_type->kind != TYPE_ARRAY)
        return arr_val;

    Type *elem_type = arr_type->as.array.elem;
    if (elem_type == NULL)
        return arr_val;

    bool elem_needs_clone = (elem_type->kind == TYPE_STRING) ||
                            (elem_type->kind == TYPE_STRUCT && elem_type->as.strukt.has_drop);
    if (!elem_needs_clone)
        return arr_val; /* trivial elements — value copy is fine */

#if CG_DEBUG
    {
        char dbg_fmt[64];
        snprintf(dbg_fmt, sizeof(dbg_fmt),
                 "[cg] arr.clone  size=%d\n", arr_type->as.array.size);
        cg_emit_debug_printf(ctx, dbg_fmt, NULL, 0);
    }
#endif

    LLVMTypeRef elem_llvm = type_to_llvm(ctx, elem_type);
    LLVMValueRef result = arr_val;
    int n = arr_type->as.array.size;

    for (int i = 0; i < n; i++)
    {
        LLVMValueRef elem = LLVMBuildExtractValue(ctx->builder, result,
                                                  (unsigned)i, "ac.elem");
        LLVMValueRef cloned;
        if (elem_type->kind == TYPE_STRING)
            cloned = emit_string_clone_val(ctx, elem);
        else
            cloned = emit_struct_clone_val(ctx, elem, elem_llvm, elem_type);
        result = LLVMBuildInsertValue(ctx->builder, result, cloned,
                                      (unsigned)i, "ac.ins");
    }

    (void)llvm_arr_type;
    return result;
}

/* emit_vec_clone_val — produce an independent deep copy of a vec value.
   The original vec is not freed or modified.
   Algorithm:
     new_cap  = original cap (or LS_MIN_VEC_CAP if 0)
     new_data = malloc(new_cap * sizeof(T))
     for i in 0..len: new_data[i] = clone(original_data[i])
     return LsVec{ new_data, original_len, new_cap }
   For trivial element types (no heap ownership) we just memcpy the data. */
static LLVMValueRef emit_vec_clone_val(CodegenContext *ctx, LLVMValueRef vec_val,
                                       Type *elem_type)
{
    if (elem_type == NULL)
        return vec_val;

    LLVMTypeRef vec_t = ls_vec_type(ctx);
    LLVMTypeRef elem_llvm = type_to_llvm(ctx, elem_type);
    LLVMTypeRef i32_t = LLVMInt32TypeInContext(ctx->context);
    LLVMTypeRef i64_t = LLVMInt64TypeInContext(ctx->context);

    /* Extract original fields */
    LLVMValueRef orig_data = LLVMBuildExtractValue(ctx->builder, vec_val, 0, "vc.data");
    LLVMValueRef orig_len = LLVMBuildExtractValue(ctx->builder, vec_val, 1, "vc.len");
    LLVMValueRef orig_cap = LLVMBuildExtractValue(ctx->builder, vec_val, 2, "vc.cap");

    /* elem_size = sizeof(T) */
    LLVMValueRef elem_size = LLVMSizeOf(elem_llvm); /* i64 */

#if CG_DEBUG
    {
        LLVMValueRef dbg_args[2] = {orig_len, orig_cap};
        cg_emit_debug_printf(ctx, "[cg] vec.clone  len=%d cap=%d\n", dbg_args, 2);
    }
#endif

    /* Use cap for allocation size; if cap == 0 use LS_MIN_VEC_CAP (8) */
    LLVMValueRef cap64 = LLVMBuildSExt(ctx->builder, orig_cap, i64_t, "vc.cap64");
    LLVMValueRef min_cap = LLVMConstInt(i64_t, 8, 0);
    LLVMValueRef cap_ok = LLVMBuildICmp(ctx->builder, LLVMIntSGT, cap64,
                                        LLVMConstInt(i64_t, 0, 0), "vc.capok");
    LLVMValueRef alloc_cap = LLVMBuildSelect(ctx->builder, cap_ok, cap64, min_cap, "vc.alcap");

    /* bytes = alloc_cap * elem_size */
    LLVMValueRef bytes = LLVMBuildMul(ctx->builder, alloc_cap, elem_size, "vc.bytes");

    /* new_data = malloc(bytes) — vec clone for collection element copying */
    LLVMValueRef new_data = cg_emit_alloc(ctx, bytes, "vec.clone",
                                          CG_LINE(ctx), CG_COL(ctx));

    bool elem_needs_clone = (elem_type->kind == TYPE_STRING) ||
                            (elem_type->kind == TYPE_STRUCT && elem_type->as.strukt.has_drop);

    if (!elem_needs_clone)
    {
        /* Trivial elements: memcpy the whole data region */
        LLVMValueRef len64 = LLVMBuildSExt(ctx->builder, orig_len, i64_t, "vc.len64");
        LLVMValueRef copy_bytes = LLVMBuildMul(ctx->builder, len64, elem_size, "vc.cpbytes");
        LLVMValueRef memcpy_fn = LLVMGetNamedFunction(ctx->module, "memcpy");
        LLVMTypeRef memcpy_type = LLVMGlobalGetValueType(memcpy_fn);
        LLVMValueRef mcargs[3] = {new_data, orig_data, copy_bytes};
        LLVMBuildCall2(ctx->builder, memcpy_type, memcpy_fn, mcargs, 3, "");
    }
    else
    {
        /* has_drop elements: loop and clone each one */
        LLVMValueRef cur_fn = LLVMGetBasicBlockParent(LLVMGetInsertBlock(ctx->builder));
        LLVMBasicBlockRef loop_cond = LLVMAppendBasicBlockInContext(ctx->context, cur_fn, "vc.cond");
        LLVMBasicBlockRef loop_body = LLVMAppendBasicBlockInContext(ctx->context, cur_fn, "vc.body");
        LLVMBasicBlockRef loop_end = LLVMAppendBasicBlockInContext(ctx->context, cur_fn, "vc.end");

        /* alloca for loop counter */
        LLVMBuilderRef entry_builder = LLVMCreateBuilderInContext(ctx->context);
        LLVMBasicBlockRef entry_bb = LLVMGetEntryBasicBlock(cur_fn);
        LLVMValueRef first_instr = LLVMGetFirstInstruction(entry_bb);
        if (first_instr)
            LLVMPositionBuilderBefore(entry_builder, first_instr);
        else
            LLVMPositionBuilderAtEnd(entry_builder, entry_bb);
        LLVMValueRef idx_alloca = LLVMBuildAlloca(entry_builder, i32_t, "vc.i");
        LLVMDisposeBuilder(entry_builder);

        LLVMBuildStore(ctx->builder, LLVMConstInt(i32_t, 0, 0), idx_alloca);
        LLVMBuildBr(ctx->builder, loop_cond);

        /* cond: i < len */
        LLVMPositionBuilderAtEnd(ctx->builder, loop_cond);
        LLVMValueRef i_val = LLVMBuildLoad2(ctx->builder, i32_t, idx_alloca, "vc.i");
        LLVMValueRef cmp = LLVMBuildICmp(ctx->builder, LLVMIntSLT, i_val, orig_len, "vc.cmp");
        LLVMBuildCondBr(ctx->builder, cmp, loop_body, loop_end);

        /* body: clone orig_data[i] → new_data[i] */
        LLVMPositionBuilderAtEnd(ctx->builder, loop_body);
        LLVMValueRef i64_val = LLVMBuildSExt(ctx->builder, i_val, i64_t, "vc.i64");
        LLVMValueRef src_gep = LLVMBuildGEP2(ctx->builder, elem_llvm, orig_data,
                                             &i64_val, 1, "vc.srcp");
        LLVMValueRef dst_gep = LLVMBuildGEP2(ctx->builder, elem_llvm, new_data,
                                             &i64_val, 1, "vc.dstp");
        LLVMValueRef src_elem = LLVMBuildLoad2(ctx->builder, elem_llvm, src_gep, "vc.se");
        LLVMValueRef cloned;
        if (elem_type->kind == TYPE_STRING)
            cloned = emit_string_clone_val(ctx, src_elem);
        else
            cloned = emit_struct_clone_val(ctx, src_elem, elem_llvm, elem_type);
        LLVMBuildStore(ctx->builder, cloned, dst_gep);
        /* i++ */
        LLVMValueRef next = LLVMBuildAdd(ctx->builder, i_val, LLVMConstInt(i32_t, 1, 0), "vc.ni");
        LLVMBuildStore(ctx->builder, next, idx_alloca);
        LLVMBuildBr(ctx->builder, loop_cond);

        LLVMPositionBuilderAtEnd(ctx->builder, loop_end);
    }

    /* new_cap as i32 */
    LLVMValueRef new_cap32 = LLVMBuildTrunc(ctx->builder, alloc_cap, i32_t, "vc.nc32");

    /* Build result LsVec struct */
    LLVMValueRef result = LLVMGetUndef(vec_t);
    result = LLVMBuildInsertValue(ctx->builder, result, new_data, 0, "vc.r0");
    result = LLVMBuildInsertValue(ctx->builder, result, orig_len, 1, "vc.r1");
    result = LLVMBuildInsertValue(ctx->builder, result, new_cap32, 2, "vc.r2");
    return result;
}

/* ---- Temporary string slot management ---- */

/* Register a dynamic string value in the temp slot list.
   Creates an alloca in the function entry block, stores str_val there,
   and registers the alloca for statement-level cleanup.
   Returns str_val unchanged (the SSA value, not a reload). */
static LLVMValueRef cg_push_temp_string(CodegenContext *ctx, LLVMValueRef str_val)
{
    if (ctx->current_fn == NULL)
        return str_val; /* global context: no alloca */
    LLVMTypeRef str_type = ls_string_type(ctx);

    /* Create alloca in entry block so it dominates all uses */
    LLVMBasicBlockRef entry = LLVMGetEntryBasicBlock(ctx->current_fn);
    LLVMBuilderRef tmp = LLVMCreateBuilderInContext(ctx->context);
    LLVMValueRef first = LLVMGetFirstInstruction(entry);
    if (first)
        LLVMPositionBuilderBefore(tmp, first);
    else
        LLVMPositionBuilderAtEnd(tmp, entry);
    LLVMValueRef slot = LLVMBuildAlloca(tmp, str_type, "tmp.str");
    LLVMDisposeBuilder(tmp);

    /* Store current value into the slot */
    LLVMBuildStore(ctx->builder, str_val, slot);

    /* Grow and register */
    if (ctx->temp_string_count >= ctx->temp_string_cap)
    {
        ctx->temp_string_cap = GROW_CAPACITY(ctx->temp_string_cap);
        ctx->temp_string_slots = GROW_ARRAY(LLVMValueRef,
                                            ctx->temp_string_slots, ctx->temp_string_cap);
    }
    ctx->temp_string_slots[ctx->temp_string_count++] = slot;
    return str_val;
}

/* Mark the last temp slot (if any created since mark) as moved (cap = -1).
   Used after a dynamic string is stored into a variable: prevents double-free
   when both the temp slot and the variable's alloca are cleaned up. */
static void cg_mark_last_temp_moved(CodegenContext *ctx, int mark, const char *reason)
{
    if (ctx->temp_string_count > mark)
    {
        mark_string_moved(ctx, ctx->temp_string_slots[ctx->temp_string_count - 1], reason);
    }
}

/* Free temp string slots in [mark, count) and reset count to mark.
   skip_last: if true, skip the last slot (it was moved to a named variable).
   All non-skipped slots are conditionally freed (cap > 0 check). */
static void cg_flush_temps(CodegenContext *ctx, int mark, bool skip_last)
{
    LLVMBasicBlockRef cur = LLVMGetInsertBlock(ctx->builder);
    if (cur && LLVMGetBasicBlockTerminator(cur) != NULL)
    {
        /* Block already terminated (e.g. after a break/continue): skip flush */
        ctx->temp_string_count = mark;
        return;
    }
    int end = ctx->temp_string_count;
    if (skip_last && end > mark)
        end--;
    for (int i = mark; i < end; i++)
    {
        emit_string_free(ctx, ctx->temp_string_slots[i]);
    }
    ctx->temp_string_count = mark;
}

/* ---- String Move Semantics ---- */

/* Check if an expression is a variable (identifier) of string type.
   If so, return the symbol; otherwise return NULL. */
static CgSymbol *get_string_var_symbol(AstNode *expr, CodegenContext *ctx)
{
    if (expr == NULL)
        return NULL;
    if (expr->kind != AST_IDENT)
        return NULL;
    Type *t = expr->resolved_type;
    if (t == NULL || t->kind != TYPE_STRING)
        return NULL;
    return cg_scope_resolve(ctx->current_scope, expr->as.ident.name);
}

/* Emit string move: copy struct from src to dst, then mark src as moved.
   dst_alloca: destination variable's alloca
   src_alloca: source variable's alloca
   dst and src are LsString structs (by value). */
static void emit_string_move(CodegenContext *ctx, LLVMValueRef dst_alloca,
                             LLVMValueRef src_alloca)
{
    LLVMTypeRef str_type = ls_string_type(ctx);

    /* Load source string struct */
    LLVMValueRef src_val = LLVMBuildLoad2(ctx->builder, str_type, src_alloca, "move.src");

    /* Store to destination (struct copy) */
    LLVMBuildStore(ctx->builder, src_val, dst_alloca);

    /* Mark source as moved (cap = -1) */
    mark_string_moved(ctx, src_alloca, "assign: src moved to dst");
}

/* Returns true when `node` is a vec(string)[i] access — a candidate for auto-borrow.
   In read-only contexts (print, string methods, comparisons, f-string, concatenation)
   the vec retains ownership so the element can be borrowed without cloning. */
static bool is_vec_string_index(AstNode *node)
{
    if (!node || node->kind != AST_INDEX)
        return false;
    Type *obj_t = node->as.index_expr.object
                      ? node->as.index_expr.object->resolved_type
                      : NULL;
    if (!obj_t || obj_t->kind != TYPE_VECTOR)
        return false;
    Type *elem = obj_t->as.vec.elem;
    return elem && elem->kind == TYPE_STRING;
}

/* Check if an expression produces a dynamic (owned) string that needs cleanup.
   This identifies temporary strings that aren't assigned to variables. */
static bool expr_produces_dynamic_string(AstNode *node)
{
    if (node == NULL)
        return false;

    /* String method calls that allocate new strings */
    if (node->kind == AST_CALL &&
        node->as.call.callee->kind == AST_FIELD)
    {
        AstNode *obj = node->as.call.callee->as.field_access.object;
        if (obj && obj->resolved_type && obj->resolved_type->kind == TYPE_STRING)
        {
            const char *method = node->as.call.callee->as.field_access.field;
            /* Methods that allocate new strings: upper, lower, substr, trim, replace, copy */
            if (strcmp(method, "upper") == 0 ||
                strcmp(method, "lower") == 0 ||
                strcmp(method, "substr") == 0 ||
                strcmp(method, "trim") == 0 ||
                strcmp(method, "replace") == 0 ||
                strcmp(method, "copy") == 0)
            {
                return true;
            }
        }
    }

    /* String concatenation s1 + s2 */
    if (node->kind == AST_BINARY)
    {
        Type *lt = node->as.binary.left->resolved_type;
        Type *rt = node->as.binary.right->resolved_type;
        if (lt && rt &&
            lt->kind == TYPE_STRING &&
            rt->kind == TYPE_STRING)
        {
            return true;
        }
    }

    /* Format string f"..." */
    if (node->kind == AST_FORMAT_STRING)
    {
        return true;
    }

    /* Nested call that may produce dynamic string */
    if (node->kind == AST_CALL)
    {
        Type *rt = node->resolved_type;
        if (rt && rt->kind == TYPE_STRING)
        {
            /* Check if it's not a string method (already handled above) */
            if (node->as.call.callee->kind == AST_IDENT)
            {
                return true;
            }
        }
    }

    return false;
}

/* Forward declaration for struct drop */
static LLVMBasicBlockRef emit_struct_drop_separate(CodegenContext *ctx, LLVMValueRef drop_ptr,
                                                   Type *struct_type, LLVMValueRef moved_flag);

/* Emit cleanup IR for all dynamic string locals in the current scope.
   Uses LIFO order (reverse traversal) to match C++ destructor semantics. */
/* Emit cleanup for all owned string/struct-with-drop symbols in the current scope.
   Uses LIFO order (reverse declaration). Each cleanup function (emit_string_free /
   emit_struct_drop_cond) branches from the current block to separate cleanup blocks
   and leaves the builder at the continuation block, ready for the next cleanup or
   the caller's next instruction.
   Skips borrowed symbols (is_borrowed=true) and trivial types.
   Does nothing if the current block is already terminated. */
static void emit_scope_cleanup(CodegenContext *ctx)
{
    CgScope *scope = ctx->current_scope;
    if (scope == NULL)
        return;

    LLVMBasicBlockRef cur_bb = LLVMGetInsertBlock(ctx->builder);
    if (cur_bb == NULL)
        return;
    if (LLVMGetBasicBlockTerminator(cur_bb) != NULL)
        return;

    /* LIFO order: clean up in reverse declaration order.
       Each emit_string_free / emit_struct_drop_cond branches from the current
       block and leaves the builder at the cont_bb of that cleanup step.
       Subsequent calls chain naturally. */
    for (int i = scope->count - 1; i >= 0; i--)
    {
        CgSymbol *sym = &scope->symbols[i];
        if (sym->type == NULL || sym->is_borrowed)
            continue;

        if (sym->type->kind == TYPE_STRING)
        {
#if CG_DEBUG
            {
                char dbg_fmt[128];
                snprintf(dbg_fmt, sizeof(dbg_fmt),
                         "[cg] scope.drop  var=%-12s type=string\n",
                         sym->name ? sym->name : "?");
                cg_emit_debug_printf(ctx, dbg_fmt, NULL, 0);
            }
#endif
            emit_string_free_named(ctx, sym->value, sym->name);
        }
        else if (sym->type->kind == TYPE_STRUCT && sym->type->as.strukt.has_drop)
        {
            emit_struct_drop_cond(ctx, sym->value, sym->type, sym->moved_flag);
        }
        else if (sym->type->kind == TYPE_ENUM && sym->type->as.enom.has_drop)
        {
            /* Owned enum value: call the auto-generated EnumName.__drop on its alloca.
               sym->value is &enum (alloca pointer). */
            emit_enum_drop(ctx, sym->value, sym->type);
        }
        else if (sym->type->kind == TYPE_MAP)
        {
            /* map drop: traverse all nodes, free keys/vals/nodes, free bucket array */
            char msuffix[64];
            map_type_id(sym->type->as.map.key, sym->type->as.map.val, msuffix, sizeof(msuffix));
            char mnm[96];
            snprintf(mnm, sizeof(mnm), "__ls_map_%s_drop", msuffix);
            emit_map_helpers_for(ctx, sym->type->as.map.key, sym->type->as.map.val);
            LLVMValueRef mfn = LLVMGetNamedFunction(ctx->module, mnm);
            if (mfn)
            {
                LLVMTypeRef mft = LLVMGlobalGetValueType(mfn);
                LLVMBuildCall2(ctx->builder, mft, mfn, &sym->value, 1, "");
            }
        }
        /* Trivial types (int, float, bool, vec, etc.) need no cleanup here. */
    }
    /* After this returns, the builder is positioned at the last cont_bb,
       ready for the caller to add the next instruction (e.g. LLVMBuildBr). */
}

/* Emit cleanup for all scopes from current up to (not including) stop.
   Creates a single cleanup block and emits all cleanups inline. */
/* Emit cleanup for all dynamic string locals up to (but not including) the given scope.
   Uses LIFO order (reverse traversal) to match C++ destructor semantics.
   Creates a single cleanup block with inline cleanups.
   Skips the variable whose alloca == skip_alloca (used for return value to avoid double-free).
   Uses LLVMValueRef (stable LLVM object) instead of CgSymbol* to avoid dangling pointer
   risk if the scope's symbol array is reallocated between resolve and use. */
static void emit_cleanup_to(CodegenContext *ctx, CgScope *stop, LLVMValueRef skip_alloca)
{
    LLVMBasicBlockRef cur_bb = LLVMGetInsertBlock(ctx->builder);
    if (cur_bb == NULL)
        return;
    if (LLVMGetBasicBlockTerminator(cur_bb) != NULL)
        return;

    LLVMValueRef cur_fn = LLVMGetBasicBlockParent(cur_bb);

    /* Count how many cleanups we need (excluding skip_alloca).
       Compare sym->value (LLVMValueRef alloca) — stable across realloc of the CgScope array.
       This correctly distinguishes same-name variables in different scopes (shadowing). */
    int count = 0;
    for (CgScope *s = ctx->current_scope; s != NULL && s != stop; s = s->parent)
    {
        for (int i = s->count - 1; i >= 0; i--)
        {
            CgSymbol *sym = &s->symbols[i];
            if (sym->type == NULL)
                continue;
            if (skip_alloca != NULL && sym->value == skip_alloca)
                continue;
            /* Skip global variables — they are cleaned up by __ls_global_cleanup,
               not by local scope cleanup inside functions. */
            if (sym->value && LLVMIsAGlobalVariable(sym->value))
                continue;
            /* Skip borrowed symbols (vec/struct params passed by ref) — caller owns the data */
            if (sym->is_borrowed)
                continue;
            if (sym->type->kind == TYPE_STRING ||
                (sym->type->kind == TYPE_STRUCT && sym->type->as.strukt.has_drop) ||
                (sym->type->kind == TYPE_ENUM && sym->type->as.enom.has_drop))
            {
                count++;
            }
            else if (sym->type->kind == TYPE_ARRAY && sym->type->as.array.elem)
            {
                Type *elem = sym->type->as.array.elem;
                /* BUG #3 fix: array(string,N) and array(struct-with-drop,N) both need cleanup */
                if (elem->kind == TYPE_STRING ||
                    (elem->kind == TYPE_STRUCT && elem->as.strukt.has_drop))
                {
                    count += (int)sym->type->as.array.size;
                }
            }
            else if (sym->type->kind == TYPE_VECTOR)
            {
                count++; /* vec cleanup: drop all elements + free data (runtime conditional) */
            }
            else if (sym->type->kind == TYPE_MAP)
            {
                count++; /* map cleanup: traverse all nodes + free buckets */
            }
        }
    }

    if (count == 0)
        return;

    /* Create single cleanup block and branch to it */
    LLVMBasicBlockRef cleanup_bb = LLVMAppendBasicBlockInContext(
        ctx->context, cur_fn, "cleanup");
    LLVMBuildBr(ctx->builder, cleanup_bb);
    LLVMPositionBuilderAtEnd(ctx->builder, cleanup_bb);

    /* Emit all cleanups inline in the cleanup block.
       For each cleanup, emit the free/drop logic directly.
       After the last cleanup, the block ends (will be branched to from entry). */
    int idx = 0;
    for (CgScope *s = ctx->current_scope; s != NULL && s != stop; s = s->parent)
    {
        for (int i = s->count - 1; i >= 0; i--)
        {
            CgSymbol *sym = &s->symbols[i];
            if (sym->type == NULL)
                continue;
            if (skip_alloca != NULL && sym->value == skip_alloca)
                continue;
            /* Skip global variables — handled by __ls_global_cleanup */
            if (sym->value && LLVMIsAGlobalVariable(sym->value))
                continue;
            /* Skip borrowed symbols (vec params, etc.) — caller owns the data */
            if (sym->is_borrowed)
                continue;

            if (sym->type->kind == TYPE_STRING)
            {
                /* String cleanup: if cap > 0, call free(data) */
#if CG_DEBUG
                {
                    char dbg_fmt[128];
                    snprintf(dbg_fmt, sizeof(dbg_fmt),
                             "[cg] scope.drop  var=%-12s type=string\n",
                             sym->name ? sym->name : "?");
                    cg_emit_debug_printf(ctx, dbg_fmt, NULL, 0);
                }
#endif
                LLVMTypeRef str_type = ls_string_type(ctx);
                LLVMTypeRef i32_type = LLVMInt32TypeInContext(ctx->context);
                LLVMValueRef str_val = LLVMBuildLoad2(ctx->builder, str_type, sym->value, "sf.str");
                LLVMValueRef cap = LLVMBuildExtractValue(ctx->builder, str_val, 2, "sf.cap");
                LLVMValueRef zero = LLVMConstInt(i32_type, 0, 0);
                LLVMValueRef is_owned = LLVMBuildICmp(ctx->builder, LLVMIntSGT, cap, zero, "sf.owned");

                char free_name[32], skip_name[32], cont_name[32];
                snprintf(free_name, sizeof(free_name), "sf.free%d", idx);
                snprintf(skip_name, sizeof(skip_name), "sf.skip%d", idx);
                snprintf(cont_name, sizeof(cont_name), "sf.cont%d", idx);
                LLVMBasicBlockRef free_bb = LLVMAppendBasicBlockInContext(ctx->context, cur_fn, free_name);
                LLVMBasicBlockRef skip_bb = LLVMAppendBasicBlockInContext(ctx->context, cur_fn, skip_name);
                LLVMBasicBlockRef cont_bb = LLVMAppendBasicBlockInContext(ctx->context, cur_fn, cont_name);

                LLVMBuildCondBr(ctx->builder, is_owned, free_bb, skip_bb);

                LLVMPositionBuilderAtEnd(ctx->builder, free_bb);
                LLVMValueRef data = LLVMBuildExtractValue(ctx->builder, str_val, 0, "sf.data");
#if CG_DEBUG
                {
                    LLVMValueRef len_val = LLVMBuildExtractValue(ctx->builder, str_val, 1, "sf.len");
                    char dbg_fmt[160];
                    snprintf(dbg_fmt, sizeof(dbg_fmt),
                             "[cg] str.free   var=%-12s cap=%%d len=%%d ptr=%%p \"%%.*s\"\n",
                             sym->name ? sym->name : "?");
                    LLVMValueRef dbg_args[5] = {cap, len_val, data, len_val, data};
                    cg_emit_debug_printf(ctx, dbg_fmt, dbg_args, 5);
                }
#endif
                cg_emit_free(ctx, data, "string.scope_drop", CG_LINE(ctx), CG_COL(ctx));
                LLVMBuildBr(ctx->builder, cont_bb);

                LLVMPositionBuilderAtEnd(ctx->builder, skip_bb);
#if CG_DEBUG
                {
                    char dbg_fmt[128];
                    snprintf(dbg_fmt, sizeof(dbg_fmt),
                             "[cg] str.skip   var=%-12s cap=%%d  (static or moved)\n",
                             sym->name ? sym->name : "?");
                    LLVMValueRef skip_da[1] = {cap};
                    cg_emit_debug_printf(ctx, dbg_fmt, skip_da, 1);
                }
#endif
                LLVMBuildBr(ctx->builder, cont_bb);

                LLVMPositionBuilderAtEnd(ctx->builder, cont_bb);
                idx++;
            }
            else if (sym->type->kind == TYPE_ARRAY && sym->type->as.array.elem)
            {
                Type *elem_type = sym->type->as.array.elem;
                int arr_size = (int)sym->type->as.array.size;
                LLVMTypeRef arr_type = type_to_llvm(ctx, sym->type);
                LLVMTypeRef i64_type = LLVMInt64TypeInContext(ctx->context);
                LLVMValueRef zero64 = LLVMConstInt(i64_type, 0, 0);

                if (elem_type->kind == TYPE_STRING)
                {
                    /* BUG #3: free each owned string element */
                    for (int ei = 0; ei < arr_size; ei++)
                    {
                        LLVMValueRef eidx = LLVMConstInt(i64_type, (uint64_t)ei, 0);
                        LLVMValueRef eindices[2] = {zero64, eidx};
                        LLVMValueRef elem_ptr = LLVMBuildGEP2(ctx->builder, arr_type,
                                                              sym->value, eindices, 2, "ae.ptr");
                        emit_string_free(ctx, elem_ptr);
                        idx++;
                    }
                }
                else if (elem_type->kind == TYPE_STRUCT && elem_type->as.strukt.has_drop)
                {
                    /* BUG #3: call drop for each struct element with destructor */
                    LLVMValueRef drop_fn = (LLVMValueRef)elem_type->as.strukt.drop_fn;
                    if (drop_fn != NULL)
                    {
                        LLVMTypeRef drop_fn_type = LLVMGlobalGetValueType(drop_fn);
                        LLVMTypeRef elem_llvm = type_to_llvm(ctx, elem_type);
                        for (int ei = 0; ei < arr_size; ei++)
                        {
                            LLVMValueRef eidx = LLVMConstInt(i64_type, (uint64_t)ei, 0);
                            LLVMValueRef eindices[2] = {zero64, eidx};
                            LLVMValueRef elem_ptr = LLVMBuildGEP2(ctx->builder, arr_type,
                                                                  sym->value, eindices, 2,
                                                                  "ae.sptr");
                            (void)elem_llvm; /* GEP uses arr_type, elem_ptr is *ElemType */
#if CG_DEBUG
                            {
                                char dbg_fmt[128];
                                snprintf(dbg_fmt, sizeof(dbg_fmt),
                                         "[cg] arr.elem.drop   type=%s  idx=%d\n",
                                         elem_type->as.strukt.name ? elem_type->as.strukt.name : "?",
                                         ei);
                                cg_emit_debug_printf(ctx, dbg_fmt, NULL, 0);
                            }
#endif
                            LLVMBuildCall2(ctx->builder, drop_fn_type, drop_fn,
                                           &elem_ptr, 1, "");
                            idx++;
                        }
                    }
                }
            }
            else if (sym->type->kind == TYPE_STRUCT && sym->type->as.strukt.has_drop)
            {
#if CG_DEBUG
                {
                    char dbg_fmt[128];
                    snprintf(dbg_fmt, sizeof(dbg_fmt),
                             "[cg] scope.drop  var=%s  type=struct(%s)\n",
                             sym->name ? sym->name : "?",
                             sym->type->as.strukt.name ? sym->type->as.strukt.name : "?");
                    cg_emit_debug_printf(ctx, dbg_fmt, NULL, 0);
                }
#endif
                LLVMValueRef drop_fn = (LLVMValueRef)sym->type->as.strukt.drop_fn;
                if (drop_fn != NULL && sym->moved_flag != NULL)
                {
                    LLVMTypeRef i1_type = LLVMInt1TypeInContext(ctx->context);
                    char skip_name[32], call_name[32];
                    snprintf(skip_name, sizeof(skip_name), "drop.skip%d", idx);
                    snprintf(call_name, sizeof(call_name), "drop.call%d", idx);
                    LLVMBasicBlockRef skip_bb = LLVMAppendBasicBlockInContext(ctx->context, cur_fn, skip_name);
                    LLVMBasicBlockRef call_bb = LLVMAppendBasicBlockInContext(ctx->context, cur_fn, call_name);

                    LLVMValueRef is_moved = LLVMBuildLoad2(ctx->builder, i1_type, sym->moved_flag, "drop.flag");
                    LLVMBuildCondBr(ctx->builder, is_moved, skip_bb, call_bb);

                    LLVMPositionBuilderAtEnd(ctx->builder, call_bb);
                    LLVMTypeRef fn_type = LLVMGlobalGetValueType(drop_fn);
                    LLVMBuildCall2(ctx->builder, fn_type, drop_fn, &sym->value, 1, "");
                    LLVMBuildBr(ctx->builder, skip_bb);

                    LLVMPositionBuilderAtEnd(ctx->builder, skip_bb);
                    idx++;
                }
                else if (drop_fn != NULL)
                {
                    /* drop_fn is always a complete drop (user wrapper or auto-generated).
                       Just call it — no additional inline cleanup needed. */
                    LLVMTypeRef fn_type = LLVMGlobalGetValueType(drop_fn);
                    LLVMBuildCall2(ctx->builder, fn_type, drop_fn, &sym->value, 1, "");
                    idx++;
                }
                else
                {
                    /* Fallback: drop_fn should always be set after Pass 2.5.
                       This branch is dead code for well-formed programs. */
                    emit_struct_drop(ctx, sym->value, sym->type);
                    idx++;
                }
            }
            else if (sym->type->kind == TYPE_VECTOR)
            {
                /* vec(T) cleanup: drop all live elements then free(data) if cap > 0 */
#if CG_DEBUG
                {
                    char dbg_fmt[128];
                    snprintf(dbg_fmt, sizeof(dbg_fmt),
                             "[cg] scope.drop  var=%s  type=vec\n",
                             sym->name ? sym->name : "?");
                    cg_emit_debug_printf(ctx, dbg_fmt, NULL, 0);
                }
#endif
                Type *elem_type = sym->type->as.vec.elem;
                LLVMTypeRef vec_t = ls_vec_type(ctx);
                LLVMTypeRef i32_t = LLVMInt32TypeInContext(ctx->context);
                LLVMTypeRef i64_t = LLVMInt64TypeInContext(ctx->context);
                LLVMTypeRef ptr_t = LLVMPointerTypeInContext(ctx->context, 0);

                LLVMValueRef vec_val = LLVMBuildLoad2(ctx->builder, vec_t, sym->value, "vcd.v");
                LLVMValueRef cap_v = LLVMBuildExtractValue(ctx->builder, vec_val, 2, "vcd.cap");
                LLVMValueRef len_v = LLVMBuildExtractValue(ctx->builder, vec_val, 1, "vcd.len");
                LLVMValueRef data_v = LLVMBuildExtractValue(ctx->builder, vec_val, 0, "vcd.data");

                LLVMValueRef zero32 = LLVMConstInt(i32_t, 0, 0);
                LLVMValueRef has_buf = LLVMBuildICmp(ctx->builder, LLVMIntSGT, cap_v, zero32, "vcd.hasbuf");

                char vfree_name[32], vdone_name[32];
                snprintf(vfree_name, sizeof(vfree_name), "vcd.free%d", idx);
                snprintf(vdone_name, sizeof(vdone_name), "vcd.done%d", idx);
                LLVMBasicBlockRef vfree_bb = LLVMAppendBasicBlockInContext(ctx->context, cur_fn, vfree_name);
                LLVMBasicBlockRef vdone_bb = LLVMAppendBasicBlockInContext(ctx->context, cur_fn, vdone_name);
                LLVMBuildCondBr(ctx->builder, has_buf, vfree_bb, vdone_bb);

                LLVMPositionBuilderAtEnd(ctx->builder, vfree_bb);

                /* Drop each element if elem type needs it */
                bool elem_needs_drop = (elem_type &&
                                        (elem_type->kind == TYPE_STRING ||
                                         (elem_type->kind == TYPE_STRUCT && elem_type->as.strukt.has_drop)));

                if (elem_needs_drop)
                {
                    LLVMTypeRef elem_llvm = type_to_llvm(ctx, elem_type);
                    /* Inner loop: for ei in 0..len: drop(data[ei]) */
                    LLVMBasicBlockRef el_cond = LLVMAppendBasicBlockInContext(ctx->context, cur_fn, "vcd.el.cond");
                    LLVMBasicBlockRef el_body = LLVMAppendBasicBlockInContext(ctx->context, cur_fn, "vcd.el.body");
                    LLVMBasicBlockRef el_end = LLVMAppendBasicBlockInContext(ctx->context, cur_fn, "vcd.el.end");

                    /* Allocate loop counter in entry (or use a temp) */
                    LLVMBuilderRef tb2 = LLVMCreateBuilderInContext(ctx->context);
                    LLVMBasicBlockRef fn_entry = LLVMGetEntryBasicBlock(cur_fn);
                    LLVMValueRef fi2 = LLVMGetFirstInstruction(fn_entry);
                    if (fi2)
                        LLVMPositionBuilderBefore(tb2, fi2);
                    else
                        LLVMPositionBuilderAtEnd(tb2, fn_entry);
                    LLVMValueRef ei_alloca = LLVMBuildAlloca(tb2, i32_t, "vcd.ei");
                    LLVMDisposeBuilder(tb2);

                    LLVMBuildStore(ctx->builder, zero32, ei_alloca);
                    LLVMBuildBr(ctx->builder, el_cond);

                    LLVMPositionBuilderAtEnd(ctx->builder, el_cond);
                    LLVMValueRef cur_ei = LLVMBuildLoad2(ctx->builder, i32_t, ei_alloca, "vcd.cei");
                    LLVMValueRef cmp = LLVMBuildICmp(ctx->builder, LLVMIntSLT, cur_ei, len_v, "vcd.lt");
                    LLVMBuildCondBr(ctx->builder, cmp, el_body, el_end);

                    LLVMPositionBuilderAtEnd(ctx->builder, el_body);
#if CG_DEBUG
                    if (elem_type->kind == TYPE_STRING)
                    {
                        /* Print the runtime index so we can correlate each str.free/str.skip
                           with its position inside the vec. */
                        char dbg_fmt[128];
                        snprintf(dbg_fmt, sizeof(dbg_fmt),
                                 "[cg] vec.elem.drop  var=%-10s i=%%d  type=string\n",
                                 sym->name ? sym->name : "?");
                        LLVMValueRef dbg_args[1] = {cur_ei};
                        cg_emit_debug_printf(ctx, dbg_fmt, dbg_args, 1);
                    }
#endif
                    LLVMValueRef ei64 = LLVMBuildSExt(ctx->builder, cur_ei, i64_t, "vcd.ei64");
                    LLVMValueRef ep = LLVMBuildGEP2(ctx->builder, elem_llvm, data_v, &ei64, 1, "vcd.ep");
                    /* Construct a compile-time label "vecname[i]" so str.free/str.skip
                       messages show which vec the element belongs to. */
                    char vec_elem_label[64];
                    snprintf(vec_elem_label, sizeof(vec_elem_label),
                             "%s[i]", sym->name ? sym->name : "vec");
                    emit_vec_elem_drop_at(ctx, ep, elem_type, idx, vec_elem_label);

                    LLVMBasicBlockRef after = LLVMGetInsertBlock(ctx->builder);
                    if (LLVMGetBasicBlockTerminator(after) == NULL)
                    {
                        LLVMValueRef one32 = LLVMConstInt(i32_t, 1, 0);
                        LLVMValueRef ni = LLVMBuildAdd(ctx->builder, cur_ei, one32, "vcd.ni");
                        LLVMBuildStore(ctx->builder, ni, ei_alloca);
                        LLVMBuildBr(ctx->builder, el_cond);
                    }

                    LLVMPositionBuilderAtEnd(ctx->builder, el_end);
                }

                /* free(data) */
#if CG_DEBUG
                {
                    LLVMValueRef dbg_args[1] = {data_v};
                    cg_emit_debug_printf(ctx, "[cg] vec.free   ptr=%p\n", dbg_args, 1);
                }
#endif
                cg_emit_free(ctx, data_v, "vec.scope_drop", CG_LINE(ctx), CG_COL(ctx));
                LLVMBuildBr(ctx->builder, vdone_bb);

                LLVMPositionBuilderAtEnd(ctx->builder, vdone_bb);
                (void)ptr_t;
                idx++;
            }
            else if (sym->type->kind == TYPE_MAP)
            {
                /* map(K,V) cleanup: call __ls_map_XX_YY_drop(alloca) */
#if CG_DEBUG
                {
                    char dbg_fmt[128];
                    snprintf(dbg_fmt, sizeof(dbg_fmt),
                             "[cg] scope.drop  var=%s  type=map\n",
                             sym->name ? sym->name : "?");
                    cg_emit_debug_printf(ctx, dbg_fmt, NULL, 0);
                }
#endif
                char msuffix[64];
                map_type_id(sym->type->as.map.key, sym->type->as.map.val, msuffix, sizeof(msuffix));
                char mnm[96];
                snprintf(mnm, sizeof(mnm), "__ls_map_%s_drop", msuffix);
                emit_map_helpers_for(ctx, sym->type->as.map.key, sym->type->as.map.val);
                LLVMValueRef mfn = LLVMGetNamedFunction(ctx->module, mnm);
                if (mfn)
                {
                    LLVMTypeRef mft = LLVMGlobalGetValueType(mfn);
                    LLVMBuildCall2(ctx->builder, mft, mfn, &sym->value, 1, "");
                }
                idx++;
            }
            else if (sym->type->kind == TYPE_ENUM && sym->type->as.enom.has_drop)
            {
                /* Enum with heap payload: call EnumName.__drop(alloca). */
                LLVMValueRef drop_fn = (LLVMValueRef)sym->type->as.enom.drop_fn;
                if (drop_fn)
                {
                    LLVMTypeRef fnt = LLVMGlobalGetValueType(drop_fn);
                    LLVMBuildCall2(ctx->builder, fnt, drop_fn, &sym->value, 1, "");
                }
                idx++;
            }
        }
    }
}

/* ---- Struct destructor (RAII) ---- */

/* Emit a conditional struct drop call with moved/returning flag check.
   `drop_ptr` is the pointer to the struct (*Struct).
   `struct_type` is the LS Type*.
   `moved_flag` is the i1 alloca (can be NULL for unconditional drop). */
static void emit_struct_drop_cond(CodegenContext *ctx, LLVMValueRef drop_ptr,
                                  Type *struct_type, LLVMValueRef moved_flag)
{
    if (struct_type == NULL || struct_type->kind != TYPE_STRUCT)
        return;
    if (!struct_type->as.strukt.has_drop)
        return;

    LLVMValueRef drop_fn = (LLVMValueRef)struct_type->as.strukt.drop_fn;
    if (drop_fn == NULL)
        return;

    LLVMBasicBlockRef cur_bb = LLVMGetInsertBlock(ctx->builder);
    LLVMValueRef cur_fn = LLVMGetBasicBlockParent(cur_bb);

    LLVMBasicBlockRef drop_bb = NULL;
    LLVMBasicBlockRef skip_bb = NULL;
    LLVMBasicBlockRef cont_bb = NULL;

    if (moved_flag != NULL)
    {
        drop_bb = LLVMAppendBasicBlockInContext(ctx->context, cur_fn, "drop.call");
        skip_bb = LLVMAppendBasicBlockInContext(ctx->context, cur_fn, "drop.skip");
        cont_bb = LLVMAppendBasicBlockInContext(ctx->context, cur_fn, "drop.cont");

        LLVMTypeRef i1_type = LLVMInt1TypeInContext(ctx->context);
        LLVMValueRef is_moved = LLVMBuildLoad2(ctx->builder, i1_type, moved_flag, "drop.flag");
        LLVMBuildCondBr(ctx->builder, is_moved, skip_bb, drop_bb);

        LLVMPositionBuilderAtEnd(ctx->builder, skip_bb);
#if CG_DEBUG
        {
            char dbg_fmt[128];
            snprintf(dbg_fmt, sizeof(dbg_fmt),
                     "[cg] struct.skip   (moved)  type=%s\n",
                     struct_type->as.strukt.name ? struct_type->as.strukt.name : "?");
            cg_emit_debug_printf(ctx, dbg_fmt, NULL, 0);
        }
#endif
        LLVMBuildBr(ctx->builder, cont_bb);

        LLVMPositionBuilderAtEnd(ctx->builder, drop_bb);
    }

    /* Call complete drop_fn (user wrapper or auto-generated — both handle all cleanup) */
#if CG_DEBUG
    {
        char dbg_fmt[128];
        snprintf(dbg_fmt, sizeof(dbg_fmt),
                 "[cg] struct.drop   type=%s\n",
                 struct_type->as.strukt.name ? struct_type->as.strukt.name : "?");
        cg_emit_debug_printf(ctx, dbg_fmt, NULL, 0);
    }
#endif
    LLVMTypeRef fn_type = LLVMGlobalGetValueType(drop_fn);
    LLVMBuildCall2(ctx->builder, fn_type, drop_fn, &drop_ptr, 1, "");

    if (moved_flag != NULL)
    {
        LLVMBuildBr(ctx->builder, cont_bb);
        LLVMPositionBuilderAtEnd(ctx->builder, cont_bb);
    }
}

/* Emit struct drop in a separate block chain, returns continuation block. */
static LLVMBasicBlockRef emit_struct_drop_separate(CodegenContext *ctx, LLVMValueRef drop_ptr,
                                                   Type *struct_type, LLVMValueRef moved_flag)
{
    if (struct_type == NULL || struct_type->kind != TYPE_STRUCT)
        return NULL;
    if (!struct_type->as.strukt.has_drop)
        return NULL;

    LLVMValueRef drop_fn = (LLVMValueRef)struct_type->as.strukt.drop_fn;
    if (drop_fn == NULL)
        return NULL;

    LLVMBasicBlockRef cur_bb = LLVMGetInsertBlock(ctx->builder);
    LLVMValueRef cur_fn = LLVMGetBasicBlockParent(cur_bb);

    LLVMBasicBlockRef cleanup_bb = LLVMAppendBasicBlockInContext(
        ctx->context, cur_fn, "drop.cleanup");

    /* Branch to cleanup block */
    LLVMBuildBr(ctx->builder, cleanup_bb);
    LLVMPositionBuilderAtEnd(ctx->builder, cleanup_bb);

    if (moved_flag != NULL)
    {
        LLVMBasicBlockRef drop_bb = LLVMAppendBasicBlockInContext(
            ctx->context, cur_fn, "drop.call");
        LLVMBasicBlockRef skip_bb = LLVMAppendBasicBlockInContext(
            ctx->context, cur_fn, "drop.skip");
        LLVMBasicBlockRef cont_bb = LLVMAppendBasicBlockInContext(
            ctx->context, cur_fn, "drop.cont");

        LLVMTypeRef i1_type = LLVMInt1TypeInContext(ctx->context);
        LLVMValueRef is_moved = LLVMBuildLoad2(ctx->builder, i1_type, moved_flag, "drop.flag");
        LLVMBuildCondBr(ctx->builder, is_moved, skip_bb, drop_bb);

        LLVMPositionBuilderAtEnd(ctx->builder, skip_bb);
#if CG_DEBUG
        {
            char dbg_fmt[128];
            snprintf(dbg_fmt, sizeof(dbg_fmt),
                     "[cg] struct.skip   (moved)\n");
            cg_emit_debug_printf(ctx, dbg_fmt, NULL, 0);
        }
#endif
        LLVMBuildBr(ctx->builder, cont_bb);

        LLVMPositionBuilderAtEnd(ctx->builder, drop_bb);
#if CG_DEBUG
        {
            char dbg_fmt[128];
            snprintf(dbg_fmt, sizeof(dbg_fmt),
                     "[cg] struct.drop   type=%s\n",
                     struct_type->as.strukt.name ? struct_type->as.strukt.name : "?");
            cg_emit_debug_printf(ctx, dbg_fmt, NULL, 0);
        }
#endif
        LLVMTypeRef fn_type = LLVMGlobalGetValueType(drop_fn);
        LLVMBuildCall2(ctx->builder, fn_type, drop_fn, &drop_ptr, 1, "");
        LLVMBuildBr(ctx->builder, cont_bb);

        LLVMPositionBuilderAtEnd(ctx->builder, cont_bb);
        return cont_bb;
    }
    else
    {
        /* drop_fn is complete — no additional inline cleanup needed */
#if CG_DEBUG
        {
            char dbg_fmt[128];
            snprintf(dbg_fmt, sizeof(dbg_fmt),
                     "[cg] struct.drop   type=%s\n",
                     struct_type->as.strukt.name ? struct_type->as.strukt.name : "?");
            cg_emit_debug_printf(ctx, dbg_fmt, NULL, 0);
        }
#endif
        LLVMTypeRef fn_type = LLVMGlobalGetValueType(drop_fn);
        LLVMBuildCall2(ctx->builder, fn_type, drop_fn, &drop_ptr, 1, "");
        return cleanup_bb;
    }
}

/* Generate a compiler-defined __drop function for struct.
   This is called when struct has has_drop=true but no user-defined __drop().
   The function recursively frees string fields and calls member struct drops. */
static void emit_auto_drop_fn(CodegenContext *ctx, Type *struct_type)
{
    if (struct_type == NULL || struct_type->kind != TYPE_STRUCT)
        return;
    if (struct_type->as.strukt.drop_fn != NULL)
        return; /* already has user drop */
    if (!struct_type->as.strukt.has_drop)
        return; /* nothing to drop */
    if (struct_type->as.strukt.field_count == 0)
        return;

    /* Save builder position so we can restore after generating the new function */
    LLVMBasicBlockRef saved_bb = LLVMGetInsertBlock(ctx->builder);

    const char *struct_name = struct_type->as.strukt.name;
    char drop_fn_name[256];
    snprintf(drop_fn_name, sizeof(drop_fn_name), "%s.__drop", struct_name);

    /* Check if already defined */
    if (LLVMGetNamedFunction(ctx->module, drop_fn_name) != NULL)
        return;

    /* Create function type: void __drop(*Struct) */
    LLVMTypeRef llvm_struct = type_to_llvm(ctx, struct_type);
    LLVMTypeRef ptr_struct = LLVMPointerType(llvm_struct, 0);
    LLVMTypeRef fn_type = LLVMFunctionType(LLVMVoidTypeInContext(ctx->context),
                                           &ptr_struct, 1, 0);

    /* Add function to module */
    LLVMValueRef drop_fn = LLVMAddFunction(ctx->module, drop_fn_name, fn_type);
    LLVMSetFunctionCallConv(drop_fn, LLVMCCallConv);

    /* Set dropping convention */
    LLVMBasicBlockRef entry = LLVMAppendBasicBlockInContext(ctx->context, drop_fn, "entry");
    LLVMPositionBuilderAtEnd(ctx->builder, entry);

    /* Get self parameter */
    LLVMValueRef self_ptr = LLVMGetParam(drop_fn, 0);
    LLVMSetValueName(self_ptr, "self");

    /* Generate cleanup for each field **in reverse order** */
    for (int i = struct_type->as.strukt.field_count - 1; i >= 0; i--)
    {
        Type *field_type = struct_type->as.strukt.fields[i].type;
        if (field_type == NULL)
            continue;

        /* Skip pointer types */
        if (field_type->kind == TYPE_POINTER)
            continue;

        /* Free string fields */
        if (field_type->kind == TYPE_STRING)
        {
            LLVMValueRef field_ptr = LLVMBuildStructGEP2(ctx->builder, llvm_struct,
                                                         self_ptr, (unsigned)i, "drop.strfield");
            /* emit_string_free leaves the builder at the cont block — safe to continue */
            emit_string_free(ctx, field_ptr);
            continue;
        }

        /* Recursively call __drop for struct fields with drop */
        if (field_type->kind == TYPE_STRUCT && field_type->as.strukt.has_drop)
        {
            LLVMValueRef field_ptr = LLVMBuildStructGEP2(ctx->builder, llvm_struct,
                                                         self_ptr, (unsigned)i, "drop.field");
            /* Look up the member's __drop function — it must already be in the module
               (either user-defined from Pass 2a, or auto-generated by the ordered Pass 2.5
               which processes members before containers). Recursive emit_auto_drop_fn here
               would corrupt the builder position, so we rely on pre-ordering. */
            const char *member_name = field_type->as.strukt.name;
            char member_drop_name[256];
            snprintf(member_drop_name, sizeof(member_drop_name), "%s.__drop", member_name);
            LLVMValueRef member_drop_fn = LLVMGetNamedFunction(ctx->module, member_drop_name);
            if (member_drop_fn == NULL)
            {
                /* Fallback: use drop_fn stored in the type (set by Pass 2a or earlier iteration) */
                member_drop_fn = (LLVMValueRef)field_type->as.strukt.drop_fn;
            }
            if (member_drop_fn != NULL)
            {
                LLVMTypeRef fn_type2 = LLVMGlobalGetValueType(member_drop_fn);
                LLVMBuildCall2(ctx->builder, fn_type2, member_drop_fn, &field_ptr, 1, "");
            }
            /* After call, create next block */
            LLVMBasicBlockRef cur_bb2 = LLVMGetInsertBlock(ctx->builder);
            LLVMValueRef cur_fn2 = LLVMGetBasicBlockParent(cur_bb2);
            LLVMBasicBlockRef next_entry = LLVMAppendBasicBlockInContext(ctx->context, cur_fn2, "drop.next");
            LLVMBuildBr(ctx->builder, next_entry);
            LLVMPositionBuilderAtEnd(ctx->builder, next_entry);
        }
    }

    LLVMBuildRetVoid(ctx->builder);

    /* Register this function as the drop_fn for the struct */
    struct_type->as.strukt.drop_fn = drop_fn;

    /* Restore builder to its position before we started generating this function */
    if (saved_bb != NULL)
    {
        LLVMPositionBuilderAtEnd(ctx->builder, saved_bb);
    }
}

/* Recursively emit __drop for struct and its member structs (non-pointer).
   `drop_ptr` is the pointer to the struct (*Struct).
   `struct_type` is the LS Type*. */
static void emit_struct_drop(CodegenContext *ctx, LLVMValueRef drop_ptr,
                             Type *struct_type)
{
    if (struct_type == NULL || struct_type->kind != TYPE_STRUCT)
        return;
    if (!struct_type->as.strukt.has_drop)
        return;

    LLVMValueRef drop_fn = (LLVMValueRef)struct_type->as.strukt.drop_fn;

    /* drop_fn is always complete (user wrapper or auto-generated).
       Just call it — reverse-order cleanup is already baked in. */
    if (drop_fn != NULL)
    {
        LLVMTypeRef fn_type = LLVMGlobalGetValueType(drop_fn);
        LLVMBuildCall2(ctx->builder, fn_type, drop_fn, &drop_ptr, 1, "");
    }
    else
    {
        /* Fallback: inline cleanup in reverse order (dead code after Pass 2.5) */
        LLVMTypeRef llvm_struct = type_to_llvm(ctx, struct_type);
        for (int i = struct_type->as.strukt.field_count - 1; i >= 0; i--)
        {
            Type *field_type = struct_type->as.strukt.fields[i].type;
            if (field_type == NULL)
                continue;
            if (field_type->kind == TYPE_POINTER)
                continue;
            if (field_type->kind == TYPE_STRING)
            {
                LLVMValueRef field_ptr = LLVMBuildStructGEP2(ctx->builder, llvm_struct,
                                                             drop_ptr, (unsigned)i, "drop.strfield");
                emit_string_free(ctx, field_ptr);
                continue;
            }
            if (field_type->kind == TYPE_STRUCT && field_type->as.strukt.has_drop)
            {
                LLVMValueRef field_ptr = LLVMBuildStructGEP2(ctx->builder, llvm_struct,
                                                             drop_ptr, (unsigned)i, "drop.field");
                emit_struct_drop(ctx, field_ptr, field_type);
            }
        }
    }
}

/* ---- Type mapping: LS Type -> LLVMTypeRef ---- */

LLVMTypeRef type_to_llvm(CodegenContext *ctx, Type *t)
{
    if (t == NULL)
        return LLVMVoidTypeInContext(ctx->context);

    switch (t->kind)
    {
    case TYPE_INT:
    case TYPE_I32:
        return LLVMInt32TypeInContext(ctx->context);
    case TYPE_I8:
    case TYPE_U8:
        return LLVMInt8TypeInContext(ctx->context);
    case TYPE_I16:
    case TYPE_U16:
        return LLVMInt16TypeInContext(ctx->context);
    case TYPE_I64:
    case TYPE_U64:
        return LLVMInt64TypeInContext(ctx->context);
    case TYPE_F32:
        return LLVMFloatTypeInContext(ctx->context);
    case TYPE_F64:
        return LLVMDoubleTypeInContext(ctx->context);
    case TYPE_BOOL:
        return LLVMInt1TypeInContext(ctx->context);
    case TYPE_CHAR:
        return LLVMInt32TypeInContext(ctx->context); /* char = i32 (same as int) */
    case TYPE_VOID:
        return LLVMVoidTypeInContext(ctx->context);
    case TYPE_NIL:
        return LLVMPointerTypeInContext(ctx->context, 0);
    case TYPE_STRING:
        return ls_string_type(ctx);
    case TYPE_OBJECT:
        return LLVMPointerTypeInContext(ctx->context, 0);
    case TYPE_POINTER:
        return LLVMPointerTypeInContext(ctx->context, 0);
    case TYPE_REFERENCE:
        /* ABI policy for reference types:
             &string   — by-value (16-byte POD, cap=0 marker prevents free)
             &!string  — pointer (mutations must write through to caller)
             &vec(T)   — pointer (Phase 5.6, uniform with &!vec; checker forbids mutation)
             &!vec(T)  — pointer (Phase 5.6, writes through to caller)
           String is the sole by-value specialisation; every other &T defaults
           to pointer. emit_scope_cleanup honours is_borrowed on the CgSymbol
           so borrowed slots are never freed regardless of ABI choice. */
        if (t->is_mut)
            return LLVMPointerTypeInContext(ctx->context, 0);
        if (t->as.pointer_to &&
            (t->as.pointer_to->kind == TYPE_VECTOR ||
             t->as.pointer_to->kind == TYPE_MAP ||
             t->as.pointer_to->kind == TYPE_STRUCT))
            return LLVMPointerTypeInContext(ctx->context, 0);
        return type_to_llvm(ctx, t->as.pointer_to);
    case TYPE_LIB:
        return LLVMPointerTypeInContext(ctx->context, 0);
    case TYPE_ARRAY:
        return LLVMArrayType2(type_to_llvm(ctx, t->as.array.elem),
                              (unsigned)t->as.array.size);
    case TYPE_FUNCTION:
    {
        int n = t->as.function.param_count;
        LLVMTypeRef *params = NULL;
        if (n > 0)
        {
            params = (LLVMTypeRef *)malloc_safe((size_t)n * sizeof(LLVMTypeRef));
            for (int i = 0; i < n; i++)
            {
                params[i] = type_to_llvm(ctx, t->as.function.params[i]);
            }
        }
        LLVMTypeRef ret = type_to_llvm(ctx, t->as.function.return_type);
        LLVMTypeRef fn_type = LLVMFunctionType(ret, params, (unsigned)n,
                                               t->as.function.is_vararg ? 1 : 0);
        free(params);
        return fn_type;
    }
    case TYPE_STRUCT:
    {
        if (t->as.strukt.name)
        {
            LLVMTypeRef found = find_struct_llvm(ctx, t->as.strukt.name);
            if (found)
                return found;
        }
        /* Fallback: build struct type */
        int n = t->as.strukt.field_count;
        LLVMTypeRef *fields = NULL;
        if (n > 0)
        {
            fields = (LLVMTypeRef *)malloc_safe((size_t)n * sizeof(LLVMTypeRef));
            for (int i = 0; i < n; i++)
            {
                fields[i] = type_to_llvm(ctx, t->as.strukt.fields[i].type);
            }
        }
        LLVMTypeRef st = LLVMStructTypeInContext(ctx->context, fields, (unsigned)n, 0);
        free(fields);
        return st;
    }
    case TYPE_VECTOR:
        return ls_vec_type(ctx);
    case TYPE_MAP:
        return ls_map_type(ctx);
    case TYPE_ENUM:
    {
        if (t->as.enom.name)
        {
            LLVMTypeRef found = find_enum_llvm(ctx, t->as.enom.name);
            if (found) return found;

            /* Lazy build for instantiated templates (Option(int), Result(...)).
               Mirrors codegen_enum_decl but works straight from the Type
               structure without an AST node. */
            LLVMTypeRef i8 = LLVMInt8TypeInContext(ctx->context);
            LLVMTargetDataRef td = LLVMGetModuleDataLayout(ctx->module);
            int max_payload = 0;
            for (int v = 0; v < t->as.enom.variant_count; v++)
            {
                if (t->as.enom.variants[v].payload_count == 0) continue;
                LLVMTypeRef vstruct = build_variant_payload_struct(ctx, t, v);
                unsigned long long sz = LLVMABISizeOfType(td, vstruct);
                if ((int)sz > max_payload) max_payload = (int)sz;
            }
            LLVMTypeRef payload = LLVMArrayType(i8, (unsigned)max_payload);
            LLVMTypeRef body[2] = { i8, payload };
            LLVMTypeRef llvm_type = LLVMStructCreateNamed(ctx->context, t->as.enom.name);
            LLVMStructSetBody(llvm_type, body, 2, 0);
            register_enum_llvm(ctx, t->as.enom.name, llvm_type, t, max_payload);
            if (t->as.enom.has_drop)
                emit_auto_enum_drop_fn(ctx, t);
            return llvm_type;
        }
        LLVMTypeRef i8 = LLVMInt8TypeInContext(ctx->context);
        LLVMTypeRef payload = LLVMArrayType(i8, 0);
        LLVMTypeRef fields[2] = { i8, payload };
        return LLVMStructTypeInContext(ctx->context, fields, 2, 0);
    }
    case TYPE_MODULE:
        return LLVMVoidTypeInContext(ctx->context);
    }
    return LLVMVoidTypeInContext(ctx->context);
}

/* ---- Forward declarations ---- */

LLVMValueRef codegen_expr(CodegenContext *ctx, AstNode *node);
static LLVMValueRef codegen_expr_or_borrow(CodegenContext *ctx, AstNode *node);
static LLVMValueRef codegen_vec_string_borrow(CodegenContext *ctx, AstNode *node);
static LLVMValueRef codegen_short_circuit(CodegenContext *ctx, AstNode *node);
static LLVMValueRef codegen_ffi_call(CodegenContext *ctx, AstNode *node);
static void codegen_stmt(CodegenContext *ctx, AstNode *node);
static void codegen_decl(CodegenContext *ctx, AstNode *node);

/* ---- Lvalue pointer resolution ---- */

/* Returns an LLVM pointer (alloca or GEP) for the given lvalue node without
   loading it. Handles nested field access (p1.s.k), array index, and pointer
   dereference. Returns NULL if the node is not a valid lvalue. */
static LLVMValueRef codegen_lvalue_ptr(CodegenContext *ctx, AstNode *node)
{
    if (node->kind == AST_IDENT)
    {
        CgSymbol *sym = cg_scope_resolve(ctx->current_scope, node->as.ident.name);
        return sym ? sym->value : NULL;
    }

    if (node->kind == AST_FIELD)
    {
        AstNode *obj_node = node->as.field_access.object;
        Type *obj_type = obj_node->resolved_type;
        const char *fname = node->as.field_access.field;

        /* Auto-dereference pointer-to-struct */
        bool is_ptr = false;
        Type *stype = obj_type;
        if (stype && stype->kind == TYPE_POINTER && stype->as.pointer_to &&
            stype->as.pointer_to->kind == TYPE_STRUCT)
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
        if (obj_type == NULL || obj_type->kind != TYPE_ARRAY)
            return NULL;

        LLVMValueRef arr_ptr = codegen_lvalue_ptr(ctx, obj);
        if (arr_ptr == NULL)
            return NULL;

        LLVMValueRef index = codegen_expr(ctx, node->as.index_expr.index);
        if (index == NULL)
            return NULL;

        LLVMTypeRef i64_type = LLVMInt64TypeInContext(ctx->context);
        if (LLVMTypeOf(index) != i64_type)
            index = LLVMBuildSExtOrBitCast(ctx->builder, index, i64_type, "idx.ext");

        LLVMTypeRef arr_llvm = type_to_llvm(ctx, obj_type);
        LLVMValueRef zero = LLVMConstInt(i64_type, 0, 0);
        LLVMValueRef indices[2] = {zero, index};
        return LLVMBuildGEP2(ctx->builder, arr_llvm, arr_ptr, indices, 2, "arr.elem.ptr");
    }

    if (node->kind == AST_UNARY && node->as.unary.op == TOKEN_STAR)
    {
        /* *ptr — the lvalue pointer is the pointer value itself */
        return codegen_expr(ctx, node->as.unary.operand);
    }

    return NULL;
}

/* ---- Printf format specifier for a given type ---- */

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
    case TYPE_BOOL:
        return "%s";
    case TYPE_STRING:
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

/* Helper: emit a printf call with the given format string and args */
static LLVMValueRef emit_printf(CodegenContext *ctx, const char *fmt,
                                LLVMValueRef *extra_args, int extra_count)
{
    LLVMValueRef printf_fn = LLVMGetNamedFunction(ctx->module, "printf");
    LLVMTypeRef printf_type = LLVMGlobalGetValueType(printf_fn);

    int total = 1 + extra_count;
    LLVMValueRef *args = (LLVMValueRef *)malloc_safe((size_t)total * sizeof(LLVMValueRef));
    args[0] = LLVMBuildGlobalStringPtr(ctx->builder, fmt, "fmt");
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

        /* String: extract .data from LsString for printf */
        if (elem_type->kind == TYPE_STRING)
        {
            val = ls_string_data(ctx, val);
        }
        /* Bool: convert to "true"/"false" */
        else if (elem_type->kind == TYPE_BOOL)
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

/* Helper: emit the printf val (key or value) for a map field.
   Handles string (extract .data + quote), bool, small ints, and numerics. */
static LLVMValueRef map_print_coerce(CodegenContext *ctx, LLVMValueRef raw, Type *t,
                                     bool is_string_key)
{
    if (t == NULL)
        return raw;
    if (t->kind == TYPE_STRING)
    {
        (void)is_string_key;
        return ls_string_data(ctx, raw);
    }
    if (t->kind == TYPE_BOOL)
    {
        LLVMValueRef ts = LLVMBuildGlobalStringPtr(ctx->builder, "true", "ts");
        LLVMValueRef fs = LLVMBuildGlobalStringPtr(ctx->builder, "false", "fs");
        return LLVMBuildSelect(ctx->builder, raw, ts, fs, "boolstr");
    }
    if (t->kind == TYPE_I8 || t->kind == TYPE_I16)
        return LLVMBuildSExt(ctx->builder, raw,
                             LLVMInt32TypeInContext(ctx->context), "sext");
    if (t->kind == TYPE_U8 || t->kind == TYPE_U16)
        return LLVMBuildZExt(ctx->builder, raw,
                             LLVMInt32TypeInContext(ctx->context), "zext");
    return raw;
}

/* Forward declaration: defined below codegen_print_map */
static void codegen_print_struct_value(CodegenContext *ctx, LLVMValueRef val, Type *t);

/* Print a map value inline (no trailing newline).
   Output format:
     map(K,V){len=N, cap=N}            — empty map
     map(K,V){len=N, cap=N, k0: v0, k1: v1, ...}   — non-empty
   String keys are printed quoted: "hello": 42
   Struct values are printed as StructName{field=val, ...}
   Max MAX_PRINT_PAIRS pairs are shown; if more exist, ", ..." is appended. */
#define MAP_MAX_PRINT_PAIRS 16
static void codegen_print_map(CodegenContext *ctx, AstNode *arg)
{
    Type *map_type = arg->resolved_type;
    if (!map_type || map_type->kind != TYPE_MAP)
        return;

    Type *key_type = map_type->as.map.key;
    Type *val_type = map_type->as.map.val;

    /* Resolve map alloca */
    LLVMValueRef map_alloca = NULL;
    if (arg->kind == AST_IDENT)
    {
        CgSymbol *sym = cg_scope_resolve(ctx->current_scope, arg->as.ident.name);
        if (sym)
            map_alloca = sym->value;
    }
    if (map_alloca == NULL)
        return;

    /* Ensure helpers exist (needed for node struct type) */
    emit_map_helpers_for(ctx, key_type, val_type);

    LLVMTypeRef map_t = ls_map_type(ctx);
    LLVMTypeRef node_t = ls_map_node_type(ctx, key_type, val_type);
    LLVMTypeRef key_lt = type_to_llvm(ctx, key_type);
    LLVMTypeRef val_lt = type_to_llvm(ctx, val_type);
    LLVMTypeRef ptr_t = LLVMPointerTypeInContext(ctx->context, 0);
    LLVMTypeRef i32_t = LLVMInt32TypeInContext(ctx->context);
    LLVMTypeRef i64_t = LLVMInt64TypeInContext(ctx->context);

    /* Load map struct */
    LLVMValueRef mv = LLVMBuildLoad2(ctx->builder, map_t, map_alloca, "mv");
    LLVMValueRef buckets = LLVMBuildExtractValue(ctx->builder, mv, 0, "bkts");
    LLVMValueRef len_v = LLVMBuildExtractValue(ctx->builder, mv, 1, "len");
    LLVMValueRef cap_v = LLVMBuildExtractValue(ctx->builder, mv, 2, "cap");

    /* Build key/val type name strings for header */
    const char *kname = type_name(key_type);
    const char *vname = type_name(val_type);
    char hdr_fmt[128];
    snprintf(hdr_fmt, sizeof(hdr_fmt), "map(%s,%s){len=%%d, cap=%%d", kname, vname);

    /* Print header: map(K,V){len=N, cap=N */
    LLVMValueRef hdr_args[] = {len_v, cap_v};
    emit_printf(ctx, hdr_fmt, hdr_args, 2);

    /* Get current function for basic block creation */
    LLVMBasicBlockRef cur_bb = LLVMGetInsertBlock(ctx->builder);
    LLVMValueRef cur_fn = LLVMGetBasicBlockParent(cur_bb);

    /* Branch: if cap==0, go directly to done */
    LLVMValueRef cap_zero = LLVMBuildICmp(ctx->builder, LLVMIntEQ, cap_v,
                                          LLVMConstInt(i32_t, 0, 0), "czero");
    LLVMBasicBlockRef done_bb = LLVMAppendBasicBlockInContext(ctx->context, cur_fn, "pm.done");
    LLVMBasicBlockRef loop_init_bb = LLVMAppendBasicBlockInContext(ctx->context, cur_fn, "pm.linit");
    LLVMBuildCondBr(ctx->builder, cap_zero, done_bb, loop_init_bb);

    /* ---- loop_init_bb: allocate loop state ---- */
    LLVMPositionBuilderAtEnd(ctx->builder, loop_init_bb);
    LLVMValueRef bi_a = LLVMBuildAlloca(ctx->builder, i64_t, "bi");   /* bucket index */
    LLVMValueRef nd_a = LLVMBuildAlloca(ctx->builder, ptr_t, "nd");   /* current node */
    LLVMValueRef cnt_a = LLVMBuildAlloca(ctx->builder, i32_t, "cnt"); /* pairs printed */
    LLVMBuildStore(ctx->builder, LLVMConstInt(i64_t, 0, 0), bi_a);
    LLVMBuildStore(ctx->builder, LLVMConstInt(i32_t, 0, 0), cnt_a);

    LLVMBasicBlockRef outer_cond_bb = LLVMAppendBasicBlockInContext(ctx->context, cur_fn, "pm.ocond");
    LLVMBasicBlockRef outer_body_bb = LLVMAppendBasicBlockInContext(ctx->context, cur_fn, "pm.obody");
    LLVMBasicBlockRef outer_next_bb = LLVMAppendBasicBlockInContext(ctx->context, cur_fn, "pm.onext");
    LLVMBasicBlockRef inner_cond_bb = LLVMAppendBasicBlockInContext(ctx->context, cur_fn, "pm.icond");
    LLVMBasicBlockRef inner_body_bb = LLVMAppendBasicBlockInContext(ctx->context, cur_fn, "pm.ibody");
    LLVMBasicBlockRef inner_next_bb = LLVMAppendBasicBlockInContext(ctx->context, cur_fn, "pm.inext");
    LLVMBasicBlockRef truncated_bb = LLVMAppendBasicBlockInContext(ctx->context, cur_fn, "pm.trunc");
    LLVMBuildBr(ctx->builder, outer_cond_bb);

    /* ---- outer_cond_bb: bi < cap ---- */
    LLVMPositionBuilderAtEnd(ctx->builder, outer_cond_bb);
    LLVMValueRef bi_v = LLVMBuildLoad2(ctx->builder, i64_t, bi_a, "bi");
    LLVMValueRef cap64 = LLVMBuildZExt(ctx->builder, cap_v, i64_t, "cap64");
    LLVMValueRef bi_lt = LLVMBuildICmp(ctx->builder, LLVMIntSLT, bi_v, cap64, "bilt");
    LLVMBuildCondBr(ctx->builder, bi_lt, outer_body_bb, done_bb);

    /* ---- outer_body_bb: load bucket head → inner loop ---- */
    LLVMPositionBuilderAtEnd(ctx->builder, outer_body_bb);
    LLVMValueRef slot_p = LLVMBuildGEP2(ctx->builder, ptr_t, buckets, &bi_v, 1, "slot_p");
    LLVMValueRef slot = LLVMBuildLoad2(ctx->builder, ptr_t, slot_p, "slot");
    LLVMBuildStore(ctx->builder, slot, nd_a);
    LLVMBuildBr(ctx->builder, inner_cond_bb);

    /* ---- inner_cond_bb: nd != NULL ---- */
    LLVMPositionBuilderAtEnd(ctx->builder, inner_cond_bb);
    LLVMValueRef nd_v = LLVMBuildLoad2(ctx->builder, ptr_t, nd_a, "nd");
    LLVMValueRef is_null = LLVMBuildICmp(ctx->builder, LLVMIntEQ, nd_v,
                                         LLVMConstNull(ptr_t), "isnull");
    LLVMBuildCondBr(ctx->builder, is_null, outer_next_bb, inner_body_bb);

    /* ---- inner_body_bb: print one key:val pair ---- */
    LLVMPositionBuilderAtEnd(ctx->builder, inner_body_bb);

    /* Check printed count against max */
    LLVMValueRef cnt_v = LLVMBuildLoad2(ctx->builder, i32_t, cnt_a, "cnt");
    LLVMValueRef max_v = LLVMConstInt(i32_t, MAP_MAX_PRINT_PAIRS, 0);
    LLVMValueRef at_max = LLVMBuildICmp(ctx->builder, LLVMIntEQ, cnt_v, max_v, "atmax");
    LLVMBuildCondBr(ctx->builder, at_max, truncated_bb, inner_next_bb);

    /* ---- truncated_bb: print " ..." and jump to done ---- */
    LLVMPositionBuilderAtEnd(ctx->builder, truncated_bb);
    emit_printf(ctx, ", ...", NULL, 0);
    LLVMBuildBr(ctx->builder, done_bb);

    /* ---- inner_next_bb: print separator then key:val ---- */
    LLVMPositionBuilderAtEnd(ctx->builder, inner_next_bb);
    /* Separator: ", " before every pair */
    emit_printf(ctx, ", ", NULL, 0);

    /* Load key from node field 1 */
    LLVMValueRef key_p = LLVMBuildStructGEP2(ctx->builder, node_t, nd_v, 1, "kp");
    LLVMValueRef key_v = LLVMBuildLoad2(ctx->builder, key_lt, key_p, "kv");

    /* Print key (string keys get quoted) */
    if (key_type->kind == TYPE_STRING)
    {
        LLVMValueRef kdata = ls_string_data(ctx, key_v);
        emit_printf(ctx, "\"%s\": ", &kdata, 1);
    }
    else
    {
        LLVMValueRef kcoerced = map_print_coerce(ctx, key_v, key_type, false);
        char kfmt[32];
        snprintf(kfmt, sizeof(kfmt), "%s: ", printf_fmt_for_type(key_type));
        emit_printf(ctx, kfmt, &kcoerced, 1);
    }

    /* Load val from node field 2 */
    LLVMValueRef val_p = LLVMBuildStructGEP2(ctx->builder, node_t, nd_v, 2, "vp");
    LLVMValueRef val_v = LLVMBuildLoad2(ctx->builder, val_lt, val_p, "vv");

    /* Print val (type-specific formatting) */
    if (val_type->kind == TYPE_STRING)
    {
        LLVMValueRef vdata = ls_string_data(ctx, val_v);
        emit_printf(ctx, "\"%s\"", &vdata, 1);
    }
    else if (val_type->kind == TYPE_STRUCT)
    {
        /* Print struct fields: StructName{field=val, ...} */
        codegen_print_struct_value(ctx, val_v, val_type);
    }
    else
    {
        LLVMValueRef vcoerced = map_print_coerce(ctx, val_v, val_type, false);
        const char *vfmt = printf_fmt_for_type(val_type);
        emit_printf(ctx, vfmt, &vcoerced, 1);
    }

    /* Advance: nd = nd->next (field 3), cnt++ */
    LLVMValueRef next_p = LLVMBuildStructGEP2(ctx->builder, node_t, nd_v, 3, "nxp");
    LLVMValueRef next_v = LLVMBuildLoad2(ctx->builder, ptr_t, next_p, "nxv");
    LLVMBuildStore(ctx->builder, next_v, nd_a);
    LLVMValueRef cnt_inc = LLVMBuildAdd(ctx->builder, cnt_v,
                                        LLVMConstInt(i32_t, 1, 0), "cntinc");
    LLVMBuildStore(ctx->builder, cnt_inc, cnt_a);
    LLVMBuildBr(ctx->builder, inner_cond_bb);

    /* ---- outer_next_bb: bi++ ---- */
    LLVMPositionBuilderAtEnd(ctx->builder, outer_next_bb);
    LLVMValueRef bi_inc = LLVMBuildAdd(ctx->builder,
                                       LLVMBuildLoad2(ctx->builder, i64_t, bi_a, "bi2"),
                                       LLVMConstInt(i64_t, 1, 0), "biinc");
    LLVMBuildStore(ctx->builder, bi_inc, bi_a);
    LLVMBuildBr(ctx->builder, outer_cond_bb);

    /* ---- done_bb: print closing "}" ---- */
    LLVMPositionBuilderAtEnd(ctx->builder, done_bb);
    emit_printf(ctx, "}", NULL, 0);
}
#undef MAP_MAX_PRINT_PAIRS

/* Helper: print a struct aggregate value as StructName{field=val, ...} (no trailing newline) */
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

        if (ftype->kind == TYPE_STRUCT)
        {
            /* Nested struct — recurse */
            codegen_print_struct_value(ctx, fval, ftype);
            continue;
        }
        else if (ftype->kind == TYPE_STRING)
        {
            fval = ls_string_data(ctx, fval);
        }
        else if (ftype->kind == TYPE_BOOL)
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

        const char *spec = printf_fmt_for_type(ftype);
        emit_printf(ctx, spec, &fval, 1);
    }

    emit_printf(ctx, "}", NULL, 0);
}

/* Print a vec value inline (no trailing newline).
   Output format:
     vec(T){len=0, cap=0}                               — empty vec
     vec(T){len=N, cap=C, [e0, e1, ..., e15, ...]}     — non-empty (max 16 elems shown)
   String elements are printed quoted: "hello"
   Struct elements are printed as StructName{field=val, ...}
   If len > 16 a trailing ", ..." is appended before the closing "]". */
#define VEC_MAX_PRINT_ELEMS 16
static void codegen_print_vec(CodegenContext *ctx, AstNode *arg)
{
    Type *vec_type = arg->resolved_type;
    if (!vec_type || vec_type->kind != TYPE_VECTOR)
        return;
    Type *elem_type = vec_type->as.vec.elem;

    /* Resolve vec alloca (only IDENT supported, same as array/map) */
    LLVMValueRef vec_alloca = NULL;
    if (arg->kind == AST_IDENT)
    {
        CgSymbol *sym = cg_scope_resolve(ctx->current_scope, arg->as.ident.name);
        if (sym)
            vec_alloca = sym->value;
    }
    if (vec_alloca == NULL)
        return;

    LLVMTypeRef vec_t = ls_vec_type(ctx);
    LLVMTypeRef elem_llvm = type_to_llvm(ctx, elem_type);
    LLVMTypeRef i32_t = LLVMInt32TypeInContext(ctx->context);
    LLVMTypeRef i64_t = LLVMInt64TypeInContext(ctx->context);

    /* Load LsVec struct and extract fields */
    LLVMValueRef vv = LLVMBuildLoad2(ctx->builder, vec_t, vec_alloca, "pv.v");
    LLVMValueRef data_v = LLVMBuildExtractValue(ctx->builder, vv, 0, "pv.data");
    LLVMValueRef len_v = LLVMBuildExtractValue(ctx->builder, vv, 1, "pv.len");
    LLVMValueRef cap_v = LLVMBuildExtractValue(ctx->builder, vv, 2, "pv.cap");

    /* Header: vec(T){len=N, cap=N */
    const char *ename = type_name(elem_type);
    char hdr_fmt[128];
    snprintf(hdr_fmt, sizeof(hdr_fmt), "vec(%s){len=%%d, cap=%%d", ename);
    LLVMValueRef hdr_args[] = {len_v, cap_v};
    emit_printf(ctx, hdr_fmt, hdr_args, 2);

    /* Branch: if len == 0, skip element section */
    LLVMValueRef cur_fn = LLVMGetBasicBlockParent(LLVMGetInsertBlock(ctx->builder));
    LLVMValueRef len_eq0 = LLVMBuildICmp(ctx->builder, LLVMIntEQ, len_v,
                                         LLVMConstInt(i32_t, 0, 0), "pv.eq0");
    LLVMBasicBlockRef elems_bb = LLVMAppendBasicBlockInContext(ctx->context, cur_fn, "pv.elems");
    LLVMBasicBlockRef done_bb = LLVMAppendBasicBlockInContext(ctx->context, cur_fn, "pv.done");
    LLVMBuildCondBr(ctx->builder, len_eq0, done_bb, elems_bb);

    /* ---- elems_bb: len > 0, print ", [" then loop ---- */
    LLVMPositionBuilderAtEnd(ctx->builder, elems_bb);
    emit_printf(ctx, ", [", NULL, 0);

    /* Compute limit = min(len, VEC_MAX_PRINT_ELEMS) and remember if truncated */
    LLVMValueRef max_v = LLVMConstInt(i32_t, VEC_MAX_PRINT_ELEMS, 0);
    LLVMValueRef truncated = LLVMBuildICmp(ctx->builder, LLVMIntSGT, len_v, max_v, "pv.trunc");
    LLVMValueRef limit_v = LLVMBuildSelect(ctx->builder, truncated, max_v, len_v, "pv.lim");

    /* Alloca for loop counter — placed in function entry block */
    LLVMBuilderRef tmp_b = LLVMCreateBuilderInContext(ctx->context);
    LLVMBasicBlockRef fn_entry = LLVMGetEntryBasicBlock(cur_fn);
    LLVMValueRef first_instr = LLVMGetFirstInstruction(fn_entry);
    if (first_instr)
        LLVMPositionBuilderBefore(tmp_b, first_instr);
    else
        LLVMPositionBuilderAtEnd(tmp_b, fn_entry);
    LLVMValueRef i_alloca = LLVMBuildAlloca(tmp_b, i32_t, "pv.i");
    LLVMDisposeBuilder(tmp_b);

    LLVMBuildStore(ctx->builder, LLVMConstInt(i32_t, 0, 0), i_alloca);

    LLVMBasicBlockRef cond_bb = LLVMAppendBasicBlockInContext(ctx->context, cur_fn, "pv.cond");
    LLVMBasicBlockRef body_bb = LLVMAppendBasicBlockInContext(ctx->context, cur_fn, "pv.body");
    LLVMBasicBlockRef sep_bb = LLVMAppendBasicBlockInContext(ctx->context, cur_fn, "pv.sep");
    LLVMBasicBlockRef pelem_bb = LLVMAppendBasicBlockInContext(ctx->context, cur_fn, "pv.pelem");
    LLVMBasicBlockRef lend_bb = LLVMAppendBasicBlockInContext(ctx->context, cur_fn, "pv.lend");
    LLVMBasicBlockRef oflow_bb = LLVMAppendBasicBlockInContext(ctx->context, cur_fn, "pv.oflow");
    LLVMBasicBlockRef close_bb = LLVMAppendBasicBlockInContext(ctx->context, cur_fn, "pv.close");
    LLVMBuildBr(ctx->builder, cond_bb);

    /* ---- cond_bb: i < limit ---- */
    LLVMPositionBuilderAtEnd(ctx->builder, cond_bb);
    LLVMValueRef i_v = LLVMBuildLoad2(ctx->builder, i32_t, i_alloca, "pv.iv");
    LLVMValueRef lt = LLVMBuildICmp(ctx->builder, LLVMIntSLT, i_v, limit_v, "pv.lt");
    LLVMBuildCondBr(ctx->builder, lt, body_bb, lend_bb);

    /* ---- body_bb: print ", " separator (if not first element) ---- */
    LLVMPositionBuilderAtEnd(ctx->builder, body_bb);
    LLVMValueRef not_first = LLVMBuildICmp(ctx->builder, LLVMIntSGT, i_v,
                                           LLVMConstInt(i32_t, 0, 0), "pv.nf");
    LLVMBuildCondBr(ctx->builder, not_first, sep_bb, pelem_bb);

    LLVMPositionBuilderAtEnd(ctx->builder, sep_bb);
    emit_printf(ctx, ", ", NULL, 0);
    LLVMBuildBr(ctx->builder, pelem_bb);

    /* ---- pelem_bb: GEP + load + print one element ---- */
    LLVMPositionBuilderAtEnd(ctx->builder, pelem_bb);
    LLVMValueRef i64 = LLVMBuildSExt(ctx->builder, i_v, i64_t, "pv.i64");
    LLVMValueRef ep = LLVMBuildGEP2(ctx->builder, elem_llvm, data_v, &i64, 1, "pv.ep");
    LLVMValueRef ev = LLVMBuildLoad2(ctx->builder, elem_llvm, ep, "pv.ev");

    if (elem_type->kind == TYPE_STRING)
    {
        LLVMValueRef edata = ls_string_data(ctx, ev);
        emit_printf(ctx, "\"%s\"", &edata, 1);
    }
    else if (elem_type->kind == TYPE_STRUCT)
    {
        codegen_print_struct_value(ctx, ev, elem_type);
    }
    else if (elem_type->kind == TYPE_BOOL)
    {
        LLVMValueRef ts = LLVMBuildGlobalStringPtr(ctx->builder, "true", "pvts");
        LLVMValueRef fs = LLVMBuildGlobalStringPtr(ctx->builder, "false", "pvfs");
        LLVMValueRef bv = LLVMBuildSelect(ctx->builder, ev, ts, fs, "pvbool");
        emit_printf(ctx, "%s", &bv, 1);
    }
    else
    {
        /* Numeric types: coerce small ints before printf */
        LLVMValueRef coerced = map_print_coerce(ctx, ev, elem_type, false);
        const char *fmt = printf_fmt_for_type(elem_type);
        emit_printf(ctx, fmt, &coerced, 1);
    }

    /* i++ and back to cond */
    LLVMValueRef i_inc = LLVMBuildAdd(ctx->builder, i_v,
                                      LLVMConstInt(i32_t, 1, 0), "pv.inc");
    LLVMBuildStore(ctx->builder, i_inc, i_alloca);
    LLVMBuildBr(ctx->builder, cond_bb);

    /* ---- lend_bb: loop done — check if truncated ---- */
    LLVMPositionBuilderAtEnd(ctx->builder, lend_bb);
    LLVMBuildCondBr(ctx->builder, truncated, oflow_bb, close_bb);

    /* ---- oflow_bb: print ", ..." to indicate more elements ---- */
    LLVMPositionBuilderAtEnd(ctx->builder, oflow_bb);
    emit_printf(ctx, ", ...", NULL, 0);
    LLVMBuildBr(ctx->builder, close_bb);

    /* ---- close_bb: close the bracket ---- */
    LLVMPositionBuilderAtEnd(ctx->builder, close_bb);
    emit_printf(ctx, "]", NULL, 0);
    LLVMBuildBr(ctx->builder, done_bb);

    /* ---- done_bb: close the brace ---- */
    LLVMPositionBuilderAtEnd(ctx->builder, done_bb);
    emit_printf(ctx, "}", NULL, 0);
}
#undef VEC_MAX_PRINT_ELEMS

/* Codegen for print() with any type — generates inline printf */
static LLVMValueRef codegen_print_call(CodegenContext *ctx, AstNode *node)
{
    int argc = node->as.call.arg_count;

    /* Build format string and args based on each argument's resolved type */
    char fmt_buf[1024];
    int fmt_len = 0;

    /* Collect codegen'd args and their bool-converted values */
    LLVMValueRef *printf_args = (LLVMValueRef *)malloc_safe(
        (size_t)(argc * 2) * sizeof(LLVMValueRef)); /* worst case: bool needs extra */
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
                /* Text part before expression */
                const char *txt = arg->as.format_string.parts[j];
                int tlen = (int)strlen(txt);
                if (fmt_len + tlen < 1020)
                {
                    memcpy(fmt_buf + fmt_len, txt, (size_t)tlen);
                    fmt_len += tlen;
                }

                /* Expression — borrow vec(string)[i] instead of cloning */
                AstNode *expr = arg->as.format_string.exprs[j];
                LLVMValueRef val = codegen_expr_or_borrow(ctx, expr);
                if (val == NULL)
                {
                    free(printf_args);
                    return NULL;
                }

                Type *et = expr->resolved_type;
                const char *spec = printf_fmt_for_type(et);
                int slen = (int)strlen(spec);
                if (fmt_len + slen < 1020)
                {
                    memcpy(fmt_buf + fmt_len, spec, (size_t)slen);
                    fmt_len += slen;
                }

                /* String: extract .data from LsString for printf */
                if (et && et->kind == TYPE_STRING)
                {
                    val = ls_string_data(ctx, val);
                }
                /* Bool needs special handling: convert i1 to "true"/"false" string */
                else if (et && et->kind == TYPE_BOOL)
                {
                    LLVMValueRef true_str = LLVMBuildGlobalStringPtr(ctx->builder, "true", "true");
                    LLVMValueRef false_str = LLVMBuildGlobalStringPtr(ctx->builder, "false", "false");
                    val = LLVMBuildSelect(ctx->builder, val, true_str, false_str, "boolstr");
                }
                printf_args[printf_argc++] = val;
            }
            /* Trailing text part */
            if (arg->as.format_string.part_count > arg->as.format_string.expr_count)
            {
                const char *txt = arg->as.format_string.parts[arg->as.format_string.expr_count];
                int tlen = (int)strlen(txt);
                if (fmt_len + tlen < 1020)
                {
                    memcpy(fmt_buf + fmt_len, txt, (size_t)tlen);
                    fmt_len += tlen;
                }
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

        /* Map: print as map(K,V){len=N, cap=N, k0: v0, k1: v1, ...} */
        if (t && t->kind == TYPE_MAP)
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
            codegen_print_map(ctx, arg);
            continue;
        }

        /* Vec: print as vec(T){len=N, cap=N, [e0, e1, ..., e15, ...]} */
        if (t && t->kind == TYPE_VECTOR)
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
            codegen_print_vec(ctx, arg);
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
            continue;
        }

        /* Use auto-borrow for vec(string)[i] — only reads .data, no clone needed */
        LLVMValueRef val = codegen_expr_or_borrow(ctx, arg);
        if (val == NULL)
        {
            free(printf_args);
            return NULL;
        }

        /* For string arguments, create a temp alloca and register for cleanup.
           This handles cases like print("hello".upper()) where the temporary
           string result would otherwise leak. */
        if (t && t->kind == TYPE_STRING && expr_produces_dynamic_string(arg))
        {
            LLVMValueRef str_alloca = LLVMBuildAlloca(ctx->builder,
                                                      ls_string_type(ctx), "str.argtmp");
            LLVMBuildStore(ctx->builder, val, str_alloca);
            Type *str_type = type_string();
            cg_scope_define(ctx->current_scope, "__argtmp", str_alloca, str_type, NULL);
        }

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

        /* String: extract .data pointer from LsString struct for printf */
        if (t && t->kind == TYPE_STRING)
        {
            val = ls_string_data(ctx, val);
        }
        /* Bool: convert i1 to "true"/"false" */
        else if (t && t->kind == TYPE_BOOL)
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

    LLVMValueRef *vals = (LLVMValueRef *)malloc_safe(
        (size_t)(expr_count + 1) * sizeof(LLVMValueRef));
    int val_count = 0;

    for (int i = 0; i < expr_count; i++)
    {
        /* Text part */
        const char *txt = node->as.format_string.parts[i];
        int tlen = (int)strlen(txt);
        if (fmt_len + tlen < 1020)
        {
            memcpy(fmt_buf + fmt_len, txt, (size_t)tlen);
            fmt_len += tlen;
        }

        /* Expression */
        AstNode *expr = node->as.format_string.exprs[i];
        LLVMValueRef val = codegen_expr(ctx, expr);
        if (val == NULL)
        {
            free(vals);
            return NULL;
        }

        Type *et = expr->resolved_type;
        const char *spec = printf_fmt_for_type(et);
        int slen = (int)strlen(spec);
        if (fmt_len + slen < 1020)
        {
            memcpy(fmt_buf + fmt_len, spec, (size_t)slen);
            fmt_len += slen;
        }

        /* String: extract .data for sprintf */
        if (et && et->kind == TYPE_STRING)
        {
            val = ls_string_data(ctx, val);
        }
        else if (et && et->kind == TYPE_BOOL)
        {
            LLVMValueRef true_str = LLVMBuildGlobalStringPtr(ctx->builder, "true", "true");
            LLVMValueRef false_str = LLVMBuildGlobalStringPtr(ctx->builder, "false", "false");
            val = LLVMBuildSelect(ctx->builder, val, true_str, false_str, "boolstr");
        }
        else if (et && (et->kind == TYPE_I8 || et->kind == TYPE_I16))
        {
            val = LLVMBuildSExt(ctx->builder, val,
                                LLVMInt32TypeInContext(ctx->context), "sext");
        }
        else if (et && (et->kind == TYPE_U8 || et->kind == TYPE_U16))
        {
            val = LLVMBuildZExt(ctx->builder, val,
                                LLVMInt32TypeInContext(ctx->context), "zext");
        }

        vals[val_count++] = val;
    }

    /* Trailing text */
    if (part_count > expr_count)
    {
        const char *txt = node->as.format_string.parts[expr_count];
        int tlen = (int)strlen(txt);
        if (fmt_len + tlen < 1020)
        {
            memcpy(fmt_buf + fmt_len, txt, (size_t)tlen);
            fmt_len += tlen;
        }
    }
    fmt_buf[fmt_len] = '\0';

    /* If no expressions, just return as static LsString */
    if (expr_count == 0)
    {
        LLVMValueRef result = ls_string_from_literal(ctx, fmt_buf, "fstr");
        free(vals);
        return result;
    }

    /* We'll use snprintf to measure, then malloc, then sprintf to fill.
       For simplicity, use a stack buffer of 4096 as intermediate. */

    /* Declare strlen for measuring the result */
    LLVMValueRef strlen_fn = LLVMGetNamedFunction(ctx->module, "strlen");
    LLVMTypeRef ptr_t = LLVMPointerTypeInContext(ctx->context, 0);
    LLVMTypeRef i32_t = LLVMInt32TypeInContext(ctx->context);
    LLVMTypeRef i64_t = LLVMInt64TypeInContext(ctx->context);
    if (strlen_fn == NULL)
    {
        LLVMTypeRef sl_params[] = {ptr_t};
        LLVMTypeRef sl_type = LLVMFunctionType(i64_t, sl_params, 1, 0);
        strlen_fn = LLVMAddFunction(ctx->module, "strlen", sl_type);
    }

    /* Declare sprintf for string formatting */
    LLVMValueRef sprintf_fn = LLVMGetNamedFunction(ctx->module, "sprintf");
    if (sprintf_fn == NULL)
    {
        LLVMTypeRef sp_params[] = {ptr_t, ptr_t};
        LLVMTypeRef sp_type = LLVMFunctionType(i32_t, sp_params, 2, 1);
        sprintf_fn = LLVMAddFunction(ctx->module, "sprintf", sp_type);
    }
    LLVMTypeRef sp_type = LLVMGlobalGetValueType(sprintf_fn);

    LLVMValueRef fmt_str = LLVMBuildGlobalStringPtr(ctx->builder, fmt_buf, "fstr.fmt");

    /* Allocate heap buffer: 4096 bytes (should be enough for most f-strings) */
    LLVMValueRef buf_size = LLVMConstInt(i32_t, 4096, 0);
    LLVMValueRef buf_size64 = LLVMBuildZExt(ctx->builder, buf_size, i64_t, "fstr.size64");
    LLVMValueRef buf = cg_emit_alloc(ctx, buf_size64, "string.fstring",
                                     node->line, node->column);

    /* Call sprintf(buf, fmt, ...) */
    int total = 2 + val_count;
    LLVMValueRef *sp_args = (LLVMValueRef *)malloc_safe((size_t)total * sizeof(LLVMValueRef));
    sp_args[0] = buf;
    sp_args[1] = fmt_str;
    for (int i = 0; i < val_count; i++)
        sp_args[2 + i] = vals[i];

    LLVMBuildCall2(ctx->builder, sp_type, sprintf_fn,
                   sp_args, (unsigned)total, "");
    free(sp_args);
    free(vals);

    /* Measure result length with strlen(buf) */
    LLVMTypeRef sl_type = LLVMGlobalGetValueType(strlen_fn);
    LLVMValueRef len64 = LLVMBuildCall2(ctx->builder, sl_type, strlen_fn,
                                        &buf, 1, "fstr.len64");
    LLVMValueRef len = LLVMBuildTrunc(ctx->builder, len64, i32_t, "fstr.len");

    /* Build LsString: { data=buf, len=len, cap=4096 (heap-allocated) } */
    LLVMValueRef cap = LLVMConstInt(i32_t, 4096, 0);
    return cg_push_temp_string(ctx, ls_string_make(ctx, buf, len, cap));
}

/* Codegen for to_string(x) builtin — converts numeric/bool to LsString */
static LLVMValueRef codegen_to_string(CodegenContext *ctx, AstNode *node)
{
    if (node->as.call.arg_count != 1)
        return NULL;
    AstNode *arg = node->as.call.args[0];
    LLVMValueRef val = codegen_expr(ctx, arg);
    if (val == NULL)
        return NULL;

    Type *arg_type = arg->resolved_type;
    if (arg_type == NULL)
        return NULL;

    LLVMTypeRef i8_type = LLVMInt8TypeInContext(ctx->context);
    LLVMTypeRef i32_type = LLVMInt32TypeInContext(ctx->context);
    LLVMTypeRef i64_type = LLVMInt64TypeInContext(ctx->context);
    LLVMTypeRef ptr_type = LLVMPointerType(i8_type, 0);

    /* Allocate a buffer for the formatted string (256 bytes should be enough) */
    LLVMValueRef buf_size = LLVMConstInt(i32_type, 256, 0);
    LLVMValueRef malloc_fn = LLVMGetNamedFunction(ctx->module, "malloc");
    LLVMTypeRef malloc_type = LLVMGlobalGetValueType(malloc_fn);
    LLVMValueRef buf64 = LLVMBuildZExt(ctx->builder, buf_size, i64_type, "tostr.size64");
    LLVMValueRef buf = LLVMBuildCall2(ctx->builder, malloc_type, malloc_fn, &buf64, 1, "tostr.buf");

    /* Declare sprintf */
    LLVMTypeRef sprintf_args[] = {ptr_type, ptr_type};
    LLVMTypeRef sprintf_type = LLVMFunctionType(i32_type, sprintf_args, 2, 1);
    LLVMValueRef sprintf_fn = LLVMGetNamedFunction(ctx->module, "sprintf");
    if (sprintf_fn == NULL)
    {
        sprintf_fn = LLVMAddFunction(ctx->module, "sprintf", sprintf_type);
    }

    LLVMValueRef fmt_str;
    LLVMValueRef sprintf_call;

    if (arg_type->kind == TYPE_INT || arg_type->kind == TYPE_I8 ||
        arg_type->kind == TYPE_I16 || arg_type->kind == TYPE_I32 ||
        arg_type->kind == TYPE_I64 || arg_type->kind == TYPE_U8 ||
        arg_type->kind == TYPE_U16 || arg_type->kind == TYPE_U32 ||
        arg_type->kind == TYPE_U64)
    {
        /* Integer: use %d format (extend to i64 if needed) */
        LLVMValueRef ival = val;
        if (LLVMGetTypeKind(LLVMTypeOf(val)) != LLVMIntegerTypeKind)
        {
            ival = LLVMBuildZExt(ctx->builder, val, i64_type, "tostr.i64");
        }
        fmt_str = LLVMBuildGlobalStringPtr(ctx->builder, "%d", "tostr.fmt.int");
        LLVMValueRef sp_args[] = {buf, fmt_str, ival};
        sprintf_call = LLVMBuildCall2(ctx->builder, sprintf_type, sprintf_fn, sp_args, 3, "tostr.sp.i");
    }
    else if (arg_type->kind == TYPE_F32 || arg_type->kind == TYPE_F64)
    {
        /* Float: use %.17g format for full precision */
        LLVMValueRef fval = val;
        if (arg_type->kind == TYPE_F32)
        {
            fval = LLVMBuildFPExt(ctx->builder, val, LLVMDoubleTypeInContext(ctx->context), "tostr.f64");
        }
        fmt_str = LLVMBuildGlobalStringPtr(ctx->builder, "%.17g", "tostr.fmt.f64");
        LLVMValueRef sp_args[] = {buf, fmt_str, fval};
        sprintf_call = LLVMBuildCall2(ctx->builder, sprintf_type, sprintf_fn, sp_args, 3, "tostr.sp.f");
    }
    else if (arg_type->kind == TYPE_BOOL)
    {
        /* Bool: conditional string "true" or "false" */
        LLVMValueRef true_str = LLVMBuildGlobalStringPtr(ctx->builder, "true", "tostr.true");
        LLVMValueRef false_str = LLVMBuildGlobalStringPtr(ctx->builder, "false", "tostr.false");
        LLVMValueRef cmp = LLVMBuildICmp(ctx->builder, LLVMIntEQ, val,
                                         LLVMConstInt(LLVMTypeOf(val), 1, 0), "tostr.is_true");
        LLVMValueRef selected_str = LLVMBuildSelect(ctx->builder, cmp, true_str, false_str, "tostr.sel");

        /* Copy selected string to buf using memcpy */
        LLVMValueRef strlen_fn = LLVMGetNamedFunction(ctx->module, "strlen");
        LLVMTypeRef strlen_type = LLVMGlobalGetValueType(strlen_fn);
        LLVMValueRef src_len64 = LLVMBuildCall2(ctx->builder, strlen_type, strlen_fn,
                                                &selected_str, 1, "tostr.slen64");
        LLVMValueRef src_len = LLVMBuildTrunc(ctx->builder, src_len64, i32_type, "tostr.slen");

        LLVMValueRef memcpy_fn = LLVMGetNamedFunction(ctx->module, "memcpy");
        LLVMTypeRef memcpy_type = LLVMGlobalGetValueType(memcpy_fn);
        LLVMValueRef copy_len = LLVMBuildAdd(ctx->builder, src_len,
                                             LLVMConstInt(i32_type, 1, 0), "tostr.cplen");
        LLVMValueRef copy_len64 = LLVMBuildZExt(ctx->builder, copy_len, i64_type, "tostr.cp64");
        LLVMValueRef mc_args[] = {buf, selected_str, copy_len64};
        LLVMBuildCall2(ctx->builder, memcpy_type, memcpy_fn, mc_args, 3, "");

        /* strlen of buf */
        LLVMValueRef len64 = LLVMBuildCall2(ctx->builder, strlen_type, strlen_fn, &buf, 1, "tostr.len64");
        LLVMValueRef len = LLVMBuildTrunc(ctx->builder, len64, i32_type, "tostr.len");

        /* Build LsString with cap=16 (dynamic allocation) */
        LLVMValueRef cap = LLVMConstInt(i32_type, 16, 0);
        return cg_push_temp_string(ctx, ls_string_make(ctx, buf, len, cap));
    }
    else
    {
        cg_error(ctx, node->line, node->column, "to_string() requires numeric or bool type");
        return NULL;
    }

    /* Measure result length with strlen(buf) */
    LLVMValueRef strlen_fn = LLVMGetNamedFunction(ctx->module, "strlen");
    LLVMTypeRef strlen_type = LLVMGlobalGetValueType(strlen_fn);
    LLVMValueRef len64 = LLVMBuildCall2(ctx->builder, strlen_type, strlen_fn, &buf, 1, "tostr.len64");
    LLVMValueRef len = LLVMBuildTrunc(ctx->builder, len64, i32_type, "tostr.len");

    /* Build LsString: cap = next_power_of_2(len+1), min 16 */
    LLVMValueRef one = LLVMConstInt(i32_type, 1, 0);
    LLVMValueRef need = LLVMBuildAdd(ctx->builder, len, one, "tostr.need");
    LLVMValueRef min_cap = LLVMConstInt(i32_type, 16, 0);
    LLVMValueRef gt = LLVMBuildICmp(ctx->builder, LLVMIntUGT, need, min_cap, "tostr.gt");
    LLVMValueRef cap = LLVMBuildSelect(ctx->builder, gt, need, min_cap, "tostr.cap");

    return cg_push_temp_string(ctx, ls_string_make(ctx, buf, len, cap));
}

/* Codegen for from_int(string) -> int builtin */
static LLVMValueRef codegen_from_int(CodegenContext *ctx, AstNode *node)
{
    if (node->as.call.arg_count != 1)
        return NULL;
    AstNode *arg = node->as.call.args[0];
    LLVMValueRef str_val = codegen_expr(ctx, arg);
    if (str_val == NULL)
        return NULL;

    LLVMValueRef s_data = ls_string_data(ctx, str_val);

    /* Declare atoi if not already declared */
    LLVMTypeRef i32_type = LLVMInt32TypeInContext(ctx->context);
    LLVMTypeRef ptr_type = LLVMPointerType(LLVMInt8TypeInContext(ctx->context), 0);
    LLVMValueRef atoi_fn = LLVMGetNamedFunction(ctx->module, "atoi");
    if (atoi_fn == NULL)
    {
        LLVMTypeRef atoi_type = LLVMFunctionType(i32_type, &ptr_type, 1, 0);
        atoi_fn = LLVMAddFunction(ctx->module, "atoi", atoi_type);
    }

    LLVMValueRef result = LLVMBuildCall2(ctx->builder,
                                         LLVMGlobalGetValueType(atoi_fn), atoi_fn, &s_data, 1, "from_int");
    return result;
}

/* Codegen for from_float(string) -> f64 builtin */
static LLVMValueRef codegen_from_float(CodegenContext *ctx, AstNode *node)
{
    if (node->as.call.arg_count != 1)
        return NULL;
    AstNode *arg = node->as.call.args[0];
    LLVMValueRef str_val = codegen_expr(ctx, arg);
    if (str_val == NULL)
        return NULL;

    LLVMValueRef s_data = ls_string_data(ctx, str_val);

    /* Declare atof if not already declared */
    LLVMTypeRef f64_type = LLVMDoubleTypeInContext(ctx->context);
    LLVMTypeRef ptr_type = LLVMPointerType(LLVMInt8TypeInContext(ctx->context), 0);
    LLVMValueRef atof_fn = LLVMGetNamedFunction(ctx->module, "atof");
    if (atof_fn == NULL)
    {
        LLVMTypeRef atof_type = LLVMFunctionType(f64_type, &ptr_type, 1, 0);
        atof_fn = LLVMAddFunction(ctx->module, "atof", atof_type);
    }

    LLVMValueRef result = LLVMBuildCall2(ctx->builder,
                                         LLVMGlobalGetValueType(atof_fn), atof_fn, &s_data, 1, "from_float");
    return result;
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

    /* null path: build empty static LsString and skip the malloc/memcpy. */
    LLVMPositionBuilderAtEnd(ctx->builder, null_bb);
    LLVMValueRef empty_str = ls_string_from_literal(ctx, "", "fromcstr.empty");
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

    LLVMValueRef ok_str = ls_string_make(ctx, buf, len32, cap32);
    LLVMBuildBr(ctx->builder, cont_bb);

    /* phi the two paths */
    LLVMPositionBuilderAtEnd(ctx->builder, cont_bb);
    LLVMTypeRef ls_str_t = ls_string_type(ctx);
    LLVMValueRef phi = LLVMBuildPhi(ctx->builder, ls_str_t, "fromcstr.r");
    LLVMValueRef vals[2] = { empty_str, ok_str };
    LLVMBasicBlockRef blks[2] = { null_bb, ok_bb };
    LLVMAddIncoming(phi, vals, blks, 2);

    /* Register as a temp so scope cleanup will free the heap (cap > 0). */
    cg_push_temp_string(ctx, phi);
    (void)i8_t;
    return phi;
}

/* ---- vec[i] auto-borrow for string elements ---- */

/* Borrow a string element from vec[i] WITHOUT deep-cloning it.
   Safe only in read-only contexts: print, string method calls, comparisons,
   f-string interpolation, and concatenation operands.
   Returns an LsString SSA value with cap=0 so cleanup code (emit_string_free /
   emit_scope_cleanup) treats it as a static literal and skips freeing it.
   The original vec element's memory is NEVER modified (InsertValue creates a fresh
   SSA value, it does NOT write back to the vec's data buffer). */
static LLVMValueRef codegen_vec_string_borrow(CodegenContext *ctx, AstNode *node)
{
    AstNode *obj = node->as.index_expr.object;
    AstNode *idx_node = node->as.index_expr.index;
    Type *obj_type = obj->resolved_type;
    Type *elem_type = obj_type->as.vec.elem; /* TYPE_STRING */

    LLVMTypeRef elem_llvm = type_to_llvm(ctx, elem_type);
    LLVMTypeRef vec_t = ls_vec_type(ctx);
    LLVMTypeRef i32_t = LLVMInt32TypeInContext(ctx->context);
    LLVMTypeRef i64_t = LLVMInt64TypeInContext(ctx->context);

    /* Resolve vec alloca (only AST_IDENT supported, same as regular AST_INDEX) */
    LLVMValueRef vec_alloca = NULL;
    if (obj->kind == AST_IDENT)
    {
        CgSymbol *sym = cg_scope_resolve(ctx->current_scope, obj->as.ident.name);
        if (sym)
            vec_alloca = sym->value;
    }
    if (vec_alloca == NULL)
    {
        cg_error(ctx, node->line, node->column, "auto-borrow: cannot get address of vec");
        return NULL;
    }

    /* Load vec fields */
    LLVMValueRef vec_val = LLVMBuildLoad2(ctx->builder, vec_t, vec_alloca, "vb.v");
    LLVMValueRef data_ptr = LLVMBuildExtractValue(ctx->builder, vec_val, 0, "vb.data");
    LLVMValueRef len_val = LLVMBuildExtractValue(ctx->builder, vec_val, 1, "vb.len");

    /* Evaluate index expression */
    LLVMValueRef index = codegen_expr(ctx, idx_node);
    if (index == NULL)
        return NULL;
    if (LLVMTypeOf(index) != i64_t)
        index = LLVMBuildSExtOrBitCast(ctx->builder, index, i64_t, "vb.idx");

    /* Bounds check: 0 <= index < len */
    LLVMValueRef len64 = LLVMBuildSExt(ctx->builder, len_val, i64_t, "vb.len64");
    LLVMValueRef zero64 = LLVMConstInt(i64_t, 0, 0);
    LLVMValueRef ge_zero = LLVMBuildICmp(ctx->builder, LLVMIntSGE, index, zero64, "vb.ge0");
    LLVMValueRef lt_len = LLVMBuildICmp(ctx->builder, LLVMIntSLT, index, len64, "vb.ltl");
    LLVMValueRef in_bounds = LLVMBuildAnd(ctx->builder, ge_zero, lt_len, "vb.inb");

    LLVMValueRef cur_fn = LLVMGetBasicBlockParent(LLVMGetInsertBlock(ctx->builder));
    int id = g_block_counter++;
    char ok_name[32], oob_name[32], merge_name[32];
    snprintf(ok_name, sizeof(ok_name), "vb.ok%d", id);
    snprintf(oob_name, sizeof(oob_name), "vb.oob%d", id);
    snprintf(merge_name, sizeof(merge_name), "vb.merge%d", id);
    LLVMBasicBlockRef ok_bb = LLVMAppendBasicBlockInContext(ctx->context, cur_fn, ok_name);
    LLVMBasicBlockRef oob_bb = LLVMAppendBasicBlockInContext(ctx->context, cur_fn, oob_name);
    LLVMBasicBlockRef merge_bb = LLVMAppendBasicBlockInContext(ctx->context, cur_fn, merge_name);

    /* Allocate result slot at function entry (avoids alloca in non-entry block) */
    LLVMBuilderRef tb = LLVMCreateBuilderInContext(ctx->context);
    LLVMBasicBlockRef entry_bb = LLVMGetEntryBasicBlock(cur_fn);
    LLVMValueRef fi = LLVMGetFirstInstruction(entry_bb);
    if (fi)
        LLVMPositionBuilderBefore(tb, fi);
    else
        LLVMPositionBuilderAtEnd(tb, entry_bb);
    LLVMValueRef result_alloca = LLVMBuildAlloca(tb, elem_llvm, "vb.res");
    LLVMDisposeBuilder(tb);

    /* Default value (out-of-bounds path): empty static string */
    LLVMBuildStore(ctx->builder, ls_string_from_literal(ctx, "", "vb.empty"), result_alloca);
    LLVMBuildCondBr(ctx->builder, in_bounds, ok_bb, oob_bb);

    /* ok_bb: GEP + load + zero out cap (borrow — no clone, no ownership transfer) */
    LLVMPositionBuilderAtEnd(ctx->builder, ok_bb);
    LLVMValueRef gep = LLVMBuildGEP2(ctx->builder, elem_llvm, data_ptr, &index, 1, "vb.ep");
    LLVMValueRef elem_raw = LLVMBuildLoad2(ctx->builder, elem_llvm, gep, "vb.elem");
    /* InsertValue produces a NEW SSA value — the vec's memory is untouched.
       cap=0 marks this copy as "static literal" so emit_string_free skips it. */
    LLVMValueRef zero_cap = LLVMConstInt(i32_t, 0, 0);
    LLVMValueRef borrowed = LLVMBuildInsertValue(ctx->builder, elem_raw,
                                                 zero_cap, 2, "vb.borrow");

#if CG_DEBUG
    {
        LLVMValueRef idx32 = LLVMBuildTrunc(ctx->builder, index, i32_t, "vb.dbg.idx");
        LLVMValueRef bdata = ls_string_data(ctx, borrowed);
        LLVMValueRef dbgargs[2] = {idx32, bdata};
        cg_emit_debug_printf(ctx, "[cg] vec[i].borrow  idx=%d ptr=%p\n", dbgargs, 2);
    }
#endif

    LLVMBuildStore(ctx->builder, borrowed, result_alloca);
    LLVMBuildBr(ctx->builder, merge_bb);

    /* oob_bb: out-of-bounds — warn, result stays as empty string */
    LLVMPositionBuilderAtEnd(ctx->builder, oob_bb);
    {
        LLVMValueRef oob_idx = LLVMBuildTrunc(ctx->builder, index, i32_t, "vb.oob.idx");
        LLVMValueRef warn_args[2] = {oob_idx, len_val};
        emit_printf(ctx, "[warning] vec index out of bounds: index=%d, len=%d\n", warn_args, 2);
    }
    LLVMBuildBr(ctx->builder, merge_bb);

    /* merge_bb: load result — NOT registered in temp_string_slots (non-owned borrow) */
    LLVMPositionBuilderAtEnd(ctx->builder, merge_bb);
    return LLVMBuildLoad2(ctx->builder, elem_llvm, result_alloca, "vb.r");
}

/* Evaluate `node`, using auto-borrow when the node is a vec(string)[i] access
   in a read-only context.  Falls back to full codegen_expr for everything else. */
static LLVMValueRef codegen_expr_or_borrow(CodegenContext *ctx, AstNode *node)
{
    if (is_vec_string_index(node))
        return codegen_vec_string_borrow(ctx, node);
    return codegen_expr(ctx, node);
}

/* ---- String method codegen (Batch 1: query methods, no allocation) ---- */

/* Generate LLVM IR for string builtin method calls.
   Returns the result value, or NULL on error. */
static LLVMValueRef codegen_string_method(CodegenContext *ctx, AstNode *node)
{
    AstNode *obj_node = node->as.call.callee->as.field_access.object;
    const char *method = node->as.call.callee->as.field_access.field;
    /* Use auto-borrow when the object is a vec(string)[i] — avoids a deep clone
       because string methods only read the source content. */
    LLVMValueRef str_val = codegen_expr_or_borrow(ctx, obj_node);
    if (str_val == NULL)
        return NULL;

    LLVMValueRef s_data = ls_string_data(ctx, str_val);
    LLVMValueRef s_len = ls_string_len(ctx, str_val);
    LLVMTypeRef i32_type = LLVMInt32TypeInContext(ctx->context);
    LLVMTypeRef ptr_type = LLVMPointerTypeInContext(ctx->context, 0);

    /* s.empty() -> bool: len == 0 */
    if (strcmp(method, "empty") == 0)
    {
        LLVMValueRef zero = LLVMConstInt(i32_type, 0, 0);
        return LLVMBuildICmp(ctx->builder, LLVMIntEQ, s_len, zero, "empty");
    }

    /* Phase E.3.3: s.to_cstr() -> object  — return raw i8* for FFI.
       Caller must NOT free; the LS string retains ownership. The buffer
       always has a NUL terminator (managed strings are heap-allocated as
       len+1 bytes; static strings come from .rodata which is also NUL-terminated). */
    if (strcmp(method, "to_cstr") == 0)
    {
        return s_data;
    }

    /* s.at(int i) -> int: bounds-checked load.
       - len == 0: print warning, return 0
       - idx < 0 or idx >= len: print warning, return 0
       - else: return s_data[idx] zero-extended to i32 */
    if (strcmp(method, "at") == 0)
    {
        LLVMValueRef idx = codegen_expr(ctx, node->as.call.args[0]);
        if (idx == NULL)
            return NULL;
        /* Normalize idx to i32 for comparison with len */
        LLVMTypeRef idx_type = LLVMTypeOf(idx);
        if (idx_type != i32_type)
        {
            if (LLVMGetTypeKind(idx_type) == LLVMIntegerTypeKind &&
                LLVMGetIntTypeWidth(idx_type) > 32)
                idx = LLVMBuildTrunc(ctx->builder, idx, i32_type, "at.idx32");
            else
                idx = LLVMBuildSExtOrBitCast(ctx->builder, idx, i32_type, "at.idx32");
        }

        LLVMValueRef zero32 = LLVMConstInt(i32_type, 0, 0);
        LLVMValueRef ge_zero = LLVMBuildICmp(ctx->builder, LLVMIntSGE, idx, zero32, "at.ge0");
        LLVMValueRef lt_len = LLVMBuildICmp(ctx->builder, LLVMIntSLT, idx, s_len, "at.ltlen");
        LLVMValueRef in_bounds = LLVMBuildAnd(ctx->builder, ge_zero, lt_len, "at.inb");

        LLVMValueRef cur_fn = ctx->current_fn;
        LLVMBasicBlockRef ok_bb = LLVMAppendBasicBlockInContext(ctx->context, cur_fn, "at.ok");
        LLVMBasicBlockRef oob_bb = LLVMAppendBasicBlockInContext(ctx->context, cur_fn, "at.oob");
        LLVMBasicBlockRef cont_bb = LLVMAppendBasicBlockInContext(ctx->context, cur_fn, "at.cont");
        LLVMBuildCondBr(ctx->builder, in_bounds, ok_bb, oob_bb);

        /* ok: load byte */
        LLVMPositionBuilderAtEnd(ctx->builder, ok_bb);
        LLVMValueRef gep = LLVMBuildGEP2(ctx->builder,
                                         LLVMInt8TypeInContext(ctx->context), s_data, &idx, 1, "at.ptr");
        LLVMValueRef byte = LLVMBuildLoad2(ctx->builder,
                                           LLVMInt8TypeInContext(ctx->context), gep, "at.byte");
        LLVMValueRef ok_val = LLVMBuildZExt(ctx->builder, byte, i32_type, "at.val");
        LLVMBuildBr(ctx->builder, cont_bb);

        /* oob: emit warning via printf, yield 0 */
        LLVMPositionBuilderAtEnd(ctx->builder, oob_bb);
        {
            LLVMValueRef printf_fn = LLVMGetNamedFunction(ctx->module, "printf");
            LLVMTypeRef printf_ft = LLVMGlobalGetValueType(printf_fn);
            LLVMValueRef fmt = LLVMBuildGlobalStringPtr(ctx->builder,
                "[warning] string.at(%d): index out of range (len=%d)\n", "at.warn.fmt");
            LLVMValueRef pa[3] = {fmt, idx, s_len};
            LLVMBuildCall2(ctx->builder, printf_ft, printf_fn, pa, 3, "");
        }
        LLVMBuildBr(ctx->builder, cont_bb);

        /* cont: phi */
        LLVMPositionBuilderAtEnd(ctx->builder, cont_bb);
        LLVMValueRef phi = LLVMBuildPhi(ctx->builder, i32_type, "at.result");
        LLVMValueRef vals[2] = {ok_val, zero32};
        LLVMBasicBlockRef blks[2] = {ok_bb, oob_bb};
        LLVMAddIncoming(phi, vals, blks, 2);
        return phi;
    }

    /* s.find(string sub) -> int: strstr then pointer subtraction, -1 if NULL */
    if (strcmp(method, "find") == 0)
    {
        LLVMValueRef sub_val = codegen_expr(ctx, node->as.call.args[0]);
        if (sub_val == NULL)
            return NULL;
        LLVMValueRef sub_data = ls_string_data(ctx, sub_val);

        LLVMValueRef strstr_fn = LLVMGetNamedFunction(ctx->module, "strstr");
        LLVMTypeRef strstr_type = LLVMGlobalGetValueType(strstr_fn);
        LLVMValueRef strstr_args[] = {s_data, sub_data};
        LLVMValueRef found = LLVMBuildCall2(ctx->builder, strstr_type, strstr_fn,
                                            strstr_args, 2, "find.ptr");

        /* if found == NULL, return -1; else return found - s_data */
        LLVMValueRef null_ptr = LLVMConstNull(ptr_type);
        LLVMValueRef is_null = LLVMBuildICmp(ctx->builder, LLVMIntEQ,
                                             found, null_ptr, "find.null");

        LLVMValueRef found_int = LLVMBuildPtrToInt(ctx->builder, found,
                                                   LLVMInt64TypeInContext(ctx->context), "find.fint");
        LLVMValueRef base_int = LLVMBuildPtrToInt(ctx->builder, s_data,
                                                  LLVMInt64TypeInContext(ctx->context), "find.bint");
        LLVMValueRef diff64 = LLVMBuildSub(ctx->builder, found_int, base_int, "find.diff");
        LLVMValueRef diff32 = LLVMBuildTrunc(ctx->builder, diff64, i32_type, "find.off");

        LLVMValueRef neg1 = LLVMConstInt(i32_type, (unsigned long long)-1, 1);
        return LLVMBuildSelect(ctx->builder, is_null, neg1, diff32, "find.result");
    }

    /* s.contains(string sub) -> bool: strstr != NULL */
    if (strcmp(method, "contains") == 0)
    {
        LLVMValueRef sub_val = codegen_expr(ctx, node->as.call.args[0]);
        if (sub_val == NULL)
            return NULL;
        LLVMValueRef sub_data = ls_string_data(ctx, sub_val);

        LLVMValueRef strstr_fn = LLVMGetNamedFunction(ctx->module, "strstr");
        LLVMTypeRef strstr_type = LLVMGlobalGetValueType(strstr_fn);
        LLVMValueRef strstr_args[] = {s_data, sub_data};
        LLVMValueRef found = LLVMBuildCall2(ctx->builder, strstr_type, strstr_fn,
                                            strstr_args, 2, "cont.ptr");

        LLVMValueRef null_ptr = LLVMConstNull(ptr_type);
        return LLVMBuildICmp(ctx->builder, LLVMIntNE, found, null_ptr, "contains");
    }

    /* s.starts_with(string prefix) -> bool: strncmp(s.data, pre.data, pre.len) == 0 */
    if (strcmp(method, "starts_with") == 0)
    {
        LLVMValueRef pre_val = codegen_expr(ctx, node->as.call.args[0]);
        if (pre_val == NULL)
            return NULL;
        LLVMValueRef pre_data = ls_string_data(ctx, pre_val);
        LLVMValueRef pre_len = ls_string_len(ctx, pre_val);

        /* Extend pre_len to i64 for strncmp's size_t parameter */
        LLVMValueRef pre_len64 = LLVMBuildZExt(ctx->builder, pre_len,
                                               LLVMInt64TypeInContext(ctx->context), "sw.len64");

        LLVMValueRef strncmp_fn = LLVMGetNamedFunction(ctx->module, "strncmp");
        LLVMTypeRef strncmp_type = LLVMGlobalGetValueType(strncmp_fn);
        LLVMValueRef strncmp_args[] = {s_data, pre_data, pre_len64};
        LLVMValueRef cmp = LLVMBuildCall2(ctx->builder, strncmp_type, strncmp_fn,
                                          strncmp_args, 3, "sw.cmp");

        LLVMValueRef zero = LLVMConstInt(i32_type, 0, 0);
        return LLVMBuildICmp(ctx->builder, LLVMIntEQ, cmp, zero, "starts_with");
    }

    /* s.ends_with(string suffix) -> bool:
       if s.len < suf.len: false
       else: strcmp(s.data + s.len - suf.len, suf.data) == 0 */
    if (strcmp(method, "ends_with") == 0)
    {
        LLVMValueRef suf_val = codegen_expr(ctx, node->as.call.args[0]);
        if (suf_val == NULL)
            return NULL;
        LLVMValueRef suf_data = ls_string_data(ctx, suf_val);
        LLVMValueRef suf_len = ls_string_len(ctx, suf_val);

        /* Check if s.len >= suf.len */
        LLVMValueRef len_ok = LLVMBuildICmp(ctx->builder, LLVMIntSGE,
                                            s_len, suf_len, "ew.lenok");

        /* Compute offset: s.len - suf.len */
        LLVMValueRef offset = LLVMBuildSub(ctx->builder, s_len, suf_len, "ew.off");
        /* GEP to s.data + offset */
        LLVMValueRef tail_ptr = LLVMBuildGEP2(ctx->builder,
                                              LLVMInt8TypeInContext(ctx->context), s_data, &offset, 1, "ew.tail");

        LLVMValueRef strcmp_fn = LLVMGetNamedFunction(ctx->module, "strcmp");
        LLVMTypeRef strcmp_type = LLVMGlobalGetValueType(strcmp_fn);
        LLVMValueRef strcmp_args[] = {tail_ptr, suf_data};
        LLVMValueRef cmp = LLVMBuildCall2(ctx->builder, strcmp_type, strcmp_fn,
                                          strcmp_args, 2, "ew.cmp");

        LLVMValueRef zero = LLVMConstInt(i32_type, 0, 0);
        LLVMValueRef str_eq = LLVMBuildICmp(ctx->builder, LLVMIntEQ,
                                            cmp, zero, "ew.eq");

        /* Final: len_ok AND str_eq */
        return LLVMBuildAnd(ctx->builder, len_ok, str_eq, "ends_with");
    }

    /* s.compare(string other) -> int: strcmp(s.data, other.data) */
    if (strcmp(method, "compare") == 0)
    {
        LLVMValueRef other_val = codegen_expr(ctx, node->as.call.args[0]);
        if (other_val == NULL)
            return NULL;
        LLVMValueRef other_data = ls_string_data(ctx, other_val);

        LLVMValueRef strcmp_fn = LLVMGetNamedFunction(ctx->module, "strcmp");
        LLVMTypeRef strcmp_type = LLVMGlobalGetValueType(strcmp_fn);
        LLVMValueRef strcmp_args[] = {s_data, other_data};
        return LLVMBuildCall2(ctx->builder, strcmp_type, strcmp_fn,
                              strcmp_args, 2, "compare");
    }

    /* ---- Batch 2: methods that allocate new strings ---- */

    LLVMTypeRef i8_type = LLVMInt8TypeInContext(ctx->context);
    LLVMTypeRef i64_type = LLVMInt64TypeInContext(ctx->context);

    /* s.copy() -> string: malloc + memcpy, returns a heap-owned duplicate */
    if (strcmp(method, "copy") == 0)
    {
        LLVMValueRef one = LLVMConstInt(i32_type, 1, 0);
        LLVMValueRef alloc_need = LLVMBuildAdd(ctx->builder, s_len, one, "cp.need");
        LLVMValueRef min_cap = LLVMConstInt(i32_type, LS_MIN_STR_CAP, 0);
        LLVMValueRef need_gt = LLVMBuildICmp(ctx->builder, LLVMIntUGT,
                                             alloc_need, min_cap, "cp.gt");
        LLVMValueRef cap = LLVMBuildSelect(ctx->builder, need_gt,
                                           alloc_need, min_cap, "cp.cap");

        LLVMValueRef cap64 = LLVMBuildZExt(ctx->builder, cap, i64_type, "cp.cap64");
        LLVMValueRef buf = cg_emit_alloc(ctx, cap64, "string.copy",
                                         node->line, node->column);

        /* memcpy(buf, s.data, len + 1) — includes null terminator */
        LLVMValueRef memcpy_fn = LLVMGetNamedFunction(ctx->module, "memcpy");
        LLVMTypeRef mc_type = LLVMGlobalGetValueType(memcpy_fn);
        LLVMValueRef copy_len = LLVMBuildZExt(ctx->builder, alloc_need,
                                              i64_type, "cp.copy64");
        LLVMValueRef mc_args[] = {buf, s_data, copy_len};
        LLVMBuildCall2(ctx->builder, mc_type, memcpy_fn, mc_args, 3, "");

        return cg_push_temp_string(ctx, ls_string_make(ctx, buf, s_len, cap));
    }

    /* s.upper() -> string: malloc + copy + toupper each byte */
    if (strcmp(method, "upper") == 0)
    {
        /* alloc_need = len + 1 */
        LLVMValueRef one = LLVMConstInt(i32_type, 1, 0);
        LLVMValueRef alloc_need = LLVMBuildAdd(ctx->builder, s_len, one, "up.need");
        LLVMValueRef min_cap = LLVMConstInt(i32_type, LS_MIN_STR_CAP, 0);
        LLVMValueRef need_gt = LLVMBuildICmp(ctx->builder, LLVMIntUGT,
                                             alloc_need, min_cap, "up.gt");
        LLVMValueRef cap = LLVMBuildSelect(ctx->builder, need_gt,
                                           alloc_need, min_cap, "up.cap");

        LLVMValueRef cap64 = LLVMBuildZExt(ctx->builder, cap, i64_type, "up.cap64");
        LLVMValueRef buf = cg_emit_alloc(ctx, cap64, "string.upper",
                                         node->line, node->column);

        /* Copy source data first */
        LLVMValueRef memcpy_fn = LLVMGetNamedFunction(ctx->module, "memcpy");
        LLVMTypeRef mc_type = LLVMGlobalGetValueType(memcpy_fn);
        LLVMValueRef copy_len = LLVMBuildZExt(ctx->builder,
                                              LLVMBuildAdd(ctx->builder, s_len, one, "up.copylen"),
                                              i64_type, "up.copy64");
        LLVMValueRef mc_args[] = {buf, s_data, copy_len};
        LLVMBuildCall2(ctx->builder, mc_type, memcpy_fn, mc_args, 3, "");

        /* Loop: for (i = 0; i < len; i++) buf[i] = toupper(buf[i]) */
        LLVMValueRef current_fn = LLVMGetBasicBlockParent(
            LLVMGetInsertBlock(ctx->builder));
        LLVMBasicBlockRef cond_bb = LLVMAppendBasicBlockInContext(
            ctx->context, current_fn, "up.cond");
        LLVMBasicBlockRef body_bb = LLVMAppendBasicBlockInContext(
            ctx->context, current_fn, "up.body");
        LLVMBasicBlockRef end_bb = LLVMAppendBasicBlockInContext(
            ctx->context, current_fn, "up.end");

        /* alloca for loop index */
        LLVMValueRef idx_ptr = LLVMBuildAlloca(ctx->builder, i32_type, "up.idx");
        LLVMBuildStore(ctx->builder, LLVMConstInt(i32_type, 0, 0), idx_ptr);
        LLVMBuildBr(ctx->builder, cond_bb);

        /* cond: i < len */
        LLVMPositionBuilderAtEnd(ctx->builder, cond_bb);
        LLVMValueRef idx = LLVMBuildLoad2(ctx->builder, i32_type, idx_ptr, "up.i");
        LLVMValueRef cmp = LLVMBuildICmp(ctx->builder, LLVMIntSLT,
                                         idx, s_len, "up.cmp");
        LLVMBuildCondBr(ctx->builder, cmp, body_bb, end_bb);

        /* body: buf[i] = toupper(buf[i]) */
        LLVMPositionBuilderAtEnd(ctx->builder, body_bb);
        LLVMValueRef idx2 = LLVMBuildLoad2(ctx->builder, i32_type, idx_ptr, "up.i2");
        LLVMValueRef gep = LLVMBuildGEP2(ctx->builder, i8_type, buf,
                                         &idx2, 1, "up.ptr");
        LLVMValueRef byte = LLVMBuildLoad2(ctx->builder, i8_type, gep, "up.byte");
        /* toupper inline: if ('a' <= ch && ch <= 'z') ch -= 32 */
        LLVMValueRef ch32 = LLVMBuildZExt(ctx->builder, byte, i32_type, "up.ch");
        LLVMValueRef is_lower_a = LLVMBuildICmp(ctx->builder, LLVMIntSGE,
                                                ch32, LLVMConstInt(i32_type, 'a', 0), "up.gea");
        LLVMValueRef is_lower_z = LLVMBuildICmp(ctx->builder, LLVMIntSLE,
                                                ch32, LLVMConstInt(i32_type, 'z', 0), "up.lez");
        LLVMValueRef is_lower = LLVMBuildAnd(ctx->builder, is_lower_a, is_lower_z,
                                             "up.islower");
        LLVMValueRef uppered = LLVMBuildSub(ctx->builder, ch32,
                                            LLVMConstInt(i32_type, 32, 0), "up.upper");
        LLVMValueRef result_ch = LLVMBuildSelect(ctx->builder, is_lower,
                                                 uppered, ch32, "up.sel");
        LLVMValueRef result_byte = LLVMBuildTrunc(ctx->builder, result_ch,
                                                  i8_type, "up.trunc");
        LLVMBuildStore(ctx->builder, result_byte, gep);

        /* i++ */
        LLVMValueRef one_i = LLVMConstInt(i32_type, 1, 0);
        LLVMValueRef next = LLVMBuildAdd(ctx->builder, idx2, one_i, "up.next");
        LLVMBuildStore(ctx->builder, next, idx_ptr);
        LLVMBuildBr(ctx->builder, cond_bb);

        /* end */
        LLVMPositionBuilderAtEnd(ctx->builder, end_bb);
        return cg_push_temp_string(ctx, ls_string_make(ctx, buf, s_len, cap));
    }

    /* s.lower() -> string: malloc + copy + tolower each byte */
    if (strcmp(method, "lower") == 0)
    {
        LLVMValueRef one = LLVMConstInt(i32_type, 1, 0);
        LLVMValueRef alloc_need = LLVMBuildAdd(ctx->builder, s_len, one, "lo.need");
        LLVMValueRef min_cap = LLVMConstInt(i32_type, LS_MIN_STR_CAP, 0);
        LLVMValueRef need_gt = LLVMBuildICmp(ctx->builder, LLVMIntUGT,
                                             alloc_need, min_cap, "lo.gt");
        LLVMValueRef cap = LLVMBuildSelect(ctx->builder, need_gt,
                                           alloc_need, min_cap, "lo.cap");

        LLVMValueRef cap64 = LLVMBuildZExt(ctx->builder, cap, i64_type, "lo.cap64");
        LLVMValueRef buf = cg_emit_alloc(ctx, cap64, "string.lower",
                                         node->line, node->column);

        LLVMValueRef memcpy_fn = LLVMGetNamedFunction(ctx->module, "memcpy");
        LLVMTypeRef mc_type = LLVMGlobalGetValueType(memcpy_fn);
        LLVMValueRef copy_len = LLVMBuildZExt(ctx->builder,
                                              LLVMBuildAdd(ctx->builder, s_len, one, "lo.copylen"),
                                              i64_type, "lo.copy64");
        LLVMValueRef mc_args[] = {buf, s_data, copy_len};
        LLVMBuildCall2(ctx->builder, mc_type, memcpy_fn, mc_args, 3, "");

        /* Loop: for (i = 0; i < len; i++) buf[i] = tolower(buf[i]) */
        LLVMValueRef current_fn = LLVMGetBasicBlockParent(
            LLVMGetInsertBlock(ctx->builder));
        LLVMBasicBlockRef cond_bb = LLVMAppendBasicBlockInContext(
            ctx->context, current_fn, "lo.cond");
        LLVMBasicBlockRef body_bb = LLVMAppendBasicBlockInContext(
            ctx->context, current_fn, "lo.body");
        LLVMBasicBlockRef end_bb = LLVMAppendBasicBlockInContext(
            ctx->context, current_fn, "lo.end");

        LLVMValueRef idx_ptr = LLVMBuildAlloca(ctx->builder, i32_type, "lo.idx");
        LLVMBuildStore(ctx->builder, LLVMConstInt(i32_type, 0, 0), idx_ptr);
        LLVMBuildBr(ctx->builder, cond_bb);

        LLVMPositionBuilderAtEnd(ctx->builder, cond_bb);
        LLVMValueRef idx = LLVMBuildLoad2(ctx->builder, i32_type, idx_ptr, "lo.i");
        LLVMValueRef cmp = LLVMBuildICmp(ctx->builder, LLVMIntSLT,
                                         idx, s_len, "lo.cmp");
        LLVMBuildCondBr(ctx->builder, cmp, body_bb, end_bb);

        LLVMPositionBuilderAtEnd(ctx->builder, body_bb);
        LLVMValueRef idx2 = LLVMBuildLoad2(ctx->builder, i32_type, idx_ptr, "lo.i2");
        LLVMValueRef gep = LLVMBuildGEP2(ctx->builder, i8_type, buf,
                                         &idx2, 1, "lo.ptr");
        LLVMValueRef byte = LLVMBuildLoad2(ctx->builder, i8_type, gep, "lo.byte");
        LLVMValueRef ch32 = LLVMBuildZExt(ctx->builder, byte, i32_type, "lo.ch");
        LLVMValueRef is_upper_a = LLVMBuildICmp(ctx->builder, LLVMIntSGE,
                                                ch32, LLVMConstInt(i32_type, 'A', 0), "lo.geA");
        LLVMValueRef is_upper_z = LLVMBuildICmp(ctx->builder, LLVMIntSLE,
                                                ch32, LLVMConstInt(i32_type, 'Z', 0), "lo.leZ");
        LLVMValueRef is_upper = LLVMBuildAnd(ctx->builder, is_upper_a, is_upper_z,
                                             "lo.isupper");
        LLVMValueRef lowered = LLVMBuildAdd(ctx->builder, ch32,
                                            LLVMConstInt(i32_type, 32, 0), "lo.lower");
        LLVMValueRef result_ch = LLVMBuildSelect(ctx->builder, is_upper,
                                                 lowered, ch32, "lo.sel");
        LLVMValueRef result_byte = LLVMBuildTrunc(ctx->builder, result_ch,
                                                  i8_type, "lo.trunc");
        LLVMBuildStore(ctx->builder, result_byte, gep);

        LLVMValueRef one_i = LLVMConstInt(i32_type, 1, 0);
        LLVMValueRef next = LLVMBuildAdd(ctx->builder, idx2, one_i, "lo.next");
        LLVMBuildStore(ctx->builder, next, idx_ptr);
        LLVMBuildBr(ctx->builder, cond_bb);

        LLVMPositionBuilderAtEnd(ctx->builder, end_bb);
        return cg_push_temp_string(ctx, ls_string_make(ctx, buf, s_len, cap));
    }

    /* s.substr(int start[, int len]) -> string: extract substring */
    if (strcmp(method, "substr") == 0)
    {
        LLVMValueRef raw_start = codegen_expr(ctx, node->as.call.args[0]);
        if (raw_start == NULL)
            return NULL;
        LLVMValueRef start_val, sub_len;
        if (node->as.call.arg_count == 1)
        {
            /* Single-arg: substr(start) — from start to end, clamp to [0, s_len] */
            LLVMValueRef zero32 = LLVMConstInt(i32_type, 0, 0);
            LLVMValueRef gt_len = LLVMBuildICmp(ctx->builder, LLVMIntSGT,
                                                raw_start, s_len, "ss1.gt");
            LLVMValueRef hi = LLVMBuildSelect(ctx->builder, gt_len,
                                              s_len, raw_start, "ss1.hi");
            LLVMValueRef lt_zero = LLVMBuildICmp(ctx->builder, LLVMIntSLT,
                                                 hi, zero32, "ss1.lt");
            start_val = LLVMBuildSelect(ctx->builder, lt_zero, zero32, hi, "ss1.sc");
            sub_len = LLVMBuildSub(ctx->builder, s_len, start_val, "ss1.len");
        }
        else
        {
            start_val = raw_start;
            sub_len = codegen_expr(ctx, node->as.call.args[1]);
            if (sub_len == NULL)
                return NULL;
        }

        /* alloc_need = sub_len + 1 */
        LLVMValueRef one = LLVMConstInt(i32_type, 1, 0);
        LLVMValueRef alloc_need = LLVMBuildAdd(ctx->builder, sub_len, one, "ss.need");
        LLVMValueRef min_cap = LLVMConstInt(i32_type, LS_MIN_STR_CAP, 0);
        LLVMValueRef need_gt = LLVMBuildICmp(ctx->builder, LLVMIntUGT,
                                             alloc_need, min_cap, "ss.gt");
        LLVMValueRef cap = LLVMBuildSelect(ctx->builder, need_gt,
                                           alloc_need, min_cap, "ss.cap");

        LLVMValueRef cap64 = LLVMBuildZExt(ctx->builder, cap, i64_type, "ss.cap64");
        LLVMValueRef buf = cg_emit_alloc(ctx, cap64, "string.substr",
                                         node->line, node->column);

        /* memcpy(buf, s.data + start, sub_len) */
        LLVMValueRef src_ptr = LLVMBuildGEP2(ctx->builder, i8_type, s_data,
                                             &start_val, 1, "ss.src");
        LLVMValueRef memcpy_fn = LLVMGetNamedFunction(ctx->module, "memcpy");
        LLVMTypeRef mc_type = LLVMGlobalGetValueType(memcpy_fn);
        LLVMValueRef sub_len64 = LLVMBuildZExt(ctx->builder, sub_len,
                                               i64_type, "ss.len64");
        LLVMValueRef mc_args[] = {buf, src_ptr, sub_len64};
        LLVMBuildCall2(ctx->builder, mc_type, memcpy_fn, mc_args, 3, "");

        /* null-terminate: buf[sub_len] = 0 */
        LLVMValueRef end_ptr = LLVMBuildGEP2(ctx->builder, i8_type, buf,
                                             &sub_len, 1, "ss.end");
        LLVMBuildStore(ctx->builder, LLVMConstInt(i8_type, 0, 0), end_ptr);

        return cg_push_temp_string(ctx, ls_string_make(ctx, buf, sub_len, cap));
    }

    /* s.trim() -> string: trim leading/trailing whitespace */
    if (strcmp(method, "trim") == 0)
    {
        /* We build this with basic blocks:
           1. Scan forward to find first non-space (start)
           2. Scan backward to find last non-space (end)
           3. new_len = end - start
           4. malloc + memcpy + null-terminate */
        LLVMValueRef current_fn_val = LLVMGetBasicBlockParent(
            LLVMGetInsertBlock(ctx->builder));

        /* Alloca for start and end indices */
        LLVMValueRef start_ptr = LLVMBuildAlloca(ctx->builder, i32_type, "tr.start");
        LLVMValueRef end_ptr = LLVMBuildAlloca(ctx->builder, i32_type, "tr.end");
        LLVMBuildStore(ctx->builder, LLVMConstInt(i32_type, 0, 0), start_ptr);
        LLVMValueRef len_minus1 = LLVMBuildSub(ctx->builder, s_len,
                                               LLVMConstInt(i32_type, 1, 0), "tr.lm1");
        LLVMBuildStore(ctx->builder, len_minus1, end_ptr);

        /* Forward scan: while (start < len && isspace(s[start])) start++ */
        LLVMBasicBlockRef fwd_cond = LLVMAppendBasicBlockInContext(
            ctx->context, current_fn_val, "tr.fwd.cond");
        LLVMBasicBlockRef fwd_body = LLVMAppendBasicBlockInContext(
            ctx->context, current_fn_val, "tr.fwd.body");
        LLVMBasicBlockRef fwd_end = LLVMAppendBasicBlockInContext(
            ctx->context, current_fn_val, "tr.fwd.end");

        LLVMBuildBr(ctx->builder, fwd_cond);
        LLVMPositionBuilderAtEnd(ctx->builder, fwd_cond);
        LLVMValueRef fs = LLVMBuildLoad2(ctx->builder, i32_type, start_ptr, "tr.fs");
        LLVMValueRef fwd_in_range = LLVMBuildICmp(ctx->builder, LLVMIntSLT,
                                                  fs, s_len, "tr.fir");
        /* Load byte and check if whitespace (space, tab, newline, cr) */
        LLVMValueRef fwd_gep = LLVMBuildGEP2(ctx->builder, i8_type, s_data,
                                             &fs, 1, "tr.fgep");
        LLVMValueRef fwd_byte = LLVMBuildLoad2(ctx->builder, i8_type, fwd_gep,
                                               "tr.fbyte");
        LLVMValueRef fwd_ch = LLVMBuildZExt(ctx->builder, fwd_byte, i32_type, "tr.fch");
        /* isspace: ch==' ' || ch=='\t' || ch=='\n' || ch=='\r' */
        LLVMValueRef is_sp = LLVMBuildICmp(ctx->builder, LLVMIntEQ, fwd_ch,
                                           LLVMConstInt(i32_type, ' ', 0), "tr.sp");
        LLVMValueRef is_tab = LLVMBuildICmp(ctx->builder, LLVMIntEQ, fwd_ch,
                                            LLVMConstInt(i32_type, '\t', 0), "tr.tab");
        LLVMValueRef is_nl = LLVMBuildICmp(ctx->builder, LLVMIntEQ, fwd_ch,
                                           LLVMConstInt(i32_type, '\n', 0), "tr.nl");
        LLVMValueRef is_cr = LLVMBuildICmp(ctx->builder, LLVMIntEQ, fwd_ch,
                                           LLVMConstInt(i32_type, '\r', 0), "tr.cr");
        LLVMValueRef is_ws = LLVMBuildOr(ctx->builder,
                                         LLVMBuildOr(ctx->builder, is_sp, is_tab, "tr.or1"),
                                         LLVMBuildOr(ctx->builder, is_nl, is_cr, "tr.or2"), "tr.isws");
        LLVMValueRef fwd_cont = LLVMBuildAnd(ctx->builder, fwd_in_range, is_ws,
                                             "tr.fcont");
        LLVMBuildCondBr(ctx->builder, fwd_cont, fwd_body, fwd_end);

        LLVMPositionBuilderAtEnd(ctx->builder, fwd_body);
        LLVMValueRef fs2 = LLVMBuildLoad2(ctx->builder, i32_type, start_ptr, "tr.fs2");
        LLVMBuildStore(ctx->builder,
                       LLVMBuildAdd(ctx->builder, fs2, LLVMConstInt(i32_type, 1, 0), "tr.fsinc"),
                       start_ptr);
        LLVMBuildBr(ctx->builder, fwd_cond);

        LLVMPositionBuilderAtEnd(ctx->builder, fwd_end);

        /* Backward scan: while (end >= start && isspace(s[end])) end-- */
        LLVMBasicBlockRef bwd_cond = LLVMAppendBasicBlockInContext(
            ctx->context, current_fn_val, "tr.bwd.cond");
        LLVMBasicBlockRef bwd_body = LLVMAppendBasicBlockInContext(
            ctx->context, current_fn_val, "tr.bwd.body");
        LLVMBasicBlockRef bwd_end = LLVMAppendBasicBlockInContext(
            ctx->context, current_fn_val, "tr.bwd.end");

        LLVMBuildBr(ctx->builder, bwd_cond);
        LLVMPositionBuilderAtEnd(ctx->builder, bwd_cond);
        LLVMValueRef be = LLVMBuildLoad2(ctx->builder, i32_type, end_ptr, "tr.be");
        LLVMValueRef bs_val = LLVMBuildLoad2(ctx->builder, i32_type, start_ptr, "tr.bs");
        LLVMValueRef bwd_in_range = LLVMBuildICmp(ctx->builder, LLVMIntSGE,
                                                  be, bs_val, "tr.bir");
        LLVMValueRef bwd_gep = LLVMBuildGEP2(ctx->builder, i8_type, s_data,
                                             &be, 1, "tr.bgep");
        LLVMValueRef bwd_byte = LLVMBuildLoad2(ctx->builder, i8_type, bwd_gep,
                                               "tr.bbyte");
        LLVMValueRef bwd_ch = LLVMBuildZExt(ctx->builder, bwd_byte, i32_type, "tr.bch");
        LLVMValueRef b_sp = LLVMBuildICmp(ctx->builder, LLVMIntEQ, bwd_ch,
                                          LLVMConstInt(i32_type, ' ', 0), "tr.bsp");
        LLVMValueRef b_tab = LLVMBuildICmp(ctx->builder, LLVMIntEQ, bwd_ch,
                                           LLVMConstInt(i32_type, '\t', 0), "tr.btab");
        LLVMValueRef b_nl = LLVMBuildICmp(ctx->builder, LLVMIntEQ, bwd_ch,
                                          LLVMConstInt(i32_type, '\n', 0), "tr.bnl");
        LLVMValueRef b_cr = LLVMBuildICmp(ctx->builder, LLVMIntEQ, bwd_ch,
                                          LLVMConstInt(i32_type, '\r', 0), "tr.bcr");
        LLVMValueRef b_ws = LLVMBuildOr(ctx->builder,
                                        LLVMBuildOr(ctx->builder, b_sp, b_tab, "tr.bor1"),
                                        LLVMBuildOr(ctx->builder, b_nl, b_cr, "tr.bor2"), "tr.bisws");
        LLVMValueRef bwd_cont = LLVMBuildAnd(ctx->builder, bwd_in_range, b_ws,
                                             "tr.bcont");
        LLVMBuildCondBr(ctx->builder, bwd_cont, bwd_body, bwd_end);

        LLVMPositionBuilderAtEnd(ctx->builder, bwd_body);
        LLVMValueRef be2 = LLVMBuildLoad2(ctx->builder, i32_type, end_ptr, "tr.be2");
        LLVMBuildStore(ctx->builder,
                       LLVMBuildSub(ctx->builder, be2, LLVMConstInt(i32_type, 1, 0), "tr.bedec"),
                       end_ptr);
        LLVMBuildBr(ctx->builder, bwd_cond);

        LLVMPositionBuilderAtEnd(ctx->builder, bwd_end);

        /* new_len = end - start + 1 (if end >= start, else 0) */
        LLVMValueRef final_start = LLVMBuildLoad2(ctx->builder, i32_type, start_ptr,
                                                  "tr.fstart");
        LLVMValueRef final_end = LLVMBuildLoad2(ctx->builder, i32_type, end_ptr,
                                                "tr.fend");
        LLVMValueRef raw_len = LLVMBuildSub(ctx->builder, final_end, final_start,
                                            "tr.rawlen");
        LLVMValueRef new_len = LLVMBuildAdd(ctx->builder, raw_len,
                                            LLVMConstInt(i32_type, 1, 0), "tr.newlen");
        /* Clamp: if end < start, len = 0 */
        LLVMValueRef valid = LLVMBuildICmp(ctx->builder, LLVMIntSGE,
                                           final_end, final_start, "tr.valid");
        new_len = LLVMBuildSelect(ctx->builder, valid, new_len,
                                  LLVMConstInt(i32_type, 0, 0), "tr.clamped");

        /* Allocate and copy */
        LLVMValueRef one = LLVMConstInt(i32_type, 1, 0);
        LLVMValueRef alloc_need = LLVMBuildAdd(ctx->builder, new_len, one, "tr.alneed");
        LLVMValueRef min_cap = LLVMConstInt(i32_type, LS_MIN_STR_CAP, 0);
        LLVMValueRef tr_gt = LLVMBuildICmp(ctx->builder, LLVMIntUGT,
                                           alloc_need, min_cap, "tr.gt");
        LLVMValueRef cap = LLVMBuildSelect(ctx->builder, tr_gt,
                                           alloc_need, min_cap, "tr.cap");

        LLVMValueRef cap64 = LLVMBuildZExt(ctx->builder, cap, i64_type, "tr.cap64");
        LLVMValueRef buf = cg_emit_alloc(ctx, cap64, "string.trim",
                                         node->line, node->column);

        LLVMValueRef src_ptr2 = LLVMBuildGEP2(ctx->builder, i8_type, s_data,
                                              &final_start, 1, "tr.src");
        LLVMValueRef memcpy_fn = LLVMGetNamedFunction(ctx->module, "memcpy");
        LLVMTypeRef mc_type = LLVMGlobalGetValueType(memcpy_fn);
        LLVMValueRef new_len64 = LLVMBuildZExt(ctx->builder, new_len,
                                               i64_type, "tr.nlen64");
        LLVMValueRef mc_args[] = {buf, src_ptr2, new_len64};
        LLVMBuildCall2(ctx->builder, mc_type, memcpy_fn, mc_args, 3, "");

        /* null-terminate */
        LLVMValueRef term_ptr = LLVMBuildGEP2(ctx->builder, i8_type, buf,
                                              &new_len, 1, "tr.term");
        LLVMBuildStore(ctx->builder, LLVMConstInt(i8_type, 0, 0), term_ptr);

        return cg_push_temp_string(ctx, ls_string_make(ctx, buf, new_len, cap));
    }

    /* s.append(string|char|int) -> void: append in-place, growing buffer if needed */
    if (strcmp(method, "append") == 0)
    {
        if (node->as.call.arg_count < 1)
        {
            fprintf(stderr, "[codegen] string.append requires 1 argument\n");
            return NULL;
        }
        /* Require a named variable as receiver (need an alloca to mutate) */
        if (obj_node->kind != AST_IDENT)
        {
            fprintf(stderr, "[codegen] string.append: receiver must be a named variable\n");
            return NULL;
        }
        CgSymbol *recv_sym = cg_scope_resolve(ctx->current_scope, obj_node->as.ident.name);
        if (recv_sym == NULL)
        {
            fprintf(stderr, "[codegen] string.append: cannot resolve '%s'\n",
                    obj_node->as.ident.name);
            return NULL;
        }
        LLVMValueRef str_alloca = recv_sym->value;

        AstNode *arg_node = node->as.call.args[0];
        Type *arg_type = arg_node->resolved_type;

        if (arg_type != NULL && arg_type->kind == TYPE_STRING)
        {
            /* append another string */
            LLVMValueRef arg_val = codegen_expr(ctx, arg_node);
            if (arg_val == NULL)
                return NULL;
            LLVMValueRef suf_data = ls_string_data(ctx, arg_val);
            LLVMValueRef suf_len = ls_string_len(ctx, arg_val);
            emit_string_append_inline(ctx, str_alloca, suf_data, suf_len);
        }
        else /* TYPE_CHAR or TYPE_INT — treat as single byte */
        {
            LLVMTypeRef i8_t = LLVMInt8TypeInContext(ctx->context);
            LLVMTypeRef i32_t = LLVMInt32TypeInContext(ctx->context);
            LLVMValueRef char_val = codegen_expr(ctx, arg_node);
            if (char_val == NULL)
                return NULL;
            LLVMValueRef char_i8 = LLVMBuildTrunc(ctx->builder, char_val, i8_t, "ap.ci8");
            /* Store char into a stack slot so we have an i8* to pass */
            LLVMValueRef char_slot = LLVMBuildAlloca(ctx->builder, i8_t, "ap.slot");
            LLVMBuildStore(ctx->builder, char_i8, char_slot);
            LLVMValueRef one32 = LLVMConstInt(i32_t, 1, 0);
            emit_string_append_inline(ctx, str_alloca, char_slot, one32);
            (void)i32_t;
        }
        return NULL; /* void */
    }

    /* s.replace(string old, string new) -> string: replace all occurrences */
    if (strcmp(method, "replace") == 0)
    {
        LLVMValueRef old_val = codegen_expr(ctx, node->as.call.args[0]);
        if (old_val == NULL)
            return NULL;
        LLVMValueRef new_val = codegen_expr(ctx, node->as.call.args[1]);
        if (new_val == NULL)
            return NULL;

        LLVMValueRef old_data = ls_string_data(ctx, old_val);
        LLVMValueRef old_len = ls_string_len(ctx, old_val);
        LLVMValueRef new_data = ls_string_data(ctx, new_val);
        LLVMValueRef new_len = ls_string_len(ctx, new_val);

        /* Call the runtime helper __ls_str_replace(s_data, s_len, old_data, old_len,
           new_data, new_len, out_len_ptr) -> i8* */
        LLVMValueRef replace_fn = LLVMGetNamedFunction(ctx->module, "__ls_str_replace");
        LLVMTypeRef replace_type = LLVMGlobalGetValueType(replace_fn);

        LLVMValueRef out_len_ptr = LLVMBuildAlloca(ctx->builder, i32_type, "rp.outlen");
        LLVMValueRef rp_args[] = {
            s_data, s_len, old_data, old_len, new_data, new_len, out_len_ptr};
        LLVMValueRef result_buf = LLVMBuildCall2(ctx->builder, replace_type,
                                                 replace_fn, rp_args, 7, "rp.buf");

        LLVMValueRef result_len = LLVMBuildLoad2(ctx->builder, i32_type,
                                                 out_len_ptr, "rp.rlen");

        /* Compute cap: max(LS_MIN_STR_CAP, result_len + 1) */
        LLVMValueRef one = LLVMConstInt(i32_type, 1, 0);
        LLVMValueRef alloc_need = LLVMBuildAdd(ctx->builder, result_len, one,
                                               "rp.need");
        LLVMValueRef min_cap = LLVMConstInt(i32_type, LS_MIN_STR_CAP, 0);
        LLVMValueRef rp_gt = LLVMBuildICmp(ctx->builder, LLVMIntUGT,
                                           alloc_need, min_cap, "rp.gt");
        LLVMValueRef cap = LLVMBuildSelect(ctx->builder, rp_gt,
                                           alloc_need, min_cap, "rp.cap");

        return cg_push_temp_string(ctx, ls_string_make(ctx, result_buf, result_len, cap));
    }

    /* ---- Batch 3: rfind / count / split / join ---- */

    /* s.rfind(string sub) -> int: index of last occurrence, or -1 */
    if (strcmp(method, "rfind") == 0)
    {
        LLVMValueRef sub_val = codegen_expr(ctx, node->as.call.args[0]);
        if (sub_val == NULL)
            return NULL;
        LLVMValueRef sub_data_v = ls_string_data(ctx, sub_val);
        LLVMValueRef sub_len_v = ls_string_len(ctx, sub_val);

        LLVMValueRef cur_fn = LLVMGetBasicBlockParent(LLVMGetInsertBlock(ctx->builder));
        LLVMValueRef last_ptr = LLVMBuildAlloca(ctx->builder, ptr_type, "rf.last");
        LLVMValueRef p_ptr = LLVMBuildAlloca(ctx->builder, ptr_type, "rf.p");
        LLVMBuildStore(ctx->builder, LLVMConstNull(ptr_type), last_ptr);
        LLVMBuildStore(ctx->builder, s_data, p_ptr);

        LLVMBasicBlockRef rf_cond = LLVMAppendBasicBlockInContext(ctx->context, cur_fn, "rf.cond");
        LLVMBasicBlockRef rf_body = LLVMAppendBasicBlockInContext(ctx->context, cur_fn, "rf.body");
        LLVMBasicBlockRef rf_end = LLVMAppendBasicBlockInContext(ctx->context, cur_fn, "rf.end");
        LLVMBuildBr(ctx->builder, rf_cond);

        /* rf.cond: q = strstr(p, sub); if NULL goto rf.end */
        LLVMPositionBuilderAtEnd(ctx->builder, rf_cond);
        LLVMValueRef rf_p = LLVMBuildLoad2(ctx->builder, ptr_type, p_ptr, "rf.pv");
        LLVMValueRef rf_strstr = LLVMGetNamedFunction(ctx->module, "strstr");
        LLVMTypeRef rf_sst = LLVMGlobalGetValueType(rf_strstr);
        LLVMValueRef rf_ss_args[] = {rf_p, sub_data_v};
        LLVMValueRef rf_q = LLVMBuildCall2(ctx->builder, rf_sst, rf_strstr,
                                           rf_ss_args, 2, "rf.q");
        LLVMValueRef rf_null = LLVMConstNull(ptr_type);
        LLVMValueRef rf_isnull = LLVMBuildICmp(ctx->builder, LLVMIntEQ,
                                               rf_q, rf_null, "rf.null");
        LLVMBuildCondBr(ctx->builder, rf_isnull, rf_end, rf_body);

        /* rf.body: last = q; p = q + max(1, sub_len) */
        LLVMPositionBuilderAtEnd(ctx->builder, rf_body);
        LLVMBuildStore(ctx->builder, rf_q, last_ptr);
        LLVMValueRef rf_one = LLVMConstInt(i32_type, 1, 0);
        LLVMValueRef rf_sgt = LLVMBuildICmp(ctx->builder, LLVMIntSGT,
                                            sub_len_v, rf_one, "rf.sgt");
        LLVMValueRef rf_adv = LLVMBuildSelect(ctx->builder, rf_sgt,
                                              sub_len_v, rf_one, "rf.adv");
        LLVMValueRef rf_np = LLVMBuildGEP2(ctx->builder, i8_type, rf_q,
                                           &rf_adv, 1, "rf.np");
        LLVMBuildStore(ctx->builder, rf_np, p_ptr);
        LLVMBuildBr(ctx->builder, rf_cond);

        /* rf.end: if last == NULL return -1 else return (last - s_data) */
        LLVMPositionBuilderAtEnd(ctx->builder, rf_end);
        LLVMValueRef last_v = LLVMBuildLoad2(ctx->builder, ptr_type, last_ptr, "rf.lv");
        LLVMValueRef lnull = LLVMBuildICmp(ctx->builder, LLVMIntEQ,
                                           last_v, rf_null, "rf.lnull");
        LLVMValueRef li = LLVMBuildPtrToInt(ctx->builder, last_v,
                                            LLVMInt64TypeInContext(ctx->context), "rf.lint");
        LLVMValueRef bi = LLVMBuildPtrToInt(ctx->builder, s_data,
                                            LLVMInt64TypeInContext(ctx->context), "rf.bint");
        LLVMValueRef di = LLVMBuildSub(ctx->builder, li, bi, "rf.diff");
        LLVMValueRef d32 = LLVMBuildTrunc(ctx->builder, di, i32_type, "rf.d32");
        LLVMValueRef neg1 = LLVMConstInt(i32_type, (unsigned long long)-1, 1);
        return LLVMBuildSelect(ctx->builder, lnull, neg1, d32, "rfind.res");
    }

    /* s.count(string sub) -> int: number of non-overlapping occurrences */
    if (strcmp(method, "count") == 0)
    {
        LLVMValueRef sub_val = codegen_expr(ctx, node->as.call.args[0]);
        if (sub_val == NULL)
            return NULL;
        LLVMValueRef sub_data_v = ls_string_data(ctx, sub_val);
        LLVMValueRef sub_len_v = ls_string_len(ctx, sub_val);

        LLVMValueRef cur_fn = LLVMGetBasicBlockParent(LLVMGetInsertBlock(ctx->builder));
        LLVMValueRef cnt_ptr = LLVMBuildAlloca(ctx->builder, i32_type, "cn.cnt");
        LLVMValueRef p_ptr = LLVMBuildAlloca(ctx->builder, ptr_type, "cn.p");
        LLVMBuildStore(ctx->builder, LLVMConstInt(i32_type, 0, 0), cnt_ptr);
        LLVMBuildStore(ctx->builder, s_data, p_ptr);

        LLVMBasicBlockRef cn_cond = LLVMAppendBasicBlockInContext(ctx->context, cur_fn, "cn.cond");
        LLVMBasicBlockRef cn_body = LLVMAppendBasicBlockInContext(ctx->context, cur_fn, "cn.body");
        LLVMBasicBlockRef cn_end = LLVMAppendBasicBlockInContext(ctx->context, cur_fn, "cn.end");
        LLVMBuildBr(ctx->builder, cn_cond);

        /* cn.cond: q = strstr(p, sub); if NULL goto cn.end */
        LLVMPositionBuilderAtEnd(ctx->builder, cn_cond);
        LLVMValueRef cn_p = LLVMBuildLoad2(ctx->builder, ptr_type, p_ptr, "cn.pv");
        LLVMValueRef cn_strstr = LLVMGetNamedFunction(ctx->module, "strstr");
        LLVMTypeRef cn_sst = LLVMGlobalGetValueType(cn_strstr);
        LLVMValueRef cn_ss_args[] = {cn_p, sub_data_v};
        LLVMValueRef cn_q = LLVMBuildCall2(ctx->builder, cn_sst, cn_strstr,
                                           cn_ss_args, 2, "cn.q");
        LLVMValueRef cn_null = LLVMConstNull(ptr_type);
        LLVMValueRef cn_isnull = LLVMBuildICmp(ctx->builder, LLVMIntEQ,
                                               cn_q, cn_null, "cn.null");
        LLVMBuildCondBr(ctx->builder, cn_isnull, cn_end, cn_body);

        /* cn.body: cnt++; p = q + max(1, sub_len) */
        LLVMPositionBuilderAtEnd(ctx->builder, cn_body);
        LLVMValueRef cn_cv = LLVMBuildLoad2(ctx->builder, i32_type, cnt_ptr, "cn.cv");
        LLVMValueRef cn_one = LLVMConstInt(i32_type, 1, 0);
        LLVMBuildStore(ctx->builder,
                       LLVMBuildAdd(ctx->builder, cn_cv, cn_one, "cn.ci"),
                       cnt_ptr);
        LLVMValueRef cn_sgt = LLVMBuildICmp(ctx->builder, LLVMIntSGT,
                                            sub_len_v, cn_one, "cn.sgt");
        LLVMValueRef cn_adv = LLVMBuildSelect(ctx->builder, cn_sgt,
                                              sub_len_v, cn_one, "cn.adv");
        LLVMValueRef cn_np = LLVMBuildGEP2(ctx->builder, i8_type, cn_q,
                                           &cn_adv, 1, "cn.np");
        LLVMBuildStore(ctx->builder, cn_np, p_ptr);
        LLVMBuildBr(ctx->builder, cn_cond);

        /* cn.end: return count */
        LLVMPositionBuilderAtEnd(ctx->builder, cn_end);
        return LLVMBuildLoad2(ctx->builder, i32_type, cnt_ptr, "count.res");
    }

    /* s.split(string sep) -> vec(string) */
    if (strcmp(method, "split") == 0)
    {
        LLVMValueRef sep_val = codegen_expr(ctx, node->as.call.args[0]);
        if (sep_val == NULL)
            return NULL;
        LLVMValueRef sep_data_v = ls_string_data(ctx, sep_val);
        LLVMValueRef sep_len_v = ls_string_len(ctx, sep_val);

        LLVMTypeRef vec_t = ls_vec_type(ctx);

        /* Out-param allocas for __ls_str_split */
        LLVMValueRef out_data_pp = LLVMBuildAlloca(ctx->builder, ptr_type, "spl.odp");
        LLVMValueRef out_len_p = LLVMBuildAlloca(ctx->builder, i32_type, "spl.olp");
        LLVMValueRef out_cap_p = LLVMBuildAlloca(ctx->builder, i32_type, "spl.ocp");
        LLVMBuildStore(ctx->builder, LLVMConstNull(ptr_type), out_data_pp);
        LLVMBuildStore(ctx->builder, LLVMConstInt(i32_type, 0, 0), out_len_p);
        LLVMBuildStore(ctx->builder, LLVMConstInt(i32_type, 0, 0), out_cap_p);

        LLVMValueRef split_fn = LLVMGetNamedFunction(ctx->module, "__ls_str_split");
        LLVMTypeRef split_t = LLVMGlobalGetValueType(split_fn);
        LLVMValueRef spl_args[] = {s_data, s_len, sep_data_v, sep_len_v,
                                   out_data_pp, out_len_p, out_cap_p};
        LLVMBuildCall2(ctx->builder, split_t, split_fn, spl_args, 7, "");

        LLVMValueRef res_data = LLVMBuildLoad2(ctx->builder, ptr_type, out_data_pp, "spl.data");
        LLVMValueRef res_len = LLVMBuildLoad2(ctx->builder, i32_type, out_len_p, "spl.len");
        LLVMValueRef res_cap = LLVMBuildLoad2(ctx->builder, i32_type, out_cap_p, "spl.cap");

        LLVMValueRef result = LLVMGetUndef(vec_t);
        result = LLVMBuildInsertValue(ctx->builder, result, res_data, 0, "spl.r0");
        result = LLVMBuildInsertValue(ctx->builder, result, res_len, 1, "spl.r1");
        result = LLVMBuildInsertValue(ctx->builder, result, res_cap, 2, "spl.r2");
        return result;
    }

    /* sep.join(vec(string)) -> string */
    if (strcmp(method, "join") == 0)
    {
        LLVMValueRef vec_val = codegen_expr(ctx, node->as.call.args[0]);
        if (vec_val == NULL)
            return NULL;
        LLVMValueRef vec_data_v = LLVMBuildExtractValue(ctx->builder, vec_val, 0, "jn.vd");
        LLVMValueRef vec_len_v = LLVMBuildExtractValue(ctx->builder, vec_val, 1, "jn.vl");

        LLVMValueRef out_len_p = LLVMBuildAlloca(ctx->builder, i32_type, "jn.olp");

        LLVMValueRef join_fn = LLVMGetNamedFunction(ctx->module, "__ls_str_join");
        LLVMTypeRef join_t = LLVMGlobalGetValueType(join_fn);
        LLVMValueRef jn_args[] = {vec_data_v, vec_len_v, s_data, s_len, out_len_p};
        LLVMValueRef result_buf = LLVMBuildCall2(ctx->builder, join_t, join_fn,
                                                 jn_args, 5, "jn.buf");

        LLVMValueRef result_len = LLVMBuildLoad2(ctx->builder, i32_type, out_len_p, "jn.len");
        LLVMValueRef one = LLVMConstInt(i32_type, 1, 0);
        LLVMValueRef alloc_need = LLVMBuildAdd(ctx->builder, result_len, one, "jn.need");
        LLVMValueRef min_cap = LLVMConstInt(i32_type, LS_MIN_STR_CAP, 0);
        LLVMValueRef jn_gt = LLVMBuildICmp(ctx->builder, LLVMIntUGT,
                                           alloc_need, min_cap, "jn.gt");
        LLVMValueRef cap = LLVMBuildSelect(ctx->builder, jn_gt,
                                           alloc_need, min_cap, "jn.cap");
        return cg_push_temp_string(ctx, ls_string_make(ctx, result_buf, result_len, cap));
    }

    cg_error(ctx, node->line, node->column,
             "unknown string method '%s'", method);
    return NULL;
}

/* ============================================================
 * vec(T) built-in method codegen
 * ============================================================ */

/* Drop a single vec element stored at `elem_ptr` (a *T raw pointer into data[]).
   For string elements: conditional free(data) via emit_string_free_with_cont.
   For struct-with-drop elements: call the drop function.
   label: optional CG_DEBUG identifier shown in str.free/str.skip messages, e.g. "v[i]".
   Returns the continuation basic-block (builder positioned at end). */
static LLVMBasicBlockRef emit_vec_elem_drop_at(CodegenContext *ctx,
                                               LLVMValueRef elem_ptr,
                                               Type *elem_type,
                                               int idx_suffix,
                                               const char *label)
{
    if (elem_type == NULL)
        return LLVMGetInsertBlock(ctx->builder);
    (void)idx_suffix;

    if (elem_type->kind == TYPE_STRING)
    {
        /* elem_ptr is a *LsString inside the vec data buffer.
           Must use emit_string_free_with_cont so the builder ends up at a
           continuation block (no terminator), allowing callers to emit
           more instructions after the drop (e.g. the len-- store in pop()). */
        emit_string_free_with_cont(ctx, elem_ptr, NULL, label ? label : "vec[i]",
                                    "string.vec_elem_drop", CG_LINE(ctx), CG_COL(ctx));
        return LLVMGetInsertBlock(ctx->builder);
    }
    if (elem_type->kind == TYPE_STRUCT && elem_type->as.strukt.has_drop)
    {
        LLVMValueRef drop_fn_val = (LLVMValueRef)elem_type->as.strukt.drop_fn;
        if (drop_fn_val)
        {
#if CG_DEBUG
            {
                char dbg_fmt[128];
                snprintf(dbg_fmt, sizeof(dbg_fmt),
                         "[cg] vec.elem.drop   type=%s\n",
                         elem_type->as.strukt.name ? elem_type->as.strukt.name : "?");
                cg_emit_debug_printf(ctx, dbg_fmt, NULL, 0);
            }
#endif
            LLVMTypeRef fn_t = LLVMGlobalGetValueType(drop_fn_val);
            LLVMBuildCall2(ctx->builder, fn_t, drop_fn_val, &elem_ptr, 1, "");
        }
    }
    return LLVMGetInsertBlock(ctx->builder);
}

/* Inline grow logic: if (len >= cap) double the buffer via realloc.
   `vec_alloca` is the alloca holding the LsVec struct.
   `elem_llvm`  is the LLVM type of a single element.
   On entry the builder must be in a valid basic-block.
   On exit the builder is at the "no-grow-needed" / "after-grow" merge block. */
static void emit_vec_grow_inline(CodegenContext *ctx, LLVMValueRef vec_alloca,
                                 LLVMTypeRef elem_llvm)
{
    LLVMTypeRef vec_t = ls_vec_type(ctx);
    LLVMTypeRef i32_t = LLVMInt32TypeInContext(ctx->context);
    LLVMTypeRef i64_t = LLVMInt64TypeInContext(ctx->context);
    LLVMValueRef cur_fn = LLVMGetBasicBlockParent(LLVMGetInsertBlock(ctx->builder));

    /* Load current len and cap */
    LLVMValueRef vec_val = LLVMBuildLoad2(ctx->builder, vec_t, vec_alloca, "vg.v");
    LLVMValueRef len_val = LLVMBuildExtractValue(ctx->builder, vec_val, 1, "vg.len");
    LLVMValueRef cap_val = LLVMBuildExtractValue(ctx->builder, vec_val, 2, "vg.cap");

    /* if (len >= cap) branch to grow_bb, else to no_grow_bb */
    LLVMValueRef need_grow = LLVMBuildICmp(ctx->builder, LLVMIntSGE, len_val, cap_val, "vg.need");
    LLVMBasicBlockRef grow_bb = LLVMAppendBasicBlockInContext(ctx->context, cur_fn, "vg.grow");
    LLVMBasicBlockRef no_grow_bb = LLVMAppendBasicBlockInContext(ctx->context, cur_fn, "vg.ok");
    LLVMBuildCondBr(ctx->builder, need_grow, grow_bb, no_grow_bb);

    /* grow_bb: new_cap = cap == 0 ? 4 : cap * 2; data = realloc(data, new_cap*sizeof(elem)) */
    LLVMPositionBuilderAtEnd(ctx->builder, grow_bb);
    LLVMValueRef vec_val2 = LLVMBuildLoad2(ctx->builder, vec_t, vec_alloca, "vg.v2");
    LLVMValueRef old_data = LLVMBuildExtractValue(ctx->builder, vec_val2, 0, "vg.data");
    LLVMValueRef old_cap = LLVMBuildExtractValue(ctx->builder, vec_val2, 2, "vg.cap2");

    /* new_cap = (cap == 0) ? 4 : cap * 2 */
    LLVMValueRef zero32 = LLVMConstInt(i32_t, 0, 0);
    LLVMValueRef four32 = LLVMConstInt(i32_t, 4, 0);
    LLVMValueRef two32 = LLVMConstInt(i32_t, 2, 0);
    LLVMValueRef dbl_cap = LLVMBuildMul(ctx->builder, old_cap, two32, "vg.dbl");
    LLVMValueRef is_zero = LLVMBuildICmp(ctx->builder, LLVMIntEQ, old_cap, zero32, "vg.iszero");
    LLVMValueRef new_cap = LLVMBuildSelect(ctx->builder, is_zero, four32, dbl_cap, "vg.newcap");

    /* bytes = (i64)new_cap * sizeof(elem) */
    LLVMValueRef new_cap64 = LLVMBuildSExt(ctx->builder, new_cap, i64_t, "vg.cap64");
    LLVMValueRef elem_size = LLVMSizeOf(elem_llvm); /* compile-time i64 constant */
    LLVMValueRef bytes = LLVMBuildMul(ctx->builder, new_cap64, elem_size, "vg.bytes");

#if CG_DEBUG
    {
        LLVMValueRef dbg_args[2] = {old_cap, new_cap};
        cg_emit_debug_printf(ctx, "[cg] vec.grow   old_cap=%d new_cap=%d\n", dbg_args, 2);
    }
#endif
    /* new_data = realloc(old_data, bytes) */
    LLVMValueRef realloc_fn = LLVMGetNamedFunction(ctx->module, "realloc");
    LLVMTypeRef realloc_ft = LLVMGlobalGetValueType(realloc_fn);
    LLVMValueRef ra_args[2] = {old_data, bytes};
    LLVMValueRef new_data = LLVMBuildCall2(ctx->builder, realloc_ft, realloc_fn,
                                           ra_args, 2, "vg.newdata");

    /* Update vec struct: store new data ptr and new cap */
    LLVMValueRef vec_upd = LLVMBuildLoad2(ctx->builder, vec_t, vec_alloca, "vg.upd");
    vec_upd = LLVMBuildInsertValue(ctx->builder, vec_upd, new_data, 0, "vg.ud");
    vec_upd = LLVMBuildInsertValue(ctx->builder, vec_upd, new_cap, 2, "vg.uc");
    LLVMBuildStore(ctx->builder, vec_upd, vec_alloca);
    LLVMBuildBr(ctx->builder, no_grow_bb);

    /* no_grow_bb: builder continues here */
    LLVMPositionBuilderAtEnd(ctx->builder, no_grow_bb);
}

/* Codegen for vec(T) built-in method calls:
   v.push(x), v.pop(), v.clear(), v.reserve(n),
   v.is_empty(), v.first(), v.last().
   Returns the resulting LLVMValueRef (NULL for void methods). */
static LLVMValueRef codegen_vec_method(CodegenContext *ctx, AstNode *call_node, Type *vec_type)
{
    const char *method = call_node->as.call.callee->as.field_access.field;
    AstNode *obj_node = call_node->as.call.callee->as.field_access.object;
    Type *elem_type = vec_type->as.vec.elem;
    LLVMTypeRef vec_t = ls_vec_type(ctx);
    LLVMTypeRef i32_t = LLVMInt32TypeInContext(ctx->context);
    LLVMTypeRef i64_t = LLVMInt64TypeInContext(ctx->context);

    /* Resolve alloca for the vec object.
       - vec(T) IDENT: sym->value IS the alloca of the LsVec struct.
       - *vec(T) IDENT: sym->value is an alloca holding a pointer to a LsVec;
         load the pointer to get the actual vec storage address. */
    LLVMValueRef vec_alloca = NULL;
    if (obj_node->kind == AST_IDENT)
    {
        CgSymbol *sym = cg_scope_resolve(ctx->current_scope, obj_node->as.ident.name);
        if (sym)
        {
            Type *obj_t = obj_node->resolved_type;
            if (obj_t && obj_t->kind == TYPE_POINTER && obj_t->as.pointer_to &&
                obj_t->as.pointer_to->kind == TYPE_VECTOR)
            {
                /* *vec(T): load the pointer value to reach the caller's LsVec */
                LLVMTypeRef ptr_t = LLVMPointerTypeInContext(ctx->context, 0);
                vec_alloca = LLVMBuildLoad2(ctx->builder, ptr_t, sym->value, "vptr.deref");
            }
            else
            {
                vec_alloca = sym->value;
            }
        }
    }
    if (vec_alloca == NULL)
    {
        cg_error(ctx, call_node->line, call_node->column,
                 "vec method call: cannot get address of vec object");
        return NULL;
    }

    LLVMTypeRef elem_llvm = type_to_llvm(ctx, elem_type);

    /* ---- push(val) ---- */
    if (strcmp(method, "push") == 0)
    {
        /* Record temp count before evaluating arg so we can transfer ownership */
        int temp_mark_push = ctx->temp_string_count;
        LLVMValueRef val = codegen_expr(ctx, call_node->as.call.args[0]);
        if (val == NULL)
            return NULL;

        /* Grow if needed */
        emit_vec_grow_inline(ctx, vec_alloca, elem_llvm);

        /* data[len] = val; len++ */
        LLVMValueRef vec_val = LLVMBuildLoad2(ctx->builder, vec_t, vec_alloca, "vp.v");
        LLVMValueRef data_ptr = LLVMBuildExtractValue(ctx->builder, vec_val, 0, "vp.data");
        LLVMValueRef len_val = LLVMBuildExtractValue(ctx->builder, vec_val, 1, "vp.len");
        LLVMValueRef len64 = LLVMBuildSExt(ctx->builder, len_val, i64_t, "vp.len64");
        LLVMValueRef elem_ptr = LLVMBuildGEP2(ctx->builder, elem_llvm, data_ptr,
                                              &len64, 1, "vp.slot");
        LLVMBuildStore(ctx->builder, val, elem_ptr);

        /* Transfer ownership: mark the pushed value as moved so the caller's scope cleanup
           doesn't double-free the data now owned by the vec. */
        if (elem_type->kind == TYPE_STRING)
        {
            /* For string temporaries (e.g. "x".upper()), mark the temp slot as moved */
            cg_mark_last_temp_moved(ctx, temp_mark_push, "vec.push: temp owned by vec");
            /* For named string variables (e.g. v.push(s) or v.push(__move(s))),
               mark the source alloca as moved (cap = -1) so scope cleanup won't
               free the same data the vec owns. Unwrap __move() wrappers here. */
            AstNode *arg0 = ast_unwrap_move(call_node->as.call.args[0]);
            if (arg0->kind == AST_IDENT)
            {
                CgSymbol *argsym = cg_scope_resolve(ctx->current_scope, arg0->as.ident.name);
                if (argsym && argsym->type && argsym->type->kind == TYPE_STRING)
                {
                    mark_string_moved(ctx, argsym->value, "vec.push: var owned by vec");
                }
            }
        }
        if (elem_type->kind == TYPE_STRUCT && elem_type->as.strukt.has_drop)
        {
            /* Mark ALL string temps created during struct argument evaluation as moved.
               These are now owned by the vec element — cg_flush_temps must not free them.
               This handles struct literals like Person{name:"x".upper()} as well as
               any other expression that produces a struct with owned string fields. */
            for (int t = temp_mark_push; t < ctx->temp_string_count; t++)
            {
                mark_string_moved(ctx, ctx->temp_string_slots[t], "vec.push: struct temp owned by vec");
            }

            /* For struct-with-drop identifiers, set their moved_flag so scope cleanup skips them */
            AstNode *arg0 = ast_unwrap_move(call_node->as.call.args[0]);
            if (arg0->kind == AST_IDENT)
            {
                CgSymbol *argsym = cg_scope_resolve(ctx->current_scope, arg0->as.ident.name);
                if (argsym && argsym->moved_flag)
                {
                    LLVMTypeRef i1_t = LLVMInt1TypeInContext(ctx->context);
                    LLVMBuildStore(ctx->builder, LLVMConstInt(i1_t, 1, 0), argsym->moved_flag);
                }
            }
        }

        /* len++ */
        LLVMValueRef one32 = LLVMConstInt(i32_t, 1, 0);
        LLVMValueRef new_len = LLVMBuildAdd(ctx->builder, len_val, one32, "vp.nlen");
        LLVMValueRef vec_upd = LLVMBuildLoad2(ctx->builder, vec_t, vec_alloca, "vp.upd");
        vec_upd = LLVMBuildInsertValue(ctx->builder, vec_upd, new_len, 1, "vp.ul");
        LLVMBuildStore(ctx->builder, vec_upd, vec_alloca);
        return NULL; /* push() is void */
    }

    /* ---- pop() ---- */
    if (strcmp(method, "pop") == 0)
    {
        LLVMValueRef vec_val = LLVMBuildLoad2(ctx->builder, vec_t, vec_alloca, "vpc.v");
        LLVMValueRef len_val = LLVMBuildExtractValue(ctx->builder, vec_val, 1, "vpc.len");
        LLVMValueRef data_ptr = LLVMBuildExtractValue(ctx->builder, vec_val, 0, "vpc.data");

        /* len-- (no underflow guard for now — caller is responsible) */
        LLVMValueRef one32 = LLVMConstInt(i32_t, 1, 0);
        LLVMValueRef new_len = LLVMBuildSub(ctx->builder, len_val, one32, "vpc.nlen");

        /* Drop the element at data[new_len] if necessary */
        LLVMValueRef idx64 = LLVMBuildSExt(ctx->builder, new_len, i64_t, "vpc.idx64");
        LLVMValueRef old_ep = LLVMBuildGEP2(ctx->builder, elem_llvm, data_ptr,
                                            &idx64, 1, "vpc.ep");
        emit_vec_elem_drop_at(ctx, old_ep, elem_type, 0, "pop.elem");

        /* Store len-- back */
        LLVMValueRef vec_upd = LLVMBuildLoad2(ctx->builder, vec_t, vec_alloca, "vpc.upd");
        vec_upd = LLVMBuildInsertValue(ctx->builder, vec_upd, new_len, 1, "vpc.ul");
        LLVMBuildStore(ctx->builder, vec_upd, vec_alloca);
        return NULL; /* pop() is void */
    }

    /* ---- clear() ---- */
    if (strcmp(method, "clear") == 0)
    {
        LLVMValueRef vec_val = LLVMBuildLoad2(ctx->builder, vec_t, vec_alloca, "vcl.v");
        LLVMValueRef len_val = LLVMBuildExtractValue(ctx->builder, vec_val, 1, "vcl.len");
        LLVMValueRef data_ptr = LLVMBuildExtractValue(ctx->builder, vec_val, 0, "vcl.data");

        /* If element needs drop, loop over 0..len and drop each */
        bool elem_needs_drop = (elem_type &&
                                (elem_type->kind == TYPE_STRING ||
                                 (elem_type->kind == TYPE_STRUCT && elem_type->as.strukt.has_drop)));

        if (elem_needs_drop)
        {
            LLVMValueRef cur_fn = LLVMGetBasicBlockParent(LLVMGetInsertBlock(ctx->builder));
            /* Allocate loop counter in entry block */
            LLVMBasicBlockRef entry = LLVMGetEntryBasicBlock(cur_fn);
            LLVMBuilderRef tb = LLVMCreateBuilderInContext(ctx->context);
            LLVMValueRef fi = LLVMGetFirstInstruction(entry);
            if (fi)
                LLVMPositionBuilderBefore(tb, fi);
            else
                LLVMPositionBuilderAtEnd(tb, entry);
            LLVMValueRef idx_alloca = LLVMBuildAlloca(tb, i32_t, "vcl.i");
            LLVMDisposeBuilder(tb);

            LLVMValueRef zero32 = LLVMConstInt(i32_t, 0, 0);
            LLVMBuildStore(ctx->builder, zero32, idx_alloca);

            LLVMBasicBlockRef cond_bb = LLVMAppendBasicBlockInContext(ctx->context, cur_fn, "vcl.cond");
            LLVMBasicBlockRef body_bb = LLVMAppendBasicBlockInContext(ctx->context, cur_fn, "vcl.body");
            LLVMBasicBlockRef end_bb = LLVMAppendBasicBlockInContext(ctx->context, cur_fn, "vcl.end");

            LLVMBuildBr(ctx->builder, cond_bb);

            LLVMPositionBuilderAtEnd(ctx->builder, cond_bb);
            LLVMValueRef cur_i = LLVMBuildLoad2(ctx->builder, i32_t, idx_alloca, "vcl.ci");
            /* Reload len each iteration (in case it changes, though we just set it to 0 after) */
            LLVMValueRef cur_len = LLVMBuildLoad2(ctx->builder, i32_t,
                                                  /* extract from alloca again for freshness */
                                                  LLVMBuildStructGEP2(ctx->builder, vec_t, vec_alloca, 1, "vcl.lenptr"),
                                                  "vcl.clen");
            LLVMValueRef cmp = LLVMBuildICmp(ctx->builder, LLVMIntSLT, cur_i, cur_len, "vcl.lt");
            LLVMBuildCondBr(ctx->builder, cmp, body_bb, end_bb);

            LLVMPositionBuilderAtEnd(ctx->builder, body_bb);
            LLVMValueRef cur_data = LLVMBuildLoad2(ctx->builder, LLVMPointerTypeInContext(ctx->context, 0),
                                                   LLVMBuildStructGEP2(ctx->builder, vec_t, vec_alloca, 0, "vcl.dp"),
                                                   "vcl.cdata");
            LLVMValueRef idx64 = LLVMBuildSExt(ctx->builder, cur_i, i64_t, "vcl.idx64");
            LLVMValueRef ep = LLVMBuildGEP2(ctx->builder, elem_llvm, cur_data, &idx64, 1, "vcl.ep");
            emit_vec_elem_drop_at(ctx, ep, elem_type, 0, "clear[i]");

            /* Ensure we're still in a valid BB after potential branch chains from drop */
            LLVMBasicBlockRef after_drop = LLVMGetInsertBlock(ctx->builder);
            if (LLVMGetBasicBlockTerminator(after_drop) == NULL)
            {
                LLVMValueRef one32 = LLVMConstInt(i32_t, 1, 0);
                LLVMValueRef next_i = LLVMBuildAdd(ctx->builder, cur_i, one32, "vcl.ni");
                LLVMBuildStore(ctx->builder, next_i, idx_alloca);
                LLVMBuildBr(ctx->builder, cond_bb);
            }

            LLVMPositionBuilderAtEnd(ctx->builder, end_bb);
        }

        /* Set len = 0 (keep buffer allocated) */
        LLVMValueRef zero32 = LLVMConstInt(i32_t, 0, 0);
        LLVMValueRef vec_upd = LLVMBuildLoad2(ctx->builder, vec_t, vec_alloca, "vcl.upd");
        vec_upd = LLVMBuildInsertValue(ctx->builder, vec_upd, zero32, 1, "vcl.ul");
        LLVMBuildStore(ctx->builder, vec_upd, vec_alloca);
        (void)len_val;
        (void)data_ptr;
        return NULL; /* clear() is void */
    }

    /* ---- reserve(n) ---- */
    if (strcmp(method, "reserve") == 0)
    {
        LLVMValueRef n_val = codegen_expr(ctx, call_node->as.call.args[0]);
        if (n_val == NULL)
            return NULL;
        LLVMValueRef vec_val = LLVMBuildLoad2(ctx->builder, vec_t, vec_alloca, "vr.v");
        LLVMValueRef cap_val = LLVMBuildExtractValue(ctx->builder, vec_val, 2, "vr.cap");
        LLVMValueRef old_data = LLVMBuildExtractValue(ctx->builder, vec_val, 0, "vr.data");

        LLVMValueRef cur_fn = LLVMGetBasicBlockParent(LLVMGetInsertBlock(ctx->builder));
        LLVMBasicBlockRef need_bb = LLVMAppendBasicBlockInContext(ctx->context, cur_fn, "vr.need");
        LLVMBasicBlockRef ok_bb = LLVMAppendBasicBlockInContext(ctx->context, cur_fn, "vr.ok");

        LLVMValueRef needs = LLVMBuildICmp(ctx->builder, LLVMIntSGT, n_val, cap_val, "vr.needs");
        LLVMBuildCondBr(ctx->builder, needs, need_bb, ok_bb);

        LLVMPositionBuilderAtEnd(ctx->builder, need_bb);
        LLVMValueRef n64 = LLVMBuildSExt(ctx->builder, n_val, i64_t, "vr.n64");
        LLVMValueRef esz = LLVMSizeOf(elem_llvm);
        LLVMValueRef bytes = LLVMBuildMul(ctx->builder, n64, esz, "vr.bytes");
        LLVMValueRef rfa = LLVMGetNamedFunction(ctx->module, "realloc");
        LLVMTypeRef rft = LLVMGlobalGetValueType(rfa);
        LLVMValueRef ra_a[2] = {old_data, bytes};
        LLVMValueRef nd = LLVMBuildCall2(ctx->builder, rft, rfa, ra_a, 2, "vr.nd");
        LLVMValueRef upd = LLVMBuildLoad2(ctx->builder, vec_t, vec_alloca, "vr.upd");
        upd = LLVMBuildInsertValue(ctx->builder, upd, nd, 0, "vr.ud");
        upd = LLVMBuildInsertValue(ctx->builder, upd, n_val, 2, "vr.uc");
        LLVMBuildStore(ctx->builder, upd, vec_alloca);
        LLVMBuildBr(ctx->builder, ok_bb);

        LLVMPositionBuilderAtEnd(ctx->builder, ok_bb);
        return NULL; /* reserve() is void */
    }

    /* ---- is_empty() -> bool ---- */
    if (strcmp(method, "is_empty") == 0)
    {
        LLVMValueRef vec_val = LLVMBuildLoad2(ctx->builder, vec_t, vec_alloca, "vie.v");
        LLVMValueRef len_val = LLVMBuildExtractValue(ctx->builder, vec_val, 1, "vie.len");
        LLVMValueRef zero32 = LLVMConstInt(i32_t, 0, 0);
        return LLVMBuildICmp(ctx->builder, LLVMIntEQ, len_val, zero32, "vie.res");
    }

    /* Phase E.3.3: as_ptr() -> object  — raw data pointer for FFI buffer use.
       Returned pointer aliases the vec's heap; valid until the vec is grown
       or freed. Caller must NOT free. Empty vec returns NULL. */
    if (strcmp(method, "as_ptr") == 0)
    {
        LLVMValueRef vec_val = LLVMBuildLoad2(ctx->builder, vec_t, vec_alloca, "vap.v");
        return LLVMBuildExtractValue(ctx->builder, vec_val, 0, "vap.data");
    }

    /* ---- first() -> T  (deep clone of element[0], default if empty) ---- */
    if (strcmp(method, "first") == 0)
    {
        LLVMValueRef vec_val = LLVMBuildLoad2(ctx->builder, vec_t, vec_alloca, "vfst.v");
        LLVMValueRef len_val = LLVMBuildExtractValue(ctx->builder, vec_val, 1, "vfst.len");
        LLVMValueRef data_ptr = LLVMBuildExtractValue(ctx->builder, vec_val, 0, "vfst.data");

        LLVMValueRef zero32 = LLVMConstInt(i32_t, 0, 0);
        LLVMValueRef not_empty = LLVMBuildICmp(ctx->builder, LLVMIntSGT, len_val, zero32, "vfst.ne");

        LLVMValueRef cur_fn = LLVMGetBasicBlockParent(LLVMGetInsertBlock(ctx->builder));
        int id = g_block_counter++;
        char ok_name[40], emp_name[40], merge_name[40];
        snprintf(ok_name, sizeof(ok_name), "vfst.ok%d", id);
        snprintf(emp_name, sizeof(emp_name), "vfst.emp%d", id);
        snprintf(merge_name, sizeof(merge_name), "vfst.merge%d", id);
        LLVMBasicBlockRef ok_bb = LLVMAppendBasicBlockInContext(ctx->context, cur_fn, ok_name);
        LLVMBasicBlockRef emp_bb = LLVMAppendBasicBlockInContext(ctx->context, cur_fn, emp_name);
        LLVMBasicBlockRef merge_bb = LLVMAppendBasicBlockInContext(ctx->context, cur_fn, merge_name);

        /* Allocate result slot in function entry block */
        LLVMBuilderRef tb = LLVMCreateBuilderInContext(ctx->context);
        LLVMBasicBlockRef entry_bb = LLVMGetEntryBasicBlock(cur_fn);
        LLVMValueRef fi = LLVMGetFirstInstruction(entry_bb);
        if (fi)
            LLVMPositionBuilderBefore(tb, fi);
        else
            LLVMPositionBuilderAtEnd(tb, entry_bb);
        LLVMValueRef result_alloca = LLVMBuildAlloca(tb, elem_llvm, "vfst.res");
        LLVMDisposeBuilder(tb);

        /* Default (empty vec) value */
        LLVMValueRef zero_val;
        if (elem_type && elem_type->kind == TYPE_STRING)
            zero_val = ls_string_from_literal(ctx, "", "vfst.empty");
        else
            zero_val = LLVMConstNull(elem_llvm);
        LLVMBuildStore(ctx->builder, zero_val, result_alloca);

        LLVMBuildCondBr(ctx->builder, not_empty, ok_bb, emp_bb);

        /* ok_bb: load element[0] and deep-clone */
        LLVMPositionBuilderAtEnd(ctx->builder, ok_bb);
        LLVMValueRef zero64 = LLVMConstInt(i64_t, 0, 0);
        LLVMValueRef gep = LLVMBuildGEP2(ctx->builder, elem_llvm, data_ptr, &zero64, 1, "vfst.ep");
        LLVMValueRef elem = LLVMBuildLoad2(ctx->builder, elem_llvm, gep, "vfst.elem");
        if (elem_type->kind == TYPE_STRING)
            elem = emit_string_clone_val(ctx, elem);
        else if (elem_type->kind == TYPE_STRUCT && elem_type->as.strukt.has_drop)
            elem = emit_struct_clone_val(ctx, elem, elem_llvm, elem_type);
#if CG_DEBUG
        {
            const char *tn = (elem_type->kind == TYPE_STRUCT && elem_type->as.strukt.name)
                                 ? elem_type->as.strukt.name
                                 : "?";
            char dbg_fmt[128];
            snprintf(dbg_fmt, sizeof(dbg_fmt), "[cg] vec.first  clone  type=%s\n", tn);
            cg_emit_debug_printf(ctx, dbg_fmt, NULL, 0);
        }
#endif
        LLVMBuildStore(ctx->builder, elem, result_alloca);
        LLVMBuildBr(ctx->builder, merge_bb);

        /* emp_bb: empty vec — warn, result stays zero/default */
        LLVMPositionBuilderAtEnd(ctx->builder, emp_bb);
        emit_printf(ctx, "[warning] vec.first() called on empty vec\n", NULL, 0);
        LLVMBuildBr(ctx->builder, merge_bb);

        /* merge_bb: return result */
        LLVMPositionBuilderAtEnd(ctx->builder, merge_bb);
        LLVMValueRef r = LLVMBuildLoad2(ctx->builder, elem_llvm, result_alloca, "vfst.r");

        /* Register string clone as temp for ownership tracking */
        if (elem_type->kind == TYPE_STRING && ctx->current_fn != NULL)
        {
            if (ctx->temp_string_count >= ctx->temp_string_cap)
            {
                ctx->temp_string_cap = GROW_CAPACITY(ctx->temp_string_cap);
                ctx->temp_string_slots = GROW_ARRAY(LLVMValueRef,
                                                    ctx->temp_string_slots,
                                                    ctx->temp_string_cap);
            }
            ctx->temp_string_slots[ctx->temp_string_count++] = result_alloca;
        }
        return r;
    }

    /* ---- last() -> T  (deep clone of element[len-1], default if empty) ---- */
    if (strcmp(method, "last") == 0)
    {
        LLVMValueRef vec_val = LLVMBuildLoad2(ctx->builder, vec_t, vec_alloca, "vlst.v");
        LLVMValueRef len_val = LLVMBuildExtractValue(ctx->builder, vec_val, 1, "vlst.len");
        LLVMValueRef data_ptr = LLVMBuildExtractValue(ctx->builder, vec_val, 0, "vlst.data");

        LLVMValueRef zero32 = LLVMConstInt(i32_t, 0, 0);
        LLVMValueRef not_empty = LLVMBuildICmp(ctx->builder, LLVMIntSGT, len_val, zero32, "vlst.ne");

        LLVMValueRef cur_fn = LLVMGetBasicBlockParent(LLVMGetInsertBlock(ctx->builder));
        int id = g_block_counter++;
        char ok_name[40], emp_name[40], merge_name[40];
        snprintf(ok_name, sizeof(ok_name), "vlst.ok%d", id);
        snprintf(emp_name, sizeof(emp_name), "vlst.emp%d", id);
        snprintf(merge_name, sizeof(merge_name), "vlst.merge%d", id);
        LLVMBasicBlockRef ok_bb = LLVMAppendBasicBlockInContext(ctx->context, cur_fn, ok_name);
        LLVMBasicBlockRef emp_bb = LLVMAppendBasicBlockInContext(ctx->context, cur_fn, emp_name);
        LLVMBasicBlockRef merge_bb = LLVMAppendBasicBlockInContext(ctx->context, cur_fn, merge_name);

        /* Allocate result slot in function entry block */
        LLVMBuilderRef tb = LLVMCreateBuilderInContext(ctx->context);
        LLVMBasicBlockRef entry_bb = LLVMGetEntryBasicBlock(cur_fn);
        LLVMValueRef fi = LLVMGetFirstInstruction(entry_bb);
        if (fi)
            LLVMPositionBuilderBefore(tb, fi);
        else
            LLVMPositionBuilderAtEnd(tb, entry_bb);
        LLVMValueRef result_alloca = LLVMBuildAlloca(tb, elem_llvm, "vlst.res");
        LLVMDisposeBuilder(tb);

        /* Default (empty vec) value */
        LLVMValueRef zero_val;
        if (elem_type && elem_type->kind == TYPE_STRING)
            zero_val = ls_string_from_literal(ctx, "", "vlst.empty");
        else
            zero_val = LLVMConstNull(elem_llvm);
        LLVMBuildStore(ctx->builder, zero_val, result_alloca);

        LLVMBuildCondBr(ctx->builder, not_empty, ok_bb, emp_bb);

        /* ok_bb: load element[len-1] and deep-clone */
        LLVMPositionBuilderAtEnd(ctx->builder, ok_bb);
        LLVMValueRef one32 = LLVMConstInt(i32_t, 1, 0);
        LLVMValueRef last_i = LLVMBuildSub(ctx->builder, len_val, one32, "vlst.li");
        LLVMValueRef last64 = LLVMBuildSExt(ctx->builder, last_i, i64_t, "vlst.l64");
        LLVMValueRef gep = LLVMBuildGEP2(ctx->builder, elem_llvm, data_ptr, &last64, 1, "vlst.ep");
        LLVMValueRef elem = LLVMBuildLoad2(ctx->builder, elem_llvm, gep, "vlst.elem");
        if (elem_type->kind == TYPE_STRING)
            elem = emit_string_clone_val(ctx, elem);
        else if (elem_type->kind == TYPE_STRUCT && elem_type->as.strukt.has_drop)
            elem = emit_struct_clone_val(ctx, elem, elem_llvm, elem_type);
#if CG_DEBUG
        {
            const char *tn = (elem_type->kind == TYPE_STRUCT && elem_type->as.strukt.name)
                                 ? elem_type->as.strukt.name
                                 : "?";
            char dbg_fmt[128];
            snprintf(dbg_fmt, sizeof(dbg_fmt), "[cg] vec.last   clone  type=%s\n", tn);
            cg_emit_debug_printf(ctx, dbg_fmt, NULL, 0);
        }
#endif
        LLVMBuildStore(ctx->builder, elem, result_alloca);
        LLVMBuildBr(ctx->builder, merge_bb);

        /* emp_bb: empty vec — warn, result stays zero/default */
        LLVMPositionBuilderAtEnd(ctx->builder, emp_bb);
        emit_printf(ctx, "[warning] vec.last() called on empty vec\n", NULL, 0);
        LLVMBuildBr(ctx->builder, merge_bb);

        /* merge_bb: return result */
        LLVMPositionBuilderAtEnd(ctx->builder, merge_bb);
        LLVMValueRef r = LLVMBuildLoad2(ctx->builder, elem_llvm, result_alloca, "vlst.r");

        /* Register string clone as temp for ownership tracking */
        if (elem_type->kind == TYPE_STRING && ctx->current_fn != NULL)
        {
            if (ctx->temp_string_count >= ctx->temp_string_cap)
            {
                ctx->temp_string_cap = GROW_CAPACITY(ctx->temp_string_cap);
                ctx->temp_string_slots = GROW_ARRAY(LLVMValueRef,
                                                    ctx->temp_string_slots,
                                                    ctx->temp_string_cap);
            }
            ctx->temp_string_slots[ctx->temp_string_count++] = result_alloca;
        }
        return r;
    }

    /* ---- get(i) -> T  (deep clone of element[i], default if out-of-bounds) ---- */
    if (strcmp(method, "get") == 0)
    {
        LLVMValueRef i_val = codegen_expr(ctx, call_node->as.call.args[0]);
        if (i_val == NULL)
            return NULL;
        if (LLVMTypeOf(i_val) != i32_t)
            i_val = LLVMBuildIntCast2(ctx->builder, i_val, i32_t, 1, "vget.i32");

        LLVMValueRef vec_val = LLVMBuildLoad2(ctx->builder, vec_t, vec_alloca, "vget.v");
        LLVMValueRef len_val = LLVMBuildExtractValue(ctx->builder, vec_val, 1, "vget.len");
        LLVMValueRef data_ptr = LLVMBuildExtractValue(ctx->builder, vec_val, 0, "vget.data");

        /* In-bounds: i >= 0 && i < len  (signed compare both, len is non-negative) */
        LLVMValueRef zero32 = LLVMConstInt(i32_t, 0, 0);
        LLVMValueRef i_ge0 = LLVMBuildICmp(ctx->builder, LLVMIntSGE, i_val, zero32, "vget.ge0");
        LLVMValueRef i_lt_len = LLVMBuildICmp(ctx->builder, LLVMIntSLT, i_val, len_val, "vget.ltl");
        LLVMValueRef in_bounds = LLVMBuildAnd(ctx->builder, i_ge0, i_lt_len, "vget.ib");

        LLVMValueRef cur_fn = LLVMGetBasicBlockParent(LLVMGetInsertBlock(ctx->builder));
        int id = g_block_counter++;
        char ok_name[40], oob_name[40], merge_name[40];
        snprintf(ok_name, sizeof(ok_name), "vget.ok%d", id);
        snprintf(oob_name, sizeof(oob_name), "vget.oob%d", id);
        snprintf(merge_name, sizeof(merge_name), "vget.merge%d", id);
        LLVMBasicBlockRef ok_bb = LLVMAppendBasicBlockInContext(ctx->context, cur_fn, ok_name);
        LLVMBasicBlockRef oob_bb = LLVMAppendBasicBlockInContext(ctx->context, cur_fn, oob_name);
        LLVMBasicBlockRef merge_bb = LLVMAppendBasicBlockInContext(ctx->context, cur_fn, merge_name);

        /* Allocate result slot in function entry block */
        LLVMBuilderRef tb = LLVMCreateBuilderInContext(ctx->context);
        LLVMBasicBlockRef entry_bb = LLVMGetEntryBasicBlock(cur_fn);
        LLVMValueRef fi = LLVMGetFirstInstruction(entry_bb);
        if (fi)
            LLVMPositionBuilderBefore(tb, fi);
        else
            LLVMPositionBuilderAtEnd(tb, entry_bb);
        LLVMValueRef result_alloca = LLVMBuildAlloca(tb, elem_llvm, "vget.res");
        LLVMDisposeBuilder(tb);

        /* Default (out-of-bounds) value */
        LLVMValueRef zero_val;
        if (elem_type && elem_type->kind == TYPE_STRING)
            zero_val = ls_string_from_literal(ctx, "", "vget.empty");
        else
            zero_val = LLVMConstNull(elem_llvm);
        LLVMBuildStore(ctx->builder, zero_val, result_alloca);

        LLVMBuildCondBr(ctx->builder, in_bounds, ok_bb, oob_bb);

        /* ok_bb: load element[i] and deep-clone */
        LLVMPositionBuilderAtEnd(ctx->builder, ok_bb);
        LLVMValueRef i64 = LLVMBuildSExt(ctx->builder, i_val, i64_t, "vget.i64");
        LLVMValueRef gep = LLVMBuildGEP2(ctx->builder, elem_llvm, data_ptr, &i64, 1, "vget.ep");
        LLVMValueRef elem = LLVMBuildLoad2(ctx->builder, elem_llvm, gep, "vget.elem");
        if (elem_type->kind == TYPE_STRING)
            elem = emit_string_clone_val(ctx, elem);
        else if (elem_type->kind == TYPE_STRUCT && elem_type->as.strukt.has_drop)
            elem = emit_struct_clone_val(ctx, elem, elem_llvm, elem_type);
        LLVMBuildStore(ctx->builder, elem, result_alloca);
        LLVMBuildBr(ctx->builder, merge_bb);

        /* oob_bb: out-of-bounds — warn, result stays zero/default */
        LLVMPositionBuilderAtEnd(ctx->builder, oob_bb);
        emit_printf(ctx, "[warning] vec.get() index out of bounds\n", NULL, 0);
        LLVMBuildBr(ctx->builder, merge_bb);

        /* merge_bb: return result */
        LLVMPositionBuilderAtEnd(ctx->builder, merge_bb);
        LLVMValueRef r = LLVMBuildLoad2(ctx->builder, elem_llvm, result_alloca, "vget.r");

        /* Register string clone as temp for ownership tracking */
        if (elem_type->kind == TYPE_STRING && ctx->current_fn != NULL)
        {
            if (ctx->temp_string_count >= ctx->temp_string_cap)
            {
                ctx->temp_string_cap = GROW_CAPACITY(ctx->temp_string_cap);
                ctx->temp_string_slots = GROW_ARRAY(LLVMValueRef,
                                                    ctx->temp_string_slots,
                                                    ctx->temp_string_cap);
            }
            ctx->temp_string_slots[ctx->temp_string_count++] = result_alloca;
        }
        return r;
    }

    /* ---- truncate(n) -> void  — drop [n, len), set len = n ---- */
    if (strcmp(method, "truncate") == 0)
    {
        LLVMValueRef n_val = codegen_expr(ctx, call_node->as.call.args[0]);
        if (n_val == NULL)
            return NULL;
        if (LLVMTypeOf(n_val) != i32_t)
            n_val = LLVMBuildIntCast2(ctx->builder, n_val, i32_t, 1, "vtr.n32");

        /* Clamp n to 0 if negative */
        LLVMValueRef zero32 = LLVMConstInt(i32_t, 0, 0);
        LLVMValueRef is_neg = LLVMBuildICmp(ctx->builder, LLVMIntSLT, n_val, zero32, "vtr.neg");
        n_val = LLVMBuildSelect(ctx->builder, is_neg, zero32, n_val, "vtr.nn");

        LLVMValueRef vec_val = LLVMBuildLoad2(ctx->builder, vec_t, vec_alloca, "vtr.v");
        LLVMValueRef len_val = LLVMBuildExtractValue(ctx->builder, vec_val, 1, "vtr.len");
        LLVMValueRef data_ptr = LLVMBuildExtractValue(ctx->builder, vec_val, 0, "vtr.data");

#if CG_DEBUG
        {
            LLVMValueRef dbg_args[2] = {len_val, n_val};
            cg_emit_debug_printf(ctx,
                                 "[cg] vec.truncate  old_len=%d new_len=%d\n", dbg_args, 2);
        }
#endif

        LLVMValueRef cur_fn = LLVMGetBasicBlockParent(LLVMGetInsertBlock(ctx->builder));
        LLVMBasicBlockRef do_bb = LLVMAppendBasicBlockInContext(ctx->context, cur_fn, "vtr.do");
        LLVMBasicBlockRef end_bb = LLVMAppendBasicBlockInContext(ctx->context, cur_fn, "vtr.end");

        /* Only act when n < len */
        LLVMValueRef needs = LLVMBuildICmp(ctx->builder, LLVMIntSLT, n_val, len_val, "vtr.need");
        LLVMBuildCondBr(ctx->builder, needs, do_bb, end_bb);

        LLVMPositionBuilderAtEnd(ctx->builder, do_bb);

        bool tr_needs_drop = elem_type &&
                             (elem_type->kind == TYPE_STRING ||
                              (elem_type->kind == TYPE_STRUCT && elem_type->as.strukt.has_drop));
        if (tr_needs_drop)
        {
            /* Allocate loop index in function entry block */
            LLVMBuilderRef tb = LLVMCreateBuilderInContext(ctx->context);
            LLVMBasicBlockRef entry_bb = LLVMGetEntryBasicBlock(cur_fn);
            LLVMValueRef fi = LLVMGetFirstInstruction(entry_bb);
            if (fi)
                LLVMPositionBuilderBefore(tb, fi);
            else
                LLVMPositionBuilderAtEnd(tb, entry_bb);
            LLVMValueRef idx_alloca = LLVMBuildAlloca(tb, i32_t, "vtr.i");
            LLVMDisposeBuilder(tb);

            LLVMBuildStore(ctx->builder, n_val, idx_alloca);

            LLVMBasicBlockRef cond_bb = LLVMAppendBasicBlockInContext(ctx->context, cur_fn, "vtr.cond");
            LLVMBasicBlockRef body_bb = LLVMAppendBasicBlockInContext(ctx->context, cur_fn, "vtr.body");
            LLVMBasicBlockRef dend_bb = LLVMAppendBasicBlockInContext(ctx->context, cur_fn, "vtr.dend");
            LLVMBuildBr(ctx->builder, cond_bb);

            LLVMPositionBuilderAtEnd(ctx->builder, cond_bb);
            LLVMValueRef cur_i = LLVMBuildLoad2(ctx->builder, i32_t, idx_alloca, "vtr.ci");
            LLVMValueRef cmp = LLVMBuildICmp(ctx->builder, LLVMIntSLT, cur_i, len_val, "vtr.lt");
            LLVMBuildCondBr(ctx->builder, cmp, body_bb, dend_bb);

            LLVMPositionBuilderAtEnd(ctx->builder, body_bb);
            LLVMValueRef idx64 = LLVMBuildSExt(ctx->builder, cur_i, i64_t, "vtr.idx64");
            LLVMValueRef ep = LLVMBuildGEP2(ctx->builder, elem_llvm, data_ptr, &idx64, 1, "vtr.ep");
#if CG_DEBUG
            {
                cg_emit_debug_printf(ctx, "[cg] vec.truncate  elem_drop\n", NULL, 0);
            }
#endif
            emit_vec_elem_drop_at(ctx, ep, elem_type, 0, "trunc[i]");

            /* After drop the builder may be in a continuation block; only add
               the increment + back-edge if the block has no terminator yet. */
            LLVMBasicBlockRef after_drop = LLVMGetInsertBlock(ctx->builder);
            if (LLVMGetBasicBlockTerminator(after_drop) == NULL)
            {
                LLVMValueRef one32 = LLVMConstInt(i32_t, 1, 0);
                LLVMValueRef next_i = LLVMBuildAdd(ctx->builder, cur_i, one32, "vtr.ni");
                LLVMBuildStore(ctx->builder, next_i, idx_alloca);
                LLVMBuildBr(ctx->builder, cond_bb);
            }

            LLVMPositionBuilderAtEnd(ctx->builder, dend_bb);
        }

        /* len = n */
        LLVMValueRef vec_upd = LLVMBuildLoad2(ctx->builder, vec_t, vec_alloca, "vtr.upd");
        vec_upd = LLVMBuildInsertValue(ctx->builder, vec_upd, n_val, 1, "vtr.ul");
        LLVMBuildStore(ctx->builder, vec_upd, vec_alloca);
        LLVMBuildBr(ctx->builder, end_bb);

        LLVMPositionBuilderAtEnd(ctx->builder, end_bb);
        (void)data_ptr;
        return NULL;
    }

    /* ---- remove(i) -> void  — drop element[i], memmove tail left, len-- ---- */
    if (strcmp(method, "remove") == 0)
    {
        LLVMValueRef i_val = codegen_expr(ctx, call_node->as.call.args[0]);
        if (i_val == NULL)
            return NULL;
        if (LLVMTypeOf(i_val) != i32_t)
            i_val = LLVMBuildIntCast2(ctx->builder, i_val, i32_t, 1, "vrm.i32");

        LLVMValueRef vec_val = LLVMBuildLoad2(ctx->builder, vec_t, vec_alloca, "vrm.v");
        LLVMValueRef len_val = LLVMBuildExtractValue(ctx->builder, vec_val, 1, "vrm.len");
        LLVMValueRef data_ptr = LLVMBuildExtractValue(ctx->builder, vec_val, 0, "vrm.data");

        LLVMValueRef cur_fn = LLVMGetBasicBlockParent(LLVMGetInsertBlock(ctx->builder));
        LLVMBasicBlockRef do_bb = LLVMAppendBasicBlockInContext(ctx->context, cur_fn, "vrm.do");
        LLVMBasicBlockRef oob_bb = LLVMAppendBasicBlockInContext(ctx->context, cur_fn, "vrm.oob");
        LLVMBasicBlockRef end_bb = LLVMAppendBasicBlockInContext(ctx->context, cur_fn, "vrm.end");

        /* Bounds check: 0 <= i < len */
        LLVMValueRef zero32 = LLVMConstInt(i32_t, 0, 0);
        LLVMValueRef ge_zero = LLVMBuildICmp(ctx->builder, LLVMIntSGE, i_val, zero32, "vrm.ge0");
        LLVMValueRef lt_len = LLVMBuildICmp(ctx->builder, LLVMIntSLT, i_val, len_val, "vrm.ltl");
        LLVMValueRef in_range = LLVMBuildAnd(ctx->builder, ge_zero, lt_len, "vrm.inr");
        LLVMBuildCondBr(ctx->builder, in_range, do_bb, oob_bb);

        /* oob_bb: print warning, skip operation */
        LLVMPositionBuilderAtEnd(ctx->builder, oob_bb);
        {
            LLVMValueRef wa[2] = {i_val, len_val};
            emit_printf(ctx,
                        "[warning] vec.remove() index out of bounds: index=%d, len=%d\n", wa, 2);
        }
        LLVMBuildBr(ctx->builder, end_bb);

        /* do_bb: valid index — drop + memmove + len-- */
        LLVMPositionBuilderAtEnd(ctx->builder, do_bb);
#if CG_DEBUG
        {
            LLVMValueRef dbg_args[2] = {i_val, len_val};
            cg_emit_debug_printf(ctx,
                                 "[cg] vec.remove  idx=%d old_len=%d\n", dbg_args, 2);
        }
#endif
        /* Step 1: drop element[i] */
        LLVMValueRef i64_i = LLVMBuildSExt(ctx->builder, i_val, i64_t, "vrm.i64");
        LLVMValueRef elem_ptr = LLVMBuildGEP2(ctx->builder, elem_llvm, data_ptr, &i64_i, 1, "vrm.ep");
        emit_vec_elem_drop_at(ctx, elem_ptr, elem_type, 0, "remove[i]");
        /* After drop the builder is in a valid (no-terminator) block. */

        /* Step 2: memmove remaining elements left — only if i < len-1 */
        LLVMValueRef one32 = LLVMConstInt(i32_t, 1, 0);
        LLVMValueRef move_count = LLVMBuildSub(ctx->builder, len_val, i_val, "vrm.mc0");
        move_count = LLVMBuildSub(ctx->builder, move_count, one32, "vrm.mc");
        LLVMValueRef mc64 = LLVMBuildSExt(ctx->builder, move_count, i64_t, "vrm.mc64");
        LLVMValueRef elem_size = LLVMSizeOf(elem_llvm);
        LLVMValueRef mv_bytes = LLVMBuildMul(ctx->builder, mc64, elem_size, "vrm.bytes");

        LLVMValueRef zero64 = LLVMConstInt(i64_t, 0, 0);
        LLVMValueRef need_mv = LLVMBuildICmp(ctx->builder, LLVMIntSGT, mc64, zero64, "vrm.nm");
        LLVMBasicBlockRef mv_bb = LLVMAppendBasicBlockInContext(ctx->context, cur_fn, "vrm.mv");
        LLVMBasicBlockRef mv_end = LLVMAppendBasicBlockInContext(ctx->context, cur_fn, "vrm.mvend");
        LLVMBuildCondBr(ctx->builder, need_mv, mv_bb, mv_end);

        LLVMPositionBuilderAtEnd(ctx->builder, mv_bb);
        {
            /* dst = data + i,  src = data + i + 1 */
            LLVMValueRef one64 = LLVMConstInt(i64_t, 1, 0);
            LLVMValueRef ip1_64 = LLVMBuildAdd(ctx->builder, i64_i, one64, "vrm.ip1");
            LLVMValueRef dst = LLVMBuildGEP2(ctx->builder, elem_llvm, data_ptr, &i64_i, 1, "vrm.dst");
            LLVMValueRef src = LLVMBuildGEP2(ctx->builder, elem_llvm, data_ptr, &ip1_64, 1, "vrm.src");
            LLVMValueRef mm_fn = LLVMGetNamedFunction(ctx->module, "memmove");
            LLVMTypeRef mm_ft = LLVMGlobalGetValueType(mm_fn);
            LLVMValueRef mm_a[3] = {dst, src, mv_bytes};
            LLVMBuildCall2(ctx->builder, mm_ft, mm_fn, mm_a, 3, "");
        }
        LLVMBuildBr(ctx->builder, mv_end);

        /* Step 3: len-- */
        LLVMPositionBuilderAtEnd(ctx->builder, mv_end);
        LLVMValueRef new_len = LLVMBuildSub(ctx->builder, len_val, one32, "vrm.nl");
        LLVMValueRef vec_upd = LLVMBuildLoad2(ctx->builder, vec_t, vec_alloca, "vrm.upd");
        vec_upd = LLVMBuildInsertValue(ctx->builder, vec_upd, new_len, 1, "vrm.ul");
        LLVMBuildStore(ctx->builder, vec_upd, vec_alloca);
        LLVMBuildBr(ctx->builder, end_bb);

        LLVMPositionBuilderAtEnd(ctx->builder, end_bb);
        return NULL;
    }

    /* ---- swap(i, j) -> void  — raw byte swap of two elements (no drop/clone) ---- */
    if (strcmp(method, "swap") == 0)
    {
        LLVMValueRef i_val = codegen_expr(ctx, call_node->as.call.args[0]);
        LLVMValueRef j_val = codegen_expr(ctx, call_node->as.call.args[1]);
        if (i_val == NULL || j_val == NULL)
            return NULL;
        if (LLVMTypeOf(i_val) != i32_t)
            i_val = LLVMBuildIntCast2(ctx->builder, i_val, i32_t, 1, "vsw.i32");
        if (LLVMTypeOf(j_val) != i32_t)
            j_val = LLVMBuildIntCast2(ctx->builder, j_val, i32_t, 1, "vsw.j32");

        LLVMValueRef vec_val = LLVMBuildLoad2(ctx->builder, vec_t, vec_alloca, "vsw.v");
        LLVMValueRef len_val = LLVMBuildExtractValue(ctx->builder, vec_val, 1, "vsw.len");
        LLVMValueRef data_ptr = LLVMBuildExtractValue(ctx->builder, vec_val, 0, "vsw.data");

        LLVMValueRef cur_fn = LLVMGetBasicBlockParent(LLVMGetInsertBlock(ctx->builder));

        /* Bounds check both indices */
        LLVMValueRef zero32 = LLVMConstInt(i32_t, 0, 0);
        LLVMValueRef i_ge0 = LLVMBuildICmp(ctx->builder, LLVMIntSGE, i_val, zero32, "vsw.ige");
        LLVMValueRef i_ltl = LLVMBuildICmp(ctx->builder, LLVMIntSLT, i_val, len_val, "vsw.ilt");
        LLVMValueRef j_ge0 = LLVMBuildICmp(ctx->builder, LLVMIntSGE, j_val, zero32, "vsw.jge");
        LLVMValueRef j_ltl = LLVMBuildICmp(ctx->builder, LLVMIntSLT, j_val, len_val, "vsw.jlt");
        LLVMValueRef i_ok = LLVMBuildAnd(ctx->builder, i_ge0, i_ltl, "vsw.iok");
        LLVMValueRef j_ok = LLVMBuildAnd(ctx->builder, j_ge0, j_ltl, "vsw.jok");
        LLVMValueRef both_ok = LLVMBuildAnd(ctx->builder, i_ok, j_ok, "vsw.ok");

        LLVMBasicBlockRef chk_bb = LLVMAppendBasicBlockInContext(ctx->context, cur_fn, "vsw.chk");
        LLVMBasicBlockRef oob_bb = LLVMAppendBasicBlockInContext(ctx->context, cur_fn, "vsw.oob");
        LLVMBasicBlockRef do_bb = LLVMAppendBasicBlockInContext(ctx->context, cur_fn, "vsw.do");
        LLVMBasicBlockRef end_bb = LLVMAppendBasicBlockInContext(ctx->context, cur_fn, "vsw.end");

        LLVMBuildCondBr(ctx->builder, both_ok, chk_bb, oob_bb);

        /* oob_bb: warn, skip */
        LLVMPositionBuilderAtEnd(ctx->builder, oob_bb);
        {
            LLVMValueRef wa[3] = {i_val, j_val, len_val};
            emit_printf(ctx,
                        "[warning] vec.swap() index out of bounds: i=%d, j=%d, len=%d\n", wa, 3);
        }
        LLVMBuildBr(ctx->builder, end_bb);

        /* chk_bb: if i == j, nop */
        LLVMPositionBuilderAtEnd(ctx->builder, chk_bb);
        LLVMValueRef eq_ij = LLVMBuildICmp(ctx->builder, LLVMIntEQ, i_val, j_val, "vsw.eq");
        LLVMBuildCondBr(ctx->builder, eq_ij, end_bb, do_bb);

        /* do_bb: raw byte swap via load/store (ownership moves in place, no free/clone) */
        LLVMPositionBuilderAtEnd(ctx->builder, do_bb);
#if CG_DEBUG
        {
            LLVMValueRef dbg_args[2] = {i_val, j_val};
            cg_emit_debug_printf(ctx, "[cg] vec.swap  i=%d j=%d\n", dbg_args, 2);
        }
#endif
        LLVMValueRef i64_i = LLVMBuildSExt(ctx->builder, i_val, i64_t, "vsw.i64i");
        LLVMValueRef i64_j = LLVMBuildSExt(ctx->builder, j_val, i64_t, "vsw.i64j");
        LLVMValueRef ptr_i = LLVMBuildGEP2(ctx->builder, elem_llvm, data_ptr, &i64_i, 1, "vsw.pi");
        LLVMValueRef ptr_j = LLVMBuildGEP2(ctx->builder, elem_llvm, data_ptr, &i64_j, 1, "vsw.pj");
        LLVMValueRef tmp = LLVMBuildLoad2(ctx->builder, elem_llvm, ptr_i, "vsw.tmp");
        LLVMValueRef jv = LLVMBuildLoad2(ctx->builder, elem_llvm, ptr_j, "vsw.jv");
        LLVMBuildStore(ctx->builder, jv, ptr_i);
        LLVMBuildStore(ctx->builder, tmp, ptr_j);
        LLVMBuildBr(ctx->builder, end_bb);

        LLVMPositionBuilderAtEnd(ctx->builder, end_bb);
        return NULL;
    }

    /* ---- reverse() -> void  — reverse elements in-place (raw byte swap loop) ---- */
    if (strcmp(method, "reverse") == 0)
    {
        LLVMValueRef vec_val = LLVMBuildLoad2(ctx->builder, vec_t, vec_alloca, "vrev.v");
        LLVMValueRef len_val = LLVMBuildExtractValue(ctx->builder, vec_val, 1, "vrev.len");
        LLVMValueRef data_ptr = LLVMBuildExtractValue(ctx->builder, vec_val, 0, "vrev.data");

        LLVMValueRef cur_fn = LLVMGetBasicBlockParent(LLVMGetInsertBlock(ctx->builder));

#if CG_DEBUG
        {
            LLVMValueRef dbg_args[1] = {len_val};
            cg_emit_debug_printf(ctx, "[cg] vec.reverse  len=%d\n", dbg_args, 1);
        }
#endif

        /* Only reverse if len >= 2 */
        LLVMValueRef two32 = LLVMConstInt(i32_t, 2, 0);
        LLVMValueRef need_rv = LLVMBuildICmp(ctx->builder, LLVMIntSGE, len_val, two32, "vrev.nr");

        LLVMBasicBlockRef do_bb = LLVMAppendBasicBlockInContext(ctx->context, cur_fn, "vrev.do");
        LLVMBasicBlockRef cond_bb = LLVMAppendBasicBlockInContext(ctx->context, cur_fn, "vrev.cond");
        LLVMBasicBlockRef body_bb = LLVMAppendBasicBlockInContext(ctx->context, cur_fn, "vrev.body");
        LLVMBasicBlockRef end_bb = LLVMAppendBasicBlockInContext(ctx->context, cur_fn, "vrev.end");
        LLVMBuildCondBr(ctx->builder, need_rv, do_bb, end_bb);

        /* do_bb: set up loop counter k = 0, half = len / 2 */
        LLVMPositionBuilderAtEnd(ctx->builder, do_bb);
        LLVMBuilderRef tb = LLVMCreateBuilderInContext(ctx->context);
        LLVMBasicBlockRef entry_bb = LLVMGetEntryBasicBlock(cur_fn);
        LLVMValueRef fi = LLVMGetFirstInstruction(entry_bb);
        if (fi)
            LLVMPositionBuilderBefore(tb, fi);
        else
            LLVMPositionBuilderAtEnd(tb, entry_bb);
        LLVMValueRef k_alloca = LLVMBuildAlloca(tb, i32_t, "vrev.k");
        LLVMDisposeBuilder(tb);

        LLVMValueRef zero32 = LLVMConstInt(i32_t, 0, 0);
        LLVMBuildStore(ctx->builder, zero32, k_alloca);
        LLVMValueRef half = LLVMBuildSDiv(ctx->builder, len_val, two32, "vrev.half");
        LLVMBuildBr(ctx->builder, cond_bb);

        /* cond_bb: while k < half */
        LLVMPositionBuilderAtEnd(ctx->builder, cond_bb);
        LLVMValueRef k = LLVMBuildLoad2(ctx->builder, i32_t, k_alloca, "vrev.k");
        LLVMValueRef cmp = LLVMBuildICmp(ctx->builder, LLVMIntSLT, k, half, "vrev.lt");
        LLVMBuildCondBr(ctx->builder, cmp, body_bb, end_bb);

        /* body_bb: swap data[k] and data[len-1-k] via raw load/store */
        LLVMPositionBuilderAtEnd(ctx->builder, body_bb);
        LLVMValueRef one32 = LLVMConstInt(i32_t, 1, 0);
        LLVMValueRef lm1 = LLVMBuildSub(ctx->builder, len_val, one32, "vrev.lm1");
        LLVMValueRef j = LLVMBuildSub(ctx->builder, lm1, k, "vrev.j");
        LLVMValueRef k64 = LLVMBuildSExt(ctx->builder, k, i64_t, "vrev.k64");
        LLVMValueRef j64 = LLVMBuildSExt(ctx->builder, j, i64_t, "vrev.j64");
        LLVMValueRef pk = LLVMBuildGEP2(ctx->builder, elem_llvm, data_ptr, &k64, 1, "vrev.pk");
        LLVMValueRef pj = LLVMBuildGEP2(ctx->builder, elem_llvm, data_ptr, &j64, 1, "vrev.pj");
        LLVMValueRef tmp = LLVMBuildLoad2(ctx->builder, elem_llvm, pk, "vrev.tmp");
        LLVMValueRef jv = LLVMBuildLoad2(ctx->builder, elem_llvm, pj, "vrev.jv");
        LLVMBuildStore(ctx->builder, jv, pk);
        LLVMBuildStore(ctx->builder, tmp, pj);

        /* k++ */
        LLVMValueRef next_k = LLVMBuildAdd(ctx->builder, k, one32, "vrev.nk");
        LLVMBuildStore(ctx->builder, next_k, k_alloca);
        LLVMBuildBr(ctx->builder, cond_bb);

        LLVMPositionBuilderAtEnd(ctx->builder, end_bb);
        (void)data_ptr;
        return NULL;
    }

    /* ---- extend(src) -> void  — append all elements of src (deep-clone non-trivials) ---- */
    if (strcmp(method, "extend") == 0)
    {
        LLVMValueRef cur_fn = LLVMGetBasicBlockParent(LLVMGetInsertBlock(ctx->builder));
        LLVMValueRef zero32 = LLVMConstInt(i32_t, 0, 0);
        LLVMValueRef one32 = LLVMConstInt(i32_t, 1, 0);
        LLVMValueRef two32 = LLVMConstInt(i32_t, 2, 0);
        LLVMValueRef esz = LLVMSizeOf(elem_llvm); /* compile-time constant */

        /* Evaluate src (the source vec aggregate value) */
        LLVMValueRef src_val = codegen_expr(ctx, call_node->as.call.args[0]);
        if (!src_val)
            return NULL;
        LLVMValueRef src_len = LLVMBuildExtractValue(ctx->builder, src_val, 1, "vex.sl");
        LLVMValueRef src_data = LLVMBuildExtractValue(ctx->builder, src_val, 0, "vex.sd");

        /* Skip everything if src is empty */
        LLVMValueRef src_ne = LLVMBuildICmp(ctx->builder, LLVMIntSGT, src_len, zero32, "vex.ne");
        LLVMBasicBlockRef do_bb = LLVMAppendBasicBlockInContext(ctx->context, cur_fn, "vex.do");
        LLVMBasicBlockRef end_bb = LLVMAppendBasicBlockInContext(ctx->context, cur_fn, "vex.end");
        LLVMBuildCondBr(ctx->builder, src_ne, do_bb, end_bb);

        LLVMPositionBuilderAtEnd(ctx->builder, do_bb);

        /* Load self */
        LLVMValueRef sv = LLVMBuildLoad2(ctx->builder, vec_t, vec_alloca, "vex.sv");
        LLVMValueRef sl = LLVMBuildExtractValue(ctx->builder, sv, 1, "vex.selflen");
        LLVMValueRef sc = LLVMBuildExtractValue(ctx->builder, sv, 2, "vex.selfcap");

        /* needed = self_len + src_len */
        LLVMValueRef needed = LLVMBuildAdd(ctx->builder, sl, src_len, "vex.needed");

        /* Inline reserve: if needed > self_cap → realloc with max(self_cap*2, needed) */
        LLVMValueRef must_grow = LLVMBuildICmp(ctx->builder, LLVMIntSGT, needed, sc, "vex.mg");
        LLVMBasicBlockRef grow_bb = LLVMAppendBasicBlockInContext(ctx->context, cur_fn, "vex.grow");
        LLVMBasicBlockRef after_grow = LLVMAppendBasicBlockInContext(ctx->context, cur_fn, "vex.cpy");
        LLVMBuildCondBr(ctx->builder, must_grow, grow_bb, after_grow);

        LLVMPositionBuilderAtEnd(ctx->builder, grow_bb);
        {
            LLVMValueRef sv2 = LLVMBuildLoad2(ctx->builder, vec_t, vec_alloca, "vex.sv2");
            LLVMValueRef od = LLVMBuildExtractValue(ctx->builder, sv2, 0, "vex.od");
            LLVMValueRef oc = LLVMBuildExtractValue(ctx->builder, sv2, 2, "vex.oc");
            LLVMValueRef dbl = LLVMBuildMul(ctx->builder, oc, two32, "vex.dbl");
            LLVMValueRef use_dbl = LLVMBuildICmp(ctx->builder, LLVMIntSGT, dbl, needed, "vex.ud");
            LLVMValueRef new_cap = LLVMBuildSelect(ctx->builder, use_dbl, dbl, needed, "vex.nc");
            LLVMValueRef nc64 = LLVMBuildSExt(ctx->builder, new_cap, i64_t, "vex.nc64");
            LLVMValueRef bytes = LLVMBuildMul(ctx->builder, nc64, esz, "vex.bytes");
#if CG_DEBUG
            {
                LLVMValueRef dbg_args[2] = {oc, new_cap};
                cg_emit_debug_printf(ctx, "[cg] vec.extend.grow  old_cap=%d new_cap=%d\n",
                                     dbg_args, 2);
            }
#endif
            LLVMValueRef realloc_fn = LLVMGetNamedFunction(ctx->module, "realloc");
            LLVMTypeRef realloc_ft = LLVMGlobalGetValueType(realloc_fn);
            LLVMValueRef ra_args[2] = {od, bytes};
            LLVMValueRef new_data = LLVMBuildCall2(ctx->builder, realloc_ft, realloc_fn,
                                                   ra_args, 2, "vex.nd");
            LLVMValueRef sv3 = LLVMBuildLoad2(ctx->builder, vec_t, vec_alloca, "vex.sv3");
            sv3 = LLVMBuildInsertValue(ctx->builder, sv3, new_data, 0, "vex.id");
            sv3 = LLVMBuildInsertValue(ctx->builder, sv3, new_cap, 2, "vex.ic");
            LLVMBuildStore(ctx->builder, sv3, vec_alloca);
        }
        LLVMBuildBr(ctx->builder, after_grow);

        /* after_grow (vex.cpy): copy elements into self */
        LLVMPositionBuilderAtEnd(ctx->builder, after_grow);
        /* Reload self data + len after potential realloc */
        LLVMValueRef sv4 = LLVMBuildLoad2(ctx->builder, vec_t, vec_alloca, "vex.sv4");
        LLVMValueRef sl4 = LLVMBuildExtractValue(ctx->builder, sv4, 1, "vex.sl4");
        LLVMValueRef sd4 = LLVMBuildExtractValue(ctx->builder, sv4, 0, "vex.sd4");

        bool elem_needs_drop = (elem_type &&
                                (elem_type->kind == TYPE_STRING ||
                                 (elem_type->kind == TYPE_STRUCT &&
                                  elem_type->as.strukt.has_drop)));
        if (!elem_needs_drop)
        {
            /* Trivial: single memcpy(self_data + self_len, src_data, src_len * elem_size) */
            LLVMValueRef sl64 = LLVMBuildSExt(ctx->builder, sl4, i64_t, "vex.sl64");
            LLVMValueRef dst = LLVMBuildGEP2(ctx->builder, elem_llvm, sd4, &sl64, 1, "vex.dst");
            LLVMValueRef slen64 = LLVMBuildSExt(ctx->builder, src_len, i64_t, "vex.slen64");
            LLVMValueRef copy_bytes = LLVMBuildMul(ctx->builder, slen64, esz, "vex.cpb");
            LLVMValueRef memcpy_fn = LLVMGetNamedFunction(ctx->module, "memcpy");
            LLVMTypeRef memcpy_ft = LLVMGlobalGetValueType(memcpy_fn);
            LLVMValueRef mc_args[3] = {dst, src_data, copy_bytes};
            LLVMBuildCall2(ctx->builder, memcpy_ft, memcpy_fn, mc_args, 3, "");
#if CG_DEBUG
            {
                LLVMValueRef dbg_args[1] = {src_len};
                cg_emit_debug_printf(ctx, "[cg] vec.extend  (trivial memcpy) count=%d\n",
                                     dbg_args, 1);
            }
#endif
        }
        else
        {
            /* Non-trivial: clone loop — for k in 0..src_len */
            /* Alloca loop counter k in entry block */
            LLVMBasicBlockRef entry = LLVMGetEntryBasicBlock(cur_fn);
            LLVMBuilderRef tb = LLVMCreateBuilderInContext(ctx->context);
            LLVMValueRef fi = LLVMGetFirstInstruction(entry);
            if (fi)
                LLVMPositionBuilderBefore(tb, fi);
            else
                LLVMPositionBuilderAtEnd(tb, entry);
            LLVMValueRef k_alloca = LLVMBuildAlloca(tb, i32_t, "vex.k");
            LLVMDisposeBuilder(tb);

            LLVMBuildStore(ctx->builder, zero32, k_alloca);

            LLVMBasicBlockRef lc_bb = LLVMAppendBasicBlockInContext(ctx->context, cur_fn, "vex.lcond");
            LLVMBasicBlockRef lb_bb = LLVMAppendBasicBlockInContext(ctx->context, cur_fn, "vex.lbody");
            LLVMBasicBlockRef le_bb = LLVMAppendBasicBlockInContext(ctx->context, cur_fn, "vex.lend");
            LLVMBuildBr(ctx->builder, lc_bb);

            /* lc_bb: while k < src_len */
            LLVMPositionBuilderAtEnd(ctx->builder, lc_bb);
            LLVMValueRef kv = LLVMBuildLoad2(ctx->builder, i32_t, k_alloca, "vex.kv");
            LLVMValueRef klt = LLVMBuildICmp(ctx->builder, LLVMIntSLT, kv, src_len, "vex.klt");
            LLVMBuildCondBr(ctx->builder, klt, lb_bb, le_bb);

            /* lb_bb: clone src.data[k] → self.data[self_len + k] */
            LLVMPositionBuilderAtEnd(ctx->builder, lb_bb);
            LLVMValueRef sv5 = LLVMBuildLoad2(ctx->builder, vec_t, vec_alloca, "vex.sv5");
            LLVMValueRef sd5 = LLVMBuildExtractValue(ctx->builder, sv5, 0, "vex.sd5");
            LLVMValueRef sl5 = LLVMBuildExtractValue(ctx->builder, sv5, 1, "vex.sl5");
            LLVMValueRef kv2 = LLVMBuildLoad2(ctx->builder, i32_t, k_alloca, "vex.kv2");
            LLVMValueRef k64 = LLVMBuildSExt(ctx->builder, kv2, i64_t, "vex.k64");

            LLVMValueRef sep = LLVMBuildGEP2(ctx->builder, elem_llvm, src_data, &k64, 1, "vex.sep");
            LLVMValueRef se = LLVMBuildLoad2(ctx->builder, elem_llvm, sep, "vex.se");

            LLVMValueRef cloned;
            if (elem_type->kind == TYPE_STRING)
                cloned = emit_string_clone_val(ctx, se);
            else
                cloned = emit_struct_clone_val(ctx, se, elem_llvm, elem_type);

            LLVMValueRef di = LLVMBuildAdd(ctx->builder, sl5, kv2, "vex.di");
            LLVMValueRef di64 = LLVMBuildSExt(ctx->builder, di, i64_t, "vex.di64");
            LLVMValueRef dep = LLVMBuildGEP2(ctx->builder, elem_llvm, sd5, &di64, 1, "vex.dep");
            LLVMBuildStore(ctx->builder, cloned, dep);

            /* k++ and back to lc_bb */
            LLVMValueRef nk = LLVMBuildAdd(ctx->builder, kv2, one32, "vex.nk");
            LLVMBuildStore(ctx->builder, nk, k_alloca);
            LLVMBuildBr(ctx->builder, lc_bb);

            /* Position at loop exit (le_bb) for the len update below */
            LLVMPositionBuilderAtEnd(ctx->builder, le_bb);
        }

        /* Update self.len += src_len  (builder is at after_grow for trivial, le_bb for non-trivial) */
        LLVMValueRef sv6 = LLVMBuildLoad2(ctx->builder, vec_t, vec_alloca, "vex.sv6");
        LLVMValueRef sl6 = LLVMBuildExtractValue(ctx->builder, sv6, 1, "vex.sl6");
        LLVMValueRef new_len = LLVMBuildAdd(ctx->builder, sl6, src_len, "vex.nl");
        sv6 = LLVMBuildInsertValue(ctx->builder, sv6, new_len, 1, "vex.ul");
        LLVMBuildStore(ctx->builder, sv6, vec_alloca);
        LLVMBuildBr(ctx->builder, end_bb);

        LLVMPositionBuilderAtEnd(ctx->builder, end_bb);
        (void)src_data;
        (void)zero32;
        (void)one32;
        (void)two32;
        (void)esz;
        return NULL;
    }

    /* ---- insert(i, x) -> void  — insert x at position i, shift [i, len) right ---- */
    if (strcmp(method, "insert") == 0)
    {
        LLVMValueRef cur_fn = LLVMGetBasicBlockParent(LLVMGetInsertBlock(ctx->builder));
        LLVMValueRef zero32 = LLVMConstInt(i32_t, 0, 0);
        LLVMValueRef one32 = LLVMConstInt(i32_t, 1, 0);

        /* Record temp mark before evaluating args */
        int temp_mark_ins = ctx->temp_string_count;

        /* Evaluate index i and value x */
        LLVMValueRef i_val = codegen_expr(ctx, call_node->as.call.args[0]);
        if (!i_val)
            return NULL;
        LLVMValueRef x_val = codegen_expr(ctx, call_node->as.call.args[1]);
        if (!x_val)
            return NULL;

        /* Load self len */
        LLVMValueRef sv = LLVMBuildLoad2(ctx->builder, vec_t, vec_alloca, "vins.sv");
        LLVMValueRef len = LLVMBuildExtractValue(ctx->builder, sv, 1, "vins.len");

        /* Bounds check: 0 <= i <= len  (i == len means append, valid) */
        LLVMValueRef ge0 = LLVMBuildICmp(ctx->builder, LLVMIntSGE, i_val, zero32, "vins.ge0");
        LLVMValueRef lel = LLVMBuildICmp(ctx->builder, LLVMIntSLE, i_val, len, "vins.lel");
        LLVMValueRef ok = LLVMBuildAnd(ctx->builder, ge0, lel, "vins.ok");

        LLVMBasicBlockRef do_bb = LLVMAppendBasicBlockInContext(ctx->context, cur_fn, "vins.do");
        LLVMBasicBlockRef oob_bb = LLVMAppendBasicBlockInContext(ctx->context, cur_fn, "vins.oob");
        LLVMBasicBlockRef end_bb = LLVMAppendBasicBlockInContext(ctx->context, cur_fn, "vins.end");
        LLVMBuildCondBr(ctx->builder, ok, do_bb, oob_bb);

        /* oob_bb: warn and skip */
        LLVMPositionBuilderAtEnd(ctx->builder, oob_bb);
        {
            LLVMValueRef wa[2] = {i_val, len};
            emit_printf(ctx,
                        "[warning] vec.insert() index out of bounds (i=%d len=%d)\n", wa, 2);
        }
        LLVMBuildBr(ctx->builder, end_bb);

        /* do_bb: grow, conditional memmove, store, len++ */
        LLVMPositionBuilderAtEnd(ctx->builder, do_bb);

        /* Grow capacity by 1 if needed */
        emit_vec_grow_inline(ctx, vec_alloca, elem_llvm);

        /* Reload self after potential grow */
        LLVMValueRef sv2 = LLVMBuildLoad2(ctx->builder, vec_t, vec_alloca, "vins.sv2");
        LLVMValueRef len2 = LLVMBuildExtractValue(ctx->builder, sv2, 1, "vins.len2");
        LLVMValueRef dat2 = LLVMBuildExtractValue(ctx->builder, sv2, 0, "vins.dat2");

        /* If i < len, memmove data[i+1..len] = data[i..len-1]  (shift right by 1) */
        LLVMValueRef need_shift = LLVMBuildICmp(ctx->builder, LLVMIntSLT, i_val, len2, "vins.ns");
        LLVMBasicBlockRef shift_bb = LLVMAppendBasicBlockInContext(ctx->context, cur_fn, "vins.shift");
        LLVMBasicBlockRef store_bb = LLVMAppendBasicBlockInContext(ctx->context, cur_fn, "vins.store");
        LLVMBuildCondBr(ctx->builder, need_shift, shift_bb, store_bb);

        LLVMPositionBuilderAtEnd(ctx->builder, shift_bb);
        {
            LLVMValueRef i64 = LLVMBuildSExt(ctx->builder, i_val, i64_t, "vins.i64");
            LLVMValueRef ip1 = LLVMBuildAdd(ctx->builder, i_val, one32, "vins.ip1");
            LLVMValueRef ip1_64 = LLVMBuildSExt(ctx->builder, ip1, i64_t, "vins.ip164");
            LLVMValueRef mv_src = LLVMBuildGEP2(ctx->builder, elem_llvm, dat2, &i64, 1, "vins.msrc");
            LLVMValueRef mv_dst = LLVMBuildGEP2(ctx->builder, elem_llvm, dat2, &ip1_64, 1, "vins.mdst");
            /* count = len - i elements to shift */
            LLVMValueRef cnt = LLVMBuildSub(ctx->builder, len2, i_val, "vins.cnt");
            LLVMValueRef cnt64 = LLVMBuildSExt(ctx->builder, cnt, i64_t, "vins.cnt64");
            LLVMValueRef mv_bytes = LLVMBuildMul(ctx->builder, cnt64, LLVMSizeOf(elem_llvm), "vins.mvb");
            LLVMValueRef memmove_fn = LLVMGetNamedFunction(ctx->module, "memmove");
            LLVMTypeRef memmove_ft = LLVMGlobalGetValueType(memmove_fn);
            LLVMValueRef mm_args[3] = {mv_dst, mv_src, mv_bytes};
            LLVMBuildCall2(ctx->builder, memmove_ft, memmove_fn, mm_args, 3, "");
        }
        LLVMBuildBr(ctx->builder, store_bb);

        /* store_bb: store x at data[i] */
        LLVMPositionBuilderAtEnd(ctx->builder, store_bb);
        {
            LLVMValueRef sv3 = LLVMBuildLoad2(ctx->builder, vec_t, vec_alloca, "vins.sv3");
            LLVMValueRef dat3 = LLVMBuildExtractValue(ctx->builder, sv3, 0, "vins.dat3");
            LLVMValueRef sti = LLVMBuildSExt(ctx->builder, i_val, i64_t, "vins.sti");
            LLVMValueRef slot = LLVMBuildGEP2(ctx->builder, elem_llvm, dat3, &sti, 1, "vins.slot");
            LLVMBuildStore(ctx->builder, x_val, slot);
        }

        /* Transfer ownership — same pattern as push() */
        if (elem_type->kind == TYPE_STRING)
        {
            cg_mark_last_temp_moved(ctx, temp_mark_ins, "vec.insert: temp owned by vec");
            AstNode *arg1 = ast_unwrap_move(call_node->as.call.args[1]);
            if (arg1->kind == AST_IDENT)
            {
                CgSymbol *argsym = cg_scope_resolve(ctx->current_scope, arg1->as.ident.name);
                if (argsym && argsym->type && argsym->type->kind == TYPE_STRING)
                    mark_string_moved(ctx, argsym->value, "vec.insert: var owned by vec");
            }
        }
        if (elem_type->kind == TYPE_STRUCT && elem_type->as.strukt.has_drop)
        {
            for (int t = temp_mark_ins; t < ctx->temp_string_count; t++)
                mark_string_moved(ctx, ctx->temp_string_slots[t], "vec.insert: struct temp owned by vec");
            AstNode *arg1 = ast_unwrap_move(call_node->as.call.args[1]);
            if (arg1->kind == AST_IDENT)
            {
                CgSymbol *argsym = cg_scope_resolve(ctx->current_scope, arg1->as.ident.name);
                if (argsym && argsym->moved_flag)
                {
                    LLVMTypeRef i1_t = LLVMInt1TypeInContext(ctx->context);
                    LLVMBuildStore(ctx->builder, LLVMConstInt(i1_t, 1, 0), argsym->moved_flag);
                }
            }
        }

        /* len++ */
        LLVMValueRef sv4 = LLVMBuildLoad2(ctx->builder, vec_t, vec_alloca, "vins.sv4");
        LLVMValueRef len4 = LLVMBuildExtractValue(ctx->builder, sv4, 1, "vins.len4");
        LLVMValueRef nl = LLVMBuildAdd(ctx->builder, len4, one32, "vins.nl");
        sv4 = LLVMBuildInsertValue(ctx->builder, sv4, nl, 1, "vins.ul");
        LLVMBuildStore(ctx->builder, sv4, vec_alloca);
        LLVMBuildBr(ctx->builder, end_bb);

        LLVMPositionBuilderAtEnd(ctx->builder, end_bb);
        (void)zero32;
        return NULL;
    }

    /* ---- contains(x) -> bool  — linear search, true if any element equals x ---- */
    if (strcmp(method, "contains") == 0 || strcmp(method, "index_of") == 0)
    {
        bool is_contains = (strcmp(method, "contains") == 0);
        const char *pfx = is_contains ? "vcnt" : "vidx";
        LLVMValueRef cur_fn = LLVMGetBasicBlockParent(LLVMGetInsertBlock(ctx->builder));
        LLVMTypeRef i1_t = LLVMInt1TypeInContext(ctx->context);

        /* Evaluate x */
        LLVMValueRef x_val = codegen_expr(ctx, call_node->as.call.args[0]);
        if (!x_val)
            return NULL;

        /* Alloca result + loop counter in entry block */
        LLVMBasicBlockRef entry = LLVMGetEntryBasicBlock(cur_fn);
        LLVMBuilderRef tb = LLVMCreateBuilderInContext(ctx->context);
        LLVMValueRef fi = LLVMGetFirstInstruction(entry);
        if (fi)
            LLVMPositionBuilderBefore(tb, fi);
        else
            LLVMPositionBuilderAtEnd(tb, entry);
        LLVMValueRef res_alloca = LLVMBuildAlloca(tb, is_contains ? i1_t : i32_t, "vsi.res");
        LLVMValueRef k_alloca = LLVMBuildAlloca(tb, i32_t, "vsi.k");
        LLVMDisposeBuilder(tb);

        /* Init: result = false / -1,  k = 0 */
        LLVMValueRef zero32 = LLVMConstInt(i32_t, 0, 0);
        LLVMValueRef one32 = LLVMConstInt(i32_t, 1, 0);
        LLVMValueRef neg1_32 = LLVMConstInt(i32_t, (unsigned long long)-1, 1);
        if (is_contains)
            LLVMBuildStore(ctx->builder, LLVMConstInt(i1_t, 0, 0), res_alloca);
        else
            LLVMBuildStore(ctx->builder, neg1_32, res_alloca);
        LLVMBuildStore(ctx->builder, zero32, k_alloca);

        /* Load vec */
        LLVMValueRef sv = LLVMBuildLoad2(ctx->builder, vec_t, vec_alloca, "vsi.sv");
        LLVMValueRef len = LLVMBuildExtractValue(ctx->builder, sv, 1, "vsi.len");
        LLVMValueRef data = LLVMBuildExtractValue(ctx->builder, sv, 0, "vsi.data");

        /* Loop blocks */
        char cond_name[32], body_name[32], found_name[32], next_name[32], end_name[32];
        snprintf(cond_name, sizeof(cond_name), "%s.cond", pfx);
        snprintf(body_name, sizeof(body_name), "%s.body", pfx);
        snprintf(found_name, sizeof(found_name), "%s.found", pfx);
        snprintf(next_name, sizeof(next_name), "%s.next", pfx);
        snprintf(end_name, sizeof(end_name), "%s.end", pfx);

        LLVMBasicBlockRef cond_bb = LLVMAppendBasicBlockInContext(ctx->context, cur_fn, cond_name);
        LLVMBasicBlockRef body_bb = LLVMAppendBasicBlockInContext(ctx->context, cur_fn, body_name);
        LLVMBasicBlockRef found_bb = LLVMAppendBasicBlockInContext(ctx->context, cur_fn, found_name);
        LLVMBasicBlockRef next_bb = LLVMAppendBasicBlockInContext(ctx->context, cur_fn, next_name);
        LLVMBasicBlockRef end_bb = LLVMAppendBasicBlockInContext(ctx->context, cur_fn, end_name);
        LLVMBuildBr(ctx->builder, cond_bb);

        /* cond_bb: while k < len */
        LLVMPositionBuilderAtEnd(ctx->builder, cond_bb);
        LLVMValueRef kv = LLVMBuildLoad2(ctx->builder, i32_t, k_alloca, "vsi.kv");
        LLVMValueRef klt = LLVMBuildICmp(ctx->builder, LLVMIntSLT, kv, len, "vsi.klt");
        LLVMBuildCondBr(ctx->builder, klt, body_bb, end_bb);

        /* body_bb: load elem[k], compare with x */
        LLVMPositionBuilderAtEnd(ctx->builder, body_bb);
        LLVMValueRef kv64 = LLVMBuildSExt(ctx->builder, kv, i64_t, "vsi.kv64");
        LLVMValueRef ep = LLVMBuildGEP2(ctx->builder, elem_llvm, data, &kv64, 1, "vsi.ep");
        LLVMValueRef ev = LLVMBuildLoad2(ctx->builder, elem_llvm, ep, "vsi.ev");

        /* Element comparison */
        LLVMValueRef is_eq;
        if (elem_type->kind == TYPE_STRING)
        {
            LLVMValueRef ad = LLVMBuildExtractValue(ctx->builder, ev, 0, "vsi.ad");
            LLVMValueRef bd = LLVMBuildExtractValue(ctx->builder, x_val, 0, "vsi.bd");
            LLVMValueRef strcmp_fn = LLVMGetNamedFunction(ctx->module, "strcmp");
            LLVMTypeRef strcmp_ft = LLVMGlobalGetValueType(strcmp_fn);
            LLVMValueRef sc_args[2] = {ad, bd};
            LLVMValueRef sc = LLVMBuildCall2(ctx->builder, strcmp_ft, strcmp_fn,
                                             sc_args, 2, "vsi.sc");
            is_eq = LLVMBuildICmp(ctx->builder, LLVMIntEQ, sc, zero32, "vsi.eq");
        }
        else if (elem_type->kind == TYPE_F32 || elem_type->kind == TYPE_F64)
        {
            is_eq = LLVMBuildFCmp(ctx->builder, LLVMRealOEQ, ev, x_val, "vsi.eq");
        }
        else
        {
            is_eq = LLVMBuildICmp(ctx->builder, LLVMIntEQ, ev, x_val, "vsi.eq");
        }
        LLVMBuildCondBr(ctx->builder, is_eq, found_bb, next_bb);

        /* found_bb: store result (true / k) and exit loop */
        LLVMPositionBuilderAtEnd(ctx->builder, found_bb);
        if (is_contains)
            LLVMBuildStore(ctx->builder, LLVMConstInt(i1_t, 1, 0), res_alloca);
        else
            LLVMBuildStore(ctx->builder, kv, res_alloca);
        LLVMBuildBr(ctx->builder, end_bb);

        /* next_bb: k++ */
        LLVMPositionBuilderAtEnd(ctx->builder, next_bb);
        LLVMValueRef nk = LLVMBuildAdd(ctx->builder, kv, one32, "vsi.nk");
        LLVMBuildStore(ctx->builder, nk, k_alloca);
        LLVMBuildBr(ctx->builder, cond_bb);

        /* end_bb: return result */
        LLVMPositionBuilderAtEnd(ctx->builder, end_bb);
        LLVMTypeRef res_t = is_contains ? i1_t : i32_t;
        return LLVMBuildLoad2(ctx->builder, res_t, res_alloca, "vsi.ret");
    }

    /* ---- resize(n) -> void  — grow (zero/empty fill) or shrink (drop excess) ---- */
    if (strcmp(method, "resize") == 0)
    {
        LLVMValueRef cur_fn = LLVMGetBasicBlockParent(LLVMGetInsertBlock(ctx->builder));
        LLVMValueRef zero32 = LLVMConstInt(i32_t, 0, 0);
        LLVMValueRef one32 = LLVMConstInt(i32_t, 1, 0);

        /* Evaluate n, clamp negative to 0 */
        LLVMValueRef n_val = codegen_expr(ctx, call_node->as.call.args[0]);
        if (!n_val)
            return NULL;

        LLVMValueRef is_neg = LLVMBuildICmp(ctx->builder, LLVMIntSLT, n_val, zero32, "vrsz.neg");
        LLVMValueRef n_clamped = LLVMBuildSelect(ctx->builder, is_neg, zero32, n_val, "vrsz.nc");

        /* Load self */
        LLVMValueRef sv = LLVMBuildLoad2(ctx->builder, vec_t, vec_alloca, "vrsz.sv");
        LLVMValueRef len = LLVMBuildExtractValue(ctx->builder, sv, 1, "vrsz.len");

        LLVMValueRef gt = LLVMBuildICmp(ctx->builder, LLVMIntSGT, n_clamped, len, "vrsz.gt");
        LLVMValueRef lt = LLVMBuildICmp(ctx->builder, LLVMIntSLT, n_clamped, len, "vrsz.lt");

        LLVMBasicBlockRef grow_bb = LLVMAppendBasicBlockInContext(ctx->context, cur_fn, "vrsz.grow");
        LLVMBasicBlockRef shrink_bb = LLVMAppendBasicBlockInContext(ctx->context, cur_fn, "vrsz.shrink");
        LLVMBasicBlockRef end_bb = LLVMAppendBasicBlockInContext(ctx->context, cur_fn, "vrsz.end");

        /* gt → grow, lt → shrink, else → nop (end) */
        LLVMBasicBlockRef chk_shrink = LLVMAppendBasicBlockInContext(ctx->context, cur_fn, "vrsz.chk");
        LLVMBuildCondBr(ctx->builder, gt, grow_bb, chk_shrink);
        LLVMPositionBuilderAtEnd(ctx->builder, chk_shrink);
        LLVMBuildCondBr(ctx->builder, lt, shrink_bb, end_bb);

        /* ----- grow_bb: realloc if needed, zero-fill [len, n_clamped), update len ----- */
        LLVMPositionBuilderAtEnd(ctx->builder, grow_bb);
        {
            LLVMValueRef sv2 = LLVMBuildLoad2(ctx->builder, vec_t, vec_alloca, "vrsz.sv2");
            LLVMValueRef cap2 = LLVMBuildExtractValue(ctx->builder, sv2, 2, "vrsz.cap2");

            /* Inline reserve: if n_clamped > cap → realloc with max(cap*2, n_clamped) */
            LLVMValueRef mg = LLVMBuildICmp(ctx->builder, LLVMIntSGT, n_clamped, cap2, "vrsz.mg");
            LLVMBasicBlockRef ra_bb = LLVMAppendBasicBlockInContext(ctx->context, cur_fn, "vrsz.ra");
            LLVMBasicBlockRef fill_bb = LLVMAppendBasicBlockInContext(ctx->context, cur_fn, "vrsz.fill");
            LLVMBuildCondBr(ctx->builder, mg, ra_bb, fill_bb);

            LLVMPositionBuilderAtEnd(ctx->builder, ra_bb);
            {
                LLVMValueRef sv3 = LLVMBuildLoad2(ctx->builder, vec_t, vec_alloca, "vrsz.sv3");
                LLVMValueRef od = LLVMBuildExtractValue(ctx->builder, sv3, 0, "vrsz.od");
                LLVMValueRef oc = LLVMBuildExtractValue(ctx->builder, sv3, 2, "vrsz.oc");
                LLVMValueRef two32 = LLVMConstInt(i32_t, 2, 0);
                LLVMValueRef dbl = LLVMBuildMul(ctx->builder, oc, two32, "vrsz.dbl");
                LLVMValueRef use_d = LLVMBuildICmp(ctx->builder, LLVMIntSGT, dbl, n_clamped, "vrsz.ud");
                LLVMValueRef new_cap = LLVMBuildSelect(ctx->builder, use_d, dbl, n_clamped, "vrsz.nc2");
                LLVMValueRef nc64 = LLVMBuildSExt(ctx->builder, new_cap, i64_t, "vrsz.nc64");
                LLVMValueRef bytes = LLVMBuildMul(ctx->builder, nc64, LLVMSizeOf(elem_llvm), "vrsz.bytes");
                LLVMValueRef realloc_fn = LLVMGetNamedFunction(ctx->module, "realloc");
                LLVMTypeRef realloc_ft = LLVMGlobalGetValueType(realloc_fn);
                LLVMValueRef ra_args[2] = {od, bytes};
                LLVMValueRef nd = LLVMBuildCall2(ctx->builder, realloc_ft, realloc_fn,
                                                 ra_args, 2, "vrsz.nd");
                sv3 = LLVMBuildInsertValue(ctx->builder, sv3, nd, 0, "vrsz.id");
                sv3 = LLVMBuildInsertValue(ctx->builder, sv3, new_cap, 2, "vrsz.ic");
                LLVMBuildStore(ctx->builder, sv3, vec_alloca);
            }
            LLVMBuildBr(ctx->builder, fill_bb);

            /* fill_bb: loop from len to n_clamped, store zero/empty element */
            LLVMPositionBuilderAtEnd(ctx->builder, fill_bb);

            /* Alloca fill loop counter in entry block */
            LLVMBasicBlockRef entry2 = LLVMGetEntryBasicBlock(cur_fn);
            LLVMBuilderRef tb2 = LLVMCreateBuilderInContext(ctx->context);
            LLVMValueRef fi2 = LLVMGetFirstInstruction(entry2);
            if (fi2)
                LLVMPositionBuilderBefore(tb2, fi2);
            else
                LLVMPositionBuilderAtEnd(tb2, entry2);
            LLVMValueRef fk_alloca = LLVMBuildAlloca(tb2, i32_t, "vrsz.fk");
            LLVMDisposeBuilder(tb2);

            /* k_fill = self.len (before grow) */
            LLVMValueRef sv4 = LLVMBuildLoad2(ctx->builder, vec_t, vec_alloca, "vrsz.sv4");
            LLVMValueRef len4 = LLVMBuildExtractValue(ctx->builder, sv4, 1, "vrsz.len4");
            LLVMBuildStore(ctx->builder, len4, fk_alloca);

            /* fill value: null/zero for all types; for string use empty literal */
            LLVMValueRef fill_val;
            if (elem_type->kind == TYPE_STRING)
                fill_val = ls_string_from_literal(ctx, "", "vrsz.empty");
            else
                fill_val = LLVMConstNull(elem_llvm);

            LLVMBasicBlockRef fcond_bb = LLVMAppendBasicBlockInContext(ctx->context, cur_fn, "vrsz.fcond");
            LLVMBasicBlockRef fbody_bb = LLVMAppendBasicBlockInContext(ctx->context, cur_fn, "vrsz.fbody");
            LLVMBasicBlockRef fend_bb = LLVMAppendBasicBlockInContext(ctx->context, cur_fn, "vrsz.fend");
            LLVMBuildBr(ctx->builder, fcond_bb);

            LLVMPositionBuilderAtEnd(ctx->builder, fcond_bb);
            LLVMValueRef fkv = LLVMBuildLoad2(ctx->builder, i32_t, fk_alloca, "vrsz.fkv");
            LLVMValueRef flt = LLVMBuildICmp(ctx->builder, LLVMIntSLT, fkv, n_clamped, "vrsz.flt");
            LLVMBuildCondBr(ctx->builder, flt, fbody_bb, fend_bb);

            LLVMPositionBuilderAtEnd(ctx->builder, fbody_bb);
            LLVMValueRef sv5 = LLVMBuildLoad2(ctx->builder, vec_t, vec_alloca, "vrsz.sv5");
            LLVMValueRef dat5 = LLVMBuildExtractValue(ctx->builder, sv5, 0, "vrsz.dat5");
            LLVMValueRef fk64 = LLVMBuildSExt(ctx->builder, fkv, i64_t, "vrsz.fk64");
            LLVMValueRef fep = LLVMBuildGEP2(ctx->builder, elem_llvm, dat5, &fk64, 1, "vrsz.fep");
            LLVMBuildStore(ctx->builder, fill_val, fep);
            LLVMValueRef fnk = LLVMBuildAdd(ctx->builder, fkv, one32, "vrsz.fnk");
            LLVMBuildStore(ctx->builder, fnk, fk_alloca);
            LLVMBuildBr(ctx->builder, fcond_bb);

            LLVMPositionBuilderAtEnd(ctx->builder, fend_bb);
            /* Update len = n_clamped */
            LLVMValueRef sv6 = LLVMBuildLoad2(ctx->builder, vec_t, vec_alloca, "vrsz.sv6");
            sv6 = LLVMBuildInsertValue(ctx->builder, sv6, n_clamped, 1, "vrsz.ul");
            LLVMBuildStore(ctx->builder, sv6, vec_alloca);
        }
        LLVMBuildBr(ctx->builder, end_bb);

        /* ----- shrink_bb: drop elements [n_clamped, len), update len ----- */
        LLVMPositionBuilderAtEnd(ctx->builder, shrink_bb);
        {
            bool elem_needs_drop = (elem_type &&
                                    (elem_type->kind == TYPE_STRING ||
                                     (elem_type->kind == TYPE_STRUCT &&
                                      elem_type->as.strukt.has_drop)));

            if (elem_needs_drop)
            {
                /* Drop loop: for k in n_clamped..len */
                LLVMBasicBlockRef entry3 = LLVMGetEntryBasicBlock(cur_fn);
                LLVMBuilderRef tb3 = LLVMCreateBuilderInContext(ctx->context);
                LLVMValueRef fi3 = LLVMGetFirstInstruction(entry3);
                if (fi3)
                    LLVMPositionBuilderBefore(tb3, fi3);
                else
                    LLVMPositionBuilderAtEnd(tb3, entry3);
                LLVMValueRef dk_alloca = LLVMBuildAlloca(tb3, i32_t, "vrsz.dk");
                LLVMDisposeBuilder(tb3);

                LLVMBuildStore(ctx->builder, n_clamped, dk_alloca);

                LLVMBasicBlockRef dcond_bb = LLVMAppendBasicBlockInContext(ctx->context, cur_fn, "vrsz.dcond");
                LLVMBasicBlockRef dbody_bb = LLVMAppendBasicBlockInContext(ctx->context, cur_fn, "vrsz.dbody");
                LLVMBasicBlockRef dend_bb = LLVMAppendBasicBlockInContext(ctx->context, cur_fn, "vrsz.dend");
                LLVMBuildBr(ctx->builder, dcond_bb);

                LLVMPositionBuilderAtEnd(ctx->builder, dcond_bb);
                LLVMValueRef dkv = LLVMBuildLoad2(ctx->builder, i32_t, dk_alloca, "vrsz.dkv");
                LLVMValueRef svd = LLVMBuildLoad2(ctx->builder, vec_t, vec_alloca, "vrsz.svd");
                LLVMValueRef lend = LLVMBuildExtractValue(ctx->builder, svd, 1, "vrsz.lend");
                LLVMValueRef dlt = LLVMBuildICmp(ctx->builder, LLVMIntSLT, dkv, lend, "vrsz.dlt");
                LLVMBuildCondBr(ctx->builder, dlt, dbody_bb, dend_bb);

                LLVMPositionBuilderAtEnd(ctx->builder, dbody_bb);
                LLVMValueRef svdb = LLVMBuildLoad2(ctx->builder, vec_t, vec_alloca, "vrsz.svdb");
                LLVMValueRef datd = LLVMBuildExtractValue(ctx->builder, svdb, 0, "vrsz.datd");
                LLVMValueRef dk64 = LLVMBuildSExt(ctx->builder, dkv, i64_t, "vrsz.dk64");
                LLVMValueRef dep = LLVMBuildGEP2(ctx->builder, elem_llvm, datd, &dk64, 1, "vrsz.dep");
                emit_vec_elem_drop_at(ctx, dep, elem_type, 0, "resize[i]");

                /* k++ — check terminator safety */
                if (!LLVMGetBasicBlockTerminator(LLVMGetInsertBlock(ctx->builder)))
                {
                    LLVMValueRef dkv2 = LLVMBuildLoad2(ctx->builder, i32_t, dk_alloca, "vrsz.dkv2");
                    LLVMValueRef dnk = LLVMBuildAdd(ctx->builder, dkv2, one32, "vrsz.dnk");
                    LLVMBuildStore(ctx->builder, dnk, dk_alloca);
                    LLVMBuildBr(ctx->builder, dcond_bb);
                }

                LLVMPositionBuilderAtEnd(ctx->builder, dend_bb);
            }

            /* Update len = n_clamped */
            LLVMValueRef svs = LLVMBuildLoad2(ctx->builder, vec_t, vec_alloca, "vrsz.svs");
            svs = LLVMBuildInsertValue(ctx->builder, svs, n_clamped, 1, "vrsz.uls");
            LLVMBuildStore(ctx->builder, svs, vec_alloca);
        }
        LLVMBuildBr(ctx->builder, end_bb);

        LLVMPositionBuilderAtEnd(ctx->builder, end_bb);
        return NULL;
    }

    /* ---- copy() -> vec(T)  — deep clone entire vec into a new independent vec ---- */
    if (strcmp(method, "copy") == 0)
    {
        LLVMValueRef cur_fn = LLVMGetBasicBlockParent(LLVMGetInsertBlock(ctx->builder));
        LLVMTypeRef ptr_t = LLVMPointerTypeInContext(ctx->context, 0);

        /* Alloca result vec in entry block (init to zeros = {null, 0, 0}) */
        LLVMBasicBlockRef entry = LLVMGetEntryBasicBlock(cur_fn);
        LLVMBuilderRef tb = LLVMCreateBuilderInContext(ctx->context);
        LLVMValueRef fi = LLVMGetFirstInstruction(entry);
        if (fi)
            LLVMPositionBuilderBefore(tb, fi);
        else
            LLVMPositionBuilderAtEnd(tb, entry);
        LLVMValueRef res_alloca = LLVMBuildAlloca(tb, vec_t, "vcpy.res");
        LLVMDisposeBuilder(tb);
        LLVMBuildStore(ctx->builder, LLVMConstNull(vec_t), res_alloca);

        /* Load self */
        LLVMValueRef sv = LLVMBuildLoad2(ctx->builder, vec_t, vec_alloca, "vcpy.sv");
        LLVMValueRef slen = LLVMBuildExtractValue(ctx->builder, sv, 1, "vcpy.slen");
        LLVMValueRef sdat = LLVMBuildExtractValue(ctx->builder, sv, 0, "vcpy.sdat");

        /* If self is empty, return empty vec */
        LLVMValueRef zero32 = LLVMConstInt(i32_t, 0, 0);
        LLVMValueRef is_empty = LLVMBuildICmp(ctx->builder, LLVMIntEQ, slen, zero32, "vcpy.emp");
        LLVMBasicBlockRef do_bb = LLVMAppendBasicBlockInContext(ctx->context, cur_fn, "vcpy.do");
        LLVMBasicBlockRef end_bb = LLVMAppendBasicBlockInContext(ctx->context, cur_fn, "vcpy.end");
        LLVMBuildCondBr(ctx->builder, is_empty, end_bb, do_bb);

        LLVMPositionBuilderAtEnd(ctx->builder, do_bb);

        /* Allocate new data: malloc(slen * elem_size) */
        LLVMValueRef slen64 = LLVMBuildSExt(ctx->builder, slen, i64_t, "vcpy.slen64");
        LLVMValueRef bytes = LLVMBuildMul(ctx->builder, slen64, LLVMSizeOf(elem_llvm), "vcpy.bytes");
        LLVMValueRef malloc_fn = LLVMGetNamedFunction(ctx->module, "malloc");
        LLVMTypeRef malloc_ft = LLVMGlobalGetValueType(malloc_fn);
        LLVMValueRef new_data = LLVMBuildCall2(ctx->builder, malloc_ft, malloc_fn,
                                               &bytes, 1, "vcpy.nd");

#if CG_DEBUG
        {
            LLVMValueRef dbg_args[1] = {slen};
            cg_emit_debug_printf(ctx, "[cg] vec.copy  len=%d\n", dbg_args, 1);
        }
#endif

        bool elem_needs_drop = (elem_type &&
                                (elem_type->kind == TYPE_STRING ||
                                 (elem_type->kind == TYPE_STRUCT &&
                                  elem_type->as.strukt.has_drop)));

        if (!elem_needs_drop)
        {
            /* Trivial: single memcpy */
            LLVMValueRef memcpy_fn = LLVMGetNamedFunction(ctx->module, "memcpy");
            LLVMTypeRef memcpy_ft = LLVMGlobalGetValueType(memcpy_fn);
            LLVMValueRef mc_args[3] = {new_data, sdat, bytes};
            LLVMBuildCall2(ctx->builder, memcpy_ft, memcpy_fn, mc_args, 3, "");
        }
        else
        {
            /* Non-trivial: clone loop */
            LLVMBasicBlockRef entry2 = LLVMGetEntryBasicBlock(cur_fn);
            LLVMBuilderRef tb2 = LLVMCreateBuilderInContext(ctx->context);
            LLVMValueRef fi2 = LLVMGetFirstInstruction(entry2);
            if (fi2)
                LLVMPositionBuilderBefore(tb2, fi2);
            else
                LLVMPositionBuilderAtEnd(tb2, entry2);
            LLVMValueRef ck_alloca = LLVMBuildAlloca(tb2, i32_t, "vcpy.ck");
            LLVMDisposeBuilder(tb2);

            LLVMBuildStore(ctx->builder, zero32, ck_alloca);

            LLVMBasicBlockRef lc = LLVMAppendBasicBlockInContext(ctx->context, cur_fn, "vcpy.lcond");
            LLVMBasicBlockRef lb = LLVMAppendBasicBlockInContext(ctx->context, cur_fn, "vcpy.lbody");
            LLVMBasicBlockRef le = LLVMAppendBasicBlockInContext(ctx->context, cur_fn, "vcpy.lend");
            LLVMBuildBr(ctx->builder, lc);

            LLVMPositionBuilderAtEnd(ctx->builder, lc);
            LLVMValueRef ckv = LLVMBuildLoad2(ctx->builder, i32_t, ck_alloca, "vcpy.ckv");
            LLVMValueRef clt = LLVMBuildICmp(ctx->builder, LLVMIntSLT, ckv, slen, "vcpy.clt");
            LLVMBuildCondBr(ctx->builder, clt, lb, le);

            LLVMPositionBuilderAtEnd(ctx->builder, lb);
            LLVMValueRef ck64 = LLVMBuildSExt(ctx->builder, ckv, i64_t, "vcpy.ck64");
            LLVMValueRef sep = LLVMBuildGEP2(ctx->builder, elem_llvm, sdat, &ck64, 1, "vcpy.sep");
            LLVMValueRef se = LLVMBuildLoad2(ctx->builder, elem_llvm, sep, "vcpy.se");

            LLVMValueRef cloned;
            if (elem_type->kind == TYPE_STRING)
                cloned = emit_string_clone_val(ctx, se);
            else
                cloned = emit_struct_clone_val(ctx, se, elem_llvm, elem_type);

            LLVMValueRef dep = LLVMBuildGEP2(ctx->builder, elem_llvm, new_data, &ck64, 1, "vcpy.dep");
            LLVMBuildStore(ctx->builder, cloned, dep);

            LLVMValueRef one32 = LLVMConstInt(i32_t, 1, 0);
            LLVMValueRef nck = LLVMBuildAdd(ctx->builder, ckv, one32, "vcpy.nck");
            LLVMBuildStore(ctx->builder, nck, ck_alloca);
            LLVMBuildBr(ctx->builder, lc);

            LLVMPositionBuilderAtEnd(ctx->builder, le);
        }

        /* Build result: {new_data, slen, slen}  (cap == len for exact fit) */
        LLVMValueRef res = LLVMGetUndef(vec_t);
        res = LLVMBuildInsertValue(ctx->builder, res, new_data, 0, "vcpy.r0");
        res = LLVMBuildInsertValue(ctx->builder, res, slen, 1, "vcpy.r1");
        res = LLVMBuildInsertValue(ctx->builder, res, slen, 2, "vcpy.r2");
        LLVMBuildStore(ctx->builder, res, res_alloca);
        LLVMBuildBr(ctx->builder, end_bb);

        /* end_bb: load and return result vec */
        LLVMPositionBuilderAtEnd(ctx->builder, end_bb);
        (void)ptr_t;
        return LLVMBuildLoad2(ctx->builder, vec_t, res_alloca, "vcpy.ret");
    }

    /* ---- sort() -> void  — in-place ascending sort via qsort + generated comparator ---- */
    /* ---- sort_by(cmp fn(T,T)->int) -> void  — same but user-supplied comparator ---- */
    if (strcmp(method, "sort") == 0 || strcmp(method, "sort_by") == 0)
    {
        bool is_sort_by = (strcmp(method, "sort_by") == 0);

        /* For sort(): generate a unique internal comparator function __ls_vcmp_N.
           The comparator receives two const void* pointers, casts them to elem*, and
           returns negative/zero/positive like strcmp.
           For sort_by(): caller supplies the function pointer directly. */
        LLVMValueRef cmp_fn_ptr = NULL;

        if (!is_sort_by)
        {
            /* Generate comparator: int __ls_vcmp_N(void* a, void* b) */
            int sort_id = g_block_counter++;
            char cmp_name[64];
            snprintf(cmp_name, sizeof(cmp_name), "__ls_vcmp_%d", sort_id);

            LLVMTypeRef ptr_t = LLVMPointerTypeInContext(ctx->context, 0);
            LLVMTypeRef cmp_ft = LLVMFunctionType(i32_t, (LLVMTypeRef[]){ptr_t, ptr_t}, 2, 0);
            LLVMValueRef cmp_fn = LLVMAddFunction(ctx->module, cmp_name, cmp_ft);
            LLVMSetFunctionCallConv(cmp_fn, LLVMCCallConv);

            /* Build comparator body */
            LLVMBasicBlockRef saved_bb = LLVMGetInsertBlock(ctx->builder);
            LLVMBasicBlockRef cmp_entry = LLVMAppendBasicBlockInContext(ctx->context, cmp_fn, "entry");
            LLVMPositionBuilderAtEnd(ctx->builder, cmp_entry);

            LLVMValueRef pa = LLVMGetParam(cmp_fn, 0);
            LLVMValueRef pb = LLVMGetParam(cmp_fn, 1);

            if (elem_type->kind == TYPE_STRING)
            {
                /* Load LsString structs, extract .data, call strcmp */
                LLVMValueRef sa = LLVMBuildLoad2(ctx->builder, elem_llvm, pa, "cmp.sa");
                LLVMValueRef sb = LLVMBuildLoad2(ctx->builder, elem_llvm, pb, "cmp.sb");
                LLVMValueRef da = LLVMBuildExtractValue(ctx->builder, sa, 0, "cmp.da");
                LLVMValueRef db = LLVMBuildExtractValue(ctx->builder, sb, 0, "cmp.db");
                LLVMValueRef strcmp_fn = LLVMGetNamedFunction(ctx->module, "strcmp");
                LLVMTypeRef strcmp_ft = LLVMGlobalGetValueType(strcmp_fn);
                LLVMValueRef sc_args[2] = {da, db};
                LLVMValueRef res = LLVMBuildCall2(ctx->builder, strcmp_ft, strcmp_fn,
                                                  sc_args, 2, "cmp.res");
                LLVMBuildRet(ctx->builder, res);
            }
            else if (elem_type->kind == TYPE_F32 || elem_type->kind == TYPE_F64)
            {
                /* float: return (int)(a > b) - (int)(a < b) */
                LLVMValueRef va = LLVMBuildLoad2(ctx->builder, elem_llvm, pa, "cmp.va");
                LLVMValueRef vb = LLVMBuildLoad2(ctx->builder, elem_llvm, pb, "cmp.vb");
                LLVMValueRef gt = LLVMBuildFCmp(ctx->builder, LLVMRealOGT, va, vb, "cmp.gt");
                LLVMValueRef lt = LLVMBuildFCmp(ctx->builder, LLVMRealOLT, va, vb, "cmp.lt");
                LLVMValueRef igt = LLVMBuildZExt(ctx->builder, gt, i32_t, "cmp.igt");
                LLVMValueRef ilt = LLVMBuildZExt(ctx->builder, lt, i32_t, "cmp.ilt");
                LLVMValueRef res = LLVMBuildSub(ctx->builder, igt, ilt, "cmp.res");
                LLVMBuildRet(ctx->builder, res);
            }
            else
            {
                /* Integer / bool: saturating signed subtraction pattern */
                LLVMValueRef va = LLVMBuildLoad2(ctx->builder, elem_llvm, pa, "cmp.va");
                LLVMValueRef vb = LLVMBuildLoad2(ctx->builder, elem_llvm, pb, "cmp.vb");
                /* Extend to i64 to avoid overflow on subtraction */
                LLVMTypeRef i64_ext = LLVMInt64TypeInContext(ctx->context);
                LLVMValueRef va64;
                LLVMValueRef vb64;
                bool is_unsigned = (elem_type->kind == TYPE_U8 || elem_type->kind == TYPE_U16 ||
                                    elem_type->kind == TYPE_U32 || elem_type->kind == TYPE_U64);
                if (is_unsigned)
                {
                    va64 = LLVMBuildZExt(ctx->builder, va, i64_ext, "cmp.va64");
                    vb64 = LLVMBuildZExt(ctx->builder, vb, i64_ext, "cmp.vb64");
                }
                else
                {
                    va64 = LLVMBuildSExt(ctx->builder, va, i64_ext, "cmp.va64");
                    vb64 = LLVMBuildSExt(ctx->builder, vb, i64_ext, "cmp.vb64");
                }
                LLVMValueRef diff = LLVMBuildSub(ctx->builder, va64, vb64, "cmp.diff");
                /* Truncate back to i32 via select: diff>0→1, diff<0→-1, 0→0 */
                LLVMValueRef zero64 = LLVMConstInt(i64_ext, 0, 0);
                LLVMValueRef one32 = LLVMConstInt(i32_t, 1, 0);
                LLVMValueRef neg132 = LLVMConstInt(i32_t, (unsigned long long)-1, 1);
                LLVMValueRef zero32_v = LLVMConstInt(i32_t, 0, 0);
                LLVMValueRef is_pos = LLVMBuildICmp(ctx->builder, LLVMIntSGT, diff, zero64, "cmp.pos");
                LLVMValueRef is_neg = LLVMBuildICmp(ctx->builder, LLVMIntSLT, diff, zero64, "cmp.neg");
                LLVMValueRef r1 = LLVMBuildSelect(ctx->builder, is_pos, one32, zero32_v, "cmp.r1");
                LLVMValueRef res = LLVMBuildSelect(ctx->builder, is_neg, neg132, r1, "cmp.res");
                LLVMBuildRet(ctx->builder, res);
            }

            /* Restore builder position */
            if (saved_bb)
                LLVMPositionBuilderAtEnd(ctx->builder, saved_bb);

            cmp_fn_ptr = cmp_fn;
        }
        else
        {
            /* sort_by: evaluate the comparator argument (function reference) */
            cmp_fn_ptr = codegen_expr(ctx, call_node->as.call.args[0]);
            if (!cmp_fn_ptr)
                return NULL;
        }

        /* Call qsort(data, len, sizeof(elem), cmp) */
        LLVMValueRef sv = LLVMBuildLoad2(ctx->builder, vec_t, vec_alloca, "vst.sv");
        LLVMValueRef len = LLVMBuildExtractValue(ctx->builder, sv, 1, "vst.len");
        LLVMValueRef dat = LLVMBuildExtractValue(ctx->builder, sv, 0, "vst.dat");

        LLVMValueRef len64 = LLVMBuildSExt(ctx->builder, len, i64_t, "vst.len64");
        LLVMValueRef esz = LLVMSizeOf(elem_llvm);
        LLVMValueRef qsort_fn = LLVMGetNamedFunction(ctx->module, "qsort");
        LLVMTypeRef qsort_ft = LLVMGlobalGetValueType(qsort_fn);
        LLVMValueRef qs_args[4] = {dat, len64, esz, cmp_fn_ptr};
        LLVMBuildCall2(ctx->builder, qsort_ft, qsort_fn, qs_args, 4, "");

#if CG_DEBUG
        {
            LLVMValueRef dbg_args[1] = {len};
            cg_emit_debug_printf(ctx, "[cg] vec.sort  len=%d\n", dbg_args, 1);
        }
#endif
        return NULL;
    }

    /* ---- slice(start, end) -> vec(T)  — deep-clone [start, end) into a new vec ---- */
    if (strcmp(method, "slice") == 0)
    {
        LLVMValueRef cur_fn = LLVMGetBasicBlockParent(LLVMGetInsertBlock(ctx->builder));
        LLVMValueRef zero32 = LLVMConstInt(i32_t, 0, 0);
        LLVMValueRef one32 = LLVMConstInt(i32_t, 1, 0);

        LLVMValueRef start_val = codegen_expr(ctx, call_node->as.call.args[0]);
        if (!start_val)
            return NULL;
        LLVMValueRef end_val = codegen_expr(ctx, call_node->as.call.args[1]);
        if (!end_val)
            return NULL;

        /* Load self */
        LLVMValueRef sv = LLVMBuildLoad2(ctx->builder, vec_t, vec_alloca, "vsl.sv");
        LLVMValueRef len = LLVMBuildExtractValue(ctx->builder, sv, 1, "vsl.len");
        LLVMValueRef dat = LLVMBuildExtractValue(ctx->builder, sv, 0, "vsl.dat");

        /* Clamp start/end to [0, len] */
        LLVMValueRef s_neg = LLVMBuildICmp(ctx->builder, LLVMIntSLT, start_val, zero32, "vsl.sneg");
        LLVMValueRef s_cl = LLVMBuildSelect(ctx->builder, s_neg, zero32, start_val, "vsl.scl");
        LLVMValueRef s_big = LLVMBuildICmp(ctx->builder, LLVMIntSGT, s_cl, len, "vsl.sbig");
        LLVMValueRef s = LLVMBuildSelect(ctx->builder, s_big, len, s_cl, "vsl.s");

        LLVMValueRef e_neg = LLVMBuildICmp(ctx->builder, LLVMIntSLT, end_val, zero32, "vsl.eneg");
        LLVMValueRef e_cl = LLVMBuildSelect(ctx->builder, e_neg, zero32, end_val, "vsl.ecl");
        LLVMValueRef e_big = LLVMBuildICmp(ctx->builder, LLVMIntSGT, e_cl, len, "vsl.ebig");
        LLVMValueRef e = LLVMBuildSelect(ctx->builder, e_big, len, e_cl, "vsl.e");

        /* count = max(0, e - s) */
        LLVMValueRef diff = LLVMBuildSub(ctx->builder, e, s, "vsl.diff");
        LLVMValueRef neg_diff = LLVMBuildICmp(ctx->builder, LLVMIntSLE, diff, zero32, "vsl.nd");
        LLVMValueRef count = LLVMBuildSelect(ctx->builder, neg_diff, zero32, diff, "vsl.cnt");

        /* Alloca result vec in entry block (init = zeros) */
        LLVMBasicBlockRef entry = LLVMGetEntryBasicBlock(cur_fn);
        LLVMBuilderRef tb = LLVMCreateBuilderInContext(ctx->context);
        LLVMValueRef fi = LLVMGetFirstInstruction(entry);
        if (fi)
            LLVMPositionBuilderBefore(tb, fi);
        else
            LLVMPositionBuilderAtEnd(tb, entry);
        LLVMValueRef res_alloca = LLVMBuildAlloca(tb, vec_t, "vsl.res");
        LLVMDisposeBuilder(tb);
        LLVMBuildStore(ctx->builder, LLVMConstNull(vec_t), res_alloca);

        /* Skip if count == 0 */
        LLVMValueRef is_empty_sl = LLVMBuildICmp(ctx->builder, LLVMIntEQ, count, zero32, "vsl.emp");
        LLVMBasicBlockRef do_bb = LLVMAppendBasicBlockInContext(ctx->context, cur_fn, "vsl.do");
        LLVMBasicBlockRef end_bb = LLVMAppendBasicBlockInContext(ctx->context, cur_fn, "vsl.end");
        LLVMBuildCondBr(ctx->builder, is_empty_sl, end_bb, do_bb);

        LLVMPositionBuilderAtEnd(ctx->builder, do_bb);

        /* Allocate new data: malloc(count * elem_size) */
        LLVMValueRef cnt64 = LLVMBuildSExt(ctx->builder, count, i64_t, "vsl.cnt64");
        LLVMValueRef bytes = LLVMBuildMul(ctx->builder, cnt64, LLVMSizeOf(elem_llvm), "vsl.bytes");
        LLVMValueRef malloc_fn = LLVMGetNamedFunction(ctx->module, "malloc");
        LLVMTypeRef malloc_ft = LLVMGlobalGetValueType(malloc_fn);
        LLVMValueRef new_data = LLVMBuildCall2(ctx->builder, malloc_ft, malloc_fn,
                                               &bytes, 1, "vsl.nd");

#if CG_DEBUG
        {
            LLVMValueRef dbg_args[3] = {s, e, count};
            cg_emit_debug_printf(ctx, "[cg] vec.slice  start=%d end=%d count=%d\n", dbg_args, 3);
        }
#endif

        bool elem_needs_drop = (elem_type &&
                                (elem_type->kind == TYPE_STRING ||
                                 (elem_type->kind == TYPE_STRUCT &&
                                  elem_type->as.strukt.has_drop)));

        if (!elem_needs_drop)
        {
            /* Trivial: memcpy(new_data, &src[s], count * elem_size) */
            LLVMValueRef s64 = LLVMBuildSExt(ctx->builder, s, i64_t, "vsl.s64");
            LLVMValueRef src_p = LLVMBuildGEP2(ctx->builder, elem_llvm, dat, &s64, 1, "vsl.srcp");
            LLVMValueRef memcpy_fn = LLVMGetNamedFunction(ctx->module, "memcpy");
            LLVMTypeRef memcpy_ft = LLVMGlobalGetValueType(memcpy_fn);
            LLVMValueRef mc[3] = {new_data, src_p, bytes};
            LLVMBuildCall2(ctx->builder, memcpy_ft, memcpy_fn, mc, 3, "");
        }
        else
        {
            /* Non-trivial: clone loop for k in 0..count */
            LLVMBasicBlockRef entry2 = LLVMGetEntryBasicBlock(cur_fn);
            LLVMBuilderRef tb2 = LLVMCreateBuilderInContext(ctx->context);
            LLVMValueRef fi2 = LLVMGetFirstInstruction(entry2);
            if (fi2)
                LLVMPositionBuilderBefore(tb2, fi2);
            else
                LLVMPositionBuilderAtEnd(tb2, entry2);
            LLVMValueRef sk_alloca = LLVMBuildAlloca(tb2, i32_t, "vsl.sk");
            LLVMDisposeBuilder(tb2);

            LLVMBuildStore(ctx->builder, zero32, sk_alloca);

            LLVMBasicBlockRef lc = LLVMAppendBasicBlockInContext(ctx->context, cur_fn, "vsl.lcond");
            LLVMBasicBlockRef lb = LLVMAppendBasicBlockInContext(ctx->context, cur_fn, "vsl.lbody");
            LLVMBasicBlockRef le = LLVMAppendBasicBlockInContext(ctx->context, cur_fn, "vsl.lend");
            LLVMBuildBr(ctx->builder, lc);

            LLVMPositionBuilderAtEnd(ctx->builder, lc);
            LLVMValueRef skv = LLVMBuildLoad2(ctx->builder, i32_t, sk_alloca, "vsl.skv");
            LLVMValueRef slt = LLVMBuildICmp(ctx->builder, LLVMIntSLT, skv, count, "vsl.slt");
            LLVMBuildCondBr(ctx->builder, slt, lb, le);

            LLVMPositionBuilderAtEnd(ctx->builder, lb);
            LLVMValueRef si = LLVMBuildAdd(ctx->builder, s, skv, "vsl.si");
            LLVMValueRef si64 = LLVMBuildSExt(ctx->builder, si, i64_t, "vsl.si64");
            LLVMValueRef sep = LLVMBuildGEP2(ctx->builder, elem_llvm, dat, &si64, 1, "vsl.sep");
            LLVMValueRef se = LLVMBuildLoad2(ctx->builder, elem_llvm, sep, "vsl.se");

            LLVMValueRef cloned;
            if (elem_type->kind == TYPE_STRING)
                cloned = emit_string_clone_val(ctx, se);
            else
                cloned = emit_struct_clone_val(ctx, se, elem_llvm, elem_type);

            LLVMValueRef skv64 = LLVMBuildSExt(ctx->builder, skv, i64_t, "vsl.skv64");
            LLVMValueRef dep = LLVMBuildGEP2(ctx->builder, elem_llvm, new_data, &skv64, 1, "vsl.dep");
            LLVMBuildStore(ctx->builder, cloned, dep);

            LLVMValueRef nsk = LLVMBuildAdd(ctx->builder, skv, one32, "vsl.nsk");
            LLVMBuildStore(ctx->builder, nsk, sk_alloca);
            LLVMBuildBr(ctx->builder, lc);

            LLVMPositionBuilderAtEnd(ctx->builder, le);
        }

        /* Build result: {new_data, count, count} */
        LLVMValueRef res = LLVMGetUndef(vec_t);
        res = LLVMBuildInsertValue(ctx->builder, res, new_data, 0, "vsl.r0");
        res = LLVMBuildInsertValue(ctx->builder, res, count, 1, "vsl.r1");
        res = LLVMBuildInsertValue(ctx->builder, res, count, 2, "vsl.r2");
        LLVMBuildStore(ctx->builder, res, res_alloca);
        LLVMBuildBr(ctx->builder, end_bb);

        LLVMPositionBuilderAtEnd(ctx->builder, end_bb);
        return LLVMBuildLoad2(ctx->builder, vec_t, res_alloca, "vsl.ret");
    }

    /* ---- shrink_to_fit() -> void  — realloc data to exact len; releases excess capacity ---- */
    if (strcmp(method, "shrink_to_fit") == 0)
    {
        LLVMValueRef cur_fn = LLVMGetBasicBlockParent(LLVMGetInsertBlock(ctx->builder));
        LLVMValueRef zero32 = LLVMConstInt(i32_t, 0, 0);

        LLVMValueRef sv = LLVMBuildLoad2(ctx->builder, vec_t, vec_alloca, "vstf.sv");
        LLVMValueRef len = LLVMBuildExtractValue(ctx->builder, sv, 1, "vstf.len");
        LLVMValueRef cap = LLVMBuildExtractValue(ctx->builder, sv, 2, "vstf.cap");
        LLVMValueRef dat = LLVMBuildExtractValue(ctx->builder, sv, 0, "vstf.dat");

        /* Only realloc if len < cap (and len > 0 to keep data non-null) */
        LLVMValueRef lt_cap = LLVMBuildICmp(ctx->builder, LLVMIntSLT, len, cap, "vstf.lt");
        LLVMBasicBlockRef do_bb = LLVMAppendBasicBlockInContext(ctx->context, cur_fn, "vstf.do");
        LLVMBasicBlockRef end_bb = LLVMAppendBasicBlockInContext(ctx->context, cur_fn, "vstf.end");
        LLVMBuildCondBr(ctx->builder, lt_cap, do_bb, end_bb);

        LLVMPositionBuilderAtEnd(ctx->builder, do_bb);

        /* If len == 0: free data, set data = null, cap = 0 */
        LLVMValueRef is_zero = LLVMBuildICmp(ctx->builder, LLVMIntEQ, len, zero32, "vstf.iz");
        LLVMBasicBlockRef free_bb = LLVMAppendBasicBlockInContext(ctx->context, cur_fn, "vstf.free");
        LLVMBasicBlockRef realloc_bb = LLVMAppendBasicBlockInContext(ctx->context, cur_fn, "vstf.ra");
        LLVMBuildCondBr(ctx->builder, is_zero, free_bb, realloc_bb);

        /* free_bb: free(data) + {null, 0, 0} */
        LLVMPositionBuilderAtEnd(ctx->builder, free_bb);
        {
            cg_emit_free(ctx, dat, "vec.scope_drop", CG_LINE(ctx), CG_COL(ctx));
            LLVMValueRef sv2 = LLVMBuildLoad2(ctx->builder, vec_t, vec_alloca, "vstf.sv2");
            LLVMTypeRef ptr_t = LLVMPointerTypeInContext(ctx->context, 0);
            sv2 = LLVMBuildInsertValue(ctx->builder, sv2, LLVMConstNull(ptr_t), 0, "vstf.np");
            sv2 = LLVMBuildInsertValue(ctx->builder, sv2, zero32, 2, "vstf.zc");
            LLVMBuildStore(ctx->builder, sv2, vec_alloca);
        }
        LLVMBuildBr(ctx->builder, end_bb);

        /* realloc_bb: realloc(data, len * elem_size) + update cap = len */
        LLVMPositionBuilderAtEnd(ctx->builder, realloc_bb);
        {
            LLVMValueRef len64 = LLVMBuildSExt(ctx->builder, len, i64_t, "vstf.l64");
            LLVMValueRef bytes = LLVMBuildMul(ctx->builder, len64, LLVMSizeOf(elem_llvm), "vstf.bytes");
            LLVMValueRef realloc_fn = LLVMGetNamedFunction(ctx->module, "realloc");
            LLVMTypeRef realloc_ft = LLVMGlobalGetValueType(realloc_fn);
            LLVMValueRef ra_args[2] = {dat, bytes};
            LLVMValueRef new_data = LLVMBuildCall2(ctx->builder, realloc_ft, realloc_fn,
                                                   ra_args, 2, "vstf.nd");
            LLVMValueRef sv3 = LLVMBuildLoad2(ctx->builder, vec_t, vec_alloca, "vstf.sv3");
            sv3 = LLVMBuildInsertValue(ctx->builder, sv3, new_data, 0, "vstf.ud");
            sv3 = LLVMBuildInsertValue(ctx->builder, sv3, len, 2, "vstf.uc");
            LLVMBuildStore(ctx->builder, sv3, vec_alloca);
#if CG_DEBUG
            {
                LLVMValueRef dbg_args[2] = {cap, len};
                cg_emit_debug_printf(ctx, "[cg] vec.shrink_to_fit  old_cap=%d new_cap=%d\n",
                                     dbg_args, 2);
            }
#endif
        }
        LLVMBuildBr(ctx->builder, end_bb);

        LLVMPositionBuilderAtEnd(ctx->builder, end_bb);
        return NULL;
    }

    cg_error(ctx, call_node->line, call_node->column,
             "unknown vec method '%s'", method);
    return NULL;
}

/* ---- Map method dispatch ---- */

static LLVMValueRef codegen_map_method(CodegenContext *ctx, AstNode *call_node, Type *map_type)
{
    const char *method = call_node->as.call.callee->as.field_access.field;
    AstNode *obj_node = call_node->as.call.callee->as.field_access.object;
    Type *key_type = map_type->as.map.key;
    Type *val_type = map_type->as.map.val;

    /* Ensure all map helper functions are emitted */
    emit_map_helpers_for(ctx, key_type, val_type);

    char suffix[64];
    map_type_id(key_type, val_type, suffix, sizeof(suffix));

    /* Resolve alloca for the map object */
    LLVMValueRef map_alloca = NULL;
    if (obj_node->kind == AST_IDENT)
    {
        CgSymbol *sym = cg_scope_resolve(ctx->current_scope, obj_node->as.ident.name);
        if (sym)
            map_alloca = sym->value;
    }
    if (map_alloca == NULL)
    {
        cg_error(ctx, call_node->line, call_node->column,
                 "map method call: cannot get address of map object");
        return NULL;
    }

    LLVMTypeRef val_lt = type_to_llvm(ctx, val_type);
    LLVMTypeRef key_lt = type_to_llvm(ctx, key_type);

    /* ---- set(key, val) ---- */
    if (strcmp(method, "set") == 0)
    {
        char nm[96];
        snprintf(nm, sizeof(nm), "__ls_map_%s_set", suffix);
        LLVMValueRef fn = LLVMGetNamedFunction(ctx->module, nm);
        LLVMTypeRef ft = LLVMGlobalGetValueType(fn);
        LLVMValueRef kv = codegen_expr(ctx, call_node->as.call.args[0]);
        LLVMValueRef vv = codegen_expr(ctx, call_node->as.call.args[1]);
        if (!kv || !vv)
            return NULL;
        LLVMValueRef args[] = {map_alloca, kv, vv};
        LLVMBuildCall2(ctx->builder, ft, fn, args, 3, "");
        return LLVMGetUndef(LLVMVoidTypeInContext(ctx->context));
    }
    /* ---- get(key) ---- */
    if (strcmp(method, "get") == 0)
    {
        char nm[96];
        snprintf(nm, sizeof(nm), "__ls_map_%s_get", suffix);
        LLVMValueRef fn = LLVMGetNamedFunction(ctx->module, nm);
        LLVMTypeRef ft = LLVMGlobalGetValueType(fn);
        LLVMValueRef kv = codegen_expr(ctx, call_node->as.call.args[0]);
        if (!kv)
            return NULL;
        LLVMValueRef args[] = {map_alloca, kv};
        return LLVMBuildCall2(ctx->builder, ft, fn, args, 2, "mg");
    }
    /* ---- contains_key(key) ---- */
    if (strcmp(method, "contains_key") == 0)
    {
        char nm[96];
        snprintf(nm, sizeof(nm), "__ls_map_%s_contains", suffix);
        LLVMValueRef fn = LLVMGetNamedFunction(ctx->module, nm);
        LLVMTypeRef ft = LLVMGlobalGetValueType(fn);
        LLVMValueRef kv = codegen_expr(ctx, call_node->as.call.args[0]);
        if (!kv)
            return NULL;
        LLVMValueRef args[] = {map_alloca, kv};
        return LLVMBuildCall2(ctx->builder, ft, fn, args, 2, "mck");
    }
    /* ---- remove(key) ---- */
    if (strcmp(method, "remove") == 0)
    {
        char nm[96];
        snprintf(nm, sizeof(nm), "__ls_map_%s_remove", suffix);
        LLVMValueRef fn = LLVMGetNamedFunction(ctx->module, nm);
        LLVMTypeRef ft = LLVMGlobalGetValueType(fn);
        LLVMValueRef kv = codegen_expr(ctx, call_node->as.call.args[0]);
        if (!kv)
            return NULL;
        LLVMValueRef args[] = {map_alloca, kv};
        LLVMBuildCall2(ctx->builder, ft, fn, args, 2, "");
        return LLVMGetUndef(LLVMVoidTypeInContext(ctx->context));
    }
    /* ---- clear() ---- */
    if (strcmp(method, "clear") == 0)
    {
        char nm[96];
        snprintf(nm, sizeof(nm), "__ls_map_%s_clear", suffix);
        LLVMValueRef fn = LLVMGetNamedFunction(ctx->module, nm);
        LLVMTypeRef ft = LLVMGlobalGetValueType(fn);
        LLVMBuildCall2(ctx->builder, ft, fn, &map_alloca, 1, "");
        return LLVMGetUndef(LLVMVoidTypeInContext(ctx->context));
    }
    /* ---- is_empty() ---- */
    if (strcmp(method, "is_empty") == 0)
    {
        LLVMTypeRef map_t = ls_map_type(ctx);
        LLVMTypeRef i32_t = LLVMInt32TypeInContext(ctx->context);
        LLVMValueRef mv = LLVMBuildLoad2(ctx->builder, map_t, map_alloca, "mv");
        LLVMValueRef lv = LLVMBuildExtractValue(ctx->builder, mv, 1, "len");
        return LLVMBuildICmp(ctx->builder, LLVMIntEQ, lv, LLVMConstInt(i32_t, 0, 0), "ie");
    }
    /* ---- length (field access treated as call in some code paths) ---- */
    if (strcmp(method, "length") == 0)
    {
        LLVMTypeRef map_t = ls_map_type(ctx);
        LLVMValueRef mv = LLVMBuildLoad2(ctx->builder, map_t, map_alloca, "mv");
        return LLVMBuildExtractValue(ctx->builder, mv, 1, "len");
    }
    (void)val_lt;
    (void)key_lt;

    cg_error(ctx, call_node->line, call_node->column,
             "unknown map method '%s'", method);
    return NULL;
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
        if (!sym)
            return NULL;
        Type *rtype = node->resolved_type;
        /* *Struct variable: alloca holds a pointer value; load it to get the heap address */
        if (rtype && rtype->kind == TYPE_POINTER &&
            rtype->as.pointer_to && rtype->as.pointer_to->kind == TYPE_STRUCT)
        {
            LLVMTypeRef ptr_llvm = LLVMPointerTypeInContext(ctx->context, 0);
            return LLVMBuildLoad2(ctx->builder, ptr_llvm, sym->value, "self.deref");
        }
        /* Stack struct: alloca IS the struct storage */
        return sym->value;
    }

    if (node->kind == AST_INDEX)
    {
        /* arr[index] — get pointer to array element */
        AstNode *arr_obj = node->as.index_expr.object;
        Type *arr_type = arr_obj->resolved_type;
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
                struct_ptr = LLVMBuildAlloca(ctx->builder, st_llvm, "tmp.struct");
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

    return NULL; /* Other lvalue forms not yet handled */
}

/* ---- Expression codegen ---- */

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
        return LLVMConstInt(LLVMInt32TypeInContext(ctx->context),
                            (unsigned long long)node->as.int_lit.value, 1);

    case AST_FLOAT_LIT:
        return LLVMConstReal(LLVMDoubleTypeInContext(ctx->context),
                             node->as.float_lit.value);

    case AST_BOOL_LIT:
        return LLVMConstInt(LLVMInt1TypeInContext(ctx->context),
                            node->as.bool_lit.value ? 1 : 0, 0);

    case AST_STRING_LIT:
        return ls_string_from_literal(ctx, node->as.string_lit.value, "str");

    case AST_NIL_LIT:
        return LLVMConstNull(LLVMPointerTypeInContext(ctx->context, 0));

    case AST_IDENT:
    {
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
        if (op == NULL || op->kind != AST_IDENT)
        {
            cg_error(ctx, node->line, node->column,
                     "&! operand must be an identifier");
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

        case TOKEN_AMP:
        {
            /* &x — get the alloca address of x */
            if (node->as.unary.operand->kind == AST_IDENT)
            {
                CgSymbol *sym = cg_scope_resolve(ctx->current_scope,
                                                 node->as.unary.operand->as.ident.name);
                if (sym)
                    return sym->value; /* alloca IS the address */
            }
            cg_error(ctx, node->line, node->column, "cannot take address of expression");
            return NULL;
        }

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
        /* Short-circuit for logical && and || */
        if (node->as.binary.op == TOKEN_AND || node->as.binary.op == TOKEN_OR)
        {
            return codegen_short_circuit(ctx, node);
        }

        /* For string operands that come from vec[i], borrow instead of deep-clone —
           all string binary ops (==, !=, +) only read .data/.len from the operands. */
        LLVMValueRef left = codegen_expr_or_borrow(ctx, node->as.binary.left);
        LLVMValueRef right = codegen_expr_or_borrow(ctx, node->as.binary.right);
        if (left == NULL || right == NULL)
            return NULL;

        Type *lt = node->as.binary.left->resolved_type;
        Type *rt = node->as.binary.right->resolved_type;

        /* Implicit numeric widening: if the operands have different numeric
           types but the checker accepted them, the result type is the common
           wider type. Promote each operand to that common type so the
           subsequent op (add/sub/cmp/...) sees uniform LLVM types. String
           concat (TYPE_STRING) is handled separately below and skips this. */
        Type *common = NULL;
        if (lt && rt && lt->kind != TYPE_STRING &&
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

        bool is_fp = lt && type_is_float(lt);
        bool is_signed_int = lt && type_is_signed(lt);

        switch (node->as.binary.op)
        {
        case TOKEN_PLUS:
            /* String concatenation: malloc(len1+len2+1), memcpy, memcpy, null-terminate */
            if (lt && lt->kind == TYPE_STRING)
            {
                LLVMTypeRef i32_t = LLVMInt32TypeInContext(ctx->context);
                LLVMTypeRef i64_t = LLVMInt64TypeInContext(ctx->context);
                LLVMTypeRef ptr_t = LLVMPointerTypeInContext(ctx->context, 0);

                /* Extract .data and .len from both operands */
                LLVMValueRef l_data = ls_string_data(ctx, left);
                LLVMValueRef l_len = ls_string_len(ctx, left);
                LLVMValueRef r_data = ls_string_data(ctx, right);
                LLVMValueRef r_len = ls_string_len(ctx, right);

                /* total_len = l_len + r_len */
                LLVMValueRef total_len = LLVMBuildAdd(ctx->builder, l_len, r_len, "cat.tlen");
                /* alloc_size = total_len + 1 (for null terminator) */
                LLVMValueRef one = LLVMConstInt(i32_t, 1, 0);
                LLVMValueRef alloc_need = LLVMBuildAdd(ctx->builder, total_len, one, "cat.need");

                /* Compute cap: max(LS_MIN_STR_CAP, alloc_need) at runtime.
                   For simplicity, use a select: if alloc_need > MIN_CAP then alloc_need else MIN_CAP */
                LLVMValueRef min_cap = LLVMConstInt(i32_t, LS_MIN_STR_CAP, 0);
                LLVMValueRef need_gt = LLVMBuildICmp(ctx->builder, LLVMIntUGT,
                                                     alloc_need, min_cap, "cat.gt");
                LLVMValueRef cap = LLVMBuildSelect(ctx->builder, need_gt,
                                                   alloc_need, min_cap, "cat.cap");

                /* malloc(cap) — extend to i64 for malloc arg */
                LLVMValueRef cap64 = LLVMBuildZExt(ctx->builder, cap, i64_t, "cat.cap64");
                LLVMValueRef buf = cg_emit_alloc(ctx, cap64, "string.concat",
                                                 node->line, node->column);

                /* memcpy(buf, l_data, l_len) */
                LLVMValueRef memcpy_fn = LLVMGetNamedFunction(ctx->module, "memcpy");
                if (memcpy_fn == NULL)
                {
                    LLVMTypeRef mc_params[] = {ptr_t, ptr_t, i64_t};
                    LLVMTypeRef mc_type = LLVMFunctionType(ptr_t, mc_params, 3, 0);
                    memcpy_fn = LLVMAddFunction(ctx->module, "memcpy", mc_type);
                }
                LLVMTypeRef mc_type = LLVMGlobalGetValueType(memcpy_fn);
                LLVMValueRef l_len64 = LLVMBuildZExt(ctx->builder, l_len, i64_t, "l.len64");
                LLVMValueRef mc_args1[] = {buf, l_data, l_len64};
                LLVMBuildCall2(ctx->builder, mc_type, memcpy_fn, mc_args1, 3, "");

                /* memcpy(buf + l_len, r_data, r_len + 1) — includes null terminator */
                LLVMValueRef buf_mid = LLVMBuildGEP2(ctx->builder,
                                                     LLVMInt8TypeInContext(ctx->context), buf, &l_len, 1, "cat.mid");
                LLVMValueRef r_copy_len = LLVMBuildAdd(ctx->builder, r_len, one, "r.copylen");
                LLVMValueRef r_copy64 = LLVMBuildZExt(ctx->builder, r_copy_len, i64_t, "r.copy64");
                LLVMValueRef mc_args2[] = {buf_mid, r_data, r_copy64};
                LLVMBuildCall2(ctx->builder, mc_type, memcpy_fn, mc_args2, 3, "");

                /* Build LsString { buf, total_len, cap } */
                return cg_push_temp_string(ctx, ls_string_make(ctx, buf, total_len, cap));
            }
            if (is_fp)
                return LLVMBuildFAdd(ctx->builder, left, right, "fadd");
            return LLVMBuildAdd(ctx->builder, left, right, "add");
        case TOKEN_MINUS:
            if (is_fp)
                return LLVMBuildFSub(ctx->builder, left, right, "fsub");
            return LLVMBuildSub(ctx->builder, left, right, "sub");
        case TOKEN_STAR:
            if (is_fp)
                return LLVMBuildFMul(ctx->builder, left, right, "fmul");
            return LLVMBuildMul(ctx->builder, left, right, "mul");
        case TOKEN_SLASH:
            if (is_fp)
                return LLVMBuildFDiv(ctx->builder, left, right, "fdiv");
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
            if (lt && lt->kind == TYPE_STRING)
            {
                /* String value comparison via strcmp */
                LLVMValueRef strcmp_fn = LLVMGetNamedFunction(ctx->module, "strcmp");
                if (strcmp_fn == NULL)
                {
                    LLVMTypeRef ptr_t = LLVMPointerTypeInContext(ctx->context, 0);
                    LLVMTypeRef sc_params[] = {ptr_t, ptr_t};
                    LLVMTypeRef sc_type = LLVMFunctionType(
                        LLVMInt32TypeInContext(ctx->context), sc_params, 2, 0);
                    strcmp_fn = LLVMAddFunction(ctx->module, "strcmp", sc_type);
                }
                LLVMTypeRef sc_type = LLVMGlobalGetValueType(strcmp_fn);
                LLVMValueRef l_data = ls_string_data(ctx, left);
                LLVMValueRef r_data = ls_string_data(ctx, right);
                LLVMValueRef cmp_args[] = {l_data, r_data};
                LLVMValueRef cmp = LLVMBuildCall2(ctx->builder, sc_type, strcmp_fn,
                                                  cmp_args, 2, "strcmp");
                return LLVMBuildICmp(ctx->builder, LLVMIntEQ, cmp,
                                     LLVMConstInt(LLVMInt32TypeInContext(ctx->context), 0, 0), "streq");
            }
            if (is_fp)
                return LLVMBuildFCmp(ctx->builder, LLVMRealOEQ, left, right, "feq");
            return LLVMBuildICmp(ctx->builder, LLVMIntEQ, left, right, "eq");
        case TOKEN_NEQ:
            if (lt && lt->kind == TYPE_STRING)
            {
                LLVMValueRef strcmp_fn = LLVMGetNamedFunction(ctx->module, "strcmp");
                if (strcmp_fn == NULL)
                {
                    LLVMTypeRef ptr_t = LLVMPointerTypeInContext(ctx->context, 0);
                    LLVMTypeRef sc_params[] = {ptr_t, ptr_t};
                    LLVMTypeRef sc_type = LLVMFunctionType(
                        LLVMInt32TypeInContext(ctx->context), sc_params, 2, 0);
                    strcmp_fn = LLVMAddFunction(ctx->module, "strcmp", sc_type);
                }
                LLVMTypeRef sc_type = LLVMGlobalGetValueType(strcmp_fn);
                LLVMValueRef l_data = ls_string_data(ctx, left);
                LLVMValueRef r_data = ls_string_data(ctx, right);
                LLVMValueRef cmp_args[] = {l_data, r_data};
                LLVMValueRef cmp = LLVMBuildCall2(ctx->builder, sc_type, strcmp_fn,
                                                  cmp_args, 2, "strcmp");
                return LLVMBuildICmp(ctx->builder, LLVMIntNE, cmp,
                                     LLVMConstInt(LLVMInt32TypeInContext(ctx->context), 0, 0), "strne");
            }
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

    case AST_CALL:
    {
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
            strcmp(node->as.call.callee->as.ident.name, "__move") == 0 &&
            node->as.call.arg_count == 1)
        {
            return codegen_expr(ctx, node->as.call.args[0]);
        }

        /* Intercept print() calls — generate inline printf with type-aware format */
        if (node->as.call.callee->kind == AST_IDENT && strcmp(node->as.call.callee->as.ident.name, "print") == 0)
        {
            return codegen_print_call(ctx, node);
        }

        /* Intercept to_string() calls — convert numeric/bool to string */
        if (node->as.call.callee->kind == AST_IDENT && strcmp(node->as.call.callee->as.ident.name, "to_string") == 0)
        {
            return codegen_to_string(ctx, node);
        }

        /* Intercept from_int() calls — parse string as int */
        if (node->as.call.callee->kind == AST_IDENT && strcmp(node->as.call.callee->as.ident.name, "from_int") == 0)
        {
            return codegen_from_int(ctx, node);
        }

        /* Intercept from_float() calls — parse string as float */
        if (node->as.call.callee->kind == AST_IDENT && strcmp(node->as.call.callee->as.ident.name, "from_float") == 0)
        {
            return codegen_from_float(ctx, node);
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

        /* Intercept string builtin method calls: s.method(args...) */
        if (node->as.call.callee->kind == AST_FIELD)
        {
            AstNode *obj_node = node->as.call.callee->as.field_access.object;
            if (obj_node->resolved_type && obj_node->resolved_type->kind == TYPE_STRING)
            {
                return codegen_string_method(ctx, node);
            }
        }

        /* Intercept vec builtin method calls: v.push(x), v.pop(), v.clear(), v.reserve(n)
           Also handle *vec(T) parameter with auto-deref. */
        if (node->as.call.callee->kind == AST_FIELD)
        {
            AstNode *obj_node = node->as.call.callee->as.field_access.object;
            Type *obj_rt = obj_node->resolved_type;
            /* auto-deref *vec(T) → vec(T) */
            if (obj_rt && obj_rt->kind == TYPE_POINTER && obj_rt->as.pointer_to &&
                obj_rt->as.pointer_to->kind == TYPE_VECTOR)
                obj_rt = obj_rt->as.pointer_to;
            if (obj_rt && obj_rt->kind == TYPE_VECTOR)
            {
                return codegen_vec_method(ctx, node, obj_rt);
            }
            if (obj_rt && obj_rt->kind == TYPE_MAP)
            {
                return codegen_map_method(ctx, node, obj_rt);
            }
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
                Type *deref = obj_type;
                if (deref->kind == TYPE_POINTER && deref->as.pointer_to &&
                    deref->as.pointer_to->kind == TYPE_STRUCT)
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
                Type *deref = obj_type;
                if (deref->kind == TYPE_POINTER && deref->as.pointer_to &&
                    deref->as.pointer_to->kind == TYPE_STRUCT)
                {
                    deref = deref->as.pointer_to;
                }
                if (deref->kind == TYPE_STRUCT)
                {
                    is_struct_method = true;
                    struct_name = deref->as.strukt.name;
                }
            }
            /* Also detect static call via type name (obj_type may be NULL for bare struct name) */
            if (!is_struct_method && obj_node->kind == AST_IDENT && !obj_type)
            {
                is_struct_method = true;
                struct_name = obj_node->as.ident.name;
            }

            if (is_struct_method)
            {
                const char *method_name = node->as.call.callee->as.field_access.field;
                /* Build qualified name: StructName.method_name */
                static char qualified_name[256];
                snprintf(qualified_name, sizeof(qualified_name), "%s.%s",
                         struct_name ? struct_name : "", method_name);
                fn_name = qualified_name;
                callee = LLVMGetNamedFunction(ctx->module, fn_name);
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
                callee = LLVMGetNamedFunction(ctx->module, fn_name);
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
                    strcmp(mod_t->as.module.name, "math") == 0)
                {
                    return builtin_math_emit_call(ctx, fn_name,
                                                  node->as.call.args,
                                                  node->as.call.arg_count);
                }
                /* Phase E.4: io has been migrated to pure-LS stdlib/io.ls.
                   `import io` now goes through the normal user-module path
                   (is_builtin == false). No special dispatch here. */

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
                sret_slot = LLVMBuildAlloca(ctx->builder, st_lt, "sret.slot");
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
                LLVMValueRef arg_val = codegen_expr(ctx, node->as.call.args[i]);
                if (arg_val == NULL)
                {
                    free(args);
                    return NULL;
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
                            else
                            {
                                LLVMTypeRef llvm_st = type_to_llvm(ctx, arg_type);
                                arg_val = emit_struct_clone_val(ctx, arg_val, llvm_st, arg_type);
                            }
                        }
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
                    if (!(arg_type && (arg_type->kind == TYPE_VECTOR ||
                                       arg_type->kind == TYPE_MAP ||
                                       arg_type->kind == TYPE_STRUCT))) continue;
                    if ((unsigned)i >= pc) continue;
                    LLVMTypeRef param_llvm = LLVMTypeOf(LLVMGetParam(callee, (unsigned)i));
                    if (LLVMGetTypeKind(param_llvm) != LLVMPointerTypeKind) continue;
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
                    if (arg_type->kind == TYPE_VECTOR) {
                        store_t = ls_vec_type(ctx);
                        tmp_name = "vec.borrow.tmp";
                    } else if (arg_type->kind == TYPE_MAP) {
                        store_t = ls_map_type(ctx);
                        tmp_name = "map.borrow.tmp";
                    } else {
                        store_t = type_to_llvm(ctx, arg_type);
                        tmp_name = "struct.borrow.tmp";
                    }
                    LLVMValueRef tmp = LLVMBuildAlloca(ctx->builder, store_t, tmp_name);
                    LLVMBuildStore(ctx->builder, args[i], tmp);
                    args[i] = tmp;
                }
            }

            /* String arg fixup: borrow (zero cap) for LS functions, extract .data for C ABI. */
            unsigned param_count = LLVMCountParams(callee);
            for (int i = arg_offset; i < total_argc; i++)
            {
                int user_i = i - arg_offset;
                Type *arg_type = node->as.call.args[user_i]->resolved_type;
                if (arg_type && arg_type->kind == TYPE_STRING)
                {
                    if ((unsigned)i < param_count)
                    {
                        LLVMValueRef param = LLVMGetParam(callee, (unsigned)i);
                        LLVMTypeRef param_type = LLVMTypeOf(param);
                        if (LLVMGetTypeKind(param_type) == LLVMPointerTypeKind)
                        {
                            /* C ABI: pass raw data pointer */
                            args[i] = ls_string_data(ctx, args[i]);
                        }
                        else
                        {
                            /* LS ABI: zero out cap so callee won't free the data
                               (borrow semantics — caller retains ownership) */
                            LLVMValueRef zero32 = LLVMConstInt(
                                LLVMInt32TypeInContext(ctx->context), 0, 0);
                            args[i] = LLVMBuildInsertValue(
                                ctx->builder, args[i], zero32, 2, "arg.borrow");
                        }
                    }
                    else if (LLVMIsFunctionVarArg(fn_type))
                    {
                        /* Variadic C ABI: pass raw data pointer */
                        args[i] = ls_string_data(ctx, args[i]);
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
                    LLVMValueRef tmp = LLVMBuildAlloca(ctx->builder, st_lt,
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
                LLVMValueRef slot = LLVMBuildAlloca(ctx->builder, st_lt,
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
                strcmp(obj_type->as.module.name, "math") == 0)
            {
                LLVMValueRef cst = builtin_math_emit_const(ctx, name);
                if (cst) return cst;
                cg_error(ctx, node->line, node->column,
                         "math.%s is not a constant; use it in a call expression",
                         name);
                return NULL;
            }

            /* Try function first */
            LLVMValueRef fn = LLVMGetNamedFunction(ctx->module, name);
            if (fn)
                return fn;
            /* Try global variable (module-exported variable) */
            LLVMValueRef gv = LLVMGetNamedGlobal(ctx->module, name);
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

        /* Vec .length / .capacity — extract fields 1 and 2 from LsVec struct.
           Also handles *vec(T) with auto-deref. */
        {
            Type *vt_check = obj_type;
            bool is_ptr_vec = false;
            if (vt_check && vt_check->kind == TYPE_POINTER && vt_check->as.pointer_to &&
                vt_check->as.pointer_to->kind == TYPE_VECTOR)
            {
                vt_check = vt_check->as.pointer_to;
                is_ptr_vec = true;
            }
            if (vt_check && vt_check->kind == TYPE_VECTOR)
            {
                const char *field = node->as.field_access.field;
                if (strcmp(field, "length") == 0 || strcmp(field, "capacity") == 0)
                {
                    LLVMTypeRef vec_t = ls_vec_type(ctx);
                    LLVMValueRef vec_val = NULL;
                    if (obj_node->kind == AST_IDENT)
                    {
                        CgSymbol *sym = cg_scope_resolve(ctx->current_scope,
                                                         obj_node->as.ident.name);
                        if (sym)
                        {
                            if (is_ptr_vec)
                            {
                                /* *vec(T): load the pointer, then load the LsVec through it */
                                LLVMTypeRef ptr_t = LLVMPointerTypeInContext(ctx->context, 0);
                                LLVMValueRef ptr = LLVMBuildLoad2(ctx->builder, ptr_t,
                                                                  sym->value, "vpf.ptr");
                                vec_val = LLVMBuildLoad2(ctx->builder, vec_t, ptr, "vpf.v");
                            }
                            else
                            {
                                vec_val = LLVMBuildLoad2(ctx->builder, vec_t, sym->value, "vf.v");
                            }
                        }
                    }
                    if (vec_val == NULL)
                        vec_val = codegen_expr(ctx, obj_node);
                    if (vec_val == NULL)
                        return NULL;
                    unsigned idx = (strcmp(field, "length") == 0) ? 1 : 2;
                    return LLVMBuildExtractValue(ctx->builder, vec_val, idx,
                                                 strcmp(field, "length") == 0 ? "v.len" : "v.cap");
                }
                cg_error(ctx, node->line, node->column,
                         "vec has no field '%s'", node->as.field_access.field);
                return NULL;
            }
        }

        /* Map .length — extract len field (index 1) from LsMap struct */
        if (obj_type && obj_type->kind == TYPE_MAP)
        {
            if (strcmp(node->as.field_access.field, "length") == 0)
            {
                (void)ls_map_type(ctx); /* ensure type is registered */
                LLVMValueRef mv = codegen_expr(ctx, obj_node);
                if (!mv)
                    return NULL;
                return LLVMBuildExtractValue(ctx->builder, mv, 1, "m.len");
            }
            cg_error(ctx, node->line, node->column,
                     "map has no field '%s'", node->as.field_access.field);
            return NULL;
        }

        /* String .length — extract .len field from LsString struct */
        if (obj_type && obj_type->kind == TYPE_STRING)
        {
            if (strcmp(node->as.field_access.field, "length") == 0)
            {
                /* .length only reads the len field — borrow instead of clone for vec[i] */
                LLVMValueRef str_val = codegen_expr_or_borrow(ctx, obj_node);
                if (str_val == NULL)
                    return NULL;
                return ls_string_len(ctx, str_val);
            }
            cg_error(ctx, node->line, node->column,
                     "string has no field '%s'", node->as.field_access.field);
            return NULL;
        }

        /* Auto-dereference pointer-to-struct for field access (self.x where self is *Struct) */
        bool is_ptr_deref = false;
        Type *struct_type = obj_type;
        if (obj_type && obj_type->kind == TYPE_POINTER && obj_type->as.pointer_to &&
            obj_type->as.pointer_to->kind == TYPE_STRUCT)
        {
            struct_type = obj_type->as.pointer_to;
            is_ptr_deref = true;
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
        if (obj_node->kind == AST_IDENT)
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
                struct_ptr = LLVMBuildAlloca(ctx->builder, st_llvm, "tmp.struct");
                LLVMBuildStore(ctx->builder, sub_val, struct_ptr);
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
        if (field_type && field_type->kind == TYPE_STRING)
        {
            field_val = emit_string_clone_val(ctx, field_val);
            /* The clone is a fresh owned heap string. Register as a temp
               slot so the next statement boundary frees it (unless a
               var_decl / assign consumes it via mark_moved). */
            field_val = cg_push_temp_string(ctx, field_val);
        }
        else if (field_type && field_type->kind == TYPE_STRUCT && field_type->as.strukt.has_drop)
            field_val = emit_struct_clone_val(ctx, field_val, field_llvm, field_type);
        return field_val;
    }

    case AST_MATCH:
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
            LLVMDisposeBuilder(tmp);
        }

        Type *subj_type = node->as.match.subject->resolved_type;
        bool is_fp = subj_type && type_is_float(subj_type);

        /* ---- Enum subject: switch on discriminant + binder extraction ---- */
        if (subj_type && subj_type->kind == TYPE_ENUM)
        {
            LLVMTypeRef enum_llvm = type_to_llvm(ctx, subj_type);
            LLVMTypeRef i8 = LLVMInt8TypeInContext(ctx->context);
            LLVMTypeRef ptr_type = LLVMPointerTypeInContext(ctx->context, 0);

            /* Stash subject in an alloca so we can GEP into the payload. */
            LLVMBasicBlockRef entry = LLVMGetEntryBasicBlock(ctx->current_fn);
            LLVMBuilderRef tmp_b = LLVMCreateBuilderInContext(ctx->context);
            LLVMValueRef first_inst = LLVMGetFirstInstruction(entry);
            if (first_inst) LLVMPositionBuilderBefore(tmp_b, first_inst);
            else            LLVMPositionBuilderAtEnd(tmp_b, entry);
            LLVMValueRef subj_alloca = LLVMBuildAlloca(tmp_b, enum_llvm, "match.subj");
            LLVMDisposeBuilder(tmp_b);
            LLVMBuildStore(ctx->builder, subject, subj_alloca);

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
                    LLVMValueRef body_val = codegen_expr(ctx, arm->body);
                    if (result_alloca && body_val)
                        LLVMBuildStore(ctx->builder, body_val, result_alloca);
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
                        LLVMValueRef val;
                        if (pt == subj_type) {
                            /* Self-recursive payload: payload slot stores an i8*
                               pointing at a heap-boxed enum.  Load the box pointer,
                               then load the enum value through it. */
                            LLVMValueRef box = LLVMBuildLoad2(ctx->builder, ptr_type,
                                                              field_ptr, "box");
                            val = LLVMBuildLoad2(ctx->builder, field_llvm, box, bname);
                        } else {
                            val = LLVMBuildLoad2(ctx->builder, field_llvm, field_ptr, bname);
                        }

                        /* Materialise an alloca so existing IDENT-load paths work,
                           then bind in current scope.  Mark as borrowed so scope
                           cleanup leaves heap ownership with the enum (move
                           tracking for binders is a future Phase B item). */
                        LLVMBuilderRef bb_tmp = LLVMCreateBuilderInContext(ctx->context);
                        LLVMValueRef first_i = LLVMGetFirstInstruction(entry);
                        if (first_i) LLVMPositionBuilderBefore(bb_tmp, first_i);
                        else         LLVMPositionBuilderAtEnd(bb_tmp, entry);
                        LLVMValueRef bind_alloca = LLVMBuildAlloca(bb_tmp, field_llvm, bname);
                        LLVMDisposeBuilder(bb_tmp);
                        LLVMBuildStore(ctx->builder, val, bind_alloca);
                        CgSymbol *sym = cg_scope_define(ctx->current_scope, bname,
                                                        bind_alloca, pt, NULL);
                        if (sym) sym->is_borrowed = true;
                    }
                }

                LLVMValueRef body_val = codegen_expr(ctx, arm->body);
                pop_scope(ctx);
                if (result_alloca && body_val)
                    LLVMBuildStore(ctx->builder, body_val, result_alloca);
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
                if (!subject_owned_by_scope && !is_self_recursive)
                    emit_enum_drop(ctx, subj_alloca, subj_type);
            }

            if (result_alloca)
                return LLVMBuildLoad2(ctx->builder, res_llvm, result_alloca, "match.val");
            return NULL;
        }

        for (int i = 0; i < node->as.match.arm_count; i++)
        {
            MatchArm *arm = &node->as.match.arms[i];
            bool is_wildcard = arm->pattern->kind == AST_IDENT &&
                               strcmp(arm->pattern->as.ident.name, "_") == 0;

            if (is_wildcard)
            {
                /* Default arm — generate body and branch to merge */
                LLVMValueRef body_val = codegen_expr(ctx, arm->body);
                if (result_alloca && body_val)
                {
                    LLVMBuildStore(ctx->builder, body_val, result_alloca);
                }
                LLVMBuildBr(ctx->builder, merge_bb);
            }
            else
            {
                LLVMValueRef pattern = codegen_expr(ctx, arm->pattern);
                if (pattern == NULL)
                    continue;

                LLVMValueRef cmp;
                if (subj_type && subj_type->kind == TYPE_STRING)
                {
                    /* String match: compare via strcmp */
                    LLVMValueRef strcmp_fn = LLVMGetNamedFunction(ctx->module, "strcmp");
                    LLVMTypeRef sc_type = LLVMGlobalGetValueType(strcmp_fn);
                    LLVMValueRef s_data = ls_string_data(ctx, subject);
                    LLVMValueRef p_data = ls_string_data(ctx, pattern);
                    LLVMValueRef sc_args[] = {s_data, p_data};
                    LLVMValueRef sc_res = LLVMBuildCall2(ctx->builder, sc_type, strcmp_fn,
                                                         sc_args, 2, "match.strcmp");
                    cmp = LLVMBuildICmp(ctx->builder, LLVMIntEQ, sc_res,
                                        LLVMConstInt(LLVMInt32TypeInContext(ctx->context), 0, 0),
                                        "match.cmp");
                }
                else if (is_fp)
                {
                    cmp = LLVMBuildFCmp(ctx->builder, LLVMRealOEQ, subject, pattern, "match.cmp");
                }
                else
                {
                    cmp = LLVMBuildICmp(ctx->builder, LLVMIntEQ, subject, pattern, "match.cmp");
                }

                LLVMBasicBlockRef then_bb = LLVMAppendBasicBlockInContext(
                    ctx->context, ctx->current_fn, "match.then");
                LLVMBasicBlockRef next_bb = LLVMAppendBasicBlockInContext(
                    ctx->context, ctx->current_fn, "match.next");

                LLVMBuildCondBr(ctx->builder, cmp, then_bb, next_bb);

                LLVMPositionBuilderAtEnd(ctx->builder, then_bb);
                LLVMValueRef body_val = codegen_expr(ctx, arm->body);
                if (result_alloca && body_val)
                {
                    LLVMBuildStore(ctx->builder, body_val, result_alloca);
                }
                LLVMBuildBr(ctx->builder, merge_bb);

                LLVMPositionBuilderAtEnd(ctx->builder, next_bb);
            }
        }

        /* If last arm wasn't wildcard, add unreachable fallthrough */
        if (node->as.match.arm_count > 0)
        {
            MatchArm *last = &node->as.match.arms[node->as.match.arm_count - 1];
            bool last_is_wildcard = last->pattern->kind == AST_IDENT &&
                                    strcmp(last->pattern->as.ident.name, "_") == 0;
            if (!last_is_wildcard)
            {
                LLVMBuildBr(ctx->builder, merge_bb);
            }
        }

        LLVMPositionBuilderAtEnd(ctx->builder, merge_bb);
        if (result_alloca)
        {
            return LLVMBuildLoad2(ctx->builder, res_llvm, result_alloca, "match.val");
        }
        return NULL;
    }

    case AST_TRY:
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
        int try_temp_mark = ctx->temp_string_count;
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
            /* If the unwrapped value is a string, zero its cap in inner_alloca
               so scope-cleanup skips the now-moved data (ownership transferred). */
            if (success_t && success_t->kind == TYPE_STRING) {
                LLVMTypeRef str_t = ls_string_type(ctx);
                LLVMValueRef cap_ptr = LLVMBuildStructGEP2(
                    ctx->builder, str_t, field_ptr, 2, "try.ok.cap.z");
                LLVMBuildStore(ctx->builder,
                    LLVMConstInt(LLVMInt32TypeInContext(ctx->context), 0, 0),
                    cap_ptr);
            }
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
            /* Zero the string cap in inner_alloca's Err payload so scope
               cleanup of the try's temporary alloca doesn't double-free the
               data now owned by ret_alloca. */
            Type *err_type = inner_type->as.enom.variants[failure_idx].payload_types[0];
            if (err_type && err_type->kind == TYPE_STRING) {
                LLVMValueRef err_field = LLVMBuildStructGEP2(
                    ctx->builder, err_struct, in_payload, 0, "try.err.field");
                LLVMTypeRef str_t = ls_string_type(ctx);
                LLVMValueRef cap_ptr = LLVMBuildStructGEP2(
                    ctx->builder, str_t, err_field, 2, "try.err.cap.z");
                LLVMBuildStore(ctx->builder,
                    LLVMConstInt(LLVMInt32TypeInContext(ctx->context), 0, 0),
                    cap_ptr);
            }
        }

        /* Flush temp strings from the inner expression before scope cleanup */
        cg_flush_temps(ctx, try_temp_mark, false);
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
        cg_flush_temps(ctx, try_temp_mark, false);
        if (result_alloca && success_llvm)
            return LLVMBuildLoad2(ctx->builder, success_llvm, result_alloca, "try.val");
        return NULL;
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
            if (from->kind == TYPE_F32 && to->kind == TYPE_F64)
                return LLVMBuildFPExt(ctx->builder, val, to_llvm, "fpext");
            return LLVMBuildFPTrunc(ctx->builder, val, to_llvm, "fptrunc");
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

        /* vec(T)[index] — runtime bounds check: warn + nop if out-of-bounds */
        if (obj_type && obj_type->kind == TYPE_VECTOR)
        {
            LLVMValueRef vec_alloca = NULL;
            if (obj->kind == AST_IDENT)
            {
                CgSymbol *sym = cg_scope_resolve(ctx->current_scope, obj->as.ident.name);
                if (sym)
                    vec_alloca = sym->value;
            }
            if (vec_alloca == NULL)
            {
                cg_error(ctx, node->line, node->column, "cannot get address of vec");
                return NULL;
            }
            LLVMTypeRef vec_t = ls_vec_type(ctx);
            Type *elem_type = obj_type->as.vec.elem;
            LLVMTypeRef elem_llvm = type_to_llvm(ctx, elem_type);
            LLVMTypeRef i32_t = LLVMInt32TypeInContext(ctx->context);
            LLVMTypeRef i64_t = LLVMInt64TypeInContext(ctx->context);

            LLVMValueRef vec_val = LLVMBuildLoad2(ctx->builder, vec_t, vec_alloca, "vi.v");
            LLVMValueRef data_ptr = LLVMBuildExtractValue(ctx->builder, vec_val, 0, "vi.data");
            LLVMValueRef len_val = LLVMBuildExtractValue(ctx->builder, vec_val, 1, "vi.len");

            LLVMValueRef index = codegen_expr(ctx, idx_node);
            if (index == NULL)
                return NULL;
            if (LLVMTypeOf(index) != i64_t)
                index = LLVMBuildSExtOrBitCast(ctx->builder, index, i64_t, "vi.idx");

            /* Bounds check: 0 <= index < len */
            LLVMValueRef len64 = LLVMBuildSExt(ctx->builder, len_val, i64_t, "vi.len64");
            LLVMValueRef zero64 = LLVMConstInt(i64_t, 0, 0);
            LLVMValueRef ge_zero = LLVMBuildICmp(ctx->builder, LLVMIntSGE, index, zero64, "vi.ge0");
            LLVMValueRef lt_len = LLVMBuildICmp(ctx->builder, LLVMIntSLT, index, len64, "vi.ltl");
            LLVMValueRef in_bounds = LLVMBuildAnd(ctx->builder, ge_zero, lt_len, "vi.inb");

            LLVMValueRef cur_fn = LLVMGetBasicBlockParent(LLVMGetInsertBlock(ctx->builder));
            int id = g_block_counter++;
            char ok_name[32], oob_name[32], merge_name[32];
            snprintf(ok_name, sizeof(ok_name), "vi.ok%d", id);
            snprintf(oob_name, sizeof(oob_name), "vi.oob%d", id);
            snprintf(merge_name, sizeof(merge_name), "vi.merge%d", id);
            LLVMBasicBlockRef ok_bb = LLVMAppendBasicBlockInContext(ctx->context, cur_fn, ok_name);
            LLVMBasicBlockRef oob_bb = LLVMAppendBasicBlockInContext(ctx->context, cur_fn, oob_name);
            LLVMBasicBlockRef merge_bb = LLVMAppendBasicBlockInContext(ctx->context, cur_fn, merge_name);

            /* Allocate result slot in function entry block */
            LLVMBuilderRef tb = LLVMCreateBuilderInContext(ctx->context);
            LLVMBasicBlockRef entry_bb = LLVMGetEntryBasicBlock(cur_fn);
            LLVMValueRef fi = LLVMGetFirstInstruction(entry_bb);
            if (fi)
                LLVMPositionBuilderBefore(tb, fi);
            else
                LLVMPositionBuilderAtEnd(tb, entry_bb);
            LLVMValueRef result_alloca = LLVMBuildAlloca(tb, elem_llvm, "vi.res");
            LLVMDisposeBuilder(tb);

            /* Default (out-of-bounds) value: empty string or zero */
            LLVMValueRef zero_val;
            if (elem_type && elem_type->kind == TYPE_STRING)
                zero_val = ls_string_from_literal(ctx, "", "vi.empty");
            else
                zero_val = LLVMConstNull(elem_llvm);
            LLVMBuildStore(ctx->builder, zero_val, result_alloca);

            LLVMBuildCondBr(ctx->builder, in_bounds, ok_bb, oob_bb);

            /* ok_bb: in-bounds — GEP + load + deep-clone if element owns heap data */
            LLVMPositionBuilderAtEnd(ctx->builder, ok_bb);
            LLVMValueRef gep = LLVMBuildGEP2(ctx->builder, elem_llvm, data_ptr, &index, 1, "vi.ep");
            LLVMValueRef elem = LLVMBuildLoad2(ctx->builder, elem_llvm, gep, "vi.elem");
            /* vec[i] is a READ — the vec retains ownership.  We give the caller a deep
               clone so both the caller's variable and the vec element can be freed
               independently without double-free. */
            if (elem_type->kind == TYPE_STRING)
                elem = emit_string_clone_val(ctx, elem);
            else if (elem_type->kind == TYPE_STRUCT && elem_type->as.strukt.has_drop)
                elem = emit_struct_clone_val(ctx, elem, elem_llvm, elem_type);
            /* builder may have moved to a new block inside the clone helpers */
            LLVMBuildStore(ctx->builder, elem, result_alloca);
            LLVMBuildBr(ctx->builder, merge_bb);

            /* oob_bb: out-of-bounds — print warning, result stays as zero/empty */
            LLVMPositionBuilderAtEnd(ctx->builder, oob_bb);
            LLVMValueRef idx32 = LLVMBuildTrunc(ctx->builder, index, i32_t, "vi.idx32");
            LLVMValueRef warn_args[2] = {idx32, len_val};
            emit_printf(ctx,
                        "[warning] vec index out of bounds: index=%d, len=%d\n",
                        warn_args, 2);
            LLVMBuildBr(ctx->builder, merge_bb);

            /* merge_bb: load and return result */
            LLVMPositionBuilderAtEnd(ctx->builder, merge_bb);
            LLVMValueRef r = LLVMBuildLoad2(ctx->builder, elem_llvm, result_alloca, "vi.r");

            /* Register the owned clone as a temp so that:
               - AST_VAR_DECL sees temp_string_count > temp_mark and transfers
                 ownership via cg_mark_last_temp_moved (avoids a second clone).
               - If the result is used transiently (e.g. in print(v[0])),
                 cg_flush_temps will free it correctly. */
            if (elem_type->kind == TYPE_STRING && ctx->current_fn != NULL)
            {
                if (ctx->temp_string_count >= ctx->temp_string_cap)
                {
                    ctx->temp_string_cap = GROW_CAPACITY(ctx->temp_string_cap);
                    ctx->temp_string_slots = GROW_ARRAY(LLVMValueRef,
                                                        ctx->temp_string_slots,
                                                        ctx->temp_string_cap);
                }
                ctx->temp_string_slots[ctx->temp_string_count++] = result_alloca;
            }
            return r;
        }

        /* map[key] — call __ls_map_XX_YY_get(map, key) */
        if (obj_type && obj_type->kind == TYPE_MAP)
        {
            LLVMValueRef map_alloca = NULL;
            if (obj->kind == AST_IDENT)
            {
                CgSymbol *sym = cg_scope_resolve(ctx->current_scope, obj->as.ident.name);
                if (sym)
                    map_alloca = sym->value;
            }
            if (map_alloca == NULL)
            {
                cg_error(ctx, node->line, node->column, "cannot get address of map");
                return NULL;
            }
            emit_map_helpers_for(ctx, obj_type->as.map.key, obj_type->as.map.val);
            char suffix[64];
            map_type_id(obj_type->as.map.key, obj_type->as.map.val, suffix, sizeof(suffix));
            char nm[96];
            snprintf(nm, sizeof(nm), "__ls_map_%s_get", suffix);
            LLVMValueRef fn = LLVMGetNamedFunction(ctx->module, nm);
            LLVMTypeRef ft = LLVMGlobalGetValueType(fn);
            LLVMValueRef kv = codegen_expr(ctx, idx_node);
            if (!kv)
                return NULL;
            LLVMValueRef args[] = {map_alloca, kv};
            return LLVMBuildCall2(ctx->builder, ft, fn, args, 2, "mg");
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
        if (elem_type && elem_type->kind == TYPE_STRING)
            elem = emit_string_clone_val(ctx, elem);
        else if (elem_type && elem_type->kind == TYPE_STRUCT && elem_type->as.strukt.has_drop)
            elem = emit_struct_clone_val(ctx, elem, elem_llvm, elem_type);
        return elem;
    }

    case AST_ARRAY_LIT:
    {
        /* Array literal — build constant array if possible, else return NULL
           (caller VAR_DECL handles element-by-element store) */
        Type *arr_type = node->resolved_type;
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

        /* Allocate storage: stack alloca for value literal, malloc for new */
        LLVMValueRef storage;
        if (on_stack)
        {
            storage = LLVMBuildAlloca(ctx->builder, st_llvm, "sl.tmp");
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

        /* Apply field initializers */
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
            LLVMValueRef val = codegen_expr(ctx, node->as.new_expr.field_inits[i].value);
            if (val == NULL)
                return NULL;
            /* Clone owned strings so the struct field holds an independent copy.
               Otherwise the temp string's data pointer is shared: the temp flush
               frees it → struct field double-frees on scope exit. */
            Type *field_type = struct_type->as.strukt.fields[field_idx].type;
            if (field_type && field_type->kind == TYPE_STRING)
                val = emit_string_clone_val(ctx, val);
            LLVMValueRef field_ptr = LLVMBuildStructGEP2(ctx->builder, st_llvm,
                                                         storage, (unsigned)field_idx,
                                                         "field_ptr");
            LLVMBuildStore(ctx->builder, val, field_ptr);
        }

        if (on_stack)
        {
            /* Return the loaded struct aggregate value */
            return LLVMBuildLoad2(ctx->builder, st_llvm, storage, "sl.val");
        }
        return storage; /* new: return pointer */
    }

    default:
        cg_error(ctx, node->line, node->column,
                 "unsupported expression node: %s", ast_kind_name(node->kind));
        return NULL;
    }
}

/* ---- Short-circuit for && and || ---- */

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
    LLVMValueRef right = codegen_expr(ctx, node->as.binary.right);
    LLVMBasicBlockRef rhs_end = LLVMGetInsertBlock(ctx->builder);
    LLVMBuildBr(ctx->builder, merge_bb);

    LLVMPositionBuilderAtEnd(ctx->builder, merge_bb);
    LLVMValueRef phi = LLVMBuildPhi(ctx->builder, LLVMInt1TypeInContext(ctx->context), "sc");
    LLVMValueRef incoming_vals[2] = {left, right};
    LLVMBasicBlockRef incoming_bbs[2] = {entry_bb, rhs_end};
    LLVMAddIncoming(phi, incoming_vals, incoming_bbs, 2);
    return phi;
}

/* ---- Statement codegen ---- */

static void codegen_stmt(CodegenContext *ctx, AstNode *node)
{
    if (node == NULL)
        return;

#if CG_DEBUG_LV2
    printf(">>>>> codegen_stmt, node->kind:%u\n", node->kind);
#endif

    switch (node->kind)
    {
    case AST_VAR_DECL:
    {
        Type *var_type = node->resolved_type;
        if (var_type == NULL)
            return;
        LLVMTypeRef llvm_type = type_to_llvm(ctx, var_type);

        /* Alloca at function entry */
        LLVMBasicBlockRef entry = LLVMGetEntryBasicBlock(ctx->current_fn);
        LLVMBuilderRef tmp = LLVMCreateBuilderInContext(ctx->context);
        LLVMValueRef first_inst = LLVMGetFirstInstruction(entry);
        if (first_inst)
            LLVMPositionBuilderBefore(tmp, first_inst);
        else
            LLVMPositionBuilderAtEnd(tmp, entry);
        LLVMValueRef alloca = LLVMBuildAlloca(tmp, llvm_type, node->as.var_decl.name);
        LLVMDisposeBuilder(tmp);

        /* Allocate moved_flag for struct-with-drop types */
        LLVMValueRef moved_flag = NULL;
        if (var_type->kind == TYPE_STRUCT && var_type->as.strukt.has_drop)
        {
            LLVMTypeRef i1_type = LLVMInt1TypeInContext(ctx->context);
            moved_flag = LLVMBuildAlloca(ctx->builder, i1_type, "var.moved");
            LLVMBuildStore(ctx->builder, LLVMConstInt(i1_type, 0, 0), moved_flag);
        }

        /* Zero-initialize arrays that contain strings or droppable structs.
           Without this, unassigned array elements have garbage cap/data values,
           causing emit_cleanup_to to call free() on invalid pointers at scope exit. */
        if (var_type->kind == TYPE_ARRAY && var_type->as.array.elem)
        {
            Type *elem = var_type->as.array.elem;
            bool needs_zero_init =
                (elem->kind == TYPE_STRING) ||
                (elem->kind == TYPE_STRUCT && elem->as.strukt.has_drop);
            if (needs_zero_init)
            {
                LLVMBuildStore(ctx->builder, LLVMConstNull(llvm_type), alloca);
            }
        }
        /* Zero-initialize structs-with-drop so that any string/nested-struct fields
           start with cap=0/data=NULL. Without this, an uninitialized struct pushed into
           a vec would have garbage cap values → free() on garbage pointer on scope exit. */
        if (var_type->kind == TYPE_STRUCT && var_type->as.strukt.has_drop)
        {
            LLVMBuildStore(ctx->builder, LLVMConstNull(llvm_type), alloca);
        }
        /* Initialize uninitialized string variables to a static empty string
           ({data="", len=0, cap=0}). Without this, the alloca holds garbage,
           causing print() to dereference an invalid pointer and `s += ...`
           to read a garbage cap/len. cap=0 marks it static, so scope cleanup
           skips free(), and the static→owned path in str.append handles growth. */
        if (var_type->kind == TYPE_STRING && !node->as.var_decl.init)
        {
            LLVMBuildStore(ctx->builder,
                           ls_string_from_literal(ctx, "", "str.empty"),
                           alloca);
        }
        /* Zero-initialize vec variables so that cap=0, data=NULL, len=0.
           emit_cleanup_to checks cap > 0 before freeing, so this is safe. */
        if (var_type->kind == TYPE_VECTOR)
        {
            LLVMBuildStore(ctx->builder, LLVMConstNull(llvm_type), alloca);
        }
        /* Zero-initialize map variables so that cap=0, buckets=NULL, len=0. */
        if (var_type->kind == TYPE_MAP)
        {
            LLVMBuildStore(ctx->builder, LLVMConstNull(llvm_type), alloca);
            /* Ensure map helpers are emitted for this (K,V) pair */
            emit_map_helpers_for(ctx, var_type->as.map.key, var_type->as.map.val);
        }

        if (node->as.var_decl.init)
        {
            /* Track temp slots created during init expression evaluation */
            int temp_mark = ctx->temp_string_count;

            /* Special handling for map literal initialization:
               { key -> val, ... } — call set for each pair into the already-zero'd alloca */
            if (var_type->kind == TYPE_MAP &&
                node->as.var_decl.init->kind == AST_MAP_LIT)
            {
                AstNode *ml = node->as.var_decl.init;
                Type *key_type = var_type->as.map.key;
                Type *val_type = var_type->as.map.val;

                /* Helpers already emitted above when zero-initialising the map alloca */
                char suffix[64];
                map_type_id(key_type, val_type, suffix, sizeof(suffix));
                char nm_set[96];
                snprintf(nm_set, sizeof(nm_set), "__ls_map_%s_set", suffix);
                LLVMValueRef fn_set = LLVMGetNamedFunction(ctx->module, nm_set);
                if (fn_set)
                {
                    LLVMTypeRef ft_set = LLVMGlobalGetValueType(fn_set);
                    for (int i = 0; i < ml->as.map_lit.pair_count; i++)
                    {
                        LLVMValueRef kv = codegen_expr(ctx, ml->as.map_lit.keys[i]);
                        LLVMValueRef vv = codegen_expr(ctx, ml->as.map_lit.vals[i]);
                        if (!kv || !vv)
                            continue;
                        LLVMValueRef set_args[] = {alloca, kv, vv};
                        LLVMBuildCall2(ctx->builder, ft_set, fn_set, set_args, 3, "");
                    }
                }
                /* map.set deep-copies keys/values, so temps from literal keys/values
                   (e.g. "x".upper()) are not consumed — free them here. */
                cg_flush_temps(ctx, temp_mark, false);
            }
            /* Special handling for vec literal initialization:
               vec(T) v = [e1, e2, ...] (or empty []).
               Build the vec by pushing each element, mirroring vec.push semantics
               (grow + slot store + ownership transfer for owned-data elements). */
            else if (var_type->kind == TYPE_VECTOR &&
                     node->as.var_decl.init->kind == AST_ARRAY_LIT)
            {
                AstNode *lit = node->as.var_decl.init;
                int count = lit->as.array_lit.count;
                Type *vec_elem = var_type->as.vec.elem;
                LLVMTypeRef vec_elem_llvm = type_to_llvm(ctx, vec_elem);
                LLVMTypeRef vec_t_llvm = ls_vec_type(ctx);
                LLVMTypeRef i32_t_llvm = LLVMInt32TypeInContext(ctx->context);
                LLVMTypeRef i64_t_llvm = LLVMInt64TypeInContext(ctx->context);

                /* alloca was already zero-initialized by the TYPE_VECTOR branch above. */

                for (int i = 0; i < count; i++)
                {
                    int push_temp_mark = ctx->temp_string_count;
                    LLVMValueRef val = codegen_expr(ctx, lit->as.array_lit.elements[i]);
                    if (val == NULL)
                        continue;

                    /* Grow if needed (cap=0 → 4, then doubling). */
                    emit_vec_grow_inline(ctx, alloca, vec_elem_llvm);

                    /* For string elements, ensure we store an owned copy:
                       - Fresh temp (e.g. "x".upper()) → mark moved, transfer ownership.
                       - Plain IDENT or literal returns no temp; clone if it's an IDENT
                         so the source variable can still be freed. Static literals
                         (cap=0) are safe to store directly. */
                    if (vec_elem->kind == TYPE_STRING)
                    {
                        if (ctx->temp_string_count > push_temp_mark)
                        {
                            cg_mark_last_temp_moved(ctx, push_temp_mark, "vec literal: temp owned by vec");
                        }
                        else
                        {
                            AstNode *src = ast_unwrap_move(lit->as.array_lit.elements[i]);
                            if (src->kind == AST_IDENT)
                                val = emit_string_clone_val(ctx, val);
                        }
                    }

                    /* data[len] = val; len++ */
                    LLVMValueRef vec_val = LLVMBuildLoad2(ctx->builder, vec_t_llvm, alloca, "vl.v");
                    LLVMValueRef data_ptr = LLVMBuildExtractValue(ctx->builder, vec_val, 0, "vl.data");
                    LLVMValueRef len_val = LLVMBuildExtractValue(ctx->builder, vec_val, 1, "vl.len");
                    LLVMValueRef len64 = LLVMBuildSExt(ctx->builder, len_val, i64_t_llvm, "vl.len64");
                    LLVMValueRef elem_ptr = LLVMBuildGEP2(ctx->builder, vec_elem_llvm, data_ptr,
                                                          &len64, 1, "vl.slot");
                    LLVMBuildStore(ctx->builder, val, elem_ptr);

                    LLVMValueRef one32 = LLVMConstInt(i32_t_llvm, 1, 0);
                    LLVMValueRef new_len = LLVMBuildAdd(ctx->builder, len_val, one32, "vl.nlen");
                    LLVMValueRef vec_upd = LLVMBuildLoad2(ctx->builder, vec_t_llvm, alloca, "vl.upd");
                    vec_upd = LLVMBuildInsertValue(ctx->builder, vec_upd, new_len, 1, "vl.ul");
                    LLVMBuildStore(ctx->builder, vec_upd, alloca);
                }
                ctx->temp_string_count = temp_mark;
            }
            /* Special handling for array literal initialization */
            else if (var_type->kind == TYPE_ARRAY &&
                     node->as.var_decl.init->kind == AST_ARRAY_LIT)
            {
                AstNode *lit = node->as.var_decl.init;
                int count = lit->as.array_lit.count;
                LLVMTypeRef i64_type = LLVMInt64TypeInContext(ctx->context);
                LLVMValueRef zero = LLVMConstInt(i64_type, 0, 0);

                /* Try constant array first */
                LLVMValueRef const_arr = codegen_expr(ctx, lit);
                if (const_arr)
                {
                    LLVMBuildStore(ctx->builder, const_arr, alloca);
                }
                else
                {
                    /* Element-by-element store */
                    for (int i = 0; i < count; i++)
                    {
                        LLVMValueRef elem_val = codegen_expr(ctx, lit->as.array_lit.elements[i]);
                        if (elem_val == NULL)
                            continue;
                        LLVMValueRef idx = LLVMConstInt(i64_type, (uint64_t)i, 0);
                        LLVMValueRef indices[2] = {zero, idx};
                        LLVMValueRef gep = LLVMBuildGEP2(ctx->builder, llvm_type,
                                                         alloca, indices, 2, "arr.init");
                        LLVMBuildStore(ctx->builder, elem_val, gep);
                    }
                }
                /* For array-of-string init, each element temp was stored into the array;
                   mark all temps as moved so they don't get double-freed. */
                if (var_type->as.array.elem && var_type->as.array.elem->kind == TYPE_STRING)
                {
                    for (int ti = temp_mark; ti < ctx->temp_string_count; ti++)
                    {
                        mark_string_moved(ctx, ctx->temp_string_slots[ti], "array init: temp owned by array");
                    }
                }
                ctx->temp_string_count = temp_mark;
            }
            else
            {
                LLVMValueRef init = codegen_expr(ctx, node->as.var_decl.init);
                if (init)
                {
                    /* Implicit numeric widening: var_decl init expr's type may
                       differ from var_type (e.g. f64 x = 5_int). The checker
                       allowed this iff type_widens_to(init_t, var_type). */
                    Type *init_t = node->as.var_decl.init->resolved_type;
                    if (init_t && type_is_numeric(init_t) &&
                        type_is_numeric(var_type) && !type_equals(init_t, var_type))
                    {
                        init = cg_widen(ctx, init, init_t, var_type);
                    }
                    if (var_type->kind == TYPE_STRING)
                    {
                        if (ctx->temp_string_count > temp_mark)
                        {
                            /* A fresh owned string was produced during evaluation
                               (e.g. "x".upper(), concat, f-string).  Transfer
                               ownership: mark the last temp slot as moved so the
                               temp-cleanup at statement end won't free it. */
                            cg_mark_last_temp_moved(ctx, temp_mark, "var_decl: temp owned by new var");
                        }
                        else
                        {
                            /* No temp was created. Determine: clone (source
                               retains ownership) or transfer (rvalue, callee
                               gave up ownership).
                               - AST_CALL returning string: transfer; clone here
                                 would leak the heap returned by the callee.
                               - AST_IDENT / AST_FIELD: clone so both source and
                                 this var have independently-owned copies.
                               - static literal (cap==0): clone is a no-op (just
                                 copies the {data, len, 0} value). */
                            AstNode *init_node = ast_unwrap_move(node->as.var_decl.init);
                            bool is_rvalue_transfer =
                                init_node &&
                                (init_node->kind == AST_CALL ||
                                 init_node->kind == AST_TRY);
                            if (is_rvalue_transfer)
                            {
                                /* Transfer: rvalue from a call / try expression
                                   already gave up ownership; cloning here would
                                   leak the heap that came back from the callee. */
                            }
                            else
                            {
                                init = emit_string_clone_val(ctx, init);
                            }
                        }
                    }
                    else if (var_type->kind == TYPE_STRUCT &&
                             var_type->as.strukt.has_drop &&
                             ast_unwrap_move(node->as.var_decl.init)->kind == AST_IDENT)
                    {
                        /* Source is another named struct variable — deep-clone so both
                           this variable and the source have independently owned string
                           fields and can be freed without double-free. */
                        LLVMTypeRef llvm_st = type_to_llvm(ctx, var_type);
                        init = emit_struct_clone_val(ctx, init, llvm_st, var_type);
                    }

                    LLVMBuildStore(ctx->builder, init, alloca);
                }

                if (var_type->kind == TYPE_STRING)
                    cg_flush_temps(ctx, temp_mark, true);
                else
                    cg_flush_temps(ctx, temp_mark, false);
            }
        }

        cg_scope_define(ctx->current_scope, node->as.var_decl.name, alloca, var_type, moved_flag);
        break;
    }

    case AST_ASSIGN:
    {
        /* Optimization: a = a + b → in-place append (avoids malloc+free round-trip).
           Detect before evaluating RHS so we never allocate the combined string. */
        if (node->as.assign.op == TOKEN_ASSIGN &&
            node->as.assign.target->kind == AST_IDENT &&
            node->as.assign.value->kind == AST_BINARY &&
            node->as.assign.value->as.binary.op == TOKEN_PLUS)
        {
            AstNode *tgt = node->as.assign.target;
            AstNode *bin = node->as.assign.value;
            Type *tgt_type = tgt->resolved_type;
            /* Only apply when target type is string */
            if (tgt_type && tgt_type->kind == TYPE_STRING &&
                bin->as.binary.left->kind == AST_IDENT &&
                strcmp(tgt->as.ident.name, bin->as.binary.left->as.ident.name) == 0)
            {
                CgSymbol *sym = cg_scope_resolve(ctx->current_scope, tgt->as.ident.name);
                if (sym != NULL)
                {
                    int tmp_mark = ctx->temp_string_count;
                    LLVMValueRef rhs = codegen_expr(ctx, bin->as.binary.right);
                    if (rhs == NULL)
                        return;
                    Type *rhs_type = bin->as.binary.right->resolved_type;
                    if (rhs_type && rhs_type->kind == TYPE_STRING)
                    {
                        emit_string_append_inline(ctx, sym->value,
                                                  ls_string_data(ctx, rhs),
                                                  ls_string_len(ctx, rhs));
                    }
                    else /* char or int */
                    {
                        LLVMTypeRef i8_t = LLVMInt8TypeInContext(ctx->context);
                        LLVMTypeRef i32_t = LLVMInt32TypeInContext(ctx->context);
                        LLVMValueRef ci8 = LLVMBuildTrunc(ctx->builder, rhs, i8_t, "opt.ci8");
                        LLVMValueRef slot = LLVMBuildAlloca(ctx->builder, i8_t, "opt.slot");
                        LLVMBuildStore(ctx->builder, ci8, slot);
                        emit_string_append_inline(ctx, sym->value, slot,
                                                  LLVMConstInt(i32_t, 1, 0));
                        (void)i32_t;
                    }
                    cg_flush_temps(ctx, tmp_mark, false);
                    break; /* exit case AST_ASSIGN */
                }
            }
        }

        /* Track temp string slots created during value evaluation */
        int temp_mark = ctx->temp_string_count;
        LLVMValueRef val = codegen_expr(ctx, node->as.assign.value);
        if (val == NULL)
            return;

        /* Implicit numeric widening on assignment: rhs may be a smaller
           numeric type that widens to the target's type. */
        {
            Type *as_tgt_t = node->as.assign.target->resolved_type;
            Type *as_rhs_t = node->as.assign.value->resolved_type;
            if (as_tgt_t && as_rhs_t && type_is_numeric(as_tgt_t) &&
                type_is_numeric(as_rhs_t) && !type_equals(as_tgt_t, as_rhs_t) &&
                node->as.assign.op == TOKEN_ASSIGN)
            {
                val = cg_widen(ctx, val, as_rhs_t, as_tgt_t);
            }
        }

        /* Simple variable assignment */
        if (node->as.assign.target->kind == AST_IDENT)
        {
            CgSymbol *sym = cg_scope_resolve(ctx->current_scope,
                                             node->as.assign.target->as.ident.name);
            if (sym == NULL)
            {
                cg_error(ctx, node->line, node->column, "undefined variable in assignment");
                return;
            }

            if (node->as.assign.op == TOKEN_ASSIGN)
            {
                if (sym->type && sym->type->kind == TYPE_STRING)
                {
                    CgSymbol *src_sym = get_string_var_symbol(node->as.assign.value, ctx);
                    if (src_sym != NULL && src_sym->value == sym->value)
                    {
                        /* Self-assignment (s = s): do nothing */
                    }
                    else if (src_sym != NULL)
                    {
                        /* String variable → string variable: clone semantics.
                           Free old dst, deep-copy src so both variables remain valid.
                           If dst is a vec loop var its cap was zeroed at load time,
                           so emit_string_free is a no-op at runtime (cap == 0). */
                        emit_string_free(ctx, sym->value);
                        LLVMTypeRef str_type = ls_string_type(ctx);
                        LLVMValueRef src_val = LLVMBuildLoad2(ctx->builder, str_type,
                                                              src_sym->value, "sass.src");
                        LLVMValueRef cloned = emit_string_clone_val(ctx, src_val);
                        LLVMBuildStore(ctx->builder, cloned, sym->value);
                    }
                    else
                    {
                        /* Expression result → string variable: free old dst, store new.
                           (After the AST_FIELD/array clone fixes, 'val' from field-reads
                           and collection-reads is already an independently owned copy.)
                           If dst is a vec loop var its cap was zeroed, so free is no-op. */
                        emit_string_free(ctx, sym->value);
                        LLVMBuildStore(ctx->builder, val, sym->value);
                        cg_mark_last_temp_moved(ctx, temp_mark, "assign: temp owned by string var");
                    }
                    cg_flush_temps(ctx, temp_mark, true);
                }
                else if (sym->type && sym->type->kind == TYPE_STRUCT &&
                         sym->type->as.strukt.has_drop)
                {
                    /* Struct-with-drop assignment:
                       1. Drop the old value currently in sym (frees its owned strings etc.)
                       2. If source is another variable (IDENT), deep-clone it so both dst
                          and src have independently owned copies.
                       3. Store (cloned or fresh) value.
                       If dst is a vec loop var, sym->moved_flag (borrowed_flag) is 1 at
                       runtime, so emit_struct_drop_cond is a no-op for the borrowed data. */

                    /* Step 1: drop old value (runtime-conditional via moved_flag) */
                    emit_struct_drop_cond(ctx, sym->value, sym->type, sym->moved_flag);

                    /* Step 2: clone if source is an IDENT (shared variable).
                       __move(x) unwraps to x so the clone still protects against
                       shallow-copy aliasing — the checker has blocked later use
                       of x, but both ends of this assignment still scope-drop. */
                    if (ast_unwrap_move(node->as.assign.value)->kind == AST_IDENT)
                    {
                        LLVMTypeRef llvm_st = type_to_llvm(ctx, sym->type);
                        val = emit_struct_clone_val(ctx, val, llvm_st, sym->type);
                    }

                    /* Step 3: store */
                    LLVMBuildStore(ctx->builder, val, sym->value);

                    /* After assignment the variable is alive again — clear moved_flag */
                    if (sym->moved_flag)
                    {
                        LLVMTypeRef i1 = LLVMInt1TypeInContext(ctx->context);
                        LLVMBuildStore(ctx->builder, LLVMConstInt(i1, 0, 0), sym->moved_flag);
                    }
                    ctx->temp_string_count = temp_mark;
                }
                else
                {
                    LLVMBuildStore(ctx->builder, val, sym->value);
                    ctx->temp_string_count = temp_mark;
                }
            }
            else
            {
                /* string += string|char|int: in-place append */
                if (sym->type && sym->type->kind == TYPE_STRING &&
                    node->as.assign.op == TOKEN_PLUS_ASSIGN)
                {
                    Type *rhs_type = node->as.assign.value->resolved_type;
                    if (rhs_type && rhs_type->kind == TYPE_STRING)
                    {
                        LLVMValueRef suf_data = ls_string_data(ctx, val);
                        LLVMValueRef suf_len = ls_string_len(ctx, val);
                        emit_string_append_inline(ctx, sym->value, suf_data, suf_len);
                    }
                    else /* char or int: single-byte append */
                    {
                        LLVMTypeRef i8_t = LLVMInt8TypeInContext(ctx->context);
                        LLVMTypeRef i32_t = LLVMInt32TypeInContext(ctx->context);
                        LLVMValueRef char_i8 = LLVMBuildTrunc(ctx->builder, val, i8_t, "paeq.ci8");
                        LLVMValueRef char_slot = LLVMBuildAlloca(ctx->builder, i8_t, "paeq.slot");
                        LLVMBuildStore(ctx->builder, char_i8, char_slot);
                        LLVMValueRef one32 = LLVMConstInt(i32_t, 1, 0);
                        emit_string_append_inline(ctx, sym->value, char_slot, one32);
                        (void)i32_t;
                    }
                    /* RHS temp string data was copied — free it */
                    cg_flush_temps(ctx, temp_mark, false);
                    break; /* exit case AST_ASSIGN */
                }

                /* Compound assignment: load current, operate, store */
                LLVMTypeRef lt = type_to_llvm(ctx, sym->type);
                LLVMValueRef current = LLVMBuildLoad2(ctx->builder, lt, sym->value, "cur");
                bool is_fp = sym->type && type_is_float(sym->type);
                LLVMValueRef result = NULL;

                switch (node->as.assign.op)
                {
                case TOKEN_PLUS_ASSIGN:
                    result = is_fp ? LLVMBuildFAdd(ctx->builder, current, val, "fadd")
                                   : LLVMBuildAdd(ctx->builder, current, val, "add");
                    break;
                case TOKEN_MINUS_ASSIGN:
                    result = is_fp ? LLVMBuildFSub(ctx->builder, current, val, "fsub")
                                   : LLVMBuildSub(ctx->builder, current, val, "sub");
                    break;
                case TOKEN_STAR_ASSIGN:
                    result = is_fp ? LLVMBuildFMul(ctx->builder, current, val, "fmul")
                                   : LLVMBuildMul(ctx->builder, current, val, "mul");
                    break;
                case TOKEN_SLASH_ASSIGN:
                    result = is_fp ? LLVMBuildFDiv(ctx->builder, current, val, "fdiv")
                                   : LLVMBuildSDiv(ctx->builder, current, val, "sdiv");
                    break;
                default:
                    break;
                }
                if (result)
                    LLVMBuildStore(ctx->builder, result, sym->value);
                ctx->temp_string_count = temp_mark;
            }
        }
        else if (node->as.assign.target->kind == AST_FIELD)
        {
            /* Struct field assignment — use codegen_lvalue_ptr to handle nested access
               (e.g. p1.s.k = expr) which the old IDENT-only path could not reach */
            LLVMValueRef field_ptr = codegen_lvalue_ptr(ctx, node->as.assign.target);
            if (field_ptr != NULL)
            {
                Type *field_type = node->as.assign.target->resolved_type;
                bool field_is_string = (field_type && field_type->kind == TYPE_STRING);

                if (field_is_string)
                {
                    /* Free old field value before overwriting */
                    emit_string_free(ctx, field_ptr);
                    CgSymbol *src_sym = get_string_var_symbol(node->as.assign.value, ctx);
                    if (src_sym != NULL)
                    {
                        /* Clone semantics: deep-copy src so field and src remain independent */
                        LLVMTypeRef str_type = ls_string_type(ctx);
                        LLVMValueRef src_val = LLVMBuildLoad2(ctx->builder, str_type,
                                                              src_sym->value, "sfld.src");
                        LLVMValueRef cloned = emit_string_clone_val(ctx, src_val);
                        LLVMBuildStore(ctx->builder, cloned, field_ptr);
                    }
                    else
                    {
                        LLVMBuildStore(ctx->builder, val, field_ptr);
                        cg_mark_last_temp_moved(ctx, temp_mark, "assign: temp owned by struct field");
                    }
                    cg_flush_temps(ctx, temp_mark, true);
                }
                else
                {
                    LLVMBuildStore(ctx->builder, val, field_ptr);
                    ctx->temp_string_count = temp_mark;
                }
            }
            else
            {
                ctx->temp_string_count = temp_mark;
            }
        }
        else if (node->as.assign.target->kind == AST_INDEX)
        {
            /* arr[i] = val or v[i] = val */
            AstNode *target = node->as.assign.target;
            AstNode *obj = target->as.index_expr.object;
            Type *obj_type = obj->resolved_type;

            if (obj_type && obj_type->kind == TYPE_VECTOR)
            {
                /* vec[i] = val — load data ptr, GEP, drop old if needed, store */
                LLVMValueRef vec_alloca = NULL;
                if (obj->kind == AST_IDENT)
                {
                    CgSymbol *sym = cg_scope_resolve(ctx->current_scope, obj->as.ident.name);
                    if (sym)
                        vec_alloca = sym->value;
                }
                if (vec_alloca == NULL)
                    return;

                LLVMTypeRef vec_t = ls_vec_type(ctx);
                LLVMTypeRef elem_llvm = type_to_llvm(ctx, obj_type->as.vec.elem);
                LLVMTypeRef i64_type = LLVMInt64TypeInContext(ctx->context);

                LLVMValueRef index = codegen_expr(ctx, target->as.index_expr.index);
                if (index == NULL)
                    return;
                if (LLVMTypeOf(index) != i64_type)
                    index = LLVMBuildSExtOrBitCast(ctx->builder, index, i64_type, "vis.idx");

                LLVMValueRef vec_val = LLVMBuildLoad2(ctx->builder, vec_t, vec_alloca, "vis.v");
                LLVMValueRef data_ptr = LLVMBuildExtractValue(ctx->builder, vec_val, 0, "vis.data");
                LLVMValueRef gep = LLVMBuildGEP2(ctx->builder, elem_llvm, data_ptr,
                                                 &index, 1, "vis.ep");

                Type *elem_type = obj_type->as.vec.elem;
                bool elem_is_string = (elem_type && elem_type->kind == TYPE_STRING);
                if (elem_is_string)
                {
                    emit_string_free(ctx, gep);
                    LLVMBuildStore(ctx->builder, val, gep);
                    cg_mark_last_temp_moved(ctx, temp_mark, "assign: temp owned by vec element");
                    cg_flush_temps(ctx, temp_mark, true);
                }
                else if (elem_type && elem_type->kind == TYPE_STRUCT &&
                         elem_type->as.strukt.has_drop)
                {
                    /* drop old struct element before overwriting */
                    emit_vec_elem_drop_at(ctx, gep, elem_type, 0, "idx.assign");
                    LLVMBuildStore(ctx->builder, val, gep);
                    ctx->temp_string_count = temp_mark;
                }
                else
                {
                    LLVMBuildStore(ctx->builder, val, gep);
                    ctx->temp_string_count = temp_mark;
                }
            }
            else if (obj_type && obj_type->kind == TYPE_MAP)
            {
                /* map[key] = val — call __ls_map_XX_YY_set(map, key, val) */
                LLVMValueRef map_alloca = NULL;
                if (obj->kind == AST_IDENT)
                {
                    CgSymbol *sym = cg_scope_resolve(ctx->current_scope, obj->as.ident.name);
                    if (sym)
                        map_alloca = sym->value;
                }
                if (map_alloca == NULL)
                    return;
                emit_map_helpers_for(ctx, obj_type->as.map.key, obj_type->as.map.val);
                char msuffix[64];
                map_type_id(obj_type->as.map.key, obj_type->as.map.val, msuffix, sizeof(msuffix));
                char mnm[96];
                snprintf(mnm, sizeof(mnm), "__ls_map_%s_set", msuffix);
                LLVMValueRef mfn = LLVMGetNamedFunction(ctx->module, mnm);
                LLVMTypeRef mft = LLVMGlobalGetValueType(mfn);
                LLVMValueRef mk = codegen_expr(ctx, target->as.index_expr.index);
                if (!mk)
                    return;
                LLVMValueRef margs[] = {map_alloca, mk, val};
                LLVMBuildCall2(ctx->builder, mft, mfn, margs, 3, "");
                ctx->temp_string_count = temp_mark;
            }
            else
            {
                /* fixed array[i] = val */
                if (obj_type == NULL || obj_type->kind != TYPE_ARRAY)
                    return;

                LLVMValueRef arr_ptr = NULL;
                if (obj->kind == AST_IDENT)
                {
                    CgSymbol *sym = cg_scope_resolve(ctx->current_scope, obj->as.ident.name);
                    if (sym)
                        arr_ptr = sym->value;
                }
                if (arr_ptr == NULL)
                    return;

                LLVMValueRef index = codegen_expr(ctx, target->as.index_expr.index);
                if (index == NULL)
                    return;

                LLVMTypeRef i64_type = LLVMInt64TypeInContext(ctx->context);
                if (LLVMTypeOf(index) != i64_type)
                {
                    index = LLVMBuildSExtOrBitCast(ctx->builder, index, i64_type, "idx.ext");
                }

                LLVMTypeRef arr_llvm = type_to_llvm(ctx, obj_type);
                LLVMValueRef zero = LLVMConstInt(i64_type, 0, 0);
                LLVMValueRef indices[2] = {zero, index};
                LLVMValueRef gep = LLVMBuildGEP2(ctx->builder, arr_llvm, arr_ptr,
                                                 indices, 2, "arr.store");

                bool elem_is_string = (obj_type->as.array.elem &&
                                       obj_type->as.array.elem->kind == TYPE_STRING);
                if (elem_is_string)
                {
                    /* BUG #4 fix: free old element value before overwriting */
                    emit_string_free(ctx, gep);
                    LLVMBuildStore(ctx->builder, val, gep);
                    cg_mark_last_temp_moved(ctx, temp_mark, "assign: temp owned by array element");
                    cg_flush_temps(ctx, temp_mark, true);
                }
                else
                {
                    LLVMBuildStore(ctx->builder, val, gep);
                    ctx->temp_string_count = temp_mark;
                }
            }
        }
        else if (node->as.assign.target->kind == AST_UNARY &&
                 node->as.assign.target->as.unary.op == TOKEN_STAR)
        {
            /* *ptr = val — must drop old pointed-to value before overwriting */
            LLVMValueRef ptr = codegen_expr(ctx, node->as.assign.target->as.unary.operand);
            if (ptr == NULL)
                return;

            /* target->resolved_type is T (the pointed-to type, not *T) */
            Type *target_type = node->as.assign.target->resolved_type;

            if (target_type && target_type->kind == TYPE_STRING)
            {
                /* Drop old string at *ptr */
                emit_string_free(ctx, ptr);
                /* Clone if RHS is a named string variable */
                CgSymbol *src_sym = get_string_var_symbol(node->as.assign.value, ctx);
                if (src_sym != NULL)
                {
                    LLVMTypeRef str_type = ls_string_type(ctx);
                    LLVMValueRef src_val = LLVMBuildLoad2(ctx->builder, str_type,
                                                          src_sym->value, "dref.src");
                    LLVMValueRef cloned = emit_string_clone_val(ctx, src_val);
                    LLVMBuildStore(ctx->builder, cloned, ptr);
                }
                else
                {
                    LLVMBuildStore(ctx->builder, val, ptr);
                    cg_mark_last_temp_moved(ctx, temp_mark, "assign: temp owned by deref target");
                }
                cg_flush_temps(ctx, temp_mark, true);
            }
            else if (target_type && target_type->kind == TYPE_STRUCT &&
                     target_type->as.strukt.has_drop)
            {
                /* Drop old struct at *ptr (no moved_flag — heap-allocated, no scope cleanup) */
                emit_struct_drop_cond(ctx, ptr, target_type, NULL);
                /* Clone if RHS is a named struct variable (unwrap __move). */
                if (ast_unwrap_move(node->as.assign.value)->kind == AST_IDENT)
                {
                    LLVMTypeRef llvm_st = type_to_llvm(ctx, target_type);
                    val = emit_struct_clone_val(ctx, val, llvm_st, target_type);
                }
                LLVMBuildStore(ctx->builder, val, ptr);
                ctx->temp_string_count = temp_mark;
            }
            else
            {
                /* Trivial type: just store */
                LLVMBuildStore(ctx->builder, val, ptr);
                ctx->temp_string_count = temp_mark;
            }
        }
        break;
    }

    case AST_RETURN:
    {
        /* Before returning:
           1. If returning a string/struct variable by name, find its alloca.
           2. Emit all scope cleanups, skipping the returned variable's alloca.
           3. Return the value.

           We use LLVMValueRef (the alloca itself) as the skip key rather than
           a CgSymbol* pointer. This is stable across scope array reallocations
           and correctly handles same-name shadowed variables: each alloca is a
           unique LLVM object, so inner-scope "x" and outer-scope "x" have
           distinct alloca values and are never confused. */
        LLVMValueRef return_alloca = NULL;

        if (node->as.return_stmt.value &&
            node->as.return_stmt.value->kind == AST_IDENT &&
            node->as.return_stmt.value->resolved_type)
        {
            Type *ret_type = node->as.return_stmt.value->resolved_type;
            /* For string/struct/vec IDENT returns: ownership transfers to caller —
               skip scope cleanup for this variable so we don't free its data. */
            if (ret_type->kind == TYPE_STRING ||
                ret_type->kind == TYPE_STRUCT ||
                ret_type->kind == TYPE_VECTOR)
            {
                const char *name = node->as.return_stmt.value->as.ident.name;
                CgSymbol *sym = cg_scope_resolve(ctx->current_scope, name);
                if (sym)
                    return_alloca = sym->value;
            }
        }

        if (node->as.return_stmt.value)
        {
            int ret_temp_mark = ctx->temp_string_count;
            LLVMValueRef val = codegen_expr(ctx, node->as.return_stmt.value);
            if (val)
            {
                Type *ret_type = node->as.return_stmt.value->resolved_type;
                /* Flush temp strings from return expression evaluation.
                   For string returns: mark the last temp as moved so its cap
                   becomes -1 (ownership transfers to caller via the SSA value).
                   For non-string returns: flush all temps (the enum constructor
                   already cloned any string args). */
                if (ret_type && ret_type->kind == TYPE_STRING &&
                    ctx->temp_string_count > ret_temp_mark) {
                    cg_mark_last_temp_moved(ctx, ret_temp_mark,
                        "return: ownership transferred to caller");
                }
                cg_flush_temps(ctx, ret_temp_mark, false);
                AstNode *ret_expr = node->as.return_stmt.value;

                /* Implicit numeric widening to the function's declared return
                   type (e.g. `fn f() -> f64 { return 5 }`). The checker has
                   already validated assignability via type_widens_to. */
                if (ctx->current_fn_return_type && ret_type &&
                    type_is_numeric(ret_type) &&
                    type_is_numeric(ctx->current_fn_return_type) &&
                    !type_equals(ret_type, ctx->current_fn_return_type))
                {
                    val = cg_widen(ctx, val, ret_type, ctx->current_fn_return_type);
                    ret_type = ctx->current_fn_return_type;
                }

                if (ret_type && ret_type->kind == TYPE_ARRAY)
                {
                    /* array with has_drop elements: clone before returning so local
                       array's scope-cleanup doesn't free data the caller will use. */
                    Type *elem = ret_type->as.array.elem;
                    bool needs_clone = elem &&
                                       ((elem->kind == TYPE_STRING) ||
                                        (elem->kind == TYPE_STRUCT && elem->as.strukt.has_drop));
                    if (needs_clone)
                    {
                        LLVMTypeRef arr_llvm = type_to_llvm(ctx, ret_type);
                        val = emit_array_clone_val(ctx, val, arr_llvm, ret_type);
                    }
                }
                else if (ret_type && ret_type->kind == TYPE_VECTOR &&
                         ret_expr->kind != AST_IDENT)
                {
                    /* vec return from non-IDENT (e.g. *ptr deref): deep-clone the vec.
                       IDENT return is handled as move (return_alloca skip above). */
                    val = emit_vec_clone_val(ctx, val, ret_type->as.vec.elem);
                }

                emit_cleanup_to(ctx, NULL, return_alloca);
                cg_emit_mc_leave(ctx);   /* D.1: pop frame */
                LLVMBuildRet(ctx->builder, val);
            }
        }
        else
        {
            /* void return: clean up everything */
            emit_cleanup_to(ctx, NULL, NULL);
            /* For user-defined __drop: inject string field cleanup before return */
            emit_drop_field_cleanup(ctx);
            if (LLVMGetBasicBlockTerminator(LLVMGetInsertBlock(ctx->builder)) == NULL)
            {
                cg_emit_mc_leave(ctx);   /* D.1: pop frame */
                LLVMBuildRetVoid(ctx->builder);
            }
        }
        break;
    }

    case AST_IF:
    {
        LLVMValueRef cond = codegen_expr(ctx, node->as.if_stmt.cond);
        if (cond == NULL)
            return;

        LLVMBasicBlockRef then_bb = LLVMAppendBasicBlockInContext(
            ctx->context, ctx->current_fn, "if.then");
        LLVMBasicBlockRef else_bb = NULL;
        LLVMBasicBlockRef merge_bb = LLVMAppendBasicBlockInContext(
            ctx->context, ctx->current_fn, "if.merge");

        if (node->as.if_stmt.else_block)
        {
            else_bb = LLVMAppendBasicBlockInContext(
                ctx->context, ctx->current_fn, "if.else");
            LLVMBuildCondBr(ctx->builder, cond, then_bb, else_bb);
        }
        else
        {
            LLVMBuildCondBr(ctx->builder, cond, then_bb, merge_bb);
        }

        /* then */
        LLVMPositionBuilderAtEnd(ctx->builder, then_bb);
        codegen_stmt(ctx, node->as.if_stmt.then_block);
        if (LLVMGetBasicBlockTerminator(LLVMGetInsertBlock(ctx->builder)) == NULL)
        {
            LLVMBuildBr(ctx->builder, merge_bb);
        }

        /* else */
        if (else_bb)
        {
            LLVMPositionBuilderAtEnd(ctx->builder, else_bb);
            codegen_stmt(ctx, node->as.if_stmt.else_block);
            if (LLVMGetBasicBlockTerminator(LLVMGetInsertBlock(ctx->builder)) == NULL)
            {
                LLVMBuildBr(ctx->builder, merge_bb);
            }
        }

        LLVMPositionBuilderAtEnd(ctx->builder, merge_bb);
        break;
    }

    case AST_WHILE:
    {
        LLVMBasicBlockRef cond_bb = LLVMAppendBasicBlockInContext(
            ctx->context, ctx->current_fn, "while.cond");
        LLVMBasicBlockRef body_bb = LLVMAppendBasicBlockInContext(
            ctx->context, ctx->current_fn, "while.body");
        LLVMBasicBlockRef end_bb = LLVMAppendBasicBlockInContext(
            ctx->context, ctx->current_fn, "while.end");

        LLVMBasicBlockRef saved_break = ctx->break_bb;
        LLVMBasicBlockRef saved_continue = ctx->continue_bb;
        CgScope *saved_loop_scope = ctx->loop_scope;
        ctx->break_bb = end_bb;
        ctx->continue_bb = cond_bb;
        ctx->loop_scope = ctx->current_scope;

        LLVMBuildBr(ctx->builder, cond_bb);

        LLVMPositionBuilderAtEnd(ctx->builder, cond_bb);
        LLVMValueRef cond = codegen_expr(ctx, node->as.while_stmt.cond);
        if (cond)
            LLVMBuildCondBr(ctx->builder, cond, body_bb, end_bb);

        LLVMPositionBuilderAtEnd(ctx->builder, body_bb);
        codegen_stmt(ctx, node->as.while_stmt.body);
        if (LLVMGetBasicBlockTerminator(LLVMGetInsertBlock(ctx->builder)) == NULL)
        {
            LLVMBuildBr(ctx->builder, cond_bb);
        }

        ctx->break_bb = saved_break;
        ctx->continue_bb = saved_continue;
        ctx->loop_scope = saved_loop_scope;

        LLVMPositionBuilderAtEnd(ctx->builder, end_bb);
        break;
    }

    case AST_FOR:
    {
        /* foreach loop: for var in iter { body }
           Supported iterators:
             - Range expression (a..b): iterate var from a to b-1
             - Integer expression (n): iterate var from 0 to n-1
             - Array: iterate over elements
             - Vec(T): iterate over elements (runtime length) */
        push_scope(ctx);

        AstNode *iter_node = node->as.for_stmt.iter;
        Type *iter_type = iter_node->resolved_type;
        bool is_array_iter = (iter_type && iter_type->kind == TYPE_ARRAY);
        bool is_vec_iter = (iter_type && iter_type->kind == TYPE_VECTOR);
        LLVMValueRef start_val = NULL, end_val = NULL;
        LLVMValueRef arr_ptr = NULL;    /* for fixed-array iter: alloca of array */
        LLVMValueRef vec_alloca = NULL; /* for vec iter: alloca of LsVec struct */

        if (is_array_iter)
        {
            /* Array iteration: index from 0..size */
            start_val = LLVMConstInt(LLVMInt32TypeInContext(ctx->context), 0, false);
            end_val = LLVMConstInt(LLVMInt32TypeInContext(ctx->context),
                                   (unsigned long long)iter_type->as.array.size, false);
            /* Get array pointer */
            if (iter_node->kind == AST_IDENT)
            {
                CgSymbol *sym = cg_scope_resolve(ctx->current_scope, iter_node->as.ident.name);
                if (sym)
                    arr_ptr = sym->value;
            }
            if (arr_ptr == NULL)
            {
                pop_scope(ctx);
                break;
            }
        }
        else if (is_vec_iter)
        {
            /* Vec iteration: index from 0..len (runtime) */
            start_val = LLVMConstInt(LLVMInt32TypeInContext(ctx->context), 0, false);
            if (iter_node->kind == AST_IDENT)
            {
                CgSymbol *sym = cg_scope_resolve(ctx->current_scope, iter_node->as.ident.name);
                if (sym)
                    vec_alloca = sym->value;
            }
            if (vec_alloca == NULL)
            {
                pop_scope(ctx);
                break;
            }
            /* end_val = vec.len (load now, before loop — snapshot at loop start) */
            LLVMTypeRef vec_t = ls_vec_type(ctx);
            LLVMValueRef vv = LLVMBuildLoad2(ctx->builder, vec_t, vec_alloca, "fv.v");
            end_val = LLVMBuildExtractValue(ctx->builder, vv, 1, "fv.len");
        }
        else if (iter_node->kind == AST_RANGE)
        {
            /* Range: start..end */
            start_val = codegen_expr(ctx, iter_node->as.range.start);
            end_val = codegen_expr(ctx, iter_node->as.range.end);
        }
        else
        {
            /* Single integer expression: iterate 0..n */
            start_val = LLVMConstInt(LLVMInt32TypeInContext(ctx->context), 0, false);
            end_val = codegen_expr(ctx, iter_node);
        }
        if (start_val == NULL || end_val == NULL)
        {
            pop_scope(ctx);
            break;
        }

        LLVMTypeRef i32_ty = LLVMInt32TypeInContext(ctx->context);
        LLVMBasicBlockRef entry = LLVMGetEntryBasicBlock(ctx->current_fn);

        /* Allocate loop index counter and element variable */
        LLVMValueRef idx_var = NULL;  /* internal index counter for array/vec iter */
        LLVMValueRef loop_var = NULL; /* user-visible loop variable */

        {
            LLVMBuilderRef tmp = LLVMCreateBuilderInContext(ctx->context);
            LLVMValueRef first_inst = LLVMGetFirstInstruction(entry);
            if (first_inst)
                LLVMPositionBuilderBefore(tmp, first_inst);
            else
                LLVMPositionBuilderAtEnd(tmp, entry);

            if (is_array_iter)
            {
                idx_var = LLVMBuildAlloca(tmp, i32_ty, "foreach.idx");
                Type *elem_type = iter_type->as.array.elem;
                LLVMTypeRef elem_llvm = type_to_llvm(ctx, elem_type);
                loop_var = LLVMBuildAlloca(tmp, elem_llvm, node->as.for_stmt.var);
            }
            else if (is_vec_iter)
            {
                idx_var = LLVMBuildAlloca(tmp, i32_ty, "foreach.vidx");
                Type *elem_type = iter_type->as.vec.elem;
                LLVMTypeRef elem_llvm = type_to_llvm(ctx, elem_type);
                loop_var = LLVMBuildAlloca(tmp, elem_llvm, node->as.for_stmt.var);
            }
            else
            {
                loop_var = LLVMBuildAlloca(tmp, i32_ty, node->as.for_stmt.var);
            }
            LLVMDisposeBuilder(tmp);
        }

        if (is_array_iter)
        {
            LLVMBuildStore(ctx->builder, start_val, idx_var);
            {
                CgSymbol *lvsym = cg_scope_define(ctx->current_scope, node->as.for_stmt.var,
                                                  loop_var, iter_type->as.array.elem, NULL);
                /* Array/vec loop variable is a copy of the element — mark borrowed so scope
                   cleanup doesn't drop it (the container still owns the data). */
                if (lvsym)
                    lvsym->is_borrowed = true;
            }
        }
        else if (is_vec_iter)
        {
            LLVMBuildStore(ctx->builder, start_val, idx_var);
            {
                /* vec loop var is defined per-iteration inside body_bb — see below */
                (void)0;
            }
        }
        else
        {
            LLVMBuildStore(ctx->builder, start_val, loop_var);
            cg_scope_define(ctx->current_scope, node->as.for_stmt.var, loop_var, type_int(), NULL);
        }

        /* Create basic blocks */
        LLVMBasicBlockRef cond_bb = LLVMAppendBasicBlockInContext(
            ctx->context, ctx->current_fn, "foreach.cond");
        LLVMBasicBlockRef body_bb = LLVMAppendBasicBlockInContext(
            ctx->context, ctx->current_fn, "foreach.body");
        LLVMBasicBlockRef update_bb = LLVMAppendBasicBlockInContext(
            ctx->context, ctx->current_fn, "foreach.update");
        LLVMBasicBlockRef end_bb = LLVMAppendBasicBlockInContext(
            ctx->context, ctx->current_fn, "foreach.end");

        /* Save and set break/continue targets */
        LLVMBasicBlockRef saved_break = ctx->break_bb;
        LLVMBasicBlockRef saved_continue = ctx->continue_bb;
        CgScope *saved_loop_scope = ctx->loop_scope;
        ctx->break_bb = end_bb;
        ctx->continue_bb = update_bb;
        ctx->loop_scope = ctx->current_scope;

        /* Branch to condition */
        LLVMBuildBr(ctx->builder, cond_bb);

        /* Condition: idx < end */
        LLVMPositionBuilderAtEnd(ctx->builder, cond_bb);
        LLVMValueRef cmp_var = (is_array_iter || is_vec_iter) ? idx_var : loop_var;
        LLVMValueRef cur = LLVMBuildLoad2(ctx->builder, i32_ty, cmp_var, "cur");
        LLVMValueRef cond = LLVMBuildICmp(ctx->builder, LLVMIntSLT, cur, end_val, "foreach.lt");
        LLVMBuildCondBr(ctx->builder, cond, body_bb, end_bb);

        /* Body */
        LLVMPositionBuilderAtEnd(ctx->builder, body_bb);

        if (is_array_iter)
        {
            /* Load current element into loop variable: loop_var = arr[idx] */
            LLVMValueRef cur_idx = LLVMBuildLoad2(ctx->builder, i32_ty, idx_var, "arr.i");
            LLVMTypeRef i64_type = LLVMInt64TypeInContext(ctx->context);
            LLVMValueRef idx64 = LLVMBuildSExtOrBitCast(ctx->builder, cur_idx, i64_type, "idx64");
            LLVMTypeRef arr_llvm = type_to_llvm(ctx, iter_type);
            LLVMValueRef zero64 = LLVMConstInt(i64_type, 0, 0);
            LLVMValueRef indices[2] = {zero64, idx64};
            LLVMValueRef gep = LLVMBuildGEP2(ctx->builder, arr_llvm, arr_ptr,
                                             indices, 2, "foreach.gep");
            LLVMTypeRef elem_llvm = type_to_llvm(ctx, iter_type->as.array.elem);
            LLVMValueRef elem_val = LLVMBuildLoad2(ctx->builder, elem_llvm, gep, "foreach.elem");
            LLVMBuildStore(ctx->builder, elem_val, loop_var);
        }
        else if (is_vec_iter)
        {
            /* Per-iteration scope for the loop variable x.
               Lifetime model:
               - On entry: push scope, load element.
                   string: zero cap field so emit_string_free is a no-op at runtime
                           (the vec still owns the original data).
                   struct-with-drop: create a borrowed_flag (i1=1) used as moved_flag,
                           so emit_struct_drop_cond skips the initial borrowed data.
               - If x is assigned inside the body: emit_string_free / emit_struct_drop_cond
                   sees cap=0 / moved_flag=1 → no-op; then stores new owned value and
                   clears moved_flag to 0 (existing assignment path handles this).
               - On exit: scope cleanup calls emit_string_free / emit_struct_drop_cond.
                   If never assigned: cap=0 or moved_flag=1 → skip (correct, vec owns data).
                   If assigned: cap>0 or moved_flag=0 → free/drop the owned copy (correct).
               break/continue: emit_cleanup_to(loop_scope) cleans this scope too.      */
            push_scope(ctx);

            LLVMValueRef cur_idx = LLVMBuildLoad2(ctx->builder, i32_ty, idx_var, "fv.i");
            LLVMTypeRef i64_type = LLVMInt64TypeInContext(ctx->context);
            LLVMValueRef idx64 = LLVMBuildSExtOrBitCast(ctx->builder, cur_idx, i64_type, "fv.idx64");
            LLVMTypeRef vec_t = ls_vec_type(ctx);
            LLVMTypeRef elem_llvm = type_to_llvm(ctx, iter_type->as.vec.elem);
            LLVMValueRef vv2 = LLVMBuildLoad2(ctx->builder, vec_t, vec_alloca, "fv.v2");
            LLVMValueRef data_ptr = LLVMBuildExtractValue(ctx->builder, vv2, 0, "fv.data");
            LLVMValueRef gep = LLVMBuildGEP2(ctx->builder, elem_llvm, data_ptr,
                                             &idx64, 1, "fv.gep");
            LLVMValueRef elem_val = LLVMBuildLoad2(ctx->builder, elem_llvm, gep, "fv.elem");
            Type *elem_type = iter_type->as.vec.elem;

            LLVMValueRef loop_var_moved_flag = NULL;

            if (elem_type->kind == TYPE_STRING)
            {
                /* Zero out cap (field 2) — marks this copy as non-owning at runtime.
                   emit_string_free checks cap > 0, so it naturally skips borrowed data. */
                LLVMValueRef zero32 = LLVMConstInt(LLVMInt32TypeInContext(ctx->context), 0, 0);
                elem_val = LLVMBuildInsertValue(ctx->builder, elem_val, zero32, 2, "fv.borrow");
            }
            else if (elem_type->kind == TYPE_STRUCT && elem_type->as.strukt.has_drop)
            {
                /* Create a borrowed_flag alloca (i1), initialised to 1.
                   emit_struct_drop_cond uses this as moved_flag: 1 = skip drop.
                   The assignment path clears it to 0 when x is first written,
                   so subsequent cleanup correctly drops the owned copy. */
                LLVMTypeRef i1_ty = LLVMInt1TypeInContext(ctx->context);
                loop_var_moved_flag = LLVMBuildAlloca(ctx->builder, i1_ty, "fv.borrowed");
                LLVMBuildStore(ctx->builder, LLVMConstInt(i1_ty, 1, 0), loop_var_moved_flag);
            }

            LLVMBuildStore(ctx->builder, elem_val, loop_var);

            /* Define x in the per-iteration scope (no is_borrowed flag needed —
               runtime cap / moved_flag carries the ownership state). */
            cg_scope_define(ctx->current_scope, node->as.for_stmt.var,
                            loop_var, elem_type, loop_var_moved_flag);
        }

        codegen_stmt(ctx, node->as.for_stmt.body);

        /* Per-iteration scope teardown (vec only).
           emit_scope_cleanup handles x: frees/drops if it owns data, skips if not.
           Only emit if the block is not already terminated (break/continue already
           called emit_cleanup_to which covers this scope). */
        if (is_vec_iter)
        {
            if (LLVMGetBasicBlockTerminator(LLVMGetInsertBlock(ctx->builder)) == NULL)
            {
                emit_scope_cleanup(ctx);
                LLVMBuildBr(ctx->builder, update_bb);
            }
            pop_scope(ctx);
        }
        else if (LLVMGetBasicBlockTerminator(LLVMGetInsertBlock(ctx->builder)) == NULL)
        {
            LLVMBuildBr(ctx->builder, update_bb);
        }

        /* Update: idx/var = idx/var + 1 */
        LLVMPositionBuilderAtEnd(ctx->builder, update_bb);
        LLVMValueRef inc_var = (is_array_iter || is_vec_iter) ? idx_var : loop_var;
        LLVMValueRef cur2 = LLVMBuildLoad2(ctx->builder, i32_ty, inc_var, "cur2");
        LLVMValueRef next = LLVMBuildAdd(ctx->builder, cur2,
                                         LLVMConstInt(i32_ty, 1, false), "next");
        LLVMBuildStore(ctx->builder, next, inc_var);
        LLVMBuildBr(ctx->builder, cond_bb);

        /* Restore break/continue/loop_scope */
        ctx->break_bb = saved_break;
        ctx->continue_bb = saved_continue;
        ctx->loop_scope = saved_loop_scope;

        pop_scope(ctx);
        LLVMPositionBuilderAtEnd(ctx->builder, end_bb);
        break;
    }

    case AST_FOR_C:
    {
        /* C-style for: for (init; cond; update) { body }
           Structure: init → cond_bb → body_bb → update_bb → cond_bb
                                    ↘ end_bb                         */
        push_scope(ctx);

        /* Emit init clause (if any) */
        if (node->as.for_c_stmt.init)
        {
            codegen_stmt(ctx, node->as.for_c_stmt.init);
        }

        LLVMBasicBlockRef cond_bb = LLVMAppendBasicBlockInContext(
            ctx->context, ctx->current_fn, "for.cond");
        LLVMBasicBlockRef body_bb = LLVMAppendBasicBlockInContext(
            ctx->context, ctx->current_fn, "for.body");
        LLVMBasicBlockRef update_bb = LLVMAppendBasicBlockInContext(
            ctx->context, ctx->current_fn, "for.update");
        LLVMBasicBlockRef end_bb = LLVMAppendBasicBlockInContext(
            ctx->context, ctx->current_fn, "for.end");

        /* Save and set break/continue targets */
        LLVMBasicBlockRef saved_break = ctx->break_bb;
        LLVMBasicBlockRef saved_continue = ctx->continue_bb;
        CgScope *saved_loop_scope = ctx->loop_scope;
        ctx->break_bb = end_bb;
        ctx->continue_bb = update_bb; /* continue jumps to update, not cond */
        ctx->loop_scope = ctx->current_scope;

        /* Branch to condition check */
        LLVMBuildBr(ctx->builder, cond_bb);

        /* Condition block */
        LLVMPositionBuilderAtEnd(ctx->builder, cond_bb);
        if (node->as.for_c_stmt.cond)
        {
            LLVMValueRef cond = codegen_expr(ctx, node->as.for_c_stmt.cond);
            if (cond)
                LLVMBuildCondBr(ctx->builder, cond, body_bb, end_bb);
        }
        else
        {
            /* No condition → infinite loop (like for(;;)) */
            LLVMBuildBr(ctx->builder, body_bb);
        }

        /* Body block */
        LLVMPositionBuilderAtEnd(ctx->builder, body_bb);
        codegen_stmt(ctx, node->as.for_c_stmt.body);
        if (LLVMGetBasicBlockTerminator(LLVMGetInsertBlock(ctx->builder)) == NULL)
        {
            LLVMBuildBr(ctx->builder, update_bb);
        }

        /* Update block */
        LLVMPositionBuilderAtEnd(ctx->builder, update_bb);
        if (node->as.for_c_stmt.update)
        {
            codegen_stmt(ctx, node->as.for_c_stmt.update);
        }
        if (LLVMGetBasicBlockTerminator(LLVMGetInsertBlock(ctx->builder)) == NULL)
        {
            LLVMBuildBr(ctx->builder, cond_bb);
        }

        /* Restore break/continue/loop_scope */
        ctx->break_bb = saved_break;
        ctx->continue_bb = saved_continue;
        ctx->loop_scope = saved_loop_scope;

        pop_scope(ctx);
        LLVMPositionBuilderAtEnd(ctx->builder, end_bb);
        break;
    }

    case AST_BLOCK:
    {
        push_scope(ctx);
        CgScope *block_parent = ctx->current_scope->parent;
        for (int i = 0; i < node->as.block.stmt_count; i++)
        {
            codegen_stmt(ctx, node->as.block.stmts[i]);
            /* Stop generating if we've hit a terminator */
            if (LLVMGetBasicBlockTerminator(LLVMGetInsertBlock(ctx->builder)) != NULL)
                break;
        }
        /* Only clean up variables declared in THIS block, not outer scopes */
        emit_cleanup_to(ctx, block_parent, NULL);
        pop_scope(ctx);
        break;
    }

    case AST_EXPR_STMT:
    {
        /* Evaluate the expression, then free any temporary strings it produced.
           The unified temp slot mechanism handles all dynamic strings (method calls,
           concatenation, f-strings) — no need for the old expr_produces_dynamic_string
           heuristic. */
        int temp_mark = ctx->temp_string_count;
        codegen_expr(ctx, node->as.expr_stmt.expr);
        /* Free all temps produced (none are moved/kept — this is a discarded result) */
        cg_flush_temps(ctx, temp_mark, false);
        break;
    }

    case AST_BREAK:
        if (ctx->break_bb)
        {
            /* Free string locals from current scope up to (not including) loop scope */
            emit_cleanup_to(ctx, ctx->loop_scope, NULL);
            LLVMBuildBr(ctx->builder, ctx->break_bb);
        }
        break;

    case AST_CONTINUE:
        if (ctx->continue_bb)
        {
            /* Free string locals from current scope up to (not including) loop scope */
            emit_cleanup_to(ctx, ctx->loop_scope, NULL);
            LLVMBuildBr(ctx->builder, ctx->continue_bb);
        }
        break;

    default:
        codegen_decl(ctx, node);
        break;
    }
}

/* ---- Declaration codegen ---- */

/* Helper: Inject string field cleanup for user-defined __drop functions.
   This is called before any return (implicit or explicit) in a __drop method. */
static void emit_drop_field_cleanup(CodegenContext *ctx)
{
    /* Check if current function is a __drop method.
       With wrapper model, user body is "StructName.__drop$" (internal, $ can't appear in
       LS identifiers so users can't conflict with it). Skip that — cleanup is handled by
       the wrapper "StructName.__drop". Only match exact ".__drop" suffix. */
    LLVMValueRef fn = ctx->current_fn;
    if (fn == NULL)
        return;
    const char *fn_name = LLVMGetValueName(fn);
    if (fn_name == NULL)
        return;
    size_t fn_len = strlen(fn_name);
    /* Must end with exactly ".__drop" — exclude internal body ".__drop$" */
    if (fn_len < 7 || strcmp(fn_name + fn_len - 7, ".__drop") != 0)
        return;

    /* Find the struct type. fn_name is "StructName.__drop" */
    char struct_name[128];
    const char *dot = strchr(fn_name, '.');
    if (dot == NULL)
        return;
    size_t len = (size_t)(dot - fn_name);
    if (len >= sizeof(struct_name))
        len = sizeof(struct_name) - 1;
    strncpy(struct_name, fn_name, len);
    struct_name[len] = '\0';

    Type *st = find_struct_ls_type(ctx, struct_name);
    if (st == NULL || st->kind != TYPE_STRUCT)
        return;

    /* Inject field cleanup */
    LLVMTypeRef llvm_struct = type_to_llvm(ctx, st);
    LLVMValueRef self_ptr = LLVMGetParam(fn, 0);
    for (int i = 0; i < st->as.strukt.field_count; i++)
    {
        Type *ft = st->as.strukt.fields[i].type;
        if (ft && ft->kind == TYPE_STRING)
        {
            LLVMValueRef fptr = LLVMBuildStructGEP2(ctx->builder, llvm_struct, self_ptr, (unsigned)i, "drop.field");
            emit_string_free(ctx, fptr);
        }
        /* Handle nested struct fields with drop functions */
        if (ft && ft->kind == TYPE_STRUCT && ft->as.strukt.has_drop)
        {
            LLVMValueRef field_ptr = LLVMBuildStructGEP2(ctx->builder, llvm_struct, self_ptr, (unsigned)i, "drop.nested");
            LLVMValueRef member_drop_fn = (LLVMValueRef)ft->as.strukt.drop_fn;
            if (member_drop_fn == NULL)
            {
                /* Recursively generate for member first */
                emit_auto_drop_fn(ctx, ft);
                member_drop_fn = (LLVMValueRef)ft->as.strukt.drop_fn;
            }
            if (member_drop_fn != NULL)
            {
                LLVMTypeRef fn_type = LLVMGlobalGetValueType(member_drop_fn);
                LLVMBuildCall2(ctx->builder, fn_type, member_drop_fn, &field_ptr, 1, "");
            }
        }
    }
}

static void codegen_fn_decl(CodegenContext *ctx, AstNode *node)
{
    const char *name = node->as.fn_decl.name;
    int user_n = node->as.fn_decl.param_count;
    bool is_instance_method = (node->as.fn_decl.impl_struct_name != NULL && !node->as.fn_decl.is_static);

    Type *fn_type_ml = node->resolved_type;
    if (fn_type_ml == NULL || fn_type_ml->kind != TYPE_FUNCTION)
        return;

    /* Each function compiles in isolation: reset temp string slots so that
       temps from a previous function don't bleed into this one. */
    int saved_temp_count = ctx->temp_string_count;
    ctx->temp_string_count = 0;

    int total_n = fn_type_ml->as.function.param_count;
    LLVMTypeRef fn_type = type_to_llvm(ctx, fn_type_ml);

    /* Check for existing function (forward decl) */
    LLVMValueRef fn = LLVMGetNamedFunction(ctx->module, name);
    if (fn == NULL)
    {
        fn = LLVMAddFunction(ctx->module, name, fn_type);
    }

    /* If function has no body (extern), skip */
    if (node->as.fn_decl.body == NULL)
        return;

    LLVMBasicBlockRef entry = LLVMAppendBasicBlockInContext(ctx->context, fn, "entry");
    LLVMPositionBuilderAtEnd(ctx->builder, entry);

    /* D.1 — push a frame for this user function so allocations made within
       its dynamic extent capture this function in their backtrace. We pull
       the file from the module identifier (same pattern as cg_make_site).
       Synthesized helpers (drop bodies, __ls_global_init, runtime intrinsics)
       take a different codegen path and are correctly skipped. */
    {
        const char *fn_file = "?";
        size_t mn_len = 0;
        const char *mn = LLVMGetModuleIdentifier(ctx->module, &mn_len);
        if (mn && mn_len > 0) fn_file = mn;
        cg_emit_mc_enter(ctx, name, fn_file, node->line);
    }

    LLVMValueRef saved_fn = ctx->current_fn;
    Type *saved_fn_ret = ctx->current_fn_return_type;
    ctx->current_fn = fn;
    ctx->current_fn_return_type = fn_type_ml ? fn_type_ml->as.function.return_type : NULL;

    push_scope(ctx);

    /* For instance methods, param[0] is the implicit self pointer (LLVM-level
       always pointer; semantics depend on self_borrow_kind):
         - sbk=0 (legacy): self is *Struct (alloca-of-pointer; loads to deref)
         - sbk=1/2 (&self/&!self): self is Struct borrow — sym->value IS the
           caller's struct pointer (no alloca, no copy). */
    int param_offset = 0;
    if (is_instance_method)
    {
        int sbk = node->as.fn_decl.self_borrow_kind;
        if (sbk == 0)
        {
            Type *self_type = fn_type_ml->as.function.params[0]; /* *Struct */
            LLVMTypeRef self_llvm = type_to_llvm(ctx, self_type);
            LLVMValueRef self_alloca = LLVMBuildAlloca(ctx->builder, self_llvm, "self");
            LLVMBuildStore(ctx->builder, LLVMGetParam(fn, 0), self_alloca);
            cg_scope_define(ctx->current_scope, "self", self_alloca, self_type, NULL);
        }
        else
        {
            /* Body sees self as TYPE_STRUCT borrow. */
            Type *self_struct_type =
                fn_type_ml->as.function.params[0]->as.pointer_to;
            LLVMValueRef ptr = LLVMGetParam(fn, 0);
            CgSymbol *psym = cg_scope_define(ctx->current_scope, "self",
                                             ptr, self_struct_type, NULL);
            if (psym)
            {
                psym->is_borrowed = true; /* skip scope cleanup */
                if (sbk == 2) psym->is_mut_borrow = true;
            }
        }
        param_offset = 1;
    }

    /* Alloca for each user-declared parameter */
    for (int i = 0; i < user_n; i++)
    {
        int llvm_idx = i + param_offset;
        Type *param_type = fn_type_ml->as.function.params[llvm_idx];
        /* Phase 5.5: &!T (writable borrow) uses true pointer ABI — the LLVM
           parameter is an LsString* supplied by the caller. We register the
           symbol with value = that pointer directly (no alloca, no copy) so
           that all loads/stores go through the caller's slot. */
        bool is_mut_borrow_param = false;
        if (param_type && param_type->kind == TYPE_REFERENCE && param_type->is_mut)
        {
            param_type = param_type->as.pointer_to;
            LLVMValueRef ptr = LLVMGetParam(fn, (unsigned)llvm_idx);
            CgSymbol *psym = cg_scope_define(ctx->current_scope,
                                             node->as.fn_decl.param_names[i],
                                             ptr, param_type, NULL);
            if (psym)
            {
                psym->is_mut_borrow = true;
                psym->is_borrowed = true; /* skip scope cleanup */
            }
            is_mut_borrow_param = true;
            (void)is_mut_borrow_param;
            continue;
        }
        /* Phase 5.6/5.7: &vec(T) / &map(K,V) (read-only) use pointer ABI like
           the writable variants. Register sym->value as a raw pointer to the
           underlying LsVec or LsMap struct — every vec/map codegen path
           already treats sym->value as a pointer to that struct. The checker statically forbids any mutating
           method on this symbol, so writes never happen through this pointer. */
        if (param_type && param_type->kind == TYPE_REFERENCE &&
            !param_type->is_mut &&
            param_type->as.pointer_to &&
            (param_type->as.pointer_to->kind == TYPE_VECTOR ||
             param_type->as.pointer_to->kind == TYPE_MAP ||
             param_type->as.pointer_to->kind == TYPE_STRUCT))
        {
            param_type = param_type->as.pointer_to;
            LLVMValueRef ptr = LLVMGetParam(fn, (unsigned)llvm_idx);
            CgSymbol *psym = cg_scope_define(ctx->current_scope,
                                             node->as.fn_decl.param_names[i],
                                             ptr, param_type, NULL);
            if (psym)
            {
                /* No is_mut_borrow flag — mutations are blocked by the checker.
                   is_borrowed skips scope cleanup so caller retains ownership. */
                psym->is_borrowed = true;
            }
            continue;
        }
        /* Phase 5: unwrap &T → T (read-only borrow keeps by-value ABI for string). */
        if (param_type && param_type->kind == TYPE_REFERENCE)
            param_type = param_type->as.pointer_to;
        LLVMTypeRef param_llvm = type_to_llvm(ctx, param_type);
        LLVMValueRef alloca = LLVMBuildAlloca(ctx->builder, param_llvm,
                                              node->as.fn_decl.param_names[i]);
        LLVMBuildStore(ctx->builder, LLVMGetParam(fn, (unsigned)llvm_idx), alloca);
        /* Allocate moved_flag for struct-with-drop parameters */
        LLVMValueRef moved_flag = NULL;
        if (param_type && param_type->kind == TYPE_STRUCT && param_type->as.strukt.has_drop)
        {
            LLVMTypeRef i1_type = LLVMInt1TypeInContext(ctx->context);
            moved_flag = LLVMBuildAlloca(ctx->builder, i1_type, "param.moved");
            LLVMBuildStore(ctx->builder, LLVMConstInt(i1_type, 0, 0), moved_flag);
        }
        /* String parameters: zero out cap so emit_string_free treats them as
           borrowed (cap=0 → skip). The caller owns the original data buffer.
           Reassignment inside the body replaces cap with the new owned value,
           which will be freed at scope cleanup — no double-free, no leak. */
        if (param_type && param_type->kind == TYPE_STRING)
        {
            LLVMTypeRef str_type = ls_string_type(ctx);
            LLVMValueRef str_val = LLVMBuildLoad2(ctx->builder, str_type, alloca, "param.str");
            LLVMValueRef zero32 = LLVMConstInt(LLVMInt32TypeInContext(ctx->context), 0, 0);
            str_val = LLVMBuildInsertValue(ctx->builder, str_val, zero32, 2, "param.borrow");
            LLVMBuildStore(ctx->builder, str_val, alloca);
        }
        CgSymbol *psym = cg_scope_define(ctx->current_scope, node->as.fn_decl.param_names[i], alloca, param_type, moved_flag);
        /* vec parameters are borrowed: the caller owns the data buffer, so don't free on return */
        if (psym && param_type && param_type->kind == TYPE_VECTOR)
            psym->is_borrowed = true;
        /* Self-recursive enum parameters (e.g. Tree in `fn sum_tree(Tree t)`)
           share heap-boxed sub-nodes with the caller's value. Marking the
           param as borrowed prevents the callee's scope cleanup from
           recursively freeing boxes the caller still owns — fixes Bug 5
           (sum_tree double-free) for Phase B. Move tracking is a future
           refinement. */
        if (psym && param_type && param_type->kind == TYPE_ENUM &&
            param_type->as.enom.has_drop)
        {
            for (int v = 0; v < param_type->as.enom.variant_count; v++)
            {
                int pc = param_type->as.enom.variants[v].payload_count;
                for (int j = 0; j < pc; j++)
                {
                    if (param_type->as.enom.variants[v].payload_types[j] == param_type)
                    {
                        psym->is_borrowed = true;
                        break;
                    }
                }
                if (psym->is_borrowed) break;
            }
        }
    }
    (void)total_n;

    /* Compile body — handle implicit return of last expression */
    bool is_non_void = fn_type_ml->as.function.return_type->kind != TYPE_VOID;
    AstNode *body = node->as.fn_decl.body;

    if (is_non_void && body->kind == AST_BLOCK && body->as.block.stmt_count > 0)
    {
        /* Inline the block to intercept the last statement */
        push_scope(ctx);
        int last_idx = body->as.block.stmt_count - 1;
        for (int si = 0; si < last_idx; si++)
        {
            codegen_stmt(ctx, body->as.block.stmts[si]);
            if (LLVMGetBasicBlockTerminator(LLVMGetInsertBlock(ctx->builder)) != NULL)
                break;
        }
        /* Last statement: if it's an expression stmt, return its value */
        AstNode *last = body->as.block.stmts[last_idx];
        if (last->kind == AST_EXPR_STMT &&
            LLVMGetBasicBlockTerminator(LLVMGetInsertBlock(ctx->builder)) == NULL)
        {
            LLVMValueRef val = codegen_expr(ctx, last->as.expr_stmt.expr);
            if (val && LLVMGetBasicBlockTerminator(LLVMGetInsertBlock(ctx->builder)) == NULL)
            {
                /* Emit cleanup BEFORE return */
                emit_cleanup_to(ctx, NULL, NULL);
                cg_emit_mc_leave(ctx);   /* D.1: pop frame */
                LLVMBuildRet(ctx->builder, val);
            }
        }
        else
        {
            /* Guard: a nested block (e.g. an inner { return x }) may have already
               inserted a terminator.  Only process the last statement if the current
               basic block is still open; otherwise the last statement is unreachable
               and generating IR for it would produce "terminator in the middle of a
               basic block" errors. */
            if (LLVMGetBasicBlockTerminator(LLVMGetInsertBlock(ctx->builder)) == NULL)
            {
                codegen_stmt(ctx, last);
            }
            /* Emit cleanup before implicit return if no terminator was added */
            if (LLVMGetBasicBlockTerminator(LLVMGetInsertBlock(ctx->builder)) == NULL)
            {
                emit_cleanup_to(ctx, NULL, NULL);
            }
        }
        pop_scope(ctx);
    }
    else
    {
        codegen_stmt(ctx, body);
    }

    /* Ensure function has a terminator — clean up remaining string locals first */
    LLVMBasicBlockRef current_bb = LLVMGetInsertBlock(ctx->builder);
    if (LLVMGetBasicBlockTerminator(current_bb) == NULL)
    {
        emit_cleanup_to(ctx, NULL, NULL); /* clean param-scope strings before implicit ret */

        /* If this is a user-defined __drop, inject string field cleanup now.
           emit_drop_field_cleanup() may emit conditional branches (via emit_string_free),
           moving the builder to a new continuation block. We must emit the ret
           at the current builder position AFTER the field cleanup, not at the
           saved original block (which would be already terminated). */
        emit_drop_field_cleanup(ctx);

        /* Emit terminator at current builder position (after all cleanups and field frees).
           Guard with a terminator check in case emit_drop_field_cleanup somehow
           already terminated the block. */
        if (LLVMGetBasicBlockTerminator(LLVMGetInsertBlock(ctx->builder)) == NULL)
        {
            cg_emit_mc_leave(ctx);   /* D.1: pop frame for implicit ret */
            if (!is_non_void)
            {
                LLVMBuildRetVoid(ctx->builder);
            }
            else
            {
                /* Return zero/null default to avoid LLVM verification error */
                LLVMTypeRef ret_llvm = type_to_llvm(ctx, fn_type_ml->as.function.return_type);
                LLVMBuildRet(ctx->builder, LLVMConstNull(ret_llvm));
            }
        }
    }

    pop_scope(ctx);
    ctx->current_fn = saved_fn;
    ctx->current_fn_return_type = saved_fn_ret;

    /* Restore temp string count to what it was before compiling this function */
    ctx->temp_string_count = saved_temp_count;

    /* Verify function */
    if (LLVMVerifyFunction(fn, LLVMPrintMessageAction))
    {
        fprintf(stderr, "[codegen] warning: function '%s' failed verification\n", name);
    }
}

/* Build the LLVM struct type for a single variant's payload. Self-recursive
   payload fields (where the field type is the enclosing enum) are stored as
   opaque pointers (boxed-on-heap), avoiding infinite type recursion. */
static LLVMTypeRef build_variant_payload_struct(CodegenContext *ctx, Type *enum_type, int variant_idx)
{
    int pc = enum_type->as.enom.variants[variant_idx].payload_count;
    if (pc == 0)
        return LLVMStructTypeInContext(ctx->context, NULL, 0, 0);

    LLVMTypeRef ptr_type = LLVMPointerTypeInContext(ctx->context, 0);
    LLVMTypeRef *fields = (LLVMTypeRef *)malloc_safe((size_t)pc * sizeof(LLVMTypeRef));
    for (int i = 0; i < pc; i++)
    {
        Type *pt = enum_type->as.enom.variants[variant_idx].payload_types[i];
        if (pt == enum_type)
            fields[i] = ptr_type;       /* self-recursive → boxed */
        else
            fields[i] = type_to_llvm(ctx, pt);
    }
    LLVMTypeRef ty = LLVMStructTypeInContext(ctx->context, fields, (unsigned)pc, 0);
    free(fields);
    return ty;
}

static void codegen_enum_decl(CodegenContext *ctx, AstNode *node)
{
    const char *name = node->as.enum_decl.name;
    Type *et = node->resolved_type;
    if (et == NULL || et->kind != TYPE_ENUM) return;
    if (find_enum_llvm(ctx, name)) return;  /* already registered */

    LLVMTypeRef i8 = LLVMInt8TypeInContext(ctx->context);

    /* Compute max payload size across all variants */
    LLVMTargetDataRef td = LLVMGetModuleDataLayout(ctx->module);
    int max_payload = 0;
    for (int v = 0; v < et->as.enom.variant_count; v++)
    {
        if (et->as.enom.variants[v].payload_count == 0) continue;
        LLVMTypeRef vstruct = build_variant_payload_struct(ctx, et, v);
        unsigned long long sz = LLVMABISizeOfType(td, vstruct);
        if ((int)sz > max_payload) max_payload = (int)sz;
    }

    /* Build {i8 disc, [N x i8] payload} as a named opaque struct */
    LLVMTypeRef payload = LLVMArrayType(i8, (unsigned)max_payload);
    LLVMTypeRef body[2] = { i8, payload };
    LLVMTypeRef llvm_type = LLVMStructCreateNamed(ctx->context, name);
    LLVMStructSetBody(llvm_type, body, 2, 0);

    register_enum_llvm(ctx, name, llvm_type, et, max_payload);

    /* If the enum owns heap memory, generate its drop function. */
    if (et->as.enom.has_drop)
        emit_auto_enum_drop_fn(ctx, et);
}

/* True if a payload type contributes heap ownership to the enum. */
static bool cg_type_owns_heap_for_enum(const Type *t)
{
    if (t == NULL) return false;
    switch (t->kind)
    {
    case TYPE_STRING: return true;
    case TYPE_VECTOR: return true;
    case TYPE_MAP:    return true;
    case TYPE_STRUCT: return t->as.strukt.has_drop;
    case TYPE_ENUM:   return t->as.enom.has_drop;
    default:          return false;
    }
}

/* Generate `EnumName.__drop(EnumName *self)` for has_drop enums:
   switch on disc → for each variant case → free each owned payload field.
   Self-recursive payload is heap-boxed; we drop the pointee then free the box. */
static void emit_auto_enum_drop_fn(CodegenContext *ctx, Type *enum_type)
{
    if (enum_type == NULL || enum_type->kind != TYPE_ENUM) return;
    if (!enum_type->as.enom.has_drop) return;
    if (enum_type->as.enom.drop_fn != NULL) return;

    const char *enum_name = enum_type->as.enom.name;
    char drop_fn_name[256];
    snprintf(drop_fn_name, sizeof(drop_fn_name), "%s.__drop", enum_name);
    if (LLVMGetNamedFunction(ctx->module, drop_fn_name) != NULL) return;

    LLVMBasicBlockRef saved_bb = LLVMGetInsertBlock(ctx->builder);

    LLVMTypeRef enum_llvm = type_to_llvm(ctx, enum_type);
    LLVMTypeRef ptr_enum = LLVMPointerType(enum_llvm, 0);
    LLVMTypeRef i8 = LLVMInt8TypeInContext(ctx->context);
    LLVMTypeRef ptr_type = LLVMPointerTypeInContext(ctx->context, 0);
    LLVMTypeRef fn_type = LLVMFunctionType(LLVMVoidTypeInContext(ctx->context),
                                            &ptr_enum, 1, 0);

    LLVMValueRef drop_fn = LLVMAddFunction(ctx->module, drop_fn_name, fn_type);
    LLVMSetFunctionCallConv(drop_fn, LLVMCCallConv);

    /* Pre-register so recursive variants find it during emission. */
    enum_type->as.enom.drop_fn = drop_fn;

    LLVMBasicBlockRef entry = LLVMAppendBasicBlockInContext(ctx->context, drop_fn, "entry");
    LLVMPositionBuilderAtEnd(ctx->builder, entry);

    LLVMValueRef self_ptr = LLVMGetParam(drop_fn, 0);
    LLVMSetValueName(self_ptr, "self");

    LLVMValueRef disc_ptr = LLVMBuildStructGEP2(ctx->builder, enum_llvm, self_ptr, 0, "disc.p");
    LLVMValueRef disc = LLVMBuildLoad2(ctx->builder, i8, disc_ptr, "disc");
    LLVMValueRef payload_ptr = LLVMBuildStructGEP2(ctx->builder, enum_llvm, self_ptr, 1, "payload.p");

    LLVMBasicBlockRef end_bb = LLVMAppendBasicBlockInContext(ctx->context, drop_fn, "drop.end");

    /* Count variants that need cleanup */
    int needs_count = 0;
    for (int v = 0; v < enum_type->as.enom.variant_count; v++)
    {
        for (int i = 0; i < enum_type->as.enom.variants[v].payload_count; i++)
        {
            Type *pt = enum_type->as.enom.variants[v].payload_types[i];
            if (pt == enum_type || cg_type_owns_heap_for_enum(pt)) { needs_count++; break; }
        }
    }

    LLVMValueRef sw = LLVMBuildSwitch(ctx->builder, disc, end_bb, (unsigned)needs_count);

    for (int v = 0; v < enum_type->as.enom.variant_count; v++)
    {
        bool needs = false;
        for (int i = 0; i < enum_type->as.enom.variants[v].payload_count; i++)
        {
            Type *pt = enum_type->as.enom.variants[v].payload_types[i];
            if (pt == enum_type || cg_type_owns_heap_for_enum(pt)) { needs = true; break; }
        }
        if (!needs) continue;

        LLVMBasicBlockRef case_bb = LLVMAppendBasicBlockInContext(ctx->context, drop_fn, "drop.case");
        LLVMAddCase(sw, LLVMConstInt(i8, (unsigned long long)v, 0), case_bb);
        LLVMPositionBuilderAtEnd(ctx->builder, case_bb);

        LLVMTypeRef variant_struct = build_variant_payload_struct(ctx, enum_type, v);
        for (int i = 0; i < enum_type->as.enom.variants[v].payload_count; i++)
        {
            Type *pt = enum_type->as.enom.variants[v].payload_types[i];
            LLVMValueRef field_ptr = LLVMBuildStructGEP2(ctx->builder, variant_struct,
                                                         payload_ptr, (unsigned)i, "drop.field");

            if (pt == enum_type)
            {
                /* Self-recursive: load box ptr, recursively drop, then free box. */
                LLVMValueRef box = LLVMBuildLoad2(ctx->builder, ptr_type, field_ptr, "box");
                LLVMBuildCall2(ctx->builder, fn_type, drop_fn, &box, 1, "");
                cg_emit_free(ctx, box, "enum.scope_drop", CG_LINE(ctx), CG_COL(ctx));
            }
            else if (pt && pt->kind == TYPE_STRING)
            {
                emit_string_free(ctx, field_ptr);
            }
            else if (pt && pt->kind == TYPE_STRUCT && pt->as.strukt.has_drop)
            {
                emit_struct_drop(ctx, field_ptr, pt);
            }
            /* TODO: vec / map / nested-enum cases (8b minimal: string + struct + self-recursive) */
        }

        LLVMBuildBr(ctx->builder, end_bb);
    }

    LLVMPositionBuilderAtEnd(ctx->builder, end_bb);
    LLVMBuildRetVoid(ctx->builder);

    if (saved_bb != NULL)
        LLVMPositionBuilderAtEnd(ctx->builder, saved_bb);
}

/* Inline call to enum's drop function from a pointer-to-enum (for scope cleanup). */
static void emit_enum_drop(CodegenContext *ctx, LLVMValueRef enum_ptr, Type *enum_type)
{
    if (!enum_type || enum_type->kind != TYPE_ENUM) return;
    if (!enum_type->as.enom.has_drop) return;
    LLVMValueRef drop_fn = (LLVMValueRef)enum_type->as.enom.drop_fn;
    if (drop_fn == NULL) return;
    LLVMTypeRef fn_type = LLVMGlobalGetValueType(drop_fn);
    LLVMBuildCall2(ctx->builder, fn_type, drop_fn, &enum_ptr, 1, "");
}

/* Build the enum value for a variant constructor call/identifier.
   Allocates an enum struct on the stack, stores the discriminant byte at offset 0,
   and writes payload fields into the bytes-array at offset 1 via a bitcast to the
   variant's payload struct.  Self-recursive payload fields are heap-boxed via
   malloc + store, with the pointer stored into the slot. */
static LLVMValueRef emit_enum_ctor(CodegenContext *ctx, AstNode *node,
                                   Type *enum_type, int variant_idx,
                                   AstNode **args, int arg_count)
{
    (void)node;
    LLVMTypeRef enum_llvm = type_to_llvm(ctx, enum_type);
    LLVMTypeRef i8 = LLVMInt8TypeInContext(ctx->context);
    LLVMTypeRef i64 = LLVMInt64TypeInContext(ctx->context);

    /* Hoist the alloca to entry block to keep stack usage bounded in loops. */
    LLVMBasicBlockRef entry = LLVMGetEntryBasicBlock(ctx->current_fn);
    LLVMBuilderRef tmp_b = LLVMCreateBuilderInContext(ctx->context);
    LLVMValueRef first_inst = LLVMGetFirstInstruction(entry);
    if (first_inst) LLVMPositionBuilderBefore(tmp_b, first_inst);
    else            LLVMPositionBuilderAtEnd(tmp_b, entry);
    LLVMValueRef ealloca = LLVMBuildAlloca(tmp_b, enum_llvm, "enum.ctor");
    LLVMDisposeBuilder(tmp_b);

    /* Zero the payload bytes via memset so any unused tail bytes are deterministic. */
    LLVMTargetDataRef td = LLVMGetModuleDataLayout(ctx->module);
    unsigned long long total_sz = LLVMABISizeOfType(td, enum_llvm);
    LLVMValueRef memset_fn = LLVMGetNamedFunction(ctx->module, "memset");
    LLVMTypeRef  memset_ty = memset_fn ? LLVMGlobalGetValueType(memset_fn) : NULL;
    if (memset_fn)
    {
        LLVMValueRef ms_args[3] = {
            ealloca,
            LLVMConstInt(LLVMInt32TypeInContext(ctx->context), 0, 0),
            LLVMConstInt(i64, total_sz, 0)
        };
        LLVMBuildCall2(ctx->builder, memset_ty, memset_fn, ms_args, 3, "");
    }

    /* temp_string mark hoisted to the function scope so the post-loop flush
       still has access to it even when arg_count == 0. */
    int enum_temp_mark = ctx->temp_string_count;

    /* Store discriminant byte */
    LLVMValueRef disc_ptr = LLVMBuildStructGEP2(ctx->builder, enum_llvm, ealloca, 0, "disc.p");
    LLVMBuildStore(ctx->builder,
                   LLVMConstInt(i8, (unsigned long long)variant_idx, 0),
                   disc_ptr);

    if (arg_count > 0)
    {
        LLVMTypeRef variant_struct = build_variant_payload_struct(ctx, enum_type, variant_idx);
        LLVMValueRef payload_ptr = LLVMBuildStructGEP2(ctx->builder, enum_llvm, ealloca, 1, "payload.p");

        for (int i = 0; i < arg_count; i++)
        {
            LLVMValueRef v = codegen_expr(ctx, args[i]);
            if (v == NULL) continue;
            Type *pt = enum_type->as.enom.variants[variant_idx].payload_types[i];
            LLVMValueRef field_ptr = LLVMBuildStructGEP2(ctx->builder, variant_struct,
                                                         payload_ptr, (unsigned)i, "field.p");

            if (pt == enum_type)
            {
                /* Self-recursive payload: heap-box the value, store the pointer.
                   `v` from codegen_expr may be an alloca pointer (variable ref)
                   or an SSA value (fn call returning aggregate). */
                LLVMValueRef sz_arg = LLVMConstInt(i64, total_sz, 0);
                LLVMValueRef box = cg_emit_alloc(ctx, sz_arg, "enum.box",
                                                  CG_LINE(ctx), CG_COL(ctx));
                LLVMValueRef box_val = v;
                if (LLVMGetTypeKind(LLVMTypeOf(v)) == LLVMPointerTypeKind) {
                    /* v is an alloca pointer — load the value, then zero the
                       source so scope-cleanup won't double-free the heap boxes
                       now owned by this new box. */
                    box_val = LLVMBuildLoad2(ctx->builder, enum_llvm, v, "box.val");
                    if (memset_fn) {
                        LLVMValueRef z_args[3] = {
                            v,
                            LLVMConstInt(LLVMInt32TypeInContext(ctx->context), 0, 0),
                            LLVMConstInt(i64, total_sz, 0)
                        };
                        LLVMBuildCall2(ctx->builder, memset_ty, memset_fn, z_args, 3, "");
                    }
                }
                LLVMBuildStore(ctx->builder, box_val, box);
                LLVMBuildStore(ctx->builder, box, field_ptr);
            }
            else if (pt && pt->kind == TYPE_STRING)
            {
                /* Clone string into the enum so it owns its own heap copy.
                   Static literals (cap==0) clone to themselves cheaply; owned
                   sources get a fresh malloc'd buffer.  Without this the enum
                   would alias the source and trigger double-free at scope exit. */
                LLVMValueRef cloned = emit_string_clone_val(ctx, v);
                LLVMBuildStore(ctx->builder, cloned, field_ptr);
            }
            else
            {
                LLVMBuildStore(ctx->builder, v, field_ptr);
            }
        }
    }

    /* Flush temp strings created by argument evaluation (e.g. f-string inside
       Ok(f"got {x}")). The enum constructor clones string arguments into the
       payload, so the originals are safe to free here. */
    cg_flush_temps(ctx, enum_temp_mark, false);

    /* Return the loaded enum value */
    return LLVMBuildLoad2(ctx->builder, enum_llvm, ealloca, "enum.val");
}

static void codegen_struct_decl(CodegenContext *ctx, AstNode *node)
{
    const char *name = node->as.struct_decl.name;
    int n = node->as.struct_decl.field_count;

    /* Build LLVM struct type */
    LLVMTypeRef *fields = NULL;
    if (n > 0)
    {
        fields = (LLVMTypeRef *)malloc_safe((size_t)n * sizeof(LLVMTypeRef));
        Type *ml_type = node->resolved_type;
        for (int i = 0; i < n; i++)
        {
            fields[i] = type_to_llvm(ctx, ml_type->as.strukt.fields[i].type);
        }
    }

    LLVMTypeRef struct_type = LLVMStructCreateNamed(ctx->context, name);
    LLVMStructSetBody(struct_type, fields, (unsigned)n, 0);
    free(fields);

    register_struct_llvm(ctx, name, struct_type, node->resolved_type);
    /* Auto-drop generation is deferred to after all impl blocks are processed (Pass 2.5)
       to avoid generating auto-drop before user-defined __drop is known. */
}

static void codegen_impl_decl(CodegenContext *ctx, AstNode *node)
{
    const char *struct_name = node->as.impl_decl.name;

    for (int i = 0; i < node->as.impl_decl.method_count; i++)
    {
        AstNode *method = node->as.impl_decl.methods[i];

        const char *orig_name = method->as.fn_decl.name;

        /* ALL impl methods (static, instance, __drop) use qualified name to avoid conflicts */
        if (method->kind == AST_FN_DECL)
        {
            static char qualified_name[256];
            snprintf(qualified_name, sizeof(qualified_name), "%s.%s", struct_name, orig_name);
            method->as.fn_decl.name = qualified_name;

            if (strcmp(orig_name, "__drop") == 0)
            {
                /* Wrapper pattern for user __drop:
                   1. Generate user body as "StructName.__drop$" (internal; '$' is not a
                      valid LS identifier char, so users can never name-conflict with it)
                   2. Generate wrapper "StructName.__drop" that calls user body, then does
                      reverse-order compiler cleanup (strings + nested struct drops)
                   This makes drop_fn a "complete" drop — callers need no extra cleanup. */
                Type *st = find_struct_ls_type(ctx, struct_name);

                /* Step 1: generate user body as "StructName.__drop$"
                   The '$' character is not a valid LS identifier char, so users can never
                   define a method that conflicts with this internal name. */
                char user_name[256];
                snprintf(user_name, sizeof(user_name), "%s.__drop$", struct_name);
                method->as.fn_decl.name = user_name;
                codegen_fn_decl(ctx, method);
                LLVMValueRef user_fn = LLVMGetNamedFunction(ctx->module, user_name);

                /* Step 2: generate wrapper __drop = call user, then reverse-order cleanup */
                if (st != NULL && user_fn != NULL)
                {
                    LLVMTypeRef llvm_struct = type_to_llvm(ctx, st);
                    LLVMTypeRef ptr_struct = LLVMPointerType(llvm_struct, 0);
                    LLVMTypeRef wrapper_type = LLVMFunctionType(
                        LLVMVoidTypeInContext(ctx->context), &ptr_struct, 1, 0);

                    /* Reuse existing forward decl if present */
                    LLVMValueRef wrapper = LLVMGetNamedFunction(ctx->module, qualified_name);
                    if (wrapper == NULL)
                    {
                        wrapper = LLVMAddFunction(ctx->module, qualified_name, wrapper_type);
                    }
                    LLVMSetFunctionCallConv(wrapper, LLVMCCallConv);
                    LLVMBasicBlockRef entry = LLVMAppendBasicBlockInContext(
                        ctx->context, wrapper, "entry");

                    LLVMBasicBlockRef saved_bb = LLVMGetInsertBlock(ctx->builder);
                    LLVMPositionBuilderAtEnd(ctx->builder, entry);

                    LLVMValueRef self_ptr = LLVMGetParam(wrapper, 0);
                    LLVMSetValueName(self_ptr, "self");

                    /* Call user body first */
                    LLVMTypeRef user_fn_type = LLVMGlobalGetValueType(user_fn);
                    LLVMBuildCall2(ctx->builder, user_fn_type, user_fn, &self_ptr, 1, "");

                    /* Reverse-order compiler cleanup: strings + nested struct drops */
                    for (int fi = st->as.strukt.field_count - 1; fi >= 0; fi--)
                    {
                        Type *ft = st->as.strukt.fields[fi].type;
                        if (ft == NULL)
                            continue;
                        if (ft->kind == TYPE_POINTER)
                            continue;

                        if (ft->kind == TYPE_STRING)
                        {
                            LLVMValueRef fptr = LLVMBuildStructGEP2(ctx->builder, llvm_struct,
                                                                    self_ptr, (unsigned)fi, "wrap.sf");
                            /* emit_string_free leaves builder at cont block — safe to continue */
                            emit_string_free(ctx, fptr);
                        }
                        else if (ft->kind == TYPE_STRUCT && ft->as.strukt.has_drop)
                        {
                            LLVMValueRef member_drop = (LLVMValueRef)ft->as.strukt.drop_fn;
                            if (member_drop == NULL)
                            {
                                char mdrop_name[256];
                                snprintf(mdrop_name, sizeof(mdrop_name), "%s.__drop",
                                         ft->as.strukt.name);
                                member_drop = LLVMGetNamedFunction(ctx->module, mdrop_name);
                            }
                            if (member_drop != NULL)
                            {
                                LLVMValueRef fptr = LLVMBuildStructGEP2(ctx->builder, llvm_struct,
                                                                        self_ptr, (unsigned)fi, "wrap.mf");
                                LLVMTypeRef mft = LLVMGlobalGetValueType(member_drop);
                                LLVMBuildCall2(ctx->builder, mft, member_drop, &fptr, 1, "");
                            }
                            LLVMBasicBlockRef cont = LLVMGetInsertBlock(ctx->builder);
                            LLVMValueRef wfn = LLVMGetBasicBlockParent(cont);
                            LLVMBasicBlockRef nxt = LLVMAppendBasicBlockInContext(
                                ctx->context, wfn, "wrap.next");
                            LLVMBuildBr(ctx->builder, nxt);
                            LLVMPositionBuilderAtEnd(ctx->builder, nxt);
                        }
                    }

                    LLVMBuildRetVoid(ctx->builder);

                    if (saved_bb != NULL)
                    {
                        LLVMPositionBuilderAtEnd(ctx->builder, saved_bb);
                    }

                    /* Register as complete drop_fn */
                    st->as.strukt.drop_fn = wrapper;
                    st->as.strukt.has_user_drop = true;
                }
            }
            else
            {
                codegen_fn_decl(ctx, method);
            }
            /* Restore original name */
            method->as.fn_decl.name = (char *)orig_name;
        }
        else
        {
            codegen_fn_decl(ctx, method);
        }
    }
}

/* Map an LS type to the C ABI type (extern fn / FFI).
   The only difference from type_to_llvm: TYPE_STRING → i8* (not LsString struct) */
static LLVMTypeRef type_to_c_abi(CodegenContext *ctx, Type *t)
{
    if (t && t->kind == TYPE_STRING)
    {
        return LLVMPointerTypeInContext(ctx->context, 0);
    }
    return type_to_llvm(ctx, t);
}

/* ===== Phase E.2: Windows x64 ABI lowering helpers for extern struct ===== */

/* Compute C ABI byte size of an extern struct. Returns -1 if not an extern
   struct, or 0 if the LLVM type body has not been emitted yet. */
static int extern_struct_size(CodegenContext *ctx, Type *t)
{
    if (!t || t->kind != TYPE_STRUCT || !t->as.strukt.is_extern_c) return -1;
    LLVMTypeRef lt = type_to_llvm(ctx, t);
    if (!lt) return -1;
    /* If body not yet set the size query returns 0 — caller should arrange
       struct decls before fn decls (see cg_predeclare_extern_structs). */
    if (LLVMIsOpaqueStruct(lt)) return 0;
    LLVMTargetDataRef td = LLVMGetModuleDataLayout(ctx->module);
    if (!td) return -1;
    return (int)LLVMABISizeOfType(td, lt);
}

/* Windows x64: a struct of size 1 / 2 / 4 / 8 fits in a single integer
   register and is passed/returned as iN. Other sizes go via byval/sret. */
static bool extern_struct_fits_in_reg(int sz)
{
    return sz == 1 || sz == 2 || sz == 4 || sz == 8;
}

/* Map a register-passable struct size to the integer LLVM type used for
   bitcasting at the call boundary. */
static LLVMTypeRef extern_struct_reg_int_type(CodegenContext *ctx, int sz)
{
    switch (sz) {
        case 1: return LLVMInt8TypeInContext(ctx->context);
        case 2: return LLVMInt16TypeInContext(ctx->context);
        case 4: return LLVMInt32TypeInContext(ctx->context);
        case 8: return LLVMInt64TypeInContext(ctx->context);
        default: return NULL;
    }
}

/* Test whether an LS type triggers Phase E.2 lowering when it appears as a
   parameter or return value of an extern fn (i.e. it is an extern struct
   whose layout has been established). */
static bool extern_type_needs_lowering(CodegenContext *ctx, Type *t)
{
    if (!t || t->kind != TYPE_STRUCT || !t->as.strukt.is_extern_c) return false;
    return extern_struct_size(ctx, t) > 0;
}

/* Build an LLVM function type for an `extern fn` declaration, applying
   Windows x64 ABI lowering for any extern-struct params or return:
     - small (1/2/4/8 bytes) struct param   → integer iN
     - other-sized struct param             → pointer (byval at call site)
     - small struct return                  → integer iN
     - other-sized struct return            → void + sret pointer prepended */
static LLVMTypeRef extern_fn_type(CodegenContext *ctx, Type *fn_type_ml)
{
    int n = fn_type_ml->as.function.param_count;
    Type *ret_ml = fn_type_ml->as.function.return_type;

    bool has_sret = false;
    int ret_sz = -1;
    if (ret_ml && ret_ml->kind == TYPE_STRUCT && ret_ml->as.strukt.is_extern_c)
    {
        ret_sz = extern_struct_size(ctx, ret_ml);
        if (ret_sz > 0 && !extern_struct_fits_in_reg(ret_sz))
            has_sret = true;
    }

    int extra = has_sret ? 1 : 0;
    int total = n + extra;
    LLVMTypeRef *params = NULL;
    if (total > 0)
    {
        params = (LLVMTypeRef *)malloc_safe((size_t)total * sizeof(LLVMTypeRef));
    }

    int idx = 0;
    if (has_sret)
    {
        /* Hidden first parameter: pointer to caller-allocated return slot */
        params[idx++] = LLVMPointerTypeInContext(ctx->context, 0);
    }

    for (int i = 0; i < n; i++)
    {
        Type *pt = fn_type_ml->as.function.params[i];
        if (pt && pt->kind == TYPE_STRUCT && pt->as.strukt.is_extern_c)
        {
            int sz = extern_struct_size(ctx, pt);
            if (sz > 0 && extern_struct_fits_in_reg(sz))
                params[idx++] = extern_struct_reg_int_type(ctx, sz);
            else
                /* byval: caller copies struct to stack and passes pointer */
                params[idx++] = LLVMPointerTypeInContext(ctx->context, 0);
        }
        else
        {
            params[idx++] = type_to_c_abi(ctx, pt);
        }
    }

    LLVMTypeRef ret_llvm;
    if (has_sret)
    {
        ret_llvm = LLVMVoidTypeInContext(ctx->context);
    }
    else if (ret_ml && ret_ml->kind == TYPE_STRUCT && ret_ml->as.strukt.is_extern_c
             && ret_sz > 0)
    {
        ret_llvm = extern_struct_reg_int_type(ctx, ret_sz);
    }
    else
    {
        ret_llvm = type_to_c_abi(ctx, ret_ml);
    }

    LLVMTypeRef ft = LLVMFunctionType(ret_llvm, params, (unsigned)total,
                                      fn_type_ml->as.function.is_vararg ? 1 : 0);
    free(params);
    return ft;
}

static void codegen_extern_fn(CodegenContext *ctx, AstNode *node)
{
    Type *fn_type_ml = node->resolved_type;
    if (fn_type_ml == NULL || fn_type_ml->kind != TYPE_FUNCTION)
        return;

    /* Use C ABI type mapping: string → i8*, extern struct → byval/sret/iN */
    LLVMTypeRef fn_type = extern_fn_type(ctx, fn_type_ml);
    LLVMValueRef fn = LLVMGetNamedFunction(ctx->module, node->as.extern_fn.name);
    if (fn != NULL) return;
    fn = LLVMAddFunction(ctx->module, node->as.extern_fn.name, fn_type);
    LLVMSetLinkage(fn, LLVMExternalLinkage);

    /* Phase E.2: attach sret / byval LLVM attributes to the lowered params */
    Type *ret_ml = fn_type_ml->as.function.return_type;
    bool has_sret = false;
    if (ret_ml && ret_ml->kind == TYPE_STRUCT && ret_ml->as.strukt.is_extern_c)
    {
        int sz = extern_struct_size(ctx, ret_ml);
        if (sz > 0 && !extern_struct_fits_in_reg(sz))
        {
            has_sret = true;
            LLVMTypeRef ret_struct_t = type_to_llvm(ctx, ret_ml);
            unsigned ak = LLVMGetEnumAttributeKindForName("sret", 4);
            LLVMAttributeRef attr = LLVMCreateTypeAttribute(ctx->context, ak,
                                                            ret_struct_t);
            /* index 0 = return, 1 = first param */
            LLVMAddAttributeAtIndex(fn, 1, attr);
        }
    }

    int param_off = has_sret ? 1 : 0;
    int n = fn_type_ml->as.function.param_count;
    for (int i = 0; i < n; i++)
    {
        Type *pt = fn_type_ml->as.function.params[i];
        if (pt && pt->kind == TYPE_STRUCT && pt->as.strukt.is_extern_c)
        {
            int sz = extern_struct_size(ctx, pt);
            if (sz > 0 && !extern_struct_fits_in_reg(sz))
            {
                LLVMTypeRef pt_struct_t = type_to_llvm(ctx, pt);
                unsigned ak = LLVMGetEnumAttributeKindForName("byval", 5);
                LLVMAttributeRef attr = LLVMCreateTypeAttribute(ctx->context, ak,
                                                                pt_struct_t);
                LLVMAddAttributeAtIndex(fn, (unsigned)(param_off + i + 1), attr);
            }
        }
    }
}

/* Emit a named LLVM struct type for an 'extern struct' declaration.
   Non-packed (isPacked=0) so LLVM inserts C-compatible alignment padding. */
static void codegen_extern_struct_decl(CodegenContext *ctx, AstNode *node)
{
    if (node->resolved_type == NULL) return;
    const char *name = node->as.extern_struct_decl.name;

    /* Idempotent: skip if already registered (module re-emit path) */
    if (LLVMGetTypeByName2(ctx->context, name) != NULL) return;

    int n = node->as.extern_struct_decl.field_count;
    LLVMTypeRef *fields = NULL;
    if (n > 0)
    {
        fields = (LLVMTypeRef *)malloc_safe((size_t)n * sizeof(LLVMTypeRef));
        Type *ml_type = node->resolved_type;
        for (int i = 0; i < n; i++)
            fields[i] = type_to_llvm(ctx, ml_type->as.strukt.fields[i].type);
    }
    LLVMTypeRef st = LLVMStructCreateNamed(ctx->context, name);
    LLVMStructSetBody(st, fields, (unsigned)n, 0 /* not packed = C-compatible layout */);
    free(fields);
}

static void codegen_extern_block(CodegenContext *ctx, AstNode *node)
{
    /* Phase E.2: declare all extern structs first so subsequent extern fns
       can compute byval/sret ABI lowering against fully-laid-out types. */
    for (int i = 0; i < node->as.extern_block.decl_count; i++)
    {
        AstNode *d = node->as.extern_block.decls[i];
        if (d->kind == AST_EXTERN_STRUCT_DECL)
            codegen_extern_struct_decl(ctx, d);
    }
    for (int i = 0; i < node->as.extern_block.decl_count; i++)
    {
        AstNode *d = node->as.extern_block.decls[i];
        if (d->kind == AST_EXTERN_FN)
            codegen_extern_fn(ctx, d);
    }
}

/* Phase E.2: walk top-level decls and emit only extern struct types so
   that any subsequent extern fn declaration sees a fully laid-out LLVM
   struct (LLVMABISizeOfType requires non-opaque). Idempotent — relies
   on codegen_extern_struct_decl's LLVMGetTypeByName2 fast-path. */
static void cg_predeclare_extern_structs(CodegenContext *ctx, AstNode *ast)
{
    if (!ast || ast->kind != AST_PROGRAM) return;
    for (int i = 0; i < ast->as.program.decl_count; i++)
    {
        AstNode *d = ast->as.program.decls[i];
        if (d->kind == AST_EXTERN_STRUCT_DECL)
        {
            codegen_extern_struct_decl(ctx, d);
        }
        else if (d->kind == AST_EXTERN_BLOCK)
        {
            for (int j = 0; j < d->as.extern_block.decl_count; j++)
            {
                AstNode *e = d->as.extern_block.decls[j];
                if (e->kind == AST_EXTERN_STRUCT_DECL)
                    codegen_extern_struct_decl(ctx, e);
            }
        }
    }
}

/* Codegen for lib X = load("path") — creates a global var and init code */
static void codegen_load_lib(CodegenContext *ctx, AstNode *node)
{
    const char *var_name = node->as.load_lib.var_name;

    /* Create a global variable for the library handle (ptr, init to null) */
    LLVMTypeRef ptr_type = LLVMPointerTypeInContext(ctx->context, 0);
    LLVMValueRef global = LLVMAddGlobal(ctx->module, ptr_type, var_name);
    LLVMSetInitializer(global, LLVMConstNull(ptr_type));
    LLVMSetLinkage(global, LLVMInternalLinkage);

    /* Register in scope so other codegen phases can find it */
    cg_scope_define(ctx->current_scope, var_name, global, type_lib(), NULL);
}

/* Generate the __ls_ffi_init function that loads all libraries.
   Called once at the start of main(). Uses platform APIs directly so
   AOT-compiled executables don't need to link against ffi.c. */
static void codegen_ffi_init(CodegenContext *ctx, AstNode *ast)
{
    /* Count load_lib declarations */
    int lib_count = 0;
    for (int i = 0; i < ast->as.program.decl_count; i++)
    {
        if (ast->as.program.decls[i]->kind == AST_LOAD_LIB)
            lib_count++;
    }
    if (lib_count == 0)
        return;

    /* Create __ls_ffi_init() -> void */
    LLVMTypeRef void_type = LLVMVoidTypeInContext(ctx->context);
    LLVMTypeRef ptr_type = LLVMPointerTypeInContext(ctx->context, 0);
    LLVMTypeRef init_type = LLVMFunctionType(void_type, NULL, 0, 0);
    LLVMValueRef init_fn = LLVMAddFunction(ctx->module, "__ls_ffi_init", init_type);
    LLVMBasicBlockRef entry = LLVMAppendBasicBlockInContext(ctx->context, init_fn, "entry");
    LLVMPositionBuilderAtEnd(ctx->builder, entry);

#ifdef _WIN32
    LLVMValueRef load_fn = LLVMGetNamedFunction(ctx->module, "LoadLibraryA");
    LLVMTypeRef load_fn_type = LLVMFunctionType(ptr_type, &ptr_type, 1, 0);
#else
    LLVMValueRef load_fn = LLVMGetNamedFunction(ctx->module, "dlopen");
    LLVMTypeRef dlo_args[] = {ptr_type, LLVMInt32TypeInContext(ctx->context)};
    LLVMTypeRef load_fn_type = LLVMFunctionType(ptr_type, dlo_args, 2, 0);
#endif

    for (int i = 0; i < ast->as.program.decl_count; i++)
    {
        AstNode *decl = ast->as.program.decls[i];
        if (decl->kind != AST_LOAD_LIB)
            continue;

        const char *var_name = decl->as.load_lib.var_name;
        const char *lib_path = decl->as.load_lib.lib_path;

        LLVMValueRef global = LLVMGetNamedGlobal(ctx->module, var_name);
        if (global == NULL)
            continue;

        LLVMValueRef path_str = LLVMBuildGlobalStringPtr(ctx->builder, lib_path, "lib_path");

#ifdef _WIN32
        /* Call LoadLibraryA("path") */
        LLVMValueRef handle = LLVMBuildCall2(ctx->builder, load_fn_type, load_fn,
                                             &path_str, 1, "handle");
#else
        /* Call dlopen("path", RTLD_NOW=2) */
        LLVMValueRef dlo_call_args[] = {
            path_str,
            LLVMConstInt(LLVMInt32TypeInContext(ctx->context), 2, 0) /* RTLD_NOW */
        };
        LLVMValueRef handle = LLVMBuildCall2(ctx->builder, load_fn_type, load_fn,
                                             dlo_call_args, 2, "handle");
#endif
        /* Store handle into global */
        LLVMBuildStore(ctx->builder, handle, global);
    }

    LLVMBuildRetVoid(ctx->builder);
}

/* Codegen for lib.call("fn_name", args...) — dynamic FFI call */
static LLVMValueRef codegen_ffi_call(CodegenContext *ctx, AstNode *node)
{
    /* Get the library handle */
    LLVMValueRef lib_handle = codegen_expr(ctx, node->as.ffi_call.lib_expr);
    if (lib_handle == NULL)
    {
        cg_error(ctx, node->line, node->column, "cannot resolve library for FFI call");
        return NULL;
    }

    const char *fn_name = node->as.ffi_call.fn_name;
    LLVMTypeRef ptr_type = LLVMPointerTypeInContext(ctx->context, 0);

    /* Call GetProcAddress/dlsym to get function pointer at runtime */
#ifdef _WIN32
    LLVMValueRef sym_fn = LLVMGetNamedFunction(ctx->module, "GetProcAddress");
    LLVMTypeRef sym_args_t[] = {ptr_type, ptr_type};
    LLVMTypeRef sym_type = LLVMFunctionType(ptr_type, sym_args_t, 2, 0);
#else
    LLVMValueRef sym_fn = LLVMGetNamedFunction(ctx->module, "dlsym");
    LLVMTypeRef sym_args_t[] = {ptr_type, ptr_type};
    LLVMTypeRef sym_type = LLVMFunctionType(ptr_type, sym_args_t, 2, 0);
#endif

    LLVMValueRef name_str = LLVMBuildGlobalStringPtr(ctx->builder, fn_name, "ffi_fn_name");
    LLVMValueRef sym_args[] = {lib_handle, name_str};
    LLVMValueRef fn_ptr = LLVMBuildCall2(ctx->builder, sym_type, sym_fn,
                                         sym_args, 2, "ffi_sym");

    /* Build the call argument list */
    int argc = node->as.ffi_call.arg_count;
    LLVMValueRef *call_args = NULL;
    LLVMTypeRef *param_types = NULL;
    if (argc > 0)
    {
        call_args = (LLVMValueRef *)malloc_safe((size_t)argc * sizeof(LLVMValueRef));
        param_types = (LLVMTypeRef *)malloc_safe((size_t)argc * sizeof(LLVMTypeRef));
        for (int i = 0; i < argc; i++)
        {
            call_args[i] = codegen_expr(ctx, node->as.ffi_call.args[i]);
            if (call_args[i] == NULL)
            {
                free(call_args);
                free(param_types);
                return NULL;
            }
            /* For FFI calls, use C ABI types: string → i8*, not LsString */
            Type *arg_t = node->as.ffi_call.args[i]->resolved_type;
            if (arg_t && arg_t->kind == TYPE_STRING)
            {
                call_args[i] = ls_string_data(ctx, call_args[i]);
                param_types[i] = ptr_type;
            }
            else
            {
                param_types[i] = arg_t ? type_to_llvm(ctx, arg_t) : ptr_type;
            }
        }
    }

    /* Build a varargs function type for the dynamic call.
       Since we don't know the exact signature, use varargs.
       Return type: assume i32 (int) as default for C functions. */
    LLVMTypeRef ret_type = LLVMInt32TypeInContext(ctx->context);
    if (node->resolved_type && node->resolved_type->kind != TYPE_VOID)
    {
        ret_type = type_to_llvm(ctx, node->resolved_type);
    }

    LLVMTypeRef call_fn_type = LLVMFunctionType(ret_type, param_types, (unsigned)argc, 0);
    LLVMValueRef result = LLVMBuildCall2(ctx->builder, call_fn_type, fn_ptr,
                                         call_args, (unsigned)argc, "ffi_call");

    free(call_args);
    free(param_types);
    return result;
}

static void codegen_decl(CodegenContext *ctx, AstNode *node)
{
    if (node == NULL)
        return;
    switch (node->kind)
    {
    case AST_FN_DECL:
        codegen_fn_decl(ctx, node);
        break;
    case AST_STRUCT_DECL:
        codegen_struct_decl(ctx, node);
        break;
    case AST_ENUM_DECL:
        codegen_enum_decl(ctx, node);
        break;
    case AST_IMPL_DECL:
        codegen_impl_decl(ctx, node);
        break;
    case AST_EXTERN_FN:
        codegen_extern_fn(ctx, node);
        break;
    case AST_EXTERN_STRUCT_DECL:
        codegen_extern_struct_decl(ctx, node);
        break;
    case AST_EXTERN_BLOCK:
        codegen_extern_block(ctx, node);
        break;
    case AST_MODULE_DECL:
    case AST_IMPORT_DECL:
        break; /* no codegen needed */
    case AST_LOAD_LIB:
        codegen_load_lib(ctx, node);
        break;
    default:
        codegen_stmt(ctx, node);
        break;
    }
}

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

    /* __ls_str_split — runtime helper for string.split() */
    /* void __ls_str_split(ptr src, i32 src_len, ptr sep, i32 sep_len,
                           ptr out_data, ptr out_len, ptr out_cap) */
    {
        LLVMTypeRef sp_params[] = {
            ptr_type, i32_type, ptr_type, i32_type,
            ptr_type, ptr_type, ptr_type};
        LLVMTypeRef sp_type = LLVMFunctionType(LLVMVoidTypeInContext(ctx->context),
                                               sp_params, 7, 0);
        if (!LLVMGetNamedFunction(ctx->module, "__ls_str_split"))
            LLVMAddFunction(ctx->module, "__ls_str_split", sp_type);
    }

    /* __ls_str_join — runtime helper for string.join() */
    /* ptr __ls_str_join(ptr vec_data, i32 vec_len, ptr sep, i32 sep_len, ptr out_len) */
    {
        LLVMTypeRef jn_params[] = {
            ptr_type, i32_type, ptr_type, i32_type, ptr_type};
        LLVMTypeRef jn_type = LLVMFunctionType(ptr_type, jn_params, 5, 0);
        if (!LLVMGetNamedFunction(ctx->module, "__ls_str_join"))
            LLVMAddFunction(ctx->module, "__ls_str_join", jn_type);
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
       only declare it (no body) so it resolves via symbol search. */
    if (ctx->extern_builtins)
        return;

    LLVMValueRef fn = LLVMGetNamedFunction(ctx->module, "__ls_str_replace");
    if (fn == NULL || LLVMCountBasicBlocks(fn) > 0)
        return;

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
    LLVMValueRef count_ptr = LLVMBuildAlloca(ctx->builder, i32_t, "count");
    LLVMValueRef p_ptr = LLVMBuildAlloca(ctx->builder, ptr_t, "p");
    LLVMValueRef src_ptr = LLVMBuildAlloca(ctx->builder, ptr_t, "src");
    LLVMValueRef dst_ptr = LLVMBuildAlloca(ctx->builder, ptr_t, "dst");

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

/* Emit __ls_str_split(src_data, src_len, sep_data, sep_len,
                       out_data_ptr, out_len_ptr, out_cap_ptr) -> void
   Splits src by sep, returns array of heap-owned LsString elements via out params. */
static void emit_str_split_helper(CodegenContext *ctx)
{
    if (ctx->extern_builtins)
        return;

    LLVMValueRef fn = LLVMGetNamedFunction(ctx->module, "__ls_str_split");
    if (fn == NULL || LLVMCountBasicBlocks(fn) > 0)
        return;

    LLVMTypeRef i8_t = LLVMInt8TypeInContext(ctx->context);
    LLVMTypeRef i32_t = LLVMInt32TypeInContext(ctx->context);
    LLVMTypeRef i64_t = LLVMInt64TypeInContext(ctx->context);
    LLVMTypeRef ptr_t = LLVMPointerTypeInContext(ctx->context, 0);
    LLVMTypeRef str_t = ls_string_type(ctx);

    LLVMBasicBlockRef saved_bb = LLVMGetInsertBlock(ctx->builder);

    LLVMValueRef src_data = LLVMGetParam(fn, 0);
    LLVMValueRef src_len = LLVMGetParam(fn, 1);
    LLVMValueRef sep_data = LLVMGetParam(fn, 2);
    LLVMValueRef sep_len = LLVMGetParam(fn, 3);
    LLVMValueRef out_data_pp = LLVMGetParam(fn, 4);
    LLVMValueRef out_len_p = LLVMGetParam(fn, 5);
    LLVMValueRef out_cap_p = LLVMGetParam(fn, 6);

    LLVMValueRef zero32 = LLVMConstInt(i32_t, 0, 0);
    LLVMValueRef one32 = LLVMConstInt(i32_t, 1, 0);
    LLVMValueRef min_cap = LLVMConstInt(i32_t, LS_MIN_STR_CAP, 0);
    LLVMValueRef str_size = LLVMSizeOf(str_t); /* i64: sizeof(LsString) */

    LLVMValueRef strstr_fn = LLVMGetNamedFunction(ctx->module, "strstr");
    LLVMTypeRef strstr_t = LLVMGlobalGetValueType(strstr_fn);
    LLVMValueRef malloc_fn = LLVMGetNamedFunction(ctx->module, "malloc");
    LLVMTypeRef malloc_t = LLVMGlobalGetValueType(malloc_fn);
    LLVMValueRef memcpy_fn = LLVMGetNamedFunction(ctx->module, "memcpy");
    LLVMTypeRef memcpy_t = LLVMGlobalGetValueType(memcpy_fn);

    /* Append all basic blocks */
    LLVMBasicBlockRef entry_bb = LLVMAppendBasicBlockInContext(ctx->context, fn, "entry");
    LLVMBasicBlockRef special_bb = LLVMAppendBasicBlockInContext(ctx->context, fn, "spl.special");
    LLVMBasicBlockRef cnt_init_bb = LLVMAppendBasicBlockInContext(ctx->context, fn, "spl.cnt.init");
    LLVMBasicBlockRef cnt_cond_bb = LLVMAppendBasicBlockInContext(ctx->context, fn, "spl.cnt.cond");
    LLVMBasicBlockRef cnt_body_bb = LLVMAppendBasicBlockInContext(ctx->context, fn, "spl.cnt.body");
    LLVMBasicBlockRef cnt_end_bb = LLVMAppendBasicBlockInContext(ctx->context, fn, "spl.cnt.end");
    LLVMBasicBlockRef fill_init_bb = LLVMAppendBasicBlockInContext(ctx->context, fn, "spl.fill.init");
    LLVMBasicBlockRef fill_cond_bb = LLVMAppendBasicBlockInContext(ctx->context, fn, "spl.fill.cond");
    LLVMBasicBlockRef fill_body_bb = LLVMAppendBasicBlockInContext(ctx->context, fn, "spl.fill.body");
    LLVMBasicBlockRef fill_end_bb = LLVMAppendBasicBlockInContext(ctx->context, fn, "spl.fill.end");

    /* entry: allocas, check sep_len == 0 */
    LLVMPositionBuilderAtEnd(ctx->builder, entry_bb);
    LLVMValueRef count_ptr = LLVMBuildAlloca(ctx->builder, i32_t, "count");
    LLVMValueRef p_ptr = LLVMBuildAlloca(ctx->builder, ptr_t, "p");
    LLVMValueRef i_ptr = LLVMBuildAlloca(ctx->builder, i32_t, "spl.i");
    LLVMValueRef sep_empty = LLVMBuildICmp(ctx->builder, LLVMIntEQ, sep_len, zero32, "sep.empty");
    LLVMBuildCondBr(ctx->builder, sep_empty, special_bb, cnt_init_bb);

    /* special: sep is empty — return 1-element vec with copy of src */
    LLVMPositionBuilderAtEnd(ctx->builder, special_bb);
    {
        /* Allocate 1-element LsString array */
        LLVMValueRef arr_buf = LLVMBuildCall2(ctx->builder, malloc_t, malloc_fn,
                                              &str_size, 1, "spl.arr1");
        /* Allocate copy of src */
        LLVMValueRef need = LLVMBuildAdd(ctx->builder, src_len, one32, "spl.need");
        LLVMValueRef need_gt = LLVMBuildICmp(ctx->builder, LLVMIntUGT, need, min_cap, "spl.ngt");
        LLVMValueRef cap = LLVMBuildSelect(ctx->builder, need_gt, need, min_cap, "spl.cap");
        LLVMValueRef cap64 = LLVMBuildZExt(ctx->builder, cap, i64_t, "spl.cap64");
        LLVMValueRef sbuf = LLVMBuildCall2(ctx->builder, malloc_t, malloc_fn,
                                           &cap64, 1, "spl.sbuf");
        LLVMValueRef copylen = LLVMBuildZExt(ctx->builder,
                                             LLVMBuildAdd(ctx->builder, src_len, one32, "spl.cl"),
                                             i64_t, "spl.cl64");
        LLVMValueRef mc_sp[] = {sbuf, src_data, copylen};
        LLVMBuildCall2(ctx->builder, memcpy_t, memcpy_fn, mc_sp, 3, "");
        LLVMValueRef df = LLVMBuildStructGEP2(ctx->builder, str_t, arr_buf, 0, "spl.sp.df");
        LLVMValueRef lf = LLVMBuildStructGEP2(ctx->builder, str_t, arr_buf, 1, "spl.sp.lf");
        LLVMValueRef cf = LLVMBuildStructGEP2(ctx->builder, str_t, arr_buf, 2, "spl.sp.cf");
        LLVMBuildStore(ctx->builder, sbuf, df);
        LLVMBuildStore(ctx->builder, src_len, lf);
        LLVMBuildStore(ctx->builder, cap, cf);
        LLVMBuildStore(ctx->builder, arr_buf, out_data_pp);
        LLVMBuildStore(ctx->builder, one32, out_len_p);
        LLVMBuildStore(ctx->builder, one32, out_cap_p);
        LLVMBuildRetVoid(ctx->builder);
    }

    /* cnt_init: init counting loop */
    LLVMPositionBuilderAtEnd(ctx->builder, cnt_init_bb);
    LLVMBuildStore(ctx->builder, zero32, count_ptr);
    LLVMBuildStore(ctx->builder, src_data, p_ptr);
    LLVMBuildBr(ctx->builder, cnt_cond_bb);

    /* cnt_cond: q = strstr(p, sep); if NULL → cnt_end else → cnt_body */
    LLVMPositionBuilderAtEnd(ctx->builder, cnt_cond_bb);
    LLVMValueRef p_val_c = LLVMBuildLoad2(ctx->builder, ptr_t, p_ptr, "spl.pc");
    LLVMValueRef ss_c[] = {p_val_c, sep_data};
    LLVMValueRef q_c = LLVMBuildCall2(ctx->builder, strstr_t, strstr_fn, ss_c, 2, "spl.qc");
    LLVMValueRef null_p = LLVMConstNull(ptr_t);
    LLVMValueRef qnull_c = LLVMBuildICmp(ctx->builder, LLVMIntEQ, q_c, null_p, "spl.qnc");
    LLVMBuildCondBr(ctx->builder, qnull_c, cnt_end_bb, cnt_body_bb);

    /* cnt_body: count++; p = q + sep_len; → cnt_cond */
    LLVMPositionBuilderAtEnd(ctx->builder, cnt_body_bb);
    LLVMValueRef cv = LLVMBuildLoad2(ctx->builder, i32_t, count_ptr, "spl.cv");
    LLVMBuildStore(ctx->builder, LLVMBuildAdd(ctx->builder, cv, one32, "spl.ci"), count_ptr);
    LLVMValueRef np_c = LLVMBuildGEP2(ctx->builder, i8_t, q_c, &sep_len, 1, "spl.npc");
    LLVMBuildStore(ctx->builder, np_c, p_ptr);
    LLVMBuildBr(ctx->builder, cnt_cond_bb);

    /* cnt_end: num_parts = count + 1; allocate array */
    LLVMPositionBuilderAtEnd(ctx->builder, cnt_end_bb);
    LLVMValueRef final_cnt = LLVMBuildLoad2(ctx->builder, i32_t, count_ptr, "spl.fcnt");
    LLVMValueRef num_parts = LLVMBuildAdd(ctx->builder, final_cnt, one32, "spl.np");
    LLVMValueRef np64 = LLVMBuildZExt(ctx->builder, num_parts, i64_t, "spl.np64");
    LLVMValueRef arr_size = LLVMBuildMul(ctx->builder, np64, str_size, "spl.arrsz");
    LLVMValueRef arr_data = LLVMBuildCall2(ctx->builder, malloc_t, malloc_fn,
                                           &arr_size, 1, "spl.arr");
    LLVMBuildBr(ctx->builder, fill_init_bb);

    /* fill_init: reset p and i for fill loop */
    LLVMPositionBuilderAtEnd(ctx->builder, fill_init_bb);
    LLVMBuildStore(ctx->builder, src_data, p_ptr);
    LLVMBuildStore(ctx->builder, zero32, i_ptr);
    LLVMBuildBr(ctx->builder, fill_cond_bb);

    /* fill_cond: if i >= num_parts → fill_end */
    LLVMPositionBuilderAtEnd(ctx->builder, fill_cond_bb);
    LLVMValueRef iv = LLVMBuildLoad2(ctx->builder, i32_t, i_ptr, "spl.iv");
    LLVMValueRef idone = LLVMBuildICmp(ctx->builder, LLVMIntSGE, iv, num_parts, "spl.idone");
    LLVMBuildCondBr(ctx->builder, idone, fill_end_bb, fill_body_bb);

    /* fill_body: compute segment, malloc+copy, store LsString into arr_data[i] */
    LLVMPositionBuilderAtEnd(ctx->builder, fill_body_bb);
    LLVMValueRef p_cur = LLVMBuildLoad2(ctx->builder, ptr_t, p_ptr, "spl.pcur");
    LLVMValueRef ss_f[] = {p_cur, sep_data};
    LLVMValueRef q_f = LLVMBuildCall2(ctx->builder, strstr_t, strstr_fn, ss_f, 2, "spl.qf");
    LLVMValueRef qnull_f = LLVMBuildICmp(ctx->builder, LLVMIntEQ, q_f, null_p, "spl.qnf");

    /* seg_end: if q found use q, else use src_data + src_len */
    LLVMValueRef src_end = LLVMBuildGEP2(ctx->builder, i8_t, src_data, &src_len, 1, "spl.send");
    LLVMValueRef qf_int = LLVMBuildPtrToInt(ctx->builder, q_f, i64_t, "spl.qfi");
    LLVMValueRef pc_int = LLVMBuildPtrToInt(ctx->builder, p_cur, i64_t, "spl.pci");
    LLVMValueRef se_int = LLVMBuildPtrToInt(ctx->builder, src_end, i64_t, "spl.sei");
    LLVMValueRef diff_q = LLVMBuildSub(ctx->builder, qf_int, pc_int, "spl.dq");
    LLVMValueRef diff_e = LLVMBuildSub(ctx->builder, se_int, pc_int, "spl.de");
    LLVMValueRef seg_l64 = LLVMBuildSelect(ctx->builder, qnull_f, diff_e, diff_q, "spl.sl64");
    LLVMValueRef seg_l32 = LLVMBuildTrunc(ctx->builder, seg_l64, i32_t, "spl.sl32");

    /* Allocate heap-owned string for segment */
    LLVMValueRef sneed = LLVMBuildAdd(ctx->builder, seg_l32, one32, "spl.sneed");
    LLVMValueRef sneed_gt = LLVMBuildICmp(ctx->builder, LLVMIntUGT, sneed, min_cap, "spl.sngt");
    LLVMValueRef seg_cap = LLVMBuildSelect(ctx->builder, sneed_gt, sneed, min_cap, "spl.scap");
    LLVMValueRef scap64 = LLVMBuildZExt(ctx->builder, seg_cap, i64_t, "spl.sc64");
    LLVMValueRef seg_buf = LLVMBuildCall2(ctx->builder, malloc_t, malloc_fn, &scap64, 1, "spl.seg");
    LLVMValueRef mc_f[] = {seg_buf, p_cur, seg_l64};
    LLVMBuildCall2(ctx->builder, memcpy_t, memcpy_fn, mc_f, 3, "");
    /* null-terminate */
    LLVMValueRef null_pos = LLVMBuildGEP2(ctx->builder, i8_t, seg_buf, &seg_l32, 1, "spl.npos");
    LLVMBuildStore(ctx->builder, LLVMConstInt(i8_t, 0, 0), null_pos);

    /* Store LsString {seg_buf, seg_l32, seg_cap} into arr_data[i] */
    LLVMValueRef iv64 = LLVMBuildZExt(ctx->builder, iv, i64_t, "spl.iv64");
    LLVMValueRef elem_p = LLVMBuildGEP2(ctx->builder, str_t, arr_data, &iv64, 1, "spl.ep");
    LLVMValueRef fdf = LLVMBuildStructGEP2(ctx->builder, str_t, elem_p, 0, "spl.fdf");
    LLVMValueRef flf = LLVMBuildStructGEP2(ctx->builder, str_t, elem_p, 1, "spl.flf");
    LLVMValueRef fcf = LLVMBuildStructGEP2(ctx->builder, str_t, elem_p, 2, "spl.fcf");
    LLVMBuildStore(ctx->builder, seg_buf, fdf);
    LLVMBuildStore(ctx->builder, seg_l32, flf);
    LLVMBuildStore(ctx->builder, seg_cap, fcf);

    /* Advance p: if sep found, p = q + sep_len; else p = src_end */
    LLVMValueRef nfp = LLVMBuildGEP2(ctx->builder, i8_t, q_f, &sep_len, 1, "spl.nfp");
    LLVMValueRef nextp = LLVMBuildSelect(ctx->builder, qnull_f, src_end, nfp, "spl.nextp");
    LLVMBuildStore(ctx->builder, nextp, p_ptr);
    LLVMBuildStore(ctx->builder,
                   LLVMBuildAdd(ctx->builder, iv, one32, "spl.inext"), i_ptr);
    LLVMBuildBr(ctx->builder, fill_cond_bb);

    /* fill_end: store results and return */
    LLVMPositionBuilderAtEnd(ctx->builder, fill_end_bb);
    LLVMBuildStore(ctx->builder, arr_data, out_data_pp);
    LLVMBuildStore(ctx->builder, num_parts, out_len_p);
    LLVMBuildStore(ctx->builder, num_parts, out_cap_p);
    LLVMBuildRetVoid(ctx->builder);

    if (saved_bb)
        LLVMPositionBuilderAtEnd(ctx->builder, saved_bb);
}

/* Emit __ls_str_join(vec_data, vec_len, sep_data, sep_len, out_len_ptr) -> ptr
   Joins vec(string) elements with separator, returns malloc'd buffer. */
static void emit_str_join_helper(CodegenContext *ctx)
{
    if (ctx->extern_builtins)
        return;

    LLVMValueRef fn = LLVMGetNamedFunction(ctx->module, "__ls_str_join");
    if (fn == NULL || LLVMCountBasicBlocks(fn) > 0)
        return;

    LLVMTypeRef i8_t = LLVMInt8TypeInContext(ctx->context);
    LLVMTypeRef i32_t = LLVMInt32TypeInContext(ctx->context);
    LLVMTypeRef i64_t = LLVMInt64TypeInContext(ctx->context);
    LLVMTypeRef ptr_t = LLVMPointerTypeInContext(ctx->context, 0);
    LLVMTypeRef str_t = ls_string_type(ctx);

    LLVMBasicBlockRef saved_bb = LLVMGetInsertBlock(ctx->builder);

    LLVMValueRef vec_data = LLVMGetParam(fn, 0);  /* ptr to LsString array */
    LLVMValueRef vec_len = LLVMGetParam(fn, 1);   /* i32 */
    LLVMValueRef sep_data = LLVMGetParam(fn, 2);  /* ptr */
    LLVMValueRef sep_len = LLVMGetParam(fn, 3);   /* i32 */
    LLVMValueRef out_len_p = LLVMGetParam(fn, 4); /* ptr to i32 */

    LLVMValueRef zero32 = LLVMConstInt(i32_t, 0, 0);
    LLVMValueRef one32 = LLVMConstInt(i32_t, 1, 0);
    LLVMValueRef min_cap = LLVMConstInt(i32_t, LS_MIN_STR_CAP, 0);

    LLVMValueRef malloc_fn = LLVMGetNamedFunction(ctx->module, "malloc");
    LLVMTypeRef malloc_t = LLVMGlobalGetValueType(malloc_fn);
    LLVMValueRef memcpy_fn = LLVMGetNamedFunction(ctx->module, "memcpy");
    LLVMTypeRef memcpy_t = LLVMGlobalGetValueType(memcpy_fn);

    /* Append all basic blocks */
    LLVMBasicBlockRef entry_bb = LLVMAppendBasicBlockInContext(ctx->context, fn, "entry");
    LLVMBasicBlockRef empty_bb = LLVMAppendBasicBlockInContext(ctx->context, fn, "jn.empty");
    LLVMBasicBlockRef tot_init_bb = LLVMAppendBasicBlockInContext(ctx->context, fn, "jn.tot.init");
    LLVMBasicBlockRef tot_cond_bb = LLVMAppendBasicBlockInContext(ctx->context, fn, "jn.tot.cond");
    LLVMBasicBlockRef tot_body_bb = LLVMAppendBasicBlockInContext(ctx->context, fn, "jn.tot.body");
    LLVMBasicBlockRef tot_end_bb = LLVMAppendBasicBlockInContext(ctx->context, fn, "jn.tot.end");
    LLVMBasicBlockRef fill_cond_bb = LLVMAppendBasicBlockInContext(ctx->context, fn, "jn.fill.cond");
    LLVMBasicBlockRef fill_body_bb = LLVMAppendBasicBlockInContext(ctx->context, fn, "jn.fill.body");
    LLVMBasicBlockRef fill_sep_bb = LLVMAppendBasicBlockInContext(ctx->context, fn, "jn.fill.sep");
    LLVMBasicBlockRef fill_elem_bb = LLVMAppendBasicBlockInContext(ctx->context, fn, "jn.fill.elem");
    LLVMBasicBlockRef fill_end_bb = LLVMAppendBasicBlockInContext(ctx->context, fn, "jn.fill.end");

    /* entry: allocas, check empty vec */
    LLVMPositionBuilderAtEnd(ctx->builder, entry_bb);
    LLVMValueRef total_ptr = LLVMBuildAlloca(ctx->builder, i32_t, "total");
    LLVMValueRef i_ptr = LLVMBuildAlloca(ctx->builder, i32_t, "i");
    LLVMValueRef dst_ptr = LLVMBuildAlloca(ctx->builder, ptr_t, "dst");
    LLVMValueRef is_empty = LLVMBuildICmp(ctx->builder, LLVMIntEQ,
                                          vec_len, zero32, "jn.empty");
    LLVMBuildCondBr(ctx->builder, is_empty, empty_bb, tot_init_bb);

    /* empty: return malloc'd empty string */
    LLVMPositionBuilderAtEnd(ctx->builder, empty_bb);
    LLVMValueRef ecap64 = LLVMConstInt(i64_t, LS_MIN_STR_CAP, 0);
    LLVMValueRef ebuf = LLVMBuildCall2(ctx->builder, malloc_t, malloc_fn,
                                       &ecap64, 1, "jn.ebuf");
    LLVMBuildStore(ctx->builder, LLVMConstInt(i8_t, 0, 0), ebuf);
    LLVMBuildStore(ctx->builder, zero32, out_len_p);
    LLVMBuildRet(ctx->builder, ebuf);

    /* tot_init: total = 0, i = 0 */
    LLVMPositionBuilderAtEnd(ctx->builder, tot_init_bb);
    LLVMBuildStore(ctx->builder, zero32, total_ptr);
    LLVMBuildStore(ctx->builder, zero32, i_ptr);
    LLVMBuildBr(ctx->builder, tot_cond_bb);

    /* tot_cond: if i >= vec_len → tot_end */
    LLVMPositionBuilderAtEnd(ctx->builder, tot_cond_bb);
    LLVMValueRef ti = LLVMBuildLoad2(ctx->builder, i32_t, i_ptr, "jn.ti");
    LLVMValueRef td = LLVMBuildICmp(ctx->builder, LLVMIntSGE, ti, vec_len, "jn.td");
    LLVMBuildCondBr(ctx->builder, td, tot_end_bb, tot_body_bb);

    /* tot_body: total += vec[i].len + (i>0 ? sep_len : 0); i++ */
    LLVMPositionBuilderAtEnd(ctx->builder, tot_body_bb);
    LLVMValueRef ti64 = LLVMBuildZExt(ctx->builder, ti, i64_t, "jn.ti64");
    LLVMValueRef tep = LLVMBuildGEP2(ctx->builder, str_t, vec_data, &ti64, 1, "jn.tep");
    LLVMValueRef telp = LLVMBuildStructGEP2(ctx->builder, str_t, tep, 1, "jn.telp");
    LLVMValueRef telen = LLVMBuildLoad2(ctx->builder, i32_t, telp, "jn.telen");
    LLVMValueRef tot = LLVMBuildLoad2(ctx->builder, i32_t, total_ptr, "jn.tot");
    LLVMValueRef tot1 = LLVMBuildAdd(ctx->builder, tot, telen, "jn.t1");
    LLVMValueRef ti_gt0 = LLVMBuildICmp(ctx->builder, LLVMIntSGT, ti, zero32, "jn.igt0");
    LLVMValueRef sadd = LLVMBuildSelect(ctx->builder, ti_gt0, sep_len, zero32, "jn.sadd");
    LLVMValueRef tot2 = LLVMBuildAdd(ctx->builder, tot1, sadd, "jn.t2");
    LLVMBuildStore(ctx->builder, tot2, total_ptr);
    LLVMBuildStore(ctx->builder,
                   LLVMBuildAdd(ctx->builder, ti, one32, "jn.tinext"), i_ptr);
    LLVMBuildBr(ctx->builder, tot_cond_bb);

    /* tot_end: allocate result buffer, reset i = 0 */
    LLVMPositionBuilderAtEnd(ctx->builder, tot_end_bb);
    LLVMValueRef total_v = LLVMBuildLoad2(ctx->builder, i32_t, total_ptr, "jn.totv");
    LLVMValueRef bsz = LLVMBuildAdd(ctx->builder, total_v, one32, "jn.bsz");
    LLVMValueRef bsz64 = LLVMBuildZExt(ctx->builder, bsz, i64_t, "jn.bsz64");
    LLVMValueRef buf = LLVMBuildCall2(ctx->builder, malloc_t, malloc_fn,
                                      &bsz64, 1, "jn.buf");
    LLVMBuildStore(ctx->builder, buf, dst_ptr);
    LLVMBuildStore(ctx->builder, zero32, i_ptr);
    LLVMBuildBr(ctx->builder, fill_cond_bb);

    /* fill_cond: if i >= vec_len → fill_end */
    LLVMPositionBuilderAtEnd(ctx->builder, fill_cond_bb);
    LLVMValueRef fi = LLVMBuildLoad2(ctx->builder, i32_t, i_ptr, "jn.fi");
    LLVMValueRef fd = LLVMBuildICmp(ctx->builder, LLVMIntSGE, fi, vec_len, "jn.fd");
    LLVMBuildCondBr(ctx->builder, fd, fill_end_bb, fill_body_bb);

    /* fill_body: if i > 0 copy sep first, then copy element data */
    LLVMPositionBuilderAtEnd(ctx->builder, fill_body_bb);
    LLVMValueRef fi_gt0 = LLVMBuildICmp(ctx->builder, LLVMIntSGT, fi, zero32, "jn.fgt0");
    LLVMBuildCondBr(ctx->builder, fi_gt0, fill_sep_bb, fill_elem_bb);

    /* fill_sep: memcpy sep into dst, advance dst */
    LLVMPositionBuilderAtEnd(ctx->builder, fill_sep_bb);
    LLVMValueRef dst_s = LLVMBuildLoad2(ctx->builder, ptr_t, dst_ptr, "jn.dsts");
    LLVMValueRef sl64 = LLVMBuildZExt(ctx->builder, sep_len, i64_t, "jn.sl64");
    LLVMValueRef mc_s[] = {dst_s, sep_data, sl64};
    LLVMBuildCall2(ctx->builder, memcpy_t, memcpy_fn, mc_s, 3, "");
    LLVMValueRef dst_s2 = LLVMBuildGEP2(ctx->builder, i8_t, dst_s, &sep_len, 1, "jn.dsts2");
    LLVMBuildStore(ctx->builder, dst_s2, dst_ptr);
    LLVMBuildBr(ctx->builder, fill_elem_bb);

    /* fill_elem: memcpy element data into dst, advance dst, i++ */
    LLVMPositionBuilderAtEnd(ctx->builder, fill_elem_bb);
    LLVMValueRef fi64 = LLVMBuildZExt(ctx->builder, fi, i64_t, "jn.fi64");
    LLVMValueRef fep = LLVMBuildGEP2(ctx->builder, str_t, vec_data, &fi64, 1, "jn.fep");
    LLVMValueRef fdp = LLVMBuildStructGEP2(ctx->builder, str_t, fep, 0, "jn.fdp");
    LLVMValueRef felp2 = LLVMBuildStructGEP2(ctx->builder, str_t, fep, 1, "jn.felp2");
    LLVMValueRef fdata = LLVMBuildLoad2(ctx->builder, ptr_t, fdp, "jn.fdata");
    LLVMValueRef flen = LLVMBuildLoad2(ctx->builder, i32_t, felp2, "jn.flen");
    LLVMValueRef dst_e = LLVMBuildLoad2(ctx->builder, ptr_t, dst_ptr, "jn.dste");
    LLVMValueRef fl64 = LLVMBuildZExt(ctx->builder, flen, i64_t, "jn.fl64");
    LLVMValueRef mc_e[] = {dst_e, fdata, fl64};
    LLVMBuildCall2(ctx->builder, memcpy_t, memcpy_fn, mc_e, 3, "");
    LLVMValueRef dst_e2 = LLVMBuildGEP2(ctx->builder, i8_t, dst_e, &flen, 1, "jn.dste2");
    LLVMBuildStore(ctx->builder, dst_e2, dst_ptr);
    LLVMBuildStore(ctx->builder,
                   LLVMBuildAdd(ctx->builder, fi, one32, "jn.finext"), i_ptr);
    LLVMBuildBr(ctx->builder, fill_cond_bb);

    /* fill_end: null-terminate, store out_len, return buf */
    LLVMPositionBuilderAtEnd(ctx->builder, fill_end_bb);
    LLVMValueRef fdst = LLVMBuildLoad2(ctx->builder, ptr_t, dst_ptr, "jn.fdst");
    LLVMBuildStore(ctx->builder, LLVMConstInt(i8_t, 0, 0), fdst);
    LLVMBuildStore(ctx->builder, total_v, out_len_p);
    LLVMBuildRet(ctx->builder, buf);

    if (saved_bb)
        LLVMPositionBuilderAtEnd(ctx->builder, saved_bb);
    (void)min_cap;
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
    free(ctx->temp_string_slots);
    ctx->temp_string_slots = NULL;
    ctx->temp_string_count = 0;
    ctx->temp_string_cap = 0;

    /* Free memcheck site cache (keys were malloc'd) */
    for (int i = 0; i < ctx->mc_site_count; i++)
        free(ctx->mc_sites[i].key);
    free(ctx->mc_sites);
    ctx->mc_sites = NULL;
    ctx->mc_site_count = 0;
    ctx->mc_site_cap = 0;

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
    LLVMValueRef global = LLVMGetNamedGlobal(ctx->module, decl->as.var_decl.name);
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
    else if (var_type->kind == TYPE_STRING &&
             decl->as.var_decl.init->kind == AST_STRING_LIT)
    {
        /* Global string literal: build constant LsString { @data, len, 0 } */
        const char *sval = decl->as.var_decl.init->as.string_lit.value;
        int slen = (int)strlen(sval);
        LLVMValueRef gdata = LLVMBuildGlobalStringPtr(ctx->builder, sval, "g.str");
        LLVMValueRef cst = ls_string_const(ctx, gdata, slen, 0);
        LLVMBuildStore(ctx->builder, cst, global);
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

/* ---- Map runtime helpers (emitted as LLVM IR functions) ---- */

/* Emit __ls_map_hash_s(ptr data) -> i64
   FNV-1a hash over a null-terminated C string. */
static void emit_map_hash_s_helper(CodegenContext *ctx)
{
    const char *fname = "__ls_map_hash_s";
    LLVMValueRef fn = LLVMGetNamedFunction(ctx->module, fname);
    if (fn == NULL)
    {
        LLVMTypeRef ptr_t = LLVMPointerTypeInContext(ctx->context, 0);
        LLVMTypeRef i64_t = LLVMInt64TypeInContext(ctx->context);
        LLVMTypeRef ftype = LLVMFunctionType(i64_t, &ptr_t, 1, 0);
        fn = LLVMAddFunction(ctx->module, fname, ftype);
    }
    if (LLVMCountBasicBlocks(fn) > 0)
        return;
    /* In JIT mode with extern_builtins, the function body lives in the builtins
       module — just keep the forward declaration so callers can reference it. */
    if (ctx->extern_builtins)
        return;

    LLVMBasicBlockRef saved_bb = LLVMGetInsertBlock(ctx->builder);
    LLVMTypeRef i8_t = LLVMInt8TypeInContext(ctx->context);
    LLVMTypeRef i64_t = LLVMInt64TypeInContext(ctx->context);
    LLVMTypeRef ptr_t = LLVMPointerTypeInContext(ctx->context, 0);

    LLVMValueRef data = LLVMGetParam(fn, 0);

    LLVMBasicBlockRef entry = LLVMAppendBasicBlockInContext(ctx->context, fn, "entry");
    LLVMPositionBuilderAtEnd(ctx->builder, entry);

    LLVMValueRef hash_ptr = LLVMBuildAlloca(ctx->builder, i64_t, "hash");
    LLVMValueRef p_ptr = LLVMBuildAlloca(ctx->builder, ptr_t, "p");

    /* FNV offset basis */
    LLVMValueRef fnv_basis = LLVMConstInt(i64_t, 14695981039346656037ULL, 0);
    LLVMBuildStore(ctx->builder, fnv_basis, hash_ptr);
    LLVMBuildStore(ctx->builder, data, p_ptr);

    LLVMBasicBlockRef cond_bb = LLVMAppendBasicBlockInContext(ctx->context, fn, "h.cond");
    LLVMBasicBlockRef body_bb = LLVMAppendBasicBlockInContext(ctx->context, fn, "h.body");
    LLVMBasicBlockRef end_bb = LLVMAppendBasicBlockInContext(ctx->context, fn, "h.end");

    LLVMBuildBr(ctx->builder, cond_bb);
    LLVMPositionBuilderAtEnd(ctx->builder, cond_bb);
    LLVMValueRef p_val = LLVMBuildLoad2(ctx->builder, ptr_t, p_ptr, "p.v");
    LLVMValueRef ch = LLVMBuildLoad2(ctx->builder, i8_t, p_val, "ch");
    LLVMValueRef zero8 = LLVMConstInt(i8_t, 0, 0);
    LLVMValueRef is_end = LLVMBuildICmp(ctx->builder, LLVMIntEQ, ch, zero8, "is_end");
    LLVMBuildCondBr(ctx->builder, is_end, end_bb, body_bb);

    LLVMPositionBuilderAtEnd(ctx->builder, body_bb);
    LLVMValueRef ch64 = LLVMBuildZExt(ctx->builder, ch, i64_t, "ch64");
    LLVMValueRef hash_v = LLVMBuildLoad2(ctx->builder, i64_t, hash_ptr, "h.v");
    LLVMValueRef xord = LLVMBuildXor(ctx->builder, hash_v, ch64, "xord");
    /* FNV prime = 1099511628211 */
    LLVMValueRef fnv_prime = LLVMConstInt(i64_t, 1099511628211ULL, 0);
    LLVMValueRef muld = LLVMBuildMul(ctx->builder, xord, fnv_prime, "muld");
    LLVMBuildStore(ctx->builder, muld, hash_ptr);
    LLVMValueRef one8 = LLVMConstInt(i64_t, 1, 0);
    LLVMValueRef next_p = LLVMBuildGEP2(ctx->builder, i8_t, p_val, &one8, 1, "next_p");
    LLVMBuildStore(ctx->builder, next_p, p_ptr);
    LLVMBuildBr(ctx->builder, cond_bb);

    LLVMPositionBuilderAtEnd(ctx->builder, end_bb);
    LLVMValueRef result = LLVMBuildLoad2(ctx->builder, i64_t, hash_ptr, "result");
    LLVMBuildRet(ctx->builder, result);

    if (saved_bb)
        LLVMPositionBuilderAtEnd(ctx->builder, saved_bb);
}

/* Emit all per-(K,V) map helper IR functions.
   Called on demand; uses "emit once" guard. */
static void emit_map_helpers_for(CodegenContext *ctx, Type *key_type, Type *val_type)
{
    /* Note: do NOT skip when ctx->extern_builtins == true.
       Map helpers are type-specific generated code, NOT general builtins.
       In JIT mode they must still be generated in the current module;
       only emit_map_hash_s_helper is forward-declared (its body lives in
       the shared builtins module and is resolved by LLJIT). */

    char suffix[64];
    map_type_id(key_type, val_type, suffix, sizeof(suffix));

    /* Build function names */
    char nm_find[96], nm_set[96], nm_get[96], nm_contains[96];
    char nm_remove[96], nm_clear[96], nm_drop[96];
    snprintf(nm_find, sizeof(nm_find), "__ls_map_%s_find", suffix);
    snprintf(nm_set, sizeof(nm_set), "__ls_map_%s_set", suffix);
    snprintf(nm_get, sizeof(nm_get), "__ls_map_%s_get", suffix);
    snprintf(nm_contains, sizeof(nm_contains), "__ls_map_%s_contains", suffix);
    snprintf(nm_remove, sizeof(nm_remove), "__ls_map_%s_remove", suffix);
    snprintf(nm_clear, sizeof(nm_clear), "__ls_map_%s_clear", suffix);
    snprintf(nm_drop, sizeof(nm_drop), "__ls_map_%s_drop", suffix);

    /* Make sure hash_s helper exists */
    bool key_is_str = (key_type && key_type->kind == TYPE_STRING);
    bool val_is_str = (val_type && val_type->kind == TYPE_STRING);
    bool val_is_struct_drop = (val_type && val_type->kind == TYPE_STRUCT &&
                               val_type->as.strukt.has_drop);
    if (key_is_str)
        emit_map_hash_s_helper(ctx);

    LLVMTypeRef map_t = ls_map_type(ctx);
    LLVMTypeRef node_t = ls_map_node_type(ctx, key_type, val_type);
    LLVMTypeRef key_lt = type_to_llvm(ctx, key_type);
    LLVMTypeRef val_lt = type_to_llvm(ctx, val_type);
    LLVMTypeRef ptr_t = LLVMPointerTypeInContext(ctx->context, 0);
    LLVMTypeRef i32_t = LLVMInt32TypeInContext(ctx->context);
    LLVMTypeRef i64_t = LLVMInt64TypeInContext(ctx->context);
    LLVMTypeRef i1_t = LLVMInt1TypeInContext(ctx->context);
    LLVMTypeRef void_t = LLVMVoidTypeInContext(ctx->context);
    LLVMTypeRef str_t = ls_string_type(ctx);

    LLVMValueRef malloc_fn = LLVMGetNamedFunction(ctx->module, "malloc");
    LLVMTypeRef malloc_ft = LLVMGlobalGetValueType(malloc_fn);
    LLVMValueRef calloc_fn = LLVMGetNamedFunction(ctx->module, "calloc");
    LLVMTypeRef calloc_ft = LLVMGlobalGetValueType(calloc_fn);
    LLVMValueRef memcpy_fn = LLVMGetNamedFunction(ctx->module, "memcpy");
    LLVMTypeRef memcpy_ft = LLVMGlobalGetValueType(memcpy_fn);
    LLVMValueRef strcmp_fn = LLVMGetNamedFunction(ctx->module, "strcmp");
    LLVMTypeRef strcmp_ft = strcmp_fn ? LLVMGlobalGetValueType(strcmp_fn) : NULL;
    (void)memcpy_ft;
    (void)key_lt;
    (void)val_lt;
    (void)strcmp_ft;

    LLVMBasicBlockRef saved_bb = LLVMGetInsertBlock(ctx->builder);

    /* Helper: compute hash of key (for int: cast to i64; for string: call hash_s) */
#define MAP_EMIT_HASH(builder_pos, key_val, out_hash)                                            \
    do                                                                                           \
    {                                                                                            \
        if (key_is_str)                                                                          \
        {                                                                                        \
            LLVMValueRef hash_s_fn = LLVMGetNamedFunction(ctx->module, "__ls_map_hash_s");       \
            LLVMTypeRef hash_s_ft = LLVMGlobalGetValueType(hash_s_fn);                           \
            LLVMValueRef kdata = LLVMBuildExtractValue(ctx->builder, (key_val), 0, "kd");        \
            (out_hash) = LLVMBuildCall2(ctx->builder, hash_s_ft, hash_s_fn, &kdata, 1, "khash"); \
        }                                                                                        \
        else                                                                                     \
        {                                                                                        \
            (out_hash) = LLVMBuildSExtOrBitCast(ctx->builder, (key_val), i64_t, "khash");        \
        }                                                                                        \
    } while (0)

    /* Helper: compare two keys (for int: icmp eq; for string: strcmp == 0) */
#define MAP_EMIT_KEY_EQ(key_a, key_b, out_eq)                                                            \
    do                                                                                                   \
    {                                                                                                    \
        if (key_is_str)                                                                                  \
        {                                                                                                \
            LLVMValueRef da = LLVMBuildExtractValue(ctx->builder, (key_a), 0, "da");                     \
            LLVMValueRef db = LLVMBuildExtractValue(ctx->builder, (key_b), 0, "db");                     \
            LLVMValueRef cmpargs[] = {da, db};                                                           \
            LLVMValueRef cmpres = LLVMBuildCall2(ctx->builder, strcmp_ft, strcmp_fn, cmpargs, 2, "cmp"); \
            LLVMValueRef zero32 = LLVMConstInt(i32_t, 0, 0);                                             \
            (out_eq) = LLVMBuildICmp(ctx->builder, LLVMIntEQ, cmpres, zero32, "keq");                    \
        }                                                                                                \
        else                                                                                             \
        {                                                                                                \
            (out_eq) = LLVMBuildICmp(ctx->builder, LLVMIntEQ, (key_a), (key_b), "keq");                  \
        }                                                                                                \
    } while (0)

    /* Helper: copy a key into the node.
       For int: value copy (no alloc).
       For string: static (cap==0) → store .rodata pointer as-is, cap stays 0, no malloc;
                   dynamic (cap>0) → deep-copy via malloc+memcpy, new cap = max(16, len).
       MAP_EMIT_FREE_KEY checks cap>0 before free, so static keys are correctly skipped. */
#define MAP_EMIT_COPY_KEY(key_val, out_key_copy)                                                                 \
    do                                                                                                           \
    {                                                                                                            \
        if (key_is_str)                                                                                          \
        {                                                                                                        \
            LLVMValueRef ks_data = LLVMBuildExtractValue(ctx->builder, (key_val), 0, "ksd");                     \
            LLVMValueRef ks_len = LLVMBuildExtractValue(ctx->builder, (key_val), 1, "ksl");                      \
            LLVMValueRef ks_cap = LLVMBuildExtractValue(ctx->builder, (key_val), 2, "ksc");                      \
            LLVMValueRef zero32 = LLVMConstInt(i32_t, 0, 0);                                                     \
            /* Branch: heap-owned (cap>0) → deep-copy; static (cap==0) → store as-is */                          \
            LLVMValueRef is_dyn = LLVMBuildICmp(ctx->builder, LLVMIntSGT, ks_cap, zero32, "kisdyn");             \
            LLVMBasicBlockRef kcp_bb = LLVMAppendBasicBlockInContext(ctx->context, cur_fn, "kcp.copy");          \
            LLVMBasicBlockRef ksk_bb = LLVMAppendBasicBlockInContext(ctx->context, cur_fn, "kcp.skip");          \
            LLVMBasicBlockRef kmg_bb = LLVMAppendBasicBlockInContext(ctx->context, cur_fn, "kcp.merge");         \
            LLVMBuildCondBr(ctx->builder, is_dyn, kcp_bb, ksk_bb);                                               \
            /* --- copy path: heap-owned string, malloc + memcpy --- */                                          \
            LLVMPositionBuilderAtEnd(ctx->builder, kcp_bb);                                                      \
            LLVMValueRef len64 = LLVMBuildZExt(ctx->builder, ks_len, i64_t, "kl64");                             \
            LLVMValueRef one64 = LLVMConstInt(i64_t, 1, 0);                                                      \
            LLVMValueRef alloc_sz = LLVMBuildAdd(ctx->builder, len64, one64, "kasZ");                            \
            LLVMValueRef new_data = cg_emit_alloc(ctx, alloc_sz, "map.node", CG_LINE(ctx), CG_COL(ctx));        \
            LLVMValueRef mc_args[] = {new_data, ks_data, alloc_sz};                                              \
            LLVMBuildCall2(ctx->builder, memcpy_ft, memcpy_fn, mc_args, 3, "");                                  \
            LLVMValueRef min_cap = LLVMConstInt(i32_t, 16, 0);                                                   \
            LLVMValueRef new_cap = LLVMBuildSelect(ctx->builder,                                                 \
                                                   LLVMBuildICmp(ctx->builder, LLVMIntSGT, ks_len, min_cap, ""), \
                                                   ks_len, min_cap, "kcap");                                     \
            {                                                                                                    \
                LLVMValueRef _da[3] = {new_cap, ks_len, new_data};                                               \
                cg_emit_debug_printf(ctx, "[cg] str.clone  key(map) cap=%d len=%d ptr=%p\n", _da, 3);            \
            }                                                                                                    \
            LLVMBuildBr(ctx->builder, kmg_bb);                                                                   \
            /* --- skip path: static string (.rodata), store pointer directly, cap=0 --- */                      \
            LLVMPositionBuilderAtEnd(ctx->builder, ksk_bb);                                                      \
            {                                                                                                    \
                LLVMValueRef _ds[1] = {ks_data};                                                                 \
                cg_emit_debug_printf(ctx, "[cg] str.skip   key(map) static val=\"%s\"\n", _ds, 1);               \
            }                                                                                                    \
            LLVMBuildBr(ctx->builder, kmg_bb);                                                                   \
            /* --- merge: PHI for data ptr and cap; len is unchanged on both paths --- */                        \
            LLVMPositionBuilderAtEnd(ctx->builder, kmg_bb);                                                      \
            LLVMValueRef phi_data = LLVMBuildPhi(ctx->builder, ptr_t, "kph_d");                                  \
            LLVMValueRef phi_cap = LLVMBuildPhi(ctx->builder, i32_t, "kph_c");                                   \
            LLVMValueRef phi_data_vals[] = {new_data, ks_data};                                                  \
            LLVMBasicBlockRef phi_data_bbs[] = {kcp_bb, ksk_bb};                                                 \
            LLVMAddIncoming(phi_data, phi_data_vals, phi_data_bbs, 2);                                           \
            LLVMValueRef phi_cap_vals[] = {new_cap, zero32};                                                     \
            LLVMBasicBlockRef phi_cap_bbs[] = {kcp_bb, ksk_bb};                                                  \
            LLVMAddIncoming(phi_cap, phi_cap_vals, phi_cap_bbs, 2);                                              \
            LLVMValueRef ks_copy = LLVMGetUndef(str_t);                                                          \
            ks_copy = LLVMBuildInsertValue(ctx->builder, ks_copy, phi_data, 0, "");                              \
            ks_copy = LLVMBuildInsertValue(ctx->builder, ks_copy, ks_len, 1, "");                                \
            ks_copy = LLVMBuildInsertValue(ctx->builder, ks_copy, phi_cap, 2, "kscopy");                         \
            (out_key_copy) = ks_copy;                                                                            \
        }                                                                                                        \
        else                                                                                                     \
        {                                                                                                        \
            (out_key_copy) = (key_val);                                                                          \
        }                                                                                                        \
    } while (0)

    /* Helper: free a key stored in node.
       cap>0 means heap-owned (deep-copied dynamic string) → free(data).
       cap==0 means static (.rodata, stored as-is by MAP_EMIT_COPY_KEY) → skip free.
       cg_emit_debug_printf is a no-op when CG_DEBUG=0, so no overhead in release. */
#define MAP_EMIT_FREE_KEY(key_val)                                                                    \
    do                                                                                                \
    {                                                                                                 \
        if (key_is_str)                                                                               \
        {                                                                                             \
            LLVMValueRef kd = LLVMBuildExtractValue(ctx->builder, (key_val), 0, "kfd");               \
            LLVMValueRef kcp = LLVMBuildExtractValue(ctx->builder, (key_val), 2, "kfc");              \
            LLVMValueRef zero32 = LLVMConstInt(i32_t, 0, 0);                                          \
            LLVMValueRef kown = LLVMBuildICmp(ctx->builder, LLVMIntSGT, kcp, zero32, "kown");         \
            LLVMBasicBlockRef kf_bb = LLVMAppendBasicBlockInContext(ctx->context, cur_fn, "kf.free"); \
            LLVMBasicBlockRef ks_bb = LLVMAppendBasicBlockInContext(ctx->context, cur_fn, "kf.skip"); \
            LLVMBasicBlockRef kc_bb = LLVMAppendBasicBlockInContext(ctx->context, cur_fn, "kf.cont"); \
            LLVMBuildCondBr(ctx->builder, kown, kf_bb, ks_bb);                                        \
            LLVMPositionBuilderAtEnd(ctx->builder, kf_bb);                                            \
            {                                                                                         \
                LLVMValueRef _da[3] = {kcp, kd, kd};                                                  \
                cg_emit_debug_printf(ctx,                                                             \
                                     "[cg] str.free   key(map) cap=%d val=\"%s\" ptr=%p\n", _da, 3);  \
            }                                                                                         \
            cg_emit_free(ctx, kd, "map.scope_drop", CG_LINE(ctx), CG_COL(ctx));                        \
            LLVMBuildBr(ctx->builder, kc_bb);                                                         \
            LLVMPositionBuilderAtEnd(ctx->builder, ks_bb);                                            \
            {                                                                                         \
                LLVMValueRef _ds[1] = {kd};                                                           \
                cg_emit_debug_printf(ctx, "[cg] str.skip   key(map) static val=\"%s\"\n", _ds, 1);    \
            }                                                                                         \
            LLVMBuildBr(ctx->builder, kc_bb);                                                         \
            LLVMPositionBuilderAtEnd(ctx->builder, kc_bb);                                            \
        }                                                                                             \
    } while (0)

    /* Helper: deep-copy a value */
#define MAP_EMIT_COPY_VAL(val_val, out_val_copy)                                    \
    do                                                                              \
    {                                                                               \
        if (val_is_str)                                                             \
        {                                                                           \
            (out_val_copy) = emit_string_clone_val(ctx, (val_val));                 \
        }                                                                           \
        else if (val_is_struct_drop)                                                \
        {                                                                           \
            LLVMTypeRef vlt2 = type_to_llvm(ctx, val_type);                         \
            (out_val_copy) = emit_struct_clone_val(ctx, (val_val), vlt2, val_type); \
        }                                                                           \
        else                                                                        \
        {                                                                           \
            (out_val_copy) = (val_val);                                             \
        }                                                                           \
    } while (0)

    /* Helper: free a value stored in node */
#define MAP_EMIT_FREE_VAL(val_alloca_ptr)                                            \
    do                                                                               \
    {                                                                                \
        if (val_is_str)                                                              \
        {                                                                            \
            emit_string_free(ctx, (val_alloca_ptr));                                 \
        }                                                                            \
        else if (val_is_struct_drop)                                                 \
        {                                                                            \
            LLVMValueRef vdrop = (LLVMValueRef)val_type->as.strukt.drop_fn;          \
            if (vdrop)                                                               \
            {                                                                        \
                LLVMTypeRef vdft = LLVMGlobalGetValueType(vdrop);                    \
                LLVMBuildCall2(ctx->builder, vdft, vdrop, &(val_alloca_ptr), 1, ""); \
            }                                                                        \
        }                                                                            \
    } while (0)

    /* Compute node struct byte size for malloc */
    LLVMValueRef node_sz64 = LLVMSizeOf(node_t); /* i64 */

    /* ====================================================
       1. __ls_map_XX_YY_find(*LsMap, key) -> *Node (or NULL)
       ==================================================== */
    {
        LLVMValueRef fn_f = LLVMGetNamedFunction(ctx->module, nm_find);
        if (fn_f == NULL)
        {
            LLVMTypeRef params[] = {ptr_t, key_lt};
            LLVMTypeRef ftype = LLVMFunctionType(ptr_t, params, 2, 0);
            fn_f = LLVMAddFunction(ctx->module, nm_find, ftype);
        }
        if (LLVMCountBasicBlocks(fn_f) == 0)
        {
            LLVMValueRef p_map = LLVMGetParam(fn_f, 0);
            LLVMValueRef p_key = LLVMGetParam(fn_f, 1);
            LLVMBasicBlockRef entry = LLVMAppendBasicBlockInContext(ctx->context, fn_f, "entry");
            LLVMBasicBlockRef cond = LLVMAppendBasicBlockInContext(ctx->context, fn_f, "cond");
            LLVMBasicBlockRef check = LLVMAppendBasicBlockInContext(ctx->context, fn_f, "check");
            LLVMBasicBlockRef next = LLVMAppendBasicBlockInContext(ctx->context, fn_f, "next");
            LLVMBasicBlockRef found = LLVMAppendBasicBlockInContext(ctx->context, fn_f, "found");
            LLVMBasicBlockRef notfnd = LLVMAppendBasicBlockInContext(ctx->context, fn_f, "notfnd");
            LLVMValueRef cur_fn = fn_f;
            (void)cur_fn;

            LLVMPositionBuilderAtEnd(ctx->builder, entry);
            LLVMValueRef map_val = LLVMBuildLoad2(ctx->builder, map_t, p_map, "mv");
            LLVMValueRef buckets = LLVMBuildExtractValue(ctx->builder, map_val, 0, "bkts");
            LLVMValueRef cap_i32 = LLVMBuildExtractValue(ctx->builder, map_val, 2, "cap");
            LLVMValueRef cap_zero = LLVMBuildICmp(ctx->builder, LLVMIntEQ, cap_i32,
                                                  LLVMConstInt(i32_t, 0, 0), "czero");
            LLVMBuildCondBr(ctx->builder, cap_zero, notfnd, cond);

            LLVMPositionBuilderAtEnd(ctx->builder, cond);
            LLVMValueRef hash;
            MAP_EMIT_HASH(cond, p_key, hash);
            LLVMValueRef cap64 = LLVMBuildZExt(ctx->builder, cap_i32, i64_t, "cap64");
            LLVMValueRef bucket = LLVMBuildURem(ctx->builder, hash, cap64, "bucket");
            LLVMValueRef bucket32 = LLVMBuildTrunc(ctx->builder, bucket, i32_t, "b32");
            LLVMValueRef node_ptr = LLVMBuildGEP2(ctx->builder, ptr_t, buckets,
                                                  &bucket32, 1, "bptr");
            /* Wait: buckets is ptr to array of ptr. Each slot is a ptr. GEP needs i64. */
            LLVMValueRef node_p = LLVMBuildLoad2(ctx->builder, ptr_t, node_ptr, "node");
            LLVMValueRef node_alloca = LLVMBuildAlloca(ctx->builder, ptr_t, "nptr");
            LLVMBuildStore(ctx->builder, node_p, node_alloca);
            LLVMBuildBr(ctx->builder, check);

            LLVMPositionBuilderAtEnd(ctx->builder, check);
            LLVMValueRef cur_node = LLVMBuildLoad2(ctx->builder, ptr_t, node_alloca, "cn");
            LLVMValueRef null_ptr = LLVMConstNull(ptr_t);
            LLVMValueRef is_null = LLVMBuildICmp(ctx->builder, LLVMIntEQ, cur_node, null_ptr, "isnull");
            LLVMBuildCondBr(ctx->builder, is_null, notfnd, next);

            LLVMPositionBuilderAtEnd(ctx->builder, next);
            /* Load node hash and key */
            LLVMValueRef node_hash_p = LLVMBuildStructGEP2(ctx->builder, node_t, cur_node, 0, "nhp");
            LLVMValueRef node_hash = LLVMBuildLoad2(ctx->builder, i64_t, node_hash_p, "nh");
            LLVMValueRef hash_eq = LLVMBuildICmp(ctx->builder, LLVMIntEQ, node_hash, hash, "heq");
            /* Only compare key if hashes match */
            LLVMBasicBlockRef cmp_bb = LLVMAppendBasicBlockInContext(ctx->context, fn_f, "cmpkey");
            LLVMBasicBlockRef adv_bb = LLVMAppendBasicBlockInContext(ctx->context, fn_f, "adv");
            LLVMBuildCondBr(ctx->builder, hash_eq, cmp_bb, adv_bb);

            LLVMPositionBuilderAtEnd(ctx->builder, cmp_bb);
            LLVMValueRef node_key_p = LLVMBuildStructGEP2(ctx->builder, node_t, cur_node, 1, "nkp");
            LLVMValueRef node_key = LLVMBuildLoad2(ctx->builder, key_lt, node_key_p, "nk");
            LLVMValueRef key_eq;
            MAP_EMIT_KEY_EQ(node_key, p_key, key_eq);
            LLVMBuildCondBr(ctx->builder, key_eq, found, adv_bb);

            LLVMPositionBuilderAtEnd(ctx->builder, adv_bb);
            LLVMValueRef next_p = LLVMBuildStructGEP2(ctx->builder, node_t, cur_node, 3, "nxtp");
            LLVMValueRef next_v = LLVMBuildLoad2(ctx->builder, ptr_t, next_p, "nxt");
            LLVMBuildStore(ctx->builder, next_v, node_alloca);
            LLVMBuildBr(ctx->builder, check);

            LLVMPositionBuilderAtEnd(ctx->builder, found);
            LLVMBuildRet(ctx->builder, cur_node);

            LLVMPositionBuilderAtEnd(ctx->builder, notfnd);
            LLVMBuildRet(ctx->builder, null_ptr);
        }
    }

    /* Build fn_find reference for reuse */
    LLVMValueRef fn_find = LLVMGetNamedFunction(ctx->module, nm_find);
    LLVMTypeRef ft_find = LLVMGlobalGetValueType(fn_find);

    /* ====================================================
       2. __ls_map_XX_YY_set(*LsMap, key, val) -> void
          Insert or update.
       ==================================================== */
    {
        LLVMValueRef fn_s = LLVMGetNamedFunction(ctx->module, nm_set);
        if (fn_s == NULL)
        {
            LLVMTypeRef params[] = {ptr_t, key_lt, val_lt};
            LLVMTypeRef ftype = LLVMFunctionType(void_t, params, 3, 0);
            fn_s = LLVMAddFunction(ctx->module, nm_set, ftype);
        }
        if (LLVMCountBasicBlocks(fn_s) == 0)
        {
            LLVMValueRef p_map = LLVMGetParam(fn_s, 0);
            LLVMValueRef p_key = LLVMGetParam(fn_s, 1);
            LLVMValueRef p_val = LLVMGetParam(fn_s, 2);
            LLVMValueRef cur_fn = fn_s;
            (void)cur_fn;

            LLVMBasicBlockRef entry = LLVMAppendBasicBlockInContext(ctx->context, fn_s, "entry");
            LLVMBasicBlockRef init_bb = LLVMAppendBasicBlockInContext(ctx->context, fn_s, "init");
            LLVMBasicBlockRef rehash_bb = LLVMAppendBasicBlockInContext(ctx->context, fn_s, "rhcheck");
            LLVMBasicBlockRef do_rh_bb = LLVMAppendBasicBlockInContext(ctx->context, fn_s, "dorehash");
            LLVMBasicBlockRef insert_bb = LLVMAppendBasicBlockInContext(ctx->context, fn_s, "insert");
            LLVMBasicBlockRef upd_bb = LLVMAppendBasicBlockInContext(ctx->context, fn_s, "update");
            LLVMBasicBlockRef ret_bb = LLVMAppendBasicBlockInContext(ctx->context, fn_s, "ret");

            LLVMPositionBuilderAtEnd(ctx->builder, entry);

            /* ---- Guard: reject MOVED string keys (cap < 0) ---- */
            if (key_is_str)
            {
                LLVMBasicBlockRef key_warn_bb = LLVMAppendBasicBlockInContext(
                    ctx->context, fn_s, "key.moved");
                LLVMBasicBlockRef key_valid_bb = LLVMAppendBasicBlockInContext(
                    ctx->context, fn_s, "key.valid");

                LLVMValueRef kc = LLVMBuildExtractValue(ctx->builder, p_key, 2, "kc");
                LLVMValueRef is_moved = LLVMBuildICmp(ctx->builder, LLVMIntSLT,
                                                      kc, LLVMConstInt(i32_t, 0, 0), "kismv");
                LLVMBuildCondBr(ctx->builder, is_moved, key_warn_bb, key_valid_bb);

                /* warn path: print message and return (nop insertion) */
                LLVMPositionBuilderAtEnd(ctx->builder, key_warn_bb);
                {
                    LLVMValueRef pfn = LLVMGetNamedFunction(ctx->module, "printf");
                    if (pfn)
                    {
                        LLVMTypeRef pft = LLVMGlobalGetValueType(pfn);
                        LLVMValueRef kd = LLVMBuildExtractValue(ctx->builder, p_key, 0, "kwd");
                        LLVMValueRef fmt = LLVMBuildGlobalStringPtr(ctx->builder,
                                                                    "[WARNING] map.set: MOVED string key \"%s\" (cap<0),"
                                                                    " insertion skipped\n",
                                                                    "wmfmt");
                        LLVMValueRef wargs[] = {fmt, kd};
                        LLVMBuildCall2(ctx->builder, pft, pfn, wargs, 2, "");
                    }
                }
                LLVMBuildBr(ctx->builder, ret_bb);

                /* valid path: continue with normal set logic below */
                LLVMPositionBuilderAtEnd(ctx->builder, key_valid_bb);
            }

            /* if map->cap == 0: calloc(8, ptr_size) to initialise bucket array */
            LLVMValueRef cap_p = LLVMBuildStructGEP2(ctx->builder, map_t, p_map, 2, "capp");
            LLVMValueRef cap_v = LLVMBuildLoad2(ctx->builder, i32_t, cap_p, "cap");
            LLVMValueRef is_zero = LLVMBuildICmp(ctx->builder, LLVMIntEQ, cap_v,
                                                 LLVMConstInt(i32_t, 0, 0), "czero");
            LLVMBuildCondBr(ctx->builder, is_zero, init_bb, rehash_bb);

            LLVMPositionBuilderAtEnd(ctx->builder, init_bb);
            {
                LLVMValueRef init_cap = LLVMConstInt(i64_t, 8, 0);
                LLVMValueRef ptr_sz = LLVMSizeOf(ptr_t);
                LLVMValueRef ca_args[] = {init_cap, ptr_sz};
                LLVMValueRef new_bkts = LLVMBuildCall2(ctx->builder, calloc_ft, calloc_fn, ca_args, 2, "ibkts");
                LLVMValueRef bkts_p = LLVMBuildStructGEP2(ctx->builder, map_t, p_map, 0, "bktsp");
                LLVMBuildStore(ctx->builder, new_bkts, bkts_p);
                LLVMBuildStore(ctx->builder, LLVMConstInt(i32_t, 8, 0), cap_p);
#if CG_DEBUG
                cg_emit_debug_printf(ctx, "[cg] map.init   cap=8\n", NULL, 0);
#endif
            }
            LLVMBuildBr(ctx->builder, rehash_bb);

            /* Rehash check: if (len+1)*4 > cap*3 */
            LLVMPositionBuilderAtEnd(ctx->builder, rehash_bb);
            LLVMValueRef cap_v2 = LLVMBuildLoad2(ctx->builder, i32_t, cap_p, "cap2");
            LLVMValueRef len_p = LLVMBuildStructGEP2(ctx->builder, map_t, p_map, 1, "lenp");
            LLVMValueRef len_v = LLVMBuildLoad2(ctx->builder, i32_t, len_p, "len");
            LLVMValueRef len1 = LLVMBuildAdd(ctx->builder, len_v, LLVMConstInt(i32_t, 1, 0), "l1");
            LLVMValueRef lhs = LLVMBuildMul(ctx->builder, len1, LLVMConstInt(i32_t, 4, 0), "lhs");
            LLVMValueRef rhs = LLVMBuildMul(ctx->builder, cap_v2, LLVMConstInt(i32_t, 3, 0), "rhs");
            LLVMValueRef need_rh = LLVMBuildICmp(ctx->builder, LLVMIntSGT, lhs, rhs, "needrh");
            LLVMBuildCondBr(ctx->builder, need_rh, do_rh_bb, insert_bb);

            /* Rehash: new_cap = cap*2; relink all nodes */
            LLVMPositionBuilderAtEnd(ctx->builder, do_rh_bb);
            {
                LLVMValueRef old_cap_i32 = LLVMBuildLoad2(ctx->builder, i32_t, cap_p, "ocap");
                LLVMValueRef new_cap_i32 = LLVMBuildMul(ctx->builder, old_cap_i32, LLVMConstInt(i32_t, 2, 0), "ncap");
                LLVMValueRef new_cap64 = LLVMBuildZExt(ctx->builder, new_cap_i32, i64_t, "nc64");
                LLVMValueRef ptr_sz = LLVMSizeOf(ptr_t);
                LLVMValueRef ca2[] = {new_cap64, ptr_sz};
                LLVMValueRef new_bkts = LLVMBuildCall2(ctx->builder, calloc_ft, calloc_fn, ca2, 2, "nbkts");
                LLVMValueRef bkts_p = LLVMBuildStructGEP2(ctx->builder, map_t, p_map, 0, "bktp");
                LLVMValueRef old_bkts = LLVMBuildLoad2(ctx->builder, ptr_t, bkts_p, "obkts");
                LLVMValueRef old_cap64 = LLVMBuildZExt(ctx->builder, old_cap_i32, i64_t, "oc64");
#if CG_DEBUG
                {
                    LLVMValueRef da[] = {old_cap_i32, new_cap_i32};
                    cg_emit_debug_printf(ctx, "[cg] map.rehash  old_cap=%d new_cap=%d\n", da, 2);
                }
#endif
                /* Relink loop: for b in 0..old_cap */
                LLVMValueRef bi_alloca = LLVMBuildAlloca(ctx->builder, i64_t, "bi");
                LLVMBuildStore(ctx->builder, LLVMConstInt(i64_t, 0, 0), bi_alloca);
                LLVMBasicBlockRef rl_cond = LLVMAppendBasicBlockInContext(ctx->context, fn_s, "rl.cond");
                LLVMBasicBlockRef rl_body = LLVMAppendBasicBlockInContext(ctx->context, fn_s, "rl.body");
                LLVMBasicBlockRef rl_end = LLVMAppendBasicBlockInContext(ctx->context, fn_s, "rl.end");
                LLVMBuildBr(ctx->builder, rl_cond);

                LLVMPositionBuilderAtEnd(ctx->builder, rl_cond);
                LLVMValueRef bi = LLVMBuildLoad2(ctx->builder, i64_t, bi_alloca, "bi");
                LLVMValueRef bl = LLVMBuildICmp(ctx->builder, LLVMIntSLT, bi, old_cap64, "bl");
                LLVMBuildCondBr(ctx->builder, bl, rl_body, rl_end);

                LLVMPositionBuilderAtEnd(ctx->builder, rl_body);
                LLVMValueRef slot_p = LLVMBuildGEP2(ctx->builder, ptr_t, old_bkts, &bi, 1, "slp");
                LLVMValueRef head = LLVMBuildLoad2(ctx->builder, ptr_t, slot_p, "head");
                LLVMValueRef nd_alloca = LLVMBuildAlloca(ctx->builder, ptr_t, "nd");
                LLVMBuildStore(ctx->builder, head, nd_alloca);

                LLVMBasicBlockRef nd_cond = LLVMAppendBasicBlockInContext(ctx->context, fn_s, "nd.cond");
                LLVMBasicBlockRef nd_body = LLVMAppendBasicBlockInContext(ctx->context, fn_s, "nd.body");
                LLVMBasicBlockRef nd_end = LLVMAppendBasicBlockInContext(ctx->context, fn_s, "nd.end");
                LLVMBuildBr(ctx->builder, nd_cond);

                LLVMPositionBuilderAtEnd(ctx->builder, nd_cond);
                LLVMValueRef nd = LLVMBuildLoad2(ctx->builder, ptr_t, nd_alloca, "nd");
                LLVMValueRef nd_null = LLVMBuildICmp(ctx->builder, LLVMIntEQ, nd,
                                                     LLVMConstNull(ptr_t), "ndnull");
                LLVMBuildCondBr(ctx->builder, nd_null, nd_end, nd_body);

                LLVMPositionBuilderAtEnd(ctx->builder, nd_body);
                LLVMValueRef nd_next_p = LLVMBuildStructGEP2(ctx->builder, node_t, nd, 3, "ndnp");
                LLVMValueRef nd_next = LLVMBuildLoad2(ctx->builder, ptr_t, nd_next_p, "ndnxt");
                LLVMValueRef nd_hash_p = LLVMBuildStructGEP2(ctx->builder, node_t, nd, 0, "ndhp");
                LLVMValueRef nd_hash = LLVMBuildLoad2(ctx->builder, i64_t, nd_hash_p, "ndh");
                LLVMValueRef new_b64 = LLVMBuildURem(ctx->builder, nd_hash, new_cap64, "nb64");
                LLVMValueRef new_slot_p = LLVMBuildGEP2(ctx->builder, ptr_t, new_bkts, &new_b64, 1, "nsp");
                LLVMValueRef new_slot_v = LLVMBuildLoad2(ctx->builder, ptr_t, new_slot_p, "nsv");
                LLVMBuildStore(ctx->builder, new_slot_v, nd_next_p);
                LLVMBuildStore(ctx->builder, nd, new_slot_p);
                LLVMBuildStore(ctx->builder, nd_next, nd_alloca);
                LLVMBuildBr(ctx->builder, nd_cond);

                LLVMPositionBuilderAtEnd(ctx->builder, nd_end);
                LLVMValueRef biinc = LLVMBuildAdd(ctx->builder, bi, LLVMConstInt(i64_t, 1, 0), "biinc");
                LLVMBuildStore(ctx->builder, biinc, bi_alloca);
                LLVMBuildBr(ctx->builder, rl_cond);

                LLVMPositionBuilderAtEnd(ctx->builder, rl_end);
                cg_emit_free(ctx, old_bkts, "map.scope_drop", CG_LINE(ctx), CG_COL(ctx));
                LLVMBuildStore(ctx->builder, new_bkts, bkts_p);
                LLVMBuildStore(ctx->builder, new_cap_i32, cap_p);
            }
            LLVMBuildBr(ctx->builder, insert_bb);

            /* Insert or update */
            LLVMPositionBuilderAtEnd(ctx->builder, insert_bb);
            {
                /* Try find first */
                LLVMValueRef fa[] = {p_map, p_key};
                LLVMValueRef existing_node = LLVMBuildCall2(ctx->builder, ft_find, fn_find, fa, 2, "exn");
                LLVMValueRef ex_null = LLVMBuildICmp(ctx->builder, LLVMIntEQ, existing_node,
                                                     LLVMConstNull(ptr_t), "exnull");
                LLVMBasicBlockRef new_bb = LLVMAppendBasicBlockInContext(ctx->context, fn_s, "new");
                LLVMBuildCondBr(ctx->builder, ex_null, new_bb, upd_bb);

                /* Update existing node */
                LLVMPositionBuilderAtEnd(ctx->builder, upd_bb);
                LLVMValueRef val_p_in_node = LLVMBuildStructGEP2(ctx->builder, node_t, existing_node, 2, "vnp");
                if (val_is_str)
                    emit_string_free(ctx, val_p_in_node);
                else if (val_is_struct_drop)
                {
                    LLVMValueRef vdrop = (LLVMValueRef)val_type->as.strukt.drop_fn;
                    if (vdrop)
                    {
                        LLVMTypeRef vdft = LLVMGlobalGetValueType(vdrop);
                        LLVMBuildCall2(ctx->builder, vdft, vdrop, &val_p_in_node, 1, "");
                    }
                }
                LLVMValueRef val_copy;
                MAP_EMIT_COPY_VAL(p_val, val_copy);
                LLVMBuildStore(ctx->builder, val_copy, val_p_in_node);
#if CG_DEBUG
                cg_emit_debug_printf(ctx, "[cg] map.update  existing key\n", NULL, 0);
#endif
                LLVMBuildBr(ctx->builder, ret_bb);

                /* New node */
                LLVMPositionBuilderAtEnd(ctx->builder, new_bb);
                LLVMValueRef new_node = cg_emit_alloc(ctx, node_sz64, "map.node",
                                                        CG_LINE(ctx), CG_COL(ctx));
                /* Compute hash + bucket */
                LLVMValueRef nh;
                MAP_EMIT_HASH(new_bb, p_key, nh);
                LLVMValueRef cap_cur = LLVMBuildLoad2(ctx->builder, i32_t, cap_p, "capcur");
                LLVMValueRef cap64c = LLVMBuildZExt(ctx->builder, cap_cur, i64_t, "c64c");
                LLVMValueRef bkts_cur = LLVMBuildLoad2(ctx->builder, ptr_t,
                                                       LLVMBuildStructGEP2(ctx->builder, map_t, p_map, 0, ""), "bcur");
                LLVMValueRef buck64 = LLVMBuildURem(ctx->builder, nh, cap64c, "buck");
                LLVMValueRef buck32 = LLVMBuildTrunc(ctx->builder, buck64, i32_t, "b32");
                LLVMValueRef bslot_p = LLVMBuildGEP2(ctx->builder, ptr_t, bkts_cur, &buck32, 1, "bsp");
                LLVMValueRef bslot_v = LLVMBuildLoad2(ctx->builder, ptr_t, bslot_p, "bsv");

                /* Fill node fields */
                LLVMValueRef nhp = LLVMBuildStructGEP2(ctx->builder, node_t, new_node, 0, "nhp");
                LLVMBuildStore(ctx->builder, nh, nhp);
                LLVMValueRef key_copy;
                MAP_EMIT_COPY_KEY(p_key, key_copy);
                LLVMValueRef nkp = LLVMBuildStructGEP2(ctx->builder, node_t, new_node, 1, "nkp");
                LLVMBuildStore(ctx->builder, key_copy, nkp);
                LLVMValueRef nval_copy;
                MAP_EMIT_COPY_VAL(p_val, nval_copy);
                LLVMValueRef nvp = LLVMBuildStructGEP2(ctx->builder, node_t, new_node, 2, "nvp");
                LLVMBuildStore(ctx->builder, nval_copy, nvp);
                LLVMValueRef nnxp = LLVMBuildStructGEP2(ctx->builder, node_t, new_node, 3, "nnxp");
                LLVMBuildStore(ctx->builder, bslot_v, nnxp);
                LLVMBuildStore(ctx->builder, new_node, bslot_p);
                LLVMValueRef len_v2 = LLVMBuildLoad2(ctx->builder, i32_t, len_p, "lv2");
                LLVMValueRef len_inc = LLVMBuildAdd(ctx->builder, len_v2, LLVMConstInt(i32_t, 1, 0), "linc");
                LLVMBuildStore(ctx->builder, len_inc, len_p);
#if CG_DEBUG
                {
                    LLVMValueRef da[] = {nh, buck32, len_inc};
                    cg_emit_debug_printf(ctx, "[cg] map.insert  hash=%lld bucket=%d new_len=%d\n", da, 3);
                }
#endif
                LLVMBuildBr(ctx->builder, ret_bb);
            }

            LLVMPositionBuilderAtEnd(ctx->builder, ret_bb);
            LLVMBuildRetVoid(ctx->builder);
        }
    }

    /* ====================================================
       3. __ls_map_XX_YY_get(*LsMap, key) -> val (deep copy)
       ==================================================== */
    {
        LLVMValueRef fn_g = LLVMGetNamedFunction(ctx->module, nm_get);
        if (fn_g == NULL)
        {
            LLVMTypeRef params[] = {ptr_t, key_lt};
            LLVMTypeRef ftype = LLVMFunctionType(val_lt, params, 2, 0);
            fn_g = LLVMAddFunction(ctx->module, nm_get, ftype);
        }
        if (LLVMCountBasicBlocks(fn_g) == 0)
        {
            LLVMValueRef p_map = LLVMGetParam(fn_g, 0);
            LLVMValueRef p_key = LLVMGetParam(fn_g, 1);
            LLVMValueRef cur_fn = fn_g;

            LLVMBasicBlockRef entry = LLVMAppendBasicBlockInContext(ctx->context, fn_g, "entry");
            LLVMBasicBlockRef fnd_bb = LLVMAppendBasicBlockInContext(ctx->context, fn_g, "found");
            LLVMBasicBlockRef nfn_bb = LLVMAppendBasicBlockInContext(ctx->context, fn_g, "notfound");

            LLVMPositionBuilderAtEnd(ctx->builder, entry);
            LLVMValueRef fa[] = {p_map, p_key};
            LLVMValueRef nd = LLVMBuildCall2(ctx->builder, ft_find, fn_find, fa, 2, "nd");
            LLVMValueRef is_null = LLVMBuildICmp(ctx->builder, LLVMIntEQ, nd, LLVMConstNull(ptr_t), "inull");
            LLVMBuildCondBr(ctx->builder, is_null, nfn_bb, fnd_bb);

            LLVMPositionBuilderAtEnd(ctx->builder, fnd_bb);
            LLVMValueRef vp = LLVMBuildStructGEP2(ctx->builder, node_t, nd, 2, "vp");
            LLVMValueRef vv = LLVMBuildLoad2(ctx->builder, val_lt, vp, "vv");
            LLVMValueRef vout;
            MAP_EMIT_COPY_VAL(vv, vout);
            /* builder may have moved inside clone helpers */
            LLVMBuildRet(ctx->builder, vout);

            LLVMPositionBuilderAtEnd(ctx->builder, nfn_bb);
            (void)cur_fn;
            LLVMBuildRet(ctx->builder, LLVMConstNull(val_lt));
        }
    }

    /* ====================================================
       4. __ls_map_XX_YY_contains(*LsMap, key) -> i1
       ==================================================== */
    {
        LLVMValueRef fn_c = LLVMGetNamedFunction(ctx->module, nm_contains);
        if (fn_c == NULL)
        {
            LLVMTypeRef params[] = {ptr_t, key_lt};
            LLVMTypeRef ftype = LLVMFunctionType(i1_t, params, 2, 0);
            fn_c = LLVMAddFunction(ctx->module, nm_contains, ftype);
        }
        if (LLVMCountBasicBlocks(fn_c) == 0)
        {
            LLVMValueRef p_map = LLVMGetParam(fn_c, 0);
            LLVMValueRef p_key = LLVMGetParam(fn_c, 1);
            LLVMBasicBlockRef entry = LLVMAppendBasicBlockInContext(ctx->context, fn_c, "entry");
            LLVMPositionBuilderAtEnd(ctx->builder, entry);
            LLVMValueRef fa[] = {p_map, p_key};
            LLVMValueRef nd = LLVMBuildCall2(ctx->builder, ft_find, fn_find, fa, 2, "nd");
            LLVMValueRef not_null = LLVMBuildICmp(ctx->builder, LLVMIntNE, nd, LLVMConstNull(ptr_t), "nn");
            LLVMBuildRet(ctx->builder, not_null);
        }
    }

    /* ====================================================
       5. __ls_map_XX_YY_remove(*LsMap, key) -> void
       ==================================================== */
    {
        LLVMValueRef fn_r = LLVMGetNamedFunction(ctx->module, nm_remove);
        if (fn_r == NULL)
        {
            LLVMTypeRef params[] = {ptr_t, key_lt};
            LLVMTypeRef ftype = LLVMFunctionType(void_t, params, 2, 0);
            fn_r = LLVMAddFunction(ctx->module, nm_remove, ftype);
        }
        if (LLVMCountBasicBlocks(fn_r) == 0)
        {
            LLVMValueRef p_map = LLVMGetParam(fn_r, 0);
            LLVMValueRef p_key = LLVMGetParam(fn_r, 1);
            LLVMValueRef cur_fn = fn_r;

            LLVMBasicBlockRef entry = LLVMAppendBasicBlockInContext(ctx->context, fn_r, "entry");
            LLVMBasicBlockRef em_bb = LLVMAppendBasicBlockInContext(ctx->context, fn_r, "empty");
            LLVMBasicBlockRef search = LLVMAppendBasicBlockInContext(ctx->context, fn_r, "search");
            LLVMBasicBlockRef loop_cond = LLVMAppendBasicBlockInContext(ctx->context, fn_r, "lp.cond");
            LLVMBasicBlockRef loop_body = LLVMAppendBasicBlockInContext(ctx->context, fn_r, "lp.body");
            LLVMBasicBlockRef cmp_k = LLVMAppendBasicBlockInContext(ctx->context, fn_r, "cmpk");
            LLVMBasicBlockRef do_rm = LLVMAppendBasicBlockInContext(ctx->context, fn_r, "dorm");
            LLVMBasicBlockRef adv_rm = LLVMAppendBasicBlockInContext(ctx->context, fn_r, "advrm");
            LLVMBasicBlockRef ret_bb = LLVMAppendBasicBlockInContext(ctx->context, fn_r, "ret");

            LLVMPositionBuilderAtEnd(ctx->builder, entry);
            LLVMValueRef map_val = LLVMBuildLoad2(ctx->builder, map_t, p_map, "mv");
            LLVMValueRef cap_v = LLVMBuildExtractValue(ctx->builder, map_val, 2, "cap");
            LLVMValueRef czero = LLVMBuildICmp(ctx->builder, LLVMIntEQ, cap_v, LLVMConstInt(i32_t, 0, 0), "cz");
            LLVMBuildCondBr(ctx->builder, czero, em_bb, search);

            LLVMPositionBuilderAtEnd(ctx->builder, em_bb);
            LLVMBuildRetVoid(ctx->builder);

            LLVMPositionBuilderAtEnd(ctx->builder, search);
            LLVMValueRef hash;
            MAP_EMIT_HASH(search, p_key, hash);
            LLVMValueRef cap64 = LLVMBuildZExt(ctx->builder, cap_v, i64_t, "c64");
            LLVMValueRef buck64 = LLVMBuildURem(ctx->builder, hash, cap64, "buck");
            LLVMValueRef bkts = LLVMBuildExtractValue(ctx->builder, map_val, 0, "bkts");
            LLVMValueRef slot_p = LLVMBuildGEP2(ctx->builder, ptr_t, bkts, &buck64, 1, "slp");

            LLVMValueRef prev_alloca = LLVMBuildAlloca(ctx->builder, ptr_t, "prev");
            LLVMValueRef cur_alloca = LLVMBuildAlloca(ctx->builder, ptr_t, "cur");
            LLVMBuildStore(ctx->builder, LLVMConstNull(ptr_t), prev_alloca);
            LLVMValueRef slot_v = LLVMBuildLoad2(ctx->builder, ptr_t, slot_p, "sv");
            LLVMBuildStore(ctx->builder, slot_v, cur_alloca);
            LLVMBuildBr(ctx->builder, loop_cond);

            LLVMPositionBuilderAtEnd(ctx->builder, loop_cond);
            LLVMValueRef cur_nd = LLVMBuildLoad2(ctx->builder, ptr_t, cur_alloca, "cnd");
            LLVMValueRef is_null = LLVMBuildICmp(ctx->builder, LLVMIntEQ, cur_nd, LLVMConstNull(ptr_t), "inull");
            LLVMBuildCondBr(ctx->builder, is_null, ret_bb, loop_body);

            LLVMPositionBuilderAtEnd(ctx->builder, loop_body);
            LLVMValueRef nd_hp = LLVMBuildStructGEP2(ctx->builder, node_t, cur_nd, 0, "nhp");
            LLVMValueRef nd_h = LLVMBuildLoad2(ctx->builder, i64_t, nd_hp, "nh");
            LLVMValueRef heq = LLVMBuildICmp(ctx->builder, LLVMIntEQ, nd_h, hash, "heq");
            LLVMBuildCondBr(ctx->builder, heq, cmp_k, adv_rm);

            LLVMPositionBuilderAtEnd(ctx->builder, cmp_k);
            LLVMValueRef nd_kp = LLVMBuildStructGEP2(ctx->builder, node_t, cur_nd, 1, "nkp");
            LLVMValueRef nd_k = LLVMBuildLoad2(ctx->builder, key_lt, nd_kp, "nk");
            LLVMValueRef keq;
            MAP_EMIT_KEY_EQ(nd_k, p_key, keq);
            LLVMBuildCondBr(ctx->builder, keq, do_rm, adv_rm);

            LLVMPositionBuilderAtEnd(ctx->builder, do_rm);
            LLVMValueRef nx_p = LLVMBuildStructGEP2(ctx->builder, node_t, cur_nd, 3, "nxp");
            LLVMValueRef nx_v = LLVMBuildLoad2(ctx->builder, ptr_t, nx_p, "nxv");
            LLVMValueRef prev_v = LLVMBuildLoad2(ctx->builder, ptr_t, prev_alloca, "pv");
            LLVMValueRef prev_null = LLVMBuildICmp(ctx->builder, LLVMIntEQ, prev_v, LLVMConstNull(ptr_t), "pnull");
            LLVMBasicBlockRef upd_slot = LLVMAppendBasicBlockInContext(ctx->context, fn_r, "updslot");
            LLVMBasicBlockRef upd_prev = LLVMAppendBasicBlockInContext(ctx->context, fn_r, "updprev");
            LLVMBasicBlockRef free_nd = LLVMAppendBasicBlockInContext(ctx->context, fn_r, "freend");
            LLVMBuildCondBr(ctx->builder, prev_null, upd_slot, upd_prev);
            LLVMPositionBuilderAtEnd(ctx->builder, upd_slot);
            LLVMBuildStore(ctx->builder, nx_v, slot_p);
            LLVMBuildBr(ctx->builder, free_nd);
            LLVMPositionBuilderAtEnd(ctx->builder, upd_prev);
            LLVMValueRef prev_nx_p = LLVMBuildStructGEP2(ctx->builder, node_t, prev_v, 3, "pnxp");
            LLVMBuildStore(ctx->builder, nx_v, prev_nx_p);
            LLVMBuildBr(ctx->builder, free_nd);

            LLVMPositionBuilderAtEnd(ctx->builder, free_nd);
            /* Free key */
            LLVMValueRef fk_v = LLVMBuildLoad2(ctx->builder, key_lt, nd_kp, "fkv");
            MAP_EMIT_FREE_KEY(fk_v);
            /* Free val */
            LLVMValueRef nd_vp = LLVMBuildStructGEP2(ctx->builder, node_t, cur_nd, 2, "ndvp");
            if (val_is_str)
            {
                emit_string_free(ctx, nd_vp);
            }
            else if (val_is_struct_drop)
            {
                LLVMValueRef vdrop = (LLVMValueRef)val_type->as.strukt.drop_fn;
                if (vdrop)
                {
#if CG_DEBUG
                    {
                        char _dfmt[128];
                        snprintf(_dfmt, sizeof(_dfmt), "[cg] struct.drop   val(map:remove) type=%s\n",
                                 val_type->as.strukt.name ? val_type->as.strukt.name : "?");
                        cg_emit_debug_printf(ctx, _dfmt, NULL, 0);
                    }
#endif
                    LLVMTypeRef vdft = LLVMGlobalGetValueType(vdrop);
                    LLVMBuildCall2(ctx->builder, vdft, vdrop, &nd_vp, 1, "");
                }
            }
            cg_emit_free(ctx, cur_nd, "map.scope_drop", CG_LINE(ctx), CG_COL(ctx));
            /* map.len-- */
            LLVMValueRef lp2 = LLVMBuildStructGEP2(ctx->builder, map_t, p_map, 1, "lp2");
            LLVMValueRef lv2 = LLVMBuildLoad2(ctx->builder, i32_t, lp2, "lv2");
            LLVMValueRef ldec = LLVMBuildSub(ctx->builder, lv2, LLVMConstInt(i32_t, 1, 0), "ldec");
            LLVMBuildStore(ctx->builder, ldec, lp2);
#if CG_DEBUG
            {
                LLVMValueRef da[] = {ldec};
                cg_emit_debug_printf(ctx, "[cg] map.remove  new_len=%d\n", da, 1);
            }
#endif
            LLVMBuildRetVoid(ctx->builder);

            LLVMPositionBuilderAtEnd(ctx->builder, adv_rm);
            LLVMBuildStore(ctx->builder, cur_nd, prev_alloca);
            LLVMValueRef nx2_p = LLVMBuildStructGEP2(ctx->builder, node_t, cur_nd, 3, "nx2p");
            LLVMValueRef nx2_v = LLVMBuildLoad2(ctx->builder, ptr_t, nx2_p, "nx2v");
            LLVMBuildStore(ctx->builder, nx2_v, cur_alloca);
            LLVMBuildBr(ctx->builder, loop_cond);

            LLVMPositionBuilderAtEnd(ctx->builder, ret_bb);
            LLVMBuildRetVoid(ctx->builder);
            (void)cur_fn;
        }
    }

    /* ==== 6. __ls_map_XX_YY_clear(*LsMap) -> void
          Walk all buckets, free every node's key/val and the node itself,
          then zero out the len field. Does NOT free the bucket array
          (that is done by drop). Safe to call multiple times. ==== */
    {
        LLVMValueRef fn_cl = LLVMGetNamedFunction(ctx->module, nm_clear);
        if (fn_cl == NULL)
        {
            LLVMTypeRef ftype_cl = LLVMFunctionType(void_t, &ptr_t, 1, 0);
            fn_cl = LLVMAddFunction(ctx->module, nm_clear, ftype_cl);
        }
        if (LLVMCountBasicBlocks(fn_cl) == 0)
        {
            LLVMValueRef p_map = LLVMGetParam(fn_cl, 0);
            LLVMValueRef cur_fn = fn_cl;

            /* entry block: check cap==0 */
            LLVMBasicBlockRef bb_entry = LLVMAppendBasicBlockInContext(ctx->context, fn_cl, "entry");
            LLVMPositionBuilderAtEnd(ctx->builder, bb_entry);

            /* Allocate loop counter in entry */
            LLVMValueRef bi_a2 = LLVMBuildAlloca(ctx->builder, i64_t, "bi");
            LLVMValueRef nd_a2 = LLVMBuildAlloca(ctx->builder, ptr_t, "nd");

            LLVMValueRef mv2 = LLVMBuildLoad2(ctx->builder, map_t, p_map, "mv");
            LLVMValueRef cap2 = LLVMBuildExtractValue(ctx->builder, mv2, 2, "cap");
            LLVMValueRef bkts2 = LLVMBuildExtractValue(ctx->builder, mv2, 0, "bkts");
            LLVMValueRef cap64_2 = LLVMBuildZExt(ctx->builder, cap2, i64_t, "c64");
            LLVMValueRef czero2 = LLVMBuildICmp(ctx->builder, LLVMIntEQ, cap2, LLVMConstInt(i32_t, 0, 0), "cz");

            LLVMBasicBlockRef bb_ret = LLVMAppendBasicBlockInContext(ctx->context, fn_cl, "ret");
            LLVMBasicBlockRef bb_lcond = LLVMAppendBasicBlockInContext(ctx->context, fn_cl, "l.cond");
            LLVMBuildCondBr(ctx->builder, czero2, bb_ret, bb_lcond);

            /* Bucket loop: for bi in 0..cap */
            LLVMPositionBuilderAtEnd(ctx->builder, bb_lcond);
            LLVMBuildStore(ctx->builder, LLVMConstInt(i64_t, 0, 0), bi_a2);
            LLVMBasicBlockRef bb_ltest = LLVMAppendBasicBlockInContext(ctx->context, fn_cl, "l.test");
            LLVMBasicBlockRef bb_lbody = LLVMAppendBasicBlockInContext(ctx->context, fn_cl, "l.body");
            LLVMBasicBlockRef bb_lnext = LLVMAppendBasicBlockInContext(ctx->context, fn_cl, "l.next");
            LLVMBasicBlockRef bb_ldone = LLVMAppendBasicBlockInContext(ctx->context, fn_cl, "l.done");
            LLVMBuildBr(ctx->builder, bb_ltest);

            LLVMPositionBuilderAtEnd(ctx->builder, bb_ltest);
            LLVMValueRef bi2 = LLVMBuildLoad2(ctx->builder, i64_t, bi_a2, "bi");
            LLVMValueRef blt2 = LLVMBuildICmp(ctx->builder, LLVMIntSLT, bi2, cap64_2, "blt");
            LLVMBuildCondBr(ctx->builder, blt2, bb_lbody, bb_ldone);

            LLVMPositionBuilderAtEnd(ctx->builder, bb_lbody);
            LLVMValueRef slot2_p = LLVMBuildGEP2(ctx->builder, ptr_t, bkts2, &bi2, 1, "sp2");
            LLVMValueRef head2 = LLVMBuildLoad2(ctx->builder, ptr_t, slot2_p, "hd2");
            LLVMBuildStore(ctx->builder, head2, nd_a2);

            /* Node loop for this bucket */
            LLVMBasicBlockRef bb_ncond = LLVMAppendBasicBlockInContext(ctx->context, fn_cl, "n.cond");
            LLVMBasicBlockRef bb_nbody = LLVMAppendBasicBlockInContext(ctx->context, fn_cl, "n.body");
            LLVMBasicBlockRef bb_nend = LLVMAppendBasicBlockInContext(ctx->context, fn_cl, "n.end");
            LLVMBuildBr(ctx->builder, bb_ncond);

            LLVMPositionBuilderAtEnd(ctx->builder, bb_ncond);
            LLVMValueRef nd2 = LLVMBuildLoad2(ctx->builder, ptr_t, nd_a2, "nd");
            LLVMValueRef nd_null2 = LLVMBuildICmp(ctx->builder, LLVMIntEQ, nd2, LLVMConstNull(ptr_t), "ndn");
            LLVMBuildCondBr(ctx->builder, nd_null2, bb_nend, bb_nbody);

            LLVMPositionBuilderAtEnd(ctx->builder, bb_nbody);
            LLVMValueRef nx2_p2 = LLVMBuildStructGEP2(ctx->builder, node_t, nd2, 3, "nxp");
            LLVMValueRef nx2_v2 = LLVMBuildLoad2(ctx->builder, ptr_t, nx2_p2, "nxv");
            /* Free key */
            LLVMValueRef fk2_p = LLVMBuildStructGEP2(ctx->builder, node_t, nd2, 1, "fkp");
            LLVMValueRef fk2_v = LLVMBuildLoad2(ctx->builder, key_lt, fk2_p, "fkv");
            MAP_EMIT_FREE_KEY(fk2_v);
            /* Free val */
            LLVMValueRef fv2_p = LLVMBuildStructGEP2(ctx->builder, node_t, nd2, 2, "fvp");
            if (val_is_str)
                emit_string_free(ctx, fv2_p);
            else if (val_is_struct_drop)
            {
                LLVMValueRef vd2 = (LLVMValueRef)val_type->as.strukt.drop_fn;
                if (vd2)
                {
#if CG_DEBUG
                    {
                        char _dfmt2[128];
                        snprintf(_dfmt2, sizeof(_dfmt2), "[cg] struct.drop   val(map:clear) type=%s\n",
                                 val_type->as.strukt.name ? val_type->as.strukt.name : "?");
                        cg_emit_debug_printf(ctx, _dfmt2, NULL, 0);
                    }
#endif
                    LLVMTypeRef vdt2 = LLVMGlobalGetValueType(vd2);
                    LLVMBuildCall2(ctx->builder, vdt2, vd2, &fv2_p, 1, "");
                }
            }
            cg_emit_free(ctx, nd2, "map.scope_drop", CG_LINE(ctx), CG_COL(ctx));
            LLVMBuildStore(ctx->builder, nx2_v2, nd_a2);
            LLVMBuildBr(ctx->builder, bb_ncond);

            LLVMPositionBuilderAtEnd(ctx->builder, bb_nend);
            /* Zero out the slot */
            LLVMBuildStore(ctx->builder, LLVMConstNull(ptr_t), slot2_p);
            LLVMBuildBr(ctx->builder, bb_lnext);

            LLVMPositionBuilderAtEnd(ctx->builder, bb_lnext);
            LLVMValueRef bi2inc = LLVMBuildAdd(ctx->builder, bi2, LLVMConstInt(i64_t, 1, 0), "biinc");
            LLVMBuildStore(ctx->builder, bi2inc, bi_a2);
            LLVMBuildBr(ctx->builder, bb_ltest);

            LLVMPositionBuilderAtEnd(ctx->builder, bb_ldone);
            LLVMValueRef lp3 = LLVMBuildStructGEP2(ctx->builder, map_t, p_map, 1, "lp3");
            LLVMBuildStore(ctx->builder, LLVMConstInt(i32_t, 0, 0), lp3);
            LLVMBuildBr(ctx->builder, bb_ret);

            LLVMPositionBuilderAtEnd(ctx->builder, bb_ret);
            LLVMBuildRetVoid(ctx->builder);
            (void)cur_fn;
        }
    }

    /* ====================================================
       7. __ls_map_XX_YY_drop(*LsMap) -> void
          Clear all nodes AND free bucket array.
          Called by scope cleanup.
       ==================================================== */
    {
        /* Ensure clear function is ready */
        LLVMValueRef fn_cl2 = LLVMGetNamedFunction(ctx->module, nm_clear);
        LLVMTypeRef ft_cl2 = LLVMGlobalGetValueType(fn_cl2);

        LLVMValueRef fn_dp = LLVMGetNamedFunction(ctx->module, nm_drop);
        if (fn_dp == NULL)
        {
            LLVMTypeRef ftype = LLVMFunctionType(void_t, &ptr_t, 1, 0);
            fn_dp = LLVMAddFunction(ctx->module, nm_drop, ftype);
        }
        if (LLVMCountBasicBlocks(fn_dp) == 0)
        {
            LLVMValueRef p_map = LLVMGetParam(fn_dp, 0);
            LLVMBasicBlockRef bb_entry = LLVMAppendBasicBlockInContext(ctx->context, fn_dp, "entry");
            LLVMBasicBlockRef bb_skip = LLVMAppendBasicBlockInContext(ctx->context, fn_dp, "skip");
            LLVMBasicBlockRef bb_do = LLVMAppendBasicBlockInContext(ctx->context, fn_dp, "do");
            LLVMBasicBlockRef bb_ret = LLVMAppendBasicBlockInContext(ctx->context, fn_dp, "ret");

            LLVMPositionBuilderAtEnd(ctx->builder, bb_entry);
            LLVMValueRef mv3 = LLVMBuildLoad2(ctx->builder, map_t, p_map, "mv");
            LLVMValueRef cap3 = LLVMBuildExtractValue(ctx->builder, mv3, 2, "cap");
            LLVMValueRef czero3 = LLVMBuildICmp(ctx->builder, LLVMIntEQ, cap3, LLVMConstInt(i32_t, 0, 0), "cz");
            LLVMBuildCondBr(ctx->builder, czero3, bb_skip, bb_do);

            LLVMPositionBuilderAtEnd(ctx->builder, bb_skip);
            LLVMBuildBr(ctx->builder, bb_ret);

            LLVMPositionBuilderAtEnd(ctx->builder, bb_do);
            /* call clear(map) */
            LLVMBuildCall2(ctx->builder, ft_cl2, fn_cl2, &p_map, 1, "");
            /* free(buckets) */
            LLVMValueRef bkts3 = LLVMBuildExtractValue(ctx->builder, mv3, 0, "bkts");
            cg_emit_free(ctx, bkts3, "map.scope_drop", CG_LINE(ctx), CG_COL(ctx));
            /* Zero out the map struct */
            LLVMValueRef bkts_p3 = LLVMBuildStructGEP2(ctx->builder, map_t, p_map, 0, "bktp");
            LLVMValueRef len_p3 = LLVMBuildStructGEP2(ctx->builder, map_t, p_map, 1, "lenp");
            LLVMValueRef cap_p3 = LLVMBuildStructGEP2(ctx->builder, map_t, p_map, 2, "capp");
            LLVMBuildStore(ctx->builder, LLVMConstNull(ptr_t), bkts_p3);
            LLVMBuildStore(ctx->builder, LLVMConstInt(i32_t, 0, 0), len_p3);
            LLVMBuildStore(ctx->builder, LLVMConstInt(i32_t, 0, 0), cap_p3);
#if CG_DEBUG
            cg_emit_debug_printf(ctx, "[cg] map.drop   freed buckets\n", NULL, 0);
#endif
            LLVMBuildBr(ctx->builder, bb_ret);

            LLVMPositionBuilderAtEnd(ctx->builder, bb_ret);
            LLVMBuildRetVoid(ctx->builder);
        }
    }

#undef MAP_EMIT_HASH
#undef MAP_EMIT_KEY_EQ
#undef MAP_EMIT_COPY_KEY
#undef MAP_EMIT_FREE_KEY
#undef MAP_EMIT_COPY_VAL
#undef MAP_EMIT_FREE_VAL

    if (saved_bb)
        LLVMPositionBuilderAtEnd(ctx->builder, saved_bb);
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

    declare_builtins(ctx);

    /* Memcheck: install internal @malloc/@free wrappers BEFORE any helper
       fn body is emitted, so all subsequent calls route through the tracker.
       declare_builtins above declared malloc/free as externs; the wrapper
       installer will rename those externs and shadow them with internals. */
    cg_install_memcheck_wrappers(ctx);

    emit_str_replace_helper(ctx);
    emit_str_split_helper(ctx);
    emit_str_join_helper(ctx);
    emit_map_hash_s_helper(ctx);

    /* Process imported modules: forward-declare and compile their declarations */
    if (registry)
    {
        for (int m = 0; m < registry->count; m++)
        {
            AstNode *mod_ast = registry->modules[m].ast;
            if (mod_ast == NULL || mod_ast->kind != AST_PROGRAM)
                continue;

            /* Forward-declare imported module's structs and functions */
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
                    if (fn_type_ml && fn_type_ml->kind == TYPE_FUNCTION)
                    {
                        LLVMTypeRef fn_type = type_to_llvm(ctx, fn_type_ml);
                        if (!LLVMGetNamedFunction(ctx->module, decl->as.fn_decl.name))
                        {
                            LLVMValueRef fn_g = LLVMAddFunction(ctx->module,
                                decl->as.fn_decl.name, fn_type);
                            /* Phase E.4: imported user-module functions get
                               internal linkage so their symbols don't collide
                               with libc/runtime names at AOT link time
                               (e.g. stdlib/io.ls's `fn remove` would otherwise
                               clash with libucrt.lib's `remove`). All inter-LS
                               calls happen within the same LLVM module so
                               internal linkage is sufficient. */
                            LLVMSetLinkage(fn_g, LLVMInternalLinkage);
                        }
                    }
                }
            }

            /* Forward-declare imported module's global variables */
            for (int i = 0; i < mod_ast->as.program.decl_count; i++)
            {
                AstNode *decl = mod_ast->as.program.decls[i];
                if (decl->kind == AST_VAR_DECL && decl->resolved_type)
                {
                    Type *var_type = decl->resolved_type;
                    LLVMTypeRef llvm_type = type_to_llvm(ctx, var_type);
                    if (!LLVMGetNamedGlobal(ctx->module, decl->as.var_decl.name))
                    {
                        LLVMValueRef gv = LLVMAddGlobal(ctx->module, llvm_type,
                                                        decl->as.var_decl.name);
                        LLVMSetLinkage(gv, LLVMExternalLinkage);
                        LLVMSetInitializer(gv, LLVMConstNull(llvm_type));
                    }
                }
            }

            /* Generate function bodies for imported module */
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
            }
        }
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
            int saved_temp_count = ctx->temp_string_count;
            ctx->temp_string_count = 0;

            /* Push a local scope so any variables declared inside top-level statements
               live in their own scope and get cleaned up on return.
               Global variables were already defined in ctx->current_scope (outer) during
               Pass 1 and remain accessible through the scope chain. */
            push_scope(ctx);

            /* 1. Initialise imported module globals first (preserve import order) */
            if (registry)
            {
                for (int m = 0; m < registry->count; m++)
                {
                    AstNode *mod_ast = registry->modules[m].ast;
                    if (mod_ast == NULL || mod_ast->kind != AST_PROGRAM)
                        continue;
                    for (int i = 0; i < mod_ast->as.program.decl_count; i++)
                    {
                        AstNode *d = mod_ast->as.program.decls[i];
                        if (d->kind == AST_VAR_DECL && d->as.var_decl.init)
                            emit_global_var_init(ctx, d);
                    }
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
            ctx->temp_string_count = saved_temp_count;
        }
    }

    /* Generate __ls_global_cleanup: free all global dynamic string variables (BUG #1 fix).
       Called just before main() returns so global strings don't leak at program exit. */
    {
        /* Collect all global VAR_DECLs that need cleanup (string or vec) */
        bool has_global_cleanup = false;

#define DECL_NEEDS_GLOBAL_CLEANUP(d)                    \
    ((d)->kind == AST_VAR_DECL && (d)->resolved_type && \
     ((d)->resolved_type->kind == TYPE_STRING ||        \
      (d)->resolved_type->kind == TYPE_VECTOR))

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
            int saved_tc2 = ctx->temp_string_count;
            ctx->temp_string_count = 0;

/* Helper macro: emit cleanup for one global var decl */
#define EMIT_GLOBAL_CLEANUP(decl)                                                                        \
    do                                                                                                   \
    {                                                                                                    \
        if ((decl)->kind != AST_VAR_DECL || !(decl)->resolved_type)                                      \
            break;                                                                                       \
        LLVMValueRef gv = LLVMGetNamedGlobal(ctx->module, (decl)->as.var_decl.name);                     \
        if (!gv)                                                                                         \
            break;                                                                                       \
        if ((decl)->resolved_type->kind == TYPE_STRING)                                                  \
        {                                                                                                \
            /* Simple cleanup: free if cap > 0 */                                                        \
            LLVMTypeRef str_type = ls_string_type(ctx);                                                  \
            LLVMValueRef str_val = LLVMBuildLoad2(ctx->builder, str_type, gv, "gcs.str");                \
            LLVMValueRef cap = LLVMBuildExtractValue(ctx->builder, str_val, 2, "gcs.cap");               \
            LLVMValueRef zero = LLVMConstInt(LLVMInt32TypeInContext(ctx->context), 0, 0);                \
            LLVMValueRef is_owned = LLVMBuildICmp(ctx->builder, LLVMIntSGT, cap, zero, "gcs.owned");     \
            LLVMBasicBlockRef cur_fn = LLVMGetBasicBlockParent(LLVMGetInsertBlock(ctx->builder));        \
            LLVMBasicBlockRef free_bb = LLVMAppendBasicBlockInContext(ctx->context, cur_fn, "gcs.free"); \
            LLVMBasicBlockRef skip_bb = LLVMAppendBasicBlockInContext(ctx->context, cur_fn, "gcs.skip"); \
            LLVMBuildCondBr(ctx->builder, is_owned, free_bb, skip_bb);                                   \
            LLVMPositionBuilderAtEnd(ctx->builder, free_bb);                                             \
            LLVMValueRef data = LLVMBuildExtractValue(ctx->builder, str_val, 0, "gcs.data");             \
            cg_emit_free(ctx, data, "string.scope_drop", CG_LINE(ctx), CG_COL(ctx));                      \
            LLVMBuildBr(ctx->builder, skip_bb);                                                          \
            LLVMPositionBuilderAtEnd(ctx->builder, skip_bb);                                             \
            LLVMBuildBr(ctx->builder, skip_bb);                                                          \
        }                                                                                                \
    } while (0)

            /* Free imported module globals first (reverse of init order) */
            if (registry)
            {
                for (int m = 0; m < registry->count; m++)
                {
                    AstNode *mod_ast = registry->modules[m].ast;
                    if (mod_ast == NULL || mod_ast->kind != AST_PROGRAM)
                        continue;
                    // for (int i = 0; i < mod_ast->as.program.decl_count; i++) {
                    //     EMIT_GLOBAL_CLEANUP(mod_ast->as.program.decls[i]);
                    // }
                }
            }
            /* Free main module globals */
            // for (int i = 0; i < ast->as.program.decl_count; i++) {
            //     EMIT_GLOBAL_CLEANUP(ast->as.program.decls[i]);
            // }

#undef EMIT_GLOBAL_CLEANUP
#undef DECL_NEEDS_GLOBAL_CLEANUP

            LLVMBuildRetVoid(ctx->builder);
            ctx->current_fn = saved_fn2;
            ctx->temp_string_count = saved_tc2;
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
            LLVMTypeRef main_ft = LLVMFunctionType(i32_t, NULL, 0, 0);
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
    /* Run optimization passes */
    LLVMRunPasses(ctx->module, "default<O2>", ctx->target_machine,
                  LLVMCreatePassBuilderOptions());

    char *error = NULL;
    if (LLVMTargetMachineEmitToFile(ctx->target_machine, ctx->module,
                                    (char *)output_path,
                                    LLVMObjectFile, &error))
    {
        fprintf(stderr, "error emitting object: %s\n", error);
        LLVMDisposeMessage(error);
        return -1;
    }
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
