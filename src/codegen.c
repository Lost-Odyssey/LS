/* codegen.c — AST to LLVM IR code generation */
#include "codegen.h"
#include "module.h"
#include "common.h"

#include <llvm-c/Core.h>
#include <llvm-c/Target.h>
#include <llvm-c/TargetMachine.h>
#include <llvm-c/Analysis.h>
#include <llvm-c/BitWriter.h>
#include <llvm-c/Transforms/PassBuilder.h>

#include <stdio.h>
#include <string.h>

/* **NOTE**: don't remove all debug code controlled by this macro */
/* #define CG_DEBUG */

/* Global counter for unique basic block names */
static int g_block_counter = 0;

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

/* Forward declarations for string cleanup (defined after LsString helpers) */
static LLVMBasicBlockRef emit_string_free_separate(CodegenContext *ctx, LLVMValueRef str_alloca);
static void emit_scope_cleanup(CodegenContext *ctx);
static void emit_cleanup_to(CodegenContext *ctx, CgScope *stop, LLVMValueRef skip_alloca);
static void emit_struct_drop(CodegenContext *ctx, LLVMValueRef drop_ptr, Type *struct_type);
static void emit_struct_drop_cond(CodegenContext *ctx, LLVMValueRef drop_ptr,
                                  Type *struct_type, LLVMValueRef moved_flag);
static LLVMBasicBlockRef emit_struct_drop_separate(CodegenContext *ctx, LLVMValueRef drop_ptr,
                                                   Type *struct_type, LLVMValueRef moved_flag);
static void emit_drop_field_cleanup(CodegenContext *ctx);
static LLVMTypeRef type_to_llvm(CodegenContext *ctx, Type *t);
static LLVMBasicBlockRef emit_vec_elem_drop_at(CodegenContext *ctx, LLVMValueRef elem_ptr,
                                               Type *elem_type, int idx_suffix);

static void pop_scope(CodegenContext *ctx)
{
    CgScope *old = ctx->current_scope;
    ctx->current_scope = old->parent;
    cg_scope_free(old);
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

/* ---- LsString LLVM type: { i8*, i32, i32 } = { data, len, cap } ---- */

/* Get or create the LsString LLVM struct type.
   cap == 0 means static literal (data points to global constant, don't free).
   cap > 0 means heap-allocated (caller must free data). */
static LLVMTypeRef ls_string_type(CodegenContext *ctx)
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
static LLVMValueRef ls_string_make(CodegenContext *ctx, LLVMValueRef data,
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
static LLVMValueRef ls_string_from_literal(CodegenContext *ctx,
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

/* Mark a string as moved by setting cap = -1.
   str_alloca: the alloca pointer to the LsString struct. */
static void mark_string_moved(CodegenContext *ctx, LLVMValueRef str_alloca)
{
    LLVMTypeRef str_type = ls_string_type(ctx);
    LLVMTypeRef i32_type = LLVMInt32TypeInContext(ctx->context);

    /* Load the struct, update cap to -1, store back */
    LLVMValueRef str_val = LLVMBuildLoad2(ctx->builder, str_type, str_alloca, "msm.val");

    /* Extract data and len fields */
    LLVMValueRef data = LLVMBuildExtractValue(ctx->builder, str_val, 0, "msm.data");
    LLVMValueRef len = LLVMBuildExtractValue(ctx->builder, str_val, 1, "msm.len");

    /* Set cap = -1 (MOVED) */
    LLVMValueRef neg1 = LLVMConstInt(i32_type, (unsigned long long)-1, 1);

    /* Rebuild struct with moved cap */
    LLVMValueRef undef = LLVMGetUndef(str_type);
    LLVMValueRef new_str = LLVMBuildInsertValue(ctx->builder, undef, data, 0, "msm.d");
    new_str = LLVMBuildInsertValue(ctx->builder, new_str, len, 1, "msm.l");
    new_str = LLVMBuildInsertValue(ctx->builder, new_str, neg1, 2, "msm.c");

    LLVMBuildStore(ctx->builder, new_str, str_alloca);
}

/* Emit IR to free a dynamic LsString: if cap > 0, call free(data).
   `str_alloca` is the alloca pointer to the LsString struct.
   Handles three states: MOVED(-1), STATIC(0), OWNED(>0) */
/* Emit string cleanup inline in the current block.
   This creates the conditional logic directly, suitable for a single cleanup. */
/* Emit string cleanup inline in the current block.
   This creates the conditional logic directly, suitable for a single cleanup. */
static void emit_string_free(CodegenContext *ctx, LLVMValueRef str_alloca)
{
    LLVMTypeRef str_type = ls_string_type(ctx);
    LLVMTypeRef i32_type = LLVMInt32TypeInContext(ctx->context);

    LLVMBasicBlockRef cur_bb = LLVMGetInsertBlock(ctx->builder);
    LLVMValueRef cur_fn = LLVMGetBasicBlockParent(cur_bb);

    LLVMValueRef str_val = LLVMBuildLoad2(ctx->builder, str_type, str_alloca, "sf.str");
    LLVMValueRef cap = LLVMBuildExtractValue(ctx->builder, str_val, 2, "sf.cap");
    LLVMValueRef zero = LLVMConstInt(i32_type, 0, 0);
    LLVMValueRef is_owned = LLVMBuildICmp(ctx->builder, LLVMIntSGT, cap, zero, "sf.owned");

    int id = g_block_counter++;
    char free_name[32], skip_name[32];
    snprintf(free_name, sizeof(free_name), "sf.free%d", id);
    snprintf(skip_name, sizeof(skip_name), "sf.skip%d", id);
    LLVMBasicBlockRef free_bb = LLVMAppendBasicBlockInContext(ctx->context, cur_fn, free_name);
    LLVMBasicBlockRef skip_bb = LLVMAppendBasicBlockInContext(ctx->context, cur_fn, skip_name);

    /* Jump from current block to the cleanup blocks */
    LLVMBuildCondBr(ctx->builder, is_owned, free_bb, skip_bb);

    /* Free path: call free(data), then return */
    LLVMPositionBuilderAtEnd(ctx->builder, free_bb);
    LLVMValueRef data = LLVMBuildExtractValue(ctx->builder, str_val, 0, "sf.data");
    LLVMValueRef free_fn = LLVMGetNamedFunction(ctx->module, "free");
    LLVMTypeRef free_type = LLVMGlobalGetValueType(free_fn);
    LLVMBuildCall2(ctx->builder, free_type, free_fn, &data, 1, "");
    LLVMBuildRetVoid(ctx->builder);

    /* Skip path: nothing to free, just return */
    LLVMPositionBuilderAtEnd(ctx->builder, skip_bb);
    LLVMBuildRetVoid(ctx->builder);
}

/* Emit string cleanup - creates a complete cleanup block with its own continuation.
   Returns the entry block of this cleanup, which should be branched to.
   The continuation block is returned separately. */
static void emit_string_free_with_cont(CodegenContext *ctx, LLVMValueRef str_alloca,
                                       LLVMBasicBlockRef *out_cont)
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

    /* Build cleanup block */
    LLVMPositionBuilderAtEnd(ctx->builder, cleanup_bb);
    LLVMValueRef str_val = LLVMBuildLoad2(ctx->builder, str_type, str_alloca, "sf.str");
    LLVMValueRef cap = LLVMBuildExtractValue(ctx->builder, str_val, 2, "sf.cap");
    LLVMValueRef zero = LLVMConstInt(i32_type, 0, 0);
    LLVMValueRef is_owned = LLVMBuildICmp(ctx->builder, LLVMIntSGT, cap, zero, "sf.owned");
    LLVMBuildCondBr(ctx->builder, is_owned, free_bb, skip_bb);

    LLVMPositionBuilderAtEnd(ctx->builder, skip_bb);
    LLVMBuildBr(ctx->builder, cont_bb);

    LLVMPositionBuilderAtEnd(ctx->builder, free_bb);
    LLVMValueRef data = LLVMBuildExtractValue(ctx->builder, str_val, 0, "sf.data");
    LLVMValueRef free_fn = LLVMGetNamedFunction(ctx->module, "free");
    LLVMTypeRef free_type = LLVMGlobalGetValueType(free_fn);
    LLVMBuildCall2(ctx->builder, free_type, free_fn, &data, 1, "");
    LLVMBuildBr(ctx->builder, cont_bb);

    LLVMPositionBuilderAtEnd(ctx->builder, cont_bb);

    if (out_cont)
        *out_cont = cont_bb;
}

/* Emit string cleanup in a separate block (for chaining multiple cleanups). */
static LLVMBasicBlockRef emit_string_free_separate(CodegenContext *ctx, LLVMValueRef str_alloca)
{
    LLVMBasicBlockRef cont;
    emit_string_free_with_cont(ctx, str_alloca, &cont);
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
    LLVMValueRef free_fn = LLVMGetNamedFunction(ctx->module, "free");
    LLVMTypeRef free_type = LLVMGlobalGetValueType(free_fn);
    LLVMBuildCall2(ctx->builder, free_type, free_fn, &data, 1, "");
    LLVMBuildBr(ctx->builder, cont_bb);

    LLVMPositionBuilderAtEnd(ctx->builder, cont_bb);
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
static void cg_mark_last_temp_moved(CodegenContext *ctx, int mark)
{
    if (ctx->temp_string_count > mark)
    {
        mark_string_moved(ctx, ctx->temp_string_slots[ctx->temp_string_count - 1]);
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
    mark_string_moved(ctx, src_alloca);
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

    LLVMBasicBlockRef first_cleanup = NULL;
    LLVMBasicBlockRef prev_cont = NULL;

    /* LIFO order: clean up in reverse declaration order */
    for (int i = scope->count - 1; i >= 0; i--)
    {
        CgSymbol *sym = &scope->symbols[i];
        if (sym->type == NULL)
            continue;

        if (sym->type->kind == TYPE_STRING)
        {
            LLVMBasicBlockRef entry = emit_string_free_separate(ctx, sym->value);
            if (first_cleanup == NULL)
            {
                first_cleanup = entry;
            }
            if (prev_cont != NULL)
            {
                LLVMPositionBuilderAtEnd(ctx->builder, prev_cont);
                LLVMBuildBr(ctx->builder, entry);
            }
            prev_cont = LLVMGetInsertBlock(ctx->builder);
        }
        else if (sym->type->kind == TYPE_STRUCT && sym->type->as.strukt.has_drop)
        {
            LLVMBasicBlockRef entry = emit_struct_drop_separate(ctx, sym->value, sym->type, sym->moved_flag);
            if (first_cleanup == NULL)
            {
                first_cleanup = entry;
            }
            if (prev_cont != NULL)
            {
                LLVMPositionBuilderAtEnd(ctx->builder, prev_cont);
                LLVMBuildBr(ctx->builder, entry);
            }
            prev_cont = LLVMGetInsertBlock(ctx->builder);
        }
    }

    /* Branch from entry to first cleanup */
    if (first_cleanup != NULL)
    {
        LLVMPositionBuilderAtEnd(ctx->builder, cur_bb);
        LLVMBuildBr(ctx->builder, first_cleanup);
    }
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
                (sym->type->kind == TYPE_STRUCT && sym->type->as.strukt.has_drop))
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
                LLVMTypeRef str_type = ls_string_type(ctx);
                LLVMTypeRef i32_type = LLVMInt32TypeInContext(ctx->context);
                LLVMValueRef str_val = LLVMBuildLoad2(ctx->builder, str_type, sym->value, "sf.str");
                LLVMValueRef cap = LLVMBuildExtractValue(ctx->builder, str_val, 2, "sf.cap");
                LLVMValueRef zero = LLVMConstInt(i32_type, 0, 0);
                LLVMValueRef is_owned = LLVMBuildICmp(ctx->builder, LLVMIntSGT, cap, zero, "sf.owned");

                char free_name[32], skip_name[32];
                snprintf(free_name, sizeof(free_name), "sf.free%d", idx);
                snprintf(skip_name, sizeof(skip_name), "sf.skip%d", idx);
                LLVMBasicBlockRef free_bb = LLVMAppendBasicBlockInContext(ctx->context, cur_fn, free_name);
                LLVMBasicBlockRef skip_bb = LLVMAppendBasicBlockInContext(ctx->context, cur_fn, skip_name);

                LLVMBuildCondBr(ctx->builder, is_owned, free_bb, skip_bb);

                LLVMPositionBuilderAtEnd(ctx->builder, free_bb);
                LLVMValueRef data = LLVMBuildExtractValue(ctx->builder, str_val, 0, "sf.data");
                LLVMValueRef free_fn = LLVMGetNamedFunction(ctx->module, "free");
                LLVMTypeRef free_type = LLVMGlobalGetValueType(free_fn);
                LLVMBuildCall2(ctx->builder, free_type, free_fn, &data, 1, "");
                LLVMBuildBr(ctx->builder, skip_bb);

                LLVMPositionBuilderAtEnd(ctx->builder, skip_bb);
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
                            LLVMBuildCall2(ctx->builder, drop_fn_type, drop_fn,
                                           &elem_ptr, 1, "");
                            idx++;
                        }
                    }
                }
            }
            else if (sym->type->kind == TYPE_STRUCT && sym->type->as.strukt.has_drop)
            {
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
                    LLVMValueRef ei64 = LLVMBuildSExt(ctx->builder, cur_ei, i64_t, "vcd.ei64");
                    LLVMValueRef ep = LLVMBuildGEP2(ctx->builder, elem_llvm, data_v, &ei64, 1, "vcd.ep");
                    emit_vec_elem_drop_at(ctx, ep, elem_type, idx);

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
                LLVMValueRef free_fn_val = LLVMGetNamedFunction(ctx->module, "free");
                LLVMTypeRef free_fn_t = LLVMGlobalGetValueType(free_fn_val);
                LLVMBuildCall2(ctx->builder, free_fn_t, free_fn_val, &data_v, 1, "");
                LLVMBuildBr(ctx->builder, vdone_bb);

                LLVMPositionBuilderAtEnd(ctx->builder, vdone_bb);
                (void)ptr_t;
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
    LLVMBasicBlockRef cont_bb = NULL;

    if (moved_flag != NULL)
    {
        drop_bb = LLVMAppendBasicBlockInContext(ctx->context, cur_fn, "drop.skip");
        cont_bb = LLVMAppendBasicBlockInContext(ctx->context, cur_fn, "drop.cont");

        LLVMTypeRef i1_type = LLVMInt1TypeInContext(ctx->context);
        LLVMValueRef is_moved = LLVMBuildLoad2(ctx->builder, i1_type, moved_flag, "drop.flag");
        LLVMBuildCondBr(ctx->builder, is_moved, cont_bb, drop_bb);

        LLVMPositionBuilderAtEnd(ctx->builder, drop_bb);
    }

    /* Call complete drop_fn (user wrapper or auto-generated — both handle all cleanup) */
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
            ctx->context, cur_fn, "drop.skip");
        LLVMBasicBlockRef cont_bb = LLVMAppendBasicBlockInContext(
            ctx->context, cur_fn, "drop.cont");

        LLVMTypeRef i1_type = LLVMInt1TypeInContext(ctx->context);
        LLVMValueRef is_moved = LLVMBuildLoad2(ctx->builder, i1_type, moved_flag, "drop.flag");
        LLVMBuildCondBr(ctx->builder, is_moved, cont_bb, drop_bb);

        LLVMPositionBuilderAtEnd(ctx->builder, drop_bb);
        LLVMTypeRef fn_type = LLVMGlobalGetValueType(drop_fn);
        LLVMBuildCall2(ctx->builder, fn_type, drop_fn, &drop_ptr, 1, "");
        LLVMBuildBr(ctx->builder, cont_bb);

        LLVMPositionBuilderAtEnd(ctx->builder, cont_bb);
        return cont_bb;
    }
    else
    {
        /* drop_fn is complete — no additional inline cleanup needed */
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

        /* Free string fields - use inline version, save/restore builder position */
        if (field_type->kind == TYPE_STRING)
        {
            LLVMValueRef field_ptr = LLVMBuildStructGEP2(ctx->builder, llvm_struct,
                                                         self_ptr, (unsigned)i, "drop.strfield");
            emit_string_free(ctx, field_ptr);
            /* After emit_string_free, builder is at cont_bb. Create a new block after it for next cleanup.
               Then branch from cont to new block, or just add new instructions to cont. */
            LLVMBasicBlockRef cont = LLVMGetInsertBlock(ctx->builder);
            LLVMValueRef cur_fn = LLVMGetBasicBlockParent(cont);
            /* LLVM handles name collisions automatically to ensure the label is unique. */
            LLVMBasicBlockRef next_entry = LLVMAppendBasicBlockInContext(ctx->context, cur_fn, "drop.next");
            LLVMBuildBr(ctx->builder, next_entry);
            LLVMPositionBuilderAtEnd(ctx->builder, next_entry);
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

static LLVMTypeRef type_to_llvm(CodegenContext *ctx, Type *t)
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
    case TYPE_MODULE:
        return LLVMVoidTypeInContext(ctx->context);
    }
    return LLVMVoidTypeInContext(ctx->context);
}

/* ---- Forward declarations ---- */

static LLVMValueRef codegen_expr(CodegenContext *ctx, AstNode *node);
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

                /* Expression */
                AstNode *expr = arg->as.format_string.exprs[j];
                LLVMValueRef val = codegen_expr(ctx, expr);
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

        LLVMValueRef val = codegen_expr(ctx, arg);
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

    /* Declare malloc for heap allocation */
    LLVMValueRef malloc_fn = LLVMGetNamedFunction(ctx->module, "malloc");
    LLVMTypeRef malloc_type = LLVMGlobalGetValueType(malloc_fn);

    /* Allocate heap buffer: 4096 bytes (should be enough for most f-strings) */
    LLVMValueRef buf_size = LLVMConstInt(i32_t, 4096, 0);
    LLVMValueRef buf_size64 = LLVMBuildZExt(ctx->builder, buf_size, i64_t, "fstr.size64");
    LLVMValueRef buf = LLVMBuildCall2(ctx->builder, malloc_type, malloc_fn,
                                      &buf_size64, 1, "fstr.buf");

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

/* ---- String method codegen (Batch 1: query methods, no allocation) ---- */

/* Generate LLVM IR for string builtin method calls.
   Returns the result value, or NULL on error. */
static LLVMValueRef codegen_string_method(CodegenContext *ctx, AstNode *node)
{
    AstNode *obj_node = node->as.call.callee->as.field_access.object;
    const char *method = node->as.call.callee->as.field_access.field;
    LLVMValueRef str_val = codegen_expr(ctx, obj_node);
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

    /* s.at(int i) -> int: load byte at index, zero-extend to i32 */
    if (strcmp(method, "at") == 0)
    {
        LLVMValueRef idx = codegen_expr(ctx, node->as.call.args[0]);
        if (idx == NULL)
            return NULL;
        LLVMValueRef gep = LLVMBuildGEP2(ctx->builder,
                                         LLVMInt8TypeInContext(ctx->context), s_data, &idx, 1, "at.ptr");
        LLVMValueRef byte = LLVMBuildLoad2(ctx->builder,
                                           LLVMInt8TypeInContext(ctx->context), gep, "at.byte");
        return LLVMBuildZExt(ctx->builder, byte, i32_type, "at.val");
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

        LLVMValueRef malloc_fn = LLVMGetNamedFunction(ctx->module, "malloc");
        LLVMTypeRef malloc_type = LLVMGlobalGetValueType(malloc_fn);
        LLVMValueRef cap64 = LLVMBuildZExt(ctx->builder, cap, i64_type, "cp.cap64");
        LLVMValueRef buf = LLVMBuildCall2(ctx->builder, malloc_type, malloc_fn,
                                          &cap64, 1, "cp.buf");

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

        LLVMValueRef malloc_fn = LLVMGetNamedFunction(ctx->module, "malloc");
        LLVMTypeRef malloc_type = LLVMGlobalGetValueType(malloc_fn);
        LLVMValueRef cap64 = LLVMBuildZExt(ctx->builder, cap, i64_type, "up.cap64");
        LLVMValueRef buf = LLVMBuildCall2(ctx->builder, malloc_type, malloc_fn,
                                          &cap64, 1, "up.buf");

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

        LLVMValueRef malloc_fn = LLVMGetNamedFunction(ctx->module, "malloc");
        LLVMTypeRef malloc_type = LLVMGlobalGetValueType(malloc_fn);
        LLVMValueRef cap64 = LLVMBuildZExt(ctx->builder, cap, i64_type, "lo.cap64");
        LLVMValueRef buf = LLVMBuildCall2(ctx->builder, malloc_type, malloc_fn,
                                          &cap64, 1, "lo.buf");

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

    /* s.substr(int start, int len) -> string: extract substring */
    if (strcmp(method, "substr") == 0)
    {
        LLVMValueRef start_val = codegen_expr(ctx, node->as.call.args[0]);
        if (start_val == NULL)
            return NULL;
        LLVMValueRef sub_len = codegen_expr(ctx, node->as.call.args[1]);
        if (sub_len == NULL)
            return NULL;

        /* alloc_need = sub_len + 1 */
        LLVMValueRef one = LLVMConstInt(i32_type, 1, 0);
        LLVMValueRef alloc_need = LLVMBuildAdd(ctx->builder, sub_len, one, "ss.need");
        LLVMValueRef min_cap = LLVMConstInt(i32_type, LS_MIN_STR_CAP, 0);
        LLVMValueRef need_gt = LLVMBuildICmp(ctx->builder, LLVMIntUGT,
                                             alloc_need, min_cap, "ss.gt");
        LLVMValueRef cap = LLVMBuildSelect(ctx->builder, need_gt,
                                           alloc_need, min_cap, "ss.cap");

        LLVMValueRef malloc_fn = LLVMGetNamedFunction(ctx->module, "malloc");
        LLVMTypeRef malloc_type = LLVMGlobalGetValueType(malloc_fn);
        LLVMValueRef cap64 = LLVMBuildZExt(ctx->builder, cap, i64_type, "ss.cap64");
        LLVMValueRef buf = LLVMBuildCall2(ctx->builder, malloc_type, malloc_fn,
                                          &cap64, 1, "ss.buf");

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

        LLVMValueRef malloc_fn = LLVMGetNamedFunction(ctx->module, "malloc");
        LLVMTypeRef malloc_type = LLVMGlobalGetValueType(malloc_fn);
        LLVMValueRef cap64 = LLVMBuildZExt(ctx->builder, cap, i64_type, "tr.cap64");
        LLVMValueRef buf = LLVMBuildCall2(ctx->builder, malloc_type, malloc_fn,
                                          &cap64, 1, "tr.buf");

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

    cg_error(ctx, node->line, node->column,
             "unknown string method '%s'", method);
    return NULL;
}

/* ============================================================
 * vec(T) built-in method codegen
 * ============================================================ */

/* Drop a single vec element stored at `elem_ptr` (a *T raw pointer into data[]).
   For string elements: conditional free(data) via emit_string_free_inline.
   For struct-with-drop elements: call the drop function.
   Returns the continuation basic-block (builder positioned at end). */
static LLVMBasicBlockRef emit_vec_elem_drop_at(CodegenContext *ctx,
                                               LLVMValueRef elem_ptr,
                                               Type *elem_type,
                                               int idx_suffix)
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
        emit_string_free_with_cont(ctx, elem_ptr, NULL);
        return LLVMGetInsertBlock(ctx->builder);
    }
    if (elem_type->kind == TYPE_STRUCT && elem_type->as.strukt.has_drop)
    {
        LLVMValueRef drop_fn_val = (LLVMValueRef)elem_type->as.strukt.drop_fn;
        if (drop_fn_val)
        {
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

/* Codegen for vec(T) built-in method calls: v.push(x), v.pop(), v.clear(), v.reserve(n).
   Returns the resulting LLVMValueRef (NULL for void methods). */
static LLVMValueRef codegen_vec_method(CodegenContext *ctx, AstNode *call_node, Type *vec_type)
{
    const char *method = call_node->as.call.callee->as.field_access.field;
    AstNode *obj_node = call_node->as.call.callee->as.field_access.object;
    Type *elem_type = vec_type->as.vec.elem;
    LLVMTypeRef vec_t = ls_vec_type(ctx);
    LLVMTypeRef i32_t = LLVMInt32TypeInContext(ctx->context);
    LLVMTypeRef i64_t = LLVMInt64TypeInContext(ctx->context);

    /* Resolve alloca for the vec object */
    LLVMValueRef vec_alloca = NULL;
    if (obj_node->kind == AST_IDENT)
    {
        CgSymbol *sym = cg_scope_resolve(ctx->current_scope, obj_node->as.ident.name);
        if (sym)
            vec_alloca = sym->value;
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
            cg_mark_last_temp_moved(ctx, temp_mark_push);
        }
        if (elem_type->kind == TYPE_STRUCT && elem_type->as.strukt.has_drop)
        {
            /* For struct-with-drop identifiers, set their moved_flag so scope cleanup skips them */
            AstNode *arg0 = call_node->as.call.args[0];
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
        emit_vec_elem_drop_at(ctx, old_ep, elem_type, 0);

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
            emit_vec_elem_drop_at(ctx, ep, elem_type, 0);

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

    cg_error(ctx, call_node->line, call_node->column,
             "unknown vec method '%s'", method);
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

static LLVMValueRef codegen_expr(CodegenContext *ctx, AstNode *node)
{
    if (node == NULL)
        return NULL;

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

        LLVMValueRef left = codegen_expr(ctx, node->as.binary.left);
        LLVMValueRef right = codegen_expr(ctx, node->as.binary.right);
        if (left == NULL || right == NULL)
            return NULL;

        Type *lt = node->as.binary.left->resolved_type;
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
                LLVMValueRef malloc_fn = LLVMGetNamedFunction(ctx->module, "malloc");
                LLVMTypeRef malloc_type = LLVMGlobalGetValueType(malloc_fn);
                LLVMValueRef cap64 = LLVMBuildZExt(ctx->builder, cap, i64_t, "cat.cap64");
                LLVMValueRef buf = LLVMBuildCall2(ctx->builder, malloc_type, malloc_fn,
                                                  &cap64, 1, "cat.buf");

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

                /* Call free(ptr) */
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

        /* Intercept vec builtin method calls: v.push(x), v.pop(), v.clear(), v.reserve(n) */
        if (node->as.call.callee->kind == AST_FIELD)
        {
            AstNode *obj_node = node->as.call.callee->as.field_access.object;
            if (obj_node->resolved_type && obj_node->resolved_type->kind == TYPE_VECTOR)
            {
                return codegen_vec_method(ctx, node, obj_node->resolved_type);
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
                fn_name = node->as.call.callee->as.field_access.field;
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
        int total_argc = cg_is_method_call ? user_argc + 1 : user_argc;
        LLVMValueRef *args = NULL;

        if (total_argc > 0)
        {
            args = (LLVMValueRef *)malloc_safe((size_t)total_argc * sizeof(LLVMValueRef));
            int arg_offset = 0;

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
                args[0] = self_ptr;
                arg_offset = 1;
            }

            for (int i = 0; i < user_argc; i++)
            {
                args[i + arg_offset] = codegen_expr(ctx, node->as.call.args[i]);
                if (args[i + arg_offset] == NULL)
                {
                    free(args);
                    return NULL;
                }
            }

            /* Auto-extract .data from LsString args when calling C ABI functions. */
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
                            args[i] = ls_string_data(ctx, args[i]);
                        }
                    }
                    else if (LLVMIsFunctionVarArg(fn_type))
                    {
                        args[i] = ls_string_data(ctx, args[i]);
                    }
                }
            }

            /* Mark moved_flag for struct-with-drop arguments that are simple identifiers.
               This prevents double-drop when the variable goes out of scope later. */
            for (int i = 0; i < user_argc; i++)
            {
                AstNode *arg = node->as.call.args[i];
                Type *arg_type = arg->resolved_type;
                if (arg_type && arg_type->kind == TYPE_STRUCT &&
                    arg_type->as.strukt.has_drop &&
                    arg->kind == AST_IDENT)
                {
                    CgSymbol *sym = cg_scope_resolve(ctx->current_scope, arg->as.ident.name);
                    if (sym && sym->moved_flag)
                    {
                        LLVMTypeRef i1_type = LLVMInt1TypeInContext(ctx->context);
                        LLVMBuildStore(ctx->builder, LLVMConstInt(i1_type, 1, 0), sym->moved_flag);
                    }
                }
            }
        }

        LLVMValueRef result = LLVMBuildCall2(ctx->builder, fn_type, callee,
                                             args, (unsigned)total_argc, "");
        /* If function returns void, we can't name the result */
        if (node->resolved_type && node->resolved_type->kind != TYPE_VOID)
        {
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

        /* Vec .length / .capacity — extract fields 1 and 2 from LsVec struct */
        if (obj_type && obj_type->kind == TYPE_VECTOR)
        {
            const char *field = node->as.field_access.field;
            if (strcmp(field, "length") == 0 || strcmp(field, "capacity") == 0)
            {
                /* The vec object: load the struct from its alloca */
                LLVMValueRef vec_val = NULL;
                if (obj_node->kind == AST_IDENT)
                {
                    CgSymbol *sym = cg_scope_resolve(ctx->current_scope,
                                                     obj_node->as.ident.name);
                    if (sym)
                    {
                        LLVMTypeRef vt = ls_vec_type(ctx);
                        vec_val = LLVMBuildLoad2(ctx->builder, vt, sym->value, "vf.v");
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

        /* String .length — extract .len field from LsString struct */
        if (obj_type && obj_type->kind == TYPE_STRING)
        {
            if (strcmp(node->as.field_access.field, "length") == 0)
            {
                LLVMValueRef str_val = codegen_expr(ctx, obj_node);
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
        LLVMTypeRef field_llvm = type_to_llvm(ctx, struct_type->as.strukt.fields[field_idx].type);
        return LLVMBuildLoad2(ctx->builder, field_llvm, gep, field_name);
    }

    case AST_MATCH:
    {
        /* Compile match as cascading if-else */
        LLVMValueRef subject = codegen_expr(ctx, node->as.match.subject);
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
            LLVMTypeRef vec_t    = ls_vec_type(ctx);
            Type       *elem_type = obj_type->as.vec.elem;
            LLVMTypeRef elem_llvm = type_to_llvm(ctx, elem_type);
            LLVMTypeRef i32_t    = LLVMInt32TypeInContext(ctx->context);
            LLVMTypeRef i64_t    = LLVMInt64TypeInContext(ctx->context);

            LLVMValueRef vec_val  = LLVMBuildLoad2(ctx->builder, vec_t, vec_alloca, "vi.v");
            LLVMValueRef data_ptr = LLVMBuildExtractValue(ctx->builder, vec_val, 0, "vi.data");
            LLVMValueRef len_val  = LLVMBuildExtractValue(ctx->builder, vec_val, 1, "vi.len");

            LLVMValueRef index = codegen_expr(ctx, idx_node);
            if (index == NULL)
                return NULL;
            if (LLVMTypeOf(index) != i64_t)
                index = LLVMBuildSExtOrBitCast(ctx->builder, index, i64_t, "vi.idx");

            /* Bounds check: 0 <= index < len */
            LLVMValueRef len64    = LLVMBuildSExt(ctx->builder, len_val, i64_t, "vi.len64");
            LLVMValueRef zero64   = LLVMConstInt(i64_t, 0, 0);
            LLVMValueRef ge_zero  = LLVMBuildICmp(ctx->builder, LLVMIntSGE, index, zero64, "vi.ge0");
            LLVMValueRef lt_len   = LLVMBuildICmp(ctx->builder, LLVMIntSLT, index, len64, "vi.ltl");
            LLVMValueRef in_bounds = LLVMBuildAnd(ctx->builder, ge_zero, lt_len, "vi.inb");

            LLVMValueRef cur_fn = LLVMGetBasicBlockParent(LLVMGetInsertBlock(ctx->builder));
            int id = g_block_counter++;
            char ok_name[32], oob_name[32], merge_name[32];
            snprintf(ok_name,    sizeof(ok_name),    "vi.ok%d",    id);
            snprintf(oob_name,   sizeof(oob_name),   "vi.oob%d",   id);
            snprintf(merge_name, sizeof(merge_name), "vi.merge%d", id);
            LLVMBasicBlockRef ok_bb    = LLVMAppendBasicBlockInContext(ctx->context, cur_fn, ok_name);
            LLVMBasicBlockRef oob_bb   = LLVMAppendBasicBlockInContext(ctx->context, cur_fn, oob_name);
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

            /* ok_bb: in-bounds — normal GEP + load */
            LLVMPositionBuilderAtEnd(ctx->builder, ok_bb);
            LLVMValueRef gep  = LLVMBuildGEP2(ctx->builder, elem_llvm, data_ptr, &index, 1, "vi.ep");
            LLVMValueRef elem = LLVMBuildLoad2(ctx->builder, elem_llvm, gep, "vi.elem");
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
            return LLVMBuildLoad2(ctx->builder, elem_llvm, result_alloca, "vi.r");
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
        LLVMTypeRef elem_llvm = type_to_llvm(ctx, obj_type->as.array.elem);
        return LLVMBuildLoad2(ctx->builder, elem_llvm, gep, "arr.elem");
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

#ifdef CG_DEBUG
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
        /* Zero-initialize vec variables so that cap=0, data=NULL, len=0.
           emit_cleanup_to checks cap > 0 before freeing, so this is safe. */
        if (var_type->kind == TYPE_VECTOR)
        {
            LLVMBuildStore(ctx->builder, LLVMConstNull(llvm_type), alloca);
        }

        if (node->as.var_decl.init)
        {
            /* Track temp slots created during init expression evaluation */
            int temp_mark = ctx->temp_string_count;

            /* Special handling for array literal initialization */
            if (var_type->kind == TYPE_ARRAY &&
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
                        mark_string_moved(ctx, ctx->temp_string_slots[ti]);
                    }
                }
                ctx->temp_string_count = temp_mark;
            }
            else
            {
                LLVMValueRef init = codegen_expr(ctx, node->as.var_decl.init);
                if (init)
                    LLVMBuildStore(ctx->builder, init, alloca);

                /* For string variables: mark last temp as moved (its value is now in alloca),
                   free any intermediate temps created during expression evaluation. */
                if (var_type->kind == TYPE_STRING)
                {
                    cg_mark_last_temp_moved(ctx, temp_mark);
                    cg_flush_temps(ctx, temp_mark, true);
                }
                else
                {
                    /* Non-string: no temps created (or not relevant) */
                    ctx->temp_string_count = temp_mark;
                }
            }
        }

        cg_scope_define(ctx->current_scope, node->as.var_decl.name, alloca, var_type, moved_flag);
        break;
    }

    case AST_ASSIGN:
    {
        /* Track temp string slots created during value evaluation */
        int temp_mark = ctx->temp_string_count;
        LLVMValueRef val = codegen_expr(ctx, node->as.assign.value);
        if (val == NULL)
            return;

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
                        /* String variable → string variable: free old dst, then move */
                        emit_string_free(ctx, sym->value); /* BUG #2 fix */
                        emit_string_move(ctx, sym->value, src_sym->value);
                    }
                    else
                    {
                        /* Expression result → string variable: free old dst, store new */
                        emit_string_free(ctx, sym->value); /* BUG #2 fix */
                        LLVMBuildStore(ctx->builder, val, sym->value);
                        /* Last temp slot holds the new value now stored in sym — mark moved */
                        cg_mark_last_temp_moved(ctx, temp_mark);
                    }
                    /* Free any intermediate temp strings produced during expression eval */
                    cg_flush_temps(ctx, temp_mark, true);
                }
                else
                {
                    LLVMBuildStore(ctx->builder, val, sym->value);
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
                bool field_is_string = (field_type && field_type->kind == TYPE_STRING);

                if (field_is_string)
                {
                    /* BUG #2 fix: free old field value before overwriting */
                    emit_string_free(ctx, field_ptr);
                    CgSymbol *src_sym = get_string_var_symbol(node->as.assign.value, ctx);
                    if (src_sym != NULL)
                    {
                        LLVMTypeRef str_type = ls_string_type(ctx);
                        LLVMValueRef src_val = LLVMBuildLoad2(ctx->builder, str_type,
                                                              src_sym->value, "sfld.src");
                        LLVMBuildStore(ctx->builder, src_val, field_ptr);
                        mark_string_moved(ctx, src_sym->value);
                    }
                    else
                    {
                        LLVMBuildStore(ctx->builder, val, field_ptr);
                        cg_mark_last_temp_moved(ctx, temp_mark);
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
                    cg_mark_last_temp_moved(ctx, temp_mark);
                    cg_flush_temps(ctx, temp_mark, true);
                }
                else if (elem_type && elem_type->kind == TYPE_STRUCT &&
                         elem_type->as.strukt.has_drop)
                {
                    /* drop old struct element before overwriting */
                    emit_vec_elem_drop_at(ctx, gep, elem_type, 0);
                    LLVMBuildStore(ctx->builder, val, gep);
                    ctx->temp_string_count = temp_mark;
                }
                else
                {
                    LLVMBuildStore(ctx->builder, val, gep);
                    ctx->temp_string_count = temp_mark;
                }
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
                    cg_mark_last_temp_moved(ctx, temp_mark);
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
            /* *ptr = val */
            LLVMValueRef ptr = codegen_expr(ctx, node->as.assign.target->as.unary.operand);
            if (ptr)
                LLVMBuildStore(ctx->builder, val, ptr);
            ctx->temp_string_count = temp_mark;
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
            if (ret_type->kind == TYPE_STRING || ret_type->kind == TYPE_STRUCT)
            {
                const char *name = node->as.return_stmt.value->as.ident.name;
                /* cg_scope_resolve returns the innermost binding for this name.
                   We immediately extract the alloca (stable LLVMValueRef) and
                   discard the CgSymbol* pointer, which could become dangling if
                   the scope's symbol array is reallocated by codegen_expr below. */
                CgSymbol *sym = cg_scope_resolve(ctx->current_scope, name);
                if (sym)
                    return_alloca = sym->value;
            }
        }

        if (node->as.return_stmt.value)
        {
            LLVMValueRef val = codegen_expr(ctx, node->as.return_stmt.value);
            if (val)
            {
                emit_cleanup_to(ctx, NULL, return_alloca);
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
                CgSymbol *lvsym = cg_scope_define(ctx->current_scope, node->as.for_stmt.var,
                                                  loop_var, iter_type->as.vec.elem, NULL);
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
            /* Load current element: loop_var = vec.data[idx] */
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
            LLVMBuildStore(ctx->builder, elem_val, loop_var);
        }

        codegen_stmt(ctx, node->as.for_stmt.body);
        if (LLVMGetBasicBlockTerminator(LLVMGetInsertBlock(ctx->builder)) == NULL)
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

    LLVMValueRef saved_fn = ctx->current_fn;
    ctx->current_fn = fn;

    push_scope(ctx);

    /* For instance methods, param[0] is the implicit self pointer (*Struct) */
    int param_offset = 0;
    if (is_instance_method)
    {
        Type *self_type = fn_type_ml->as.function.params[0]; /* *Struct */
        LLVMTypeRef self_llvm = type_to_llvm(ctx, self_type);
        LLVMValueRef self_alloca = LLVMBuildAlloca(ctx->builder, self_llvm, "self");
        LLVMBuildStore(ctx->builder, LLVMGetParam(fn, 0), self_alloca);
        cg_scope_define(ctx->current_scope, "self", self_alloca, self_type, NULL);
        param_offset = 1;
    }

    /* Alloca for each user-declared parameter */
    for (int i = 0; i < user_n; i++)
    {
        int llvm_idx = i + param_offset;
        Type *param_type = fn_type_ml->as.function.params[llvm_idx];
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
        CgSymbol *psym = cg_scope_define(ctx->current_scope, node->as.fn_decl.param_names[i], alloca, param_type, moved_flag);
        /* vec parameters are borrowed: the caller owns the data buffer, so don't free on return */
        if (psym && param_type && param_type->kind == TYPE_VECTOR)
            psym->is_borrowed = true;
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

    /* Restore temp string count to what it was before compiling this function */
    ctx->temp_string_count = saved_temp_count;

    /* Verify function */
    if (LLVMVerifyFunction(fn, LLVMPrintMessageAction))
    {
        fprintf(stderr, "[codegen] warning: function '%s' failed verification\n", name);
    }
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
                            emit_string_free(ctx, fptr);
                            /* Advance past the conditional blocks emit_string_free added */
                            LLVMBasicBlockRef cont = LLVMGetInsertBlock(ctx->builder);
                            LLVMValueRef wfn = LLVMGetBasicBlockParent(cont);
                            LLVMBasicBlockRef nxt = LLVMAppendBasicBlockInContext(
                                ctx->context, wfn, "wrap.next");
                            LLVMBuildBr(ctx->builder, nxt);
                            LLVMPositionBuilderAtEnd(ctx->builder, nxt);
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

/* Build an LLVM function type using C ABI mapping for extern fn declarations */
static LLVMTypeRef extern_fn_type(CodegenContext *ctx, Type *fn_type_ml)
{
    int n = fn_type_ml->as.function.param_count;
    LLVMTypeRef *params = NULL;
    if (n > 0)
    {
        params = (LLVMTypeRef *)malloc_safe((size_t)n * sizeof(LLVMTypeRef));
        for (int i = 0; i < n; i++)
        {
            params[i] = type_to_c_abi(ctx, fn_type_ml->as.function.params[i]);
        }
    }
    LLVMTypeRef ret = type_to_c_abi(ctx, fn_type_ml->as.function.return_type);
    LLVMTypeRef ft = LLVMFunctionType(ret, params, (unsigned)n,
                                      fn_type_ml->as.function.is_vararg ? 1 : 0);
    free(params);
    return ft;
}

static void codegen_extern_fn(CodegenContext *ctx, AstNode *node)
{
    Type *fn_type_ml = node->resolved_type;
    if (fn_type_ml == NULL || fn_type_ml->kind != TYPE_FUNCTION)
        return;

    /* Use C ABI type mapping: string → i8*, not LsString struct */
    LLVMTypeRef fn_type = extern_fn_type(ctx, fn_type_ml);
    LLVMValueRef fn = LLVMGetNamedFunction(ctx->module, node->as.extern_fn.name);
    if (fn == NULL)
    {
        fn = LLVMAddFunction(ctx->module, node->as.extern_fn.name, fn_type);
        LLVMSetLinkage(fn, LLVMExternalLinkage);
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
    case AST_IMPL_DECL:
        codegen_impl_decl(ctx, node);
        break;
    case AST_EXTERN_FN:
        codegen_extern_fn(ctx, node);
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
    emit_str_replace_helper(ctx);

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
                else if (decl->kind == AST_FN_DECL)
                {
                    Type *fn_type_ml = decl->resolved_type;
                    if (fn_type_ml && fn_type_ml->kind == TYPE_FUNCTION)
                    {
                        LLVMTypeRef fn_type = type_to_llvm(ctx, fn_type_ml);
                        if (!LLVMGetNamedFunction(ctx->module, decl->as.fn_decl.name))
                        {
                            LLVMAddFunction(ctx->module, decl->as.fn_decl.name, fn_type);
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

    /* Pass 1: Declare all structs, function signatures, and FFI lib globals */
    for (int i = 0; i < ast->as.program.decl_count; i++)
    {
        AstNode *decl = ast->as.program.decls[i];
        if (decl->kind == AST_STRUCT_DECL)
        {
            codegen_struct_decl(ctx, decl);
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
                case AST_IMPL_DECL:
                case AST_EXTERN_FN:
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
                case AST_IMPL_DECL:
                case AST_EXTERN_FN:
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
            LLVMValueRef free_fn = LLVMGetNamedFunction(ctx->module, "free");                            \
            LLVMTypeRef free_type = LLVMGlobalGetValueType(free_fn);                                     \
            LLVMBuildCall2(ctx->builder, free_type, free_fn, &data, 1, "");                              \
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
            /* Already handled */
            break;
        case AST_FN_DECL:
            codegen_fn_decl(ctx, decl);
            break;
        case AST_IMPL_DECL:
            /* Already handled in Pass 2a */
            break;
        case AST_EXTERN_FN:
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
