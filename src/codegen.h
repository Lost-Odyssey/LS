/* codegen.h — LLVM IR code generation interface */
#ifndef LS_CODEGEN_H
#define LS_CODEGEN_H

#include "ast.h"
#include "types.h"
#include "symtable.h"

#include <llvm-c/Core.h>
#include <llvm-c/Target.h>
#include <llvm-c/TargetMachine.h>
#include <llvm-c/Analysis.h>

/* Codegen symbol: associates a name with an LLVM alloca/global and its type */
typedef struct {
    const char *name;
    LLVMValueRef value;     /* alloca or global */
    Type *type;
    LLVMValueRef moved_flag; /* i1 flag: true if value has been moved (for struct) */
    bool is_borrowed;        /* true for vec/struct params passed by ptr — no cleanup ownership */
    bool is_mut_borrow;      /* true for &!string params: `value` is LsString* supplied by
                                caller (not a local alloca). Load/store go through the pointer,
                                scope cleanup skips it entirely. */
} CgSymbol;

/* Codegen scope: mirrors the checker's scope chain */
typedef struct CgScope {
    CgSymbol *symbols;
    int count;
    int capacity;
    struct CgScope *parent;
} CgScope;

/* Main code generation context */
typedef struct {
    LLVMContextRef context;
    LLVMModuleRef module;
    LLVMBuilderRef builder;
    LLVMTargetMachineRef target_machine;

    CgScope *current_scope;
    LLVMValueRef current_fn;        /* Function currently being compiled */
    Type *current_fn_return_type;   /* LS return type — used for return-value widening */
    LLVMBasicBlockRef break_bb;     /* break target (while/for) */
    LLVMBasicBlockRef continue_bb;  /* continue target (while/for) */
    CgScope *loop_scope;            /* scope at loop entry (for break/continue cleanup) */

    /* Struct type registry (name -> LLVMTypeRef) */
    struct { const char *name; LLVMTypeRef llvm_type; Type *ls_type; } *struct_types;
    int struct_type_count;
    int struct_type_cap;

    /* Enum type registry (mangled name -> LLVMTypeRef + LS Type).
       Layout per entry: { i8 disc, [N x i8] payload } where N = max payload size. */
    struct { const char *name; LLVMTypeRef llvm_type; Type *ls_type; int payload_bytes; } *enum_types;
    int enum_type_count;
    int enum_type_cap;

    bool had_error;
    bool extern_builtins;   /* JIT mode: declare builtins without bodies (defined elsewhere) */
    bool memcheck_enabled;  /* --memcheck: route all malloc/free through ls_mc_* tracker */

    /* The AST node currently being lowered. Set on entry to codegen_expr and
       restored on exit. Helpers (emit_string_clone_val, vec/map mallocs that
       lack a direct node) read this for memcheck site labelling. NULL means
       no current expression — use 0/0 line/col. */
    AstNode *current_node;

    /* Memcheck site dedup: keyed by "kind|file|line|col" string.
       Each LsMcSite global is a private constant {file, line, col, kind}.
       Reused so that the IR doesn't blow up with thousands of identical sites. */
    struct {
        char *key;            /* malloc'd composite key */
        LLVMValueRef site_gv; /* the LsMcSite global */
    } *mc_sites;
    int mc_site_count;
    int mc_site_cap;

    /* Temporary string slot tracking for sub-expression cleanup.
       Each string-producing expression (upper/lower/+/f-string etc.) registers its
       result alloca here. At statement boundaries, intermediates are freed and the
       top-level result is either moved to a variable or freed (for expr-stmts). */
    LLVMValueRef *temp_string_slots;
    int temp_string_count;
    int temp_string_cap;

    /* Phase B closures: monotonic counter for synthesised top-level functions
       (`__closure_<N>`) lifted from `|x| body` literals. Per-module, so AOT
       and JIT both see stable names without cross-call collisions. */
    int closure_id_counter;

    /* Phase C.5 temporary closure tracking: env_ptr values for closure
       literals appearing as rvalue expressions (e.g. function-call args
       that aren't bound to a local Block var). Flushed at statement
       boundaries via cg_flush_temps — the caller of the closure, not
       the callee, owns the env (callee Block params are borrowed). */
    LLVMValueRef *temp_block_envs;
    int temp_block_env_count;
    int temp_block_env_cap;
} CodegenContext;

/* Initialize the codegen context (creates LLVM module, target, etc.) */
void codegen_init(CodegenContext *ctx, const char *module_name);

/* Destroy the codegen context and free all LLVM resources */
void codegen_destroy(CodegenContext *ctx);

/* Forward declaration (full definition in module.h) */
struct ModuleRegistry;

/* Generate LLVM IR from a type-checked AST_PROGRAM node.
   registry may be NULL (no module support). */
int codegen_compile(CodegenContext *ctx, AstNode *ast,
                    struct ModuleRegistry *registry);

/* Emit an object file (.obj / .o) from the current module */
int codegen_emit_object(CodegenContext *ctx, const char *output_path);

/* Dump LLVM IR to stderr (for debugging) */
void codegen_dump_ir(CodegenContext *ctx);

/* Get the LLVM IR as a string (caller must LLVMDisposeMessage) */
char *codegen_get_ir(CodegenContext *ctx);

/* Lower an LS expression AST node to an LLVM value. Exposed so external
   built-in modules (builtins_math, future stdlib) can recurse on argument
   expressions without duplicating the dispatch logic. */
LLVMValueRef codegen_expr(CodegenContext *ctx, AstNode *node);

/* Map an LS Type to its LLVM type. Exposed for built-in stdlib codegen. */
LLVMTypeRef type_to_llvm(CodegenContext *ctx, Type *t);

/* LsString helpers — exposed for built-in stdlib codegen (io, future fs/...).
   ls_string_type:   the named LLVM struct {i8* data, i32 len, i32 cap}.
   ls_string_make:   build a value from {data, len, cap} components.
   ls_string_from_literal: build a static (cap=0) LsString from a C string. */
LLVMTypeRef ls_string_type(CodegenContext *ctx);
LLVMValueRef ls_string_make(CodegenContext *ctx, LLVMValueRef data,
                            LLVMValueRef len, LLVMValueRef cap);
LLVMValueRef ls_string_from_literal(CodegenContext *ctx,
                                    const char *text, const char *name);

/* Memcheck-aware allocator. When ctx->memcheck_enabled is true the call
   routes through ls_mc_alloc with a fresh LsMcSite global tagged with
   `kind` + line/col; otherwise it emits plain malloc(size). Exposed for
   built-in stdlib codegen (io / fs / ...) to label their allocations. */
LLVMValueRef cg_emit_alloc(CodegenContext *ctx, LLVMValueRef size,
                           const char *kind, int line, int col);

#endif /* LS_CODEGEN_H */
