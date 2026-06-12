/* codegen.c — AST to LLVM IR code generation */
#include "codegen.h"
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
static void cg_module_fn_symbol(char *out, size_t cap,
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

/* B-2: Get the LLVM-level type name for a struct/enum Type.
   For module-defined types this is the "<mod>__Name" prefixed name;
   for root-defined types it falls back to the bare name. */
static inline const char *struct_llvm_name(const Type *t)
{
    return (t->as.strukt.llvm_name) ? t->as.strukt.llvm_name : t->as.strukt.name;
}
static inline const char *enum_llvm_name_of(const Type *t)
{
    return (t->as.enom.llvm_name) ? t->as.enom.llvm_name : t->as.enom.name;
}

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

/* ---- F.6: CG_DEBUG helper wrappers for closure operations ----
   All helpers are no-ops when CG_DEBUG==0; the compiler eliminates them.
   When CG_DEBUG==1 they emit a runtime printf via cg_emit_debug_printf.   */

/* Log a capture decision (one line per captured variable). */
static void cg_dbg_capture(CodegenContext *ctx,
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
static void cg_dbg_outer_mark(CodegenContext *ctx,
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
static void cg_dbg_env_alloc(CodegenContext *ctx, int closure_id,
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
static void cg_dbg_block_op(CodegenContext *ctx,
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
/* Clone a has_drop enum VALUE: deep-copy string/struct payload fields, returns new enum value */
static LLVMValueRef emit_enum_clone_val(CodegenContext *ctx, LLVMValueRef enum_val,
                                        Type *enum_type);
/* Clone an array VALUE: deep-copy each has_drop element, returns new array value */
static LLVMValueRef emit_array_clone_val(CodegenContext *ctx, LLVMValueRef arr_val,
                                         LLVMTypeRef llvm_arr_type, Type *arr_type);
/* Unified value/element deep-clone dispatcher (string/vec/has_drop struct/enum). */
static LLVMValueRef emit_clone_value(CodegenContext *ctx, LLVMValueRef val,
                                     LLVMTypeRef llvm_type, Type *type);
static void mark_string_moved(CodegenContext *ctx, LLVMValueRef str_alloca,
                              const char *reason);
static void cg_mark_last_temp_moved(CodegenContext *ctx, int mark, const char *reason);
static AstNode *cg_match_arm_tail(AstNode *arm_body);
static LLVMValueRef cg_match_arm_own_tail(CodegenContext *ctx, AstNode *tail,
                                          LLVMValueRef body_val, LLVMTypeRef res_llvm,
                                          Type *result_type, int str_mark, int drop_floor,
                                          bool did_move_out_binder);
static void cg_match_arm_encapsulate(CodegenContext *ctx, int str_mark, int drop_floor,
                                     Type *result_type);
static void emit_scope_cleanup(CodegenContext *ctx);
static void emit_cleanup_to(CodegenContext *ctx, CgScope *stop, LLVMValueRef skip_alloca);
/* Unified value-drop authority: free heap owned by the value stored at `place_ptr`
   (a pointer to storage of `type`). Recurses through containers; POD/non-drop is a
   no-op. The drop-side counterpart of emit_clone_value. */
static void emit_drop_value(CodegenContext *ctx, LLVMValueRef place_ptr, Type *type);
/* True if `t` owns heap and needs drop/clone (string/vec/map/has_drop struct|enum). */
static bool cg_type_owns_heap_for_enum(const Type *t);
static void emit_struct_drop(CodegenContext *ctx, LLVMValueRef drop_ptr, Type *struct_type);
static void emit_auto_drop_fn(CodegenContext *ctx, Type *struct_type);
static void emit_struct_drop_cond(CodegenContext *ctx, LLVMValueRef drop_ptr,
                                  Type *struct_type, LLVMValueRef moved_flag);
static LLVMBasicBlockRef emit_struct_drop_separate(CodegenContext *ctx, LLVMValueRef drop_ptr,
                                                   Type *struct_type, LLVMValueRef moved_flag);
static void emit_drop_field_cleanup(CodegenContext *ctx);
LLVMTypeRef type_to_llvm(CodegenContext *ctx, Type *t);
LLVMValueRef codegen_expr(CodegenContext *ctx, AstNode *node);
static LLVMValueRef cg_entry_alloca(CodegenContext *ctx, LLVMTypeRef ty, const char *name);
static LLVMValueRef emit_user_from_list_value(CodegenContext *ctx, Type *struct_type,
                                              AstNode *lit);
static LLVMTypeRef build_variant_payload_struct(CodegenContext *ctx, Type *enum_type, int variant_idx);
static void cg_enum_payload_dims(CodegenContext *ctx, Type *et, int *out_size, int *out_align);
static void cg_enum_body_fields(CodegenContext *ctx, int max_payload, int max_align, LLVMTypeRef body_out[2]);
static LLVMValueRef emit_enum_ctor(CodegenContext *ctx, AstNode *node,
                                   Type *enum_type, int variant_idx,
                                   AstNode **args, int arg_count);
static void emit_auto_enum_drop_fn(CodegenContext *ctx, Type *enum_type);
static void emit_auto_enum_clone_fn(CodegenContext *ctx, Type *enum_type);
static void emit_enum_drop(CodegenContext *ctx, LLVMValueRef enum_ptr, Type *enum_type);
static void emit_enum_drop_cond(CodegenContext *ctx, LLVMValueRef enum_ptr,
                                Type *enum_type, LLVMValueRef moved_flag);
/* Phase B closures: lifted-fn synthesiser + indirect-call lowering. */
static LLVMValueRef codegen_closure_literal(CodegenContext *ctx, AstNode *node);
static LLVMValueRef codegen_block_call(CodegenContext *ctx, AstNode *node);
static LLVMValueRef codegen_fn_to_block(CodegenContext *ctx, AstNode *node);

/* M-3: 统一所有权转移 API */
typedef enum {
    CG_XFER_INTO_CONTAINER,  /* vec.push / vec[i]= / enum ctor / struct ctor */
    CG_XFER_ASSIGN_VAR,      /* string a = b（clone 语义，source 保持有效） */
    CG_XFER_RETURN,          /* return val */
} CgTransferKind;
static void cg_store_owned(CodegenContext *ctx,
                           LLVMValueRef dst_ptr,
                           LLVMValueRef val,
                           Type *type,
                           AstNode *source,
                           int temp_mark,
                           CgTransferKind kind);

static LLVMValueRef cg_declare_pending_generic_method(CodegenContext *ctx,
                                                      const char *name)
{
    LLVMValueRef existing = LLVMGetNamedFunction(ctx->module, name);
    if (existing != NULL)
        return existing;

    for (int i = 0; i < ctx->pending_gm_count; i++)
    {
        if (strcmp(ctx->pending_generic_methods[i].mangled_name, name) != 0)
            continue;

        AstNode *cfn = ctx->pending_generic_methods[i].cloned_fn;
        if (cfn == NULL || cfn->resolved_type == NULL ||
            cfn->resolved_type->kind != TYPE_FUNCTION)
            return NULL;

        LLVMTypeRef fn_type = type_to_llvm(ctx, cfn->resolved_type);
        return LLVMAddFunction(ctx->module, name, fn_type);
    }

    return NULL;
}

static LLVMValueRef cg_ensure_user_struct_drop_decl(CodegenContext *ctx,
                                                    Type *struct_type)
{
    if (struct_type == NULL || struct_type->kind != TYPE_STRUCT)
        return NULL;

    char drop_name[256];
    snprintf(drop_name, sizeof(drop_name), "%s.__drop",
             struct_llvm_name(struct_type));

    LLVMValueRef drop_fn = LLVMGetNamedFunction(ctx->module, drop_name);
    if (drop_fn == NULL)
        drop_fn = cg_declare_pending_generic_method(ctx, drop_name);
    if (drop_fn == NULL)
    {
        LLVMTypeRef ptr_struct = LLVMPointerTypeInContext(ctx->context, 0);
        LLVMTypeRef fn_type = LLVMFunctionType(LLVMVoidTypeInContext(ctx->context),
                                               &ptr_struct, 1, 0);
        drop_fn = LLVMAddFunction(ctx->module, drop_name, fn_type);
    }
    LLVMSetFunctionCallConv(drop_fn, LLVMCCallConv);
    struct_type->as.strukt.drop_fn = drop_fn;
    return drop_fn;
}

/* Phase C.5/C.7: capture types that need release work in env_drop.
   Currently:
     TYPE_STRING — env_drop frees data when cap > 0 (cap is 0 when the
                   capture aliases a caller-owned string via the by-value
                   param-borrow ABI; non-zero when cloned/owned).
     TYPE_STRUCT(has_drop) — env_drop calls Struct.__drop on the slot. */
static inline bool capture_type_is_by_move_cg(const Type *t) {
    if (t == NULL) return false;
    switch (t->kind) {
    case TYPE_STRING: return true;
    case TYPE_STRUCT: return t->as.strukt.has_drop;
    case TYPE_ENUM:   return t->as.enom.has_drop;  /* F.5: has_drop enum → by-move */
    default:          return false;
    }
}

/* True for by-ref captures: env stores a pointer to the outer alloca.
   Only the removed builtin map used this; now always false. */
static inline bool capture_type_is_by_ref_cg(const Type *t) {
    (void)t;
    return false;
}

/* Whether marking the outer-as-moved is done via the cap idiom.
   Only string uses this (cap=-1). Struct and enum use moved_flag (i1 alloca).
   vec/map are now by-ref and never mark the outer moved. */
static inline bool capture_outer_marker_uses_cap(const Type *t) {
    if (t == NULL) return false;
    return t->kind == TYPE_STRING;
}

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
static LLVMValueRef cg_get_perf_now(CodegenContext *ctx) {
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

static void cg_emit_prof_enter(CodegenContext *ctx, const char *fn_name,
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

static void cg_emit_prof_leave(CodegenContext *ctx)
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

/* Allocate a stack slot in the CURRENT function's ENTRY block, regardless of
   where the builder currently sits. Bug #24/#26 family: a plain
   LLVMBuildAlloca at the current position, if that position is inside a loop
   body, allocates a fresh slot every iteration (LLVM allocas are only released
   on function return) → stack overflow. Entry-block allocas live once per call
   and are reused. Use this for any scratch slot created during expression /
   statement codegen (string method temps, loop indices, etc.). */
static LLVMValueRef cg_entry_alloca(CodegenContext *ctx, LLVMTypeRef ty, const char *name)
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

/* Emit a user-container list literal: `StructWithFromList v = [a, b]`.
   The checker guarantees `lit->resolved_type == struct_type` and that the
   struct has `__from_list(&!self, E)`. */
static LLVMValueRef emit_user_from_list_value(CodegenContext *ctx, Type *struct_type,
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

/* True iff `t` is the pure-LS `Str` struct (recognized by name, like Vec/Map). */
static bool cg_type_is_str(Type *t)
{
    return t != NULL && t->kind == TYPE_STRUCT && t->as.strukt.name != NULL &&
           strcmp(t->as.strukt.name, "Str") == 0;
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
    LLVMValueRef data = LLVMBuildGlobalStringPtr(ctx->builder, text, "Strlit");
    LLVMTypeRef i32 = LLVMInt32TypeInContext(ctx->context);
    LLVMValueRef len = LLVMConstInt(i32, (unsigned long long)strlen(text), 0);
    LLVMValueRef cap = LLVMConstInt(i32, 0, 0);
    LLVMValueRef v = LLVMGetUndef(st);
    v = LLVMBuildInsertValue(ctx->builder, v, data, 0, "Str.d");
    v = LLVMBuildInsertValue(ctx->builder, v, len, 1, "Str.l");
    v = LLVMBuildInsertValue(ctx->builder, v, cap, 2, "Str.c");
    return v;
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
    /* M-2: Zero out cap after free so any re-triggered cleanup sees cap=0
       (static/skip) and does NOT double-free.  This happens when a temp string
       alloca is freed once per loop iteration but the short-circuit exit path
       re-runs sf.cleanup on the same alloca (double-free regression introduced
       when struct-field strings became truly cloned, cap>0). */
    {
        LLVMValueRef str_zeroed = LLVMBuildInsertValue(
            ctx->builder, str_val,
            LLVMConstInt(i32_type, 0, 0), 2, "sf.zerocp");
        LLVMBuildStore(ctx->builder, str_zeroed, str_alloca);
    }
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

    /* need_grow = (s_cap <= 0) || (new_len + 1 > s_cap)
       M-2: cap <= 0 covers both LS_CAP_STATIC(0) and LS_CAP_BORROWED(-2); both
       require a fresh malloc (we cannot realloc a literal or a borrowed buffer). */
    LLVMValueRef zero32 = LLVMConstInt(i32_type, 0, 0);
    LLVMValueRef one32 = LLVMConstInt(i32_type, 1, 0);
    LLVMValueRef is_static = LLVMBuildICmp(ctx->builder, LLVMIntSLE, s_cap, zero32, "sa.isst");
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
   If cap > 0  (LS_CAP_STATIC > 0, owned):  malloc + memcpy → new independent LsString.
   If cap == 0 (LS_CAP_STATIC, static literal): returned as-is (.rodata never freed).
   If cap == -1 (LS_CAP_MOVED):  returned as-is (shouldn't happen in healthy code).
   If cap == -2 (LS_CAP_BORROWED): malloc + memcpy → caller owns original, we own copy.
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

    /* M-2: clone if cap > 0 (owned) OR cap == -2 (borrowed).
       Static (cap=0) and moved (cap=-1) are passed through unchanged. */
    LLVMValueRef zero32   = LLVMConstInt(i32_type, 0, 0);
    LLVMValueRef neg2_32  = LLVMConstInt(i32_type, (unsigned long long)LS_CAP_BORROWED, 0);
    LLVMValueRef is_pos   = LLVMBuildICmp(ctx->builder, LLVMIntSGT, cap, zero32,  "sc.owned");
    LLVMValueRef is_borrow= LLVMBuildICmp(ctx->builder, LLVMIntEQ,  cap, neg2_32, "sc.borr");
    LLVMValueRef is_owned = LLVMBuildOr(ctx->builder, is_pos, is_borrow, "sc.need_clone");

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

    /* User-defined __clone hook: if the struct provides `fn __clone(&self) -> Self`,
       call it instead of field-wise auto-clone. Required for structs that own heap
       through a raw *T pointer field (e.g. a hand-written container): the compiler
       cannot auto-deep-clone a raw pointer, so the user supplies the deep copy.
       Symmetric with the user __drop override. */
    {
        char clone_fn_name[256];
        snprintf(clone_fn_name, sizeof(clone_fn_name), "%s.__clone",
                 struct_llvm_name(struct_type));
        LLVMValueRef user_clone = LLVMGetNamedFunction(ctx->module, clone_fn_name);
        if (user_clone == NULL)
            user_clone = cg_declare_pending_generic_method(ctx, clone_fn_name);
        if (user_clone == NULL && struct_type->as.strukt.has_user_clone)
        {
            /* Concrete (non-generic) user __clone from an imported module whose
               body hasn't been emitted yet in this LLVM module (module functions
               can emit before the defining std module). Forward-declare
               `<llvm_name>.__clone : Struct (ptr)` — JIT/AOT linking resolves it
               to the real definition. Falling through to the field-wise
               auto-clone here would shallow-copy raw *T buffers and double-free
               (hit by Str-by-value args inside module functions). */
            LLVMTypeRef ptr_t = LLVMPointerTypeInContext(ctx->context, 0);
            LLVMTypeRef ucfn_t = LLVMFunctionType(llvm_struct_type, &ptr_t, 1, 0);
            user_clone = LLVMAddFunction(ctx->module, clone_fn_name, ucfn_t);
        }
        if (user_clone != NULL)
        {
            /* __clone(&self): spill the value to a temp to get a self pointer. */
            LLVMValueRef self_tmp = cg_entry_alloca(ctx, llvm_struct_type, "uc.self");
            LLVMBuildStore(ctx->builder, struct_val, self_tmp);
            LLVMTypeRef ft = LLVMGlobalGetValueType(user_clone);
            return LLVMBuildCall2(ctx->builder, ft, user_clone, &self_tmp, 1, "uc.r");
        }
    }

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

        /* Deep-clone every heap-owning field so the cloned struct owns
           independent buffers. Without this, a shallow-copied vec/map/string
           field shares the caller's heap → callee scope_drop + caller
           scope_drop double-free (e.g. by-value `struct { vec(int) }` arg). */
        bool field_needs_clone =
            ft->kind == TYPE_STRING ||
            (ft->kind == TYPE_STRUCT && ft->as.strukt.has_drop) ||
            (ft->kind == TYPE_ENUM && ft->as.enom.has_drop);
        if (!field_needs_clone)
            continue;

        LLVMValueRef field_val = LLVMBuildExtractValue(ctx->builder, result,
                                                       (unsigned)fi, "sc.fld");
        LLVMTypeRef ft_llvm = type_to_llvm(ctx, ft);
        LLVMValueRef cloned = emit_clone_value(ctx, field_val, ft_llvm, ft);
        result = LLVMBuildInsertValue(ctx->builder, result, cloned,
                                      (unsigned)fi, "sc.ins");
    }

    (void)llvm_struct_type; /* used implicitly through extractValue field indices */
    return result;
}

/* Clone a has_drop enum VALUE: perform a bitwise copy first (already embedded in the value),
   then deep-clone any string / has_drop-struct / has_drop-enum payload fields by patching
   them in-place via a temp alloca.
   Self-recursive (boxed) payload fields are left as shallow copies — cloning a full
   recursive tree requires additional infrastructure not needed for the common case. */
static LLVMValueRef emit_enum_clone_val(CodegenContext *ctx,
                                        LLVMValueRef enum_val,
                                        Type *enum_type)
{
    if (enum_type == NULL || enum_type->kind != TYPE_ENUM)
        return enum_val;
    if (!enum_type->as.enom.has_drop)
        return enum_val;

    /* Delegate to the named __clone function to avoid infinite inline
       recursion for self-referential enums (e.g. JsonValue with
       Arr(vec(JsonValue)) or Obj(map(string, JsonValue))). */
    emit_auto_enum_clone_fn(ctx, enum_type);
    LLVMValueRef clone_fn = (LLVMValueRef)enum_type->as.enom.clone_fn;
    if (clone_fn == NULL)
        return enum_val; /* no heap fields — bitwise copy */

    /* clone_fn signature: enum_t __clone(ptr self_ptr) */
    LLVMTypeRef enum_llvm = type_to_llvm(ctx, enum_type);

    /* Allocate in entry block so the alloca dominates all uses.
       emit_enum_clone_val can be called inside loops (e.g. vec clone
       loop in __clone), and LLJIT may miscompile allocas placed in
       non-dominating blocks for large struct types like JsonValue
       (33 bytes passed by sret). */
    LLVMValueRef cur_fn = LLVMGetBasicBlockParent(LLVMGetInsertBlock(ctx->builder));
    LLVMBuilderRef eb = LLVMCreateBuilderInContext(ctx->context);
    LLVMBasicBlockRef entry_bb = LLVMGetEntryBasicBlock(cur_fn);
    LLVMValueRef fi = LLVMGetFirstInstruction(entry_bb);
    if (fi)
        LLVMPositionBuilderBefore(eb, fi);
    else
        LLVMPositionBuilderAtEnd(eb, entry_bb);
    LLVMValueRef tmp = LLVMBuildAlloca(eb, enum_llvm, "ec.tmp");
    LLVMDisposeBuilder(eb);

    LLVMBuildStore(ctx->builder, enum_val, tmp);
    LLVMTypeRef clone_ft = LLVMGlobalGetValueType(clone_fn);
    LLVMValueRef result = LLVMBuildCall2(ctx->builder, clone_ft, clone_fn, &tmp, 1, "ec.r");
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
                            (elem_type->kind == TYPE_STRUCT && elem_type->as.strukt.has_drop) ||
                            (elem_type->kind == TYPE_ENUM && elem_type->as.enom.has_drop);
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
        else if (elem_type->kind == TYPE_ENUM && elem_type->as.enom.has_drop)
            cloned = emit_enum_clone_val(ctx, elem, elem_type);
        else
            cloned = emit_struct_clone_val(ctx, elem, elem_llvm, elem_type);
        result = LLVMBuildInsertValue(ctx->builder, result, cloned,
                                      (unsigned)i, "ac.ins");
    }

    (void)llvm_arr_type;
    return result;
}

/* Unified element/value deep-clone dispatcher. Returns an independently-owned
   copy for heap-owning types (string / vec / has_drop struct / has_drop enum);
   returns the value unchanged for POD (and map/array, which keep their current
   shallow behavior). Centralizes the clone logic that was inlined at many vec
   element-read sites, where a vec element previously fell through to a shallow
   copy → double-free on nested vec(vec(...)). */
static LLVMValueRef emit_clone_value(CodegenContext *ctx, LLVMValueRef val,
                                     LLVMTypeRef llvm_type, Type *type)
{
    if (type == NULL) return val;
    switch (type->kind)
    {
    case TYPE_STRING: return emit_string_clone_val(ctx, val);
    case TYPE_ENUM:
        return type->as.enom.has_drop ? emit_enum_clone_val(ctx, val, type) : val;
    case TYPE_STRUCT:
        return type->as.strukt.has_drop
                   ? emit_struct_clone_val(ctx, val, llvm_type, type) : val;
    default: return val;
    }
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
    /* Zero-initialize the slot so that if cleanup runs on a code path
       where this temp was never populated (e.g. a different match arm),
       cap=0 causes the free to be safely skipped. */
    LLVMBuildStore(tmp, LLVMConstNull(str_type), slot);
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
#if CG_DEBUG
    {
        char dbg_fmt[64];
        snprintf(dbg_fmt, sizeof(dbg_fmt),
                 "[cg] tmp.push    slot=%d\n",
                 ctx->temp_string_count - 1);
        cg_emit_debug_printf(ctx, dbg_fmt, NULL, 0);
    }
#endif
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

/* M-4.5: register a statement-level temporary has_drop struct/enum value.
   `slot` is the alloca holding the deep-cloned value (e.g. result of vec[i]);
   `type` is its LS type (TYPE_STRUCT has_drop or TYPE_ENUM has_drop).
   assoc mark = current temp_string_count, so cg_flush_temps releases exactly
   the slots produced since a given mark (aligned with string-temp semantics). */
static void cg_push_temp_drop(CodegenContext *ctx, LLVMValueRef slot, Type *type)
{
    if (slot == NULL || type == NULL || ctx->current_fn == NULL)
        return;
    bool is_drop_struct = (type->kind == TYPE_STRUCT && type->as.strukt.has_drop);
    bool is_drop_enum   = (type->kind == TYPE_ENUM   && type->as.enom.has_drop);
    if (getenv("LS_DEBUG_TEMPS"))
        fprintf(stderr, "[tmp] push fn=%s type=%s drop=%d n=%d\n",
                ctx->current_fn ? LLVMGetValueName(ctx->current_fn) : "?",
                type->kind == TYPE_STRUCT ? (type->as.strukt.name ? type->as.strukt.name : "?")
                                          : "(enum)",
                (int)(is_drop_struct || is_drop_enum), ctx->temp_drop_count);
    if (!is_drop_struct && !is_drop_enum)
        return; /* nothing to drop — POD struct/enum or non-drop type */

    if (ctx->temp_drop_count >= ctx->temp_drop_cap)
    {
        ctx->temp_drop_cap   = GROW_CAPACITY(ctx->temp_drop_cap);
        ctx->temp_drop_slots = GROW_ARRAY(LLVMValueRef, ctx->temp_drop_slots, ctx->temp_drop_cap);
        ctx->temp_drop_types = GROW_ARRAY(Type *,       ctx->temp_drop_types, ctx->temp_drop_cap);
        ctx->temp_drop_marks = GROW_ARRAY(int,          ctx->temp_drop_marks, ctx->temp_drop_cap);
    }
    ctx->temp_drop_slots[ctx->temp_drop_count] = slot;
    ctx->temp_drop_types[ctx->temp_drop_count] = type;
    ctx->temp_drop_marks[ctx->temp_drop_count] = ctx->temp_string_count;
    ctx->temp_drop_count++;
#if CG_DEBUG
    {
        char dbg_fmt[96];
        const char *tn = (type->kind == TYPE_STRUCT) ? type->as.strukt.name
                                                      : type->as.enom.name;
        snprintf(dbg_fmt, sizeof(dbg_fmt),
                 "[cg] tdrop.push  type=%s n=%d\n", tn ? tn : "?",
                 ctx->temp_drop_count);
        cg_emit_debug_printf(ctx, dbg_fmt, NULL, 0);
    }
#endif
}

/* Remove a slot from the pending temp-drop list WITHOUT emitting its drop.
   Used when the slot has already been dropped explicitly on the current path
   (e.g. the match merge-block subject drop) so the statement-end flush won't
   drop it a second time. Removes every matching entry; order is irrelevant. */
static void cg_remove_temp_drop(CodegenContext *ctx, LLVMValueRef slot)
{
    if (slot == NULL) return;
    int keep = 0;
    for (int i = 0; i < ctx->temp_drop_count; i++)
    {
        if (ctx->temp_drop_slots[i] == slot)
            continue; /* drop the entry (already handled) */
        ctx->temp_drop_slots[keep] = ctx->temp_drop_slots[i];
        ctx->temp_drop_types[keep] = ctx->temp_drop_types[i];
        ctx->temp_drop_marks[keep] = ctx->temp_drop_marks[i];
        keep++;
    }
    ctx->temp_drop_count = keep;
}


/* L-013: unwrap a match-arm body to its tail expression (the value the arm yields).
   For a block body `=> { ...; E }` the tail is the last statement's expression;
   for a bare `=> E` the tail is E itself. Returns NULL if there is no value tail. */
static AstNode *cg_match_arm_tail(AstNode *arm_body)
{
    AstNode *tail = arm_body;
    if (tail && tail->kind == AST_BLOCK && tail->as.block.stmt_count > 0)
    {
        AstNode *last_s = tail->as.block.stmts[tail->as.block.stmt_count - 1];
        tail = (last_s && last_s->kind == AST_EXPR_STMT)
                   ? last_s->as.expr_stmt.expr
                   : NULL;
    }
    return tail;
}

/* L-013 (step 2+3): ensure the value an arm stores into result_alloca is owned
   INDEPENDENTLY by the result, returning the value to store.
   Clone is needed exactly when the tail aliases storage owned elsewhere with no
   fresh owned temp to transfer: an outer local or a borrowed payload binder. A
   freshly-produced rvalue temp (count grew) is transferred (not cloned); a binder
   we just moved out already owns its independent B2 clone (not cloned); a static
   literal / POD tail aliases no heap (stored as-is).
   `did_move_out_binder` = the arm's move-out optimization marked a payload binder
   borrowed this arm (enum path only; always false for non-enum patterns). */
static LLVMValueRef cg_match_arm_own_tail(CodegenContext *ctx, AstNode *tail,
                                          LLVMValueRef body_val, LLVMTypeRef res_llvm,
                                          Type *result_type, int str_mark, int drop_floor,
                                          bool did_move_out_binder)
{
    if (body_val == NULL || result_type == NULL)
        return body_val;
    bool owned_heap =
        (result_type->kind == TYPE_STRUCT && result_type->as.strukt.has_drop) ||
        (result_type->kind == TYPE_ENUM   && result_type->as.enom.has_drop);
    if (!owned_heap)
        return body_val;
    /* Fresh owned temp produced by this body → an rvalue we will transfer (no clone). */
    if (ctx->temp_string_count > str_mark || ctx->temp_drop_count > drop_floor)
        return body_val;
    /* A binder we just moved out already owns an independent clone (no clone). */
    if (did_move_out_binder)
        return body_val;
    /* Tail aliasing an owning IDENT (outer local, or borrowed binder) → clone so the
       result owns independently of the real owner. Static/POD tails alias nothing. */
    if (tail && tail->kind == AST_IDENT)
    {
        CgSymbol *s = cg_scope_resolve(ctx->current_scope, tail->as.ident.name);
        if (s && s->value && s->type &&
            ((s->type->kind == TYPE_STRUCT && s->type->as.strukt.has_drop) ||
             (s->type->kind == TYPE_ENUM   && s->type->as.enom.has_drop)))
            return emit_clone_value(ctx, body_val, res_llvm, result_type);
    }
    return body_val;
}

/* L-013 (step 2+3): encapsulate one match arm's statement-level temporaries after
   its body_val has been stored into result_alloca. The single owned tail value the
   arm yields is transferred to the result (which is registered as the lone result
   temp at the merge block); every OTHER arm-body temp is freed/dropped here so it
   does not leak into the outer statement temp tables.
   - str_mark / drop_floor = temp_string_count / temp_drop_count captured just before
     the body was evaluated. Pre-body temps (the subject drop at index < drop_floor,
     and outer string temps below str_mark) are untouched.
   - The tail temp matching result_type is neutralized (string: mark moved so its free
     is skipped; has_drop: removed from the drop list) — the result owns its buffer. */
static void cg_match_arm_encapsulate(CodegenContext *ctx, int str_mark, int drop_floor,
                                     Type *result_type)
{
    bool res_is_string = result_type && result_type->kind == TYPE_STRING;
    bool res_is_drop =
        result_type &&
        ((result_type->kind == TYPE_STRUCT && result_type->as.strukt.has_drop) ||
         (result_type->kind == TYPE_ENUM   && result_type->as.enom.has_drop));

    LLVMBasicBlockRef cur = LLVMGetInsertBlock(ctx->builder);
    bool terminated = cur && LLVMGetBasicBlockTerminator(cur) != NULL;

    /* Transfer the tail string temp into the result (its free is skipped below). */
    if (res_is_string && ctx->temp_string_count > str_mark)
        cg_mark_last_temp_moved(ctx, str_mark, "match arm: tail string -> result");
    /* Free the arm-body string temps in [str_mark, count); the transferred tail
       (cap=-1) is skipped by emit_string_free. */
    if (!terminated)
        for (int i = str_mark; i < ctx->temp_string_count; i++)
            emit_string_free(ctx, ctx->temp_string_slots[i]);
    ctx->temp_string_count = str_mark;

    /* Transfer the tail has_drop temp into the result: remove the last body-registered
       drop entry without emitting its drop (the result owns that buffer). */
    if (res_is_drop && ctx->temp_drop_count > drop_floor)
        ctx->temp_drop_count--;
    /* Drop the remaining arm-body has_drop temps in [drop_floor, count). */
    if (!terminated)
        for (int i = drop_floor; i < ctx->temp_drop_count; i++)
        {
            Type *t = ctx->temp_drop_types[i];
            if (t->kind == TYPE_STRUCT)    emit_struct_drop(ctx, ctx->temp_drop_slots[i], t);
            else if (t->kind == TYPE_ENUM) emit_enum_drop(ctx, ctx->temp_drop_slots[i], t);
        }
    ctx->temp_drop_count = drop_floor;
}


/* M-4.5: drop and release all temp_drop slots whose assoc mark >= `mark`.
   Compacts surviving (mark < flush-mark) entries to the front. */
static void cg_flush_temp_drops(CodegenContext *ctx, int mark)
{
    LLVMBasicBlockRef cur = LLVMGetInsertBlock(ctx->builder);
    if (getenv("LS_DEBUG_TEMPS") && ctx->temp_drop_count > 0)
        fprintf(stderr, "[tmp] flush fn=%s mark=%d n=%d term=%d\n",
                ctx->current_fn ? LLVMGetValueName(ctx->current_fn) : "?",
                mark, ctx->temp_drop_count,
                (int)(cur && LLVMGetBasicBlockTerminator(cur) != NULL));
    if (cur && LLVMGetBasicBlockTerminator(cur) != NULL)
    {
        /* terminated block: just discard the high-water slots */
        int keep0 = 0;
        for (int i = 0; i < ctx->temp_drop_count; i++)
            if (ctx->temp_drop_marks[i] < mark)
                keep0++;
        ctx->temp_drop_count = keep0;
        return;
    }
    int keep = 0;
    for (int i = 0; i < ctx->temp_drop_count; i++)
    {
        if (ctx->temp_drop_marks[i] < mark)
        {
            /* survives this flush — compact toward the front */
            ctx->temp_drop_slots[keep] = ctx->temp_drop_slots[i];
            ctx->temp_drop_types[keep] = ctx->temp_drop_types[i];
            ctx->temp_drop_marks[keep] = ctx->temp_drop_marks[i];
            keep++;
            continue;
        }
        Type *t = ctx->temp_drop_types[i];
        LLVMValueRef slot = ctx->temp_drop_slots[i];
        if (t->kind == TYPE_STRUCT)
            emit_struct_drop(ctx, slot, t);
        else if (t->kind == TYPE_ENUM)
            emit_enum_drop(ctx, slot, t);
    }
    ctx->temp_drop_count = keep;
}

/* Phase C.5: register a closure literal's env_ptr as a temporary owned by
   the current statement. Drained by cg_flush_temps. The drop_fn at env[0]
   (NULL for POD-only envs) handles per-capture cleanup; we then free the
   env block itself. */
static void cg_push_temp_block_env(CodegenContext *ctx, LLVMValueRef env_ptr)
{
    if (env_ptr == NULL) return;
    if (ctx->temp_block_env_count >= ctx->temp_block_env_cap) {
        ctx->temp_block_env_cap = GROW_CAPACITY(ctx->temp_block_env_cap);
        ctx->temp_block_envs = GROW_ARRAY(LLVMValueRef,
                                          ctx->temp_block_envs,
                                          ctx->temp_block_env_cap);
    }
    ctx->temp_block_envs[ctx->temp_block_env_count++] = env_ptr;
}

/* Emit "if env != NULL { (drop_fn?(env))(); free(env) }" for one env_ptr. */
static void cg_emit_block_env_drop(CodegenContext *ctx, LLVMValueRef env_ptr)
{
    LLVMBasicBlockRef cur_bb = LLVMGetInsertBlock(ctx->builder);
    if (cur_bb == NULL) return;
    if (LLVMGetBasicBlockTerminator(cur_bb) != NULL) return;
    /* F.6: log block env drop (runtime ptr). */
    cg_dbg_block_op(ctx, "env.drop", "", env_ptr);
    LLVMValueRef cur_fn = LLVMGetBasicBlockParent(cur_bb);
    LLVMTypeRef ptr_t = LLVMPointerTypeInContext(ctx->context, 0);
    LLVMValueRef is_nn = LLVMBuildICmp(ctx->builder, LLVMIntNE, env_ptr,
                                       LLVMConstNull(ptr_t), "tmp.env.nn");
    LLVMBasicBlockRef maybe_bb = LLVMAppendBasicBlockInContext(ctx->context, cur_fn, "tmp.env.maybe");
    LLVMBasicBlockRef call_bb  = LLVMAppendBasicBlockInContext(ctx->context, cur_fn, "tmp.env.dropcall");
    LLVMBasicBlockRef do_bb    = LLVMAppendBasicBlockInContext(ctx->context, cur_fn, "tmp.env.dofree");
    LLVMBasicBlockRef cont_bb  = LLVMAppendBasicBlockInContext(ctx->context, cur_fn, "tmp.env.cont");
    LLVMBuildCondBr(ctx->builder, is_nn, maybe_bb, cont_bb);
    LLVMPositionBuilderAtEnd(ctx->builder, maybe_bb);
    LLVMValueRef drop_fn_p = LLVMBuildLoad2(ctx->builder, ptr_t, env_ptr, "tmp.drop");
    LLVMValueRef has_drop  = LLVMBuildICmp(ctx->builder, LLVMIntNE, drop_fn_p,
                                           LLVMConstNull(ptr_t), "tmp.has_drop");
    LLVMBuildCondBr(ctx->builder, has_drop, call_bb, do_bb);
    LLVMPositionBuilderAtEnd(ctx->builder, call_bb);
    {
        LLVMTypeRef dp[1] = { ptr_t };
        LLVMTypeRef dft = LLVMFunctionType(LLVMVoidTypeInContext(ctx->context),
                                           dp, 1, 0);
        LLVMBuildCall2(ctx->builder, dft, drop_fn_p, &env_ptr, 1, "");
    }
    LLVMBuildBr(ctx->builder, do_bb);
    LLVMPositionBuilderAtEnd(ctx->builder, do_bb);
    cg_emit_free(ctx, env_ptr, "closure.env.tmp", CG_LINE(ctx), CG_COL(ctx));
    LLVMBuildBr(ctx->builder, cont_bb);
    LLVMPositionBuilderAtEnd(ctx->builder, cont_bb);
}

/* F.2: Drop the Block env stored at blk_alloca:
   load the Block, extract env_ptr, then call cg_emit_block_env_drop on it. */
static void cg_emit_block_drop_at(CodegenContext *ctx, LLVMValueRef blk_alloca)
{
    LLVMBasicBlockRef cur_bb = LLVMGetInsertBlock(ctx->builder);
    if (cur_bb == NULL || LLVMGetBasicBlockTerminator(cur_bb) != NULL) return;
    LLVMTypeRef ptr_t = LLVMPointerTypeInContext(ctx->context, 0);
    LLVMTypeRef fields[2] = { ptr_t, ptr_t };
    LLVMTypeRef blk_t = LLVMStructTypeInContext(ctx->context, fields, 2, 0);
    LLVMValueRef blk_val = LLVMBuildLoad2(ctx->builder, blk_t, blk_alloca, "blk.old.load");
    LLVMValueRef env_ptr = LLVMBuildExtractValue(ctx->builder, blk_val, 1, "blk.old.env");
    cg_emit_block_env_drop(ctx, env_ptr);
}

/* F.2: Zero the env_ptr field (field 1) of the Block stored at blk_alloca.
   Called after moving a Block to another variable so scope cleanup skips this alloca. */
static void cg_null_block_env(CodegenContext *ctx, LLVMValueRef blk_alloca)
{
    LLVMBasicBlockRef cur_bb = LLVMGetInsertBlock(ctx->builder);
    if (cur_bb == NULL || LLVMGetBasicBlockTerminator(cur_bb) != NULL) return;
    LLVMTypeRef ptr_t = LLVMPointerTypeInContext(ctx->context, 0);
    LLVMTypeRef fields[2] = { ptr_t, ptr_t };
    LLVMTypeRef blk_t = LLVMStructTypeInContext(ctx->context, fields, 2, 0);
#if CG_DEBUG
    {
        /* F.6: log block.move (source env being zeroed). */
        LLVMValueRef env_field_p = LLVMBuildStructGEP2(ctx->builder, blk_t, blk_alloca, 1, "blk.mv.ep");
        LLVMValueRef old_env = LLVMBuildLoad2(ctx->builder, ptr_t, env_field_p, "blk.mv.oldenv");
        cg_dbg_block_op(ctx, "move", "src->null", old_env);
    }
#endif
    LLVMValueRef env_field = LLVMBuildStructGEP2(ctx->builder, blk_t, blk_alloca, 1, "blk.mv.envf");
    LLVMBuildStore(ctx->builder, LLVMConstNull(ptr_t), env_field);
}

/* Phase G: deep-clone the env of a Block VALUE, producing a new LsBlock that
   owns an independent env. Used at copy-out sites (`Block g = vec[i]` /
   `struct.field` / `map.get(k)`) where the source LsBlock aliases an env still
   owned by the container — without cloning, both the new variable and the
   container element would free the same env on scope exit (double-free).

   The per-closure clone_fn lives at env field 1 (see codegen_closure_literal
   step 9a-clone). A NULL env (closure with no captures) is returned unchanged;
   when env != NULL, clone_fn is guaranteed non-NULL. Runtime shape:
       new_env = env ? ((ptr(*)(ptr))env[1])(env) : NULL
       result  = { blk.fn, new_env }                                       */
static LLVMValueRef cg_emit_block_env_clone(CodegenContext *ctx,
                                            LLVMValueRef block_val)
{
    LLVMTypeRef ptr_t = LLVMPointerTypeInContext(ctx->context, 0);
    LLVMTypeRef i64_t = LLVMInt64TypeInContext(ctx->context);
    LLVMValueRef fn_ptr  = LLVMBuildExtractValue(ctx->builder, block_val, 0, "bc.fn");
    LLVMValueRef env_ptr = LLVMBuildExtractValue(ctx->builder, block_val, 1, "bc.env");

    LLVMValueRef cur_fn = LLVMGetBasicBlockParent(LLVMGetInsertBlock(ctx->builder));
    LLVMValueRef is_null = LLVMBuildICmp(ctx->builder, LLVMIntEQ, env_ptr,
                                         LLVMConstNull(ptr_t), "bc.isnull");
    LLVMBasicBlockRef from_bb  = LLVMGetInsertBlock(ctx->builder);
    LLVMBasicBlockRef clone_bb = LLVMAppendBasicBlockInContext(ctx->context, cur_fn, "bc.clone");
    LLVMBasicBlockRef cont_bb  = LLVMAppendBasicBlockInContext(ctx->context, cur_fn, "bc.cont");
    LLVMBuildCondBr(ctx->builder, is_null, cont_bb, clone_bb);

    /* clone_bb: load clone_fn from env[1] and call it. */
    LLVMPositionBuilderAtEnd(ctx->builder, clone_bb);
    LLVMValueRef idx1 = LLVMConstInt(i64_t, 1, 0);
    LLVMValueRef clone_slot = LLVMBuildInBoundsGEP2(ctx->builder, ptr_t, env_ptr,
                                                    &idx1, 1, "bc.cfslot");
    LLVMValueRef clone_fn = LLVMBuildLoad2(ctx->builder, ptr_t, clone_slot, "bc.cf");
    LLVMTypeRef cf_param[1] = { ptr_t };
    LLVMTypeRef cf_ty = LLVMFunctionType(ptr_t, cf_param, 1, 0);
    LLVMValueRef new_env = LLVMBuildCall2(ctx->builder, cf_ty, clone_fn, &env_ptr, 1, "bc.newenv");
    LLVMBasicBlockRef clone_end = LLVMGetInsertBlock(ctx->builder);
    LLVMBuildBr(ctx->builder, cont_bb);

    /* cont_bb: phi NULL (from null path) / new_env (from clone path). */
    LLVMPositionBuilderAtEnd(ctx->builder, cont_bb);
    LLVMValueRef phi = LLVMBuildPhi(ctx->builder, ptr_t, "bc.envphi");
    LLVMValueRef inc_vals[2] = { LLVMConstNull(ptr_t), new_env };
    LLVMBasicBlockRef inc_bbs[2] = { from_bb, clone_end };
    LLVMAddIncoming(phi, inc_vals, inc_bbs, 2);

    LLVMValueRef result = LLVMGetUndef(LLVMTypeOf(block_val));
    result = LLVMBuildInsertValue(ctx->builder, result, fn_ptr, 0, "bc.rfn");
    result = LLVMBuildInsertValue(ctx->builder, result, phi, 1, "bc.renv");
    cg_dbg_block_op(ctx, "clone", "env-deepcopy", phi);
    return result;
}

static LLVMValueRef cg_resolve_function_for_block(CodegenContext *ctx,
                                                  AstNode *node,
                                                  char *name_buf,
                                                  size_t name_cap)
{
    if (!node || !name_buf || name_cap == 0)
        return NULL;
    name_buf[0] = '\0';

    if (node->kind == AST_IDENT)
    {
        snprintf(name_buf, name_cap, "%s", node->as.ident.name);
        if (ctx->current_emit_module != NULL)
        {
            char mod_sym[512];
            cg_module_fn_symbol(mod_sym, sizeof(mod_sym),
                                ctx->current_emit_module, node->as.ident.name);
            LLVMValueRef mod_fn = LLVMGetNamedFunction(ctx->module, mod_sym);
            if (mod_fn)
            {
                snprintf(name_buf, name_cap, "%s", mod_sym);
                return mod_fn;
            }
            snprintf(name_buf, name_cap, "%s", mod_sym);
        }
        LLVMValueRef fn = LLVMGetNamedFunction(ctx->module, node->as.ident.name);
        if (fn)
        {
            snprintf(name_buf, name_cap, "%s", node->as.ident.name);
            return fn;
        }
    }
    else if (node->kind == AST_FIELD &&
             node->as.field_access.object &&
             node->as.field_access.object->resolved_type &&
             node->as.field_access.object->resolved_type->kind == TYPE_MODULE)
    {
        Type *mod_t = node->as.field_access.object->resolved_type;
        const char *field = node->as.field_access.field;
        if (mod_t->as.module.name)
        {
            cg_module_fn_symbol(name_buf, name_cap, mod_t->as.module.name, field);
            LLVMValueRef mod_fn = LLVMGetNamedFunction(ctx->module, name_buf);
            if (mod_fn)
                return mod_fn;
            LLVMValueRef bare_fn = LLVMGetNamedFunction(ctx->module, field);
            if (bare_fn)
            {
                snprintf(name_buf, name_cap, "%s", field);
                return bare_fn;
            }
        }
        else
        {
            snprintf(name_buf, name_cap, "%s", field);
            LLVMValueRef fn = LLVMGetNamedFunction(ctx->module, name_buf);
            if (fn)
                return fn;
        }
    }

    if (node->resolved_type && node->resolved_type->kind == TYPE_FUNCTION &&
        name_buf[0] != '\0')
    {
        LLVMTypeRef fn_type = type_to_llvm(ctx, node->resolved_type);
        return LLVMAddFunction(ctx->module, name_buf, fn_type);
    }
    return NULL;
}

static LLVMValueRef codegen_fn_to_block(CodegenContext *ctx, AstNode *node)
{
    Type *block_t = node->coerce_block_type;
    Type *fn_t = node->resolved_type;
    if (!block_t || block_t->kind != TYPE_BLOCK ||
        !fn_t || fn_t->kind != TYPE_FUNCTION)
    {
        cg_error(ctx, node->line, node->column,
                 "internal: fn-to-Block coercion without matching types");
        return NULL;
    }

    char fn_name[512];
    LLVMValueRef target_fn = cg_resolve_function_for_block(ctx, node,
                                                           fn_name, sizeof(fn_name));
    if (!target_fn)
    {
        cg_error(ctx, node->line, node->column,
                 "undefined function '%s'", fn_name[0] ? fn_name : "<fn>");
        return NULL;
    }

    int n = block_t->as.function.param_count;
    LLVMTypeRef ptr_t = LLVMPointerTypeInContext(ctx->context, 0);
    LLVMTypeRef *params = (LLVMTypeRef *)malloc_safe(
        (size_t)(n + 1) * sizeof(LLVMTypeRef));
    params[0] = ptr_t;
    for (int i = 0; i < n; i++)
        params[i + 1] = type_to_llvm(ctx, block_t->as.function.params[i]);
    LLVMTypeRef ret_t = type_to_llvm(ctx, block_t->as.function.return_type);
    LLVMTypeRef thunk_type = LLVMFunctionType(ret_t, params, (unsigned)(n + 1), 0);
    free(params);

    char thunk_name[128];
    snprintf(thunk_name, sizeof(thunk_name), "__fnthunk_%d",
             ctx->closure_id_counter++);
    LLVMValueRef thunk = LLVMAddFunction(ctx->module, thunk_name, thunk_type);

    LLVMBasicBlockRef saved_bb = LLVMGetInsertBlock(ctx->builder);
    LLVMValueRef saved_fn = ctx->current_fn;
    Type *saved_ret = ctx->current_fn_return_type;

    LLVMBasicBlockRef entry =
        LLVMAppendBasicBlockInContext(ctx->context, thunk, "entry");
    LLVMPositionBuilderAtEnd(ctx->builder, entry);
    ctx->current_fn = thunk;
    ctx->current_fn_return_type = block_t->as.function.return_type;

    LLVMValueRef *call_args = NULL;
    if (n > 0)
    {
        call_args = (LLVMValueRef *)malloc_safe((size_t)n * sizeof(LLVMValueRef));
        for (int i = 0; i < n; i++)
            call_args[i] = LLVMGetParam(thunk, (unsigned)(i + 1));
    }

    LLVMTypeRef target_type = LLVMGlobalGetValueType(target_fn);
    bool void_ret = (LLVMGetTypeKind(ret_t) == LLVMVoidTypeKind);
    LLVMValueRef call = LLVMBuildCall2(ctx->builder, target_type, target_fn,
                                       call_args, (unsigned)n,
                                       void_ret ? "" : "fnthunk.call");
    free(call_args);
    if (void_ret)
        LLVMBuildRetVoid(ctx->builder);
    else
        LLVMBuildRet(ctx->builder, call);

    ctx->current_fn = saved_fn;
    ctx->current_fn_return_type = saved_ret;
    if (saved_bb)
        LLVMPositionBuilderAtEnd(ctx->builder, saved_bb);

    LLVMTypeRef block_llvm = type_to_llvm(ctx, block_t);
    LLVMValueRef result = LLVMGetUndef(block_llvm);
    result = LLVMBuildInsertValue(ctx->builder, result, thunk, 0, "f2b.fn");
    result = LLVMBuildInsertValue(ctx->builder, result,
                                  LLVMConstNull(ptr_t), 1, "f2b.env");
    return result;
}

/* Phase G: true when a Block-typed initializer reads a Block out of a container
   it does not own — `vec[i]`, `struct.field`, or `map.get(k)`. These produce an
   LsBlock that aliases the container's env, so a copy-out into a new variable
   must deep-clone the env. A factory call `fn()->Block` returns an owned env and
   is deliberately NOT matched here (no clone). Mirrors the checker's former
   F.3/F.4A rejection patterns. */
static bool cg_block_source_is_aliased(AstNode *src)
{
    if (!src) return false;
    if (src->kind == AST_INDEX) {
        AstNode *obj = src->as.index_expr.object;
        /* A pure-LS Vec(Block)[i] (object is the Vec struct):
           the loaded Block aliases the env owned by the container. */
        return obj && obj->resolved_type &&
               obj->resolved_type->kind == TYPE_STRUCT;
    }
    if (src->kind == AST_FIELD) {
        AstNode *obj = src->as.field_access.object;
        return obj && obj->resolved_type && obj->resolved_type->kind == TYPE_STRUCT;
    }
    if (src->kind == AST_CALL) {
        AstNode *callee = src->as.call.callee;
        if (!callee || callee->kind != AST_FIELD) return false;
        const char *m = callee->as.field_access.field;
        /* F5: a copy-out reader on a container — map.get, or a pure-LS
           Vec(Block)'s get/get!/__index/first/last (the returned Block aliases
           the container's env, so the BIND site clones; a discarded rvalue like
           `v[i](arg)` borrows and is not cloned → no leak). Cloning here rather
           than inside the method keeps discarded copy-out rvalues leak-free. */
        bool reader = strcmp(m, "get") == 0 || strcmp(m, "get!") == 0 ||
                      strcmp(m, "__index") == 0 || strcmp(m, "first") == 0 ||
                      strcmp(m, "last") == 0;
        if (!reader) return false;
        AstNode *mo = callee->as.field_access.object;
        return mo && mo->resolved_type &&
                mo->resolved_type->kind == TYPE_STRUCT;
    }
    return false;
}

/* Move-elision (Q4): invalidate a moved-from source variable so that its scope
   cleanup (or a later overwrite drop) does not double-free heap now owned by the
   destination. Called only when the checker tagged the source IDENT with
   `moved_out` (see ast.h) — i.e. the source is a named, owned, non-borrow
   variable whose later use the checker already rejects.

   The invalidation per type mirrors the existing CG_XFER_INTO_CONTAINER paths in
   cg_store_owned:
     - string : mark_string_moved (cap = -1; runtime-guarded, no-op if static)
     - struct : set moved_flag = 1 (scope-drop is moved_flag-conditional)
     - enum   : set moved_flag = 1
     - map: zero the source's cap field (scope-drop frees only if cap > 0)
   `source` is the raw RHS AST node (possibly wrapped in __move()); we unwrap and
   resolve the underlying IDENT's symbol. Borrowed sources are never invalidated
   (they hold no ownership). Returns true if the source was a recognised owned
   IDENT (so the caller can skip cloning), false otherwise. */
static bool cg_invalidate_moved_source(CodegenContext *ctx, AstNode *source, Type *type)
{
    if (!source || !type) return false;
    AstNode *src = ast_unwrap_move(source);
    if (!src || src->kind != AST_IDENT) return false;
    CgSymbol *sym = cg_scope_resolve(ctx->current_scope, src->as.ident.name);
    if (!sym || !sym->value || sym->is_borrowed || sym->is_mut_borrow)
        return false;

    switch (type->kind)
    {
    case TYPE_STRING:
        mark_string_moved(ctx, sym->value, "move-elision: string moved into dst");
        return true;
    case TYPE_STRUCT:
        if (!type->as.strukt.has_drop) return false;
        /* No moved_flag → the invalidation would be a NO-OP, so returning true
           (caller skips the clone) hands the SAME heap to the destination while
           the original owner still drops it. Hit by zero-copy match binders of
           an owned-rvalue subject (sym->value GEPs into the subject temp, no
           moved_flag): `match f() { Ok(s) => { outer = s } }` double-freed the
           payload. Fall back to clone instead. */
        if (!sym->moved_flag) return false;
        LLVMBuildStore(ctx->builder,
            LLVMConstInt(LLVMInt1TypeInContext(ctx->context), 1, 0),
            sym->moved_flag);
        return true;
    case TYPE_ENUM:
        if (!type->as.enom.has_drop) return false;
        if (!sym->moved_flag) return false;   /* same no-op-invalidate hazard */
        LLVMBuildStore(ctx->builder,
            LLVMConstInt(LLVMInt1TypeInContext(ctx->context), 1, 0),
            sym->moved_flag);
        return true;
    default:
        return false;
    }
}

/* M-3: 统一所有权转移 API
   将 val 存入 dst_ptr，根据类型和来源节点自动选择 move/clone/store 语义。
   temp_mark: codegen_expr(source) 调用之前的 ctx->temp_string_count 快照。
   kind:      CG_XFER_INTO_CONTAINER — 存入容器（move 语义，source 被消耗）
              CG_XFER_ASSIGN_VAR     — 变量赋值（clone 语义，string source 保持有效）
              CG_XFER_RETURN         — return（同 INTO_CONTAINER，move 语义）
*/
static void cg_store_owned(CodegenContext *ctx,
                           LLVMValueRef dst_ptr,
                           LLVMValueRef val,
                           Type *type,
                           AstNode *source,
                           int temp_mark,
                           CgTransferKind kind)
{
    if (!val || !dst_ptr || !type) {
        if (dst_ptr && val)
            LLVMBuildStore(ctx->builder, val, dst_ptr);
        return;
    }

    AstNode *src = source ? ast_unwrap_move(source) : NULL;

    /* 解析 source 是否是命名变量 IDENT */
    CgSymbol *src_sym = NULL;
    if (src && src->kind == AST_IDENT)
        src_sym = cg_scope_resolve(ctx->current_scope, src->as.ident.name);

    /* 是否是右值：产生新的堆缓冲区，调用者需负责释放或移交所有权。
       AST_CALL        — 函数/方法调用均为 AST_CALL（方法调用时 callee 为 AST_FIELD）
       AST_TRY         — try 表达式传播返回值
       AST_FORMAT_STRING — f"..." 格式化字符串
       注意：AST_IDENT 不在此列（命名变量，有自己的 alloca） */
    bool is_rvalue = src && (src->kind == AST_CALL         ||
                             src->kind == AST_TRY           ||
                             src->kind == AST_FORCE_UNWRAP  ||
                             src->kind == AST_FORMAT_STRING);

    /* ------------------------------------------------------------------ */
    /* STRING                                                               */
    /* ------------------------------------------------------------------ */
    if (type->kind == TYPE_STRING)
    {
        if (is_rvalue)
        {
            /* 右值：已有堆缓冲，直接存入。弹出 temp slot 防止 double-free。 */
            LLVMBuildStore(ctx->builder, val, dst_ptr);
            cg_mark_last_temp_moved(ctx, temp_mark, "xfer: rvalue string into dst");
        }
        else if (src_sym && !src_sym->is_borrowed)
        {
            if (kind == CG_XFER_ASSIGN_VAR)
            {
                /* a = b：clone 语义，b 保留所有权 */
                LLVMValueRef cloned = emit_string_clone_val(ctx, val);
                LLVMBuildStore(ctx->builder, cloned, dst_ptr);
            }
            else
            {
                /* INTO_CONTAINER / RETURN：运行时检查 cap。
                   cap > 0  → owned（来自 rvalue/__move 实参）：move 语义
                   cap == LS_CAP_BORROWED (-2) → 借用（来自命名变量实参）：clone */
                LLVMValueRef cap_xfer = LLVMBuildExtractValue(ctx->builder, val, 2, "xfer.str.cap");
                LLVMValueRef zero32 = LLVMConstInt(LLVMInt32TypeInContext(ctx->context), 0, 0);
                LLVMValueRef is_owned_xfer = LLVMBuildICmp(ctx->builder, LLVMIntSGT, cap_xfer, zero32, "xfer.str.owned");
                LLVMValueRef cur_fn = LLVMGetBasicBlockParent(LLVMGetInsertBlock(ctx->builder));
                LLVMBasicBlockRef owned_bb = LLVMAppendBasicBlockInContext(ctx->context, cur_fn, "xfer.str.owned");
                LLVMBasicBlockRef borrow_bb = LLVMAppendBasicBlockInContext(ctx->context, cur_fn, "xfer.str.borrow");
                LLVMBasicBlockRef merge_bb = LLVMAppendBasicBlockInContext(ctx->context, cur_fn, "xfer.str.merge");
                LLVMBuildCondBr(ctx->builder, is_owned_xfer, owned_bb, borrow_bb);

                /* Owned: move 语义 */
                LLVMPositionBuilderAtEnd(ctx->builder, owned_bb);
                LLVMBuildStore(ctx->builder, val, dst_ptr);
                mark_string_moved(ctx, src_sym->value, "xfer: owned string moved into container");
                LLVMBuildBr(ctx->builder, merge_bb);

                /* Borrowed: clone 语义 */
                LLVMPositionBuilderAtEnd(ctx->builder, borrow_bb);
                LLVMValueRef cloned = emit_string_clone_val(ctx, val);
                LLVMBuildStore(ctx->builder, cloned, dst_ptr);
                LLVMBuildBr(ctx->builder, merge_bb);

                LLVMPositionBuilderAtEnd(ctx->builder, merge_bb);
            }
        }
        else
        {
            /* borrowed (cap=-2) / static (cap=0) / 其他：
               emit_string_clone_val 在运行时按 cap 值决定是否克隆 */
            LLVMValueRef cloned = emit_string_clone_val(ctx, val);
            LLVMBuildStore(ctx->builder, cloned, dst_ptr);
        }
#if CG_DEBUG
        {
            const char *src_kind = is_rvalue ? "rvalue" :
                                   (src_sym && src_sym->is_borrowed) ? "borrowed" :
                                   src_sym ? "ident" : "other";
            const char *xfer_kind = (kind == CG_XFER_ASSIGN_VAR) ? "assign" :
                                    (kind == CG_XFER_RETURN)      ? "return" : "container";
            char fmt[128];
            snprintf(fmt, sizeof(fmt),
                     "[cg] xfer.string src=%-8s kind=%-9s\n", src_kind, xfer_kind);
            cg_emit_debug_printf(ctx, fmt, NULL, 0);
        }
#endif
        return;
    }

    /* ------------------------------------------------------------------ */
    /* STRUCT (has_drop)                                                   */
    /* ------------------------------------------------------------------ */
    if (type->kind == TYPE_STRUCT && type->as.strukt.has_drop)
    {
        bool source_borrowed = src_sym && src_sym->is_borrowed;
        if (source_borrowed)
        {
            /* borrowed match binder：深克隆，enum subject 仍持有原始堆内存 */
            LLVMTypeRef st_llvm = type_to_llvm(ctx, type);
            val = emit_struct_clone_val(ctx, val, st_llvm, type);
        }
        LLVMBuildStore(ctx->builder, val, dst_ptr);
        if (!source_borrowed)
        {
            if (src_sym && src_sym->moved_flag)
            {
                /* 命名 owned 变量：设置 moved_flag，scope cleanup 跳过 drop */
                LLVMBuildStore(ctx->builder,
                    LLVMConstInt(LLVMInt1TypeInContext(ctx->context), 1, 0),
                    src_sym->moved_flag);
            }
            else
            {
                /* 表达式结果：将求值期间产生的所有 string temp 标记为 moved */
                for (int t = temp_mark; t < ctx->temp_string_count; t++)
                    mark_string_moved(ctx, ctx->temp_string_slots[t],
                                      "xfer: struct temp owned by container");
            }
        }
#if CG_DEBUG
        {
            const char *sk = source_borrowed ? "borrowed" : src_sym ? "ident" : "expr";
            char fmt[64];
            snprintf(fmt, sizeof(fmt), "[cg] xfer.struct src=%-8s\n", sk);
            cg_emit_debug_printf(ctx, fmt, NULL, 0);
        }
#endif
        return;
    }

    /* ------------------------------------------------------------------ */
    /* ENUM (has_drop)                                                     */
    /* ------------------------------------------------------------------ */
    if (type->kind == TYPE_ENUM && type->as.enom.has_drop)
    {
        bool source_borrowed = src_sym && src_sym->is_borrowed;

        if (is_rvalue)
        {
            /* 右值：直接取得所有权 */
            LLVMBuildStore(ctx->builder, val, dst_ptr);
        }
        else if (source_borrowed)
        {
            /* borrowed match binder：必须深克隆 */
            LLVMValueRef cloned = emit_enum_clone_val(ctx, val, type);
            LLVMBuildStore(ctx->builder, cloned, dst_ptr);
        }
        else if (src_sym && src_sym->moved_flag)
        {
            /* 命名 owned 变量：move，设置 moved_flag */
            LLVMBuildStore(ctx->builder, val, dst_ptr);
            LLVMBuildStore(ctx->builder,
                LLVMConstInt(LLVMInt1TypeInContext(ctx->context), 1, 0),
                src_sym->moved_flag);
        }
        else
        {
            /* 其他（字面量等）：直接存入 */
            LLVMBuildStore(ctx->builder, val, dst_ptr);
        }
#if CG_DEBUG
        cg_emit_debug_printf(ctx, "[cg] xfer.enum\n", NULL, 0);
#endif
        return;
    }

    /* ------------------------------------------------------------------ */
    /* BLOCK (闭包)                                                        */
    /* ------------------------------------------------------------------ */
    if (type->kind == TYPE_BLOCK)
    {
        LLVMBuildStore(ctx->builder, val, dst_ptr);
        if (src_sym && !src_sym->is_borrowed)
            cg_null_block_env(ctx, src_sym->value);
        else if (!src_sym && ctx->temp_block_env_count > 0)
            ctx->temp_block_env_count--;
        return;
    }

    /* ------------------------------------------------------------------ */
    /* POD (int / f64 / bool / char / object / pointer 等)                */
    /* ------------------------------------------------------------------ */
    LLVMBuildStore(ctx->builder, val, dst_ptr);
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
        ctx->temp_block_env_count = 0;
        cg_flush_temp_drops(ctx, mark); /* M-4.5: discard high-water drop slots */
        return;
    }
    int end = ctx->temp_string_count;
    if (skip_last && end > mark)
        end--;
#if CG_DEBUG
    if (end > mark)
    {
        char dbg_fmt[64];
        snprintf(dbg_fmt, sizeof(dbg_fmt),
                 "[cg] tmp.flush   mark=%d end=%d skip_last=%d\n",
                 mark, end, (int)skip_last);
        cg_emit_debug_printf(ctx, dbg_fmt, NULL, 0);
    }
#endif
    for (int i = mark; i < end; i++)
    {
        emit_string_free(ctx, ctx->temp_string_slots[i]);
    }
    ctx->temp_string_count = mark;

    /* Phase C.5: drain any temporary closure envs (literals consumed as
       rvalues this statement). They're not associated with the string
       mark — always full-flush since closure literals don't compose. */
    for (int i = 0; i < ctx->temp_block_env_count; i++) {
        cg_emit_block_env_drop(ctx, ctx->temp_block_envs[i]);
    }
    ctx->temp_block_env_count = 0;

    /* M-4.5: drop statement-level temporary has_drop struct/enum values
       (e.g. vec[i] / map.get clones not transferred to a named variable). */
    cg_flush_temp_drops(ctx, mark);
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
            /* F.5: Owned enum value. Use moved_flag if present (set when
               captured by-move into a closure) to avoid double-free. */
            emit_enum_drop_cond(ctx, sym->value, sym->type, sym->moved_flag);
        }
        else if (sym->type->kind == TYPE_BLOCK)
        {
            /* Phase C/C.5 closure RAII: free the heap env if non-NULL.
               env layout = { ptr drop_fn, T0 cap0, ... }; if drop_fn slot
               is non-NULL we call it first to release any heap-owning
               captures (string in v1) before freeing the env block. */
            LLVMTypeRef block_llvm = type_to_llvm(ctx, sym->type);
            LLVMValueRef blk_val = LLVMBuildLoad2(ctx->builder, block_llvm,
                                                  sym->value, "blk.cleanup");
            LLVMValueRef env_ptr = LLVMBuildExtractValue(
                ctx->builder, blk_val, 1, "blk.env.cleanup");
#if CG_DEBUG
            {
                /* F.6: log block.drop at scope exit. */
                char lbl[64];
                snprintf(lbl, sizeof(lbl), "var='%s'", sym->name ? sym->name : "?");
                cg_dbg_block_op(ctx, "drop", lbl, env_ptr);
            }
#endif
            LLVMTypeRef ptr_t = LLVMPointerTypeInContext(ctx->context, 0);
            LLVMValueRef is_nn = LLVMBuildICmp(
                ctx->builder, LLVMIntNE, env_ptr,
                LLVMConstNull(ptr_t), "blk.env.nn");
            LLVMValueRef cur_fn = LLVMGetBasicBlockParent(
                LLVMGetInsertBlock(ctx->builder));
            LLVMBasicBlockRef do_bb = LLVMAppendBasicBlockInContext(
                ctx->context, cur_fn, "blk.free");
            LLVMBasicBlockRef cont_bb = LLVMAppendBasicBlockInContext(
                ctx->context, cur_fn, "blk.cont");
            LLVMBuildCondBr(ctx->builder, is_nn, do_bb, cont_bb);
            LLVMPositionBuilderAtEnd(ctx->builder, do_bb);

            /* Conditional drop_fn dispatch (NULL slot → POD-only env). */
            LLVMValueRef drop_fn_p = LLVMBuildLoad2(
                ctx->builder, ptr_t, env_ptr, "blk.drop");
            LLVMValueRef has_drop = LLVMBuildICmp(
                ctx->builder, LLVMIntNE, drop_fn_p,
                LLVMConstNull(ptr_t), "blk.has_drop");
            LLVMBasicBlockRef call_bb = LLVMAppendBasicBlockInContext(
                ctx->context, cur_fn, "blk.dropcall");
            LLVMBasicBlockRef freebb  = LLVMAppendBasicBlockInContext(
                ctx->context, cur_fn, "blk.dofree");
            LLVMBuildCondBr(ctx->builder, has_drop, call_bb, freebb);
            LLVMPositionBuilderAtEnd(ctx->builder, call_bb);
            {
                LLVMTypeRef dp[1] = { ptr_t };
                LLVMTypeRef dft = LLVMFunctionType(
                    LLVMVoidTypeInContext(ctx->context), dp, 1, 0);
                LLVMBuildCall2(ctx->builder, dft, drop_fn_p, &env_ptr, 1, "");
            }
            LLVMBuildBr(ctx->builder, freebb);
            LLVMPositionBuilderAtEnd(ctx->builder, freebb);

            LLVMValueRef free_fn = LLVMGetNamedFunction(ctx->module, "free");
            if (free_fn) {
                LLVMTypeRef ft = LLVMGlobalGetValueType(free_fn);
                LLVMBuildCall2(ctx->builder, ft, free_fn, &env_ptr, 1, "");
            }
            LLVMBuildBr(ctx->builder, cont_bb);
            LLVMPositionBuilderAtEnd(ctx->builder, cont_bb);
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
            else if (sym->type->kind == TYPE_BLOCK)
            {
                count++; /* Phase C closure cleanup: free env_ptr if non-NULL */
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
                if (drop_fn == NULL)
                {
                    /* Module function bodies are emitted before the main-file
                       Pass 2.5 that generates struct auto-drop fns, so a has_drop
                       struct local in a module function would otherwise fall to
                       the inline fallback below (which does not free vec/map/enum
                       fields → leak). Generate the complete drop fn on demand. */
                    emit_auto_drop_fn(ctx, sym->type);
                    drop_fn = (LLVMValueRef)sym->type->as.strukt.drop_fn;
                }
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
            else if (sym->type->kind == TYPE_ENUM && sym->type->as.enom.has_drop)
            {
                /* F.5: Enum with heap payload — conditional on moved_flag. */
                emit_enum_drop_cond(ctx, sym->value, sym->type, sym->moved_flag);
                idx++;
            }
            else if (sym->type->kind == TYPE_BLOCK)
            {
                /* Phase C/C.5 closure cleanup: env_ptr non-NULL → call its
                   drop_fn (slot 0) if present, then free env block. */
                LLVMTypeRef block_llvm = type_to_llvm(ctx, sym->type);
                LLVMValueRef blk_val = LLVMBuildLoad2(ctx->builder, block_llvm,
                                                      sym->value, "blk.cleanup");
                LLVMValueRef env_ptr = LLVMBuildExtractValue(
                    ctx->builder, blk_val, 1, "blk.env.cleanup");
#if CG_DEBUG
                {
                    /* F.6: log block.drop at scope exit (emit_cleanup_to path). */
                    char lbl[64];
                    snprintf(lbl, sizeof(lbl), "var='%s'", sym->name ? sym->name : "?");
                    cg_dbg_block_op(ctx, "drop", lbl, env_ptr);
                }
#endif
                LLVMTypeRef ptr_t = LLVMPointerTypeInContext(ctx->context, 0);
                LLVMValueRef is_nn = LLVMBuildICmp(
                    ctx->builder, LLVMIntNE, env_ptr,
                    LLVMConstNull(ptr_t), "blk.env.nn");
                char free_name[32], call_name[32], dofree_name[32], cont_name[32];
                snprintf(free_name,   sizeof(free_name),   "blk.maybe%d", idx);
                snprintf(call_name,   sizeof(call_name),   "blk.dropcall%d", idx);
                snprintf(dofree_name, sizeof(dofree_name), "blk.dofree%d", idx);
                snprintf(cont_name,   sizeof(cont_name),   "blk.cont%d", idx);
                LLVMBasicBlockRef maybe_bb = LLVMAppendBasicBlockInContext(
                    ctx->context, cur_fn, free_name);
                LLVMBasicBlockRef call_bb  = LLVMAppendBasicBlockInContext(
                    ctx->context, cur_fn, call_name);
                LLVMBasicBlockRef do_bb    = LLVMAppendBasicBlockInContext(
                    ctx->context, cur_fn, dofree_name);
                LLVMBasicBlockRef cont_bb  = LLVMAppendBasicBlockInContext(
                    ctx->context, cur_fn, cont_name);
                LLVMBuildCondBr(ctx->builder, is_nn, maybe_bb, cont_bb);

                LLVMPositionBuilderAtEnd(ctx->builder, maybe_bb);
                LLVMValueRef drop_fn_p = LLVMBuildLoad2(
                    ctx->builder, ptr_t, env_ptr, "blk.drop");
                LLVMValueRef has_drop = LLVMBuildICmp(
                    ctx->builder, LLVMIntNE, drop_fn_p,
                    LLVMConstNull(ptr_t), "blk.has_drop");
                LLVMBuildCondBr(ctx->builder, has_drop, call_bb, do_bb);

                LLVMPositionBuilderAtEnd(ctx->builder, call_bb);
                {
                    LLVMTypeRef dp[1] = { ptr_t };
                    LLVMTypeRef dft = LLVMFunctionType(
                        LLVMVoidTypeInContext(ctx->context), dp, 1, 0);
                    LLVMBuildCall2(ctx->builder, dft, drop_fn_p, &env_ptr, 1, "");
                }
                LLVMBuildBr(ctx->builder, do_bb);

                LLVMPositionBuilderAtEnd(ctx->builder, do_bb);
                cg_emit_free(ctx, env_ptr, "closure.env",
                             CG_LINE(ctx), CG_COL(ctx));
                LLVMBuildBr(ctx->builder, cont_bb);

                LLVMPositionBuilderAtEnd(ctx->builder, cont_bb);
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
    {
        /* The struct's auto-drop fn has not been generated yet. This happens
           when a has_drop struct LOCAL is dropped inside a *module* function,
           whose body is emitted before the main-file Pass 2.5 that generates
           drop fns (root/main functions are emitted after, so they're fine).
           Generate on demand — emit_auto_drop_fn saves/restores the builder. */
        emit_auto_drop_fn(ctx, struct_type);
        drop_fn = (LLVMValueRef)struct_type->as.strukt.drop_fn;
        if (drop_fn == NULL)
            return;
    }

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
    {
        /* Lazily generate the auto-drop fn (see emit_struct_drop_cond): module
           function bodies are emitted before the main-file Pass 2.5. */
        emit_auto_drop_fn(ctx, struct_type);
        drop_fn = (LLVMValueRef)struct_type->as.strukt.drop_fn;
        if (drop_fn == NULL)
            return NULL;
    }

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
    if (struct_type->as.strukt.has_user_drop)
    {
        cg_ensure_user_struct_drop_decl(ctx, struct_type);
        return;
    }
    if (struct_type->as.strukt.field_count == 0)
        return;

    /* Save builder position so we can restore after generating the new function */
    LLVMBasicBlockRef saved_bb = LLVMGetInsertBlock(ctx->builder);

    /* B-2: use LLVM-prefixed name for module-defined structs */
    const char *struct_name = struct_llvm_name(struct_type);
    char drop_fn_name[256];
    snprintf(drop_fn_name, sizeof(drop_fn_name), "%s.__drop", struct_name);

    /* Check if already defined */
    {
        LLVMValueRef existing = LLVMGetNamedFunction(ctx->module, drop_fn_name);
        if (existing != NULL)
        {
            /* BF-046: link THIS Type object to the existing drop fn. Otherwise a
               second Type instance for the same struct can keep drop_fn == NULL
               and cleanup callers may silently skip the value drop. */
            struct_type->as.strukt.drop_fn = existing;
            return;
        }
    }

    /* Create function type: void __drop(*Struct) */
    LLVMTypeRef ptr_struct = LLVMPointerTypeInContext(ctx->context, 0);
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

    LLVMTypeRef llvm_struct = type_to_llvm(ctx, struct_type);

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

        /* Drop Block fields (call env drop_fn then free env) */
        if (field_type->kind == TYPE_BLOCK)
        {
            LLVMTypeRef ptr_t = LLVMPointerTypeInContext(ctx->context, 0);
            LLVMTypeRef blk_fields[2] = { ptr_t, ptr_t };
            LLVMTypeRef blk_t = LLVMStructTypeInContext(ctx->context, blk_fields, 2, 0);
            LLVMValueRef field_ptr = LLVMBuildStructGEP2(ctx->builder, llvm_struct,
                                                         self_ptr, (unsigned)i, "drop.blkfield");
            LLVMValueRef blk_val = LLVMBuildLoad2(ctx->builder, blk_t, field_ptr, "drop.blk");
            LLVMValueRef env_p = LLVMBuildExtractValue(ctx->builder, blk_val, 1, "drop.blk.env");
            cg_dbg_block_op(ctx, "field.drop",
                            struct_type->as.strukt.fields[i].name
                                ? struct_type->as.strukt.fields[i].name
                                : "block.field",
                            env_p);
            cg_emit_block_env_drop(ctx, env_p);
            continue;
        }

        /* Drop has_drop enum fields (call the enum's __drop). */
        if (field_type->kind == TYPE_ENUM && field_type->as.enom.has_drop)
        {
            LLVMValueRef field_ptr = LLVMBuildStructGEP2(ctx->builder, llvm_struct,
                                                         self_ptr, (unsigned)i, "drop.enomfield");
            emit_enum_drop(ctx, field_ptr, field_type);
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
            /* B-2: use LLVM-prefixed name for module-defined member structs */
            const char *member_name = struct_llvm_name(field_type);
            char member_drop_name[256];
            snprintf(member_drop_name, sizeof(member_drop_name), "%s.__drop", member_name);
            LLVMValueRef member_drop_fn = LLVMGetNamedFunction(ctx->module, member_drop_name);
            if (member_drop_fn == NULL)
            {
                /* Fallback: use drop_fn stored in the type (set by Pass 2a or earlier iteration) */
                member_drop_fn = (LLVMValueRef)field_type->as.strukt.drop_fn;
            }
            if (member_drop_fn == NULL)
            {
                /* The member's __drop hasn't been generated yet. This happens when
                   the OUTER struct's drop fn is emitted lazily (e.g. a Vec(Person)
                   method monomorphization triggers Person.__drop before the main-file
                   Pass 2.5 reaches Inner). Generate it on demand — emit_auto_drop_fn
                   saves/restores the builder, so the recursion is position-safe (same
                   pattern as emit_struct_drop_cond/_separate). Without this, the nested
                   struct's owned fields (Inner.tag) silently leak. */
                emit_auto_drop_fn(ctx, field_type);
                member_drop_fn = (LLVMValueRef)field_type->as.strukt.drop_fn;
                if (member_drop_fn == NULL)
                    member_drop_fn = LLVMGetNamedFunction(ctx->module, member_drop_name);
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

    /* USER __drop not yet stamped on this Type instance (its defining module's
       body emits after the consumer module): forward-declare by llvm_name and
       call it. Falling through to the inline fallback would SKIP raw-pointer
       fields entirely — for a struct like Str{*u8,int,int} that silently frees
       nothing (leak; hit by `+`-chain temps inside module functions). */
    if (drop_fn == NULL && struct_type->as.strukt.has_user_drop)
        drop_fn = cg_ensure_user_struct_drop_decl(ctx, struct_type);

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
        /* ABI policy for reference types: uniform pointer ABI.
           P4(string→Str) removed the sole by-value specialisation
           (read-only &string, 16-byte POD with cap=0 marker); the checker
           now only admits &struct / &!struct / &enum pointees, all pointer.
           emit_scope_cleanup honours is_borrowed on the CgSymbol so
           borrowed slots are never freed. */
        return LLVMPointerTypeInContext(ctx->context, 0);
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
    case TYPE_BLOCK:
    {
        /* Phase B: closure value = 16-byte fat pointer { fn_ptr, env_ptr }.
           Both slots are opaque ptr; the actual signature lives in the LS
           type system (callee uses it to type-cast at the call site). */
        LLVMTypeRef ptr_t = LLVMPointerTypeInContext(ctx->context, 0);
        LLVMTypeRef fields[2] = { ptr_t, ptr_t };
        return LLVMStructTypeInContext(ctx->context, fields, 2, 0);
    }
    case TYPE_STRUCT:
    {
        if (t->as.strukt.name)
        {
            /* B-2: use prefixed LLVM name for module-defined structs */
            const char *ln = struct_llvm_name(t);
            LLVMTypeRef found = find_struct_llvm(ctx, ln);
            if (found)
                return found;
        }
        /* Fallback: build struct type and register it (G1: generic instances
           like "Pair(int,string)" arrive here because codegen_struct_decl
           skips templates; create a named LLVM struct and cache it). */
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
        LLVMTypeRef st;
        if (t->as.strukt.name)
        {
            const char *ln = struct_llvm_name(t);
            st = LLVMStructCreateNamed(ctx->context, ln);
            LLVMStructSetBody(st, fields, (unsigned)n, 0);
            register_struct_llvm(ctx, ln, st, (Type *)t);
        }
        else
        {
            st = LLVMStructTypeInContext(ctx->context, fields, (unsigned)n, 0);
        }
        free(fields);
        return st;
    }
    case TYPE_ENUM:
    {
        if (t->as.enom.name)
        {
            /* B-2: use prefixed LLVM name for module-defined enums */
            const char *ln = enum_llvm_name_of(t);
            LLVMTypeRef found = find_enum_llvm(ctx, ln);
            if (found) return found;

            /* Lazy build for instantiated templates (Option(int), Result(...)).
               Mirrors codegen_enum_decl but works straight from the Type
               structure without an AST node. Aligned payload (bug #25). */
            int max_payload = 0, max_align = 1;
            cg_enum_payload_dims(ctx, t, &max_payload, &max_align);
            LLVMTypeRef body[2];
            cg_enum_body_fields(ctx, max_payload, max_align, body);
            LLVMTypeRef llvm_type = LLVMStructCreateNamed(ctx->context, ln);
            LLVMStructSetBody(llvm_type, body, 2, 0);
            register_enum_llvm(ctx, ln, llvm_type, t, max_payload);
            if (t->as.enom.has_drop)
                emit_auto_enum_drop_fn(ctx, t);
            return llvm_type;
        }
        LLVMTypeRef i8 = LLVMInt8TypeInContext(ctx->context);
        LLVMTypeRef payload = LLVMArrayType2(i8, 0);
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

/* ---- Printf format specifier for a given type ---- */

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
        }
    }

    int slen = (int)strlen(conv);
    if (*p_fmt_len + slen < fmt_cap - 4) {
        memcpy(fmt_buf + *p_fmt_len, conv, (size_t)slen);
        *p_fmt_len += slen;
    }
    return val;
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
    LLVMValueRef data = LLVMBuildExtractValue(ctx->builder, val, 0, "Str.d");
    LLVMValueRef len  = LLVMBuildExtractValue(ctx->builder, val, 1, "Str.l");
    LLVMValueRef args[2] = { len, data };
    emit_printf(ctx, "%.*s", args, 2);
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

        if (ftype->kind == TYPE_STRUCT)
        {
            /* Nested struct — recurse */
            codegen_print_struct_value(ctx, fval, ftype);
            continue;
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

#undef VEC_MAX_PRINT_ELEMS

/* Codegen for print() with any type — generates inline printf */
static LLVMValueRef codegen_print_call(CodegenContext *ctx, AstNode *node)
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
                    printf_args[printf_argc++] =
                        LLVMBuildExtractValue(ctx->builder, sval, 1, "Str.l");
                    printf_args[printf_argc++] =
                        LLVMBuildExtractValue(ctx->builder, sval, 0, "Str.d");
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
                (arg->kind == AST_INDEX || arg->kind == AST_CALL))
            {
                LLVMValueRef stmp = cg_entry_alloca(ctx, type_to_llvm(ctx, t),
                                                    "print.drop");
                LLVMBuildStore(ctx->builder, sval, stmp);
                emit_drop_value(ctx, stmp, t);
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
           registered via cg_push_temp_string and freed by cg_flush_temps at
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
            vals[val_count++] = LLVMBuildExtractValue(ctx->builder, val, 1, "Str.l");
            vals[val_count++] = LLVMBuildExtractValue(ctx->builder, val, 0, "Str.d");
            /* Owned Str rvalue interpolated → drop after the result is built
               (statement-end flush runs after the snprintf below). Besides
               call/index clones this covers FIELD reads (a terminal has_drop
               field read CLONES — f"{e.color}" leaked one per evaluation) and
               lowered operator chains (f"{a + b}"). Bare ident is a borrow. */
            if (expr->kind == AST_CALL || expr->kind == AST_INDEX ||
                expr->kind == AST_FIELD ||
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
        LLVMValueRef sv = LLVMGetUndef(st);
        sv = LLVMBuildInsertValue(ctx->builder, sv, buf, 0, "Str.d");
        sv = LLVMBuildInsertValue(ctx->builder, sv, n, 1, "Str.l");
        sv = LLVMBuildInsertValue(ctx->builder, sv, cap, 2, "Str.c");
        return sv;
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
    LLVMValueRef empty_str = LLVMGetUndef(str_t);
    empty_str = LLVMBuildInsertValue(ctx->builder, empty_str, empty_data, 0, "fromcstr.e0");
    empty_str = LLVMBuildInsertValue(ctx->builder, empty_str, LLVMConstInt(i32_t, 0, 0), 1, "fromcstr.e1");
    empty_str = LLVMBuildInsertValue(ctx->builder, empty_str, LLVMConstInt(i32_t, 0, 0), 2, "fromcstr.e2");
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

    LLVMValueRef ok_str = LLVMGetUndef(str_t);
    ok_str = LLVMBuildInsertValue(ctx->builder, ok_str, buf, 0, "fromcstr.o0");
    ok_str = LLVMBuildInsertValue(ctx->builder, ok_str, len32, 1, "fromcstr.o1");
    ok_str = LLVMBuildInsertValue(ctx->builder, ok_str, cap32, 2, "fromcstr.o2");
    LLVMBuildBr(ctx->builder, cont_bb);

    /* phi the two paths. The result is an owned has_drop Str rvalue — the
       generic struct rvalue protocol (var-decl transfer / call-arg spill /
       expr-stmt drop) takes it from here; no temp_string registration. */
    LLVMPositionBuilderAtEnd(ctx->builder, cont_bb);
    LLVMValueRef phi = LLVMBuildPhi(ctx->builder, str_t, "fromcstr.r");
    LLVMValueRef vals[2] = { empty_str, ok_str };
    LLVMBasicBlockRef blks[2] = { null_bb, ok_bb };
    LLVMAddIncoming(phi, vals, blks, 2);
    (void)i8_t;
    return phi;
}

static LLVMValueRef codegen_expr_or_borrow(CodegenContext *ctx, AstNode *node)
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
    if (node->kind == AST_CALL || node->kind == AST_FORMAT_STRING)
    {
        Type *rtype = node->resolved_type;
        if (rtype == NULL)
            return NULL;
        LLVMValueRef val = codegen_expr(ctx, node);
        if (val == NULL)
            return NULL;
        LLVMTypeRef rllvm = type_to_llvm(ctx, rtype);
        LLVMValueRef tmp = cg_entry_alloca(ctx, rllvm, "tmp.rval.self");
        LLVMBuildStore(ctx->builder, val, tmp);
        cg_push_temp_drop(ctx, tmp, rtype);
        return tmp;
    }

    return NULL; /* Other lvalue forms not yet handled */
}

/* ---- Match OR-pattern helpers ---- */

/* Return true if 'pat' (possibly an OR-pattern tree) consists entirely of
   integer-literal leaves.  Wildcards are checked separately and skipped. */
static bool match_pattern_all_int_const(AstNode *pat)
{
    if (pat->kind == AST_MATCH_OR_PATTERN)
        return match_pattern_all_int_const(pat->as.or_pattern.left) &&
               match_pattern_all_int_const(pat->as.or_pattern.right);
    return pat->kind == AST_INT_LIT;
}

/* Flatten OR-pattern tree into an array of long-long integer values.
   Returns the number of values written (≤ max).  Non-INT_LIT leaves are
   silently skipped (should not happen when called after the int-const check). */
static int match_collect_int_vals(AstNode *pat, long long *out, int max)
{
    if (max <= 0) return 0;
    if (pat->kind == AST_MATCH_OR_PATTERN) {
        int n  = match_collect_int_vals(pat->as.or_pattern.left,  out,     max);
        int n2 = match_collect_int_vals(pat->as.or_pattern.right, out + n, max - n);
        return n + n2;
    }
    if (pat->kind == AST_INT_LIT) {
        out[0] = pat->as.int_lit.value;
        return 1;
    }
    return 0;
}

/* ---- Expression codegen ---- */

/* A-1 (docs/plan_runtime_primitives.md): structural match of a canonical-path
   call to a std.c primitive — `std.c.malloc/realloc/free/abort`. Mirrors the
   checker's match_stdc_prim. Returns 0=malloc 1=realloc 2=free 3=abort, else -1.
   These lower to exactly the same CRT/runtime calls the bare builtins emitted. */
static int cg_match_stdc_prim(AstNode *callee)
{
    if (callee == NULL || callee->kind != AST_FIELD)
        return -1;
    AstNode *mid = callee->as.field_access.object;
    if (mid == NULL || mid->kind != AST_FIELD)
        return -1;
    AstNode *head = mid->as.field_access.object;
    if (head == NULL || head->kind != AST_IDENT)
        return -1;
    if (strcmp(head->as.ident.name, "std") != 0)
        return -1;
    if (strcmp(mid->as.field_access.field, "c") != 0)
        return -1;
    const char *f = callee->as.field_access.field;
    if (strcmp(f, "malloc") == 0)  return 0;
    if (strcmp(f, "realloc") == 0) return 1;
    if (strcmp(f, "free") == 0)    return 2;
    if (strcmp(f, "abort") == 0)   return 3;
    return -1;
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

        bool is_fp = lt && type_is_float(lt);
        bool is_signed_int = lt && type_is_signed(lt);

        switch (node->as.binary.op)
        {
        case TOKEN_PLUS:
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

        /* Intercept __drop_at(place) — run the recursive destructor on the value
           stored at an lvalue place (raw pointer slot p[i], field, *p) WITHOUT
           freeing any backing buffer. No-op for POD. Returns void. The nested
           drop is automatic: emit_drop_value recurses (string free / vec / map /
           struct.__drop / enum.__drop), so __drop_at on a RawVec(RawVec(T)) slot
           dispatches to the inner RawVec's user __drop. */
        if (node->as.call.callee->kind == AST_IDENT &&
            strcmp(node->as.call.callee->as.ident.name, "__drop_at") == 0 &&
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
            strcmp(node->as.call.callee->as.ident.name, "__take") == 0 &&
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
                   Only for known primitive types — not modules, enums, etc. */
                else if (deref->kind == TYPE_INT  || deref->kind == TYPE_I64 ||
                         deref->kind == TYPE_F64  || deref->kind == TYPE_BOOL ||
                         deref->kind == TYPE_CHAR)
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
                Type *deref = obj_type;
                if (deref->kind == TYPE_POINTER && deref->as.pointer_to &&
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
                   like obj.map(string)(...)), append them: StructName.method(type) */
                static char qualified_name[512];
                int npos = snprintf(qualified_name, sizeof(qualified_name), "%s.%s",
                                    struct_name ? struct_name : "", method_name);
                if (node->as.call.type_arg_count > 0)
                {
                    npos += snprintf(qualified_name + npos, sizeof(qualified_name) - (size_t)npos, "(");
                    for (int ti = 0; ti < node->as.call.type_arg_count; ti++)
                    {
                        if (ti > 0)
                            npos += snprintf(qualified_name + npos, sizeof(qualified_name) - (size_t)npos, ",");
                        TypeNode *tn = node->as.call.type_args[ti];
                        const char *tname = "?";
                        if (tn->kind == TYPE_NODE_PRIMITIVE)
                        {
                            switch (tn->as.primitive)
                            {
                            case TOKEN_TYPE_INT:    tname = "int";    break;
                            case TOKEN_TYPE_I8:     tname = "i8";     break;
                            case TOKEN_TYPE_I16:    tname = "i16";    break;
                            case TOKEN_TYPE_I32:    tname = "i32";    break;
                            case TOKEN_TYPE_I64:    tname = "i64";    break;
                            case TOKEN_TYPE_U8:     tname = "u8";     break;
                            case TOKEN_TYPE_U16:    tname = "u16";    break;
                            case TOKEN_TYPE_U32:    tname = "u32";    break;
                            case TOKEN_TYPE_U64:    tname = "u64";    break;
                            case TOKEN_TYPE_F32:    tname = "f32";    break;
                            case TOKEN_TYPE_F64:    tname = "f64";    break;
                            case TOKEN_TYPE_BOOL:   tname = "bool";   break;
                            case TOKEN_TYPE_CHAR:   tname = "char";   break;
                            default:                tname = "?";      break;
                            }
                        }
                        else if (tn->kind == TYPE_NODE_NAMED)
                        {
                            tname = tn->as.named.name;
                        }
                        npos += snprintf(qualified_name + npos, sizeof(qualified_name) - (size_t)npos, "%s", tname);
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

                /* G2: generic function call — look up by mangled name */
                if (node->as.call.type_arg_count > 0)
                {
                    static char g2_mangled[512];
                    int pos = snprintf(g2_mangled, sizeof(g2_mangled), "%s(", fn_name);
                    for (int ti = 0; ti < node->as.call.type_arg_count; ti++)
                    {
                        if (ti > 0) g2_mangled[pos++] = ',';
                        TypeNode *tn = node->as.call.type_args[ti];
                        const char *tname = "?";
                        if (tn->kind == TYPE_NODE_PRIMITIVE)
                        {
                            switch (tn->as.primitive)
                            {
                            case TOKEN_TYPE_INT:    tname = "int";    break;
                            case TOKEN_TYPE_I8:     tname = "i8";     break;
                            case TOKEN_TYPE_I16:    tname = "i16";    break;
                            case TOKEN_TYPE_I32:    tname = "i32";    break;
                            case TOKEN_TYPE_I64:    tname = "i64";    break;
                            case TOKEN_TYPE_U8:     tname = "u8";     break;
                            case TOKEN_TYPE_U16:    tname = "u16";    break;
                            case TOKEN_TYPE_U32:    tname = "u32";    break;
                            case TOKEN_TYPE_U64:    tname = "u64";    break;
                            case TOKEN_TYPE_F32:    tname = "f32";    break;
                            case TOKEN_TYPE_F64:    tname = "f64";    break;
                            case TOKEN_TYPE_BOOL:   tname = "bool";   break;
                            case TOKEN_TYPE_CHAR:   tname = "char";   break;
                            default:                tname = "?";      break;
                            }
                        }
                        else if (tn->kind == TYPE_NODE_NAMED)
                        {
                            tname = tn->as.named.name;
                        }
                        pos += snprintf(g2_mangled + pos, sizeof(g2_mangled) - (size_t)pos, "%s", tname);
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
                if (callee == NULL && node->as.call.type_arg_count > 0)
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
                    strcmp(mod_t->as.module.name, "math") == 0)
                {
                    return builtin_math_emit_call(ctx, fn_name,
                                                  node->as.call.args,
                                                  node->as.call.arg_count);
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
                LLVMValueRef arg_val;
                arg_val = codegen_expr(ctx, node->as.call.args[i]);
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
                    arg_type->as.strukt.has_drop &&
                    /* B-3: a coerced Str→string arg is already an LsString with
                       cap=-2 (borrowed) — the struct clone path must not fire. */
                    !node->as.call.args[i]->coerce_str_to_string)
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
                strcmp(obj_type->as.module.name, "math") == 0)
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
            /* L-013: zero-initialize so a path that reaches merge without storing
               (e.g. a non-exhaustive integer match's default branch) leaves the
               registered result temp with cap=0 / empty → its free/drop is skipped. */
            LLVMBuildStore(tmp, LLVMConstNull(res_llvm), result_alloca);
            LLVMDisposeBuilder(tmp);
        }

        Type *subj_type = node->as.match.subject->resolved_type;
        bool is_fp = subj_type && type_is_float(subj_type);

        /* L-012: an OWNED rvalue-temp scrutinee (a call/index/field/ctor result,
           not a named var or borrow) has no other owner, so the match itself must
           drop it. For such subjects we (a) clone every has_drop binder so it is
           independent of the subject, and (b) register the subject for drop. A
           named-var / &self subject keeps the existing borrow behavior (its owner
           drops it; binders alias read-only). */
        bool subj_owned_temp =
            subj_type && subj_type->kind == TYPE_ENUM &&
            subj_type->as.enom.has_drop &&
            node->as.match.subject->kind != AST_IDENT &&
            node->as.match.subject->kind != AST_UNARY &&
            node->as.match.subject->kind != AST_MUT_BORROW;

        /* ---- Enum subject: switch on discriminant + binder extraction ---- */
        if (subj_type && subj_type->kind == TYPE_ENUM)
        {
            LLVMTypeRef enum_llvm = type_to_llvm(ctx, subj_type);
            LLVMTypeRef i8 = LLVMInt8TypeInContext(ctx->context);
            LLVMTypeRef ptr_type = LLVMPointerTypeInContext(ctx->context, 0);

            /* Phase 9: detect borrow subject — AST_IDENT with is_borrowed=true.
               For &Enum params, sym->value IS the pointer; skip alloca+store copy
               and GEP directly through the pointer (zero-copy borrow match). */
            bool subj_is_enum_borrow = false;
            LLVMValueRef subj_ptr_val = NULL; /* pointer to the enum, borrow path */
            {
                AstNode *sn = node->as.match.subject;
                if (sn->kind == AST_IDENT) {
                    CgSymbol *bsym = cg_scope_resolve(ctx->current_scope,
                                                      sn->as.ident.name);
                    if (bsym && bsym->is_borrowed) {
                        subj_is_enum_borrow = true;
                        subj_ptr_val = bsym->value;
                    }
                }
            }

            LLVMValueRef subj_alloca;  /* pointer (alloca or incoming ptr) used for GEP */
            LLVMBasicBlockRef entry = LLVMGetEntryBasicBlock(ctx->current_fn);
            if (subj_is_enum_borrow) {
                /* Borrow path: use the incoming pointer directly — no copy. */
                subj_alloca = subj_ptr_val;
            } else {
                /* Owned path: stash subject in an alloca so we can GEP into the payload. */
                LLVMBuilderRef tmp_b = LLVMCreateBuilderInContext(ctx->context);
                LLVMValueRef first_inst = LLVMGetFirstInstruction(entry);
                if (first_inst) LLVMPositionBuilderBefore(tmp_b, first_inst);
                else            LLVMPositionBuilderAtEnd(tmp_b, entry);
                subj_alloca = LLVMBuildAlloca(tmp_b, enum_llvm, "match.subj");
                LLVMDisposeBuilder(tmp_b);
                LLVMBuildStore(ctx->builder, subject, subj_alloca);

                /* L-012: own the temp scrutinee — drop it at statement end / on return
                   (binders below are cloned, so this never double-frees). */
                if (subj_owned_temp)
                    cg_push_temp_drop(ctx, subj_alloca, subj_type);
            }

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
                    int arm_str_mark = ctx->temp_string_count;   /* L-013 */
                    int arm_drop_floor = ctx->temp_drop_count;
                    LLVMValueRef body_val = codegen_expr(ctx, arm->body);
                    if (result_alloca && body_val)
                    {
                        body_val = cg_match_arm_own_tail(
                            ctx, cg_match_arm_tail(arm->body), body_val, res_llvm,
                            result_type, arm_str_mark, arm_drop_floor,
                            /*did_move_out_binder=*/false);
                        LLVMBuildStore(ctx->builder, body_val, result_alloca);
                    }
                    cg_match_arm_encapsulate(ctx, arm_str_mark, arm_drop_floor, result_type);
                    if (LLVMGetBasicBlockTerminator(LLVMGetInsertBlock(ctx->builder)) == NULL)
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

                        /* Phase 9 / Phase B: borrow subject — owned payload bindings
                           are borrows: zero-copy for vec/map/struct/nested-enum via
                           direct field pointer; cap-marked borrow for string. */
                        if (subj_is_enum_borrow) {
                            /* Phase A: self-recursive (box) payload. */
                            if (pt == subj_type) {
                                LLVMValueRef box = LLVMBuildLoad2(ctx->builder, ptr_type,
                                                                  field_ptr, "box");
                                CgSymbol *sym = cg_scope_define(ctx->current_scope, bname,
                                                                box, pt, NULL);
                                if (sym) sym->is_borrowed = true;
                                continue;
                            }

                            /* Phase B: vec/map/struct/nested-has_drop-enum payload.
                               Use field_ptr directly as sym->value (pointer into the
                               enum payload storage) — zero-copy, same ABI as &T params. */
                            if (pt->kind == TYPE_STRUCT ||
                                (pt->kind == TYPE_ENUM && pt->as.enom.has_drop)) {
                                CgSymbol *sym = cg_scope_define(ctx->current_scope, bname,
                                                                field_ptr, pt, NULL);
                                if (sym) sym->is_borrowed = true;
                                continue;
                            }

                            /* Scalars (int/f64/bool/char) and other non-owned types:
                               fall through to the normal load-into-alloca path below. */
                        }

                        LLVMValueRef val;
                        if (pt == subj_type) {
                            /* Self-recursive payload (owned subject): payload slot stores
                               an i8* pointing at a heap-boxed enum.  Load the box
                               pointer, then load the enum value through it. */
                            LLVMValueRef box = LLVMBuildLoad2(ctx->builder, ptr_type,
                                                              field_ptr, "box");
                            val = LLVMBuildLoad2(ctx->builder, field_llvm, box, bname);
                        } else {
                            val = LLVMBuildLoad2(ctx->builder, field_llvm, field_ptr, bname);
                        }

                        /* Determine whether the binder needs an independent owned copy.
                           Without cloning, a string binder shares the enum's data pointer.
                           If the binder escapes the arm (via `return s`), both the caller
                           and the enum's drop (env_drop or scope cleanup) would free the
                           same allocation → double-free.
                           Fix: clone string payloads so each binder independently owns
                           its data.  With independent ownership, is_borrowed=false and
                           scope cleanup frees the binder's copy when the arm exits
                           (unless the binder is being returned, in which case the
                           return_alloca skip list suppresses the scope drop). */
                        bool binder_owns = false;
                        if (subj_owned_temp && pt && cg_type_owns_heap_for_enum(pt)) {
                            /* Owned-temp subject: clone every has_drop binder so it
                               is independent of the subject (which we drop). */
                            val = emit_clone_value(ctx, val, field_llvm, pt);
                            binder_owns = true;
                        }

                        /* Materialise an alloca so existing IDENT-load paths work,
                           then bind in current scope.  Non-string (or non-cloned)
                           binders are marked borrowed so scope cleanup leaves heap
                           ownership with the enum subject. */
                        LLVMBuilderRef bb_tmp = LLVMCreateBuilderInContext(ctx->context);
                        LLVMValueRef first_i = LLVMGetFirstInstruction(entry);
                        if (first_i) LLVMPositionBuilderBefore(bb_tmp, first_i);
                        else         LLVMPositionBuilderAtEnd(bb_tmp, entry);
                        LLVMValueRef bind_alloca = LLVMBuildAlloca(bb_tmp, field_llvm, bname);
                        LLVMDisposeBuilder(bb_tmp);
                        LLVMBuildStore(ctx->builder, val, bind_alloca);
                        CgSymbol *sym = cg_scope_define(ctx->current_scope, bname,
                                                        bind_alloca, pt, NULL);
                        if (sym) sym->is_borrowed = !binder_owns;
                    }
                }

                int arm_str_mark = ctx->temp_string_count;   /* L-013 */
                int arm_drop_floor = ctx->temp_drop_count;
                LLVMValueRef body_val = codegen_expr(ctx, arm->body);
                bool did_move_out_binder = false;
                AstNode *tail = cg_match_arm_tail(arm->body);
                /* BF-026 / BF-029 / VR-LIM-020: a match arm clones every owned
                   has_drop payload binder (binder_owns above), so the arm scope
                   normally frees that clone on exit to avoid leaks.
                   EXCEPTION — "move out": when the arm's RESULT value is exactly one
                   of those binders, the value is transferred to the match result (and
                   on to whoever owns it), so freeing it here would double-free. The
                   tail value is the binder both for `=> binder` and for the block form
                   `=> { ...; binder }`. Suppress the binder's scope-cleanup drop by
                   marking it borrowed — uniform across string / has_drop struct·enum /
                   map (body_val, the already-loaded SSA, is what the caller receives). */
                if (body_val && tail && tail->kind == AST_IDENT)
                {
                    /* Resolve ONLY in the arm scope (the payload binders), not in
                       outer scopes — we must not silently move out outer locals. */
                    for (int si = ctx->current_scope->count - 1; si >= 0; si--)
                    {
                        CgSymbol *bs = &ctx->current_scope->symbols[si];
                        if (!bs->name ||
                            strcmp(bs->name, tail->as.ident.name) != 0)
                            continue;
                        Type *bt = bs->type;
                        bool owns_heap =
                            bt && !bs->is_borrowed && bs->value &&
                            ((bt->kind == TYPE_STRUCT && bt->as.strukt.has_drop) ||
                             (bt->kind == TYPE_ENUM && bt->as.enom.has_drop));
                        if (owns_heap)
                        {
                            bs->is_borrowed = true; /* skip drop: moved out */
                            did_move_out_binder = true;
                        }
                        break;
                    }
                }
                /* L-013: make the result OWN its value independently. Clone a tail that
                   aliases an outer local or a borrowed binder (must run while the arm
                   scope is still alive so the tail IDENT resolves to the binder). */
                if (result_alloca && body_val)
                    body_val = cg_match_arm_own_tail(ctx, tail, body_val, res_llvm,
                                                     result_type, arm_str_mark,
                                                     arm_drop_floor, did_move_out_binder);
                if (result_alloca && body_val)
                    LLVMBuildStore(ctx->builder, body_val, result_alloca);
                emit_scope_cleanup(ctx);
                pop_scope(ctx);
                /* L-013: encapsulate arm-body temps (transfer the tail temp into the
                   result, free the rest). Subject drop (index < arm_drop_floor) and
                   outer temps (< arm_str_mark) are preserved. */
                cg_match_arm_encapsulate(ctx, arm_str_mark, arm_drop_floor, result_type);
                /* Guard: arm body may end with 'return', which already terminates
                   the block.  Only emit the merge-branch when the block is still
                   open (no terminator yet). */
                if (LLVMGetBasicBlockTerminator(LLVMGetInsertBlock(ctx->builder)) == NULL)
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
                if (!subject_owned_by_scope && !is_self_recursive) {
                    emit_enum_drop(ctx, subj_alloca, subj_type);
                    /* The owned-temp subject is ALSO on the temp-drop list (L-012,
                       which covers early-return arms this merge-block drop misses).
                       Having just dropped it here on the fall-through path, remove
                       it from that list so the statement-end flush does not drop it
                       a SECOND time. Without this, the double drop is masked for
                       idempotent string free (cap zeroed) but double-frees user
                       structs/containers whose __drop doesn't zero cap (Vec/Map). */
                    cg_remove_temp_drop(ctx, subj_alloca);
                }
            }

            /* L-013: the match result is now an owned-rvalue funneled through
               result_alloca (each arm transferred/cloned its owned tail into it).
               Register it as the single statement-level result temp so the consumer
               (var_decl/assign/return/call-arg) transfers it via the existing
               "last temp moved" protocol — exactly one drop, no leak / no double-free.
               Non-owned (static/POD) results are no-ops here. */
            if (result_alloca)

            if (result_alloca)
                return LLVMBuildLoad2(ctx->builder, res_llvm, result_alloca, "match.val");
            return NULL;
        }

        /* ---- Non-enum subject ----
           Two sub-paths:
           (A) Integer switch: subject is integer-typed AND every non-wildcard
               pattern leaf is an AST_INT_LIT.  We emit a single LLVM switch
               instruction (like the enum path above), supporting OR-patterns
               that map multiple constants to one arm body.
           (B) CondBr chain: string, float, or patterns that contain variables.
               OR-patterns are supported by flattening the tree into leaves and
               OR-ing the comparisons together before the CondBr. */

        bool use_int_switch = false;
        if (subj_type && !is_fp)
        {
            /* All non-wildcard patterns must be integer constants. */
            use_int_switch = true;
            for (int i = 0; i < node->as.match.arm_count; i++)
            {
                AstNode *pat = node->as.match.arms[i].pattern;
                bool is_wild = pat->kind == AST_IDENT &&
                               strcmp(pat->as.ident.name, "_") == 0;
                if (is_wild) continue;
                if (!match_pattern_all_int_const(pat))
                {
                    use_int_switch = false;
                    break;
                }
            }
        }

        if (use_int_switch)
        {
            /* ---- (A) LLVM switch instruction for integer subjects ---- */
            LLVMTypeRef subj_llvm = type_to_llvm(ctx, subj_type);

            LLVMBasicBlockRef default_bb = LLVMAppendBasicBlockInContext(
                ctx->context, ctx->current_fn, "match.default");

            /* Count total switch cases across all OR-pattern leaves. */
            int total_cases = 0;
            for (int i = 0; i < node->as.match.arm_count; i++)
            {
                AstNode *pat = node->as.match.arms[i].pattern;
                bool is_wild = pat->kind == AST_IDENT &&
                               strcmp(pat->as.ident.name, "_") == 0;
                if (!is_wild)
                {
                    long long tmp[64];
                    total_cases += match_collect_int_vals(pat, tmp, 64);
                }
            }

            LLVMValueRef switch_inst = LLVMBuildSwitch(ctx->builder, subject,
                                                       default_bb, (unsigned)total_cases);
            bool default_used = false;

            for (int i = 0; i < node->as.match.arm_count; i++)
            {
                MatchArm *arm = &node->as.match.arms[i];
                AstNode  *pat = arm->pattern;
                bool is_wild  = pat->kind == AST_IDENT &&
                                strcmp(pat->as.ident.name, "_") == 0;

                if (is_wild)
                {
                    /* Wildcard → default block */
                    LLVMPositionBuilderAtEnd(ctx->builder, default_bb);
                    default_used = true;
                    int arm_str_mark = ctx->temp_string_count;   /* L-013 */
                    int arm_drop_floor = ctx->temp_drop_count;
                    LLVMValueRef body_val = codegen_expr(ctx, arm->body);
                    if (result_alloca && body_val)
                    {
                        body_val = cg_match_arm_own_tail(
                            ctx, cg_match_arm_tail(arm->body), body_val, res_llvm,
                            result_type, arm_str_mark, arm_drop_floor, false);
                        LLVMBuildStore(ctx->builder, body_val, result_alloca);
                    }
                    cg_match_arm_encapsulate(ctx, arm_str_mark, arm_drop_floor, result_type);
                    if (LLVMGetBasicBlockTerminator(LLVMGetInsertBlock(ctx->builder)) == NULL)
                        LLVMBuildBr(ctx->builder, merge_bb);
                }
                else
                {
                    /* Create one body block; add all OR-pattern constants as cases. */
                    LLVMBasicBlockRef body_bb = LLVMAppendBasicBlockInContext(
                        ctx->context, ctx->current_fn, "match.case");

                    long long vals[64];
                    int nvals = match_collect_int_vals(pat, vals, 64);
                    for (int j = 0; j < nvals; j++)
                    {
                        LLVMValueRef case_val = LLVMConstInt(subj_llvm,
                                                             (unsigned long long)vals[j],
                                                             /*sign_extend=*/1);
                        LLVMAddCase(switch_inst, case_val, body_bb);
                    }

                    LLVMPositionBuilderAtEnd(ctx->builder, body_bb);
                    int arm_str_mark = ctx->temp_string_count;   /* L-013 */
                    int arm_drop_floor = ctx->temp_drop_count;
                    LLVMValueRef body_val = codegen_expr(ctx, arm->body);
                    if (result_alloca && body_val)
                    {
                        body_val = cg_match_arm_own_tail(
                            ctx, cg_match_arm_tail(arm->body), body_val, res_llvm,
                            result_type, arm_str_mark, arm_drop_floor, false);
                        LLVMBuildStore(ctx->builder, body_val, result_alloca);
                    }
                    cg_match_arm_encapsulate(ctx, arm_str_mark, arm_drop_floor, result_type);
                    if (LLVMGetBasicBlockTerminator(LLVMGetInsertBlock(ctx->builder)) == NULL)
                        LLVMBuildBr(ctx->builder, merge_bb);
                }
            }

            if (!default_used)
            {
                /* No wildcard arm — default block falls through to merge. */
                LLVMPositionBuilderAtEnd(ctx->builder, default_bb);
                LLVMBuildBr(ctx->builder, merge_bb);
            }
        }
        else
        {
            /* ---- (B) CondBr chain (string / float / non-const patterns) ---- */
            for (int i = 0; i < node->as.match.arm_count; i++)
            {
                MatchArm *arm = &node->as.match.arms[i];
                bool is_wildcard = arm->pattern->kind == AST_IDENT &&
                                   strcmp(arm->pattern->as.ident.name, "_") == 0;

                if (is_wildcard)
                {
                    int arm_str_mark = ctx->temp_string_count;   /* L-013 */
                    int arm_drop_floor = ctx->temp_drop_count;
                    LLVMValueRef body_val = codegen_expr(ctx, arm->body);
                    if (result_alloca && body_val)
                    {
                        body_val = cg_match_arm_own_tail(
                            ctx, cg_match_arm_tail(arm->body), body_val, res_llvm,
                            result_type, arm_str_mark, arm_drop_floor, false);
                        LLVMBuildStore(ctx->builder, body_val, result_alloca);
                    }
                    cg_match_arm_encapsulate(ctx, arm_str_mark, arm_drop_floor, result_type);
                    if (LLVMGetBasicBlockTerminator(LLVMGetInsertBlock(ctx->builder)) == NULL)
                        LLVMBuildBr(ctx->builder, merge_bb);
                }
                else
                {
                    /* Flatten OR-pattern tree into leaf array. */
                    AstNode *leaves[64];
                    int nleaves = 0;
                    {
                        AstNode *stk[64]; int sp = 0;
                        stk[sp++] = arm->pattern;
                        while (sp > 0 && nleaves < 64)
                        {
                            AstNode *cur = stk[--sp];
                            if (cur->kind == AST_MATCH_OR_PATTERN)
                            {
                                if (sp + 2 <= 64) {
                                    stk[sp++] = cur->as.or_pattern.right;
                                    stk[sp++] = cur->as.or_pattern.left;
                                }
                            }
                            else
                                leaves[nleaves++] = cur;
                        }
                    }

                    LLVMBasicBlockRef then_bb = LLVMAppendBasicBlockInContext(
                        ctx->context, ctx->current_fn, "match.then");
                    LLVMBasicBlockRef next_bb = LLVMAppendBasicBlockInContext(
                        ctx->context, ctx->current_fn, "match.next");

                    /* Build comparison for each leaf, OR results together. */
                    LLVMValueRef combined_cmp = NULL;
                    for (int j = 0; j < nleaves; j++)
                    {
                        LLVMValueRef pattern = codegen_expr(ctx, leaves[j]);
                        if (pattern == NULL) continue;

                        LLVMValueRef cmp;
                        if (is_fp)
                            cmp = LLVMBuildFCmp(ctx->builder, LLVMRealOEQ, subject, pattern, "match.cmp");
                        else
                            cmp = LLVMBuildICmp(ctx->builder, LLVMIntEQ, subject, pattern, "match.cmp");

                        combined_cmp = (combined_cmp == NULL)
                            ? cmp
                            : LLVMBuildOr(ctx->builder, combined_cmp, cmp, "match.or");
                    }

                    if (combined_cmp == NULL)
                    {
                        /* Degenerate: no leaf patterns — skip arm. */
                        LLVMDeleteBasicBlock(then_bb);
                        LLVMDeleteBasicBlock(next_bb);
                        continue;
                    }

                    LLVMBuildCondBr(ctx->builder, combined_cmp, then_bb, next_bb);

                    LLVMPositionBuilderAtEnd(ctx->builder, then_bb);
                    int arm_str_mark = ctx->temp_string_count;   /* L-013 */
                    int arm_drop_floor = ctx->temp_drop_count;
                    LLVMValueRef body_val = codegen_expr(ctx, arm->body);
                    if (result_alloca && body_val)
                    {
                        body_val = cg_match_arm_own_tail(
                            ctx, cg_match_arm_tail(arm->body), body_val, res_llvm,
                            result_type, arm_str_mark, arm_drop_floor, false);
                        LLVMBuildStore(ctx->builder, body_val, result_alloca);
                    }
                    cg_match_arm_encapsulate(ctx, arm_str_mark, arm_drop_floor, result_type);
                    /* Guard: arm body may end with 'return'. */
                    if (LLVMGetBasicBlockTerminator(LLVMGetInsertBlock(ctx->builder)) == NULL)
                        LLVMBuildBr(ctx->builder, merge_bb);

                    LLVMPositionBuilderAtEnd(ctx->builder, next_bb);
                }
            }

            /* If last arm wasn't wildcard, fall through to merge. */
            if (node->as.match.arm_count > 0)
            {
                MatchArm *last = &node->as.match.arms[node->as.match.arm_count - 1];
                bool last_is_wildcard = last->pattern->kind == AST_IDENT &&
                                        strcmp(last->pattern->as.ident.name, "_") == 0;
                if (!last_is_wildcard)
                {
                    if (LLVMGetBasicBlockTerminator(LLVMGetInsertBlock(ctx->builder)) == NULL)
                        LLVMBuildBr(ctx->builder, merge_bb);
                }
            }
        }

        LLVMPositionBuilderAtEnd(ctx->builder, merge_bb);
        /* L-013: register the funneled owned result as the single result temp
           (mirrors the enum path); no-op for static/POD results. */
        if (result_alloca)
        if (result_alloca)
            return LLVMBuildLoad2(ctx->builder, res_llvm, result_alloca, "match.val");
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

    case AST_FORCE_UNWRAP:
    {
        /* expr! — force-unwrap Option/Result, abort on None/Err.
           Lowered to a match-like branch:
             - Ok/Some => extract payload, yield T
             - Err/None => call abort() (does not return) */
        AstNode *inner_expr = node->as.force_unwrap.expr;
        Type *inner_type = inner_expr->resolved_type;
        if (inner_type == NULL || inner_type->kind != TYPE_ENUM)
            return NULL;

        bool is_result = (strncmp(inner_type->as.enom.name, "Result(", 7) == 0);
        /* Discriminant order is fixed by the builtin Option/Result templates
           (None=0/Some=1, Ok=0/Err=1) — same convention the AST_TRY handler
           above relies on. Reordering those templates would break both sites
           and is caught immediately by the test suite. */
        int success_idx = is_result ? 0 : 1;   /* Ok=0 / Some=1 */

        LLVMValueRef inner_val = codegen_expr(ctx, inner_expr);
        if (inner_val == NULL) return NULL;

        LLVMTypeRef inner_llvm = type_to_llvm(ctx, inner_type);
        LLVMTypeRef i8 = LLVMInt8TypeInContext(ctx->context);

        /* Hoist alloca to entry block */
        LLVMBasicBlockRef entry = LLVMGetEntryBasicBlock(ctx->current_fn);
        LLVMBuilderRef tmp_b = LLVMCreateBuilderInContext(ctx->context);
        LLVMValueRef first_inst = LLVMGetFirstInstruction(entry);
        if (first_inst) LLVMPositionBuilderBefore(tmp_b, first_inst);
        else            LLVMPositionBuilderAtEnd(tmp_b, entry);
        LLVMValueRef inner_alloca = LLVMBuildAlloca(tmp_b, inner_llvm, "fuw.inner");
        Type *success_t = node->resolved_type;
        LLVMValueRef result_alloca = NULL;
        LLVMTypeRef success_llvm = NULL;
        if (success_t != NULL && success_t->kind != TYPE_VOID) {
            success_llvm = type_to_llvm(ctx, success_t);
            result_alloca = LLVMBuildAlloca(tmp_b, success_llvm, "fuw.val");
        }
        LLVMDisposeBuilder(tmp_b);

        LLVMBuildStore(ctx->builder, inner_val, inner_alloca);

        /* Load discriminant and branch */
        LLVMValueRef disc_ptr = LLVMBuildStructGEP2(ctx->builder, inner_llvm,
                                                    inner_alloca, 0, "fuw.disc.p");
        LLVMValueRef disc = LLVMBuildLoad2(ctx->builder, i8, disc_ptr, "fuw.disc");
        LLVMValueRef cmp = LLVMBuildICmp(ctx->builder, LLVMIntEQ, disc,
                                         LLVMConstInt(i8, (unsigned long long)success_idx, 0),
                                         "fuw.is_ok");

        LLVMBasicBlockRef ok_bb  = LLVMAppendBasicBlockInContext(
            ctx->context, ctx->current_fn, "fuw.ok");
        LLVMBasicBlockRef err_bb = LLVMAppendBasicBlockInContext(
            ctx->context, ctx->current_fn, "fuw.err");
        LLVMBasicBlockRef merge_bb = LLVMAppendBasicBlockInContext(
            ctx->context, ctx->current_fn, "fuw.merge");
        LLVMBuildCondBr(ctx->builder, cmp, ok_bb, err_bb);

        /* ---- Success path: extract payload ---- */
        LLVMPositionBuilderAtEnd(ctx->builder, ok_bb);
        if (result_alloca && success_llvm) {
            LLVMTypeRef variant_struct = build_variant_payload_struct(
                ctx, inner_type, success_idx);
            LLVMValueRef in_payload = LLVMBuildStructGEP2(
                ctx->builder, inner_llvm, inner_alloca, 1, "fuw.in.payload");
            LLVMValueRef field_ptr = LLVMBuildStructGEP2(
                ctx->builder, variant_struct, in_payload, 0, "fuw.ok.field");
            LLVMValueRef ok_val = LLVMBuildLoad2(
                ctx->builder, success_llvm, field_ptr, "fuw.ok.val");
            LLVMBuildStore(ctx->builder, ok_val, result_alloca);
            /* Move-elision (Q4): the success payload's ownership is transferred
               to `result_alloca`. When the operand is a named owned variable the
               checker tagged moved_out — invalidate the SOURCE enum so its scope
               cleanup skips dropping the now-moved payload (else double-free).
               Covers string / Vec / Map / struct / has_drop-enum uniformly via
               the source enum's moved_flag; POD payloads are no-ops. rvalue
               operands (calls) have no named source and are left untouched. */
            if (inner_expr->moved_out)
                cg_invalidate_moved_source(ctx, inner_expr, inner_type);
        }
        LLVMBuildBr(ctx->builder, merge_bb);

        /* ---- Failure path: print diagnostic + abort ---- */
        LLVMPositionBuilderAtEnd(ctx->builder, err_bb);
        {
            LLVMValueRef printf_fn = LLVMGetNamedFunction(ctx->module, "printf");
            /* C1: `.expect(msg)` lowers to a force-unwrap carrying a message expr.
               On the failure path print the user's message; bare `!` / `.unwrap()`
               (message == NULL) print the default diagnostic. The message string is
               evaluated only here (panic path) — any owned temp leaks are moot since
               the process is exiting. */
            AstNode *msg_node = node->as.force_unwrap.message;
            if (printf_fn && msg_node) {
                LLVMTypeRef printf_ty = LLVMGlobalGetValueType(printf_fn);
                LLVMValueRef msg_val = codegen_expr(ctx, msg_node);
                LLVMValueRef msg_data = msg_val
                    ? ls_string_data(ctx, msg_val) : NULL;
                LLVMValueRef fmt = LLVMBuildGlobalStringPtr(
                    ctx->builder, "[expect] %d:%d: %s\n", "fuw.efmt");
                LLVMValueRef line_val = LLVMConstInt(LLVMInt32TypeInContext(ctx->context),
                                                     (unsigned long long)node->line, 0);
                LLVMValueRef col_val  = LLVMConstInt(LLVMInt32TypeInContext(ctx->context),
                                                     (unsigned long long)node->column, 0);
                if (msg_data == NULL)
                    msg_data = LLVMBuildGlobalStringPtr(ctx->builder, "(expect)", "fuw.emsg0");
                LLVMValueRef pargs4[4] = { fmt, line_val, col_val, msg_data };
                LLVMBuildCall2(ctx->builder, printf_ty, printf_fn, pargs4, 4, "");
            }
            else if (printf_fn) {
                LLVMTypeRef printf_ty = LLVMGlobalGetValueType(printf_fn);
                const char *fail_variant = is_result ? "Err" : "None";
                const char *ok_variant   = is_result ? "Ok" : "Some";
                const char *type_str = inner_type->as.enom.name;
                /* Use a short fixed format to avoid dynamic string building */
                char fmt_buf[256];
                snprintf(fmt_buf, sizeof(fmt_buf),
                    "[unwrap] %%d:%%d: unwrap failed: expected %s, got %s (type: %s)\n",
                    ok_variant, fail_variant, type_str);
                LLVMValueRef fmt = LLVMBuildGlobalStringPtr(ctx->builder, fmt_buf, "fuw.fmt");
                LLVMValueRef line_val = LLVMConstInt(LLVMInt32TypeInContext(ctx->context),
                                                     (unsigned long long)node->line, 0);
                LLVMValueRef col_val  = LLVMConstInt(LLVMInt32TypeInContext(ctx->context),
                                                     (unsigned long long)node->column, 0);
                LLVMValueRef pargs[3] = { fmt, line_val, col_val };
                LLVMBuildCall2(ctx->builder, printf_ty, printf_fn, pargs, 3, "");
            }
            LLVMValueRef exit_fn = LLVMGetNamedFunction(ctx->module, "__ls_proc_exit");
            LLVMTypeRef exit_ty = LLVMFunctionType(
                LLVMVoidTypeInContext(ctx->context),
                (LLVMTypeRef[]){ LLVMInt32TypeInContext(ctx->context) }, 1, 0);
            if (exit_fn == NULL)
                exit_fn = LLVMAddFunction(ctx->module, "__ls_proc_exit", exit_ty);
            LLVMValueRef code = LLVMConstInt(LLVMInt32TypeInContext(ctx->context), 1, 0);
            LLVMBuildCall2(ctx->builder, exit_ty, exit_fn, &code, 1, "");
            LLVMBuildUnreachable(ctx->builder);
        }

        /* ---- Merge: yield unwrapped value ---- */
        LLVMPositionBuilderAtEnd(ctx->builder, merge_bb);
        if (result_alloca && success_llvm)
            return LLVMBuildLoad2(ctx->builder, success_llvm, result_alloca, "fuw.val");
        return NULL;
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
            int field_temp_mark = ctx->temp_string_count;
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
                           field_temp_mark, CG_XFER_INTO_CONTAINER);
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

            int field_temp_mark = ctx->temp_string_count;
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
                           field_temp_mark, CG_XFER_INTO_CONTAINER);
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
    /* BF-044: temps produced while evaluating the RHS (e.g. a spilled has_drop
       struct clone from `vec[i].field`) live in the conditionally-executed RHS
       block. They MUST be flushed here, inside the RHS block, before branching to
       merge — otherwise their drop is emitted at the enclosing statement boundary
       (a block not dominated by rhs_bb), giving "Instruction does not dominate all
       uses". The RHS result is a bool (i1), so it owns nothing; flushing all RHS
       temps is safe. LHS temps were created in the entry block (which dominates
       everything) and correctly flush at the outer statement boundary. */
    int rhs_temp_mark = ctx->temp_string_count;
    LLVMValueRef right = codegen_expr(ctx, node->as.binary.right);
    cg_flush_temps(ctx, rhs_temp_mark, false);
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

        /* Allocate moved_flag for struct-with-drop and has_drop enum types.
           F.5: enum with heap payload also needs move tracking so closure
           by-move capture can prevent double-free via scope cleanup. */
        LLVMValueRef moved_flag = NULL;
        if ((var_type->kind == TYPE_STRUCT && var_type->as.strukt.has_drop) ||
            (var_type->kind == TYPE_ENUM   && var_type->as.enom.has_drop))
        {
            LLVMTypeRef i1_type = LLVMInt1TypeInContext(ctx->context);
            moved_flag = cg_entry_alloca(ctx, i1_type, "var.moved");
            LLVMBuildStore(ctx->builder, LLVMConstInt(i1_type, 0, 0), moved_flag);
        }

        /* Zero-initialize arrays that contain strings or droppable structs.
           Without this, unassigned array elements have garbage cap/data values,
           causing emit_cleanup_to to call free() on invalid pointers at scope exit. */
        if (var_type->kind == TYPE_ARRAY && var_type->as.array.elem)
        {
            Type *elem = var_type->as.array.elem;
            bool needs_zero_init =
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
        /* Zero-initialize has-drop enum variables (disc=0, payload=zeroed).
           Without this, an uninitialized enum has garbage discriminant + payload,
           causing emit_auto_enum_drop_fn to access a wild pointer on scope exit. */
        if (var_type->kind == TYPE_ENUM && var_type->as.enom.has_drop)
        {
            LLVMBuildStore(ctx->builder, LLVMConstNull(llvm_type), alloca);
        }
        if (node->as.var_decl.init)
        {
            /* Track temp slots created during init expression evaluation */
            int temp_mark = ctx->temp_string_count;

            /* Collection-literal init for a user container (the `__from_list`
               protocol, checked above): zero-init the struct, then call its
               reserved `__from_list(&!self, E)` method for each element. Matches
               the builtin `vec(T) v = [..]`. */
            if (var_type->kind == TYPE_STRUCT &&
                     node->as.var_decl.init->kind == AST_ARRAY_LIT)
            {
                AstNode *lit = node->as.var_decl.init;
                LLVMBuildStore(ctx->builder, LLVMConstNull(llvm_type), alloca);
                char fl_name[256];
                snprintf(fl_name, sizeof(fl_name), "%s.__from_list",
                         struct_llvm_name(var_type));
                LLVMValueRef fl_fn = LLVMGetNamedFunction(ctx->module, fl_name);
                if (fl_fn == NULL)
                {
                    /* F6 (sibling of VR-LIM-016/F1): a local `Vec(T) v = [..]`
                       inside an IMPORTED-module function is emitted before the
                       G1.5 pending-generic pass, so Vec(T).__from_list may not
                       exist yet. Silently skipping the loop left the Vec
                       zero-initialized (len=0, data=null → reads/crashes in the
                       consumer). Forward-declare from the pending queue; the body
                       lands in G1.5. Mirrors emit_user_from_list_value (F1). */
                    fl_fn = cg_declare_pending_generic_method(ctx, fl_name);
                }
                if (fl_fn)
                {
                    LLVMTypeRef fl_ft = LLVMGlobalGetValueType(fl_fn);
                    for (int i = 0; i < lit->as.array_lit.count; i++)
                    {
                        LLVMValueRef ev = codegen_expr(ctx, lit->as.array_lit.elements[i]);
                        if (ev == NULL) continue;
                        LLVMValueRef fl_args[2] = { alloca, ev };
                        LLVMBuildCall2(ctx->builder, fl_ft, fl_fn, fl_args, 2, "");
                    }
                }
                ctx->temp_string_count = temp_mark;
            }
            /* M-LIT: key-value literal init for a user container (the
               `__from_pairs` protocol, checked above): zero-init the struct, then
               call `__from_pairs(&!self, K, V)` per pair, moving each key/value in
               (owned-param ABI, mirror __from_list). e.g. `Map(K,V) m = {k: v}`. */
            else if (var_type->kind == TYPE_STRUCT &&
                     node->as.var_decl.init->kind == AST_MAP_LIT &&
                     node->as.var_decl.init->as.map_lit.pair_count > 0)
            {
                AstNode *ml = node->as.var_decl.init;
                LLVMBuildStore(ctx->builder, LLVMConstNull(llvm_type), alloca);
                char fp_name[256];
                snprintf(fp_name, sizeof(fp_name), "%s.__from_pairs",
                         struct_llvm_name(var_type));
                LLVMValueRef fp_fn = LLVMGetNamedFunction(ctx->module, fp_name);
                if (fp_fn == NULL)
                    fp_fn = cg_declare_pending_generic_method(ctx, fp_name);
                if (fp_fn)
                {
                    LLVMTypeRef fp_ft = LLVMGlobalGetValueType(fp_fn);
                    for (int i = 0; i < ml->as.map_lit.pair_count; i++)
                    {
                        LLVMValueRef kv = codegen_expr(ctx, ml->as.map_lit.keys[i]);
                        if (kv == NULL) continue;
                        LLVMValueRef vv = codegen_expr(ctx, ml->as.map_lit.vals[i]);
                        if (vv == NULL) continue;
                        LLVMValueRef fp_args[3] = { alloca, kv, vv };
                        LLVMBuildCall2(ctx->builder, fp_ft, fp_fn, fp_args, 3, "");
                    }
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
                    if (var_type->kind == TYPE_STRUCT &&
                             var_type->as.strukt.has_drop &&
                             ast_unwrap_move(node->as.var_decl.init)->kind == AST_IDENT)
                    {
                        if (ast_unwrap_move(node->as.var_decl.init)->moved_out &&
                            cg_invalidate_moved_source(ctx, node->as.var_decl.init, var_type))
                        {
                            /* Move-elision (Q4): ownership transferred and source
                               was a real owned variable — moved + invalidated, no
                               clone. Borrow sources return false → fall to clone. */
                        }
                        else
                        {
                            /* Source is another named struct variable — deep-clone so both
                               this variable and the source have independently owned string
                               fields and can be freed without double-free. */
                            LLVMTypeRef llvm_st = type_to_llvm(ctx, var_type);
                            init = emit_struct_clone_val(ctx, init, llvm_st, var_type);
                        }
                    }
                    else if (var_type->kind == TYPE_ENUM &&
                             var_type->as.enom.has_drop &&
                             ast_unwrap_move(node->as.var_decl.init)->kind == AST_IDENT)
                    {
                        if (ast_unwrap_move(node->as.var_decl.init)->moved_out &&
                            cg_invalidate_moved_source(ctx, node->as.var_decl.init, var_type))
                        {
                            /* Move-elision (Q4): ownership transferred and source
                               was a real owned variable — moved + invalidated, no
                               clone. Borrow sources return false → fall to clone. */
                        }
                        else
                        {
                            /* Source is another named has-drop enum variable — deep-clone so
                               both this variable and the source have independently owned heap
                               payloads and can be freed without double-free.
                               e.g. JsonValue b = a  →  b gets its own deep copy of a. */
                            init = emit_enum_clone_val(ctx, init, var_type);
                        }
                    }
                    else if (var_type->kind == TYPE_BLOCK)
                    {
                        AstNode *blk_init = ast_unwrap_move(node->as.var_decl.init);
                        if (blk_init && blk_init->kind == AST_IDENT)
                        {
                            /* F.2: Block variable initialized from another Block variable.
                               Move semantics: zero source's env_ptr so its scope cleanup
                               skips (this new variable now owns the env). */
                            CgSymbol *src = cg_scope_resolve(ctx->current_scope,
                                                              blk_init->as.ident.name);
                            if (src && !src->is_borrowed)
                                cg_null_block_env(ctx, src->value); /* logs block.move internally */
                        }
                        else if (cg_block_source_is_aliased(blk_init))
                        {
                            /* Phase G: Block copied out of a vec/struct/map — the
                               source LsBlock aliases an env owned by the container.
                               Deep-clone the env so this variable owns an
                               independent one (no shared-env double-free). */
                            init = cg_emit_block_env_clone(ctx, init);
                        }
                        else if (ctx->temp_block_env_count > 0)
                        {
                            /* Phase C.5: closure literal → pop trailing temp env;
                               the var now owns it and scope cleanup is the sole releaser. */
                            ctx->temp_block_env_count--;
                        }
                    }

                    LLVMBuildStore(ctx->builder, init, alloca);
                }

                cg_flush_temps(ctx, temp_mark, false);
            }
        }

        cg_scope_define(ctx->current_scope, node->as.var_decl.name, alloca, var_type, moved_flag);
        break;
    }

    case AST_ASSIGN:
    {

        /* Track temp string slots created during value evaluation. Also snapshot
           the has_drop temp COUNT: the string-count-based mark cannot tell a
           pre-statement registration (e.g. an enclosing match's owned-rvalue
           subject) from an RHS-created spill when no string temps intervene —
           flushing by mark would double-drop the subject. */
        int temp_mark = ctx->temp_string_count;
        int assign_drop_floor = ctx->temp_drop_count;
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
                if (sym->type && sym->type->kind == TYPE_STRUCT &&
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

                    /* Step 2: clone if source is a shared IDENT. Move-elision (Q4):
                       if the checker tagged the source moved_out (ownership truly
                       transferred, later use rejected) we skip the clone and
                       invalidate the source instead. */
                    if (ast_unwrap_move(node->as.assign.value)->kind == AST_IDENT)
                    {
                        if (ast_unwrap_move(node->as.assign.value)->moved_out &&
                            cg_invalidate_moved_source(ctx, node->as.assign.value, sym->type))
                        {
                            /* moved + invalidated (real owned source) — no clone.
                               Borrow source → false → clone below. */
                        }
                        else
                        {
                            LLVMTypeRef llvm_st = type_to_llvm(ctx, sym->type);
                            val = emit_struct_clone_val(ctx, val, llvm_st, sym->type);
                        }
                    }

                    /* Step 3: store */
                    LLVMBuildStore(ctx->builder, val, sym->value);

                    /* After assignment the variable is alive again — clear moved_flag */
                    if (sym->moved_flag)
                    {
                        LLVMTypeRef i1 = LLVMInt1TypeInContext(ctx->context);
                        LLVMBuildStore(ctx->builder, LLVMConstInt(i1, 0, 0), sym->moved_flag);
                    }
                    /* Drop the has_drop temps the RHS registered (e.g. the f-string
                       Str spilled for `s = s + f"..."`): deferring to scope end
                       leaks all but the last loop iteration (the entry alloca is
                       reused). Use the temp_drop COUNT floor, NOT cg_flush_temps's
                       string-count mark — a mark-based flush also catches an
                       enclosing match's owned-rvalue subject registered before this
                       statement (same mark when no string temps intervene) and
                       double-drops it. The stored result value itself is never
                       temp_drop-registered (var_decl invariant). */
                    for (int ti = assign_drop_floor; ti < ctx->temp_drop_count; ti++)
                    {
                        Type *tt = ctx->temp_drop_types[ti];
                        if (tt->kind == TYPE_STRUCT)
                            emit_struct_drop(ctx, ctx->temp_drop_slots[ti], tt);
                        else if (tt->kind == TYPE_ENUM)
                            emit_enum_drop(ctx, ctx->temp_drop_slots[ti], tt);
                    }
                    ctx->temp_drop_count = assign_drop_floor;
                    /* String temps from the RHS are garbage here (result is a
                       struct): free them as before. */
                    for (int ti = temp_mark; ti < ctx->temp_string_count; ti++)
                        emit_string_free(ctx, ctx->temp_string_slots[ti]);
                    ctx->temp_string_count = temp_mark;
                }
                else if (sym->type && sym->type->kind == TYPE_ENUM &&
                         sym->type->as.enom.has_drop)
                {
                    /* has_drop enum assignment:
                       1. Drop old value (runtime-conditional via moved_flag)
                       2. Clone if source is an IDENT (shared variable/borrow)
                       3. Store (cloned or fresh) value
                       4. Clear moved_flag so scope cleanup works */
                    if (sym->moved_flag)
                    {
                        LLVMTypeRef i1_t = LLVMInt1TypeInContext(ctx->context);
                        LLVMValueRef cur_fn = LLVMGetBasicBlockParent(
                            LLVMGetInsertBlock(ctx->builder));
                        LLVMBasicBlockRef do_bb = LLVMAppendBasicBlockInContext(
                            ctx->context, cur_fn, "assign.edrop");
                        LLVMBasicBlockRef cont_bb = LLVMAppendBasicBlockInContext(
                            ctx->context, cur_fn, "assign.econt");
                        LLVMValueRef is_moved = LLVMBuildLoad2(ctx->builder,
                            i1_t, sym->moved_flag, "e.moved");
                        LLVMBuildCondBr(ctx->builder, is_moved, cont_bb, do_bb);
                        LLVMPositionBuilderAtEnd(ctx->builder, do_bb);
                        emit_enum_drop(ctx, sym->value, sym->type);
                        LLVMBuildBr(ctx->builder, cont_bb);
                        LLVMPositionBuilderAtEnd(ctx->builder, cont_bb);
                    }
                    else
                    {
                        emit_enum_drop(ctx, sym->value, sym->type);
                    }
                    if (ast_unwrap_move(node->as.assign.value)->kind == AST_IDENT)
                    {
                        /* Move-elision (Q4): skip the clone when the checker
                           confirmed ownership transfer AND the source is a real
                           owned variable (helper returns true). Borrow sources
                           return false → clone. */
                        if (ast_unwrap_move(node->as.assign.value)->moved_out &&
                            cg_invalidate_moved_source(ctx, node->as.assign.value, sym->type))
                        {
                            /* moved + invalidated — no clone */
                        }
                        else
                            val = emit_enum_clone_val(ctx, val, sym->type);
                    }
                    LLVMBuildStore(ctx->builder, val, sym->value);
                    if (sym->moved_flag)
                    {
                        LLVMBuildStore(ctx->builder,
                            LLVMConstInt(LLVMInt1TypeInContext(ctx->context), 0, 0),
                            sym->moved_flag);
                    }
                    /* Same as the has_drop struct branch above: drop RHS-registered
                       has_drop temps by COUNT floor (not mark — see that branch),
                       then free RHS string temps. */
                    for (int ti = assign_drop_floor; ti < ctx->temp_drop_count; ti++)
                    {
                        Type *tt = ctx->temp_drop_types[ti];
                        if (tt->kind == TYPE_STRUCT)
                            emit_struct_drop(ctx, ctx->temp_drop_slots[ti], tt);
                        else if (tt->kind == TYPE_ENUM)
                            emit_enum_drop(ctx, ctx->temp_drop_slots[ti], tt);
                    }
                    ctx->temp_drop_count = assign_drop_floor;
                    for (int ti = temp_mark; ti < ctx->temp_string_count; ti++)
                        emit_string_free(ctx, ctx->temp_string_slots[ti]);
                    ctx->temp_string_count = temp_mark;
                }
                else if (sym->type && sym->type->kind == TYPE_BLOCK)
                {
                    /* F.2: Block assignment — move semantics.
                       1. Drop old env in destination (if any).
                       2. Store new Block value.
                       3. If source is an owned IDENT, zero its env_ptr. */
                    cg_emit_block_drop_at(ctx, sym->value);
                    AstNode *rhs_node = ast_unwrap_move(node->as.assign.value);
                    if (cg_block_source_is_aliased(rhs_node))
                    {
                        /* Phase G: Block copied out of a vec/struct/map — deep-clone
                           the aliased env before storing so dst owns an independent
                           one (no shared-env double-free). */
                        val = cg_emit_block_env_clone(ctx, val);
                    }
                    LLVMBuildStore(ctx->builder, val, sym->value);
                    if (rhs_node->kind == AST_IDENT)
                    {
                        CgSymbol *src = cg_scope_resolve(ctx->current_scope,
                                                          rhs_node->as.ident.name);
                        if (src && !src->is_borrowed)
                            cg_null_block_env(ctx, src->value);
                    }
                    else if (!cg_block_source_is_aliased(rhs_node) &&
                             ctx->temp_block_env_count > 0)
                    {
                        /* Closure literal on RHS — pop temp env; dst now owns it */
                        ctx->temp_block_env_count--;
                    }
                    ctx->temp_string_count = temp_mark;
                }
                else
                {
                    /* POD / bool / int / float / char / ptr / object target.
                       The store never takes ownership of a heap string, so any
                       string temps created while evaluating the RHS (e.g. a
                       render() call buried in `ok = check(render(x)==..., n)`)
                       are pure garbage and MUST be freed here — not merely
                       discarded by resetting the count, which leaked them.
                       Free ONLY the string temps (not temp-drops / block-envs:
                       those has_drop temporaries are released by the existing
                       statement-boundary machinery, and double-flushing them
                       here would double-free, e.g. by-value struct args in
                       `acc = f(x) && f(x) && acc`). */
                    LLVMBuildStore(ctx->builder, val, sym->value);
                    for (int ti = temp_mark; ti < ctx->temp_string_count; ti++)
                        emit_string_free(ctx, ctx->temp_string_slots[ti]);
                    ctx->temp_string_count = temp_mark;
                }
            }
            else
            {

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
                if (cg_type_owns_heap_for_enum(field_type))
                {
                    /* has_drop field (vec/map/has_drop struct|enum): drop the old
                       value, then store an independently-owned value. Clone if the
                       source is a shared IDENT so field and source stay valid;
                       move-elision (Q4) skips the clone when the checker confirmed
                       ownership transfer (moved_out) and invalidates the source. */
                    emit_drop_value(ctx, field_ptr, field_type);
                    if (ast_unwrap_move(node->as.assign.value)->kind == AST_IDENT)
                    {
                        if (ast_unwrap_move(node->as.assign.value)->moved_out &&
                            cg_invalidate_moved_source(ctx, node->as.assign.value, field_type))
                        {
                            /* moved + invalidated (real owned source) — no clone.
                               Borrow source → false → clone below. */
                        }
                        else
                        {
                            LLVMTypeRef flt = type_to_llvm(ctx, field_type);
                            val = emit_clone_value(ctx, val, flt, field_type);
                        }
                    }
                    else
                    {
                        cg_mark_last_temp_moved(ctx, temp_mark,
                                                "assign: temp owned by struct field");
                    }
                    LLVMBuildStore(ctx->builder, val, field_ptr);
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

            if (obj_type && obj_type->kind == TYPE_POINTER && obj_type->as.pointer_to)
            {
                /* p[i] = val on a raw *T pointer - RAW store: typed-GEP the
                   element address and store. Does NOT drop the old slot (it may
                   be uninitialized memory - the unsafe-primitive contract; cf.
                   vec/array which drop the old element). Ownership of `val` is
                   still transferred via cg_store_owned (owned temp marked moved /
                   borrowed cloned) so a has_drop value isn't double-freed. */
                LLVMValueRef ptr_val = codegen_expr(ctx, obj);
                if (ptr_val == NULL)
                    return;
                LLVMTypeRef elem_llvm = type_to_llvm(ctx, obj_type->as.pointer_to);
                LLVMTypeRef i64_type = LLVMInt64TypeInContext(ctx->context);
                LLVMValueRef index = codegen_expr(ctx, target->as.index_expr.index);
                if (index == NULL)
                    return;
                if (LLVMTypeOf(index) != i64_type)
                    index = LLVMBuildSExtOrBitCast(ctx->builder, index, i64_type, "pis.idx");
                LLVMValueRef gep = LLVMBuildGEP2(ctx->builder, elem_llvm, ptr_val,
                                                 &index, 1, "pis.ep");
                cg_store_owned(ctx, gep, val, obj_type->as.pointer_to,
                               node->as.assign.value, temp_mark,
                               CG_XFER_INTO_CONTAINER);
                cg_flush_temps(ctx, temp_mark, true);
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

                LLVMBuildStore(ctx->builder, val, gep);
                ctx->temp_string_count = temp_mark;
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

            if (target_type && target_type->kind == TYPE_STRUCT &&
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
        /* Clone-instead-of-transfer flag for an IDENT string return:
           - P1-3/BF-041: a GLOBAL move-type var (global retains ownership, freed
             at exit by __ls_global_cleanup → transferring would double-free).
           - BF-045: a BORROWED string param (cap=-2 alias of the caller's buffer;
             transferring would dangle once the caller frees its temp after the call).
           Both cases must deep-copy so the caller receives an independent owned string. */
        bool ret_global_movetype = false;

        if (node->as.return_stmt.value &&
            node->as.return_stmt.value->kind == AST_IDENT &&
            node->as.return_stmt.value->resolved_type)
        {
            Type *ret_type = node->as.return_stmt.value->resolved_type;
            /* For string/struct/vec/Block/has_drop-enum IDENT returns:
               ownership transfers to caller — skip scope cleanup for this
               variable so we don't free its data/env. */
            if (ret_type->kind == TYPE_STRUCT ||
                ret_type->kind == TYPE_BLOCK  ||
                (ret_type->kind == TYPE_ENUM && ret_type->as.enom.has_drop))
            {
                const char *name = node->as.return_stmt.value->as.ident.name;
                CgSymbol *sym = cg_scope_resolve(ctx->current_scope, name);
                if (sym)
                {
                    bool is_global = LLVMIsAGlobalVariable(sym->value);
                    /* BF-045: a borrowed string param must clone on return.
                       Generalized: a borrowed has_drop struct/enum binder must
                       clone too. A match payload binder of a *borrow* subject
                       (&Enum param OR a closure's by-move capture, which is
                       marked is_borrowed so the body's scope cleanup leaves the
                       drop to the env) aliases the subject's payload zero-copy.
                       Returning it by transfer hands the caller an alias the
                       subject's owner (caller / env_drop) will also free →
                       double-free (string survived only because its borrow ABI
                       marks cap=BORROWED so the caller skips the free; Str/Vec/Map
                       carry no such marker). Clone so the caller owns an
                       independent copy. */
                    bool borrowed_heap =
                        sym->is_borrowed &&
                        ((ret_type->kind == TYPE_STRUCT && ret_type->as.strukt.has_drop) ||
                         (ret_type->kind == TYPE_ENUM && ret_type->as.enom.has_drop));
                    if (is_global || borrowed_heap)
                        ret_global_movetype = true; /* clone, don't transfer */
                    else
                        return_alloca = sym->value; /* local: transfer + skip */
                }
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
                /* Phase C.5: Block return transfers env ownership to the
                   caller — pop the trailing temp env so flush doesn't free
                   it. The caller will store into a Block local (or pass it
                   onward) and own the released env. */
                if (ret_type && ret_type->kind == TYPE_BLOCK &&
                    ctx->temp_block_env_count > 0) {
                    ctx->temp_block_env_count--;
                }
                cg_flush_temps(ctx, ret_temp_mark, false);
                /* P1-3 fix: returning a GLOBAL string by name shares the global's
                   data pointer. The global is freed at exit by __ls_global_cleanup,
                   so transferring it to the caller (who also frees) double-frees.
                   Clone here so caller owns an independent copy. (Local strings
                   take the move-transfer path above and must NOT clone.) */
                if (ret_global_movetype && ret_type && val)
                {
                    if ((ret_type->kind == TYPE_STRUCT && ret_type->as.strukt.has_drop) ||
                             (ret_type->kind == TYPE_ENUM && ret_type->as.enom.has_drop))
                        /* borrowed has_drop binder (see borrowed_heap above):
                           deep-copy the loaded value so the caller owns it. */
                        val = emit_clone_value(ctx, val, type_to_llvm(ctx, ret_type), ret_type);
                }

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
                                       (elem->kind == TYPE_STRUCT && elem->as.strukt.has_drop);
                    if (needs_clone)
                    {
                        LLVMTypeRef arr_llvm = type_to_llvm(ctx, ret_type);
                        val = emit_array_clone_val(ctx, val, arr_llvm, ret_type);
                    }
                }
                emit_cleanup_to(ctx, NULL, return_alloca);
                cg_emit_mc_leave(ctx);   /* D.1: pop frame */
                cg_emit_prof_leave(ctx);
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
                cg_emit_prof_leave(ctx);
                if (ctx->is_main_void)
                    LLVMBuildRet(ctx->builder,
                        LLVMConstInt(LLVMInt32TypeInContext(ctx->context), 0, 0));
                else
                    LLVMBuildRetVoid(ctx->builder);
            }
        }
        break;
    }

    case AST_IF:
    {
        int cond_temp_mark = ctx->temp_string_count;
        LLVMValueRef cond = codegen_expr(ctx, node->as.if_stmt.cond);
        if (cond == NULL)
            return;
        /* Free temporary strings produced during condition evaluation
           (e.g. f"..." interpolations, string concatenations used in
           comparisons).  The condition result is an i1 bool already
           materialised, so the strings are no longer needed. */
        cg_flush_temps(ctx, cond_temp_mark, false);

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
        {
            int cond_temp_mark = ctx->temp_string_count;
            LLVMValueRef cond = codegen_expr(ctx, node->as.while_stmt.cond);
            /* Free temporary strings from the condition before branching.
               They are re-created (and re-freed) on every loop iteration. */
            cg_flush_temps(ctx, cond_temp_mark, false);
            if (cond)
                LLVMBuildCondBr(ctx->builder, cond, body_bb, end_bb);
        }

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
        /* Iterator-protocol desugaring (for x in <user iterable>): the checker
           rewrote this into an equivalent while/match block. Emit that and stop;
           the builtin range/array/vec paths below never run for it. */
        if (node->as.for_stmt.desugared != NULL)
        {
            codegen_stmt(ctx, node->as.for_stmt.desugared);
            break;
        }

        /* foreach loop: for var in iter { body }
           Supported iterators:
             - Range expression (a..b): iterate var from a to b-1
             - Integer expression (n): iterate var from 0 to n-1
             - Array: iterate over elements
              - User-defined iterables are lowered by the checker before codegen. */
        push_scope(ctx);

        AstNode *iter_node = node->as.for_stmt.iter;
        Type *iter_type = iter_node->resolved_type;
        bool is_array_iter = (iter_type && iter_type->kind == TYPE_ARRAY);
        LLVMValueRef start_val = NULL, end_val = NULL;
        LLVMValueRef arr_ptr = NULL;    /* for fixed-array iter: alloca of array */

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
        LLVMValueRef idx_var = NULL;  /* internal index counter for array iter */
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
                /* Array loop variable is a copy of the element; mark borrowed so scope
                   cleanup doesn't drop it (the container still owns the data). */
                if (lvsym)
                    lvsym->is_borrowed = true;
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
        LLVMValueRef cmp_var = is_array_iter ? idx_var : loop_var;
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
        codegen_stmt(ctx, node->as.for_stmt.body);
        LLVMBasicBlockRef body_end = LLVMGetInsertBlock(ctx->builder);
        if (body_end && LLVMGetBasicBlockTerminator(body_end) == NULL)
            LLVMBuildBr(ctx->builder, update_bb);

        LLVMPositionBuilderAtEnd(ctx->builder, update_bb);
        LLVMValueRef inc_var = is_array_iter ? idx_var : loop_var;
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
            int cond_temp_mark = ctx->temp_string_count;
            LLVMValueRef cond = codegen_expr(ctx, node->as.for_c_stmt.cond);
            /* Free temporary strings produced by the condition expression. */
            cg_flush_temps(ctx, cond_temp_mark, false);
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
        AstNode *estmt = node->as.expr_stmt.expr;
        LLVMValueRef ev = codegen_expr(ctx, estmt);
        /* F2 (VR-LIM-014): a discarded CALL returning an owned has_drop value by
           value (e.g. `v.pop()` -> Option(string)) leaks its inner buffers —
           nothing binds or drops the rvalue. Spill to a temp and register it for
           drop so the flush below releases it. Restricted to AST_CALL on purpose:
           only a call yields a fresh owned rvalue; a bare ident/field read is a
           borrow of a live binding and must NOT be dropped here. TYPE_STRING is
           excluded — discarded string-returning calls are already freed via the
           temp-string mechanism (cg_push_temp_string). */
        if (ev != NULL && estmt->kind == AST_CALL && estmt->resolved_type)
        {
            Type *rt = estmt->resolved_type;
            bool needs_drop =
                (rt->kind == TYPE_STRUCT && rt->as.strukt.has_drop) ||
                (rt->kind == TYPE_ENUM   && rt->as.enom.has_drop);
            if (needs_drop)
            {
                LLVMTypeRef rllvm = type_to_llvm(ctx, rt);
                LLVMValueRef tmp = cg_entry_alloca(ctx, rllvm, "discard.drop");
                LLVMBuildStore(ctx->builder, ev, tmp);
                cg_push_temp_drop(ctx, tmp, rt);
            }
        }
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

/* ---- Phase B/C closure codegen ----
   Lifts a `|x| body` literal into a synthesised top-level LLVM function
   `__closure_<N>(env_ptr, params...)` and returns an LsBlock fat-pointer
   value `{ fn_ptr, env_ptr }`.

   Phase B: closures with no captures used env=NULL.
   Phase C: when the checker recorded captures on the AST node, this routine:
     1) builds an LLVM struct type matching the capture list,
     2) heap-allocates one (`cg_emit_alloc(... "closure.env" ...)`),
     3) stores the live outer values into the env at construction time,
     4) inside the synthesised body, copies each capture out of env_ptr into
        a fresh local alloca that the body sees by name (CgSymbol). POD-only
        in v1 — string/vec/map captures are rejected at the checker. */
static LLVMValueRef codegen_closure_literal(CodegenContext *ctx, AstNode *node)
{
    Type *block_t = node->resolved_type;
    if (block_t == NULL ||
        (block_t->kind != TYPE_BLOCK && block_t->kind != TYPE_FUNCTION))
    {
        cg_error(ctx, node->line, node->column,
                 "internal: closure literal has no resolved Block type");
        return NULL;
    }

    int n = block_t->as.function.param_count;
    LLVMTypeRef ptr_t = LLVMPointerTypeInContext(ctx->context, 0);

    int cap_n = node->as.closure.capture_count;

    /* Phase C.5 env layout:
         field 0: ptr drop_fn   (NULL when no has_drop captures)
         field 1..N: captures in declaration order
       The drop_fn slot lets RAII free heap captures (string/.. in v1) by
       calling a per-closure synthesised __env_drop_<id> before freeing the
       env block itself. POD-only envs store NULL there and skip the call.

       has_drop_n counts captures whose env-side ownership requires a drop:
       string + struct(has_drop): always by-move */
    int has_drop_n = 0;
    for (int i = 0; i < cap_n; i++) {
        Type *ct_i = node->as.closure.captures[i].type;
        if (capture_type_is_by_move_cg(ct_i))
            has_drop_n++;
    }

	/* 0) Snapshot outer alloca pointers (for the post-capture cap=-1 mark
	   on by-move strings) AND load each current value into a register. We
	   have to do this BEFORE detaching the scope chain, since the closure
	   body runs in a fresh isolated scope. */
    LLVMValueRef *cap_outer_vals    = NULL;
    LLVMValueRef *cap_outer_allocas = NULL;
    if (cap_n > 0) {
        cap_outer_vals    = (LLVMValueRef*)malloc_safe(
            (size_t)cap_n * sizeof(LLVMValueRef));
        cap_outer_allocas = (LLVMValueRef*)malloc_safe(
            (size_t)cap_n * sizeof(LLVMValueRef));
        for (int i = 0; i < cap_n; i++) {
            const char *name = node->as.closure.captures[i].name;
            bool explicit_move = node->as.closure.captures[i].is_explicit_move;
            CgSymbol *sym = cg_scope_resolve(ctx->current_scope, name);
            if (sym == NULL) {
                cg_error(ctx, node->line, node->column,
                         "internal: capture '%s' not in scope at codegen",
                         name);
                free(cap_outer_vals);
                free(cap_outer_allocas);
                return NULL;
            }
            cap_outer_allocas[i] = sym->value;
            Type *ct = node->as.closure.captures[i].type;
            bool is_default_by_ref = capture_type_is_by_ref_cg(ct) && !explicit_move;
            if (is_default_by_ref) {
                /* By-ref (default map): store outer alloca pointer.
                   Mutations to the outer variable are visible inside. */
                cap_outer_vals[i] = sym->value;
                cg_dbg_capture(ctx, name, ct, "borrow");
            } else if (capture_type_is_by_move_cg(ct)) {
                LLVMTypeRef ct_llvm = type_to_llvm(ctx, ct);
                cap_outer_vals[i] = LLVMBuildLoad2(ctx->builder, ct_llvm,
                                                   sym->value, "cap.load");
                /* Distinguish auto by-move vs. explicit [move] enum/string */
                cg_dbg_capture(ctx, name, ct, explicit_move ? "move-expl" : "move");
            } else {
                /* by-copy (POD / array / non-has_drop enum) */
                LLVMTypeRef ct_llvm = type_to_llvm(ctx, ct);
                cap_outer_vals[i] = LLVMBuildLoad2(ctx->builder, ct_llvm,
                                                   sym->value, "cap.load");
                cg_dbg_capture(ctx, name, ct, "copy");
            }
        }
    }

    /* 1) Build env struct LLVM type. Field 0 is the drop_fn pointer slot
       (always present so the cleanup logic stays uniform); user captures
       follow at fields 1..N.
       - by-move captures (string/struct/[move map]): value type
       - by-ref captures (default map without [move]): ptr to outer alloca
       When cap_n == 0 we still skip env entirely and pass NULL. */
    /* Phase G env layout:
         field 0: ptr drop_fn   (NULL when no has_drop captures)
         field 1: ptr clone_fn  (Phase G: per-closure __env_clone_<id>, lets a
                  `Block g = vec[i]` copy-out site deep-clone the env without
                  statically knowing its struct type; NULL only when cap_n==0,
                  in which case env itself is NULL and nothing reads this)
         field 2..N+1: captures in declaration order
       drop_fn stays at offset 0 so scope cleanup's raw offset-0 load is
       unaffected by the inserted clone_fn slot. */
    LLVMTypeRef env_struct_t = NULL;
    if (cap_n > 0) {
        LLVMTypeRef *fields = (LLVMTypeRef*)malloc_safe(
            (size_t)(cap_n + 2) * sizeof(LLVMTypeRef));
        fields[0] = ptr_t; /* drop_fn slot */
        fields[1] = ptr_t; /* clone_fn slot (Phase G) */
        for (int i = 0; i < cap_n; i++) {
            Type *ct = node->as.closure.captures[i].type;
            bool explicit_move = node->as.closure.captures[i].is_explicit_move;
            bool is_default_by_ref = capture_type_is_by_ref_cg(ct) && !explicit_move;
            if (is_default_by_ref) {
                fields[i + 2] = ptr_t; /* pointer to outer alloca */
            } else {
                fields[i + 2] = type_to_llvm(ctx, ct);
            }
        }
        env_struct_t = LLVMStructTypeInContext(ctx->context, fields,
                                               (unsigned)(cap_n + 2), 0);
        free(fields);
    }

    /* 2) Build LLVM signature: ret(env_ptr, params...) */
    LLVMTypeRef *params_llvm = (LLVMTypeRef *)malloc_safe(
        (size_t)(n + 1) * sizeof(LLVMTypeRef));
    params_llvm[0] = ptr_t; /* env */
    for (int i = 0; i < n; i++)
    {
        params_llvm[i + 1] = type_to_llvm(ctx, block_t->as.function.params[i]);
    }
    Type *ret_lst = block_t->as.function.return_type;
    LLVMTypeRef ret_llvm = type_to_llvm(ctx, ret_lst);
    LLVMTypeRef fn_type_llvm = LLVMFunctionType(ret_llvm, params_llvm,
                                                (unsigned)(n + 1), 0);
    free(params_llvm);

    /* 3) Create the function under a unique name. */
    char fn_name[64];
    int id = ctx->closure_id_counter++;
    snprintf(fn_name, sizeof(fn_name), "__closure_%d", id);
    LLVMValueRef fn = LLVMAddFunction(ctx->module, fn_name, fn_type_llvm);

    /* 4) Save outer codegen state and switch to the new function's body. */
    LLVMBasicBlockRef saved_block = LLVMGetInsertBlock(ctx->builder);
    LLVMValueRef saved_fn = ctx->current_fn;
    Type *saved_fn_ret = ctx->current_fn_return_type;
    CgScope *saved_scope = ctx->current_scope;
    int saved_temp_count = ctx->temp_string_count;
    /* Isolate the body's statement-level temp stacks from the parent's. The
       closure body is a separate function: its own rvalue temporaries (has_drop
       struct/enum/vec drops + closure-env drops) must not be drained by — and
       must not leak into — the outer function. Without this, a temp registered
       in the parent before this closure literal (e.g. the rvalue receiver of a
       chained method call `v.map(U)(...).reduce(U)(...)`) would be flushed
       INSIDE the closure body, referencing an alloca from another function
       (LLVM "instruction does not dominate all uses"). Mirrors temp_string. */
    int saved_temp_drop_count  = ctx->temp_drop_count;
    int saved_temp_block_env_count = ctx->temp_block_env_count;

    LLVMBasicBlockRef entry =
        LLVMAppendBasicBlockInContext(ctx->context, fn, "entry");
    LLVMPositionBuilderAtEnd(ctx->builder, entry);
    ctx->current_fn = fn;
    ctx->current_fn_return_type = ret_lst;
    ctx->temp_string_count = 0;
    ctx->temp_drop_count = 0;
    ctx->temp_block_env_count = 0;

    /* Detach from outer scope chain — only params + captures should be
       visible inside the closure body. */
    ctx->current_scope = NULL;
    push_scope(ctx);

    /* 5a) Materialise captures inside the body. Field 0 is the drop_fn
       slot, so user captures live at indices 1..N.

       Two strategies depending on capture kind:
       - by-move (string/struct): load value from env slot → alloca →
         cg_scope_define with is_borrowed=true (env is sole owner of heap).
       - by-ref (map): env slot holds a pointer to the OUTER alloca.
         Load the pointer from env, use it directly as sym->value. Body
         reads/writes go straight to the outer variable, so mutations are
         visible bidirectionally. Mark is_borrowed=true so scope cleanup
         doesn't call drop on what it doesn't own. */
    if (cap_n > 0) {
        LLVMValueRef env_param = LLVMGetParam(fn, 0);
        for (int i = 0; i < cap_n; i++) {
            Type *ct = node->as.closure.captures[i].type;
            const char *cap_name = node->as.closure.captures[i].name;
            bool explicit_move = node->as.closure.captures[i].is_explicit_move;
            bool is_default_by_ref = capture_type_is_by_ref_cg(ct) && !explicit_move;
            LLVMValueRef field_ptr = LLVMBuildStructGEP2(
                ctx->builder, env_struct_t, env_param,
                (unsigned)(i + 2), "cap.gep");

            if (is_default_by_ref) {
                /* By-ref (default map): load the outer alloca pointer.
                   sym->value = that pointer = the outer alloca itself.
                   Body accesses the outer map in-place. is_borrowed
                   prevents scope cleanup from dropping what it doesn't own. */
                LLVMValueRef outer_ptr = LLVMBuildLoad2(
                    ctx->builder, ptr_t, field_ptr, "cap.refptr");
                CgSymbol *cs = cg_scope_define(ctx->current_scope,
                                cap_name, outer_ptr, ct, NULL);
                if (cs) cs->is_borrowed = true;
            } else {
                /* By-move (or POD or explicit [move] map): load value,
                   alloca, store, register. */
                LLVMTypeRef ct_llvm = type_to_llvm(ctx, ct);
                LLVMValueRef field_val = LLVMBuildLoad2(
                    ctx->builder, ct_llvm, field_ptr, "cap.fromenv");
                LLVMValueRef alloca = LLVMBuildAlloca(
                    ctx->builder, ct_llvm, cap_name);
                LLVMBuildStore(ctx->builder, field_val, alloca);
                CgSymbol *cs = cg_scope_define(ctx->current_scope,
                                cap_name, alloca, ct, NULL);
                /* Body must not drop env-owned heap.
                   For [move] map: env owns the buckets; mark borrowed
                   so scope cleanup doesn't free it (env_drop handles it). */
                bool needs_borrow = capture_type_is_by_move_cg(ct);
                if (cs && needs_borrow)
                    cs->is_borrowed = true;
            }
        }
    }

    /* 5b) Define each user parameter as alloca + store. The LLVM param at
       slot (i+1) skips the env at slot 0.
       map/Block params are marked is_borrowed=true — the caller owns
       the underlying heap (bucket array / env block), so the
       closure body's scope cleanup must not free it (matches the behaviour
       of regular fn params, codegen_fn_decl line ~12117). */
    for (int i = 0; i < n; i++)
    {
        Type *pt = block_t->as.function.params[i];
        /* M5-002: a `&T` closure param uses pointer ABI (borrow), exactly like a
           regular function's &T param (codegen_fn_decl ~13201). The LLVM param IS
           the pointer; register the symbol with that pointer as its value and the
           UNWRAPPED pointee type, is_borrowed so the body GEPs through it and
           scope cleanup leaves ownership with the caller. (The call site passes a
           pointer for &T params — see codegen_block_call.) */
        if (pt && pt->kind == TYPE_REFERENCE)
        {
            Type *pointee = pt->as.pointer_to;
            LLVMValueRef ptr = LLVMGetParam(fn, (unsigned)(i + 1));
            CgSymbol *psym = cg_scope_define(ctx->current_scope,
                            node->as.closure.param_names[i], ptr, pointee, NULL);
            if (psym)
            {
                psym->is_borrowed = true;
                if (pt->is_mut) psym->is_mut_borrow = true;
            }
            continue;
        }
        LLVMTypeRef pt_llvm = type_to_llvm(ctx, pt);
        LLVMValueRef param_val = LLVMGetParam(fn, (unsigned)(i + 1));
        LLVMValueRef alloca = cg_entry_alloca(ctx, pt_llvm,
                                              node->as.closure.param_names[i]);
        LLVMBuildStore(ctx->builder, param_val, alloca);
        CgSymbol *psym = cg_scope_define(ctx->current_scope,
                        node->as.closure.param_names[i],
                        alloca, pt, NULL);
        if (psym && pt &&
            pt->kind == TYPE_BLOCK)
            psym->is_borrowed = true;
    }

    /* 6) Compile the body. */
    codegen_stmt(ctx, node->as.closure.body);

    /* 7) Ensure the entry block (and any continuation block) has a terminator.
       If the user body has no explicit return, default to ret 0 / ret void. */
    LLVMBasicBlockRef cur = LLVMGetInsertBlock(ctx->builder);
    if (cur && LLVMGetBasicBlockTerminator(cur) == NULL)
    {
        if (LLVMGetTypeKind(ret_llvm) == LLVMVoidTypeKind)
        {
            LLVMBuildRetVoid(ctx->builder);
        }
        else
        {
            LLVMBuildRet(ctx->builder, LLVMConstNull(ret_llvm));
        }
    }

    pop_scope(ctx);

    /* 8) Restore outer state. */
    ctx->current_scope = saved_scope;
    ctx->current_fn = saved_fn;
    ctx->current_fn_return_type = saved_fn_ret;
    ctx->temp_string_count = saved_temp_count;
    ctx->temp_drop_count = saved_temp_drop_count;
    ctx->temp_block_env_count = saved_temp_block_env_count;
    if (saved_block) LLVMPositionBuilderAtEnd(ctx->builder, saved_block);

    /* 9a) If any capture needs heap drop (string in v1) synthesise a per-
       closure __env_drop_<id> and remember its address — stored into env
       field 0 so RAII can call it without knowing the env's static type. */
    LLVMValueRef env_drop_fn = LLVMConstNull(ptr_t);
    if (has_drop_n > 0) {
        LLVMTypeRef drop_param_t[1] = { ptr_t };
        LLVMTypeRef drop_fn_ty = LLVMFunctionType(
            LLVMVoidTypeInContext(ctx->context), drop_param_t, 1, 0);
        char drop_name[80];
        snprintf(drop_name, sizeof(drop_name), "__env_drop_%d", id);
        LLVMValueRef drop_fn = LLVMAddFunction(ctx->module, drop_name,
                                               drop_fn_ty);

        /* Save outer state (we re-use the same save vars conceptually,
           but at this point saved_block / saved_fn already point at the
           outer post-restore state — i.e. caller's BB. We therefore
           snapshot anew for this nested emission.) */
        LLVMBasicBlockRef d_saved_block = LLVMGetInsertBlock(ctx->builder);
        LLVMBasicBlockRef d_entry = LLVMAppendBasicBlockInContext(
            ctx->context, drop_fn, "entry");
        LLVMPositionBuilderAtEnd(ctx->builder, d_entry);
        LLVMValueRef d_env = LLVMGetParam(drop_fn, 0);

        /* For each by-move capture, dispatch on type:
              string : cap > 0 → free(data)                       (C.5)
              struct : call Struct.__drop(slot_ptr)                (C.7) */
        for (int i = 0; i < cap_n; i++) {
            Type *ct = node->as.closure.captures[i].type;
            /* Drop this slot if it's a by-move type. */
            bool needs_drop = capture_type_is_by_move_cg(ct);
            if (!needs_drop) continue;
            LLVMValueRef slot = LLVMBuildStructGEP2(
                ctx->builder, env_struct_t, d_env,
                (unsigned)(i + 2), "cap.slot");

            if (ct->kind == TYPE_STRUCT && ct->as.strukt.has_drop) {
                /* Call the struct's auto/user-defined __drop on its slot. */
                LLVMValueRef sdfn = (LLVMValueRef)ct->as.strukt.drop_fn;
                if (sdfn == NULL) {
                    /* Defensive: if the struct's __drop hasn't been emitted
                       yet, force-emit it now. */
                    emit_auto_drop_fn(ctx, ct);
                    sdfn = (LLVMValueRef)ct->as.strukt.drop_fn;
                }
                if (sdfn) {
                    LLVMTypeRef sft = LLVMGlobalGetValueType(sdfn);
                    LLVMBuildCall2(ctx->builder, sft, sdfn, &slot, 1, "");
                }
            }
            else if (ct->kind == TYPE_ENUM && ct->as.enom.has_drop) {
                /* F.5: Call the enum's auto-generated __drop on its slot. */
                LLVMValueRef edfn = (LLVMValueRef)ct->as.enom.drop_fn;
                if (edfn == NULL) {
                    emit_auto_enum_drop_fn(ctx, ct);
                    edfn = (LLVMValueRef)ct->as.enom.drop_fn;
                }
                if (edfn) {
                    LLVMTypeRef eft = LLVMGlobalGetValueType(edfn);
                    LLVMBuildCall2(ctx->builder, eft, edfn, &slot, 1, "");
                }
            }
        }
        LLVMBuildRetVoid(ctx->builder);

        if (d_saved_block) LLVMPositionBuilderAtEnd(ctx->builder, d_saved_block);
        env_drop_fn = drop_fn;
    }

    /* 9a-clone) Phase G: synthesise a per-closure
         ptr __env_clone_<id>(ptr src_env)
       that deep-copies the env so a copy-out site (`Block g = vec[i]`) can own an
       INDEPENDENT env. Emitted whenever the closure has captures — even POD-only —
       because the env block itself is heap-allocated and two Block values sharing
       one env pointer would double-free it on scope exit.

       Per-capture policy:
          by-ref map (default): copy the outer-alloca pointer shallowly. The
            env does not own it (not in has_drop set), so the clone safely shares
            the by-ref just like the original — no double-free.
          string/map/struct/enum (by-move or [move]): deep clone via the
            existing emit_*_clone_val helpers.
         POD / array(POD): plain value copy (emit_clone_value default). */
    LLVMValueRef env_clone_fn = LLVMConstNull(ptr_t);
    if (cap_n > 0) {
        LLVMTypeRef clone_param_t[1] = { ptr_t };
        LLVMTypeRef clone_fn_ty = LLVMFunctionType(ptr_t, clone_param_t, 1, 0);
        char clone_name[80];
        snprintf(clone_name, sizeof(clone_name), "__env_clone_%d", id);
        LLVMValueRef clone_fn = LLVMAddFunction(ctx->module, clone_name,
                                                clone_fn_ty);

        LLVMBasicBlockRef c_saved_block = LLVMGetInsertBlock(ctx->builder);
        LLVMValueRef c_saved_fn = ctx->current_fn;
        LLVMBasicBlockRef c_entry = LLVMAppendBasicBlockInContext(
            ctx->context, clone_fn, "entry");
        LLVMPositionBuilderAtEnd(ctx->builder, c_entry);
        ctx->current_fn = clone_fn;
        LLVMValueRef c_src = LLVMGetParam(clone_fn, 0);

        /* malloc a fresh env of identical size. */
        unsigned long long esz = LLVMABISizeOfType(
            LLVMGetModuleDataLayout(ctx->module), env_struct_t);
        LLVMValueRef csz = LLVMConstInt(LLVMInt64TypeInContext(ctx->context),
                                        esz, 0);
        LLVMValueRef c_dst = cg_emit_alloc(ctx, csz, "closure.env.clone",
                                           node->line, node->column);

        /* Copy field 0 (drop_fn) and field 1 (clone_fn) verbatim. */
        for (unsigned f = 0; f < 2; f++) {
            LLVMValueRef sp = LLVMBuildStructGEP2(ctx->builder, env_struct_t,
                                                  c_src, f, "cl.shdr");
            LLVMValueRef dp = LLVMBuildStructGEP2(ctx->builder, env_struct_t,
                                                  c_dst, f, "cl.dhdr");
            LLVMValueRef hv = LLVMBuildLoad2(ctx->builder, ptr_t, sp, "cl.hdr");
            LLVMBuildStore(ctx->builder, hv, dp);
        }

        /* Per-capture deep copy. */
        for (int i = 0; i < cap_n; i++) {
            Type *ct = node->as.closure.captures[i].type;
            bool explicit_move_i = node->as.closure.captures[i].is_explicit_move;
            bool is_default_by_ref_i =
                capture_type_is_by_ref_cg(ct) && !explicit_move_i;
            LLVMValueRef s_slot = LLVMBuildStructGEP2(
                ctx->builder, env_struct_t, c_src, (unsigned)(i + 2), "cl.sslot");
            LLVMValueRef d_slot = LLVMBuildStructGEP2(
                ctx->builder, env_struct_t, c_dst, (unsigned)(i + 2), "cl.dslot");
            if (is_default_by_ref_i) {
                LLVMValueRef p = LLVMBuildLoad2(ctx->builder, ptr_t, s_slot,
                                                "cl.refp");
                LLVMBuildStore(ctx->builder, p, d_slot);
            } else {
                LLVMTypeRef ct_llvm = type_to_llvm(ctx, ct);
                LLVMValueRef sv = LLVMBuildLoad2(ctx->builder, ct_llvm, s_slot,
                                                 "cl.sv");
                LLVMValueRef cv = emit_clone_value(ctx, sv, ct_llvm, ct);
                LLVMBuildStore(ctx->builder, cv, d_slot);
            }
        }
        LLVMBuildRet(ctx->builder, c_dst);

        ctx->current_fn = c_saved_fn;
        if (c_saved_block) LLVMPositionBuilderAtEnd(ctx->builder, c_saved_block);
        env_clone_fn = clone_fn;
    }

    /* 9b) Construct the env value (heap) and store each capture into it.
       Field 0 = drop_fn slot; user captures live at fields 1..N. For
       by-move (string) captures we additionally write cap=-1 to the OUTER
       alloca so the outer scope's cleanup safely skips the now-transferred
       heap data. The runtime guard `cap > 0` keeps static strings (cap=0)
       from being mis-marked. For zero captures we leave env=NULL and skip
       all of this. */
    LLVMValueRef env_val = LLVMConstNull(ptr_t);
    if (cap_n > 0 && cap_outer_vals) {
        unsigned long long env_size_const =
            LLVMABISizeOfType(LLVMGetModuleDataLayout(ctx->module),
                              env_struct_t);
        LLVMValueRef sz_v = LLVMConstInt(LLVMInt64TypeInContext(ctx->context),
                                         env_size_const, 0);
        env_val = cg_emit_alloc(ctx, sz_v, "closure.env",
                                node->line, node->column);
        /* F.6: log env allocation (closure id + size + runtime ptr). */
        cg_dbg_env_alloc(ctx, id, env_size_const, env_val);

        /* drop_fn slot first (NULL for POD-only envs). */
        LLVMValueRef drop_slot = LLVMBuildStructGEP2(
            ctx->builder, env_struct_t, env_val, 0u, "env.dropslot");
        LLVMBuildStore(ctx->builder, env_drop_fn, drop_slot);

        /* Phase G: clone_fn slot at field 1. */
        LLVMValueRef clone_slot = LLVMBuildStructGEP2(
            ctx->builder, env_struct_t, env_val, 1u, "env.cloneslot");
        LLVMBuildStore(ctx->builder, env_clone_fn, clone_slot);

        for (int i = 0; i < cap_n; i++) {
            Type *ct = node->as.closure.captures[i].type;
            LLVMValueRef field_ptr = LLVMBuildStructGEP2(
                ctx->builder, env_struct_t, env_val,
                (unsigned)(i + 2), "cap.slot");

            /* Store capture into env:
               - by-ref (map): cap_outer_vals[i] IS the outer alloca ptr,
                  so we're storing a pointer-to-alloca into the ptr-typed slot.
                 No ownership transfer; outer remains live.
               - by-move (string/struct/POD): cap_outer_vals[i] is a loaded
                 value; env takes ownership of the heap data. */
            LLVMBuildStore(ctx->builder, cap_outer_vals[i], field_ptr);

            /* By-move marker on the outer alloca:
                 string: cap field gets -1 when currently > 0 (skip .rodata).
                 struct: moved_flag i1 alloca set to true.
                  [move] map: zero out outer map's cap field → __drop skips.
               Default by-ref map: outer is NOT marked at all. */
            bool explicit_move_i = node->as.closure.captures[i].is_explicit_move;
            bool is_default_by_ref_i = capture_type_is_by_ref_cg(ct) && !explicit_move_i;
            if (is_default_by_ref_i) {
                /* By-ref: outer is still live; do nothing. */
            } else if (capture_type_is_by_move_cg(ct) && cap_outer_allocas[i]) {
                if (capture_outer_marker_uses_cap(ct)) {
                    /* Only string uses the cap=-1 trick. */
                    LLVMTypeRef i32_t = LLVMInt32TypeInContext(ctx->context);
                    LLVMValueRef cap_ptr = LLVMBuildStructGEP2(
                        ctx->builder, ls_string_type(ctx), cap_outer_allocas[i],
                        2u, "outer.cap.ptr");
                    /* Conditional: only mark when cap > 0 (skip .rodata). */
                    LLVMValueRef cur_cap = LLVMBuildLoad2(
                        ctx->builder, i32_t, cap_ptr, "outer.cap.cur");
                    LLVMValueRef is_owned = LLVMBuildICmp(
                        ctx->builder, LLVMIntSGT, cur_cap,
                        LLVMConstInt(i32_t, 0, 0), "outer.owned");
                    LLVMValueRef cur_fn_ll = LLVMGetBasicBlockParent(
                        LLVMGetInsertBlock(ctx->builder));
                    LLVMBasicBlockRef mark_bb = LLVMAppendBasicBlockInContext(
                        ctx->context, cur_fn_ll, "cap.mark");
                    LLVMBasicBlockRef ckdone_bb = LLVMAppendBasicBlockInContext(
                        ctx->context, cur_fn_ll, "cap.markdone");
                    LLVMBuildCondBr(ctx->builder, is_owned, mark_bb, ckdone_bb);
                    LLVMPositionBuilderAtEnd(ctx->builder, mark_bb);
                    LLVMBuildStore(ctx->builder,
                                   LLVMConstInt(i32_t,
                                       (unsigned long long)(long long)-1, 1),
                                   cap_ptr);
                    LLVMBuildBr(ctx->builder, ckdone_bb);
                    LLVMPositionBuilderAtEnd(ctx->builder, ckdone_bb);
                    /* F.6: log outer cap=-1 mark for string capture. */
                    cg_dbg_outer_mark(ctx, node->as.closure.captures[i].name,
                                      "cap=-1");
                } else if (ct->kind == TYPE_STRUCT || ct->kind == TYPE_ENUM) {
                    /* F.5: enum uses the same moved_flag mechanism as struct. */
                    const char *cap_name = node->as.closure.captures[i].name;
                    CgSymbol *outer_sym =
                        cg_scope_resolve(saved_scope, cap_name);
                    if (outer_sym && outer_sym->moved_flag) {
                        LLVMTypeRef i1_t = LLVMInt1TypeInContext(ctx->context);
                        LLVMBuildStore(ctx->builder,
                                       LLVMConstInt(i1_t, 1, 0),
                                       outer_sym->moved_flag);
                        /* F.6: log moved_flag=1 mark. */
                        cg_dbg_outer_mark(ctx, cap_name, "moved_flag=1");
                    }
                }
            }
        }
    }
    free(cap_outer_vals);
    free(cap_outer_allocas);

    /* 10) Materialise the LsBlock value: { fn_ptr, env_ptr }. */
    LLVMTypeRef block_llvm = type_to_llvm(ctx, block_t);
    LLVMValueRef val = LLVMGetUndef(block_llvm);
    val = LLVMBuildInsertValue(ctx->builder, val, fn, 0, "blk.fn");
    val = LLVMBuildInsertValue(ctx->builder, val, env_val, 1, "blk.env");

    /* 11) Phase C.5: register env as a statement-temp. If a Block-typed
       var_decl / return / store consumes this literal, it will pop the
       last entry to claim ownership before flush. Otherwise the env was
       a true rvalue (e.g. arg to a call that borrowed it) and gets freed
       at the next statement boundary by cg_flush_temps. NULL envs (no
       captures) are filtered out by cg_push_temp_block_env. */
    cg_push_temp_block_env(ctx, env_val);
    return val;
}

/* Lower `closure(args)` to an indirect call through the fat pointer.
   The callee expression has already been type-checked as TYPE_BLOCK. */
static LLVMValueRef codegen_block_call(CodegenContext *ctx, AstNode *node)
{
    Type *block_t = node->as.call.callee->resolved_type;
    if (block_t == NULL ||
        (block_t->kind != TYPE_BLOCK && block_t->kind != TYPE_FUNCTION))
    {
        cg_error(ctx, node->line, node->column,
                 "internal: block call without Block type on callee");
        return NULL;
    }

    LLVMValueRef closure_val = codegen_expr(ctx, node->as.call.callee);
    if (closure_val == NULL) return NULL;

    LLVMValueRef fn_ptr  = LLVMBuildExtractValue(ctx->builder, closure_val, 0,
                                                 "blk.fn");
    LLVMValueRef env_ptr = LLVMBuildExtractValue(ctx->builder, closure_val, 1,
                                                 "blk.env");

    /* Build the actual fn type at the call site: ret(env_ptr, params...). */
    int n = block_t->as.function.param_count;
    LLVMTypeRef ptr_t = LLVMPointerTypeInContext(ctx->context, 0);
    LLVMTypeRef *params_llvm = (LLVMTypeRef *)malloc_safe(
        (size_t)(n + 1) * sizeof(LLVMTypeRef));
    params_llvm[0] = ptr_t;
    for (int i = 0; i < n; i++)
    {
        params_llvm[i + 1] = type_to_llvm(ctx, block_t->as.function.params[i]);
    }
    LLVMTypeRef ret_llvm = type_to_llvm(ctx, block_t->as.function.return_type);
    LLVMTypeRef fn_type = LLVMFunctionType(ret_llvm, params_llvm,
                                           (unsigned)(n + 1), 0);
    free(params_llvm);

    /* Build args: env_ptr first, then user args (with widening per param). */
    int argc = node->as.call.arg_count;
    if (argc != n)
    {
        cg_error(ctx, node->line, node->column,
                 "internal: closure call argc mismatch (%d vs %d)", argc, n);
        return NULL;
    }
    LLVMValueRef *args = (LLVMValueRef *)malloc_safe(
        (size_t)(n + 1) * sizeof(LLVMValueRef));
    args[0] = env_ptr;
    for (int i = 0; i < n; i++)
    {
        LLVMValueRef av = codegen_expr(ctx, node->as.call.args[i]);
        if (av == NULL) { free(args); return NULL; }
        Type *src_t = node->as.call.args[i]->resolved_type;
        Type *dst_t = block_t->as.function.params[i];

        /* M5-002: a Block parameter of reference type `&T` expects a POINTER
           (borrow ABI), exactly like a regular function's &T param. codegen_expr
           loaded the value above; pass the address instead. IDENT → its slot
           pointer (alloca, or the incoming borrow pointer); rvalue/other →
           materialise a temp alloca (registered for drop if it owns heap, since
           the callee only borrows). */
        if (dst_t && dst_t->kind == TYPE_REFERENCE)
        {
            Type *pointee = dst_t->as.pointer_to;
            AstNode *a = node->as.call.args[i];
            LLVMValueRef ptr = NULL;
            if (a->kind == AST_IDENT)
            {
                CgSymbol *sym = cg_scope_resolve(ctx->current_scope,
                                                 a->as.ident.name);
                if (sym) ptr = sym->value;
            }
            if (ptr == NULL)
            {
                LLVMTypeRef store_t = type_to_llvm(ctx, pointee ? pointee : src_t);
                LLVMValueRef tmp = cg_entry_alloca(ctx, store_t, "blk.borrow.tmp");
                LLVMBuildStore(ctx->builder, av, tmp);
                ptr = tmp;
                if (pointee &&
                    ((pointee->kind == TYPE_STRUCT && pointee->as.strukt.has_drop) ||
                     (pointee->kind == TYPE_ENUM   && pointee->as.enom.has_drop)))
                    cg_push_temp_drop(ctx, tmp, pointee);
            }
            args[i + 1] = ptr;
            continue;
        }

        av = cg_widen(ctx, av, src_t, dst_t);

        /* Match regular function-call ownership for Block parameters.
           A closure parameter is materialised as a local in the closure body:
           string params use cap to distinguish borrowed vs owned, vec/map/Block
           params are call-borrowed, and has_drop struct/enum params are dropped
           by the closure. Therefore the call site must not pass a raw alias for
           heap-owning named values. */
        Type *arg_type = dst_t ? dst_t : src_t;
        AstNode *raw = node->as.call.args[i];
        AstNode *unwrapped = ast_unwrap_move(raw);
        bool is_move_expr = (raw != unwrapped);

        if (av && arg_type && arg_type->kind == TYPE_STRUCT &&
                 arg_type->as.strukt.has_drop)
        {
            if (unwrapped && unwrapped->kind == AST_IDENT)
            {
                if (is_move_expr)
                {
                    CgSymbol *argsym = cg_scope_resolve(ctx->current_scope,
                                                        unwrapped->as.ident.name);
                    if (argsym && argsym->moved_flag)
                    {
                        LLVMBuildStore(ctx->builder,
                            LLVMConstInt(LLVMInt1TypeInContext(ctx->context), 1, 0),
                            argsym->moved_flag);
                    }
                }
                else
                {
                    LLVMTypeRef st_llvm = type_to_llvm(ctx, arg_type);
                    av = emit_struct_clone_val(ctx, av, st_llvm, arg_type);
                }
            }
        }
        else if (av && arg_type && arg_type->kind == TYPE_ENUM &&
                 arg_type->as.enom.has_drop)
        {
            if (unwrapped && unwrapped->kind == AST_IDENT)
            {
                if (is_move_expr)
                {
                    CgSymbol *argsym = cg_scope_resolve(ctx->current_scope,
                                                        unwrapped->as.ident.name);
                    if (argsym && argsym->moved_flag)
                    {
                        LLVMBuildStore(ctx->builder,
                            LLVMConstInt(LLVMInt1TypeInContext(ctx->context), 1, 0),
                            argsym->moved_flag);
                    }
                }
                else
                {
                    av = emit_enum_clone_val(ctx, av, arg_type);
                }
            }
        }
        args[i + 1] = av;
    }

    bool void_ret = (LLVMGetTypeKind(ret_llvm) == LLVMVoidTypeKind);
    LLVMValueRef result = LLVMBuildCall2(ctx->builder, fn_type, fn_ptr,
                                         args, (unsigned)(n + 1),
                                         void_ret ? "" : "blk.call");
    free(args);
    (void)env_ptr;

    return result;
}

static void codegen_fn_decl(CodegenContext *ctx, AstNode *node)
{
    /* G2: skip generic function templates — instantiated on demand */
    if (node->as.fn_decl.type_param_count > 0) return;

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
    /* Same isolation for M-4.5 has_drop temp slots: a function whose last
       statement early-returns from every match arm can leave registered
       temp_drop entries unflushed; without this reset the NEXT function's
       first flush emits drops referencing the previous function's allocas
       (LLVM "instruction does not dominate all uses"). Hit by the B-2
       string->Str call-arg bridge in std.json's io wrappers. */
    int saved_temp_drop_count = ctx->temp_drop_count;
    ctx->temp_drop_count = 0;

    int total_n = fn_type_ml->as.function.param_count;
    LLVMTypeRef fn_type = type_to_llvm(ctx, fn_type_ml);

    /* AOT fix: C runtime expects `int main()`.  When the user writes
       `fn main()` (void return), override the LLVM function type to
       return i32 so the CRT receives a well-defined exit code (0).
       Without this, `ret void` leaves EAX undefined and the OS may
       report a garbage exit code. */
    bool is_main_void = (strcmp(name, "main") == 0 &&
                         fn_type_ml->as.function.return_type->kind == TYPE_VOID &&
                         user_n == 0);
    /* bug #22: in AOT, the entry main() takes the C signature
       int main(int argc, char **argv) so we can forward argc/argv to
       __ls_set_args (done in the injection pass below), making proc.args()
       work in compiled executables. JIT sets args in main.c, so it is excluded
       via ctx->aot_entry. Module-emitted mains are never the entry point. */
    bool is_main_entry = (strcmp(name, "main") == 0 && user_n == 0 &&
                          ctx->aot_entry && ctx->current_emit_module == NULL);
    if (is_main_void || is_main_entry)
    {
        LLVMTypeRef i32_t = LLVMInt32TypeInContext(ctx->context);
        if (is_main_entry)
        {
            LLVMTypeRef pt = LLVMPointerType(LLVMInt8TypeInContext(ctx->context), 0);
            LLVMTypeRef ps[] = { i32_t, pt };  /* argc, argv */
            fn_type = LLVMFunctionType(i32_t, ps, 2, 0);
        }
        else
        {
            fn_type = LLVMFunctionType(i32_t, NULL, 0, 0);
        }
    }

    /* L-009: free functions in an imported module use a module-prefixed LLVM
       symbol so same-named functions across modules don't collide. Impl/struct
       methods are excluded — they already carry a "Struct.method" qualified name
       and their call sites resolve via that (module-qualified struct methods are
       a separate follow-up, L-009.1). */
    char sym_buf[512];
    const char *sym_name = name;
    if (ctx->current_emit_module != NULL &&
        node->as.fn_decl.impl_struct_name == NULL)
    {
        cg_module_fn_symbol(sym_buf, sizeof(sym_buf),
                            ctx->current_emit_module, name);
        sym_name = sym_buf;
    }

    /* Check for existing function (forward decl) */
    LLVMValueRef fn = LLVMGetNamedFunction(ctx->module, sym_name);
    if (fn == NULL)
    {
        fn = LLVMAddFunction(ctx->module, sym_name, fn_type);
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
        cg_emit_prof_enter(ctx, name, fn_file, node->line);
    }

    LLVMValueRef saved_fn = ctx->current_fn;
    Type *saved_fn_ret = ctx->current_fn_return_type;
    bool saved_is_main_void = ctx->is_main_void;
    ctx->current_fn = fn;
    ctx->current_fn_return_type = fn_type_ml ? fn_type_ml->as.function.return_type : NULL;
    ctx->is_main_void = is_main_void;

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
            LLVMValueRef self_alloca = cg_entry_alloca(ctx, self_llvm, "self");
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
        /* Phase 5.6/5.7/9 + P4: read-only &T uses pointer ABI (checker only
           admits struct/enum pointees since &string was removed). Register
           sym->value as a raw pointer to the underlying struct — all codegen
           paths treat sym->value as a pointer.
           Checker statically forbids mutating calls on this symbol. */
        if (param_type && param_type->kind == TYPE_REFERENCE &&
            !param_type->is_mut)
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
        LLVMTypeRef param_llvm = type_to_llvm(ctx, param_type);
        LLVMValueRef alloca = cg_entry_alloca(ctx, param_llvm,
                                              node->as.fn_decl.param_names[i]);
        LLVMBuildStore(ctx->builder, LLVMGetParam(fn, (unsigned)llvm_idx), alloca);
        /* Allocate moved_flag for struct-with-drop and has_drop enum parameters.
           F.5: enum params also need move tracking for closure by-move capture. */
        LLVMValueRef moved_flag = NULL;
        if (param_type &&
            ((param_type->kind == TYPE_STRUCT && param_type->as.strukt.has_drop) ||
             (param_type->kind == TYPE_ENUM   && param_type->as.enom.has_drop)))
        {
            LLVMTypeRef i1_type = LLVMInt1TypeInContext(ctx->context);
            moved_flag = cg_entry_alloca(ctx, i1_type, "param.moved");
            LLVMBuildStore(ctx->builder, LLVMConstInt(i1_type, 0, 0), moved_flag);
        }
        /* String parameters: keep incoming cap value.
           - cap == LS_CAP_BORROWED (-2): caller retains ownership, callee borrows.
             emit_string_free skips (cap <= 0), cg_store_owned clones on store.
           - cap > 0 (owned): callee owns the string. Passed for rvalue/__move args.
             emit_string_free frees on scope exit; cg_store_owned moves on store.
           The call site sets cap=-2 for named variable args and leaves cap>0
           for rvalue/__move args, so the distinction is preserved through ABI. */
        CgSymbol *psym = cg_scope_define(ctx->current_scope, node->as.fn_decl.param_names[i], alloca, param_type, moved_flag);
        /* Phase C.5: Block-typed parameters are call-borrowed — the caller
           owns the closure's env block, so the callee's scope cleanup must
           not free it (would double-free against the caller's local). The
           callee can call the closure freely; ownership transfer (Block
           moves) is a future refinement. */
        if (psym && param_type && param_type->kind == TYPE_BLOCK)
            psym->is_borrowed = true;
        /* NOTE (Bug-6 / M-1 overhaul): self-recursive enum parameters were
           previously marked is_borrowed=true to prevent the callee's scope
           cleanup from freeing boxes that the caller still owned (Bug 5).
           That logic was correct when the enum was passed by raw alias, but
           the call-site now always emits a full deep-clone (Tree.__clone)
           for has_drop enum arguments not wrapped in __move.  The callee
           therefore owns its own independent copy of the enum and MUST drop
           it on scope exit — leaving is_borrowed=true causes the clone to
           leak (34 boxes for build_tree(3) + sum_tree).  The is_borrowed
           hack is removed; callee-side drop is now always performed. */
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
        /* Last statement: if it's an expression stmt whose resolved type matches
           the function's return type, return its value (implicit return).
           Guard: if the expression resolves to void (e.g. a print() call whose
           underlying printf returns int), do NOT use it as the return value —
           fall through to the implicit 'ret 0' path below instead. */
        AstNode *last = body->as.block.stmts[last_idx];
        if (last->kind == AST_EXPR_STMT &&
            LLVMGetBasicBlockTerminator(LLVMGetInsertBlock(ctx->builder)) == NULL)
        {
            AstNode *expr_node = last->as.expr_stmt.expr;
            Type    *expr_type = expr_node->resolved_type;
            bool     expr_is_void = (expr_type == NULL ||
                                     expr_type->kind == TYPE_VOID);
            LLVMValueRef val = codegen_expr(ctx, expr_node);
            if (val && !expr_is_void &&
                LLVMGetBasicBlockTerminator(LLVMGetInsertBlock(ctx->builder)) == NULL)
            {
                /* Emit cleanup BEFORE return */
                emit_cleanup_to(ctx, NULL, NULL);
                cg_emit_mc_leave(ctx);   /* D.1: pop frame */
                cg_emit_prof_leave(ctx);
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
            cg_emit_prof_leave(ctx);
            if (!is_non_void)
            {
                if (is_main_void)
                {
                    /* AOT: return 0 to the C runtime */
                    LLVMBuildRet(ctx->builder,
                        LLVMConstInt(LLVMInt32TypeInContext(ctx->context), 0, 0));
                }
                else
                {
                    LLVMBuildRetVoid(ctx->builder);
                }
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
    ctx->is_main_void = saved_is_main_void;

    /* Restore temp string count to what it was before compiling this function */
    ctx->temp_string_count = saved_temp_count;
    ctx->temp_drop_count = saved_temp_drop_count;

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

/* Compute the enum payload's max size AND max alignment across all variants.
   Bug #25: the old layout `{ i8 tag, [N x i8] payload }` is byte-aligned, so an
   f64/i64 stored in the payload gets `align 1` loads/stores — significantly
   slower (misaligned access). We instead build the payload from an array of an
   alignment-carrying integer type so the whole enum aligns to its widest member.
   *out_size = max payload bytes, *out_align = max ABI alignment (>=1). */
static void cg_enum_payload_dims(CodegenContext *ctx, Type *et,
                                 int *out_size, int *out_align)
{
    LLVMTargetDataRef td = LLVMGetModuleDataLayout(ctx->module);
    int max_payload = 0;
    int max_align = 1;
    for (int v = 0; v < et->as.enom.variant_count; v++)
    {
        if (et->as.enom.variants[v].payload_count == 0) continue;
        LLVMTypeRef vstruct = build_variant_payload_struct(ctx, et, v);
        unsigned long long sz = LLVMABISizeOfType(td, vstruct);
        unsigned al = LLVMABIAlignmentOfType(td, vstruct);
        if ((int)sz > max_payload) max_payload = (int)sz;
        if ((int)al > max_align) max_align = (int)al;
    }
    *out_size = max_payload;
    *out_align = max_align;
}

/* Build the enum body type { i8 tag, <aligned payload> } given size+align.
   The payload is an array of an integer type whose width == max_align, so the
   payload (and thus the whole struct) carries that alignment. */
static void cg_enum_body_fields(CodegenContext *ctx, int max_payload, int max_align,
                                LLVMTypeRef body_out[2])
{
    LLVMTypeRef i8 = LLVMInt8TypeInContext(ctx->context);
    LLVMTypeRef align_elem;
    switch (max_align)
    {
    case 8:  align_elem = LLVMInt64TypeInContext(ctx->context); break;
    case 4:  align_elem = LLVMInt32TypeInContext(ctx->context); break;
    case 2:  align_elem = LLVMInt16TypeInContext(ctx->context); break;
    default: align_elem = i8; break;  /* align 1 (or no payload) */
    }
    int elem_sz = max_align >= 1 ? max_align : 1;
    /* round payload size up to a whole number of align-elements */
    int count = (max_payload + elem_sz - 1) / elem_sz;
    if (count < 0) count = 0;
    body_out[0] = i8;
    body_out[1] = LLVMArrayType2(align_elem, (uint64_t)count);
}

static void codegen_enum_decl(CodegenContext *ctx, AstNode *node)
{
    Type *et = node->resolved_type;
    if (et == NULL || et->kind != TYPE_ENUM) return;
    /* B-2: use LLVM-prefixed name for module-defined enums */
    const char *llvm_name = enum_llvm_name_of(et);
    if (find_enum_llvm(ctx, llvm_name)) return;  /* already registered */

    /* Compute max payload size + alignment, build aligned body (bug #25). */
    int max_payload = 0, max_align = 1;
    cg_enum_payload_dims(ctx, et, &max_payload, &max_align);

    LLVMTypeRef body[2];
    cg_enum_body_fields(ctx, max_payload, max_align, body);
    LLVMTypeRef llvm_type = LLVMStructCreateNamed(ctx->context, llvm_name);
    LLVMStructSetBody(llvm_type, body, 2, 0);

    register_enum_llvm(ctx, llvm_name, llvm_type, et, max_payload);

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
    case TYPE_STRUCT: return t->as.strukt.has_drop;
    case TYPE_ENUM:   return t->as.enom.has_drop;
    default:          return false;
    }
}

/* Unified value-drop authority — single recursive dispatch used by struct/enum
   payload drop, scope cleanup, and temp drop. Frees heap owned by the value at
   `place_ptr` (a pointer to storage of `type`). POD / non-has_drop → no-op.
   vec/map recurse via the element-aware primitives, so nested containers
   (vec(vec(...)), map(K,vec), etc.) drop correctly with no per-site logic. */
static void emit_drop_value(CodegenContext *ctx, LLVMValueRef place_ptr, Type *type)
{
    if (place_ptr == NULL || type == NULL) return;
    switch (type->kind)
    {
    case TYPE_STRING:
        emit_string_free(ctx, place_ptr);
        return;
    case TYPE_STRUCT:
        if (type->as.strukt.has_drop) emit_struct_drop(ctx, place_ptr, type);
        return;
    case TYPE_ENUM:
        if (type->as.enom.has_drop) emit_enum_drop(ctx, place_ptr, type);
        return;
    case TYPE_BLOCK:
        /* F5: a Block slot owns its closure env — free it (running the env's
           drop_fn first for any captured has_drop values). Needed so a pure-LS
           Vec(Block) drops its element envs via __drop_at(self.data[i]). */
        cg_emit_block_drop_at(ctx, place_ptr);
        return;
    default:
        return; /* POD / non-owning */
    }
}

/* Generate `EnumName.__drop(EnumName *self)` for has_drop enums:
   switch on disc → for each variant case → free each owned payload field.
   Self-recursive payload is heap-boxed; we drop the pointee then free the box. */
static void emit_auto_enum_drop_fn(CodegenContext *ctx, Type *enum_type)
{
    if (enum_type == NULL || enum_type->kind != TYPE_ENUM) return;
    if (!enum_type->as.enom.has_drop) return;
    /* NOTE: do NOT early-return on `drop_fn != NULL` here.
       In cross-module scenarios the shared Type* may hold a stale
       LLVMValueRef from a *different* LLVM module.  Fall through to
       the LLVMGetNamedFunction check which correctly validates the
       current module. */

    /* B-2: use LLVM-prefixed name for module-defined enums */
    const char *enum_name = enum_llvm_name_of(enum_type);
    char drop_fn_name[256];
    snprintf(drop_fn_name, sizeof(drop_fn_name), "%s.__drop", enum_name);
    {
        LLVMValueRef existing = LLVMGetNamedFunction(ctx->module, drop_fn_name);
        if (existing != NULL) {
            /* The LLVM function already exists (generated for a different Type*
               instance of the same logical enum type in a cross-module scenario).
               Bind the pointer to this Type* so future callers find it. */
            enum_type->as.enom.drop_fn = existing;
            return;
        }
    }

    LLVMBasicBlockRef saved_bb = LLVMGetInsertBlock(ctx->builder);

    LLVMTypeRef i8 = LLVMInt8TypeInContext(ctx->context);
    LLVMTypeRef ptr_type = LLVMPointerTypeInContext(ctx->context, 0);
    LLVMTypeRef fn_type = LLVMFunctionType(LLVMVoidTypeInContext(ctx->context),
                                            &ptr_type, 1, 0);

    LLVMValueRef drop_fn = LLVMAddFunction(ctx->module, drop_fn_name, fn_type);
    LLVMSetFunctionCallConv(drop_fn, LLVMCCallConv);

    /* Pre-register so recursive variants find it during emission. */
    enum_type->as.enom.drop_fn = drop_fn;

    LLVMBasicBlockRef entry = LLVMAppendBasicBlockInContext(ctx->context, drop_fn, "entry");
    LLVMPositionBuilderAtEnd(ctx->builder, entry);

    LLVMValueRef self_ptr = LLVMGetParam(drop_fn, 0);
    LLVMSetValueName(self_ptr, "self");

    LLVMTypeRef enum_llvm = type_to_llvm(ctx, enum_type);

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
                /* has_drop struct payload (e.g. Vec(T) owning a raw *T buffer).
                   If the struct has a user __drop hook, bind or declare that hook;
                   auto-generating here would create an empty raw-pointer fallback
                   and block the real pending generic body.  Plain compiler-managed
                   structs still get their auto-drop lazily. */
                if (pt->as.strukt.drop_fn == NULL)
                {
                    if (pt->as.strukt.has_user_drop)
                        cg_ensure_user_struct_drop_decl(ctx, pt);
                    else
                        emit_auto_drop_fn(ctx, pt);
                }
                emit_struct_drop(ctx, field_ptr, pt);
            }
            else if (pt && pt->kind == TYPE_ENUM && pt->as.enom.has_drop &&
                     pt != enum_type)
            {
                /* F.5: nested has_drop enum payload (non-self-recursive). */
                LLVMValueRef nedfn = (LLVMValueRef)pt->as.enom.drop_fn;
                if (nedfn == NULL) {
                    emit_auto_enum_drop_fn(ctx, pt);
                    nedfn = (LLVMValueRef)pt->as.enom.drop_fn;
                }
                if (nedfn) {
                    LLVMTypeRef neft = LLVMGlobalGetValueType(nedfn);
                    LLVMBuildCall2(ctx->builder, neft, nedfn, &field_ptr, 1, "");
                }
            }
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
    /* Lazy-generate the drop function if not yet emitted for this module
       (e.g. cross-module compilation where the Type* was instantiated in a
       different checker context and type_to_llvm may not have been called). */
    if (drop_fn == NULL) {
        emit_auto_enum_drop_fn(ctx, enum_type);
        drop_fn = (LLVMValueRef)enum_type->as.enom.drop_fn;
    }
    if (drop_fn == NULL) return;
    LLVMTypeRef fn_type = LLVMGlobalGetValueType(drop_fn);
    LLVMBuildCall2(ctx->builder, fn_type, drop_fn, &enum_ptr, 1, "");
    /* Idempotency: zero the slot after dropping, so a redundant drop of the SAME
       storage is a safe no-op (the discriminant becomes variant 0 with a zeroed
       payload → its drop frees nothing). Mirrors string free zeroing cap.
       This is what makes an OWNED rvalue match subject safe when both an arm-
       internal temp-drop flush AND the merge-block drop run on the same path
       (B-MAP-OPT-001: `match f() { Some(m) => for e in m {...} }`). The dropped
       value is logically dead, so overwriting it is always sound. */
    LLVMTypeRef enum_llvm = type_to_llvm(ctx, enum_type);
    LLVMBuildStore(ctx->builder, LLVMConstNull(enum_llvm), enum_ptr);
}

/* F.5: Conditional enum drop — skip if moved_flag == 1 (by-move captured).
   When moved_flag is NULL, drops unconditionally. */
static void emit_enum_drop_cond(CodegenContext *ctx, LLVMValueRef enum_ptr,
                                Type *enum_type, LLVMValueRef moved_flag)
{
    if (!enum_type || enum_type->kind != TYPE_ENUM) return;
    if (!enum_type->as.enom.has_drop) return;
    LLVMValueRef drop_fn_v = (LLVMValueRef)enum_type->as.enom.drop_fn;
    /* Lazy-generate the drop function if not yet emitted for this module. */
    if (drop_fn_v == NULL) {
        emit_auto_enum_drop_fn(ctx, enum_type);
        drop_fn_v = (LLVMValueRef)enum_type->as.enom.drop_fn;
    }
    if (drop_fn_v == NULL) return;
    if (moved_flag == NULL) {
        /* Unconditional drop (no move tracking for this variable). */
        LLVMTypeRef fnt = LLVMGlobalGetValueType(drop_fn_v);
        LLVMBuildCall2(ctx->builder, fnt, drop_fn_v, &enum_ptr, 1, "");
        return;
    }
    /* Conditional: if moved_flag == 1, skip the drop. */
    LLVMTypeRef i1_t  = LLVMInt1TypeInContext(ctx->context);
    LLVMValueRef cur_fn = LLVMGetBasicBlockParent(LLVMGetInsertBlock(ctx->builder));
    LLVMValueRef is_moved = LLVMBuildLoad2(ctx->builder, i1_t, moved_flag,
                                           "enum.moved");
    static int enum_drop_ctr = 0;
    char skip_name[40], call_name[40];
    snprintf(skip_name, sizeof(skip_name), "edrop.skip%d", enum_drop_ctr);
    snprintf(call_name, sizeof(call_name), "edrop.call%d", enum_drop_ctr);
    enum_drop_ctr++;
    LLVMBasicBlockRef call_bb = LLVMAppendBasicBlockInContext(
        ctx->context, cur_fn, call_name);
    LLVMBasicBlockRef cont_bb = LLVMAppendBasicBlockInContext(
        ctx->context, cur_fn, skip_name);
    LLVMBuildCondBr(ctx->builder, is_moved, cont_bb, call_bb);
    LLVMPositionBuilderAtEnd(ctx->builder, call_bb);
    LLVMTypeRef fnt = LLVMGlobalGetValueType(drop_fn_v);
    LLVMBuildCall2(ctx->builder, fnt, drop_fn_v, &enum_ptr, 1, "");
    LLVMBuildBr(ctx->builder, cont_bb);
    LLVMPositionBuilderAtEnd(ctx->builder, cont_bb);
}

/* ---- emit_auto_enum_clone_fn ----
   Generate a named LLVM function  EnumName.__clone(ptr) -> enum_t
   that deep-copies all heap-owning payload fields.  The function is
   pre-registered on enum_type->as.enom.clone_fn so that recursive
   types (JsonValue with Arr(vec(JsonValue))) don't cause infinite
   inline codegen.  Modeled on emit_auto_enum_drop_fn. */
static void emit_auto_enum_clone_fn(CodegenContext *ctx, Type *enum_type)
{
    if (enum_type == NULL || enum_type->kind != TYPE_ENUM) return;
    if (!enum_type->as.enom.has_drop) return;
    /* NOTE: do NOT early-return on `clone_fn != NULL` here.
       In cross-module scenarios the shared Type* may hold a stale
       LLVMValueRef from a *different* LLVM module.  Fall through to
       the LLVMGetNamedFunction check which correctly validates the
       current module. */

    /* Check if any variant actually needs cloning. */
    int needs_count = 0;
    for (int v = 0; v < enum_type->as.enom.variant_count; v++)
    {
        for (int fi = 0; fi < enum_type->as.enom.variants[v].payload_count; fi++)
        {
            Type *pt = enum_type->as.enom.variants[v].payload_types[fi];
            if (pt && (pt->kind == TYPE_STRING ||
                       (pt->kind == TYPE_STRUCT && pt->as.strukt.has_drop) ||
                       (pt->kind == TYPE_ENUM   && pt->as.enom.has_drop)))
            {
                needs_count++;
                break;
            }
        }
    }
    if (needs_count == 0)
        return; /* no heap fields — clone_fn stays NULL, caller does bitwise copy */

    /* B-2: use LLVM-prefixed name for module-defined enums */
    const char *enum_name = enum_llvm_name_of(enum_type);
    char clone_fn_name[256];
    snprintf(clone_fn_name, sizeof(clone_fn_name), "%s.__clone", enum_name);
    {
        LLVMValueRef existing = LLVMGetNamedFunction(ctx->module, clone_fn_name);
        if (existing != NULL) {
            /* Already generated for a different Type* of the same logical enum.
               Bind the pointer so future callers on this Type* find it. */
            enum_type->as.enom.clone_fn = existing;
            return;
        }
    }

    LLVMBasicBlockRef saved_bb = LLVMGetInsertBlock(ctx->builder);

    LLVMTypeRef enum_llvm = type_to_llvm(ctx, enum_type);
    LLVMTypeRef ptr_type  = LLVMPointerTypeInContext(ctx->context, 0);
    LLVMTypeRef i8        = LLVMInt8TypeInContext(ctx->context);

    /* fn signature: enum_t __clone(ptr self_ptr) */
    LLVMTypeRef fn_type = LLVMFunctionType(enum_llvm, &ptr_type, 1, 0);
    LLVMValueRef clone_fn = LLVMAddFunction(ctx->module, clone_fn_name, fn_type);
    LLVMSetFunctionCallConv(clone_fn, LLVMCCallConv);

    /* Pre-register so recursive variants find it during emission. */
    enum_type->as.enom.clone_fn = clone_fn;

    LLVMBasicBlockRef entry = LLVMAppendBasicBlockInContext(ctx->context, clone_fn, "entry");
    LLVMPositionBuilderAtEnd(ctx->builder, entry);

    LLVMValueRef self_ptr = LLVMGetParam(clone_fn, 0);
    LLVMSetValueName(self_ptr, "self");

    /* Load the full enum value and store into a mutable local. */
    LLVMValueRef orig_val = LLVMBuildLoad2(ctx->builder, enum_llvm, self_ptr, "ec.orig");
    LLVMValueRef tmp = cg_entry_alloca(ctx, enum_llvm, "ec.tmp");
    LLVMBuildStore(ctx->builder, orig_val, tmp);

    /* disc = tmp->field[0] */
    LLVMValueRef disc_ptr = LLVMBuildStructGEP2(ctx->builder, enum_llvm, tmp, 0, "ec.discp");
    LLVMValueRef disc     = LLVMBuildLoad2(ctx->builder, i8, disc_ptr, "ec.disc");

    /* payload_ptr = &tmp->field[1] */
    LLVMValueRef payload_ptr = LLVMBuildStructGEP2(ctx->builder, enum_llvm, tmp, 1, "ec.payp");
#if CG_DEBUG
    {
        LLVMValueRef di = LLVMBuildZExt(ctx->builder, disc,
            LLVMInt32TypeInContext(ctx->context), "ec.di");
        cg_emit_debug_printf(ctx, "[cg] ec.clone  disc=%d\n", &di, 1);
    }
#endif

    LLVMBasicBlockRef end_bb = LLVMAppendBasicBlockInContext(ctx->context, clone_fn, "ec.end");
    LLVMValueRef sw = LLVMBuildSwitch(ctx->builder, disc, end_bb, (unsigned)needs_count);

    for (int v = 0; v < enum_type->as.enom.variant_count; v++)
    {
        bool needs = false;
        for (int fi = 0; fi < enum_type->as.enom.variants[v].payload_count; fi++)
        {
            Type *pt = enum_type->as.enom.variants[v].payload_types[fi];
            if (pt && (pt->kind == TYPE_STRING ||
                       (pt->kind == TYPE_STRUCT && pt->as.strukt.has_drop) ||
                       (pt->kind == TYPE_ENUM   && pt->as.enom.has_drop)))
            {
                needs = true;
                break;
            }
        }
        if (!needs)
            continue;

        LLVMBasicBlockRef case_bb = LLVMAppendBasicBlockInContext(ctx->context, clone_fn, "ec.case");
        LLVMAddCase(sw, LLVMConstInt(i8, (unsigned long long)v, 0), case_bb);
        LLVMPositionBuilderAtEnd(ctx->builder, case_bb);

        LLVMTypeRef variant_struct = build_variant_payload_struct(ctx, enum_type, v);

        for (int fi = 0; fi < enum_type->as.enom.variants[v].payload_count; fi++)
        {
            Type *pt = enum_type->as.enom.variants[v].payload_types[fi];
            if (pt == NULL) continue;

            LLVMValueRef field_ptr = LLVMBuildStructGEP2(ctx->builder, variant_struct,
                                                          payload_ptr, (unsigned)fi, "ec.fp");

            if (pt == enum_type)
            {
                /* Self-recursive: box ptr → load inner enum → clone recursively → store back */
                LLVMValueRef box = LLVMBuildLoad2(ctx->builder, ptr_type, field_ptr, "ec.box");
                LLVMValueRef inner = LLVMBuildLoad2(ctx->builder, enum_llvm, box, "ec.inner");
                /* Allocate new box */
                LLVMValueRef box_sz = LLVMSizeOf(enum_llvm);
                LLVMValueRef malloc_fn = LLVMGetNamedFunction(ctx->module, "malloc");
                LLVMTypeRef malloc_ft = LLVMGlobalGetValueType(malloc_fn);
                LLVMValueRef new_box = LLVMBuildCall2(ctx->builder, malloc_ft, malloc_fn,
                                                       &box_sz, 1, "ec.newbox");
                /* Store cloned inner value into new box */
                LLVMValueRef new_box_tmp = cg_entry_alloca(ctx, enum_llvm, "ec.nbt");
                LLVMBuildStore(ctx->builder, inner, new_box_tmp);
                LLVMValueRef cloned_inner = LLVMBuildCall2(ctx->builder, fn_type, clone_fn,
                                                            &new_box_tmp, 1, "ec.ci");
                LLVMBuildStore(ctx->builder, cloned_inner, new_box);
                /* Store new box ptr into payload field */
                LLVMBuildStore(ctx->builder, new_box, field_ptr);
            }
            else if (pt->kind == TYPE_STRING)
            {
                LLVMTypeRef str_t  = ls_string_type(ctx);
                LLVMValueRef old_s = LLVMBuildLoad2(ctx->builder, str_t, field_ptr, "ec.olds");
                LLVMValueRef new_s = emit_string_clone_val(ctx, old_s);
                LLVMBuildStore(ctx->builder, new_s, field_ptr);
            }

            else if (pt->kind == TYPE_STRUCT && pt->as.strukt.has_drop)
            {
                LLVMTypeRef  st_t   = type_to_llvm(ctx, pt);
                LLVMValueRef old_sv = LLVMBuildLoad2(ctx->builder, st_t, field_ptr, "ec.oldsv");
                LLVMValueRef new_sv = emit_struct_clone_val(ctx, old_sv, st_t, pt);
                LLVMBuildStore(ctx->builder, new_sv, field_ptr);
            }
            else if (pt->kind == TYPE_ENUM && pt->as.enom.has_drop)
            {
                /* Nested has_drop enum — ensure its clone_fn exists, then call it. */
                emit_auto_enum_clone_fn(ctx, pt);
                LLVMValueRef nested_cfn = (LLVMValueRef)pt->as.enom.clone_fn;
                if (nested_cfn)
                {
                    LLVMTypeRef  ncft = LLVMGlobalGetValueType(nested_cfn);
                    LLVMValueRef cloned_ev = LLVMBuildCall2(ctx->builder, ncft, nested_cfn,
                                                             &field_ptr, 1, "ec.nev");
                    LLVMBuildStore(ctx->builder, cloned_ev, field_ptr);
                }
            }
        }

        if (LLVMGetBasicBlockTerminator(LLVMGetInsertBlock(ctx->builder)) == NULL)
            LLVMBuildBr(ctx->builder, end_bb);
    }

    LLVMPositionBuilderAtEnd(ctx->builder, end_bb);
#if CG_DEBUG
    {
        LLVMValueRef rdisc_ptr = LLVMBuildStructGEP2(ctx->builder, enum_llvm, tmp, 0, "ec.rdp");
        LLVMValueRef rdisc = LLVMBuildLoad2(ctx->builder, i8, rdisc_ptr, "ec.rd");
        LLVMValueRef rdi = LLVMBuildZExt(ctx->builder, rdisc,
            LLVMInt32TypeInContext(ctx->context), "ec.rdi");
        cg_emit_debug_printf(ctx, "[cg] ec.clone  result disc=%d\n", &rdi, 1);
    }
#endif
    LLVMValueRef result = LLVMBuildLoad2(ctx->builder, enum_llvm, tmp, "ec.r");
    LLVMBuildRet(ctx->builder, result);

    /* Restore builder position. */
    if (saved_bb)
        LLVMPositionBuilderAtEnd(ctx->builder, saved_bb);
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
            else if (pt)
            {
                /* M-3: 统一所有权转移（string/vec/map/struct/enum/POD）。
                   cg_store_owned 根据类型和 source 节点自动选择
                   move/clone/store 语义，内部处理 borrowed 深克隆。
                   enum_temp_mark 记录了本次 codegen_expr(args[i]) 前的
                   temp count，用于 rvalue string 的 pop-temp 操作。 */
                cg_store_owned(ctx, field_ptr, v, pt,
                               args[i], enum_temp_mark,
                               CG_XFER_INTO_CONTAINER);
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
    /* G1: skip generic struct templates — they have no resolved_type;
       concrete instantiations are emitted on demand via type_to_llvm. */
    if (node->as.struct_decl.type_param_count > 0) return;

    int n = node->as.struct_decl.field_count;
    Type *ml_type = node->resolved_type;

    /* B-2: use LLVM-prefixed name for module-defined structs */
    const char *llvm_name = struct_llvm_name(ml_type);

    /* Skip if already registered (e.g. type_to_llvm was called lazily first) */
    if (find_struct_llvm(ctx, llvm_name)) return;

    /* Build LLVM struct type */
    LLVMTypeRef *fields = NULL;
    if (n > 0)
    {
        fields = (LLVMTypeRef *)malloc_safe((size_t)n * sizeof(LLVMTypeRef));
        for (int i = 0; i < n; i++)
        {
            fields[i] = type_to_llvm(ctx, ml_type->as.strukt.fields[i].type);
        }
    }

    LLVMTypeRef struct_type = LLVMStructCreateNamed(ctx->context, llvm_name);
    LLVMStructSetBody(struct_type, fields, (unsigned)n, 0);
    free(fields);

    register_struct_llvm(ctx, llvm_name, struct_type, ml_type);
    /* Auto-drop generation is deferred to after all impl blocks are processed (Pass 2.5)
       to avoid generating auto-drop before user-defined __drop is known. */
}

static void codegen_impl_decl(CodegenContext *ctx, AstNode *node)
{
    /* G1.5: skip generic impl templates — methods are emitted per-instantiation */
    if (node->as.impl_decl.type_param_count > 0) return;

    const char *bare_name = node->as.impl_decl.name;
    /* Phase 2.5: `impl <builtin type>` (e.g. string). Builtin types are global,
       not owned by any module — their methods use the bare name `string.split`
       so callers in any importing file resolve the same symbol (§2.4). Skip the
       B-3 module prefixing applied to struct/enum impls. */
    bool is_builtin_impl =
        strcmp(bare_name, "string") == 0 || strcmp(bare_name, "int") == 0 ||
        strcmp(bare_name, "i64") == 0    || strcmp(bare_name, "f64") == 0 ||
        strcmp(bare_name, "bool") == 0   || strcmp(bare_name, "char") == 0;
    /* B-3: when emitting inside a module, prefix the struct/enum LLVM name so
       that qualified method names become "<mod>__Struct.method" rather than
       "Struct.method" (consistent with codegen_struct_decl's B-2 prefixing). */
    char prefixed_name_buf[512];
    const char *struct_name = bare_name;
    if (!is_builtin_impl &&
        ctx->current_emit_module != NULL && ctx->current_emit_module[0] != '\0')
    {
        cg_module_fn_symbol(prefixed_name_buf, sizeof(prefixed_name_buf),
                            ctx->current_emit_module, bare_name);
        struct_name = prefixed_name_buf;
    }
    bool is_enum_impl = (find_enum_llvm(ctx, struct_name) != NULL);

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

            if (strcmp(orig_name, "__drop") == 0 && !is_enum_impl)
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
                    LLVMTypeRef ptr_struct = LLVMPointerTypeInContext(ctx->context, 0);
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

                    LLVMTypeRef llvm_struct = type_to_llvm(ctx, st);

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

/* Codegen for `impl Trait for Struct { ... }` — same pattern as codegen_impl_decl
   but reads from impl_trait_decl AST fields. Trait methods are emitted as
   StructName.method_name, exactly like regular impl methods. */
static void codegen_impl_trait_decl(CodegenContext *ctx, AstNode *node)
{
    const char *bare_name = node->as.impl_trait_decl.struct_name;
    /* Phase 2.5 / M-H: `impl Trait for <builtin>` (e.g. `impl Hash for int`).
       Builtin types are global, not owned by any module — their methods use the
       bare name `int.hash` so callers in any importing file resolve the same
       symbol (mirrors codegen_impl_decl's is_builtin_impl). Skip B-3 prefixing. */
    bool is_builtin_impl =
        strcmp(bare_name, "string") == 0 || strcmp(bare_name, "int") == 0 ||
        strcmp(bare_name, "i64") == 0    || strcmp(bare_name, "f64") == 0 ||
        strcmp(bare_name, "bool") == 0   || strcmp(bare_name, "char") == 0;
    /* B-3: prefix trait impl method names for module-defined types */
    char prefixed_name_buf[512];
    const char *struct_name = bare_name;
    if (!is_builtin_impl &&
        ctx->current_emit_module != NULL && ctx->current_emit_module[0] != '\0')
    {
        cg_module_fn_symbol(prefixed_name_buf, sizeof(prefixed_name_buf),
                            ctx->current_emit_module, bare_name);
        struct_name = prefixed_name_buf;
    }

    for (int i = 0; i < node->as.impl_trait_decl.method_count; i++)
    {
        AstNode *method = node->as.impl_trait_decl.methods[i];
        if (method->kind != AST_FN_DECL) continue;

        const char *orig_name = method->as.fn_decl.name;

        /* Qualify with struct name: "Point.to_string" */
        static char qualified_name[256];
        snprintf(qualified_name, sizeof(qualified_name), "%s.%s", struct_name, orig_name);
        method->as.fn_decl.name = qualified_name;

        codegen_fn_decl(ctx, method);

        /* Restore original name */
        method->as.fn_decl.name = (char *)orig_name;
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
    case AST_TYPE_ALIAS_DECL:
    case AST_TRAIT_DECL:
        break; /* no codegen needed */
    case AST_IMPL_TRAIT_DECL:
        codegen_impl_trait_decl(ctx, node);
        break;
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

    /* __ls_str_lines — runtime helper for string.lines() */
    /* void __ls_str_lines(ptr src, i32 src_len, ptr out_data, ptr out_len, ptr out_cap) */
    {
        LLVMTypeRef ln_params[] = {ptr_type, i32_type, ptr_type, ptr_type, ptr_type};
        LLVMTypeRef ln_type = LLVMFunctionType(LLVMVoidTypeInContext(ctx->context),
                                               ln_params, 5, 0);
        if (!LLVMGetNamedFunction(ctx->module, "__ls_str_lines"))
            LLVMAddFunction(ctx->module, "__ls_str_lines", ln_type);
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

/* Emit __ls_str_split(src_data, src_len, sep_data, sep_len,
                       out_data_ptr, out_len_ptr, out_cap_ptr) -> void
   Splits src by sep, returns array of heap-owned LsString elements via out params. */
static void emit_str_split_helper(CodegenContext *ctx)
{
    if (ctx->extern_builtins && !ctx->memcheck_enabled)
        return;

    LLVMValueRef fn = LLVMGetNamedFunction(ctx->module, "__ls_str_split");
    if (fn == NULL || LLVMCountBasicBlocks(fn) > 0)
        return;
    if (ctx->extern_builtins && ctx->memcheck_enabled)
        LLVMSetLinkage(fn, LLVMInternalLinkage);

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
    LLVMValueRef count_ptr = cg_entry_alloca(ctx, i32_t, "count");
    LLVMValueRef p_ptr = cg_entry_alloca(ctx, ptr_t, "p");
    LLVMValueRef i_ptr = cg_entry_alloca(ctx, i32_t, "spl.i");
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
    if (ctx->extern_builtins && !ctx->memcheck_enabled)
        return;

    LLVMValueRef fn = LLVMGetNamedFunction(ctx->module, "__ls_str_join");
    if (fn == NULL || LLVMCountBasicBlocks(fn) > 0)
        return;
    if (ctx->extern_builtins && ctx->memcheck_enabled)
        LLVMSetLinkage(fn, LLVMInternalLinkage);

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
    LLVMValueRef total_ptr = cg_entry_alloca(ctx, i32_t, "total");
    LLVMValueRef i_ptr = cg_entry_alloca(ctx, i32_t, "i");
    LLVMValueRef dst_ptr = cg_entry_alloca(ctx, ptr_t, "dst");
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

/* Emit __ls_str_lines(src_data, src_len, out_data_pp, out_len_p, out_cap_p) -> void
   Splits src by '\n' (stripping '\r' before '\n' for CRLF), pushes LsString elements
   into an output vec.  Trailing newline does NOT produce an empty trailing element. */
static void emit_str_lines_helper(CodegenContext *ctx)
{
    if (ctx->extern_builtins && !ctx->memcheck_enabled)
        return;

    LLVMValueRef fn = LLVMGetNamedFunction(ctx->module, "__ls_str_lines");
    if (fn == NULL || LLVMCountBasicBlocks(fn) > 0)
        return;
    if (ctx->extern_builtins && ctx->memcheck_enabled)
        LLVMSetLinkage(fn, LLVMInternalLinkage);

    LLVMTypeRef i8_t  = LLVMInt8TypeInContext(ctx->context);
    LLVMTypeRef i32_t = LLVMInt32TypeInContext(ctx->context);
    LLVMTypeRef i64_t = LLVMInt64TypeInContext(ctx->context);
    LLVMTypeRef ptr_t = LLVMPointerTypeInContext(ctx->context, 0);
    LLVMTypeRef str_t = ls_string_type(ctx);

    LLVMBasicBlockRef saved_bb = LLVMGetInsertBlock(ctx->builder);

    LLVMValueRef src_data    = LLVMGetParam(fn, 0);
    LLVMValueRef src_len     = LLVMGetParam(fn, 1);
    LLVMValueRef out_data_pp = LLVMGetParam(fn, 2);
    LLVMValueRef out_len_p   = LLVMGetParam(fn, 3);
    LLVMValueRef out_cap_p   = LLVMGetParam(fn, 4);

    LLVMValueRef zero32  = LLVMConstInt(i32_t, 0, 0);
    LLVMValueRef one32   = LLVMConstInt(i32_t, 1, 0);
    LLVMValueRef zero64  = LLVMConstInt(i64_t, 0, 0);
    LLVMValueRef nl_ch   = LLVMConstInt(i8_t, '\n', 0);
    LLVMValueRef cr_ch   = LLVMConstInt(i8_t, '\r', 0);
    LLVMValueRef min_cap = LLVMConstInt(i32_t, LS_MIN_STR_CAP, 0);

    LLVMValueRef str_size = LLVMSizeOf(str_t);

    LLVMValueRef malloc_fn = LLVMGetNamedFunction(ctx->module, "malloc");
    LLVMTypeRef  malloc_t  = LLVMGlobalGetValueType(malloc_fn);
    LLVMValueRef memcpy_fn = LLVMGetNamedFunction(ctx->module, "memcpy");
    LLVMTypeRef  memcpy_t  = LLVMGlobalGetValueType(memcpy_fn);

    /* Basic blocks */
    LLVMBasicBlockRef entry_bb      = LLVMAppendBasicBlockInContext(ctx->context, fn, "ln.entry");
    LLVMBasicBlockRef empty_ret_bb  = LLVMAppendBasicBlockInContext(ctx->context, fn, "ln.empty");
    LLVMBasicBlockRef cnt_cond_bb   = LLVMAppendBasicBlockInContext(ctx->context, fn, "ln.cnt.cond");
    LLVMBasicBlockRef cnt_body_bb   = LLVMAppendBasicBlockInContext(ctx->context, fn, "ln.cnt.body");
    LLVMBasicBlockRef cnt_end_bb    = LLVMAppendBasicBlockInContext(ctx->context, fn, "ln.cnt.end");
    LLVMBasicBlockRef zero_ret_bb   = LLVMAppendBasicBlockInContext(ctx->context, fn, "ln.zero");
    LLVMBasicBlockRef fill_cond_bb  = LLVMAppendBasicBlockInContext(ctx->context, fn, "ln.fill.cond");
    LLVMBasicBlockRef fill_body_bb  = LLVMAppendBasicBlockInContext(ctx->context, fn, "ln.fill.body");
    LLVMBasicBlockRef fill_nl_bb    = LLVMAppendBasicBlockInContext(ctx->context, fn, "ln.fill.nl");
    LLVMBasicBlockRef fill_adv_bb   = LLVMAppendBasicBlockInContext(ctx->context, fn, "ln.fill.adv");
    LLVMBasicBlockRef fill_end_bb   = LLVMAppendBasicBlockInContext(ctx->context, fn, "ln.fill.end");
    LLVMBasicBlockRef tail_bb       = LLVMAppendBasicBlockInContext(ctx->context, fn, "ln.tail");
    LLVMBasicBlockRef done_bb       = LLVMAppendBasicBlockInContext(ctx->context, fn, "ln.done");

    /* entry: allocas, handle empty string */
    LLVMPositionBuilderAtEnd(ctx->builder, entry_bb);
    LLVMValueRef cnt_ptr   = cg_entry_alloca(ctx, i32_t, "ln.cnt");
    LLVMValueRef i_ptr     = cg_entry_alloca(ctx, i32_t, "ln.i");
    LLVMValueRef start_ptr = cg_entry_alloca(ctx, i32_t, "ln.start");
    LLVMValueRef eidx_ptr  = cg_entry_alloca(ctx, i32_t, "ln.eidx");
    LLVMValueRef arr_ptr   = cg_entry_alloca(ctx, ptr_t,  "ln.arr");
    LLVMBuildStore(ctx->builder, zero32, cnt_ptr);
    LLVMBuildStore(ctx->builder, zero32, i_ptr);
    LLVMValueRef is_empty = LLVMBuildICmp(ctx->builder, LLVMIntEQ, src_len, zero32, "ln.isempty");
    LLVMBuildCondBr(ctx->builder, is_empty, empty_ret_bb, cnt_cond_bb);

    /* empty_ret: return empty vec */
    LLVMPositionBuilderAtEnd(ctx->builder, empty_ret_bb);
    LLVMBuildStore(ctx->builder, LLVMConstNull(ptr_t), out_data_pp);
    LLVMBuildStore(ctx->builder, zero32, out_len_p);
    LLVMBuildStore(ctx->builder, zero32, out_cap_p);
    LLVMBuildRetVoid(ctx->builder);

    /* cnt_cond: while i < src_len */
    LLVMPositionBuilderAtEnd(ctx->builder, cnt_cond_bb);
    LLVMValueRef ci = LLVMBuildLoad2(ctx->builder, i32_t, i_ptr, "ln.ci");
    LLVMValueRef cnt_done = LLVMBuildICmp(ctx->builder, LLVMIntSGE, ci, src_len, "ln.cdone");
    LLVMBuildCondBr(ctx->builder, cnt_done, cnt_end_bb, cnt_body_bb);

    /* cnt_body: if data[i] == '\n' → count++ */
    LLVMPositionBuilderAtEnd(ctx->builder, cnt_body_bb);
    LLVMValueRef ci64c = LLVMBuildZExt(ctx->builder, ci, i64_t, "ln.ci64c");
    LLVMValueRef chp_c = LLVMBuildGEP2(ctx->builder, i8_t, src_data, &ci64c, 1, "ln.chp");
    LLVMValueRef ch_c  = LLVMBuildLoad2(ctx->builder, i8_t, chp_c, "ln.ch");
    LLVMValueRef is_nl = LLVMBuildICmp(ctx->builder, LLVMIntEQ, ch_c, nl_ch, "ln.isnl");
    LLVMValueRef cnt_v = LLVMBuildLoad2(ctx->builder, i32_t, cnt_ptr, "ln.cntv");
    LLVMValueRef cnt_inc = LLVMBuildAdd(ctx->builder, cnt_v, one32, "ln.cntinc");
    LLVMValueRef new_cnt = LLVMBuildSelect(ctx->builder, is_nl, cnt_inc, cnt_v, "ln.newcnt");
    LLVMBuildStore(ctx->builder, new_cnt, cnt_ptr);
    LLVMBuildStore(ctx->builder,
                   LLVMBuildAdd(ctx->builder, ci, one32, "ln.cinext"), i_ptr);
    LLVMBuildBr(ctx->builder, cnt_cond_bb);

    /* cnt_end: determine n_lines = count + (last char != '\n' ? 1 : 0) */
    LLVMPositionBuilderAtEnd(ctx->builder, cnt_end_bb);
    LLVMValueRef final_cnt = LLVMBuildLoad2(ctx->builder, i32_t, cnt_ptr, "ln.fcnt");
    LLVMValueRef last_idx  = LLVMBuildSub(ctx->builder, src_len, one32, "ln.lastidx");
    LLVMValueRef last_idx64 = LLVMBuildZExt(ctx->builder, last_idx, i64_t, "ln.li64");
    LLVMValueRef last_chp  = LLVMBuildGEP2(ctx->builder, i8_t, src_data, &last_idx64, 1, "ln.lchp");
    LLVMValueRef last_ch   = LLVMBuildLoad2(ctx->builder, i8_t, last_chp, "ln.lch");
    LLVMValueRef last_isnl = LLVMBuildICmp(ctx->builder, LLVMIntEQ, last_ch, nl_ch, "ln.lisnl");
    LLVMValueRef extra     = LLVMBuildSelect(ctx->builder, last_isnl, zero32, one32, "ln.extra");
    LLVMValueRef n_lines   = LLVMBuildAdd(ctx->builder, final_cnt, extra, "ln.nlines");
    LLVMValueRef is_zero   = LLVMBuildICmp(ctx->builder, LLVMIntEQ, n_lines, zero32, "ln.iszero");
    LLVMBuildCondBr(ctx->builder, is_zero, zero_ret_bb, fill_cond_bb);

    /* zero_ret: somehow 0 lines (shouldn't happen for non-empty src, but be safe) */
    LLVMPositionBuilderAtEnd(ctx->builder, zero_ret_bb);
    LLVMBuildStore(ctx->builder, LLVMConstNull(ptr_t), out_data_pp);
    LLVMBuildStore(ctx->builder, zero32, out_len_p);
    LLVMBuildStore(ctx->builder, zero32, out_cap_p);
    LLVMBuildRetVoid(ctx->builder);

    /* Before fill loop: allocate element array, reset state */
    /* We need to continue from cnt_end into allocation, but we branched to fill_cond.
       Insert allocation between cnt_end and fill_cond using a new bb. */
    /* Restructure: cnt_end → alloc_bb → fill_init → fill_cond */
    LLVMBasicBlockRef alloc_bb      = LLVMAppendBasicBlockInContext(ctx->context, fn, "ln.alloc");
    LLVMBasicBlockRef fill_init_bb  = LLVMAppendBasicBlockInContext(ctx->context, fn, "ln.fill.init");

    /* Patch cnt_end terminator: replace CondBr(is_zero, zero_ret, fill_cond)
       with CondBr(is_zero, zero_ret, alloc_bb) */
    {
        LLVMValueRef old_term = LLVMGetBasicBlockTerminator(cnt_end_bb);
        LLVMInstructionEraseFromParent(old_term);
        LLVMPositionBuilderAtEnd(ctx->builder, cnt_end_bb);
        LLVMBuildCondBr(ctx->builder, is_zero, zero_ret_bb, alloc_bb);
    }

    /* alloc_bb: arr = malloc(n_lines * sizeof(LsString)) */
    LLVMPositionBuilderAtEnd(ctx->builder, alloc_bb);
    LLVMValueRef n64   = LLVMBuildZExt(ctx->builder, n_lines, i64_t, "ln.n64");
    LLVMValueRef arrsz = LLVMBuildMul(ctx->builder, n64, str_size, "ln.arrsz");
    LLVMValueRef arr   = LLVMBuildCall2(ctx->builder, malloc_t, malloc_fn,
                                        &arrsz, 1, "ln.arr");
    LLVMBuildStore(ctx->builder, arr, arr_ptr);
    LLVMBuildBr(ctx->builder, fill_init_bb);

    /* fill_init: eidx = 0, start = 0, i = 0 */
    LLVMPositionBuilderAtEnd(ctx->builder, fill_init_bb);
    LLVMBuildStore(ctx->builder, zero32, eidx_ptr);
    LLVMBuildStore(ctx->builder, zero32, start_ptr);
    LLVMBuildStore(ctx->builder, zero32, i_ptr);
    LLVMBuildBr(ctx->builder, fill_cond_bb);

    /* fill_cond: while i < src_len */
    LLVMPositionBuilderAtEnd(ctx->builder, fill_cond_bb);
    LLVMValueRef fi = LLVMBuildLoad2(ctx->builder, i32_t, i_ptr, "ln.fi");
    LLVMValueRef fill_done = LLVMBuildICmp(ctx->builder, LLVMIntSGE, fi, src_len, "ln.fdone");
    LLVMBuildCondBr(ctx->builder, fill_done, fill_end_bb, fill_body_bb);

    /* fill_body: load data[i], check if '\n' */
    LLVMPositionBuilderAtEnd(ctx->builder, fill_body_bb);
    LLVMValueRef fi64 = LLVMBuildZExt(ctx->builder, fi, i64_t, "ln.fi64");
    LLVMValueRef fchp = LLVMBuildGEP2(ctx->builder, i8_t, src_data, &fi64, 1, "ln.fchp");
    LLVMValueRef fch  = LLVMBuildLoad2(ctx->builder, i8_t, fchp, "ln.fch");
    LLVMValueRef fis_nl = LLVMBuildICmp(ctx->builder, LLVMIntEQ, fch, nl_ch, "ln.fisnl");
    LLVMValueRef fi_next = LLVMBuildAdd(ctx->builder, fi, one32, "ln.finext");
    LLVMBuildStore(ctx->builder, fi_next, i_ptr);
    LLVMBuildCondBr(ctx->builder, fis_nl, fill_nl_bb, fill_adv_bb);

    /* fill_adv: not a newline, just advance */
    LLVMPositionBuilderAtEnd(ctx->builder, fill_adv_bb);
    LLVMBuildBr(ctx->builder, fill_cond_bb);

    /* fill_nl: found '\n'. Determine line_end = i (or i-1 if CR). Store element. */
    LLVMPositionBuilderAtEnd(ctx->builder, fill_nl_bb);
    LLVMValueRef fstart = LLVMBuildLoad2(ctx->builder, i32_t, start_ptr, "ln.fstart");
    /* line_end = fi (before the '\n') */
    LLVMValueRef line_end = fi;  /* the '\n' position */
    /* Check if i > start and data[i-1] == '\r' */
    LLVMValueRef fi_gt_start = LLVMBuildICmp(ctx->builder, LLVMIntSGT, fi, fstart, "ln.fgts");
    LLVMBasicBlockRef cr_check_bb = LLVMAppendBasicBlockInContext(ctx->context, fn, "ln.cr.check");
    LLVMBasicBlockRef emit_elem_bb = LLVMAppendBasicBlockInContext(ctx->context, fn, "ln.emit");
    LLVMBuildCondBr(ctx->builder, fi_gt_start, cr_check_bb, emit_elem_bb);

    /* cr_check: if data[fi-1] == '\r', line_end = fi-1 */
    LLVMPositionBuilderAtEnd(ctx->builder, cr_check_bb);
    LLVMValueRef fi_minus1 = LLVMBuildSub(ctx->builder, fi, one32, "ln.fim1");
    LLVMValueRef fi_m1_64  = LLVMBuildZExt(ctx->builder, fi_minus1, i64_t, "ln.fm164");
    LLVMValueRef prev_chp  = LLVMBuildGEP2(ctx->builder, i8_t, src_data, &fi_m1_64, 1, "ln.prevchp");
    LLVMValueRef prev_ch   = LLVMBuildLoad2(ctx->builder, i8_t, prev_chp, "ln.prevch");
    LLVMValueRef is_cr     = LLVMBuildICmp(ctx->builder, LLVMIntEQ, prev_ch, cr_ch, "ln.iscr");
    LLVMValueRef adj_end   = LLVMBuildSelect(ctx->builder, is_cr, fi_minus1, fi, "ln.adjend");
    LLVMBuildBr(ctx->builder, emit_elem_bb);

    /* emit_elem: phi for line_end, compute line_len, alloc+copy, store in arr */
    LLVMPositionBuilderAtEnd(ctx->builder, emit_elem_bb);
    LLVMValueRef line_end_phi = LLVMBuildPhi(ctx->builder, i32_t, "ln.lend");
    LLVMValueRef phi_vals2[2] = { line_end, adj_end };
    LLVMBasicBlockRef phi_bbs2[2] = { fill_nl_bb, cr_check_bb };
    LLVMAddIncoming(line_end_phi, phi_vals2, phi_bbs2, 2);

    LLVMValueRef line_len  = LLVMBuildSub(ctx->builder, line_end_phi, fstart, "ln.llen");
    LLVMValueRef need_cap  = LLVMBuildAdd(ctx->builder, line_len, one32, "ln.need");
    LLVMValueRef cap_gt    = LLVMBuildICmp(ctx->builder, LLVMIntUGT, need_cap, min_cap, "ln.cgt");
    LLVMValueRef elem_cap  = LLVMBuildSelect(ctx->builder, cap_gt, need_cap, min_cap, "ln.ecap");
    LLVMValueRef elem_cap64 = LLVMBuildZExt(ctx->builder, elem_cap, i64_t, "ln.ec64");
    LLVMValueRef elem_buf  = LLVMBuildCall2(ctx->builder, malloc_t, malloc_fn,
                                            &elem_cap64, 1, "ln.ebuf");
    /* memcpy(elem_buf, src_data+start, line_len) */
    LLVMValueRef fstart64  = LLVMBuildZExt(ctx->builder, fstart, i64_t, "ln.fs64");
    LLVMValueRef src_start = LLVMBuildGEP2(ctx->builder, i8_t, src_data, &fstart64, 1, "ln.srcst");
    LLVMValueRef ll64      = LLVMBuildZExt(ctx->builder, line_len, i64_t, "ln.ll64");
    LLVMValueRef mc_args[3] = { elem_buf, src_start, ll64 };
    LLVMBuildCall2(ctx->builder, memcpy_t, memcpy_fn, mc_args, 3, "");
    /* NUL-terminate */
    LLVMValueRef nul_off = LLVMBuildZExt(ctx->builder, line_len, i64_t, "ln.noff");
    LLVMValueRef nul_ptr = LLVMBuildGEP2(ctx->builder, i8_t, elem_buf, &nul_off, 1, "ln.nulp");
    LLVMBuildStore(ctx->builder, LLVMConstInt(i8_t, 0, 0), nul_ptr);
    /* Store into arr[eidx] */
    LLVMValueRef eidx = LLVMBuildLoad2(ctx->builder, i32_t, eidx_ptr, "ln.eidx");
    LLVMValueRef eidx64 = LLVMBuildZExt(ctx->builder, eidx, i64_t, "ln.eidx64");
    LLVMValueRef arr_cur = LLVMBuildLoad2(ctx->builder, ptr_t, arr_ptr, "ln.arrcur");
    LLVMValueRef ep     = LLVMBuildGEP2(ctx->builder, str_t, arr_cur, &eidx64, 1, "ln.ep");
    LLVMValueRef ep_d   = LLVMBuildStructGEP2(ctx->builder, str_t, ep, 0, "ln.epd");
    LLVMValueRef ep_l   = LLVMBuildStructGEP2(ctx->builder, str_t, ep, 1, "ln.epl");
    LLVMValueRef ep_c   = LLVMBuildStructGEP2(ctx->builder, str_t, ep, 2, "ln.epc");
    LLVMBuildStore(ctx->builder, elem_buf, ep_d);
    LLVMBuildStore(ctx->builder, line_len, ep_l);
    LLVMBuildStore(ctx->builder, elem_cap, ep_c);
    LLVMBuildStore(ctx->builder,
                   LLVMBuildAdd(ctx->builder, eidx, one32, "ln.eidxnext"), eidx_ptr);
    /* start = fi+1 (already stored in i_ptr as fi_next) */
    LLVMBuildStore(ctx->builder, fi_next, start_ptr);
    LLVMBuildBr(ctx->builder, fill_cond_bb);

    /* fill_end: handle last segment (if start < src_len) */
    LLVMPositionBuilderAtEnd(ctx->builder, fill_end_bb);
    LLVMValueRef end_start = LLVMBuildLoad2(ctx->builder, i32_t, start_ptr, "ln.endst");
    LLVMValueRef has_tail  = LLVMBuildICmp(ctx->builder, LLVMIntSLT, end_start, src_len, "ln.htail");
    LLVMBuildCondBr(ctx->builder, has_tail, tail_bb, done_bb);

    /* tail_bb: emit last segment (no trailing newline) */
    LLVMPositionBuilderAtEnd(ctx->builder, tail_bb);
    LLVMValueRef tail_start = end_start;
    /* Strip trailing '\r' if present */
    LLVMValueRef ts_last_idx = LLVMBuildSub(ctx->builder, src_len, one32, "ln.tsli");
    LLVMValueRef ts_li64     = LLVMBuildZExt(ctx->builder, ts_last_idx, i64_t, "ln.tsli64");
    LLVMValueRef ts_lchp     = LLVMBuildGEP2(ctx->builder, i8_t, src_data, &ts_li64, 1, "ln.tslchp");
    LLVMValueRef ts_lch      = LLVMBuildLoad2(ctx->builder, i8_t, ts_lchp, "ln.tslch");
    LLVMValueRef ts_iscr     = LLVMBuildICmp(ctx->builder, LLVMIntEQ, ts_lch, cr_ch, "ln.tsiscr");
    LLVMValueRef tail_end    = LLVMBuildSelect(ctx->builder, ts_iscr, ts_last_idx, src_len, "ln.tend");
    LLVMValueRef tail_len    = LLVMBuildSub(ctx->builder, tail_end, tail_start, "ln.tlen");
    /* Alloc + copy */
    LLVMValueRef tn_cap   = LLVMBuildAdd(ctx->builder, tail_len, one32, "ln.tncap");
    LLVMValueRef tn_cgt   = LLVMBuildICmp(ctx->builder, LLVMIntUGT, tn_cap, min_cap, "ln.tncgt");
    LLVMValueRef t_cap    = LLVMBuildSelect(ctx->builder, tn_cgt, tn_cap, min_cap, "ln.tcap");
    LLVMValueRef t_cap64  = LLVMBuildZExt(ctx->builder, t_cap, i64_t, "ln.tc64");
    LLVMValueRef t_buf    = LLVMBuildCall2(ctx->builder, malloc_t, malloc_fn,
                                           &t_cap64, 1, "ln.tbuf");
    LLVMValueRef ts64     = LLVMBuildZExt(ctx->builder, tail_start, i64_t, "ln.ts64");
    LLVMValueRef t_src    = LLVMBuildGEP2(ctx->builder, i8_t, src_data, &ts64, 1, "ln.tsrc");
    LLVMValueRef tl64     = LLVMBuildZExt(ctx->builder, tail_len, i64_t, "ln.tl64");
    LLVMValueRef tmc[3]   = { t_buf, t_src, tl64 };
    LLVMBuildCall2(ctx->builder, memcpy_t, memcpy_fn, tmc, 3, "");
    LLVMValueRef t_noff   = LLVMBuildZExt(ctx->builder, tail_len, i64_t, "ln.tnoff");
    LLVMValueRef t_nulp   = LLVMBuildGEP2(ctx->builder, i8_t, t_buf, &t_noff, 1, "ln.tnulp");
    LLVMBuildStore(ctx->builder, LLVMConstInt(i8_t, 0, 0), t_nulp);
    /* arr[eidx] */
    LLVMValueRef t_eidx   = LLVMBuildLoad2(ctx->builder, i32_t, eidx_ptr, "ln.teidx");
    LLVMValueRef t_eidx64 = LLVMBuildZExt(ctx->builder, t_eidx, i64_t, "ln.teidx64");
    LLVMValueRef t_arr    = LLVMBuildLoad2(ctx->builder, ptr_t, arr_ptr, "ln.tarr");
    LLVMValueRef t_ep     = LLVMBuildGEP2(ctx->builder, str_t, t_arr, &t_eidx64, 1, "ln.tep");
    LLVMValueRef t_ep_d   = LLVMBuildStructGEP2(ctx->builder, str_t, t_ep, 0, "ln.tepd");
    LLVMValueRef t_ep_l   = LLVMBuildStructGEP2(ctx->builder, str_t, t_ep, 1, "ln.tepl");
    LLVMValueRef t_ep_c   = LLVMBuildStructGEP2(ctx->builder, str_t, t_ep, 2, "ln.tepc");
    LLVMBuildStore(ctx->builder, t_buf, t_ep_d);
    LLVMBuildStore(ctx->builder, tail_len, t_ep_l);
    LLVMBuildStore(ctx->builder, t_cap, t_ep_c);
    /* Increment eidx so done_bb gets the correct total count */
    LLVMBuildStore(ctx->builder,
                   LLVMBuildAdd(ctx->builder, t_eidx, one32, "ln.teidxnext"), eidx_ptr);
    LLVMBuildBr(ctx->builder, done_bb);

    /* done: store out params */
    LLVMPositionBuilderAtEnd(ctx->builder, done_bb);
    LLVMValueRef final_arr = LLVMBuildLoad2(ctx->builder, ptr_t, arr_ptr, "ln.farr");
    LLVMValueRef final_nl  = n_lines; /* computed in cnt_end, available here via alloc_bb */
    (void)zero64;
    /* n_lines is not a phi — it's computed in cnt_end_bb and flows to alloc_bb → fill_init_bb.
       We need it available in done_bb.  Store it into a stack slot from the entry block. */
    LLVMBuildStore(ctx->builder, final_arr, out_data_pp);
    /* Recompute n_lines from eidx (which is the actual filled count) */
    LLVMValueRef actual_eidx = LLVMBuildLoad2(ctx->builder, i32_t, eidx_ptr, "ln.aeidx");
    /* If we fell through tail_bb, eidx was incremented there by 1 implicitly — but we
       only store n_lines as filled count.  Use actual_eidx which may be 0..n_lines-1
       at done_bb from fill_end path, or n_lines from tail_bb.  Just use n_lines. */
    LLVMBuildStore(ctx->builder, actual_eidx, out_len_p);
    LLVMBuildStore(ctx->builder, actual_eidx, out_cap_p);
    LLVMBuildRetVoid(ctx->builder);

    if (saved_bb)
        LLVMPositionBuilderAtEnd(ctx->builder, saved_bb);
    (void)min_cap; (void)zero64;
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

    /* M-4.5: release temp has_drop slot arrays */
    free(ctx->temp_drop_slots);
    free(ctx->temp_drop_types);
    free(ctx->temp_drop_marks);
    ctx->temp_drop_slots = NULL;
    ctx->temp_drop_types = NULL;
    ctx->temp_drop_marks = NULL;
    ctx->temp_drop_count = 0;
    ctx->temp_drop_cap = 0;

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
    emit_str_lines_helper(ctx);

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
            int saved_temp_count = ctx->temp_string_count;
            ctx->temp_string_count = 0;

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
            ctx->temp_string_count = saved_temp_count;
        }
    }

    /* Generate __ls_global_cleanup for globals that own heap data.
       Called just before main() returns so global values don't leak at program exit. */
    {
        /* Collect all global VAR_DECLs that need cleanup. */
        bool has_global_cleanup = false;

#define DECL_NEEDS_GLOBAL_CLEANUP(d)                                      \
    ((d)->kind == AST_VAR_DECL && (d)->resolved_type &&                   \
     ((d)->resolved_type->kind == TYPE_STRING ||                          \
      (d)->resolved_type->kind == TYPE_STRUCT ||                          \
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
            int saved_tc2 = ctx->temp_string_count;
            ctx->temp_string_count = 0;
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
        if ((decl)->resolved_type->kind == TYPE_STRING)                                                  \
        {                                                                                                \
            /* Simple cleanup: free if cap > 0 */                                                        \
            LLVMTypeRef str_type = ls_string_type(ctx);                                                  \
            LLVMValueRef str_val = LLVMBuildLoad2(ctx->builder, str_type, gv, "gcs.str");                \
            LLVMValueRef cap = LLVMBuildExtractValue(ctx->builder, str_val, 2, "gcs.cap");               \
            LLVMValueRef zero = LLVMConstInt(LLVMInt32TypeInContext(ctx->context), 0, 0);                \
            LLVMValueRef is_owned = LLVMBuildICmp(ctx->builder, LLVMIntSGT, cap, zero, "gcs.owned");     \
            LLVMValueRef cur_fn2 = LLVMGetBasicBlockParent(LLVMGetInsertBlock(ctx->builder));            \
            LLVMBasicBlockRef free_bb = LLVMAppendBasicBlockInContext(ctx->context, cur_fn2, "gcs.free");\
            LLVMBasicBlockRef skip_bb = LLVMAppendBasicBlockInContext(ctx->context, cur_fn2, "gcs.skip");\
            LLVMBuildCondBr(ctx->builder, is_owned, free_bb, skip_bb);                                   \
            LLVMPositionBuilderAtEnd(ctx->builder, free_bb);                                             \
            LLVMValueRef data = LLVMBuildExtractValue(ctx->builder, str_val, 0, "gcs.data");             \
            cg_emit_free(ctx, data, "string.scope_drop", CG_LINE(ctx), CG_COL(ctx));                      \
            LLVMBuildBr(ctx->builder, skip_bb);                                                          \
            LLVMPositionBuilderAtEnd(ctx->builder, skip_bb);                                             \
            /* builder now at skip_bb; caller (retVoid or next cleanup) terminates */\
        }                                                                                                \
        else if ((decl)->resolved_type->kind == TYPE_STRUCT)                                             \
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

            /* codegen_fn_decl uses node->as.fn_decl.name as the LLVM function name.
               Temporarily set it to the mangled name (e.g. "Pair(int,string).get_first"). */
            const char *orig_name = cfn->as.fn_decl.name;
            cfn->as.fn_decl.name = (char *)mname;
            codegen_fn_decl(ctx, cfn);
            cfn->as.fn_decl.name = (char *)orig_name;
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
    LLVMPassBuilderOptionsRef pass_opts = LLVMCreatePassBuilderOptions();
    LLVMRunPasses(ctx->module, "default<O2>", ctx->target_machine, pass_opts);
    LLVMDisposePassBuilderOptions(pass_opts);

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
