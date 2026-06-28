/* checker_internal.h — internal forward declarations shared across the
   checker translation units (docs/plan_checker_split.md).

   Step 1: every previously-static checker helper has external linkage and
   a prototype here, so any checker TU can call any helper with no implicit-
   declaration hazard. Physical splitting into checker_borrow.c / checker_lower.c
   / checker_decl.c is then pure cut-paste. Single-TU prototypes are trimmed
   back to static at the end of the split.

   The public Checker struct / checker API lives in checker.h (included below). */
#ifndef LS_CHECKER_INTERNAL_H
#define LS_CHECKER_INTERNAL_H

#include "checker.h"

/* "Self" sentinel type — defined in checker.c, used by trait checking and the
   operator-overload lowering in checker_lower.c. */
extern Type g_self_placeholder_type;

/* ---- Internal types shared across checker TUs ----
   (moved out of checker.c so cross-TU prototypes below can reference them). */

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

/* ---- Internal checker helper prototypes (auto-consolidated, Step 1) ---- */
void checker_error(Checker *c, int line, int col, const char *fmt, ...);
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
bool register_method(Checker *c, int impl_idx, const char *name, Type *type, bool is_static, int self_borrow_kind, int line, int col);
char *chk_strdup(const char *s);
AstNode *make_index_protocol_call(int line, int column, AstNode *obj, AstNode *idx, AstNode *val, const char *method);
AstNode *make_multi_index_call(int line, int column, AstNode *obj, AstNode **indices, int n, AstNode *val, const char *method);
void rewrite_index_to_call(AstNode *node, AstNode *obj, AstNode *idx, const char *method);
Type *find_method(Checker *c, const char *struct_name, const char *method_name);
Type *find_method_ensured(Checker *c, Type *st, const char *mname);
bool checker_tag_user_from_list_literal(Checker *c, Type *expected, AstNode *lit, const char *what);
bool checker_tag_user_from_pairs_literal(Checker *c, Type *expected, AstNode *lit, const char *what);
void instantiate_impl_method_types( Checker *c, Type *struct_type, const char *mangled_name, AstNode *impl_node, char **tp_names, Type **type_args, int tp_count);
Type *resolve_type_node(Checker *c, TypeNode *tn, int line, int col);
bool type_assignable(const Type *dst, const Type *src);
void chk_push_scope(Checker *c);
void chk_pop_scope(Checker *c);
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
void check_stmt(Checker *c, AstNode *node);
void register_fn_template(Checker *c, AstNode *node);
int find_fn_template(Checker *c, const char *name);
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
bool is_builtin_operator_trait(const char *name);
const char *operator_trait_for_method(const char *mname);
const char *operator_symbol_for_method(const char *mname);
bool is_optional_operator_method(const char *mname);
void register_builtin_operator_traits(Checker *c);
bool try_operator_overload(Checker *c, AstNode *node, Type *left, Type *right, Type **out_result);
bool checker_type_satisfies_trait(Checker *c, Type *type, const char *trait_name);
void check_trait_decl(Checker *c, AstNode *node);
void check_impl_trait_decl(Checker *c, AstNode *node);
void register_one_imported_trait_decl(Checker *c, AstNode *d, Type *mod_type);
void propagate_imported_traits(Checker *c, const char *import_path, const char **visited, int *vcount);
void check_decl(Checker *c, AstNode *node);
void forward_pass(Checker *c, AstNode *program);

#endif /* LS_CHECKER_INTERNAL_H */
