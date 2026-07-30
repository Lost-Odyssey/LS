/* checker_internal.h — the genuine cross-TU surface shared across the
   checker translation units (docs/plan_checker_split.md).

   These are exactly the checker helpers that are *called from a TU other than
   the one that defines them* (or referenced by a unit test) — nothing more.
   Every helper used only within its own TU is `static` there and does NOT
   appear here (W11 static-recall, plan_arch_cleanup.md §四 A4). If a helper is
   ever needed from a second TU, move its definition's `static` off and add its
   prototype below under the group owned by its defining TU.

   Defining TUs of the prototypes below:
     - checker.c        — type/method/scope registries, error sinks,
                          forward_pass, check_pass, has_drop propagation,
                          teardown/inspect, checker_check API.
     - checker_expr.c   — expression checking: check_expr_* helpers and the
                          check_expr dispatcher. Batch 7 Task 7.5.
     - checker_call.c   — call-expression checking: check_builtin_* families,
                          intrinsic registry (intrinsic_lookup), check_call_*
                          helpers, check_expr_call. Batch 7 Task 7.5.
     - checker_stmt.c   — statement checking: check_stmt_var_decl/assign/
                          return + check_stmt dispatcher. Batch 7 Task 7.5.
     - checker_decl.c   — declaration checkers (struct/enum/impl/trait/extern),
                          fn templates, imported-trait propagation.
     - checker_lower.c  — lowering/desugar (index protocol, opt-combinators,
                          bit patterns, for-in desugar, operator-overload traits).
     - checker_borrow.c — move/borrow analysis, capture scan, move snapshots.
     - checker_elide.c  — A1 clone-elision last-use pass.
     - checker_comptime.c — compile-time constant evaluator (comptime for/const).
     - checker_derive.c — @derive(...) source-text generator (expand_derives).
     - checker_generics.c — generics/type-resolution cluster (template registry,
                          instantiate_template/checker_instantiate_struct,
                          resolve_type_node family, impl/method registry,
                          method-level generic instantiation). Batch 7 Task 7.5.
   Each prototype's owning TU is noted inline in the block below.

   The public Checker struct / checker API lives in checker.h (included below). */
#ifndef LS_CHECKER_INTERNAL_H
#define LS_CHECKER_INTERNAL_H

#include "checker.h"
#include "diag.h"   /* DiagCandidateFn, used by the diag_help_suggestion proto */

/* "Self" sentinel type — defined in checker.c, used by trait checking and the
   operator-overload lowering in checker_lower.c. */
extern Type g_self_placeholder_type;

/* Compiler intrinsic registry (single source of truth for @-sigil builtins).
   canonical = the `@name` spelling; legacy = the retired `__name` spelling
   (accepted during migration, rejected after Phase 2). */
typedef enum {
    INTR_PLACE_TAKE,      /* @take    — move out of a place (raw load)       */
    INTR_PLACE_DISPOSE,   /* @dispose — in-place destructor (no buffer free) */
    INTR_PLACE_DUP,       /* @dup     — deep copy without consuming source   */
    INTR_VAR_MOVE         /* @move    — tracked ownership move of a variable  */
} IntrinsicKind;

typedef struct {
    const char   *canonical;  /* "@take"                     */
    const char   *legacy;     /* "__take" (NULL once retired) */
    IntrinsicKind kind;
    int           arity;      /* fixed argument count         */
} IntrinsicDef;

/* [def: checker_call.c since the Task 7.5 split] */
const IntrinsicDef *intrinsic_lookup(const char *name);

/* ---- Compile-time constant evaluator (checker_comptime.c) ----
   docs/plan_comptime_consteval.md. check_expr/check_stmt's AST_BLOCK arms
   call comptime_expand_block before checking a block's statements (Stage 3b:
   unrolls `comptime for f in fields(T) { ... }` in place); check_stmt's
   AST_COMPTIME_CONST arm drives the scalar/array interpreter (CtEval/
   CtScalar/CtFlow) directly to evaluate a `comptime const` initializer. */
typedef struct { bool is_float; long long i; double f; } CtScalar;
#define CT_BUDGET_DEFAULT 10000000L
typedef struct {
    const char **names; CtScalar *vals; int count, cap;            /* scalar locals */
    const char **anames; CtScalar **arrs; int *alens; bool *afloat; /* array locals (Step 4) */
    int acount, acap;
    const char *ret_array;  /* set by `return <arrayvar>` to signal an array result */
    long budget;
} CtEval;
typedef enum { CT_NORMAL, CT_RETURNED, CT_FAIL } CtFlow;

double cts_to_f(const CtScalar *v);
bool ct_eval_scalar(Checker *c, AstNode *e, CtEval *ev, CtScalar *out);
CtFlow ct_exec_block(Checker *c, AstNode *blk, CtEval *ev, CtScalar *ret);
void ct_env_free(CtEval *ev);
int ct_aenv_find(const CtEval *ev, const char *name);
void comptime_expand_block(Checker *c, AstNode *block);

/* ---- @derive(...) source-text generator (checker_derive.c) ----
   docs/plan_static_reflection.md Stage 1. checker_inspect_ex and
   checker_check each call expand_derives(c, program) once, before
   forward_pass, to expand every `@derive(Trait, ...)` struct/enum into a
   synthesized `methods Type: Trait { ... }` impl (generated as LS source
   text and re-parsed, then appended to the program's decl list). */
void expand_derives(Checker *c, AstNode *program);

/* method_display_name is defined in checker.c (used there by `ls inspect`'s
   method listing) and shared with checker_derive.c's @derive(Reflect)/
   @derive(ReflectRaw) emitters, which need the same ~/clone/operator-symbol
   display mapping. */
const char *method_display_name(const char *mname);

/* ---- A1 clone-elision (checker_elide.c) ----
   Last-use analysis over a fully-checked fn body: tags provably-final
   by-value uses of named has_drop locals with moved_out so codegen moves
   instead of clones. Call after every body check_stmt convergence point.
   No-op when c->elide_pass_enabled is false or the check had errors. */
bool checker_elide_env_enabled(void);
void checker_elide_last_use(Checker *c, AstNode *fn_decl);

/* ---- Internal types shared across checker TUs ----
   (moved out of checker.c so cross-TU prototypes below can reference them). */

/* Did-you-mean iterator states (C2-2); fed to diag_suggest via the
   diag_*_iter_next callbacks whose bodies live in checker.c. Constructed by
   checker_generics.c (unknown type), checker_expr.c (undefined variable /
   unknown method or field), and checker.c. */
typedef struct { Checker *c; int stage; int i; } DiagTypeIter;
typedef struct { Scope *sc; int i; } DiagScopeIter;
typedef struct {
    Checker *c;
    const Type *strukt;    /* NULL or TYPE_STRUCT: fields first */
    const char *impl_key;  /* impl_registry key of the receiver */
    int fi;                /* field cursor */
    int ii;                /* impl_registry cursor (find once) */
    int mi;                /* method cursor */
    bool impl_found;
} DiagMethodIter;

typedef struct {
    Symbol *sym;
    bool is_moved;
    bool is_maybe_moved;
} MoveSnapEntry;

typedef struct {
    MoveSnapEntry *entries;
    int count;
    int capacity;
} MoveSnapshot;

typedef struct {
    /* Borrowed name pointers — owned by the AST itself. */
    const char **bound;
    int bound_count;
    int bound_cap;

    /* Output list (deep-owned names). Must match AstClosureNode.captures
       layout exactly — the pointer is transferred directly to the AST node. */
    struct {
        char *name;
        Type *type;
        bool  is_explicit_move;  /* F.1: set after capture_walk from move_names */
    } *captures;
    int capture_count;
    int capture_cap;

    Checker *c;
    Scope   *outer_scope;
    AstNode *closure_node;
    bool     had_error;
    /* Closure-foundation Phase B: depth of nested closure literals currently
       being walked, relative to the closure this scan belongs to. 0 = walking
       the closure's own body; >0 = inside one or more nested `|x| ...` literals. */
    int      nested_depth;
} CaptureScan;

typedef enum {
    OPTC_NONE = 0,
    OPTC_UNWRAP, OPTC_EXPECT,    /* panic → force-unwrap (expect carries a message) */
    OPTC_UNWRAP_OR,             /* match desugar, 1 fallback arg */
    OPTC_IS_SOME, OPTC_IS_NONE, /* match desugar, Option only, → bool */
    OPTC_IS_OK,   OPTC_IS_ERR,  /* match desugar, Result only, → bool */
    OPTC_OK,      OPTC_ERR,     /* Result → Option(T) / Option(E) */
    OPTC_OK_OR,                 /* Option → Result(T, E), 1 error-value arg */
    OPTC_MAP,                   /* Option(T)→Option(U) / Result(T,E)→Result(U,E) */
    OPTC_AND_THEN,              /* closure returns Option(U)/Result(U,E) directly */
    OPTC_MAP_ERR,              /* Result(T,E)→Result(T,F), maps the error */
    OPTC_UNWRAP_OR_ELSE         /* None/Err → closure result; → T (no type arg) */
} OptCombinator;

/* ---- Cross-TU checker helper prototypes ----
   Grouped by defining TU; inline "[def: <tu>]" markers flag where the group
   crossings sit. Every prototype here is called from at least one *other*
   checker TU (or a unit test); single-TU helpers are static and absent. */

/* [def: checker.c] registries, errors, resolution, check_expr/check_stmt. */
void checker_error(Checker *c, int line, int col, const char *fmt, ...);
void checker_warning(Checker *c, int line, int col, const char *fmt, ...);
Type *find_type_alias(Checker *c, const char *name);
void checker_move_error(Checker *c, int line, int col, const char *fmt, ...);
bool type_equals_with_self(const Type *trait_t, const Type *impl_t, const Type *concrete);
char *checker_module_type_llvmname(Checker *c, const char *bare_name);
void register_struct_type(Checker *c, const char *name, Type *type);
Type *find_struct_type(Checker *c, const char *name);
Type *resolve_builtin_type_by_name(const char *name);
int find_struct_template_idx(Checker *c, const char *base_name);
void register_struct_template(Checker *c, const char *base_name, char **type_params, int type_param_count, AstNode *decl_node);
void register_enum_type(Checker *c, const char *name, Type *type);
Type *find_enum_type(Checker *c, const char *name);
int find_variant(Checker *c, const char *vname, Type **out_enum, int *out_variant_idx);
int find_template_idx(Checker *c, const char *base);
Type *instantiate_template(Checker *c, int template_idx, Type **type_args, int type_arg_count, int line, int col);
const char *impl_key_of_type(const Type *t);
int find_or_create_impl(Checker *c, const char *struct_name);
bool register_method(Checker *c, int impl_idx, const char *name, Type *type, bool is_static, int self_borrow_kind, const char *origin_iface, AstNode *decl_node, int line, int col);
char *chk_strdup(const char *s);
/* [def: checker.c] diagnostics + registry-table primitives shared with
   checker_generics.c (promoted from static at the Task 7.5 TU split). */
void checker_error_help(Checker *c, int line, int col, int len,
                        const char *help, const char *fmt, ...);
const char *diag_type_iter_next(void *ctx);
const char *diag_scope_iter_next(void *ctx);
const char *diag_method_iter_next(void *ctx);
const char *diag_help_suggestion(char *buf, size_t bufsz, const char *bad,
                                 DiagCandidateFn next, void *ctx);
bool checker_type_is_ambiguous(Checker *c, const char *name);
Type *checker_str_type(Checker *c);
Type *str_target_of_expected(const Type *t);
const char *type_impl_name(Type *t);
bool type_is_str_struct(const Type *t);
bool type_tab_disabled(void);
void type_tab_insert(TypeTabEntry **tab, int *cap, int *count,
                     const char *name, Type *type);
Type *type_tab_find(TypeTabEntry *tab, int cap, const char *name);
void impl_tab_insert(Checker *c, const char *name, int idx);
int find_impl_idx(Checker *c, const char *struct_name);
/* [def: checker_generics.c] generics/type-resolution cluster (Task 7.5).
   checker_instantiate_struct and resolve_type_node stay declared in
   checker.h / earlier groups; these are the promoted former statics. */
Type *check_variant_ctor(Checker *c, AstNode *node, Type *enum_type, int variant_idx, AstNode **args, int arg_count);
void checker_stash_resolved_type_args(Checker *c, AstNode *call, Type **args, int n);
int register_imported_struct_template(Checker *c, const char *base_name, char **type_params, int type_param_count, AstNode *decl_node, const char *module_path);
void register_type_alias(Checker *c, const char *name, Type *type);
void register_builtin_enums(Checker *c);
int method_self_borrow_kind(Checker *c, const char *struct_name, const char *method_name);
Type *find_method_origin(Checker *c, const char *struct_name, const char *method_name, const char *origin);
void method_providers(Checker *c, const char *struct_name, const char *method_name, int *inherent_count, int *iface_count, const char **ia, const char **ib);
bool checker_is_known_interface(Checker *c, const char *name);
int method_is_static(Checker *c, const char *struct_name, const char *method_name);
Type *builtin_module_make_type_merged(Checker *c, const char *name);
bool ensure_generic_method_instantiated_sym(Checker *c, const char *mangled_struct, const char *mfn_name, int line, int col);
bool ensure_generic_method_instantiated(Checker *c, const char *mangled_struct, const char *method_name, int line, int col);
Type *try_instantiate_method_level_generic(Checker *c, const char *impl_key, const char *method_name, TypeNode **call_type_args, int call_type_arg_count, Type **pre_resolved_args, int line, int col);
Type *try_infer_method_generic_from_closure(Checker *c, const char *impl_key, const char *method_name, AstNode *call, int line, int col);
void ensure_generic_struct_impls_local(Checker *c, Type *st);
/* [def: checker_lower.c] index-protocol desugar. */
AstNode *make_index_protocol_call(int line, int column, AstNode *obj, AstNode *idx, AstNode *val, const char *method);
AstNode *make_multi_index_call(int line, int column, AstNode *obj, AstNode **indices, int n, AstNode *val, const char *method);
void rewrite_index_to_call(AstNode *node, AstNode *obj, AstNode *idx, const char *method);
/* [def: checker.c] method lookup. */
Type *find_method(Checker *c, const char *struct_name, const char *method_name);
Type *find_method_ensured(Checker *c, Type *st, const char *mname);
/* [def: checker_lower.c] tag user list/pairs literals with expected type. */
bool checker_tag_user_from_list_literal(Checker *c, Type *expected, AstNode *lit, const char *what);
bool checker_tag_user_from_pairs_literal(Checker *c, Type *expected, AstNode *lit, const char *what);
/* [def: checker.c] type resolution, assignability, scope stack. */
Type *resolve_type_node(Checker *c, TypeNode *tn, int line, int col);
bool type_assignable(const Type *dst, const Type *src);
void chk_push_scope(Checker *c);
void chk_pop_scope(Checker *c);
/* [def: checker_borrow.c] move/borrow analysis, capture scan, move snapshots. */
bool type_is_movable(Type *t);
void checker_try_mark_moved(Checker *c, AstNode *arg);
bool checker_reject_mut_borrow_copy_source(Checker *c, AstNode *src, const char *what);
bool checker_reject_struct_borrow_copy_source(Checker *c, AstNode *src, const char *what);
void check_local_borrow_decl(Checker *c, AstNode *node, Type *declared);
void check_local_slice_decl(Checker *c, AstNode *node, Type *declared);
Symbol *checker_place_root_symbol(Checker *c, AstNode *e);
bool checker_reject_block_param_move(Checker *c, AstNode *src, const char *what);
void move_snap_free(MoveSnapshot *snap);
void move_snap_capture(Checker *c, MoveSnapshot *snap);
void move_snap_restore(const MoveSnapshot *snap);
void move_snap_merge_into_symbols(const MoveSnapshot *a, const MoveSnapshot *b);
void move_elevate_moves_to_maybe(const MoveSnapshot *before);
void move_preseed_maybe_from_pass1(const MoveSnapshot *before, const MoveSnapshot *after_pass1);
void cap_push_bound(CaptureScan *s, const char *name);
void capture_walk(CaptureScan *s, AstNode *node);
/* [def: checker_call.c] call-expression entry + helpers shared with
   checker_expr.c's dispatcher and f-string arm (Task 7.5 sub-split). */
Type *check_expr_call(Checker *c, AstNode *node);
bool is_builtin_function(const char *name);
bool type_is_show_aggregate(Checker *c, Type *t);
void wrap_arg_in_to_str(AstNode **slot);
/* [def: checker_lower.c] std.c prim match, module-call rewrite, opt-combinator,
   bit-pattern lowering, for-in desugar. (check_expr below is [def: checker_expr.c].) */
int match_stdc_prim(Checker *c, AstNode *callee);
bool rewrite_canonical_module_call(Checker *c, AstNode *callee);
int disambig_variant_by_hint(Checker *c, AstNode *node, const char *vname, Type **out_enum, int *out_idx);
int lower_opt_combinator(Checker *c, AstNode *node, AstNode *recv, Type *recv_type, const char *method_name, Type **out_ty);
int bit_pattern_type_bits(const Type *t);
bool pattern_has_bit_seq(const AstNode *pat);
void check_bit_pattern_seq(Checker *c, AstNode *seq, int subj_bits, bool define_binders);
Type *check_expr(Checker *c, AstNode *node);
AstNode *build_foreach_desugar(AstNode *node, bool has_iter, bool src_is_ident);
AstNode *build_foreach_borrow_desugar(AstNode *node);
/* [def: checker_stmt.c since the Task 7.5 split] */
void check_stmt(Checker *c, AstNode *node);
/* [def: checker_decl.c] declaration checkers (struct/enum/impl/trait/extern),
   fn templates, imported-trait propagation. (checker_reject_borrow_* below are
   [def: checker_borrow.c], grouped here by call proximity.) */
void register_fn_template(Checker *c, AstNode *node);
int find_fn_template(Checker *c, const char *name);
void reject_array_by_value_param(Checker *c, Type *pt,
                                 const char *pname, int line, int col);
void attach_param_defaults(Checker *c, AstNode *node, Type *fn_type, Type **params);
void checker_reject_borrow_return(Checker *c, Type *ret, AstNode *fn, int line, int col);
bool checker_reject_borrow_type_arg(Checker *c, Type *arg, const char *base, int line, int col);
void check_struct_decl(Checker *c, AstNode *node);
bool type_owns_heap_for_enum(const Type *t);
void check_enum_decl(Checker *c, AstNode *node);
void check_impl_decl(Checker *c, AstNode *node);
void check_extern_fn(Checker *c, AstNode *node);
void check_extern_struct_decl(Checker *c, AstNode *node);
void check_extern_block(Checker *c, AstNode *node);
void check_load_lib(Checker *c, AstNode *node);
/* [def: checker_lower.c] operator-overload trait helpers. */
bool is_builtin_operator_trait(const char *name);
const char *operator_trait_for_method(const char *mname);
const char *operator_symbol_for_method(const char *mname);
bool is_optional_operator_method(const char *mname);
/* [def: checker_lower.c] FromList/FromPairs -- interfaces whose registered
   signature carries only the method name + self-borrow kind (arity and element
   types come from the implementing type's generics). Callers must skip arity and
   param/return type comparison for these. */
bool is_marker_protocol_trait(const char *name);
void register_builtin_operator_traits(Checker *c);
bool try_operator_overload(Checker *c, AstNode *node, Type *left, Type *right, Type **out_result);
/* [def: checker_decl.c] trait satisfaction + trait/impl-trait decl checkers. */
bool checker_type_satisfies_trait(Checker *c, Type *type, const char *trait_name);
void check_trait_decl(Checker *c, AstNode *node);
void check_impl_trait_decl(Checker *c, AstNode *node);
void register_one_imported_trait_decl(Checker *c, AstNode *d, Type *mod_type);
void propagate_imported_traits(Checker *c, const char *import_path, const char **visited, int *vcount);
void propagate_inherited_methods(Checker *c, const char *import_path, const char **visited, int *vcount);
void checker_mark_ambiguous_type(Checker *c, const char *name);
void check_decl(Checker *c, AstNode *node);
void forward_pass(Checker *c, AstNode *program);

#endif /* LS_CHECKER_INTERNAL_H */
