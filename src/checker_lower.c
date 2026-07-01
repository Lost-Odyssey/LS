/* checker_lower.c
   脱糖 / lowering：Option/Result 组合子、操作符重载、for-in、下标协议、bit-pattern、from_list/from_pairs、规范模块调用

   Bodies mechanically relocated from checker.c (docs/plan_checker_split.md).
   No logic changes. All prototypes live in checker_internal.h. */
#include "checker.h"
#include "checker_internal.h"
#include "module.h"
#include "builtins_math.h"
#include "builtins_perf.h"
#include <stdio.h>
#include <string.h>

/* File-local helpers (single-TU; re-static'd at checker split end). */
static void add_builtin_op_trait(Checker *c, const char *name, const char *const *methods, int method_count, bool ret_is_bool);
static bool cm_build_dotted_path(const AstNode *n, char *buf, size_t cap, size_t *pos);
static int enum_type_has_variant(Type *t, const char *vname, int *out_idx);
static AstNode *foreach_mk_block(AstNode **stmts, int n, int line, int col);
static AstNode *foreach_mk_call0(AstNode *recv, const char *method, int line, int col);
static AstNode *foreach_mk_ident(const char *name, int line, int col);
static AstNode *foreach_mk_let(const char *name, AstNode *init, int line, int col);
static char *foreach_strdup(const char *s);
static AstNode *op_make_call(AstNode *obj_src, const char *method, AstNode *arg_src, int line, int col);
static AstNode *op_make_not(AstNode *inner, int line, int col);
static OptCombinator opt_combinator_id(const char *name);
static AstNode *optc_closure_arg(Checker *c, AstNode *arg, int want_params, const char *method_name, const char **out_param0, int line, int col);
static AstNode *optc_mk_bool(bool v, int line, int col);
static AstNode *optc_mk_ident(const char *name, int line, int col);
static AstNode *optc_mk_match2(AstNode *subject, AstNode *pat0, AstNode *body0, AstNode *pat1, AstNode *body1, int line, int col);
static AstNode *optc_mk_variant_call(const char *variant, AstNode *arg, int line, int col);
static AstNode *optc_mk_variant_pat(const char *variant, const char *binding, int line, int col);
static bool optc_subtree_has_return(AstNode *n);
static AstNode *optc_take_closure_body(Checker *c, AstNode *closure, const char *method_name, int line, int col);
static Type *optc_variant_payload(Type *enom, const char *vname);

/* Unique-id counters for synthesised nodes during lowering (moved from checker.c
   with their owning functions: g_optc_uid for Option/Result combinator desugar,
   g_foreach_uid for for-in desugar). */
static int g_optc_uid = 0;
static int g_foreach_uid = 0;

/* Build the equivalent method call for the Index/IndexMut protocol:
   `v[i]` -> `v.<m>(i)`, `v[i]=x` -> `v.<m>(i, x)`. `obj`, `idx`, and optional
   `val` are reused (not cloned). */
AstNode *make_index_protocol_call(int line, int column,
                                         AstNode *obj, AstNode *idx,
                                         AstNode *val, const char *method)
{
    AstNode *call = ast_new(AST_CALL, line, column);
    AstNode *callee = ast_new(AST_FIELD, line, column);
    callee->as.field_access.object = obj;
    callee->as.field_access.field  = chk_strdup(method);
    int argc = val ? 2 : 1;
    AstNode **args = (AstNode **)malloc_safe((size_t)argc * sizeof(AstNode *));
    args[0] = idx;
    if (val) args[1] = val;
    call->as.call.callee = callee;
    call->as.call.args = args;
    call->as.call.arg_count = argc;
    call->as.call.type_args = NULL;
    call->as.call.type_arg_count = 0;
    return call;
}

/* Build `obj.method(indices[0], .., indices[n-1] [, val])` for multi-subscript
   t[i,j,..] -> __index{N} / t[i,j,..]=v -> __index_set{N}. The index nodes are
   transferred (not cloned) into the call args. */
AstNode *make_multi_index_call(int line, int column, AstNode *obj,
                                      AstNode **indices, int n, AstNode *val,
                                      const char *method)
{
    AstNode *call = ast_new(AST_CALL, line, column);
    AstNode *callee = ast_new(AST_FIELD, line, column);
    callee->as.field_access.object = obj;
    callee->as.field_access.field  = chk_strdup(method);
    int argc = val ? n + 1 : n;
    AstNode **args = (AstNode **)malloc_safe((size_t)argc * sizeof(AstNode *));
    for (int i = 0; i < n; i++) args[i] = indices[i];
    if (val) args[n] = val;
    call->as.call.callee = callee;
    call->as.call.args = args;
    call->as.call.arg_count = argc;
    call->as.call.type_args = NULL;
    call->as.call.type_arg_count = 0;
    return call;
}

/* Rewrite an expression AST_INDEX node in place into an AST_CALL. */
void rewrite_index_to_call(AstNode *node, AstNode *obj, AstNode *idx,
                                  const char *method)
{
    AstNode *call = make_index_protocol_call(node->line, node->column,
                                             obj, idx, NULL, method);
    node->kind = AST_CALL;
    node->as.call = call->as.call;
    free(call);
}

/* Tag `[..]` as a user container literal when the expected type is a struct
   that opts into the reserved __from_list(&!self, E) protocol. This mirrors the
   var-decl path and is reused for struct field defaults/overrides. */
bool checker_tag_user_from_list_literal(Checker *c, Type *expected,
                                               AstNode *lit,
                                               const char *what)
{
    if (c == NULL || expected == NULL || lit == NULL)
        return false;
    if (expected->kind != TYPE_STRUCT || lit->kind != AST_ARRAY_LIT)
        return false;

    /* find_method_ensured (not find_method): an imported generic instance
       (e.g. a Vec(Str) param coming from another module) may not have its
       impl methods registered locally yet — ensure them so `f(["a","b"])`
       works at call-arg sites too, not only `Vec(Str) v = [..]`. */
    Type *fl = find_method_ensured(c, expected, "__from_list");
    if (fl == NULL)
        return false;

    Type *elem_expected = (fl->kind == TYPE_FUNCTION &&
                           fl->as.function.param_count >= 2)
                          ? fl->as.function.params[1] : NULL;
    for (int i = 0; i < lit->as.array_lit.count; i++)
    {
        AstNode *elem = lit->as.array_lit.elements[i];
        if (elem_expected)
            checker_tag_user_from_list_literal(c, elem_expected, elem, what);

        Type *saved_expected = c->expected_type;
        c->expected_type = elem_expected;
        Type *et = check_expr(c, elem);
        c->expected_type = saved_expected;
        if (elem_expected && et && !type_assignable(elem_expected, et))
        {
            checker_error(c, elem->line, elem->column,
                          "%s element type mismatch: expected '%s', got '%s'",
                          what ? what : "list-literal",
                          type_name(elem_expected), type_name(et));
        }
    }
    lit->resolved_type = expected;
    return true;
}

/* M-LIT: tag `{ k: v, ... }` (AST_MAP_LIT) as a user key-value literal when the
   expected type is a struct opting into the reserved __from_pairs(&!self, K, V)
   protocol (e.g. std.map Map(K,V)). Type-checks each pair against K/V and routes
   nested container literals (a Vec `[..]` value, a nested `{..}` map value) to
   their own from_list/from_pairs taggers. Mirrors checker_tag_user_from_list_literal. */
bool checker_tag_user_from_pairs_literal(Checker *c, Type *expected,
                                                AstNode *lit, const char *what)
{
    if (c == NULL || expected == NULL || lit == NULL)
        return false;
    if (expected->kind != TYPE_STRUCT || lit->kind != AST_MAP_LIT)
        return false;

    Type *fp = find_method(c, impl_key_of_type(expected), "__from_pairs");
    if (fp == NULL)
        return false;

    Type *kexp = (fp->kind == TYPE_FUNCTION && fp->as.function.param_count >= 3)
                 ? fp->as.function.params[1] : NULL;   /* params: [*Self, K, V] */
    Type *vexp = (fp->kind == TYPE_FUNCTION && fp->as.function.param_count >= 3)
                 ? fp->as.function.params[2] : NULL;

    for (int i = 0; i < lit->as.map_lit.pair_count; i++)
    {
        AstNode *kn = lit->as.map_lit.keys[i];
        AstNode *vn = lit->as.map_lit.vals[i];

        if (kexp) {
            checker_tag_user_from_pairs_literal(c, kexp, kn, what);
            checker_tag_user_from_list_literal(c, kexp, kn, what);
        }
        if (vexp) {
            checker_tag_user_from_pairs_literal(c, vexp, vn, what);
            checker_tag_user_from_list_literal(c, vexp, vn, what);
        }

        Type *saved = c->expected_type;
        c->expected_type = kexp;
        Type *kt = check_expr(c, kn);
        c->expected_type = vexp;
        Type *vt = check_expr(c, vn);
        c->expected_type = saved;

        if (kexp && kt && !type_assignable(kexp, kt))
            checker_error(c, kn->line, kn->column,
                          "%s key type mismatch: expected '%s', got '%s'",
                          what ? what : "map-literal", type_name(kexp), type_name(kt));
        if (vexp && vt && !type_assignable(vexp, vt))
            checker_error(c, vn->line, vn->column,
                          "%s value type mismatch: expected '%s', got '%s'",
                          what ? what : "map-literal", type_name(vexp), type_name(vt));
    }
    lit->resolved_type = expected;
    return true;
}

/* A-1 (docs/plan_runtime_primitives.md): match a canonical-path call to a
   std.sys.c memory/process primitive — literally `std.sys.c.malloc/realloc/free/abort`.
   These four are reachable by FULL canonical path (not a local alias) so they
   resolve from anywhere, including generic method bodies (std.vec/std.map) that
   are re-checked at the consumer site where no `import std.sys.c` alias is in scope.
   The compiler recognises the spelling and lowers to the same CRT/runtime call
   the bare builtins used to emit — see codegen cg_match_stdc_prim.
   Returns: 0=malloc 1=realloc 2=free 3=abort, or -1 if the callee is not one.
   Guards on `std` not being a local symbol so a user struct field chain
   `mystd.sys.c.malloc` (mystd a variable) is left to normal field handling. */
int match_stdc_prim(Checker *c, AstNode *callee)
{
    if (callee == NULL || callee->kind != AST_FIELD)
        return -1;
    AstNode *mid = callee->as.field_access.object; /* expect `std.sys.c` */
    if (mid == NULL || mid->kind != AST_FIELD)
        return -1;
    if (strcmp(mid->as.field_access.field, "c") != 0)
        return -1;
    AstNode *sysn = mid->as.field_access.object;   /* expect `std.sys` */
    if (sysn == NULL || sysn->kind != AST_FIELD)
        return -1;
    if (strcmp(sysn->as.field_access.field, "sys") != 0)
        return -1;
    AstNode *head = sysn->as.field_access.object;  /* expect `std` */
    if (head == NULL || head->kind != AST_IDENT)
        return -1;
    if (strcmp(head->as.ident.name, "std") != 0)
        return -1;
    if (c != NULL && scope_resolve(c->current_scope, "std") != NULL)
        return -1; /* `std` is a local symbol → not the std.sys.c module path */
    const char *f = callee->as.field_access.field;
    if (strcmp(f, "malloc") == 0)  return 0;
    if (strcmp(f, "realloc") == 0) return 1;
    if (strcmp(f, "free") == 0)    return 2;
    if (strcmp(f, "abort") == 0)   return 3;
    return -1;
}

/* Phase 1 (docs/plan_module_fn_resolution.md): build a dotted path string from a
   FIELD/IDENT chain — FIELD(FIELD(IDENT"std"),"time") → "std.time". Returns false
   on overflow or an unexpected node kind. */
static bool cm_build_dotted_path(const AstNode *n, char *buf, size_t cap, size_t *pos)
{
    if (n == NULL) return false;
    if (n->kind == AST_IDENT) {
        size_t len = strlen(n->as.ident.name);
        if (*pos + len + 1 > cap) return false;
        memcpy(buf + *pos, n->as.ident.name, len);
        *pos += len; buf[*pos] = '\0';
        return true;
    }
    if (n->kind == AST_FIELD) {
        if (!cm_build_dotted_path(n->as.field_access.object, buf, cap, pos)) return false;
        const char *f = n->as.field_access.field;
        size_t len = strlen(f);
        if (*pos + 1 + len + 1 > cap) return false;
        buf[(*pos)++] = '.';
        memcpy(buf + *pos, f, len);
        *pos += len; buf[*pos] = '\0';
        return true;
    }
    return false;
}

/* Phase 1: canonical module-path call `mod.path.fn(...)`. If the dotted prefix
   (everything but the last field) names an imported module bound under its full
   path, collapse the prefix FIELD chain into ONE AST_IDENT holding the dotted path.
   The rewritten callee `FIELD(IDENT"mod.path","fn")` is then structurally identical
   to an alias call (`m.fn()`), so the normal module-call checker + codegen handle
   it with zero special-casing. Returns true if it rewrote the callee.
   No-op for alias calls (head IDENT already a bound symbol → prefix won't be a
   2+ segment FIELD chain) and for std.sys.c primitives (caught earlier). */
bool rewrite_canonical_module_call(Checker *c, AstNode *callee)
{
    if (callee == NULL || callee->kind != AST_FIELD) return false;
    AstNode *obj = callee->as.field_access.object;
    if (obj == NULL || obj->kind != AST_FIELD) return false;  /* need >=2 prefix segs */
    char path[256]; size_t pos = 0;
    if (!cm_build_dotted_path(obj, path, sizeof(path), &pos)) return false;
    Symbol *s = scope_resolve(c->current_scope, path);
    if (s == NULL || s->type == NULL || s->type->kind != TYPE_MODULE) return false;
    AstNode *id = ast_new(AST_IDENT, obj->line, obj->column);
    id->as.ident.name = chk_strdup(path);
    ast_free(obj);
    callee->as.field_access.object = id;
    return true;
}

static OptCombinator opt_combinator_id(const char *name)
{
    if (strcmp(name, "unwrap")    == 0) return OPTC_UNWRAP;
    if (strcmp(name, "expect")    == 0) return OPTC_EXPECT;
    if (strcmp(name, "unwrap_or") == 0) return OPTC_UNWRAP_OR;
    if (strcmp(name, "is_some?")  == 0) return OPTC_IS_SOME;
    if (strcmp(name, "is_none?")  == 0) return OPTC_IS_NONE;
    if (strcmp(name, "is_ok?")    == 0) return OPTC_IS_OK;
    if (strcmp(name, "is_err?")   == 0) return OPTC_IS_ERR;
    if (strcmp(name, "ok")        == 0) return OPTC_OK;
    if (strcmp(name, "err")       == 0) return OPTC_ERR;
    if (strcmp(name, "ok_or")     == 0) return OPTC_OK_OR;
    if (strcmp(name, "map")       == 0) return OPTC_MAP;
    if (strcmp(name, "and_then")  == 0) return OPTC_AND_THEN;
    if (strcmp(name, "map_err")   == 0) return OPTC_MAP_ERR;
    if (strcmp(name, "unwrap_or_else") == 0) return OPTC_UNWRAP_OR_ELSE;
    return OPTC_NONE;
}

static AstNode *optc_mk_ident(const char *name, int line, int col)
{
    AstNode *n = ast_new(AST_IDENT, line, col);
    n->as.ident.name = chk_strdup(name);
    return n;
}

static AstNode *optc_mk_bool(bool v, int line, int col)
{
    AstNode *n = ast_new(AST_BOOL_LIT, line, col);
    n->as.bool_lit.value = v;
    return n;
}

/* variant pattern `Variant(binding)` — binding "_" yields a wildcard payload.
   The same AST_CALL shape doubles as a variant constructor expression. */
static AstNode *optc_mk_variant_pat(const char *variant, const char *binding,
                                    int line, int col)
{
    AstNode *call = ast_new(AST_CALL, line, col);
    call->as.call.callee = optc_mk_ident(variant, line, col);
    call->as.call.args = (AstNode **)malloc_safe(sizeof(AstNode *));
    call->as.call.args[0] = optc_mk_ident(binding, line, col);
    call->as.call.arg_count = 1;
    call->as.call.type_args = NULL;
    call->as.call.type_arg_count = 0;
    return call;
}

/* variant constructor `Variant(arg)` where arg is an arbitrary expression node. */
static AstNode *optc_mk_variant_call(const char *variant, AstNode *arg,
                                     int line, int col)
{
    AstNode *call = ast_new(AST_CALL, line, col);
    call->as.call.callee = optc_mk_ident(variant, line, col);
    call->as.call.args = (AstNode **)malloc_safe(sizeof(AstNode *));
    call->as.call.args[0] = arg;
    call->as.call.arg_count = 1;
    call->as.call.type_args = NULL;
    call->as.call.type_arg_count = 0;
    return call;
}

/* First payload type of variant `vname` in enum type `enom`, or NULL. */
static Type *optc_variant_payload(Type *enom, const char *vname)
{
    for (int i = 0; i < enom->as.enom.variant_count; i++)
        if (strcmp(enom->as.enom.variants[i].name, vname) == 0 &&
            enom->as.enom.variants[i].payload_count > 0)
            return enom->as.enom.variants[i].payload_types[0];
    return NULL;
}

/* If enum type `t` carries variant `vname`, return 1 with *out_idx set. */
static int enum_type_has_variant(Type *t, const char *vname, int *out_idx)
{
    if (t == NULL || t->kind != TYPE_ENUM) return 0;
    for (int i = 0; i < t->as.enom.variant_count; i++)
        if (strcmp(t->as.enom.variants[i].name, vname) == 0) {
            *out_idx = i;
            return 1;
        }
    return 0;
}

/* Disambiguate an otherwise-ambiguous bare variant ctor (e.g. `Some`/`None`/`Err`
   matching several Option/Result instantiations) by a type hint: the node's own
   already-resolved enum (idempotent re-check — the same ctor may be visited twice
   when an outer rewrite re-checks it as a match subject), else the expected type.
   Returns 1 with *out_enum/*out_idx set. Only consulted at the ambiguity sites,
   so it never overrides a unique match. */
int disambig_variant_by_hint(Checker *c, AstNode *node, const char *vname,
                                    Type **out_enum, int *out_idx)
{
    if (node->resolved_type && enum_type_has_variant(node->resolved_type, vname, out_idx)) {
        *out_enum = node->resolved_type;
        return 1;
    }
    if (enum_type_has_variant(c->expected_type, vname, out_idx)) {
        *out_enum = c->expected_type;
        return 1;
    }
    return 0;
}

static AstNode *optc_mk_match2(AstNode *subject,
                               AstNode *pat0, AstNode *body0,
                               AstNode *pat1, AstNode *body1, int line, int col)
{
    AstNode *m = ast_new(AST_MATCH, line, col);
    m->as.match.subject = subject;
    m->as.match.arms = (MatchArm *)malloc_safe(2 * sizeof(MatchArm));
    m->as.match.arms[0].pattern = pat0;
    m->as.match.arms[0].body = body0;
    m->as.match.arms[1].pattern = pat1;
    m->as.match.arms[1].body = body1;
    m->as.match.arm_count = 2;
    return m;
}

/* Does the subtree contain a `return` statement? Used to reject closures with
   early returns, which cannot be inlined (the inlined return would escape the
   enclosing function rather than yield the arm value). */
static bool optc_subtree_has_return(AstNode *n)
{
    if (n == NULL) return false;
    switch (n->kind) {
    case AST_RETURN: return true;
    case AST_BLOCK:
        for (int i = 0; i < n->as.block.stmt_count; i++)
            if (optc_subtree_has_return(n->as.block.stmts[i])) return true;
        return false;
    case AST_EXPR_STMT:
        return optc_subtree_has_return(n->as.expr_stmt.expr);
    case AST_IF:
        return optc_subtree_has_return(n->as.if_stmt.then_block) ||
               optc_subtree_has_return(n->as.if_stmt.else_block);
    case AST_WHILE:
        return optc_subtree_has_return(n->as.while_stmt.body);
    case AST_FOR:
        return optc_subtree_has_return(n->as.for_stmt.body);
    case AST_MATCH:
        for (int i = 0; i < n->as.match.arm_count; i++)
            if (optc_subtree_has_return(n->as.match.arms[i].body)) return true;
        return false;
    default:
        return false;
    }
}

/* Validate that `arg` is a ruby-form closure literal of the expected arity, and
   return it (with *out_param0 set to its first param name, if any). Else report
   an error and return NULL. */
static AstNode *optc_closure_arg(Checker *c, AstNode *arg, int want_params,
                                 const char *method_name,
                                 const char **out_param0, int line, int col)
{
    if (out_param0) *out_param0 = NULL;
    if (arg == NULL || arg->kind != AST_CLOSURE || !arg->as.closure.is_ruby_form) {
        checker_error(c, line, col,
                      "'%s' expects a closure literal `|...| expr` argument",
                      method_name);
        return NULL;
    }
    if (arg->as.closure.param_count != want_params) {
        checker_error(c, line, col,
                      "'%s' closure expects %d parameter(s), got %d",
                      method_name, want_params, arg->as.closure.param_count);
        return NULL;
    }
    if (out_param0 && want_params >= 1)
        *out_param0 = arg->as.closure.param_names[0];
    return arg;
}

/* Convert a ruby-closure body into an expression node usable as a match arm body,
   detaching it from the closure. The body is a block ending in `return E`; we
   rewrite that trailing return in place to an expr-stmt (so the block yields E)
   and hand back the block (a block-expression). Returns NULL on an unsupported
   shape (no trailing value, or an early return). */
static AstNode *optc_take_closure_body(Checker *c, AstNode *closure,
                                       const char *method_name, int line, int col)
{
    AstNode *body = closure->as.closure.body;
    if (body == NULL || body->kind != AST_BLOCK ||
        body->as.block.stmt_count < 1) {
        checker_error(c, line, col, "'%s' closure has no body value", method_name);
        return NULL;
    }
    int n = body->as.block.stmt_count;
    AstNode *tail = body->as.block.stmts[n - 1];
    if (tail->kind != AST_RETURN || tail->as.return_stmt.value == NULL) {
        checker_error(c, line, col,
                      "'%s' closure must end in an expression value", method_name);
        return NULL;
    }
    for (int i = 0; i < n - 1; i++)
        if (optc_subtree_has_return(body->as.block.stmts[i])) {
            checker_error(c, line, col,
                          "'%s' closure must not contain an early `return`",
                          method_name);
            return NULL;
        }
    /* Rewrite trailing `return E` -> expr-stmt E. return_stmt.value and
       expr_stmt.expr alias in the union, so the pointer survives the kind flip. */
    AstNode *e = tail->as.return_stmt.value;
    tail->kind = AST_EXPR_STMT;
    tail->as.expr_stmt.expr = e;
    closure->as.closure.body = NULL;   /* detach so ast_free(closure) won't free it */
    return body;
}

/* Rewrite `recv.METHOD(args)` (an AST_CALL `node`) in place into the lowered form
   and re-check it. Returns 1 = rewrote (result type in *out_ty), -1 = error
   reported, 0 = METHOD is not a combinator (caller falls through to normal enum
   dispatch). */
int lower_opt_combinator(Checker *c, AstNode *node, AstNode *recv,
                                Type *recv_type, const char *method_name,
                                Type **out_ty)
{
    OptCombinator id = opt_combinator_id(method_name);
    if (id == OPTC_NONE) return 0;

    bool is_result = (strncmp(recv_type->as.enom.name, "Result(", 7) == 0);
    bool is_option = (strncmp(recv_type->as.enom.name, "Option(", 7) == 0);
    if (!is_result && !is_option) return 0;   /* defensive: caller guarantees this */

    int line = node->line, col = node->column;
    int argc = node->as.call.arg_count;
    AstNode **args = node->as.call.args;
    AstNode *callee = node->as.call.callee;   /* AST_FIELD shell, discarded below */
    /* Saved so we can free the explicit type-arg nodes after node->as is stolen. */
    TypeNode **saved_targs = node->as.call.type_args;
    int saved_targ_count = node->as.call.type_arg_count;

    /* C2b closure combinators take exactly one argument (the closure). */
    bool is_c2b = (id == OPTC_MAP || id == OPTC_AND_THEN ||
                   id == OPTC_MAP_ERR || id == OPTC_UNWRAP_OR_ELSE);
    int want_args = (id == OPTC_EXPECT || id == OPTC_UNWRAP_OR ||
                     id == OPTC_OK_OR || is_c2b) ? 1 : 0;
    if (argc != want_args) {
        checker_error(c, line, col, "'%s' expects %d argument(s), got %d",
                      method_name, want_args, argc);
        return -1;
    }
    if ((id == OPTC_IS_SOME || id == OPTC_IS_NONE || id == OPTC_OK_OR) && !is_option) {
        checker_error(c, line, col,
                      "'%s' is an Option combinator, but got '%s'",
                      method_name, type_name(recv_type));
        return -1;
    }
    if ((id == OPTC_IS_OK || id == OPTC_IS_ERR ||
         id == OPTC_OK || id == OPTC_ERR || id == OPTC_MAP_ERR) && !is_result) {
        checker_error(c, line, col,
                      "'%s' is a Result combinator, but got '%s'",
                      method_name, type_name(recv_type));
        return -1;
    }
    /* C2b: map / and_then / map_err carry an explicit result type param U;
       unwrap_or_else carries none (its result is the success payload T). */
    int want_targs = (id == OPTC_MAP || id == OPTC_AND_THEN ||
                      id == OPTC_MAP_ERR) ? 1 : 0;
    if (is_c2b && node->as.call.type_arg_count != want_targs) {
        checker_error(c, line, col,
                      "'%s' expects %d type argument(s) (e.g. `x.%s(U)(|..| ..)`),"
                      " got %d", method_name, want_targs, method_name,
                      node->as.call.type_arg_count);
        return -1;
    }

    AstNode *arg0 = (argc >= 1) ? args[0] : NULL;
    /* Detach recv from the callee shell so freeing the shell won't free recv. */
    callee->as.field_access.object = NULL;

    char vb[32];
    snprintf(vb, sizeof vb, "__ocv$%d", g_optc_uid++);
    const char *succ = is_result ? "Ok" : "Some";
    const char *fail = is_result ? "Err" : "None";

    /* C2a conversion combinators build a fresh Option/Result result type and push
       it as the expected type during re-check, so the bare variant constructors in
       the arm bodies (`Some(x)` / `None` / `Err(e)`) resolve in any call context
       (chained, argument position, …) — not just where a typed LHS supplies it. */
    Type *result_ctx = NULL;

    AstNode *repl = NULL;
    switch (id) {
    case OPTC_UNWRAP:
        repl = ast_new(AST_FORCE_UNWRAP, line, col);
        repl->as.force_unwrap.expr = recv;
        repl->as.force_unwrap.message = NULL;
        break;
    case OPTC_EXPECT:
        repl = ast_new(AST_FORCE_UNWRAP, line, col);
        repl->as.force_unwrap.expr = recv;
        repl->as.force_unwrap.message = arg0;   /* user message expr (moved in) */
        arg0 = NULL;
        break;
    case OPTC_UNWRAP_OR: {
        /* match recv { succ(x) => x   fail(_) => arg0 } — fail payload (Result Err)
           is bound to `_` and dropped by the match arm; arg0 is the fallback. */
        AstNode *succ_pat = optc_mk_variant_pat(succ, vb, line, col);
        AstNode *succ_body = optc_mk_ident(vb, line, col);
        AstNode *fail_pat = is_result ? optc_mk_variant_pat(fail, "_", line, col)
                                      : optc_mk_ident(fail, line, col);
        repl = optc_mk_match2(recv, succ_pat, succ_body, fail_pat, arg0, line, col);
        arg0 = NULL;
        break;
    }
    case OPTC_IS_SOME: case OPTC_IS_NONE: {
        bool some_true = (id == OPTC_IS_SOME);
        repl = optc_mk_match2(recv,
                              optc_mk_variant_pat("Some", "_", line, col),
                              optc_mk_bool(some_true, line, col),
                              optc_mk_ident("None", line, col),
                              optc_mk_bool(!some_true, line, col), line, col);
        break;
    }
    case OPTC_IS_OK: case OPTC_IS_ERR: {
        bool ok_true = (id == OPTC_IS_OK);
        repl = optc_mk_match2(recv,
                              optc_mk_variant_pat("Ok", "_", line, col),
                              optc_mk_bool(ok_true, line, col),
                              optc_mk_variant_pat("Err", "_", line, col),
                              optc_mk_bool(!ok_true, line, col), line, col);
        break;
    }
    case OPTC_OK: {
        /* Result(T,E).ok() -> Option(T): match recv { Ok(x)=>Some(x) Err(_)=>None } */
        Type *t = optc_variant_payload(recv_type, "Ok");
        result_ctx = t ? instantiate_template(c, find_template_idx(c, "Option"),
                                              &t, 1, line, col) : NULL;
        repl = optc_mk_match2(recv,
                              optc_mk_variant_pat("Ok", vb, line, col),
                              optc_mk_variant_call("Some", optc_mk_ident(vb, line, col), line, col),
                              optc_mk_variant_pat("Err", "_", line, col),
                              optc_mk_ident("None", line, col), line, col);
        break;
    }
    case OPTC_ERR: {
        /* Result(T,E).err() -> Option(E): match recv { Ok(_)=>None Err(e)=>Some(e) } */
        Type *e = optc_variant_payload(recv_type, "Err");
        result_ctx = e ? instantiate_template(c, find_template_idx(c, "Option"),
                                              &e, 1, line, col) : NULL;
        repl = optc_mk_match2(recv,
                              optc_mk_variant_pat("Ok", "_", line, col),
                              optc_mk_ident("None", line, col),
                              optc_mk_variant_pat("Err", vb, line, col),
                              optc_mk_variant_call("Some", optc_mk_ident(vb, line, col), line, col),
                              line, col);
        break;
    }
    case OPTC_OK_OR: {
        /* Option(T).ok_or(e) -> Result(T, typeof(e)):
             match recv { Some(x)=>Ok(x)  None=>Err(e) }
           E is the error argument's type; build Result(T, E) for the arm ctors. */
        Type *t = optc_variant_payload(recv_type, "Some");
        Type *e_ty = arg0 ? check_expr(c, arg0) : NULL;
        if (t && e_ty) {
            Type *targs[2] = { t, e_ty };
            result_ctx = instantiate_template(c, find_template_idx(c, "Result"),
                                              targs, 2, line, col);
        }
        repl = optc_mk_match2(recv,
                              optc_mk_variant_pat("Some", vb, line, col),
                              optc_mk_variant_call("Ok", optc_mk_ident(vb, line, col), line, col),
                              optc_mk_ident("None", line, col),
                              optc_mk_variant_call("Err", arg0, line, col), line, col);
        arg0 = NULL;
        break;
    }
    case OPTC_MAP: {
        /* Option(T).map(U)(|x| body) -> Option(U):
             match recv { Some(x)=>Some(body)  None=>None }
           Result(T,E).map(U)(|x| body) -> Result(U,E):
             match recv { Ok(x)=>Ok(body)  Err(e$)=>Err(e$) } */
        Type *U = resolve_type_node(c, saved_targs[0], line, col);
        const char *pname = NULL;
        AstNode *clo = optc_closure_arg(c, args[0], 1, method_name, &pname, line, col);
        if (clo == NULL || U == NULL) return -1;
        AstNode *body = optc_take_closure_body(c, clo, method_name, line, col);
        if (body == NULL) return -1;
        if (is_option) {
            result_ctx = instantiate_template(c, find_template_idx(c, "Option"),
                                              &U, 1, line, col);
            repl = optc_mk_match2(recv,
                optc_mk_variant_pat("Some", pname, line, col),
                optc_mk_variant_call("Some", body, line, col),
                optc_mk_ident("None", line, col),
                optc_mk_ident("None", line, col), line, col);
        } else {
            Type *E = optc_variant_payload(recv_type, "Err");
            Type *targs[2] = { U, E };
            result_ctx = E ? instantiate_template(c, find_template_idx(c, "Result"),
                                                  targs, 2, line, col) : NULL;
            char eb[32]; snprintf(eb, sizeof eb, "__ocv$%d", g_optc_uid++);
            repl = optc_mk_match2(recv,
                optc_mk_variant_pat("Ok", pname, line, col),
                optc_mk_variant_call("Ok", body, line, col),
                optc_mk_variant_pat("Err", eb, line, col),
                optc_mk_variant_call("Err", optc_mk_ident(eb, line, col), line, col),
                line, col);
        }
        ast_free(clo);
        break;
    }
    case OPTC_AND_THEN: {
        /* Option(T).and_then(U)(|x| body) -> Option(U): the closure body already
           yields an Option(U); we just splice it into the Some arm.
             match recv { Some(x)=>body  None=>None }
           Result(T,E).and_then(U)(|x| body) -> Result(U,E):
             match recv { Ok(x)=>body  Err(e$)=>Err(e$) } */
        Type *U = resolve_type_node(c, saved_targs[0], line, col);
        const char *pname = NULL;
        AstNode *clo = optc_closure_arg(c, args[0], 1, method_name, &pname, line, col);
        if (clo == NULL || U == NULL) return -1;
        AstNode *body = optc_take_closure_body(c, clo, method_name, line, col);
        if (body == NULL) return -1;
        if (is_option) {
            result_ctx = instantiate_template(c, find_template_idx(c, "Option"),
                                              &U, 1, line, col);
            repl = optc_mk_match2(recv,
                optc_mk_variant_pat("Some", pname, line, col),
                body,
                optc_mk_ident("None", line, col),
                optc_mk_ident("None", line, col), line, col);
        } else {
            Type *E = optc_variant_payload(recv_type, "Err");
            Type *targs[2] = { U, E };
            result_ctx = E ? instantiate_template(c, find_template_idx(c, "Result"),
                                                  targs, 2, line, col) : NULL;
            char eb[32]; snprintf(eb, sizeof eb, "__ocv$%d", g_optc_uid++);
            repl = optc_mk_match2(recv,
                optc_mk_variant_pat("Ok", pname, line, col),
                body,
                optc_mk_variant_pat("Err", eb, line, col),
                optc_mk_variant_call("Err", optc_mk_ident(eb, line, col), line, col),
                line, col);
        }
        ast_free(clo);
        break;
    }
    case OPTC_MAP_ERR: {
        /* Result(T,E).map_err(F)(|e| body) -> Result(T,F):
             match recv { Ok(v$)=>Ok(v$)  Err(e)=>Err(body) } */
        Type *F = resolve_type_node(c, saved_targs[0], line, col);
        const char *pname = NULL;
        AstNode *clo = optc_closure_arg(c, args[0], 1, method_name, &pname, line, col);
        if (clo == NULL || F == NULL) return -1;
        AstNode *body = optc_take_closure_body(c, clo, method_name, line, col);
        if (body == NULL) return -1;
        Type *T = optc_variant_payload(recv_type, "Ok");
        Type *targs[2] = { T, F };
        result_ctx = T ? instantiate_template(c, find_template_idx(c, "Result"),
                                              targs, 2, line, col) : NULL;
        char vbk[32]; snprintf(vbk, sizeof vbk, "__ocv$%d", g_optc_uid++);
        repl = optc_mk_match2(recv,
            optc_mk_variant_pat("Ok", vbk, line, col),
            optc_mk_variant_call("Ok", optc_mk_ident(vbk, line, col), line, col),
            optc_mk_variant_pat("Err", pname, line, col),
            optc_mk_variant_call("Err", body, line, col), line, col);
        ast_free(clo);
        break;
    }
    case OPTC_UNWRAP_OR_ELSE: {
        /* Option(T).unwrap_or_else(|| body) -> T:
             match recv { Some(v$)=>v$  None=>body }
           Result(T,E).unwrap_or_else(|e| body) -> T:
             match recv { Ok(v$)=>v$  Err(e)=>body } */
        const char *pname = NULL;
        int want_p = is_option ? 0 : 1;
        AstNode *clo = optc_closure_arg(c, args[0], want_p, method_name, &pname, line, col);
        if (clo == NULL) return -1;
        AstNode *body = optc_take_closure_body(c, clo, method_name, line, col);
        if (body == NULL) return -1;
        result_ctx = optc_variant_payload(recv_type, succ);  /* T, helps body coerce */
        char vbk[32]; snprintf(vbk, sizeof vbk, "__ocv$%d", g_optc_uid++);
        if (is_option) {
            repl = optc_mk_match2(recv,
                optc_mk_variant_pat("Some", vbk, line, col),
                optc_mk_ident(vbk, line, col),
                optc_mk_ident("None", line, col),
                body, line, col);
        } else {
            repl = optc_mk_match2(recv,
                optc_mk_variant_pat("Ok", vbk, line, col),
                optc_mk_ident(vbk, line, col),
                optc_mk_variant_pat("Err", pname, line, col),
                body, line, col);
        }
        ast_free(clo);
        break;
    }
    default: return 0;
    }

    /* Steal repl's payload into node, then free the discarded AST_CALL shells. */
    node->kind = repl->kind;
    node->as = repl->as;
    node->resolved_type = NULL;
    free(repl);
    ast_free(callee);          /* recv detached above; frees the field name + shell */
    if (args) free(args);      /* the args array only; arg0 (if any) moved into repl */
    if (saved_targs) {         /* C2b explicit type-arg nodes (resolved above) */
        for (int i = 0; i < saved_targ_count; i++) type_node_free(saved_targs[i]);
        free(saved_targs);
    }

    /* Re-check the rewritten node. Conversion combinators push their freshly-built
       result type as the expected type so bare ctors in the arm bodies resolve. */
    Type *saved_exp = c->expected_type;
    if (result_ctx) c->expected_type = result_ctx;
    *out_ty = check_expr(c, node);
    c->expected_type = saved_exp;
    return 1;
}

/* Bit width of an integer type for bit-pattern total-width validation.
   Returns 0 for non-integer (or types we don't allow as bit-match subjects). */
int bit_pattern_type_bits(const Type *t) {
    if (t == NULL) return 0;
    switch (t->kind) {
    case TYPE_I8:  case TYPE_U8:  return 8;
    case TYPE_I16: case TYPE_U16: return 16;
    case TYPE_I32: case TYPE_U32: case TYPE_INT: return 32;
    case TYPE_I64: case TYPE_U64: return 64;
    default: return 0;
    }
}

/* True if `pat` is a bit-pattern (a seq, or an OR-tree whose leaves are seqs). */
bool pattern_has_bit_seq(const AstNode *pat) {
    if (pat == NULL) return false;
    if (pat->kind == AST_MATCH_BIT_PATTERN_SEQ) return true;
    if (pat->kind == AST_MATCH_OR_PATTERN)
        return pattern_has_bit_seq(pat->as.or_pattern.left) ||
               pattern_has_bit_seq(pat->as.or_pattern.right);
    return false;
}

/* Validate one bits[...] sequence against the subject bit width, compute each
   field's MSB-first lsb_shift, and (when define_binders) define binder vars in
   the current scope. Reports errors via the checker. */
void check_bit_pattern_seq(Checker *c, AstNode *seq, int subj_bits,
                                  bool define_binders) {
    int total = 0;
    for (int i = 0; i < seq->as.bit_pattern_seq.count; i++) {
        AstNode *item = seq->as.bit_pattern_seq.items[i];
        int w = item->as.bit_pattern.width;
        if (w < 1 || w > 64) {
            checker_error(c, item->line, item->column,
                          "bit field width must be between 1 and 64, got %d", w);
            return;
        }
        if (item->as.bit_pattern.match_value_set) {
            unsigned long long maxv = (w >= 64) ? ~0ULL : ((1ULL << w) - 1ULL);
            if ((unsigned long long)item->as.bit_pattern.match_val > maxv) {
                checker_error(c, item->line, item->column,
                              "bit-match value 0x%llx does not fit in %d bit(s)",
                              (unsigned long long)item->as.bit_pattern.match_val, w);
            }
        }
        total += w;
    }
    if (total != subj_bits) {
        checker_error(c, seq->line, seq->column,
                      "bit-match total width %d does not match subject type (%d bits)",
                      total, subj_bits);
        return;
    }
    seq->as.bit_pattern_seq.total_width = total;

    /* MSB-first: first field occupies the most-significant bits. */
    int accumulated = 0;
    for (int i = 0; i < seq->as.bit_pattern_seq.count; i++) {
        AstNode *item = seq->as.bit_pattern_seq.items[i];
        int w = item->as.bit_pattern.width;
        item->as.bit_pattern.lsb_shift = subj_bits - accumulated - w;
        accumulated += w;

        if (define_binders && item->as.bit_pattern.name != NULL) {
            /* w==1 → bool; 2..31 → int (i32, sign bit unreachable); 32..64 → i64 (a
               32-bit field fills i32's sign bit → bind i64 so the zero-extended value
               stays non-negative). Must mirror cg_emit_bit_pattern_seq in codegen_match.c. */
            Type *ft = (w == 1) ? type_bool() : (w < 32 ? type_int() : type_i64());
            /* Skip if already bound in this arm scope (e.g. repeated in OR). */
            if (scope_resolve_local(c->current_scope, item->as.bit_pattern.name) == NULL)
                scope_define(c->current_scope, item->as.bit_pattern.name, ft);
        }
    }
}

static char *foreach_strdup(const char *s)
{
    size_t n = strlen(s) + 1;
    char *p = (char *)malloc_safe(n);
    memcpy(p, s, n);
    return p;
}

static AstNode *foreach_mk_ident(const char *name, int line, int col)
{
    AstNode *n = ast_new(AST_IDENT, line, col);
    n->as.ident.name = foreach_strdup(name);
    return n;
}

/* recv.method()  — recv is consumed (owned by the returned node). */
static AstNode *foreach_mk_call0(AstNode *recv, const char *method, int line, int col)
{
    AstNode *fld = ast_new(AST_FIELD, line, col);
    fld->as.field_access.object = recv;
    fld->as.field_access.field = foreach_strdup(method);
    AstNode *call = ast_new(AST_CALL, line, col);
    call->as.call.callee = fld;
    call->as.call.args = NULL;
    call->as.call.arg_count = 0;
    call->as.call.type_args = NULL;
    call->as.call.type_arg_count = 0;
    return call;
}

/* `name = init`  — type-inferred local (var_type NULL). init is consumed. */
static AstNode *foreach_mk_let(const char *name, AstNode *init, int line, int col)
{
    AstNode *vd = ast_new(AST_VAR_DECL, line, col);
    vd->as.var_decl.var_type = NULL;
    vd->as.var_decl.name = foreach_strdup(name);
    vd->as.var_decl.init = init;
    return vd;
}

static AstNode *foreach_mk_block(AstNode **stmts, int n, int line, int col)
{
    AstNode *b = ast_new(AST_BLOCK, line, col);
    b->as.block.stmts = (AstNode **)malloc_safe((size_t)(n > 0 ? n : 1) * sizeof(AstNode *));
    for (int i = 0; i < n; i++) b->as.block.stmts[i] = stmts[i];
    b->as.block.stmt_count = n;
    return b;
}

AstNode *build_foreach_desugar(AstNode *node, bool has_iter, bool src_is_ident)
{
    int line = node->line, col = node->column;
    const char *var = node->as.for_stmt.var;
    int uid = g_foreach_uid++;
    char itname[40], srcname[40];
    snprintf(itname, sizeof itname, "__it$%d", uid);
    snprintf(srcname, sizeof srcname, "__src$%d", uid);

    AstNode *outer[3];
    int oc = 0;

    AstNode *recv;
    if (src_is_ident)
    {
        recv = ast_clone_deep(node->as.for_stmt.iter);   /* IDENT(v) — borrow */
    }
    else
    {
        /* materialize the temporary so its buffer outlives the loop */
        outer[oc++] = foreach_mk_let(srcname,
                                     ast_clone_deep(node->as.for_stmt.iter), line, col);
        recv = foreach_mk_ident(srcname, line, col);
    }

    AstNode *it_init = has_iter ? foreach_mk_call0(recv, "iter", line, col) : recv;
    outer[oc++] = foreach_mk_let(itname, it_init, line, col);

    /* match __it.next() { Some(var) => {BODY}  None => { break } } */
    AstNode *next_call = foreach_mk_call0(foreach_mk_ident(itname, line, col), "next", line, col);

    AstNode *some_pat = ast_new(AST_CALL, line, col);
    some_pat->as.call.callee = foreach_mk_ident("Some", line, col);
    some_pat->as.call.args = (AstNode **)malloc_safe(sizeof(AstNode *));
    some_pat->as.call.args[0] = foreach_mk_ident(var, line, col);
    some_pat->as.call.arg_count = 1;
    some_pat->as.call.type_args = NULL;
    some_pat->as.call.type_arg_count = 0;

    AstNode *some_body = ast_clone_deep(node->as.for_stmt.body);

    AstNode *brk = ast_new(AST_BREAK, line, col);
    AstNode *none_body = foreach_mk_block(&brk, 1, line, col);
    AstNode *none_pat = foreach_mk_ident("None", line, col);

    AstNode *mtch = ast_new(AST_MATCH, line, col);
    mtch->as.match.subject = next_call;
    mtch->as.match.arms = (MatchArm *)malloc_safe(2 * sizeof(MatchArm));
    mtch->as.match.arms[0].pattern = some_pat;
    mtch->as.match.arms[0].body = some_body;
    mtch->as.match.arms[1].pattern = none_pat;
    mtch->as.match.arms[1].body = none_body;
    mtch->as.match.arm_count = 2;

    AstNode *match_stmt = ast_new(AST_EXPR_STMT, line, col);
    match_stmt->as.expr_stmt.expr = mtch;

    AstNode *while_body = foreach_mk_block(&match_stmt, 1, line, col);
    AstNode *cond = ast_new(AST_BOOL_LIT, line, col);
    cond->as.bool_lit.value = true;
    AstNode *whl = ast_new(AST_WHILE, line, col);
    whl->as.while_stmt.cond = cond;
    whl->as.while_stmt.body = while_body;

    outer[oc++] = whl;
    return foreach_mk_block(outer, oc, line, col);
}

/* ---- borrowing for-in (`for x in &v`) desugar helpers ---------------------- */

static AstNode *fb_int_lit(long long v, int line, int col)
{
    AstNode *n = ast_new(AST_INT_LIT, line, col);
    n->as.int_lit.value = v;
    n->as.int_lit.is_char = false;
    return n;
}

/* left OP right (binary). left/right consumed. */
static AstNode *fb_binary(TokenType op, AstNode *left, AstNode *right, int line, int col)
{
    AstNode *n = ast_new(AST_BINARY, line, col);
    n->as.binary.op = op;
    n->as.binary.left = left;
    n->as.binary.right = right;
    n->as.binary.lowered = NULL;
    return n;
}

/* target = value (plain assignment). target/value consumed. */
static AstNode *fb_assign(AstNode *target, AstNode *value, int line, int col)
{
    AstNode *n = ast_new(AST_ASSIGN, line, col);
    n->as.assign.target = target;
    n->as.assign.op = TOKEN_ASSIGN;
    n->as.assign.value = value;
    return n;
}

/* recv.method(arg) — one argument. recv/arg consumed. */
static AstNode *fb_call1(AstNode *recv, const char *method, AstNode *arg, int line, int col)
{
    AstNode *fld = ast_new(AST_FIELD, line, col);
    fld->as.field_access.object = recv;
    fld->as.field_access.field = foreach_strdup(method);
    AstNode *call = ast_new(AST_CALL, line, col);
    call->as.call.callee = fld;
    call->as.call.args = (AstNode **)malloc_safe(sizeof(AstNode *));
    call->as.call.args[0] = arg;
    call->as.call.arg_count = 1;
    call->as.call.type_args = NULL;
    call->as.call.type_arg_count = 0;
    return call;
}

/* `for x in &v` — borrow each element zero-copy instead of clone-on-read.
   v (the operand of the `&`) must be a Vec-like with get_ref(i)->&T and len().
   Desugars to an index loop binding x as a borrow &T via get_ref:

     { __i = 0
       while __i < V.len() {
         x = V.get_ref(__i)     // inferred &T (get_ref returns a borrow)
         BODY
         __i = __i + 1
       }
     }

   x is a non-escaping borrow: the body may read it (auto-deref) but not move or
   store it (enforced by the borrow checker). Zero per-element clone — the win
   over the owning `for x in v` (Iterator next() clone-on-read). */
AstNode *build_foreach_borrow_desugar(AstNode *node)
{
    int line = node->line, col = node->column;
    const char *var = node->as.for_stmt.var;
    int uid = g_foreach_uid++;
    char iname[40];
    snprintf(iname, sizeof iname, "__i$%d", uid);

    /* V = the borrowed container (operand of the `&` borrow). */
    AstNode *iter = node->as.for_stmt.iter;          /* AST_UNARY(&, V) */
    AstNode *V = iter->as.unary.operand;

    AstNode *outer[2];
    int oc = 0;

    /* __i = 0 */
    outer[oc++] = foreach_mk_let(iname, fb_int_lit(0, line, col), line, col);

    /* cond: __i < V.len() */
    AstNode *len_call = foreach_mk_call0(ast_clone_deep(V), "len", line, col);
    AstNode *cond = fb_binary(TOKEN_LT, foreach_mk_ident(iname, line, col), len_call, line, col);

    /* body: { x = V.get_ref(__i); BODY; __i = __i + 1 } */
    AstNode *getref = fb_call1(ast_clone_deep(V), "get_ref",
                               foreach_mk_ident(iname, line, col), line, col);
    AstNode *bind_x = foreach_mk_let(var, getref, line, col);   /* inferred &T */
    AstNode *user_body = ast_clone_deep(node->as.for_stmt.body);
    AstNode *incr = fb_assign(foreach_mk_ident(iname, line, col),
                              fb_binary(TOKEN_PLUS, foreach_mk_ident(iname, line, col),
                                        fb_int_lit(1, line, col), line, col),
                              line, col);
    AstNode *body_stmts[3] = { bind_x, user_body, incr };
    AstNode *while_body = foreach_mk_block(body_stmts, 3, line, col);

    AstNode *whl = ast_new(AST_WHILE, line, col);
    whl->as.while_stmt.cond = cond;
    whl->as.while_stmt.body = while_body;
    outer[oc++] = whl;

    return foreach_mk_block(outer, oc, line, col);
}

/* The 7 soft-reserved built-in operator traits. */
bool is_builtin_operator_trait(const char *name)
{
    return strcmp(name, "Add") == 0 || strcmp(name, "Sub") == 0 ||
           strcmp(name, "Mul") == 0 || strcmp(name, "Div") == 0 ||
           strcmp(name, "Rem") == 0 || strcmp(name, "Equal")  == 0 ||
           strcmp(name, "Order") == 0;
}

/* Given an internal operator method name ($op_*), return the built-in trait that
   declares it, or NULL if mname is not an operator method. */
const char *operator_trait_for_method(const char *mname)
{
    if (mname == NULL || mname[0] != '$') return NULL;
    if (strcmp(mname, "$op_add") == 0) return "Add";
    if (strcmp(mname, "$op_sub") == 0) return "Sub";
    if (strcmp(mname, "$op_mul") == 0) return "Mul";
    if (strcmp(mname, "$op_div") == 0) return "Div";
    if (strcmp(mname, "$op_rem") == 0) return "Rem";
    if (strcmp(mname, "$op_eq") == 0 || strcmp(mname, "$op_ne") == 0) return "Equal";
    if (strcmp(mname, "$op_lt") == 0 || strcmp(mname, "$op_gt") == 0 ||
        strcmp(mname, "$op_le") == 0 || strcmp(mname, "$op_ge") == 0) return "Order";
    return NULL;
}

/* Map an internal operator method name back to its source symbol (for diagnostics). */
const char *operator_symbol_for_method(const char *mname)
{
    if (strcmp(mname, "$op_add") == 0) return "+";
    if (strcmp(mname, "$op_sub") == 0) return "-";
    if (strcmp(mname, "$op_mul") == 0) return "*";
    if (strcmp(mname, "$op_div") == 0) return "/";
    if (strcmp(mname, "$op_rem") == 0) return "%";
    if (strcmp(mname, "$op_eq") == 0)  return "==";
    if (strcmp(mname, "$op_ne") == 0)  return "!=";
    if (strcmp(mname, "$op_lt") == 0)  return "<";
    if (strcmp(mname, "$op_gt") == 0)  return ">";
    if (strcmp(mname, "$op_le") == 0)  return "<=";
    if (strcmp(mname, "$op_ge") == 0)  return ">=";
    return mname;
}

/* Comparison operators that may be omitted from an Equal/Order impl (derived from ==/<). */
bool is_optional_operator_method(const char *mname)
{
    return strcmp(mname, "$op_ne") == 0 || strcmp(mname, "$op_gt") == 0 ||
           strcmp(mname, "$op_le") == 0 || strcmp(mname, "$op_ge") == 0;
}

/* Register one built-in operator trait. Every method is `fn OP(&self, &Self rhs)`;
   ret_is_bool selects comparison (-> bool) vs arithmetic (-> Self). */
static void add_builtin_op_trait(Checker *c, const char *name,
                                 const char *const *methods, int method_count,
                                 bool ret_is_bool)
{
    if (c->trait_count >= c->trait_cap)
    {
        c->trait_cap = GROW_CAPACITY(c->trait_cap);
        c->trait_registry = realloc_safe(c->trait_registry,
                                         (size_t)c->trait_cap * sizeof(c->trait_registry[0]));
    }
    int idx = c->trait_count++;
    {
        size_t len = strlen(name) + 1;
        char *dup = (char *)malloc_safe(len);
        memcpy(dup, name, len);
        c->trait_registry[idx].name = dup;
    }
    c->trait_registry[idx].method_count = method_count;
    c->trait_registry[idx].methods = (void *)
        malloc_safe((size_t)method_count * sizeof(c->trait_registry[idx].methods[0]));
    for (int i = 0; i < method_count; i++)
    {
        size_t nlen = strlen(methods[i]) + 1;
        char *ndup = (char *)malloc_safe(nlen);
        memcpy(ndup, methods[i], nlen);
        Type **params = (Type **)malloc_safe(sizeof(Type *));
        params[0] = type_reference(&g_self_placeholder_type);            /* &Self rhs */
        Type *ret = ret_is_bool ? type_bool() : &g_self_placeholder_type; /* bool | Self */
        c->trait_registry[idx].methods[i].name = ndup;
        c->trait_registry[idx].methods[i].type = type_function(params, 1, ret, false);
        c->trait_registry[idx].methods[i].is_static = false;
        c->trait_registry[idx].methods[i].self_borrow_kind = 1; /* &self */
    }
}

/* Register a built-in lifecycle trait (Destroy / Clone) with one no-arg instance
   method (no rhs param), given its method name, return type and self-borrow kind. */
static void add_builtin_lifecycle_trait(Checker *c, const char *name,
                                        const char *method, Type *ret, int sbk)
{
    if (c->trait_count >= c->trait_cap)
    {
        c->trait_cap = GROW_CAPACITY(c->trait_cap);
        c->trait_registry = realloc_safe(c->trait_registry,
                                         (size_t)c->trait_cap * sizeof(c->trait_registry[0]));
    }
    int idx = c->trait_count++;
    {
        size_t len = strlen(name) + 1;
        char *dup = (char *)malloc_safe(len);
        memcpy(dup, name, len);
        c->trait_registry[idx].name = dup;
    }
    c->trait_registry[idx].method_count = 1;
    c->trait_registry[idx].methods = (void *)
        malloc_safe(sizeof(c->trait_registry[idx].methods[0]));
    {
        size_t mlen = strlen(method) + 1;
        char *mdup = (char *)malloc_safe(mlen);
        memcpy(mdup, method, mlen);
        c->trait_registry[idx].methods[0].name = mdup;
    }
    c->trait_registry[idx].methods[0].type = type_function(NULL, 0, ret, false);
    c->trait_registry[idx].methods[0].is_static = false;
    c->trait_registry[idx].methods[0].self_borrow_kind = sbk;
}

/* Pre-register the 7 built-in operator traits into the trait registry, before any
   user declarations. A user `trait Add {}` then collides via the duplicate check. */
void register_builtin_operator_traits(Checker *c)
{
    static const char *const m_add[] = {"$op_add"};
    static const char *const m_sub[] = {"$op_sub"};
    static const char *const m_mul[] = {"$op_mul"};
    static const char *const m_div[] = {"$op_div"};
    static const char *const m_rem[] = {"$op_rem"};
    static const char *const m_eq[]  = {"$op_eq", "$op_ne"};
    static const char *const m_ord[] = {"$op_lt", "$op_gt", "$op_le", "$op_ge"};
    add_builtin_op_trait(c, "Add", m_add, 1, false);
    add_builtin_op_trait(c, "Sub", m_sub, 1, false);
    add_builtin_op_trait(c, "Mul", m_mul, 1, false);
    add_builtin_op_trait(c, "Div", m_div, 1, false);
    add_builtin_op_trait(c, "Rem", m_rem, 1, false);
    add_builtin_op_trait(c, "Equal",  m_eq,  2, true);
    add_builtin_op_trait(c, "Order", m_ord, 4, true);
    /* Lifecycle traits: Destroy (the C++-style `~` destructor → internal __drop) and
       Clone (the deep-copy hook → internal __clone). Generic impls fold their method
       into the struct's inherent impl (check_impl_trait_decl); the user method name
       (`~`→__drop in the parser, `clone`→__clone in the checker) is what the existing
       RAII machinery scans for. */
    add_builtin_lifecycle_trait(c, "Destroy", "__drop",  type_void(), 2);            /* def ~(&!self) */
    add_builtin_lifecycle_trait(c, "Clone",   "__clone", &g_self_placeholder_type, 1); /* def clone(&self) -> Self */
    /* Protocol interfaces (reserved-method facades, like Destroy/Clone). Marker
       traits: the internal method name is what the literal-init / index detection
       scans for (find_method(..., "__from_list") etc.). Element/key/value types
       come from the implementing type's own generics — no interface type parameter
       needed. The `from_list`→`__from_list` rename happens in check_impl_trait_decl. */
    add_builtin_lifecycle_trait(c, "FromList",  "__from_list",  type_void(), 2); /* def from_list(&!self, E x) */
    add_builtin_lifecycle_trait(c, "FromPairs", "__from_pairs", type_void(), 3); /* def from_pairs(&!self, K k, V v) */
}

/* Build `obj.method(arg)` as a fresh AST_CALL. obj/arg are TAKEN BY OWNERSHIP
   (not cloned): they are the already-type-checked operands of the binary node,
   which is about to relinquish them (the caller nulls binary.left/right). Reusing
   the checked subtrees — rather than cloning + re-checking — is what keeps an
   `a + b + c + ...` operator chain linear instead of O(2^n). The lowered call now
   owns obj/arg and frees them via ordinary ast_free recursion. */
static AstNode *op_make_call(AstNode *obj, const char *method,
                             AstNode *arg, int line, int col)
{
    AstNode *fld = ast_new(AST_FIELD, line, col);
    fld->as.field_access.object = obj;
    size_t mlen = strlen(method) + 1;
    char *mdup = (char *)malloc_safe(mlen);
    memcpy(mdup, method, mlen);
    fld->as.field_access.field = mdup;

    AstNode *call = ast_new(AST_CALL, line, col);
    call->as.call.callee = fld;
    call->as.call.args = (AstNode **)malloc_safe(sizeof(AstNode *));
    call->as.call.args[0] = arg;
    call->as.call.arg_count = 1;
    call->as.call.type_args = NULL;
    call->as.call.type_arg_count = 0;
    return call;
}

/* Build `!(inner)`. Takes ownership of inner. */
static AstNode *op_make_not(AstNode *inner, int line, int col)
{
    AstNode *u = ast_new(AST_UNARY, line, col);
    u->as.unary.op = TOKEN_BANG;
    u->as.unary.operand = inner;
    return u;
}

bool try_operator_overload(Checker *c, AstNode *node, Type *left, Type *right,
                                  Type **out_result)
{
    (void)right;
    *out_result = NULL;

    /* Only struct/enum VALUE operands overload. Pointer operands (e.g. `*Point p`,
       `p == nil`) keep builtin pointer-identity comparison — do NOT deref. Borrow
       params (`&self`, `&Vec2 rhs`) are already presented as value types by the
       checker, so no deref is needed for them either. */
    Type *lt = left;
    if (lt == NULL || (lt->kind != TYPE_STRUCT && lt->kind != TYPE_ENUM))
        return false;  /* not an overload site — let the builtin path handle it */

    /* Map operator → (trait, primary method, derivation kind).
       derive: 0 none, 1 !=(neg-eq), 2 >(swap-lt), 3 <=(neg-swap-lt), 4 >=(neg-lt). */
    const char *trait = NULL, *prim = NULL;
    int derive = 0;
    switch (node->as.binary.op)
    {
    case TOKEN_PLUS:    trait = "Add"; prim = "$op_add"; break;
    case TOKEN_MINUS:   trait = "Sub"; prim = "$op_sub"; break;
    case TOKEN_STAR:    trait = "Mul"; prim = "$op_mul"; break;
    case TOKEN_SLASH:   trait = "Div"; prim = "$op_div"; break;
    case TOKEN_PERCENT: trait = "Rem"; prim = "$op_rem"; break;
    case TOKEN_EQ:      trait = "Equal";  prim = "$op_eq";  break;
    case TOKEN_NEQ:     trait = "Equal";  prim = "$op_ne";  derive = 1; break;
    case TOKEN_LT:      trait = "Order"; prim = "$op_lt";  break;
    case TOKEN_GT:      trait = "Order"; prim = "$op_gt";  derive = 2; break;
    case TOKEN_LEQ:     trait = "Order"; prim = "$op_le";  derive = 3; break;
    case TOKEN_GEQ:     trait = "Order"; prim = "$op_ge";  derive = 4; break;
    default:
        /* Bitwise/shift/logical on struct/enum are not overloadable; let the
           builtin path emit its "requires integer/numeric" error. */
        return false;
    }

    if (!checker_type_satisfies_trait(c, lt, trait))
    {
        checker_error(c, node->line, node->column,
                      "type '%s' does not implement interface '%s' (required for operator '%s')",
                      type_name(lt), trait, operator_symbol_for_method(prim));
        return true;  /* recognized overload site, reported */
    }

    const char *key = impl_key_of_type(lt);
    if (key == NULL) key = type_name(lt);

    AstNode *lhs = node->as.binary.left;
    AstNode *rhs = node->as.binary.right;
    int line = node->line, col = node->column;

    /* Prefer an explicit method when present (covers required ops and any
       explicit override of a derivable comparison). */
    bool have_prim = (find_method(c, key, prim) != NULL);
    AstNode *lowered = NULL;
    if (derive == 0 || have_prim)
    {
        lowered = op_make_call(lhs, prim, rhs, line, col);
    }
    else
    {
        switch (derive)
        {
        case 1: /* a != b  ->  !(a $op_eq b) */
            lowered = op_make_not(op_make_call(lhs, "$op_eq", rhs, line, col), line, col);
            break;
        case 2: /* a > b   ->  b $op_lt a */
            lowered = op_make_call(rhs, "$op_lt", lhs, line, col);
            break;
        case 3: /* a <= b  ->  !(b $op_lt a) */
            lowered = op_make_not(op_make_call(rhs, "$op_lt", lhs, line, col), line, col);
            break;
        case 4: /* a >= b  ->  !(a $op_lt b) */
            lowered = op_make_not(op_make_call(lhs, "$op_lt", rhs, line, col), line, col);
            break;
        }
    }

    /* The lowered call now OWNS lhs/rhs (op_make_call took them by ownership).
       Detach them from the binary node so ast_free(node) does not double-free —
       node->as.binary.lowered is the sole owner. Readers that traversed
       binary.left/right (capture analysis, AST hashing, C backend) now consult
       binary.lowered when present (mirrors codegen). */
    node->as.binary.left = NULL;
    node->as.binary.right = NULL;

    /* Type-check the synthesized expression (resolves dispatch, borrows, sret).
       The reused operands are already resolved; AST_BINARY memoization makes their
       re-check O(1), so this stays linear over operator chains. */
    Type *t = check_expr(c, lowered);
    if (t == NULL)
    {
        ast_free(lowered);  /* inner check already reported a precise error */
        return true;
    }
    node->as.binary.lowered = lowered;
    *out_result = t;
    return true;
}
