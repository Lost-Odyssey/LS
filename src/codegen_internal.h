/* codegen_internal.h — internal forward declarations shared across the
   codegen translation units (docs/plan_codegen_split.md).

   Step 3a: every previously-static codegen helper has been given external
   linkage and a prototype here, so any codegen TU can call any helper with
   no implicit-declaration hazard. Physical splitting of function bodies into
   codegen_own.c / codegen_match.c / codegen_expr.c / codegen_stmt.c /
   codegen_decl.c / codegen_closure.c (plus the standalone passes
   codegen_noalias.c / codegen_di.c / codegen_lifetime.c) is then pure
   cut-paste. Prototypes that end up used by only one TU are trimmed back to
   static at the end of the split (plan §7).

   The public CodegenContext / codegen_* API lives in codegen.h (included
   below); this header holds only compiler-internal helper prototypes. */
#ifndef LS_CODEGEN_INTERNAL_H
#define LS_CODEGEN_INTERNAL_H

#include "codegen.h"

/* Convenience accessors for the current expr's source location. Used by
   helpers that don't have direct AST access to avoid threading nodes
   through. Falls back to 0/0 when no current node is set. */
#define CG_LINE(ctx) ((ctx)->current_node ? (ctx)->current_node->line   : 0)
#define CG_COL(ctx)  ((ctx)->current_node ? (ctx)->current_node->column : 0)

/* Unified owned-rvalue predicate (OWN-1 fix, plan_footgun_remediation §p3).
   Replaces 7 per-site inline AST-kind whitelists (and the old
   cg_is_owned_combinator_rvalue helper) that had drifted apart — every gap
   below was a REAL leak, reproduced under memcheck 2026-07-04
   (own_rvalue_sites_test.lls):

     site (pre-unification)          old whitelist                    gaps (all leaked)
     ------------------------------  -------------------------------  -----------------------
     print-Str        expr.c:1141    CALL INDEX FIELD M/FU BIN.low    TRY
     standalone f-str expr.c:1340    CALL INDEX FIELD M/FU BIN.low    TRY
     @print inline f-str expr.c:1067 CALL INDEX FIELD      BIN.low    MATCH FORCE_UNWRAP TRY
     print-struct     expr.c:1181    CALL INDEX       M/FU            FIELD BIN.low TRY
     print-enum       expr.c:1215    CALL INDEX       M/FU            FIELD BIN.low TRY
     discard stmt     stmt.c:1852    CALL             M/FU            INDEX FIELD FSTR BIN.low TRY
     chained receiver expr.c:1795    CALL FSTR        M/FU            TRY
     (M/FU = AST_MATCH + AST_FORCE_UNWRAP via cg_is_owned_combinator_rvalue)

   Membership rationale (kind layer — each kind produces a FRESH rvalue that
   no other party registered for cleanup):
     AST_CALL          by-value return is a fresh owned value
     AST_INDEX         fixed-array element read CLONES (Vec's v[i] lowers to a
                       __index CALL instead, so this member covers array(T,N))
     AST_FIELD         has_drop field read CLONES (struct: emit_struct_clone_val;
                       enum: emit_enum_clone_val, 72c3f9d) — the AST_FIELD read
                       site in codegen_expr.c
     AST_MATCH         match rvalue: arms clone/transfer into a fresh result
                       (cg_match_arm_own_tail; L-013) — also the checker
                       lowering of Option/Result combinators (unwrap_or/…)
     AST_FORCE_UNWRAP  payload moved out of the inner enum
     AST_TRY           payload moved out, exactly like force-unwrap
                       (codegen_try_expr does NOT self-register a temp) —
                       was missing from EVERY site
     AST_FORMAT_STRING fresh heap Str; codegen_format_string does NOT
                       self-register (a bare `f"…"` statement leaked)
     AST_BINARY        only when .lowered != NULL (operator overload → hidden
                       method call producing a fresh owned result)
   Anything else (bare ident, borrow deref, literal, …) reads/borrows a live
   binding — dropping it at a consumer site would double-free the source.

   Move-by-value consumers (var-decl / assign / return / by-value call arg)
   do NOT route through the consumer sites above, so this predicate never
   double-registers a value that a binding claims. */
static inline bool cg_expr_is_fresh_rvalue_kind(const AstNode *e)
{
    if (e == NULL)
        return false;
    switch (e->kind)
    {
    case AST_CALL:
    case AST_INDEX:
    case AST_FIELD:
    case AST_MATCH:
    case AST_FORCE_UNWRAP:
    case AST_TRY:
    case AST_FORMAT_STRING:
        return true;
    case AST_BINARY:
        return e->as.binary.lowered != NULL;
    default:
        return false;
    }
}

/* Type-aware layer: true = evaluating `e` produced a fresh owned value of
   type `t` that the CONSUMING site (print / discard / f-string interpolation)
   is responsible for dropping. False for non-has_drop types: a POD rvalue
   owns no heap, there is nothing to drop.

   NOTE the chained-receiver site (codegen_addr_of, expr.c) deliberately uses
   the kind layer instead: a POD rvalue receiver still needs the spill slot to
   be addressable as `self` (drop registration is separately self-filtered by
   cg_push_temp_drop). Using this type-aware layer there would return NULL for
   a POD `match`/call receiver and break compilation. */
static inline bool cg_expr_yields_owned_rvalue(const AstNode *e, const Type *t)
{
    if (t == NULL)
        return false;
    bool has_drop =
        (t->kind == TYPE_STRUCT && t->as.strukt.has_drop) ||
        (t->kind == TYPE_ENUM   && t->as.enom.has_drop);
    return has_drop && cg_expr_is_fresh_rvalue_kind(e);
}

/* match / try / force-unwrap case bodies extracted from codegen_expr (Step 4). */
LLVMValueRef codegen_match_expr(CodegenContext *ctx, AstNode *node);
LLVMValueRef codegen_try_expr(CodegenContext *ctx, AstNode *node);
LLVMValueRef codegen_force_unwrap_expr(CodegenContext *ctx, AstNode *node);

/* A2 lifetime markers (codegen_lifetime.c) — no-ops unless AOT + LS_NO_LIFETIME
   unset. cg_emit_lifetime_start returns whether it emitted, so the var_decl site
   can stamp CgSymbol.lifetime_marked for the paired end at scope cleanup. */
bool cg_emit_lifetime_start(CodegenContext *ctx, LLVMValueRef slot, LLVMTypeRef ty);
void cg_emit_lifetime_end(CodegenContext *ctx, LLVMValueRef slot, LLVMTypeRef ty);

/* D1 debug info hooks (codegen_di.c) — no-ops unless ctx->debug_info. */
void cg_di_init(CodegenContext *ctx);
void cg_di_finalize(CodegenContext *ctx);
void cg_di_fn_begin(CodegenContext *ctx, LLVMValueRef fn, AstNode *node);
void cg_di_stmt_loc(CodegenContext *ctx, AstNode *node);

/* ---- Internal codegen helper prototypes (auto-consolidated, Step 3a) ---- */
void cg_module_fn_symbol(char *out, size_t cap, const char *module_path, const char *fn);
const char *struct_llvm_name(const Type *t);
const char *enum_llvm_name_of(const Type *t);
AstNode *ast_unwrap_move(AstNode *n);
void cg_error(CodegenContext *ctx, int line, int col, const char *fmt, ...);
void cg_emit_debug_printf(CodegenContext *ctx, const char *fmt_cstr, LLVMValueRef *args, int nargs);
void cg_dbg_capture(CodegenContext *ctx, const char *name, Type *t, const char *kind);
void cg_dbg_outer_mark(CodegenContext *ctx, const char *name, const char *marker);
void cg_dbg_env_alloc(CodegenContext *ctx, int closure_id, unsigned long long size, LLVMValueRef env_ptr);
void cg_dbg_block_op(CodegenContext *ctx, const char *op, const char *label, LLVMValueRef env_ptr);
CgScope *cg_scope_new(CgScope *parent);
void cg_scope_free(CgScope *s);
CgSymbol *cg_scope_define(CgScope *s, const char *name, LLVMValueRef val, Type *type, LLVMValueRef moved_flag);
CgSymbol *cg_scope_resolve(CgScope *s, const char *name);
void push_scope(CodegenContext *ctx);
void cg_append_type_node_name(TypeNode *tn, char *buf, int *pos, int cap);
LLVMValueRef cg_declare_pending_generic_method(CodegenContext *ctx, const char *name);
void pop_scope(CodegenContext *ctx);
LLVMValueRef cg_widen(CodegenContext *ctx, LLVMValueRef val, Type *from, Type *to);
LLVMTypeRef find_struct_llvm(CodegenContext *ctx, const char *name);
Type *find_struct_ls_type(CodegenContext *ctx, const char *name);
LLVMValueRef cg_get_perf_now(CodegenContext *ctx);
LLVMValueRef cg_mc_alloc_fn(CodegenContext *ctx);
LLVMValueRef cg_mc_free_fn(CodegenContext *ctx);
void cg_emit_mc_enter(CodegenContext *ctx, const char *fn_name, const char *file, int line);
void cg_emit_mc_leave(CodegenContext *ctx);
void cg_emit_prof_enter(CodegenContext *ctx, const char *fn_name, const char *file, int line);
void cg_emit_prof_leave(CodegenContext *ctx);
LLVMValueRef cg_make_site(CodegenContext *ctx, const char *kind, int line, int col);
LLVMValueRef cg_entry_alloca(CodegenContext *ctx, LLVMTypeRef ty, const char *name);
LLVMValueRef cg_entry_alloca_zeroed(CodegenContext *ctx, LLVMTypeRef ty, const char *name);
/* Get-or-declare libc memset (ptr memset(ptr, i32, i64)); never NULL.
   Sole authority for the declaration — do NOT re-derive it via
   LLVMGetNamedFunction + `if (memset_fn)` (that pattern was dead code on
   non-memcheck runs; see the definition in codegen.c). */
LLVMValueRef cg_ensure_memset_decl(CodegenContext *ctx);
LLVMValueRef emit_user_from_list_value(CodegenContext *ctx, Type *struct_type, AstNode *lit);
void cg_emit_free(CodegenContext *ctx, LLVMValueRef ptr, const char *kind, int line, int col);
bool cg_struct_is_move_only(const Type *t);
LLVMValueRef emit_struct_clone_val(CodegenContext *ctx, LLVMValueRef struct_val, LLVMTypeRef llvm_struct_type, Type *struct_type);
LLVMValueRef emit_enum_clone_val(CodegenContext *ctx, LLVMValueRef enum_val, Type *enum_type);
LLVMValueRef emit_array_clone_val(CodegenContext *ctx, LLVMValueRef arr_val, LLVMTypeRef llvm_arr_type, Type *arr_type);
/* Does a fixed array own heap through its element type? (nested arrays peeled) */
bool type_array_elem_owns_heap(Type *t);
void cg_emit_bounds_guard(CodegenContext *ctx, LLVMValueRef ok_cond, const char *msg, int line, int col);
LLVMValueRef emit_clone_value(CodegenContext *ctx, LLVMValueRef val, LLVMTypeRef llvm_type, Type *type);
void cg_push_temp_drop(CodegenContext *ctx, LLVMValueRef slot, Type *type);
void cg_remove_temp_drop(CodegenContext *ctx, LLVMValueRef slot);
/* Stage 9 (OWN-2): THE one way to hand a fresh owned rvalue to the statement
   temp ledger — spill `val` into an entry-block alloca, store at the current
   position, register the slot via cg_push_temp_drop. Returns the slot.
   zeroed: zero-initialise the alloca in the entry block — required when the
           registered drop can be reached on a path that skipped the store
           (conditional producer, e.g. the chained-receiver spill; see the
           dominance rationale at that site). Keep explicit, do not default.
   why:    alloca name AND diagnostic label (LS_DEBUG_TEMPS output). */
LLVMValueRef cg_spill_owned_rvalue(CodegenContext *ctx, LLVMValueRef val,
                                   Type *type, bool zeroed, const char *why);
/* Stage 6 temp-ledger oracle (LS_OWN_AUDIT=1; CG_DEBUG builds default-on;
   LS_DEBUG_TEMPS=abort is an alias). Pure compile-time assertions on the
   temp_drop ledger — never emits IR; Release default off. */
bool cg_own_audit_enabled(void);
void cg_own_audit_fail(CodegenContext *ctx, const char *site, const char *fmt, ...);
#define CG_OWN_AUDIT(ctx, cond, site, ...) \
    do { if (cg_own_audit_enabled() && !(cond)) \
             cg_own_audit_fail((ctx), (site), __VA_ARGS__); } while (0)
void cg_track_block_rvalue(CodegenContext *ctx, LLVMValueRef block_val, Type *type);
bool cg_claim_block_temp_above(CodegenContext *ctx, int floor);
void cg_emit_block_env_drop(CodegenContext *ctx, LLVMValueRef env_ptr);
void cg_emit_block_env_retain(CodegenContext *ctx, LLVMValueRef env_ptr);
void cg_emit_block_retain_val(CodegenContext *ctx, LLVMValueRef block_val);
void cg_emit_block_drop_at(CodegenContext *ctx, LLVMValueRef blk_alloca);
LLVMValueRef cg_emit_block_env_clone(CodegenContext *ctx, LLVMValueRef block_val);
void cg_null_block_env(CodegenContext *ctx, LLVMValueRef blk_alloca);
LLVMValueRef codegen_fn_to_block(CodegenContext *ctx, AstNode *node);
bool cg_block_source_is_aliased(AstNode *src);
bool cg_invalidate_moved_source(CodegenContext *ctx, AstNode *source, Type *type);
void cg_store_owned(CodegenContext *ctx, LLVMValueRef dst_ptr, LLVMValueRef val, Type *type, AstNode *source);
void cg_flush_temps(CodegenContext *ctx);
void cg_flush_temps_from(CodegenContext *ctx, int drop_floor);
void cg_flush_temps_scope_exit(CodegenContext *ctx);
void emit_scope_cleanup(CodegenContext *ctx);
void emit_cleanup_to(CodegenContext *ctx, CgScope *stop, LLVMValueRef skip_alloca);
void emit_struct_drop_cond(CodegenContext *ctx, LLVMValueRef drop_ptr, Type *struct_type, LLVMValueRef moved_flag);
void emit_auto_drop_fn(CodegenContext *ctx, Type *struct_type);
void emit_struct_drop(CodegenContext *ctx, LLVMValueRef drop_ptr, Type *struct_type);
LLVMValueRef codegen_lvalue_ptr(CodegenContext *ctx, AstNode *node);
LLVMValueRef codegen_print_call(CodegenContext *ctx, AstNode *node);
/* f-string lowering [def: codegen_print.c]; dispatched from codegen_expr. */
LLVMValueRef codegen_format_string(CodegenContext *ctx, AstNode *node);
/* call-expression lowering [def: codegen_call.c]; dispatched from
   codegen_expr (Task 7.4/7.6 call-family TU split). */
LLVMValueRef cg_expr_call(CodegenContext *ctx, AstNode *node);
/* `&lvalue` address-of (non-eager) [def: codegen_expr.c]; shared with
   codegen_call.c (self receiver materialisation). */
LLVMValueRef codegen_addr_of(CodegenContext *ctx, AstNode *node);
/* Address of an array(T,N) place for READ-ONLY consumption (element load,
   whole-array print, for-in). Single authority — see the definition comment in
   codegen_expr.c. Store sites must use codegen_lvalue_ptr instead: this one may
   return a spilled temporary, so a store through it would be lost. */
LLVMValueRef cg_array_place_ptr(CodegenContext *ctx, AstNode *node);
/* Str value helpers [def: codegen_expr.c] shared with codegen_print.c
   (Task 7.6 print-family TU split). */
bool cg_type_is_str(Type *t);
LLVMValueRef cg_make_str(CodegenContext *ctx, LLVMTypeRef st,
                         LLVMValueRef data, LLVMValueRef len, LLVMValueRef cap);
LLVMValueRef cg_str_data(CodegenContext *ctx, LLVMValueRef str_val);
LLVMValueRef cg_str_len(CodegenContext *ctx, LLVMValueRef str_val);
LLVMValueRef cg_str_struct_from_literal(CodegenContext *ctx, const char *text,
                                        Type *str_type);
LLVMValueRef codegen_expr_or_borrow(CodegenContext *ctx, AstNode *node);
int cg_match_stdc_prim(AstNode *callee);
void codegen_stmt(CodegenContext *ctx, AstNode *node);
void emit_drop_field_cleanup(CodegenContext *ctx);
LLVMValueRef codegen_closure_literal(CodegenContext *ctx, AstNode *node);
LLVMValueRef codegen_block_call(CodegenContext *ctx, AstNode *node);
void codegen_fn_decl(CodegenContext *ctx, AstNode *node);
LLVMTypeRef build_variant_payload_struct(CodegenContext *ctx, Type *enum_type, int variant_idx);
void codegen_enum_decl(CodegenContext *ctx, AstNode *node);
bool cg_type_owns_heap_for_enum(const Type *t);
void emit_drop_value(CodegenContext *ctx, LLVMValueRef place_ptr, Type *type);
void emit_auto_enum_drop_fn(CodegenContext *ctx, Type *enum_type);
void emit_enum_drop(CodegenContext *ctx, LLVMValueRef enum_ptr, Type *enum_type);
LLVMValueRef emit_enum_ctor(CodegenContext *ctx, AstNode *node, Type *enum_type, int variant_idx, AstNode **args, int arg_count);
void codegen_struct_decl(CodegenContext *ctx, AstNode *node);
void codegen_impl_decl(CodegenContext *ctx, AstNode *node);
void codegen_impl_trait_decl(CodegenContext *ctx, AstNode *node);
int extern_struct_size(CodegenContext *ctx, Type *t);
bool extern_struct_fits_in_reg(int sz);
LLVMTypeRef extern_struct_reg_int_type(CodegenContext *ctx, int sz);
LLVMTypeRef extern_fn_type(CodegenContext *ctx, Type *fn_type_ml);
void codegen_extern_fn(CodegenContext *ctx, AstNode *node);
void codegen_extern_struct_decl(CodegenContext *ctx, AstNode *node);
void codegen_extern_block(CodegenContext *ctx, AstNode *node);
void cg_predeclare_extern_structs(CodegenContext *ctx, AstNode *ast);
void codegen_load_lib(CodegenContext *ctx, AstNode *node);
void codegen_ffi_init(CodegenContext *ctx, AstNode *ast);
LLVMValueRef codegen_ffi_call(CodegenContext *ctx, AstNode *node);
void codegen_decl(CodegenContext *ctx, AstNode *node);

/* A: enable FMA contraction (a*b+c → fma) on an FP arithmetic instruction.
   Returns `inst` unchanged so it can wrap an LLVMBuildF* call inline. Default
   on; LS_NO_FMA=1 disables. See docs/plan_fma_coldpath.md. */
LLVMValueRef cg_fp_contract(LLVMValueRef inst);

/* B: mark a runtime sink (e.g. __ls_proc_exit) noreturn + cold so LLVM lays out
   the abort / bounds-check / unwrap paths off the hot path. */
void cg_mark_noreturn_cold(CodegenContext *ctx, LLVMValueRef fn);

/* A3: attach !range [0, variant_count) to an enum tag (i8) load so LLVM can
   drop switch bounds compares. LS_NO_ENUM_RANGE=1 disables. */
void cg_attach_tag_range(CodegenContext *ctx, LLVMValueRef load, int variant_count);

/* Escape hatch shared by cg_attach_borrow_attrs (codegen_decl.c) and
   ls_noalias_recover (codegen_noalias.c): LS_NO_BORROW_ATTRS=1 suppresses
   the LLVM borrow-derived parameter attributes (nonnull/dereferenceable/
   align/readonly/nocapture) at the source, which in turn starves A4 noalias
   recovery of markers to promote. Cached per process (single getenv read). */
bool cg_no_borrow_attrs(void);

#endif /* LS_CODEGEN_INTERNAL_H */
