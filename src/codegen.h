/* codegen.h — LLVM IR code generation interface */
#ifndef LS_CODEGEN_H
#define LS_CODEGEN_H

#include "ast.h"
#include "types.h"
#include "symtable.h"
#include "optpipe.h"

#include <llvm-c/Core.h>
#include <llvm-c/Target.h>
#include <llvm-c/TargetMachine.h>
#include <llvm-c/Analysis.h>
#include <llvm-c/DebugInfo.h>

/* Stage 11 (OWN-4): WHY scope cleanup must not drop a symbol. One bool
   (`is_borrowed`) used to encode three meanings; the DROP end treats every
   non-OWNED value identically (skip), but read ends that need the reason
   (cg_match_arm_own_tail clone dispatch, cg_store_owned move-vs-clone)
   branch on the specific value. Keep CG_OWNED == 0 so zero-init means
   "owned, drop at scope exit". */
typedef enum {
    CG_OWNED = 0,   /* symbol owns its value — scope cleanup drops it */
    CG_BORROWED,    /* true borrow: &self / &T / &!T params, & bindings,
                       call-borrowed Block params, env-owned captures,
                       borrow-match payload binders, for-in element copies */
    CG_MOVED_OUT,   /* ownership transferred out: match binder move-out,
                       block-tail local transfer (slot lives on in temp_drop) */
    CG_ALIAS,       /* Block value aliasing container storage it does not own
                       (raw-pointer/array index read) */
} CgNoDropReason;

/* Codegen symbol: associates a name with an LLVM alloca/global and its type */
typedef struct {
    const char *name;
    LLVMValueRef value;     /* alloca or global */
    Type *type;
    LLVMValueRef moved_flag; /* i1 flag: true if value has been moved (for struct) */
    uint8_t no_drop_reason;  /* CgNoDropReason — CG_OWNED means "drop at scope exit" */
    bool is_mut_borrow;      /* true for &! params: `value` is a pointer supplied by
                                caller (not a local alloca). Load/store go through the pointer,
                                scope cleanup skips it entirely. */
    bool lifetime_marked;    /* A2: a llvm.lifetime.start was emitted for this slot at its
                                var_decl, so scope cleanup must emit the paired lifetime.end
                                after the slot's drop. Set only for AOT aggregate locals; the
                                explicit flag guarantees start/end pairing. */
} CgSymbol;

/* Codegen scope: mirrors the checker's scope chain */
typedef struct CgScope {
    CgSymbol *symbols;
    int count;
    int capacity;
    struct CgScope *parent;
} CgScope;

/* CgCompileMode — which artifact this CodegenContext is emitting.
 *
 * Probe result (S2 Phase 0, 2026-07-05): the two legacy bools `aot_entry` and
 * `extern_builtins` were ONE dimension, not two orthogonal flags. Across all
 * five context-creation sites (main.c cmd_compile / cmd_emit_ir / cmd_ir_asm,
 * jit.c __builtins bootstrap / jit.c user module) only three combinations were
 * ever realized, and (aot_entry && extern_builtins) was impossible:
 *
 *   (aot_entry, extern_builtins)  creator                       -> mode
 *   (true,  false)                main.c compile/emit-ir/ir·asm -> CG_MODE_AOT
 *   (false, true)                 jit.c user module (run/REPL)  -> CG_MODE_JIT_USER
 *   (false, false)                jit.c "__builtins" bootstrap  -> CG_MODE_JIT_BUILTINS
 *
 * Real semantics of the two retired flags, preserved by the predicates below:
 *   - aot_entry ("this module is the final AOT artifact whose main() is the
 *     process entry point"): C-signature int main(argc,argv) + __ls_set_args
 *     forwarding (bug #22, 3 sites), __ls_flush_out injection before every ret
 *     (stdout-flake fix), internalize+GlobalDCE (A5). == cg_mode_is_aot().
 *   - extern_builtins ("builtin/runtime helper BODIES live elsewhere — the JIT
 *     __builtins module / AbsoluteSymbols — so declare, don't define"): skip
 *     inline helper bodies unless memcheck needs a tracked local copy; also
 *     gates OFF A2 lifetime markers (JIT slots span snippet boundaries).
 *     == cg_mode_builtins_extern().
 *
 * memcheck_enabled / profile_enabled are genuinely orthogonal (both combine
 * with AOT and JIT) and stay as independent bools.
 *
 * CG_MODE_JIT_BUILTINS is deliberately the zero value: the jit.c bootstrap
 * context is memset(0)-initialized, and a zeroed context must keep today's
 * default semantics (both legacy flags false). */
typedef enum {
    CG_MODE_JIT_BUILTINS = 0, /* JIT bootstrap "__builtins" module: defines the
                                 builtin bodies every later module resolves against */
    CG_MODE_JIT_USER,         /* JIT / REPL user module: builtins declared extern */
    CG_MODE_AOT,              /* standalone AOT artifact (compile / emit-ir / ir·asm) */
} CgCompileMode;

/* Main code generation context */
typedef struct {
    LLVMContextRef context;
    LLVMModuleRef module;
    LLVMBuilderRef builder;
    LLVMTargetMachineRef target_machine;
    LsOptConfig opt;                /* AOT: optimization level + target CPU for emit */

    CgScope *current_scope;
    LLVMValueRef current_fn;        /* Function currently being compiled */
    Type *current_fn_return_type;   /* LS return type — used for return-value widening */
    bool is_main_void;              /* true when compiling def main() — AOT needs ret i32 0 */
    LLVMBasicBlockRef break_bb;     /* break target (while/for) */
    LLVMBasicBlockRef continue_bb;  /* continue target (while/for) */
    CgScope *loop_scope;            /* scope at loop entry (for break/continue cleanup) */
    int loop_temp_drop_floor;       /* temp_drop_count at loop entry (break/continue flush floor) */

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
    CgCompileMode mode;     /* which artifact this context emits (see CgCompileMode) */
    bool memcheck_enabled;  /* --memcheck: route all malloc/free through ls_mc_* tracker.
                               ORTHOGONAL to mode: real in both AOT (`compile --memcheck`,
                               the aot_mc_* tests) and JIT (`run --memcheck`). */
    bool profile_enabled;   /* --profile: inject ls_prof_enter/leave for function profiling.
                               Orthogonal to mode, same as memcheck_enabled. */

    /* The AST node currently being lowered. Set on entry to codegen_expr and
       restored on exit. Helpers (clone/drop emitters, vec/map mallocs that
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

    /* M-4.5: Temporary has_drop struct slot tracking.
       `vec[i].field` / `vec[i].method()` spill the indexed element — a *deep
       clone* the container still owns a copy of — into a temp alloca. Field
       access reads one field; the rest of that temporary struct's owned
       resources (other string fields, nested drops) would otherwise leak.
       The spill slot is registered here so cg_flush_temps drops it at the
       statement boundary. Ownership-transfer forms (`Item it = vit[0]`) take a
       different path: there codegen_expr returns the clone directly and the
       named variable's scope drop is the sole releaser, so no temp_drop is
       registered. */
    LLVMValueRef *temp_drop_slots;
    Type        **temp_drop_types;
    int           temp_drop_count;
    int           temp_drop_cap;

    /* Floor that statement-boundary flushes (cg_flush_temps) must never collapse
       below. Normally 0. A match raises it to protect its subject temp(s): the
       subject lives below the arm bodies and must survive the arm-internal
       statement flushes (an `@print` / `if` / `while` inside an arm would
       otherwise reset temp_drop_count to 0, dropping the subjects prematurely AND
       — fatally — letting later borrow-arg temps reuse the freed low slot indices,
       which a not-taken branch leaves uninitialised; the arm's encapsulate then
       restores count and re-exposes that garbage slot to the enclosing flush →
       invalid free of a wild pointer). Saved/reset to 0 at every function entry. */
    int           temp_drop_base;

    /* Phase B closures: monotonic counter for synthesised top-level functions
       (`__closure_<N>`) lifted from `|x| body` literals. Per-module, so AOT
       and JIT both see stable names without cross-call collisions. */
    int closure_id_counter;

    /* (Stage 10: the former Phase C.5 temp_block_env table — a separate SSA
       env-value ledger for closure literals — was unified into temp_drop:
       literals spill like every other fresh owned Block rvalue.) */

    /* Stage 6 (LS_OWN_AUDIT) temp-ledger oracle: stack of result_alloca slots
       of the matches currently being emitted. cg_push_temp_drop consults it
       to enforce guide 坑① — "a has_drop match result is NEVER registered as
       a temp_drop" (registration would double-free: the consumer transfers
       the result). Maintained unconditionally (two pointer writes per match);
       checked only when the audit is enabled. Depth 32 = nesting of match
       EXPRESSIONS being emitted at once, far beyond real code; overflow just
       stops pushing (audit degrades, never corrupts). */
    LLVMValueRef own_audit_match_res[32];
    int own_audit_match_res_depth;

    /* G1.5: Pending generic method instantiations from checker.
       Set by caller before codegen_compile; processed during Pass 2a. */
    struct {
        AstNode *cloned_fn;     /* owned cloned fn_decl */
        char    *mangled_name;  /* "Pair(int,string).get_first" */
        Type    *struct_type;   /* instantiated struct Type */
    } *pending_generic_methods;
    int pending_gm_count;

    /* L-009: name mangling. When emitting functions that belong to an imported
       `module X`, this holds that module's path (e.g. "io", "std.json"); NULL
       while emitting root/main-file functions. Definition + call sites consult
       it via cg_module_fn_symbol so that same-named functions in different
       modules get distinct LLVM symbols (root functions stay unmangled). */
    const char *current_emit_module;

    /* D1 debug info (docs/plan_debug_info.md phase 1: line tables only).
       All of this stays NULL/false unless `-g` was passed — the default
       pipeline never touches the DIBuilder. */
    bool debug_info;              /* -g: emit line-table debug info */
    LLVMDIBuilderRef dib;         /* NULL when debug info is off */
    LLVMMetadataRef di_cu;        /* compile unit */
    LLVMMetadataRef di_file;      /* DIFile of the root source file */

    /* Source path of the module whose bodies are being emitted, so imported
       functions get a DIFile pointing at their own .lls file (NULL = root
       file). Set alongside current_emit_module in codegen_compile's Pass B
       and per pending generic method (via its defining module stamp). */
    const char *current_emit_file;
    struct { const char *path; LLVMMetadataRef file; } *di_files; /* DIFile cache */
    int di_file_count;
    int di_file_cap;
} CodegenContext;

/* Mode predicates — see the CgCompileMode probe notes above. Use these, not
   raw ctx->mode comparisons, so grep finds every mode-dependent site. */
static inline bool cg_mode_is_aot(const CodegenContext *ctx) {
    return ctx->mode == CG_MODE_AOT;
}
/* True when builtin/runtime helper bodies are defined elsewhere (JIT user
   module). Exactly the retired `extern_builtins` flag; note the JIT
   __builtins bootstrap module returns false here (it DEFINES the bodies). */
static inline bool cg_mode_builtins_extern(const CodegenContext *ctx) {
    return ctx->mode == CG_MODE_JIT_USER;
}

/* Initialize the codegen context (creates LLVM module, target, etc.) */
void codegen_init(CodegenContext *ctx, const char *module_name);

/* Forward declaration (full definition in checker.h) — pointer-only use
   below, so codegen.h need not drag the whole checker interface into every
   header that includes it (same pattern as struct ModuleRegistry). */
struct CheckerGenericMethods;

/* G1.5: hand off checker-produced pending generic method instantiations to
   a codegen context. Copies element-by-element (the two `struct { ... }
   *pending_generic_methods` array types are structurally identical but
   distinct anonymous C types, so a bulk memcpy would need a cast anyway;
   this keeps both sides honest if a field is ever added to one but not
   the other). Ownership of gm->methods transfers to ctx — this call frees
   gm->methods and resets *gm to {0} so the source is left in a safe,
   reusable state. No-op (besides zeroing *gm) when gm is NULL or empty.
   Consumers: main.c (cmd_compile / cmd_emit_ir / cmd_ir_asm, all AOT-ish
   paths) and jit.c (build_jit_module) — outside codegen.c itself, hence
   the public (not codegen_internal.h) declaration. */
void codegen_take_generic_methods(CodegenContext *ctx,
                                  struct CheckerGenericMethods *gm);

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

/* Memcheck-aware allocator. When ctx->memcheck_enabled is true the call
   routes through ls_mc_alloc with a fresh LsMcSite global tagged with
   `kind` + line/col; otherwise it emits plain malloc(size). Exposed for
   built-in stdlib codegen (io / fs / ...) to label their allocations. */
LLVMValueRef cg_emit_alloc(CodegenContext *ctx, LLVMValueRef size,
                           const char *kind, int line, int col);

#endif /* LS_CODEGEN_H */
