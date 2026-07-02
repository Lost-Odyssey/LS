/* codegen_internal.h — internal forward declarations shared across the
   codegen translation units (docs/plan_codegen_split.md).

   Step 3a: every previously-static codegen helper has been given external
   linkage and a prototype here, so any codegen TU can call any helper with
   no implicit-declaration hazard. Physical splitting of function bodies into
   codegen_own.c / codegen_match.c / codegen_expr.c / codegen_stmt.c /
   codegen_decl.c is then pure cut-paste. Prototypes that end up used by only
   one TU are trimmed back to static at the end of the split (plan §7).

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

/* L-013 follow-up: a `match` / force-unwrap (`expr!`, `unwrap`, `expect`) /
   try expression always yields a FRESH owned rvalue (its arms clone/transfer
   into the result per cg_match_arm_own_tail; force-unwrap moves the payload
   out). Unlike a bare ident/field read, it is never a borrow of a live
   binding, so an owned has_drop result must be dropped at the consuming site
   (print / discard / chained receiver) exactly like an AST_CALL result. The
   owned-rvalue consumer whitelists historically listed AST_CALL but missed
   these, leaking the Option/Result combinators (unwrap_or/ok_or/map/…) whose
   checker-lowering produces an AST_MATCH/AST_FORCE_UNWRAP. Mirror AST_CALL.
   Move-by-value consumers (var-decl/assign/return/by-value user-call arg) do
   NOT route through those sites, so this never double-frees. */
static inline bool cg_is_owned_combinator_rvalue(const AstNode *n)
{
    return n != NULL &&
           (n->kind == AST_MATCH || n->kind == AST_FORCE_UNWRAP);
}

/* M-3: 统一所有权转移 API (used by cg_store_owned below). */
typedef enum {
    CG_XFER_INTO_CONTAINER,  /* vec.push / vec[i]= / enum ctor / struct ctor */
    CG_XFER_ASSIGN_VAR,      /* string a = b（clone 语义，source 保持有效） */
    CG_XFER_RETURN,          /* return val */
} CgTransferKind;

/* match / try / force-unwrap case bodies extracted from codegen_expr (Step 4). */
LLVMValueRef codegen_match_expr(CodegenContext *ctx, AstNode *node);
LLVMValueRef codegen_try_expr(CodegenContext *ctx, AstNode *node);
LLVMValueRef codegen_force_unwrap_expr(CodegenContext *ctx, AstNode *node);

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
LLVMValueRef emit_user_from_list_value(CodegenContext *ctx, Type *struct_type, AstNode *lit);
void cg_emit_free(CodegenContext *ctx, LLVMValueRef ptr, const char *kind, int line, int col);
bool cg_struct_is_move_only(const Type *t);
LLVMValueRef emit_struct_clone_val(CodegenContext *ctx, LLVMValueRef struct_val, LLVMTypeRef llvm_struct_type, Type *struct_type);
LLVMValueRef emit_enum_clone_val(CodegenContext *ctx, LLVMValueRef enum_val, Type *enum_type);
LLVMValueRef emit_array_clone_val(CodegenContext *ctx, LLVMValueRef arr_val, LLVMTypeRef llvm_arr_type, Type *arr_type);
void cg_emit_bounds_guard(CodegenContext *ctx, LLVMValueRef ok_cond, const char *msg, int line, int col);
LLVMValueRef emit_clone_value(CodegenContext *ctx, LLVMValueRef val, LLVMTypeRef llvm_type, Type *type);
void cg_push_temp_drop(CodegenContext *ctx, LLVMValueRef slot, Type *type);
void cg_remove_temp_drop(CodegenContext *ctx, LLVMValueRef slot);
void cg_push_temp_block_env(CodegenContext *ctx, LLVMValueRef env_ptr);
void cg_emit_block_env_drop(CodegenContext *ctx, LLVMValueRef env_ptr);
void cg_emit_block_drop_at(CodegenContext *ctx, LLVMValueRef blk_alloca);
void cg_null_block_env(CodegenContext *ctx, LLVMValueRef blk_alloca);
LLVMValueRef codegen_fn_to_block(CodegenContext *ctx, AstNode *node);
bool cg_block_source_is_aliased(AstNode *src);
bool cg_invalidate_moved_source(CodegenContext *ctx, AstNode *source, Type *type);
void cg_store_owned(CodegenContext *ctx, LLVMValueRef dst_ptr, LLVMValueRef val, Type *type, AstNode *source, CgTransferKind kind);
void cg_flush_temps(CodegenContext *ctx);
void cg_flush_temps_from(CodegenContext *ctx, int env_floor, int drop_floor);
void cg_flush_temps_scope_exit(CodegenContext *ctx);
void emit_scope_cleanup(CodegenContext *ctx);
void emit_cleanup_to(CodegenContext *ctx, CgScope *stop, LLVMValueRef skip_alloca);
void emit_struct_drop_cond(CodegenContext *ctx, LLVMValueRef drop_ptr, Type *struct_type, LLVMValueRef moved_flag);
void emit_auto_drop_fn(CodegenContext *ctx, Type *struct_type);
void emit_struct_drop(CodegenContext *ctx, LLVMValueRef drop_ptr, Type *struct_type);
LLVMValueRef codegen_lvalue_ptr(CodegenContext *ctx, AstNode *node);
LLVMValueRef codegen_print_call(CodegenContext *ctx, AstNode *node);
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

#endif /* LS_CODEGEN_INTERNAL_H */
