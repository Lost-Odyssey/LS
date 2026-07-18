/* codegen_call.c — call-expression lowering, moved verbatim out of
   codegen_expr.c (Task 7.4/7.6, Batch 7). cg_expr_call is the entry
   (dispatched from codegen_expr): intrinsic/@-builtin dispatch first, then
   cg_expr_call_main resolves the callee (method / free fn / generic /
   module-qualified / indirect) and builds the argument list (sret, self,
   borrow ABI, ownership policy for owned struct/enum args, widening). */
#include "codegen.h"
#include "codegen_internal.h"
#include "block_protocol.h"
#define LS_INCLUDE_CODEGEN 1
#include "builtins_math.h"
#include "builtins_perf.h"
#include "builtins_intrinsic_cg.h"
#include "mangle.h"
#include "common.h"

#include <llvm-c/Core.h>

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static LLVMValueRef codegen_errno_call(CodegenContext *ctx);
static LLVMValueRef codegen_from_cstr(CodegenContext *ctx, AstNode *node);
static LLVMValueRef cg_expr_call_main(CodegenContext *ctx, AstNode *node);

static bool cg_is_intrinsic(const char *name, const char *canon, const char *legacy) {
    return name != NULL &&
           (strcmp(name, canon) == 0 || strcmp(name, legacy) == 0);
}


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
    LLVMValueRef empty_str = cg_make_str(ctx, str_t, empty_data,
                                         LLVMConstInt(i32_t, 0, 0),
                                         LLVMConstInt(i32_t, 0, 0));
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

    LLVMValueRef ok_str = cg_make_str(ctx, str_t, buf, len32, cap32);
    LLVMBuildBr(ctx->builder, cont_bb);

    /* phi the two paths. The result is an owned has_drop Str rvalue — the
       generic struct rvalue protocol (var-decl transfer / call-arg spill /
       expr-stmt drop) takes it from here. */
    LLVMPositionBuilderAtEnd(ctx->builder, cont_bb);
    LLVMValueRef phi = LLVMBuildPhi(ctx->builder, str_t, "fromcstr.r");
    LLVMValueRef vals[2] = { empty_str, ok_str };
    LLVMBasicBlockRef blks[2] = { null_bb, ok_bb };
    LLVMAddIncoming(phi, vals, blks, 2);
    (void)i8_t;
    return phi;
}

LLVMValueRef codegen_expr_or_borrow(CodegenContext *ctx, AstNode *node)
{
    return codegen_expr(ctx, node);
}


static LLVMValueRef cg_expr_call_main(CodegenContext *ctx, AstNode *node)
{
    /* Detect struct method calls: obj.method(args) or StructName.method(args).
       The checker has already validated and set resolved_type on the callee. */
    bool cg_is_method_call = false; /* instance method: prepend self */
    if (node->as.call.callee->kind == AST_FIELD)
    {
        AstNode *obj_node = node->as.call.callee->as.field_access.object;
        Type *obj_type = obj_node->resolved_type;
        if (obj_type)
        {
            /* Auto-deref a pointer (*T) or reference (&T / &!T) receiver to
               its pointee struct/enum so a method call whose receiver is a
               borrow-returning call result — `v.get_ref(i).eq?(x)` where
               get_ref returns &T — dispatches as an instance method (self
               prepended / qualified symbol resolved). Mirrors the checker. */
            Type *deref = obj_type;
            if ((deref->kind == TYPE_POINTER ||
                 deref->kind == TYPE_REFERENCE) && deref->as.pointer_to &&
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
               Only for known primitive scalar types — not modules, enums, etc.
               (Includes the sized int / f32 scalars so e.g. an i16 receiver's
               .show()/.to_value() prepends self like int's does.) */
            else if (deref->kind == TYPE_INT  || deref->kind == TYPE_I64 ||
                     deref->kind == TYPE_F64  || deref->kind == TYPE_BOOL ||
                     deref->kind == TYPE_CHAR ||
                     deref->kind == TYPE_I8   || deref->kind == TYPE_I16 ||
                     deref->kind == TYPE_I32  || deref->kind == TYPE_U8 ||
                     deref->kind == TYPE_U16  || deref->kind == TYPE_U32 ||
                     deref->kind == TYPE_U64  || deref->kind == TYPE_F32)
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
            /* Auto-deref a pointer (*T) or reference (&T / &!T) receiver to
               its pointee struct/enum so a method call whose receiver is a
               borrow-returning call result — `v.get_ref(i).eq?(x)` where
               get_ref returns &T — dispatches as an instance method (self
               prepended / qualified symbol resolved). Mirrors the checker. */
            Type *deref = obj_type;
            if ((deref->kind == TYPE_POINTER ||
                 deref->kind == TYPE_REFERENCE) && deref->as.pointer_to &&
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
                case TYPE_I8:    bname = "i8";     break;
                case TYPE_I16:   bname = "i16";    break;
                case TYPE_I32:   bname = "i32";    break;
                case TYPE_U8:    bname = "u8";     break;
                case TYPE_U16:   bname = "u16";    break;
                case TYPE_U32:   bname = "u32";    break;
                case TYPE_U64:   bname = "u64";    break;
                case TYPE_F32:   bname = "f32";    break;
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
               like obj.map(string)(...)), append them: StructName.method(type)
               L-002: an interface-qualified call to a CONTENDED method carries
               node.qualified_iface — emit `StructName.<Iface>.method` to match
               the disambiguated symbol from codegen_impl_trait_decl. (Non-
               contended qualified calls have qualified_iface==NULL → plain.) */
            /* Task 7.2: the def sites (codegen_impl_decl / checker pending
               queue) now emit EXACT symbols of any length; this use-site
               buffer stays fixed, so a symbol that does not fit must be a
               loud fatal error, not a silent truncation that then misses
               LLVMGetNamedFunction (mismatched def/use). Same discipline as
               link_cmd_build's truncation checks (Batch 5.3). The guards
               also keep npos from running past the buffer into the later
               `qualified_name + npos` appends (size_t underflow → OOB). */
            static char qualified_name[512];
            int npos;
#define QNAME_GUARD()                                                        \
            do {                                                             \
                if (npos >= (int)sizeof(qualified_name)) {                   \
                    cg_error(ctx, node->line, node->column,                  \
                             "method symbol too long (%d bytes, max %d): "   \
                             "'%.128s...'", npos,                            \
                             (int)sizeof(qualified_name) - 1,                \
                             qualified_name);                                \
                    return NULL;                                             \
                }                                                            \
            } while (0)
            if (node->as.call.qualified_iface)
                npos = snprintf(qualified_name, sizeof(qualified_name), "%s.%s.%s",
                                struct_name ? struct_name : "",
                                node->as.call.qualified_iface, method_name);
            else
                npos = snprintf(qualified_name, sizeof(qualified_name), "%s.%s",
                                struct_name ? struct_name : "", method_name);
            QNAME_GUARD();
            if (node->as.call.resolved_type_args)
            {
                /* Prefer the checker's resolved method-level type-arg names
                   (concrete, alias-resolved): closure-inferred calls
                   (`v.map(|x| x+1)`) carry no type_args, AND an explicit call
                   with an abstract type param inside a generic body
                   (`self.conv(T)(..)`) needs this too — codegen has no alias
                   context, so re-mangling the raw TypeNode would emit the
                   abstract `Type.conv(T)` instead of `Type.conv(int)`. */
                npos += snprintf(qualified_name + npos, sizeof(qualified_name) - (size_t)npos,
                                 "(%s)", node->as.call.resolved_type_args);
                QNAME_GUARD();
            }
            else if (node->as.call.type_arg_count > 0)
            {
                npos += snprintf(qualified_name + npos, sizeof(qualified_name) - (size_t)npos, "(");
                QNAME_GUARD();
                for (int ti = 0; ti < node->as.call.type_arg_count; ti++)
                {
                    if (ti > 0)
                    {
                        npos += snprintf(qualified_name + npos, sizeof(qualified_name) - (size_t)npos, ",");
                        QNAME_GUARD();
                    }
                    cg_append_type_node_name(node->as.call.type_args[ti],
                                             qualified_name, &npos, (int)sizeof(qualified_name));
                    QNAME_GUARD();
                }
                snprintf(qualified_name + npos, sizeof(qualified_name) - (size_t)npos, ")");
                npos += 1; /* the ')' the line above wrote (or tried to) */
                QNAME_GUARD();
            }
#undef QNAME_GUARD
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

            /* G2: generic function call — look up by mangled name.
               Explicit type args (`identity(int)(42)`) build the suffix from
               node->type_args; inferred calls (`to_csv(p)`, Gap 1) carry no
               type_args — the checker stashed the resolved type-arg names in
               resolved_type_args (same mechanism as method-generic inference). */
            if (node->as.call.type_arg_count > 0 ||
                node->as.call.resolved_type_args != NULL)
            {
                static char g2_mangled[512];
                int pos = snprintf(g2_mangled, sizeof(g2_mangled), "%s(", fn_name);
                /* Prefer the checker's resolved_type_args (concrete, alias-
                   resolved, type_name-built — byte-identical to the symbol the
                   checker registered). Re-mangling from the raw TypeNodes is the
                   legacy fallback only when resolved_type_args is absent: codegen
                   has no type-alias context, so a `make(T)(..)` call inside a
                   generic body would otherwise emit the abstract `make(T)`
                   instead of the instantiated `make(int)`. */
                if (node->as.call.resolved_type_args != NULL)
                {
                    pos += snprintf(g2_mangled + pos, sizeof(g2_mangled) - (size_t)pos,
                                    "%s", node->as.call.resolved_type_args);
                }
                else
                {
                    for (int ti = 0; ti < node->as.call.type_arg_count; ti++)
                    {
                        if (ti > 0) g2_mangled[pos++] = ',';
                        cg_append_type_node_name(node->as.call.type_args[ti],
                                                 g2_mangled, &pos, (int)sizeof(g2_mangled));
                    }
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
                node->as.call.resolved_type_args == NULL &&
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
            if (callee == NULL && (node->as.call.type_arg_count > 0 ||
                                   node->as.call.resolved_type_args != NULL))
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
                strcmp(mod_t->as.module.name, "std.core.math") == 0)
            {
                /* Primitive (sqrt/sin/abs/...) → intrinsic/libm emit. The
                   LS-derived helpers merged into math (radians/degrees/...)
                   are NOT in the builtin table, so emit returns NULL — fall
                   through to the normal module-fn symbol path
                   (std_core_math__<fn>), which codegen emitted for the
                   lib/std/core/math.ls module (registered "std.core.math"). */
                LLVMValueRef mv = builtin_math_emit_call(ctx, fn_name,
                                              node->as.call.args,
                                              node->as.call.arg_count);
                if (mv != NULL) return mv;
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

            /* __ls_bytecopy → llvm.memcpy (form-③ primitive; the runtime
               C helper is exactly one memcpy, so this lowering is
               semantics-preserving — including the `n <= 0` guard:
               llvm.memcpy with len 0 is a DEFINED no-op even for null /
               dangling pointers since LLVM 12, and the GEPs below are
               deliberately non-inbounds so a nil base + 0 offset stays
               well-defined). Overlapping ranges were already UB in the C
               helper (plain memcpy); the no-overlap contract is now
               documented on the extern decl in std/sys/c.lls. Inlining
               the copy makes it transparent to LLVM: constant lengths
               fold to loads/stores, len==0 calls vanish, and
               DSE/memcpyopt/heap-elision can see through Str building
               (an unobserved build+free chain folds away entirely).
               Only the real std.sys.c module owns this symbol (`__ls_`
               names are reserved for the runtime). LS_NO_MEMCPY_PRIM=1
               falls back to the extern call (A/B + escape hatch). */
            if (strcmp(fn_name, "__ls_bytecopy") == 0 &&
                mod_t->as.module.name &&
                strcmp(mod_t->as.module.name, "std.sys.c") == 0 &&
                node->as.call.arg_count == 5 &&
                builtin_intrinsic_bytecopy_enabled())
            {
                return builtin_intrinsic_emit_call(ctx, fn_name, node);
            }

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
            LLVMValueRef arg_val = NULL;
            /* P3 (block-refcount): floor for claiming this arg's Block rvalue
               temp when it is moved into a container/worker (F5 below). */
            int arg_drop_floor = ctx->temp_drop_count;
            /* Read-only &T borrow of a stable place (struct field / array or
               *T element): take its lvalue address directly instead of
               codegen_expr'ing a by-value CLONE. The §13 amp-strip turned an
               explicit `&d.field` into the bare place `d.field` (and bare
               `d.field` against a `&T` param auto-borrows the same way);
               reading a has_drop struct/enum field by value deep-clones it
               (emit_struct_clone_val at the AST_FIELD read site), and that
               clone — registered for drop by the struct/enum arg fixup below —
               is flushed at the loop-enclosing scope, so in a loop only the
               last iteration's clone is freed and the earlier ones leak.
               Borrowing in place via GEP avoids the clone entirely; the callee
               only reads through the &T. codegen_lvalue_ptr returns NULL for
               non-lvalue roots (e.g. `make().field`) and Vec `v[i]` (no stable
               address), falling back to the clone path. AST_IDENT (`&d` whole
               struct) is already clean — its codegen_expr is a plain load (DCEs)
               and the fixup substitutes sym->value — so it is left untouched.
               Mirrors codegen_block_call's &T field/element borrow. */
            bool arg_inplace_borrow = false;
            {
                int pslot = i + arg_offset - sret_off;
                Type *pt = (callee_fn_lst && pslot >= 0 &&
                            pslot < callee_fn_lst->as.function.param_count)
                           ? callee_fn_lst->as.function.params[pslot] : NULL;
                AstNode *an = node->as.call.args[i];
                if (pt && pt->kind == TYPE_REFERENCE && !pt->is_mut &&
                    pt->as.pointer_to &&
                    (pt->as.pointer_to->kind == TYPE_STRUCT ||
                     pt->as.pointer_to->kind == TYPE_ENUM) &&
                    (an->kind == AST_FIELD || an->kind == AST_INDEX))
                {
                    LLVMValueRef addr = codegen_lvalue_ptr(ctx, an);
                    if (addr != NULL)
                    {
                        arg_val = addr;
                        arg_inplace_borrow = true;
                    }
                }
            }
            if (!arg_inplace_borrow)
                arg_val = codegen_expr(ctx, node->as.call.args[i]);
            if (arg_val == NULL)
            {
                free(args);
                return NULL;
            }
            /* In-place borrow already produced the pointer ABI value: skip the
               numeric widening + by-value ownership-clone policy below (none
               apply to a borrowed pointer) and the struct/enum fixup pass (the
               guard there sees args[i] is already a pointer). */
            if (arg_inplace_borrow)
            {
                args[i + arg_offset] = arg_val;
                continue;
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
               a value-ABI fn: callee marks its vec/map param CG_BORROWED
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
                        else if (unwrapped->moved_out &&
                                 cg_invalidate_moved_source(ctx, raw, arg_type))
                        {
                            /* A1 clone-elision: the checker's last-use pass
                               proved this argument is the variable's final
                               use — transfer the heap instead of cloning.
                               cg_invalidate_moved_source suppressed the
                               caller-side scope drop; when it can't (borrow /
                               no moved_flag) it returns false and the clone
                               below keeps the old behavior (§3.2 safety net). */
#if CG_DEBUG
                            cg_emit_debug_printf(ctx, "[cg] elide.arg.move struct\n",
                                                 NULL, 0);
#endif
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
                        else if (unwrapped->moved_out &&
                                 cg_invalidate_moved_source(ctx, raw, arg_type))
                        {
                            /* A1 clone-elision: last use — move, not clone
                               (mirrors the struct branch above; §3.2 safety
                               net falls back to the clone when the source
                               can't be invalidated). */
#if CG_DEBUG
                            cg_emit_debug_printf(ctx, "[cg] elide.arg.move enum\n",
                                                 NULL, 0);
#endif
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
                /* Name registry: block_protocol.h (single authority,
                   incl. the fc741bf _insert_no_grow rehash rationale). */
                bool stores = cg_block_method_is_store_sink(mname);
                /* std.task: `t.run(|| ..)` forwards the closure into the worker
                   thread (via __task_spawn), which frees the env once the body
                   runs — so the caller must NOT also free it. Consume the temp
                   env here, but ONLY for a Task receiver, so an unrelated method
                   named `run` that merely BORROWS a closure is unaffected. */
                if (!stores && mname && strcmp(mname, "run") == 0)
                {
                    AstNode *recv = node->as.call.callee->as.field_access.object;
                    Type *rt = recv ? recv->resolved_type : NULL;
                    if (rt && rt->kind == TYPE_STRUCT &&
                        rt->as.strukt.generic_base &&
                        strcmp(rt->as.strukt.generic_base, "Task") == 0)
                        stores = true;
                }
                if (stores)
                {
                    AstNode *raw = node->as.call.args[i];
                    AstNode *uw = ast_unwrap_move(raw);
                    if (uw && uw->kind == AST_IDENT)
                    {
                        CgSymbol *bsym = cg_scope_resolve(ctx->current_scope,
                                                          uw->as.ident.name);
                        if (bsym && bsym->no_drop_reason == CG_OWNED)
                            cg_null_block_env(ctx, bsym->value);
                    }
                    /* Fresh rvalue arg moved into the container/worker: claim
                       its temp_drop entry (factory / force-unwrap / closure
                       literal — unified ledger, stage 10) so the caller's
                       statement flush doesn't free the env the container now
                       owns. */
                    else
                        cg_claim_block_temp_above(ctx, arg_drop_floor);
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
                /* Already a pointer: an in-place &T borrow whose lvalue address
                   was taken in the arg loop above (struct/enum field/element).
                   A by-value struct/enum arg is always an LLVM aggregate value
                   here, never a pointer, so this only skips the pre-lowered
                   borrows — storing the pointer into a struct temp would be a
                   type mismatch. */
                if (LLVMGetTypeKind(LLVMTypeOf(args[i])) == LLVMPointerTypeKind)
                    continue;
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
                /* Fallback: materialise a temporary alloca to stabilise addr.
                   Phase B: an owned rvalue (non-IDENT) passed as &T — the
                   callee borrows but doesn't own, so spill + register the
                   temp for drop (heap released after the statement). The
                   spill itself is borrow-ABI plumbing needed for POD too;
                   only the owned-rvalue branch is an ownership operation. */
                const char *tmp_name = (arg_type->kind == TYPE_ENUM)
                                           ? "enum.borrow.tmp"
                                           : "struct.borrow.tmp";
                bool owned_rvalue_arg = (a->kind != AST_IDENT) &&
                    ((arg_type->kind == TYPE_ENUM   && arg_type->as.enom.has_drop) ||
                     (arg_type->kind == TYPE_STRUCT && arg_type->as.strukt.has_drop));
                LLVMValueRef tmp;
                if (owned_rvalue_arg)
                    tmp = cg_spill_owned_rvalue(ctx, args[i], arg_type,
                                                false, tmp_name);
                else
                {
                    tmp = cg_entry_alloca(ctx, type_to_llvm(ctx, arg_type),
                                          tmp_name);
                    LLVMBuildStore(ctx->builder, args[i], tmp);
                }
                args[i] = tmp;
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
    /* P3 (block-refcount): a factory call `make()->Block` returns an OWNED
       env — track it as a statement-level temp so a discarded `make()()` is
       released at flush and a binding claims it (cg_claim_block_temp_above).
       Container readers that return a borrowed alias (Vec.get! loads
       self.data[i] without cloning) are excluded by cg_block_source_is_aliased;
       the helper is a no-op for non-Block returns. */
    if (!cg_block_source_is_aliased(node))
        cg_track_block_rvalue(ctx, result, node->resolved_type);
    return result;
}

LLVMValueRef cg_expr_call(CodegenContext *ctx, AstNode *node)
{
    /* Slice builtin `s.len()` — extract field 1 of the {ptr,len} view,
       truncated to i32 (LS `int`). */
    if (node->as.call.callee && node->as.call.callee->kind == AST_FIELD &&
        node->as.call.callee->as.field_access.field &&
        strcmp(node->as.call.callee->as.field_access.field, "len") == 0 &&
        node->as.call.callee->as.field_access.object->resolved_type &&
        node->as.call.callee->as.field_access.object->resolved_type->kind == TYPE_SLICE)
    {
        LLVMValueRef sv = codegen_expr(ctx, node->as.call.callee->as.field_access.object);
        LLVMValueRef len64 = LLVMBuildExtractValue(ctx->builder, sv, 1, "slen");
        return LLVMBuildTrunc(ctx->builder, len64,
                              LLVMInt32TypeInContext(ctx->context), "slen32");
    }

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
        cg_is_intrinsic(node->as.call.callee->as.ident.name, "@move", "__move") &&
        node->as.call.arg_count == 1)
    {
        return codegen_expr(ctx, node->as.call.args[0]);
    }

    /* Intercept @print(...) calls (callee IDENT "@print") — inline printf
       to the current sink stream with type-aware format. */
    if (node->as.call.callee->kind == AST_IDENT && strcmp(node->as.call.callee->as.ident.name, "@print") == 0)
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

    /* Structured concurrency (generic Task(T)):
         __task_spawn(Block()->T, *T box) -> object
         __task_join(object)              -> void
       __task_spawn extracts the closure's {code_fn, env}, synthesises a
       per-T `thunk` that calls the closure and stores its by-value result
       into the `*T box` slot, then hands {thunk, fn, env, box} to the
       worker. It MOVES the env into the worker (suppresses the caller-scope
       env drop — the worker frees it once after the body runs, see
       ls_thread_trampoline). This is the whole point: a Vec move-captured
       into the closure is dropped exactly once, by the worker; the spawning
       scope already marked its source MOVED. The closure returns T by value
       (LLVM handles sret/register ABI), so `*box = closure(env)` is uniform
       over POD and aggregate, and the store IS the move (no clone, no drop).
       The runtime never touches the result bytes — single owner across the
       boundary; join() moves it out via __take. */
    if (node->as.call.callee->kind == AST_IDENT &&
        strcmp(node->as.call.callee->as.ident.name, "__task_spawn") == 0 &&
        node->as.call.arg_count == 2)
    {
        AstNode *blk = node->as.call.args[0];
        AstNode *boxarg = node->as.call.args[1];
        /* T = the Block's return type (checker validated arg0 is a Block). */
        Type *blk_t = blk->resolved_type;
        if (blk_t == NULL || blk_t->kind != TYPE_BLOCK)
        {
            cg_error(ctx, node->line, node->column,
                     "internal: __task_spawn arg0 is not a Block");
            return NULL;
        }
        LLVMTypeRef res_llvm =
            type_to_llvm(ctx, blk_t->as.function.return_type);
        int spawn_drop_floor = ctx->temp_drop_count;
        LLVMValueRef closure_val = codegen_expr(ctx, blk);
        if (closure_val == NULL) return NULL;
        LLVMValueRef fn_ptr  = LLVMBuildExtractValue(ctx->builder, closure_val, 0, "task.fn");
        LLVMValueRef env_ptr = LLVMBuildExtractValue(ctx->builder, closure_val, 1, "task.env");
        LLVMValueRef box_ptr = codegen_expr(ctx, boxarg);
        if (box_ptr == NULL) return NULL;
        /* Move the env into the thread (mirror the container-store Block
           handling): a named Block var is nulled; a literal's temp env is
           consumed so the caller scope does not also free it. */
        if (blk->kind == AST_IDENT)
        {
            CgSymbol *bsym = cg_scope_resolve(ctx->current_scope, blk->as.ident.name);
            if (bsym && bsym->no_drop_reason == CG_OWNED)
                cg_null_block_env(ctx, bsym->value);
        }
        else
            /* Fresh rvalue (literal or factory, both temp_drop-tracked
               since stage 10): the thread owns the env — claim so the
               caller's statement flush doesn't release it. */
            cg_claim_block_temp_above(ctx, spawn_drop_floor);

        LLVMTypeRef ptr_t = LLVMPointerTypeInContext(ctx->context, 0);
        LLVMTypeRef void_t = LLVMVoidTypeInContext(ctx->context);

        /* Synthesise the per-T thunk:
             void __task_thunk_<id>(ptr fn, ptr env, ptr box):
                 T r = ((T(*)(ptr))fn)(env)
                 store r -> box
                 ret void
           (save/restore builder, mirroring __env_drop_<id>.) */
        int tid = ctx->closure_id_counter++;
        char thunk_name[64];
        snprintf(thunk_name, sizeof(thunk_name), "__task_thunk_%d", tid);
        LLVMTypeRef thunk_param_t[3] = { ptr_t, ptr_t, ptr_t };
        LLVMTypeRef thunk_ty = LLVMFunctionType(void_t, thunk_param_t, 3, 0);
        LLVMValueRef thunk_fn = LLVMAddFunction(ctx->module, thunk_name, thunk_ty);

        LLVMBasicBlockRef t_saved = LLVMGetInsertBlock(ctx->builder);
        LLVMBasicBlockRef t_entry =
            LLVMAppendBasicBlockInContext(ctx->context, thunk_fn, "entry");
        LLVMPositionBuilderAtEnd(ctx->builder, t_entry);
        LLVMValueRef t_fn  = LLVMGetParam(thunk_fn, 0);
        LLVMValueRef t_env = LLVMGetParam(thunk_fn, 1);
        LLVMValueRef t_box = LLVMGetParam(thunk_fn, 2);
        LLVMTypeRef clo_ty = LLVMFunctionType(res_llvm, &ptr_t, 1, 0);
        LLVMValueRef r =
            LLVMBuildCall2(ctx->builder, clo_ty, t_fn, &t_env, 1, "task.r");
        LLVMBuildStore(ctx->builder, r, t_box);   /* store IS the move */
        /* L-015 fix: drop the closure env HERE, in the worker thunk, after
           the body ran. This is the single owner of the env across the
           thread boundary (the spawning scope already suppressed its own
           env drop above). Doing it in LS-emitted code means the free goes
           through the memcheck-tracked free wrapper (ls_mc_free) — the
           runtime trampoline previously freed env with a RAW free(), which
           the tracker never saw, surfacing as a false per-spawn leak.
           The earlier rc=139 was a genuine double-free: the prior attempt
           ADDED this drop while the trampoline STILL freed env. The
           trampoline's drop+free is now removed (os_win32/os_posix). */
        cg_emit_block_env_drop(ctx, t_env);
        LLVMBuildRetVoid(ctx->builder);
        if (t_saved) LLVMPositionBuilderAtEnd(ctx->builder, t_saved);

        LLVMValueRef spawn_fn = LLVMGetNamedFunction(ctx->module, "ls_thread_spawn");
        LLVMTypeRef spawn_ty = LLVMFunctionType(
            ptr_t, (LLVMTypeRef[]){ptr_t, ptr_t, ptr_t, ptr_t}, 4, 0);
        if (spawn_fn == NULL)
            spawn_fn = LLVMAddFunction(ctx->module, "ls_thread_spawn", spawn_ty);
        LLVMValueRef sargs[4] = { thunk_fn, fn_ptr, env_ptr, box_ptr };
        return LLVMBuildCall2(ctx->builder, spawn_ty, spawn_fn, sargs, 4, "task.handle");
    }
    if (node->as.call.callee->kind == AST_IDENT &&
        strcmp(node->as.call.callee->as.ident.name, "__task_join") == 0 &&
        node->as.call.arg_count == 1)
    {
        LLVMValueRef h = codegen_expr(ctx, node->as.call.args[0]);
        if (h == NULL) return NULL;
        LLVMTypeRef ptr_t = LLVMPointerTypeInContext(ctx->context, 0);
        LLVMTypeRef void_t = LLVMVoidTypeInContext(ctx->context);
        LLVMValueRef join_fn = LLVMGetNamedFunction(ctx->module, "ls_thread_join");
        LLVMTypeRef join_ty = LLVMFunctionType(void_t, &ptr_t, 1, 0);
        if (join_fn == NULL)
            join_fn = LLVMAddFunction(ctx->module, "ls_thread_join", join_ty);
        LLVMBuildCall2(ctx->builder, join_ty, join_fn, &h, 1, "");
        return NULL; /* void */
    }

    /* Compiler intrinsic families — sync (__mutex_/__rwlock_/__cond_/
       __cpu_*, std.sync), __atomic_* (std.atomic) and __simd_* — dispatch
       through the name-keyed registry in builtins_intrinsic_cg.c (S2).
       A family-prefixed name is ALWAYS consumed here: unknown members
       produce the same "internal: unknown ... intrinsic" error the old
       inline chains did (no fall-through to user-call resolution). The
       name sets are disjoint from every other name AST_CALL tests, so
       probing all families at this position is order-equivalent to the
       retired per-family blocks. */
    if (node->as.call.callee->kind == AST_IDENT &&
        builtin_intrinsic_is_global(
            builtin_intrinsic_lookup(node->as.call.callee->as.ident.name)))
    {
        return builtin_intrinsic_emit_call(
            ctx, node->as.call.callee->as.ident.name, node);
    }

    /* Intercept __drop_at(place) — run the recursive destructor on the value
       stored at an lvalue place (raw pointer slot p[i], field, *p) WITHOUT
       freeing any backing buffer. No-op for POD. Returns void. The nested
       drop is automatic: emit_drop_value recurses (string free / vec / map /
       struct.__drop / enum.__drop), so __drop_at on a RawVec(RawVec(T)) slot
       dispatches to the inner RawVec's user __drop. */
    if (node->as.call.callee->kind == AST_IDENT &&
        cg_is_intrinsic(node->as.call.callee->as.ident.name, "@dispose", "__drop_at") &&
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
        cg_is_intrinsic(node->as.call.callee->as.ident.name, "@take", "__take") &&
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

    /* Intercept __dup(place) — DEEP COPY without consuming: load the value at
       the place and run it through emit_clone_value (POD → the loaded value
       verbatim; has_drop → a deep clone via __clone). The source place is
       untouched (stays live). The clone counterpart of __take; the generic
       value-duplication primitive behind Vec.fill / Map.get_or_insert. */
    if (node->as.call.callee->kind == AST_IDENT &&
        cg_is_intrinsic(node->as.call.callee->as.ident.name, "@dup", "__dup") &&
        node->as.call.arg_count == 1)
    {
        AstNode *place = node->as.call.args[0];
        LLVMValueRef ptr = codegen_lvalue_ptr(ctx, place);
        if (ptr == NULL)
        {
            cg_error(ctx, node->line, node->column,
                     "__dup: argument is not an addressable place");
            return NULL;
        }
        Type *et = place->resolved_type;
        LLVMTypeRef elt = type_to_llvm(ctx, et);
        LLVMValueRef loaded = LLVMBuildLoad2(ctx->builder, elt, ptr, "dup.src");
        /* TYPE_BLOCK: emit_clone_value is INTENTIONALLY shallow for closures
           (the aliasing pass-through protocol container reads rely on, see
           match_codegen_guide §4B boundary A). @dup's contract is an owned,
           independent duplicate, so deep-clone the env here explicitly —
           without this, Vec(Block).copy / @dup(Block) share the env and
           double-free (§7.A). Must NOT touch emit_clone_value (would break
           container element reads). */
        if (et && et->kind == TYPE_BLOCK)
            return cg_emit_block_env_clone(ctx, loaded);
        return emit_clone_value(ctx, loaded, elt, et);
    }

    /* __rawstr("literal") -> *u8 : emit the literal's baked .rodata pointer
       directly (the same i8* Str's .data would hold), without constructing a
       Str. Used by std.core.reflect_core. */
    if (node->as.call.callee->kind == AST_IDENT &&
        strcmp(node->as.call.callee->as.ident.name, "__rawstr") == 0 &&
        node->as.call.arg_count == 1 &&
        node->as.call.args[0]->kind == AST_STRING_LIT)
    {
        const char *text = node->as.call.args[0]->as.string_lit.value;
        return LLVMBuildGlobalStringPtr(ctx->builder, text ? text : "", ".ls.rawstr");
    }

    /* __atomic_* — migrated to builtins_intrinsic_cg.c (registry probe
       above, S2 P1 2/4). */

    /* __simd_* — migrated to builtins_intrinsic_cg.c (registry probe
       above, S2 P1 3/4). */

    return cg_expr_call_main(ctx, node);
}
