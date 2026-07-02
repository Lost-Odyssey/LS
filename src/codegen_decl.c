/* codegen_decl.c
   声明发射：struct/enum/impl/fn 声明 + enum ctor/payload + struct/enum LLVM 注册表 + type_to_llvm + FFI/extern ABI lowering

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
static void cg_enum_body_fields(CodegenContext *ctx, int max_payload, int max_align, LLVMTypeRef body_out[2]);
static void cg_enum_payload_dims(CodegenContext *ctx, Type *et, int *out_size, int *out_align);
static bool cg_ref_pointee_is_byval(Type *p);
static int enum_payload_bytes(CodegenContext *ctx, const char *name);
static bool extern_type_needs_lowering(CodegenContext *ctx, Type *t);
static int find_enum_idx(CodegenContext *ctx, const char *name);
static LLVMTypeRef find_enum_llvm(CodegenContext *ctx, const char *name);
static Type *find_struct_ls(CodegenContext *ctx, const char *name);
static void register_enum_llvm(CodegenContext *ctx, const char *name, LLVMTypeRef llvm_type, Type *ls_type, int payload_bytes);
static void register_struct_llvm(CodegenContext *ctx, const char *name, LLVMTypeRef llvm_type, Type *ls_type);
static LLVMTypeRef type_to_c_abi(CodegenContext *ctx, Type *t);

/* B-2: Get the LLVM-level type name for a struct/enum Type.
   For module-defined types this is the "<mod>__Name" prefixed name;
   for root-defined types it falls back to the bare name. */
const char *struct_llvm_name(const Type *t)
{
    return (t->as.strukt.llvm_name) ? t->as.strukt.llvm_name : t->as.strukt.name;
}

const char *enum_llvm_name_of(const Type *t)
{
    return (t->as.enom.llvm_name) ? t->as.enom.llvm_name : t->as.enom.name;
}

/* Append the canonical mangled name of a TypeNode to buf at *pos, matching the
   checker's type_name(resolve(tn)) so generic-call symbol lookups agree. Handles
   primitives, named generics WITH their args (Complex(f64), Vec(Complex(f64))),
   and pointers — the cases that appear as generic type arguments. */
void cg_append_type_node_name(TypeNode *tn, char *buf, int *pos, int cap)
{
    if (tn == NULL) { *pos += snprintf(buf + *pos, (size_t)(cap - *pos), "?"); return; }
    if (tn->kind == TYPE_NODE_PRIMITIVE)
    {
        const char *tname = "?";
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
        *pos += snprintf(buf + *pos, (size_t)(cap - *pos), "%s", tname);
    }
    else if (tn->kind == TYPE_NODE_NAMED)
    {
        *pos += snprintf(buf + *pos, (size_t)(cap - *pos), "%s", tn->as.named.name);
        if (tn->as.named.arg_count > 0)
        {
            *pos += snprintf(buf + *pos, (size_t)(cap - *pos), "(");
            for (int i = 0; i < tn->as.named.arg_count; i++)
            {
                if (i > 0) *pos += snprintf(buf + *pos, (size_t)(cap - *pos), ",");
                cg_append_type_node_name(tn->as.named.args[i], buf, pos, cap);
            }
            *pos += snprintf(buf + *pos, (size_t)(cap - *pos), ")");
        }
    }
    else if (tn->kind == TYPE_NODE_POINTER)
    {
        *pos += snprintf(buf + *pos, (size_t)(cap - *pos), "*");
        cg_append_type_node_name(tn->as.pointee, buf, pos, cap);
    }
    else
    {
        *pos += snprintf(buf + *pos, (size_t)(cap - *pos), "?");
    }
}

LLVMValueRef cg_declare_pending_generic_method(CodegenContext *ctx,
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

LLVMTypeRef find_struct_llvm(CodegenContext *ctx, const char *name)
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

Type *find_struct_ls_type(CodegenContext *ctx, const char *name)
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

/* A read-only `&T` reference degrades to the by-value ABI of T when T is a
   scalar (int/i64/char/bool/float/object/pointer/...): borrowing a scalar IS
   its value, and the pointer-borrow ABI is only meaningful for aggregates
   (struct/enum/array) whose buffers must not be copied. This keeps a generic
   `&K` parameter valid when K instantiates to a POD type — e.g. a Map(int,int)
   lookup whose method takes `&K k` — instead of emitting an `int*` param that
   the body then uses as an `int` value (a verifier error). &!T (writable) is
   always a pointer; this only affects read-only refs. */
static bool cg_ref_pointee_is_byval(Type *p)
{
    if (p == NULL) return false;
    switch (p->kind)
    {
    case TYPE_STRUCT:
    case TYPE_ENUM:
    case TYPE_ARRAY:
        return false;
    default:
        return true;
    }
}

LLVMTypeRef type_to_llvm(CodegenContext *ctx, Type *t)
{
    if (t == NULL)
        return LLVMVoidTypeInContext(ctx->context);

    switch (t->kind)
    {
    case TYPE_INT:
    case TYPE_I32:
    case TYPE_U32:
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
    case TYPE_F16:
        return LLVMHalfTypeInContext(ctx->context);
    case TYPE_BF16:
        return LLVMBFloatTypeInContext(ctx->context);
    case TYPE_BOOL:
        return LLVMInt1TypeInContext(ctx->context);
    case TYPE_CHAR:
        return LLVMInt32TypeInContext(ctx->context); /* char = i32 (same as int) */
    case TYPE_VOID:
        return LLVMVoidTypeInContext(ctx->context);
    case TYPE_NIL:
        return LLVMPointerTypeInContext(ctx->context, 0);
    case TYPE_OBJECT:
        return LLVMPointerTypeInContext(ctx->context, 0);
    case TYPE_POINTER:
        return LLVMPointerTypeInContext(ctx->context, 0);
    case TYPE_REFERENCE:
        /* ABI policy for reference types: aggregate (struct/enum/array)
           pointees use a uniform pointer borrow ABI; read-only references to
           a scalar pointee degrade to the scalar's by-value ABI (see
           cg_ref_pointee_is_byval — needed for generic `&K` over POD K).
           emit_scope_cleanup honours is_borrowed on the CgSymbol so
           borrowed slots are never freed. */
        if (!t->is_mut && cg_ref_pointee_is_byval(t->as.pointer_to))
            return type_to_llvm(ctx, t->as.pointer_to);
        return LLVMPointerTypeInContext(ctx->context, 0);
    case TYPE_LIB:
        return LLVMPointerTypeInContext(ctx->context, 0);
    case TYPE_ARRAY:
        return LLVMArrayType2(type_to_llvm(ctx, t->as.array.elem),
                              (unsigned)t->as.array.size);
    case TYPE_SIMD:
        /* Simd(T, N) -> <N x T> portable LLVM vector (register-resident). */
        return LLVMVectorType(type_to_llvm(ctx, t->as.simd.elem),
                              (unsigned)t->as.simd.lanes);
    case TYPE_SLICE:
    {
        /* Borrowed slice &[T] = { *T ptr; i64 len } fat pointer (non-owning). */
        LLVMTypeRef fields[2];
        fields[0] = LLVMPointerTypeInContext(ctx->context, 0);
        fields[1] = LLVMInt64TypeInContext(ctx->context);
        return LLVMStructTypeInContext(ctx->context, fields, 2, 0);
    }
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

/* #1 (docs/plan_borrow_noalias.md): attach a single enum parameter attribute. */
static void cg_add_enum_attr(CodegenContext *ctx, LLVMValueRef fn, unsigned attr_idx,
                             const char *name, uint64_t val)
{
    unsigned k = LLVMGetEnumAttributeKindForName(name, strlen(name));
    if (k == 0) return; /* unknown name (shouldn't happen for standard attrs) */
    LLVMAttributeRef a = LLVMCreateEnumAttribute(ctx->context, k, val);
    LLVMAddAttributeAtIndex(fn, attr_idx, a);
}

/* #1: stamp the LLVM-provable facts the LS borrow checker guarantees onto a
   pointer-ABI borrow parameter (docs/plan_borrow_noalias.md §3):
     - all borrows: nonnull + dereferenceable(sizeof pointee) + align
     - exclusive (&!T): noalias   (borrow checker enforces exclusivity)
     - read-only (&T):  readonly  (checker forbids writing through it)
     - nocapture: only when the function can't return a borrow (nocapture_ok)
   `llvm_idx` is the 0-based LLVM parameter position; the attribute index is
   llvm_idx+1 (index 0 = return value; LS functions carry no sret shift). */
static void cg_attach_borrow_attrs(CodegenContext *ctx, LLVMValueRef fn,
                                   unsigned llvm_idx, Type *pointee,
                                   bool is_mut, bool nocapture_ok)
{
    if (getenv("LS_NO_BORROW_ATTRS") != NULL) return; /* escape hatch */
    if (pointee == NULL) return;
    unsigned attr_idx = llvm_idx + 1;

    LLVMTypeRef pty = type_to_llvm(ctx, pointee);
    LLVMTargetDataRef td = LLVMGetModuleDataLayout(ctx->module);
    unsigned long long sz = pty ? LLVMABISizeOfType(td, pty) : 0;
    unsigned al = pty ? LLVMABIAlignmentOfType(td, pty) : 0;

    cg_add_enum_attr(ctx, fn, attr_idx, "nonnull", 0);
    if (sz > 0) cg_add_enum_attr(ctx, fn, attr_idx, "dereferenceable", sz);
    if (al > 0) cg_add_enum_attr(ctx, fn, attr_idx, "align", al);
    if (nocapture_ok) cg_add_enum_attr(ctx, fn, attr_idx, "nocapture", 0);
    if (!is_mut) cg_add_enum_attr(ctx, fn, attr_idx, "readonly", 0);
    /* NOTE: `noalias` on &!T is intentionally NOT emitted unconditionally.
       Borrow exclusivity is a single-thread property; LS's concurrency
       primitives (std.chan / std.sync Guard / Ring) deliberately share &!self
       memory across threads, where a second thread writes through a different
       pointer to the same bytes. noalias would let LLVM hoist/reorder a load
       past that cross-thread write and a spin/recv loop would deadlock
       (observed: test_chan_mpmc / test_chan_forin AOT hang). See
       docs/plan_borrow_noalias.md §3. A4 recovers it per-function for bodies
       the checker proves thread-local (docs/plan_opt_noalias_recovery.md). */
    /* A4: UNSOUND diagnostic switch — force noalias on every &!T to reproduce
       the historical cross-thread deadlock (the gold-standard sample
       noalias_guard.lls must hang/fail under it). Never set in normal use;
       exists so a future "is noalias the culprit?" bisect takes one env var. */
    if (is_mut && getenv("LS_FORCE_NOALIAS") != NULL)
        cg_add_enum_attr(ctx, fn, attr_idx, "noalias", 0);
    /* A4: mark writable borrows as noalias CANDIDATES. The recovery pass
       (codegen_noalias.c) later upgrades the marker to a real `noalias` on
       functions it proves thread-local, and strips it everywhere else. Gated
       on nocapture_ok: a function that can return a borrow keeps the derived
       pointer alive past its own frame — excluded conservatively. */
    else if (is_mut && nocapture_ok) {
        LLVMAttributeRef cand = LLVMCreateStringAttribute(
            ctx->context, "ls-noalias-cand", 15, "", 0);
        LLVMAddAttributeAtIndex(fn, attr_idx, cand);
    }
}

void codegen_fn_decl(CodegenContext *ctx, AstNode *node)
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
    /* Same isolation for M-4.5 has_drop temp slots: a function whose last
       statement early-returns from every match arm can leave registered
       temp_drop entries unflushed; without this reset the NEXT function's
       first flush emits drops referencing the previous function's allocas
       (LLVM "instruction does not dominate all uses"). Hit by the B-2
       string->Str call-arg bridge in std.json's io wrappers. */
    int saved_temp_drop_count = ctx->temp_drop_count;
    ctx->temp_drop_count = 0;
    /* A new function body starts with no enclosing temps: reset the protected
       flush floor (a match in the OUTER function emitting this one lazily must
       not leak its base into this independent body). Restored on exit. */
    int saved_temp_drop_base = ctx->temp_drop_base;
    ctx->temp_drop_base = 0;

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

    /* D1 (-g): attach the subprogram before any body instruction is emitted. */
    cg_di_fn_begin(ctx, fn, node);

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

    /* #1 borrow attrs: a borrow parameter is nocapture UNLESS the function may
       return a borrow derived from it (ret is a reference/slice — Phase-2 borrow
       elision). LS borrows can't otherwise escape (no store to field/global/
       capture), so the return value is the only escape route. */
    Type *rt_for_attrs = fn_type_ml->as.function.return_type;
    bool nocapture_ok = !(rt_for_attrs && (rt_for_attrs->kind == TYPE_REFERENCE ||
                                           rt_for_attrs->kind == TYPE_SLICE));

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
            /* #1: &self (sbk=1) is a read-only borrow, &!self (sbk=2) exclusive. */
            cg_attach_borrow_attrs(ctx, fn, 0, self_struct_type,
                                   /*is_mut=*/sbk == 2, nocapture_ok);
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
            /* #1: &!T exclusive borrow → noalias (+ nonnull/deref/align/nocapture). */
            cg_attach_borrow_attrs(ctx, fn, (unsigned)llvm_idx, param_type,
                                   /*is_mut=*/true, nocapture_ok);
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
            !param_type->is_mut &&
            !cg_ref_pointee_is_byval(param_type->as.pointer_to))
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
            /* #1: read-only &T aggregate borrow → readonly (+ nonnull/deref/
               align/nocapture). */
            cg_attach_borrow_attrs(ctx, fn, (unsigned)llvm_idx, param_type,
                                   /*is_mut=*/false, nocapture_ok);
            continue;
        }
        /* Read-only `&scalar` (a generic `&K` instantiated over POD K) uses the
           by-value ABI: unwrap to the pointee so the param is registered as a
           plain value (alloca + store below), matching type_to_llvm's scalar
           reference degrade and the call site (which passes the value, not an
           address, for non-aggregate args). */
        if (param_type && param_type->kind == TYPE_REFERENCE &&
            !param_type->is_mut)
            param_type = param_type->as.pointer_to;
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
           emit_drop_field_cleanup() may emit conditional branches,
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
    ctx->temp_drop_count = saved_temp_drop_count;
    ctx->temp_drop_base  = saved_temp_drop_base;

    /* Verify function */
    if (LLVMVerifyFunction(fn, LLVMPrintMessageAction))
    {
        fprintf(stderr, "[codegen] warning: function '%s' failed verification\n", name);
    }
}

/* Build the LLVM struct type for a single variant's payload. Self-recursive
   payload fields (where the field type is the enclosing enum) are stored as
   opaque pointers (boxed-on-heap), avoiding infinite type recursion. */
LLVMTypeRef build_variant_payload_struct(CodegenContext *ctx, Type *enum_type, int variant_idx)
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

void codegen_enum_decl(CodegenContext *ctx, AstNode *node)
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

/* Build the enum value for a variant constructor call/identifier.
   Allocates an enum struct on the stack, stores the discriminant byte at offset 0,
   and writes payload fields into the bytes-array at offset 1 via a bitcast to the
   variant's payload struct.  Self-recursive payload fields are heap-boxed via
   malloc + store, with the pointer stored into the slot. */
LLVMValueRef emit_enum_ctor(CodegenContext *ctx, AstNode *node,
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

    /* Snapshot the temp floors BEFORE evaluating any argument: the post-loop
       flush must drop only the temps this ctor's own arguments created, NOT
       temps an enclosing expression already registered. Critical for operator
       overloads — `Enum(x) == Enum(y)` spills the receiver `Enum(x)` and
       registers its drop before building the rhs `Enum(y)`; a full flush here
       would destroy that live receiver before the comparison call. */
    int enum_env_floor  = ctx->temp_block_env_count;
    int enum_drop_floor = ctx->temp_drop_count;

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
                               args[i],
                               CG_XFER_INTO_CONTAINER);
            }
            else
            {
                LLVMBuildStore(ctx->builder, v, field_ptr);
            }
        }
    }

    /* Flush temps created by argument evaluation (e.g. f-string inside
       Ok(f"got {x}")). The enum constructor clones string arguments into the
       payload, so the originals are safe to free here. Floor-based: only this
       ctor's own argument temps, preserving any enclosing-expression temps. */
    cg_flush_temps_from(ctx, enum_env_floor, enum_drop_floor);

    /* Return the loaded enum value */
    return LLVMBuildLoad2(ctx->builder, enum_llvm, ealloca, "enum.val");
}

void codegen_struct_decl(CodegenContext *ctx, AstNode *node)
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

void codegen_impl_decl(CodegenContext *ctx, AstNode *node)
{
    /* G1.5: skip generic impl templates — methods are emitted per-instantiation */
    if (node->as.impl_decl.type_param_count > 0) return;

    const char *bare_name = node->as.impl_decl.name;
    /* Phase 2.5: `impl <builtin type>` (e.g. int). Builtin types are global,
       not owned by any module — their methods use the bare name `int.hash`
       so callers in any importing file resolve the same symbol (§2.4). Skip the
       B-3 module prefixing applied to struct/enum impls. */
    bool is_builtin_impl =
        strcmp(bare_name, "int") == 0 ||
        strcmp(bare_name, "i64") == 0    || strcmp(bare_name, "f64") == 0 ||
        strcmp(bare_name, "bool") == 0   || strcmp(bare_name, "char") == 0 ||
        strcmp(bare_name, "i8") == 0     || strcmp(bare_name, "i16") == 0 ||
        strcmp(bare_name, "i32") == 0    || strcmp(bare_name, "u8") == 0 ||
        strcmp(bare_name, "u16") == 0    || strcmp(bare_name, "u32") == 0 ||
        strcmp(bare_name, "u64") == 0    || strcmp(bare_name, "f32") == 0;
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
    /* B-3 (docs/bugs_deferred_p5_4.md §B-3): a USER `impl ImportedStruct` (e.g.
       `impl Str` in the main file) is emitted with current_emit_module == NULL,
       so the branch above doesn't fire and struct_name stays bare ("Str"). But
       method DISPATCH resolves through the struct's prefixed llvm_name
       ("std_str__Str") — see the call-site resolution near line 5017. Mirror that
       here: when struct_name is still bare, look the struct/enum up by bare name
       and adopt its llvm_name so emitted symbol == dispatched symbol. */
    if (!is_builtin_impl && struct_name == bare_name)
    {
        for (int si = 0; si < ctx->struct_type_count; si++)
        {
            Type *slt = ctx->struct_types[si].ls_type;
            if (slt && slt->kind == TYPE_STRUCT && slt->as.strukt.name &&
                strcmp(slt->as.strukt.name, bare_name) == 0 &&
                slt->as.strukt.llvm_name != NULL)
            {
                struct_name = slt->as.strukt.llvm_name;
                break;
            }
        }
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

                        if (ft->kind == TYPE_STRUCT && ft->as.strukt.has_drop)
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
void codegen_impl_trait_decl(CodegenContext *ctx, AstNode *node)
{
    const char *bare_name = node->as.impl_trait_decl.struct_name;
    /* Phase 2.5 / M-H: `impl Trait for <builtin>` (e.g. `impl Hash for int`).
       Builtin types are global, not owned by any module — their methods use the
       bare name `int.hash` so callers in any importing file resolve the same
       symbol (mirrors codegen_impl_decl's is_builtin_impl). Skip B-3 prefixing. */
    bool is_builtin_impl =
        strcmp(bare_name, "int") == 0 ||
        strcmp(bare_name, "i64") == 0    || strcmp(bare_name, "f64") == 0 ||
        strcmp(bare_name, "bool") == 0   || strcmp(bare_name, "char") == 0 ||
        strcmp(bare_name, "i8") == 0     || strcmp(bare_name, "i16") == 0 ||
        strcmp(bare_name, "i32") == 0    || strcmp(bare_name, "u8") == 0 ||
        strcmp(bare_name, "u16") == 0    || strcmp(bare_name, "u32") == 0 ||
        strcmp(bare_name, "u64") == 0    || strcmp(bare_name, "f32") == 0;
    /* B-3: prefix trait impl method names for module-defined types. Default to the
       emit-module prefix (correct when this impl is in the type's OWN module). */
    char prefixed_name_buf[512];
    const char *struct_name = bare_name;
    if (!is_builtin_impl &&
        ctx->current_emit_module != NULL && ctx->current_emit_module[0] != '\0')
    {
        cg_module_fn_symbol(prefixed_name_buf, sizeof(prefixed_name_buf),
                            ctx->current_emit_module, bare_name);
        struct_name = prefixed_name_buf;
    }
    /* B-3 (generalized): a trait impl may be emitted in a DIFFERENT module than the
       one that DEFINES the target type — e.g. `methods Str: Serialize` placed in
       std.core.value, where Str is defined in std.core.str (str.ls can't host it: it
       would need to import value back = a circular import). Method DISPATCH resolves
       through the type's REAL llvm_name (struct_llvm_name), so adopt it here too,
       overriding the emit-module guess, so the emitted symbol == the dispatched
       symbol. For a same-module impl the real name equals the emit-module prefix, so
       this is a no-op there. Mirrors codegen_impl_decl's bare-name fallback, but
       unconditional (the impl's home module may differ from the type's). Struct
       targets only; enums have no cross-module trait impl today. */
    if (!is_builtin_impl)
    {
        for (int si = 0; si < ctx->struct_type_count; si++)
        {
            Type *slt = ctx->struct_types[si].ls_type;
            if (slt && slt->kind == TYPE_STRUCT && slt->as.strukt.name &&
                strcmp(slt->as.strukt.name, bare_name) == 0 &&
                slt->as.strukt.llvm_name != NULL)
            {
                struct_name = slt->as.strukt.llvm_name;
                break;
            }
        }
    }

    for (int i = 0; i < node->as.impl_trait_decl.method_count; i++)
    {
        AstNode *method = node->as.impl_trait_decl.methods[i];
        if (method->kind != AST_FN_DECL) continue;

        const char *orig_name = method->as.fn_decl.name;

        /* Qualify with struct name: "Point.to_string".
           L-002: when this interface method's name is CONTENDED on its type (the
           checker flagged it — there is also an inherent method of the same name,
           or another interface provides it), emit it under the disambiguated symbol
           "T.<Iface>.m" so the two bodies don't collide. The inherent method keeps
           "T.m" (codegen_impl_decl, unchanged); a single-provider interface method
           also keeps "T.m". Dispatch mirrors this in codegen_expr.c. */
        static char qualified_name[256];
        if (method->as.fn_decl.iface_method_contended)
            snprintf(qualified_name, sizeof(qualified_name), "%s.%s.%s",
                     struct_name, node->as.impl_trait_decl.trait_name, orig_name);
        else
            snprintf(qualified_name, sizeof(qualified_name), "%s.%s", struct_name, orig_name);
        method->as.fn_decl.name = qualified_name;

        codegen_fn_decl(ctx, method);

        /* Restore original name */
        method->as.fn_decl.name = (char *)orig_name;
    }
}

/* Map an LS type to the C ABI type (extern fn / FFI).
   (extern-C structs only; scalar/pointer types pass through type_to_llvm.) */
static LLVMTypeRef type_to_c_abi(CodegenContext *ctx, Type *t)
{
    return type_to_llvm(ctx, t);
}

/* Compute C ABI byte size of an extern struct. Returns -1 if not an extern
   struct, or 0 if the LLVM type body has not been emitted yet. */
int extern_struct_size(CodegenContext *ctx, Type *t)
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
bool extern_struct_fits_in_reg(int sz)
{
    return sz == 1 || sz == 2 || sz == 4 || sz == 8;
}

/* Map a register-passable struct size to the integer LLVM type used for
   bitcasting at the call boundary. */
LLVMTypeRef extern_struct_reg_int_type(CodegenContext *ctx, int sz)
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
LLVMTypeRef extern_fn_type(CodegenContext *ctx, Type *fn_type_ml)
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

void codegen_extern_fn(CodegenContext *ctx, AstNode *node)
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
void codegen_extern_struct_decl(CodegenContext *ctx, AstNode *node)
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

void codegen_extern_block(CodegenContext *ctx, AstNode *node)
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
void cg_predeclare_extern_structs(CodegenContext *ctx, AstNode *ast)
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
void codegen_load_lib(CodegenContext *ctx, AstNode *node)
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
void codegen_ffi_init(CodegenContext *ctx, AstNode *ast)
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
LLVMValueRef codegen_ffi_call(CodegenContext *ctx, AstNode *node)
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
            Type *arg_t = node->as.ffi_call.args[i]->resolved_type;
            param_types[i] = arg_t ? type_to_llvm(ctx, arg_t) : ptr_type;
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

void codegen_decl(CodegenContext *ctx, AstNode *node)
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
