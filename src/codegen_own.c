/* codegen_own.c
   所有权引擎：clone / drop / scope cleanup / temp-drop 表 / move 失效 / cg_store_owned

   Bodies mechanically relocated from codegen.c (docs/plan_codegen_split.md).
   No logic changes. All prototypes live in codegen_internal.h. */
#include "codegen.h"
#include "codegen_internal.h"
#include "module.h"
#define LS_INCLUDE_CODEGEN 1
#include "builtins_math.h"
#define LS_INCLUDE_CODEGEN 1
#include "builtins_perf.h"
#include "common.h"

#include <llvm-c/Core.h>
#include <llvm-c/Target.h>
#include <llvm-c/TargetMachine.h>
#include <llvm-c/Analysis.h>

#include <stdio.h>
#include <string.h>
#include <ctype.h>
/* File-local helpers (single-TU; re-static'd at codegen split §7). */
static LLVMValueRef cg_ensure_user_struct_drop_decl(CodegenContext *ctx, Type *struct_type);
static void cg_flush_temp_drops(CodegenContext *ctx);
static void emit_auto_enum_clone_fn(CodegenContext *ctx, Type *enum_type);
static void emit_enum_drop_cond(CodegenContext *ctx, LLVMValueRef enum_ptr, Type *enum_type, LLVMValueRef moved_flag);
static LLVMBasicBlockRef emit_struct_drop_separate(CodegenContext *ctx, LLVMValueRef drop_ptr, Type *struct_type, LLVMValueRef moved_flag);

CgScope *cg_scope_new(CgScope *parent)
{
    CgScope *s = (CgScope *)malloc_safe(sizeof(CgScope));
    s->symbols = NULL;
    s->count = 0;
    s->capacity = 0;
    s->parent = parent;
    return s;
}

void cg_scope_free(CgScope *s)
{
    if (s == NULL)
        return;
    free(s->symbols);
    free(s);
}

CgSymbol *cg_scope_define(CgScope *s, const char *name, LLVMValueRef val, Type *type,
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
    s->symbols[s->count].lifetime_marked = false;
    return &s->symbols[s->count++];
}

CgSymbol *cg_scope_resolve(CgScope *s, const char *name)
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

void push_scope(CodegenContext *ctx)
{
    ctx->current_scope = cg_scope_new(ctx->current_scope);
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

void pop_scope(CodegenContext *ctx)
{
    CgScope *old = ctx->current_scope;
    ctx->current_scope = old->parent;
    cg_scope_free(old);
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
void cg_emit_free(CodegenContext *ctx, LLVMValueRef ptr,
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

/* A struct is move-only when it has a user destructor (`~` / Destroy) that owns a
   raw pointer (*T) or object field but provides NO Clone impl: the compiler cannot
   safely auto-clone it (a field-wise copy would alias the buffer → double-free).
   Such a value must be MOVED, never copied. Used both to reject auto-clone at the
   copy site (emit_struct_clone_val) and to choose move-out over clone for a binder
   matched out of an owned enum subject (codegen_match.c). Mirrors Rust's no-Clone
   resource handles (File / Mutex / etc.). */
bool cg_struct_is_move_only(const Type *t)
{
    if (t == NULL || t->kind != TYPE_STRUCT) return false;
    if (!t->as.strukt.has_user_drop || t->as.strukt.has_user_clone) return false;
    for (int fi = 0; fi < t->as.strukt.field_count; fi++)
    {
        Type *ft = t->as.strukt.fields[fi].type;
        if (ft != NULL && (ft->kind == TYPE_POINTER || ft->kind == TYPE_OBJECT))
            return true;
    }
    return false;
}

/* Clone a struct VALUE by deep-copying all owned string fields.
   Non-string fields are copied by value (cheap, correct for int/bool/f64/pointer).
   Nested struct-with-drop fields are recursively cloned.
   Returns a new struct value with independently owned data. */
LLVMValueRef emit_struct_clone_val(CodegenContext *ctx,
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
#if CG_DEBUG
            {
                /* Same tracking as the auto-clone path below — the user-clone
                   early return previously skipped it, hiding Str/Vec/Map
                   clones from CG_DEBUG counts. */
                char dbg_fmt[128];
                snprintf(dbg_fmt, sizeof(dbg_fmt),
                         "[cg] struct.clone  user type=%s\n",
                         struct_type->as.strukt.name ? struct_type->as.strukt.name
                                                     : "?");
                cg_emit_debug_printf(ctx, dbg_fmt, NULL, 0);
            }
#endif
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

    /* Move-only safety (方式一): a struct with a user destructor (`~`) that owns a
       raw pointer (*T) or object field, but NO Clone impl, cannot be safely
       auto-cloned — the field-wise copy below would shallow-copy the raw pointer,
       aliasing the buffer so both copies free it on drop (double-free). Such a type
       is move-only: reject the copy at compile time rather than emit a latent
       double-free. (To make it copyable, `methods X: Clone { def clone(&self) -> X }`
       and deep-copy the buffer there — like Vec/Map/Str/Tensor.) A move-only payload
       matched out of an OWNED enum subject is moved (not cloned) — see codegen_match.c. */
    if (cg_struct_is_move_only(struct_type))
    {
        const char *nm = struct_type->as.strukt.name
                             ? struct_type->as.strukt.name : "<struct>";
        cg_error(ctx, 0, 0,
            "cannot copy '%s' — it has a destructor and owns a raw "
            "pointer/object field but no Clone (it is move-only); move it "
            "instead, or implement `methods %s: Clone { def clone(&self) -> %s }`",
            nm, nm, nm);
        return struct_val;
    }

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
LLVMValueRef emit_enum_clone_val(CodegenContext *ctx,
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
LLVMValueRef emit_array_clone_val(CodegenContext *ctx, LLVMValueRef arr_val,
                                         LLVMTypeRef llvm_arr_type, Type *arr_type)
{
    if (arr_type == NULL || arr_type->kind != TYPE_ARRAY)
        return arr_val;

    Type *elem_type = arr_type->as.array.elem;
    if (elem_type == NULL)
        return arr_val;

    bool elem_needs_clone =
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
        if (elem_type->kind == TYPE_ENUM && elem_type->as.enom.has_drop)
            cloned = emit_enum_clone_val(ctx, elem, elem_type);
        else
            cloned = emit_struct_clone_val(ctx, elem, elem_llvm, elem_type);
        result = LLVMBuildInsertValue(ctx->builder, result, cloned,
                                      (unsigned)i, "ac.ins");
    }

    (void)llvm_arr_type;
    return result;
}

LLVMValueRef emit_clone_value(CodegenContext *ctx, LLVMValueRef val,
                                     LLVMTypeRef llvm_type, Type *type)
{
    if (type == NULL) return val;
    switch (type->kind)
    {
    case TYPE_ENUM:
        return type->as.enom.has_drop ? emit_enum_clone_val(ctx, val, type) : val;
    case TYPE_STRUCT:
        return type->as.strukt.has_drop
                   ? emit_struct_clone_val(ctx, val, llvm_type, type) : val;
    default: return val;
    }
}

/* M-4.5: register a statement-level temporary has_drop struct/enum value.
   `slot` is the alloca holding the deep-cloned value (e.g. result of vec[i]);
   `type` is its LS type (TYPE_STRUCT has_drop or TYPE_ENUM has_drop).
   the slots produced since a given mark (aligned with string-temp semantics). */
void cg_push_temp_drop(CodegenContext *ctx, LLVMValueRef slot, Type *type)
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
    }
    ctx->temp_drop_slots[ctx->temp_drop_count] = slot;
    ctx->temp_drop_types[ctx->temp_drop_count] = type;
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
void cg_remove_temp_drop(CodegenContext *ctx, LLVMValueRef slot)
{
    if (slot == NULL) return;
    int keep = 0;
    for (int i = 0; i < ctx->temp_drop_count; i++)
    {
        if (ctx->temp_drop_slots[i] == slot)
            continue; /* drop the entry (already handled) */
        ctx->temp_drop_slots[keep] = ctx->temp_drop_slots[i];
        ctx->temp_drop_types[keep] = ctx->temp_drop_types[i];
        keep++;
    }
    ctx->temp_drop_count = keep;
}

/* M-4.5: drop and release all statement-level temp_drop slots. Marks were
   full flush (which has been the runtime behavior since literals went Str). */
static void cg_flush_temp_drops(CodegenContext *ctx)
{
    LLVMBasicBlockRef cur = LLVMGetInsertBlock(ctx->builder);
    if (getenv("LS_DEBUG_TEMPS") && ctx->temp_drop_count > 0)
        fprintf(stderr, "[tmp] flush fn=%s n=%d term=%d\n",
                ctx->current_fn ? LLVMGetValueName(ctx->current_fn) : "?",
                ctx->temp_drop_count,
                (int)(cur && LLVMGetBasicBlockTerminator(cur) != NULL));
    int base = ctx->temp_drop_base;
    if (base < 0) base = 0;
    if (base > ctx->temp_drop_count) base = ctx->temp_drop_count;
    if (cur && LLVMGetBasicBlockTerminator(cur) != NULL)
    {
        /* terminated block: just discard the slots above the protected base */
        ctx->temp_drop_count = base;
        return;
    }
    for (int i = base; i < ctx->temp_drop_count; i++)
    {
        Type *t = ctx->temp_drop_types[i];
        LLVMValueRef slot = ctx->temp_drop_slots[i];
        if (t->kind == TYPE_STRUCT)
            emit_struct_drop(ctx, slot, t);
        else if (t->kind == TYPE_ENUM)
            emit_enum_drop(ctx, slot, t);
    }
    ctx->temp_drop_count = base;
}

/* Phase C.5: register a closure literal's env_ptr as a temporary owned by
   the current statement. Drained by cg_flush_temps. The drop_fn at env[0]
   (NULL for POD-only envs) handles per-capture cleanup; we then free the
   env block itself. */
void cg_push_temp_block_env(CodegenContext *ctx, LLVMValueRef env_ptr)
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

/* Phase G: true when a Block-typed initializer reads a Block out of a container
   it does not own — `vec[i]`, `struct.field`, or `map.get(k)`. These produce an
   LsBlock that aliases the container's env, so a copy-out into a new variable
   must deep-clone the env. A factory call `fn()->Block` returns an owned env and
   is deliberately NOT matched here (no clone). Mirrors the checker's former
   F.3/F.4A rejection patterns. */
bool cg_block_source_is_aliased(AstNode *src)
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
bool cg_invalidate_moved_source(CodegenContext *ctx, AstNode *source, Type *type)
{
    if (!source || !type) return false;
    AstNode *src = ast_unwrap_move(source);
    if (!src || src->kind != AST_IDENT) return false;
    CgSymbol *sym = cg_scope_resolve(ctx->current_scope, src->as.ident.name);
    if (!sym || !sym->value || sym->is_borrowed || sym->is_mut_borrow)
        return false;

    switch (type->kind)
    {
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
   kind:      CG_XFER_INTO_CONTAINER — 存入容器（move 语义，source 被消耗）
              CG_XFER_ASSIGN_VAR     — 变量赋值（clone 语义，string source 保持有效）
              CG_XFER_RETURN         — return（同 INTO_CONTAINER，move 语义）
*/
void cg_store_owned(CodegenContext *ctx,
                           LLVMValueRef dst_ptr,
                           LLVMValueRef val,
                           Type *type,
                           AstNode *source,
                           CgTransferKind kind)
{
    if (!val || !dst_ptr || !type) {
        if (dst_ptr && val)
            LLVMBuildStore(ctx->builder, val, dst_ptr);
        return;
    }

    AstNode *src = source ? ast_unwrap_move(source) : NULL;
    /* A block-expression `{ ...; E }` yields E; its ownership-transfer source is
       the tail E. The closure combinators (map/map_err) wrap the closure body in
       a ctor — `Some(body)` where body is the `{ x }` block of `|x| x` — so the
       source here is a block whose tail is the moved binder. Without peeling,
       src_sym below stays NULL and the binder is never marked moved → it is both
       moved into the payload AND dropped on arm exit → double-free (L-013 family,
       identity-closure case). Peel to the tail (through nested blocks / __move).
       A non-binder tail (e.g. `|s| s.upper()` → CALL) resolves to no src_sym and
       falls through to the existing rvalue/store path, unchanged. */
    while (src && src->kind == AST_BLOCK && src->as.block.stmt_count > 0) {
        AstNode *last = src->as.block.stmts[src->as.block.stmt_count - 1];
        if (last && last->kind == AST_EXPR_STMT && last->as.expr_stmt.expr)
            src = ast_unwrap_move(last->as.expr_stmt.expr);
        else
            break;
    }
    (void)kind;

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

/* Statement-boundary temp flush: drain temporary closure envs (literals
   consumed as rvalues this statement) and statement-level has_drop temps. */
void cg_flush_temps(CodegenContext *ctx)
{
    LLVMBasicBlockRef cur = LLVMGetInsertBlock(ctx->builder);
    if (cur && LLVMGetBasicBlockTerminator(cur) != NULL)
    {
        ctx->temp_block_env_count = 0;
        cg_flush_temp_drops(ctx);
        return;
    }
    for (int i = 0; i < ctx->temp_block_env_count; i++)
        cg_emit_block_env_drop(ctx, ctx->temp_block_envs[i]);
    ctx->temp_block_env_count = 0;
    cg_flush_temp_drops(ctx);
}

/* Scope-exit temp flush: drain EVERY statement-level temp (from index 0),
   bypassing temp_drop_base. Used on `return` (and try's error-return), which
   leaves the enclosing function entirely — every live temp, including a match
   subject the base protects for the fall-through/next-arm paths, must be
   released on THIS exiting path.

   The base is restored afterwards (only base, not count): the exiting block is
   terminated right after, so its post-flush count is dead. SIBLING blocks that
   still need the protected subject (e.g. the `if.merge` after an `if.then` that
   returned) recover the correct count from the enclosing AST_IF, which snapshots
   and restores temp_drop_count across its branches — keeping that responsibility
   in one place rather than spread over every scope-exit. */
void cg_flush_temps_scope_exit(CodegenContext *ctx)
{
    int saved_base = ctx->temp_drop_base;
    ctx->temp_drop_base = 0;
    cg_flush_temps(ctx);
    ctx->temp_drop_base = saved_base;
}

/* Partial temp flush: drop and release only the temp_block_env / temp_drop
   slots registered ABOVE the given floors (i.e. created since a snapshot),
   leaving lower-index temps owned by an enclosing expression intact. With
   floors of 0 this is identical to cg_flush_temps. Used by emit_enum_ctor so
   that building an enum-ctor argument (e.g. the rhs of an operator-overload
   `Enum(x) == Enum(y)`) flushes ONLY that ctor's own argument temps and not
   the enclosing call's already-spilled receiver — which the call still needs
   and will drop at the statement boundary. */
void cg_flush_temps_from(CodegenContext *ctx, int env_floor, int drop_floor)
{
    if (env_floor  < 0) env_floor  = 0;
    if (drop_floor < 0) drop_floor = 0;
    LLVMBasicBlockRef cur = LLVMGetInsertBlock(ctx->builder);
    bool terminated = (cur && LLVMGetBasicBlockTerminator(cur) != NULL);
    if (!terminated)
    {
        for (int i = env_floor; i < ctx->temp_block_env_count; i++)
            cg_emit_block_env_drop(ctx, ctx->temp_block_envs[i]);
        for (int i = drop_floor; i < ctx->temp_drop_count; i++)
        {
            Type *t = ctx->temp_drop_types[i];
            LLVMValueRef slot = ctx->temp_drop_slots[i];
            if (t->kind == TYPE_STRUCT)
                emit_struct_drop(ctx, slot, t);
            else if (t->kind == TYPE_ENUM)
                emit_enum_drop(ctx, slot, t);
        }
    }
    if (ctx->temp_block_env_count > env_floor)  ctx->temp_block_env_count = env_floor;
    if (ctx->temp_drop_count      > drop_floor) ctx->temp_drop_count      = drop_floor;
}

/* Emit cleanup IR for all dynamic string locals in the current scope.
   Uses LIFO order (reverse traversal) to match C++ destructor semantics. */
/* Emit cleanup for all owned string/struct-with-drop symbols in the current scope.
   Uses LIFO order (reverse declaration). Each cleanup function
   (emit_struct_drop_cond etc.) branches from the current block to separate cleanup blocks
   and leaves the builder at the continuation block, ready for the next cleanup or
   the caller's next instruction.
   Skips borrowed symbols (is_borrowed=true) and trivial types.
   Does nothing if the current block is already terminated. */
void emit_scope_cleanup(CodegenContext *ctx)
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
       Each emit_struct_drop_cond / enum drop branches from the current
       block and leaves the builder at the cont_bb of that cleanup step.
       Subsequent calls chain naturally. */
    for (int i = scope->count - 1; i >= 0; i--)
    {
        CgSymbol *sym = &scope->symbols[i];
        if (sym->type == NULL || sym->is_borrowed)
            continue;

        if (sym->type->kind == TYPE_STRUCT && sym->type->as.strukt.has_drop)
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
    /* A2: emit the paired lifetime.end for every marked aggregate slot in this
       scope, after all its drops above (a lifetime.end must follow the last read
       of the slot, and the drop is that read). No-op when lifetime is disabled
       (no slot is ever marked in that case). */
    for (int i = scope->count - 1; i >= 0; i--)
    {
        CgSymbol *sym = &scope->symbols[i];
        if (sym->lifetime_marked && sym->type != NULL)
            cg_emit_lifetime_end(ctx, sym->value, type_to_llvm(ctx, sym->type));
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
void emit_cleanup_to(CodegenContext *ctx, CgScope *stop, LLVMValueRef skip_alloca)
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
            if ((sym->type->kind == TYPE_STRUCT && sym->type->as.strukt.has_drop) ||
                (sym->type->kind == TYPE_ENUM && sym->type->as.enom.has_drop))
            {
                count++;
            }
            else if (sym->type->kind == TYPE_ARRAY && sym->type->as.array.elem)
            {
                Type *elem = sym->type->as.array.elem;
                /* BUG #3 fix: array(struct-with-drop,N) needs cleanup */
                if (elem->kind == TYPE_STRUCT && elem->as.strukt.has_drop)
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

    /* A2: does any marked aggregate slot in this scope range need a lifetime.end?
       (Excludes the return-value slot — skip_alloca — whose storage is still
       read to materialise the return value; and mirrors the drop exclusions.)
       When lifetime is disabled no slot is ever marked, so has_ends stays false
       and the count==0 early-return path below is byte-identical to the baseline. */
    bool has_ends = false;
    for (CgScope *s = ctx->current_scope; s != NULL && s != stop && !has_ends; s = s->parent)
    {
        for (int i = 0; i < s->count; i++)
        {
            CgSymbol *sym = &s->symbols[i];
            if (sym->lifetime_marked && sym->type != NULL &&
                !(skip_alloca != NULL && sym->value == skip_alloca))
            {
                has_ends = true;
                break;
            }
        }
    }

    if (count == 0 && !has_ends)
        return;

    /* Create single cleanup block and branch to it — only when there are actual
       drops. Pure lifetime.end emission (count==0, has_ends) stays inline in the
       current block, so it needs no separate block. */
    if (count > 0)
    {
        LLVMBasicBlockRef cleanup_bb = LLVMAppendBasicBlockInContext(
            ctx->context, cur_fn, "cleanup");
        LLVMBuildBr(ctx->builder, cleanup_bb);
        LLVMPositionBuilderAtEnd(ctx->builder, cleanup_bb);
    }

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

            if (sym->type->kind == TYPE_ARRAY && sym->type->as.array.elem)
            {
                Type *elem_type = sym->type->as.array.elem;
                int arr_size = (int)sym->type->as.array.size;
                LLVMTypeRef arr_type = type_to_llvm(ctx, sym->type);
                LLVMTypeRef i64_type = LLVMInt64TypeInContext(ctx->context);
                LLVMValueRef zero64 = LLVMConstInt(i64_type, 0, 0);

                if (elem_type->kind == TYPE_STRUCT && elem_type->as.strukt.has_drop)
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

    /* A2: paired lifetime.end for every marked aggregate slot in the cleaned
       scope range, after all drops above and before the caller's ret/br. Skips
       the return-value slot (skip_alloca) — its storage still feeds the return.
       No-op when lifetime is disabled (nothing is ever marked then). */
    for (CgScope *s = ctx->current_scope; s != NULL && s != stop; s = s->parent)
    {
        for (int i = s->count - 1; i >= 0; i--)
        {
            CgSymbol *sym = &s->symbols[i];
            if (!sym->lifetime_marked || sym->type == NULL)
                continue;
            if (skip_alloca != NULL && sym->value == skip_alloca)
                continue;
            cg_emit_lifetime_end(ctx, sym->value, type_to_llvm(ctx, sym->type));
        }
    }
}

/* Emit a conditional struct drop call with moved/returning flag check.
   `drop_ptr` is the pointer to the struct (*Struct).
   `struct_type` is the LS Type*.
   `moved_flag` is the i1 alloca (can be NULL for unconditional drop). */
void emit_struct_drop_cond(CodegenContext *ctx, LLVMValueRef drop_ptr,
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
void emit_auto_drop_fn(CodegenContext *ctx, Type *struct_type)
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
void emit_struct_drop(CodegenContext *ctx, LLVMValueRef drop_ptr,
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
            if (field_type->kind == TYPE_STRUCT && field_type->as.strukt.has_drop)
            {
                LLVMValueRef field_ptr = LLVMBuildStructGEP2(ctx->builder, llvm_struct,
                                                             drop_ptr, (unsigned)i, "drop.field");
                emit_struct_drop(ctx, field_ptr, field_type);
            }
        }
    }
}

/* Helper: Inject string field cleanup for user-defined __drop functions.
   This is called before any return (implicit or explicit) in a __drop method. */
void emit_drop_field_cleanup(CodegenContext *ctx)
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

/* True if a payload type contributes heap ownership to the enum. */
bool cg_type_owns_heap_for_enum(const Type *t)
{
    if (t == NULL) return false;
    switch (t->kind)
    {
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
void emit_drop_value(CodegenContext *ctx, LLVMValueRef place_ptr, Type *type)
{
    if (place_ptr == NULL || type == NULL) return;
    switch (type->kind)
    {
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
void emit_auto_enum_drop_fn(CodegenContext *ctx, Type *enum_type)
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
void emit_enum_drop(CodegenContext *ctx, LLVMValueRef enum_ptr, Type *enum_type)
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
            if (pt && ((pt->kind == TYPE_STRUCT && pt->as.strukt.has_drop) ||
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
            if (pt && ((pt->kind == TYPE_STRUCT && pt->as.strukt.has_drop) ||
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
