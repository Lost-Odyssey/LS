/* checker_elide.c — A1 clone-elision: last-use analysis
   (docs/plan_opt_clone_elision.md).

   A backward liveness pass over a fully-checked function body. It finds
   has_drop locals whose by-value consumption — call argument, `T x = e` /
   `x = e` / `s.f = e` source, or `return e` — is provably the variable's
   LAST use, and tags that use with the same `moved_out` AST flag `@move(x)`
   sets. Codegen's existing move-elision channel then transfers the heap
   instead of deep-cloning it. This keeps LS "values by default, the compiler
   silently elides the copy" (plan §3.0): no user annotation is ever required,
   `@move` stays a manual override, and un-marked code keeps the conservative
   clone.

   Safety shape (plan §3.2): the pass may only turn "clone" into "move +
   invalidate source". Every codegen consumer routes the mark through
   cg_invalidate_moved_source, which refuses borrows and sources without a
   runtime moved_flag and makes the caller fall back to the clone — so even a
   wrong mark degrades to the old behavior instead of a double-free. To keep
   marks true by construction we only consider NAMED LOCALS declared by a
   var_decl directly inside a block of this body (those always carry a
   moved_flag) and exclude everything the predicates below reject.

   v1 exclusions (plan §3.1 — one predicate each, see prescan):
     - globals / struct fields / container slots: never candidates, only a
       bare IDENT declared by an in-body var_decl qualifies;
     - closure-captured names (the closure body runs later / elsewhere);
     - borrow or slice sources (`&x`, `&!x`, `&T r = <init>`), whose alias
       must outlive any move;
     - shadowed / re-bound names (2+ binders anywhere in the body) — the
       cheap name-keyed analysis cannot tell the bindings apart;
     - intrinsic place-op operands (@take/@dispose manage the slot manually);
     - match binders (v2, plan §3.3) — they bind pattern names, and any name
       collision with a local is excluded via the shadow rule.

   Loop back edges are handled by pre-marking every candidate that is READ
   anywhere inside the loop and DECLARED outside it as used before walking
   the body (plan §3.2 merge rule) — per-iteration locals keep their precise
   result, outer variables never elide inside a loop.

   Disable with LS_NO_ELIDE=1 (A/B comparison and escape hatch). */
#include "checker_internal.h"
#include "common.h"

#include <stdlib.h>
#include <string.h>

/* LS_NO_ELIDE=1 (any non-"0" value) turns the pass off; read once at Checker
   creation into c->elide_pass_enabled. */
bool checker_elide_env_enabled(void)
{
    const char *v = getenv("LS_NO_ELIDE");
    return !(v != NULL && v[0] != '\0' && strcmp(v, "0") != 0);
}

/* `@move(x)` wrapper peel — local mirror of codegen's ast_unwrap_move (that
   one lives in a codegen TU; the checker must not link against it). */
static AstNode *elide_unwrap_move(AstNode *n)
{
    while (n && n->kind == AST_CALL &&
           n->as.call.callee && n->as.call.callee->kind == AST_IDENT &&
           (strcmp(n->as.call.callee->as.ident.name, "@move") == 0 ||
            strcmp(n->as.call.callee->as.ident.name, "__move") == 0) &&
           n->as.call.arg_count == 1)
        n = n->as.call.args[0];
    return n;
}

/* Only has_drop struct/enum values are clone-elision material: they are the
   two kinds codegen deep-clones at by-value consumption sites AND the two
   kinds whose named locals carry a runtime moved_flag for invalidation. */
static bool elide_type_eligible(Type *t)
{
    if (t == NULL) return false;
    if (t->kind == TYPE_STRUCT) return t->as.strukt.has_drop;
    if (t->kind == TYPE_ENUM)   return t->as.enom.has_drop;
    return false;
}

/* ---- analysis tables ---- */

typedef struct {
    const char *name;    /* borrowed from the AST */
    AstNode *decl;       /* the AST_VAR_DECL that binds it */
    AstNode *decl_block; /* the AST_BLOCK whose stmt list holds decl */
    Type *type;          /* resolved eligible type (shadow guard: the IDENT's
                            resolved_type must be this exact Type*) */
    bool excluded;       /* any exclusion predicate fired for this name */
    /* backward-scan state */
    bool in_scope;       /* between its decl and the end of decl_block */
    bool used;           /* read on some path after the current point */
    /* loop-preset scratch */
    bool loop_read;
    bool loop_decl;
} EVar;

typedef struct {
    const char *name;
    int binds;           /* how many binders introduce this name in the body */
    bool excluded;       /* hard exclusion (capture / borrow src / place op) */
} EName;

typedef struct {
    EVar  *vars;  int var_count;  int var_cap;
    EName *names; int name_count; int name_cap;
} Elide;

static EName *ename_get(Elide *e, const char *name)
{
    for (int i = 0; i < e->name_count; i++)
        if (strcmp(e->names[i].name, name) == 0)
            return &e->names[i];
    if (e->name_count >= e->name_cap)
    {
        e->name_cap = e->name_cap < 16 ? 16 : e->name_cap * 2;
        e->names = (EName *)realloc_safe(e->names,
                                         (size_t)e->name_cap * sizeof(EName));
    }
    EName *n = &e->names[e->name_count++];
    n->name = name;
    n->binds = 0;
    n->excluded = false;
    return n;
}

static void ename_bind(Elide *e, const char *name)
{
    if (name) ename_get(e, name)->binds++;
}

static void ename_exclude(Elide *e, const char *name)
{
    if (name) ename_get(e, name)->excluded = true;
}

static EVar *evar_find(Elide *e, const char *name)
{
    for (int i = 0; i < e->var_count; i++)
        if (strcmp(e->vars[i].name, name) == 0)
            return &e->vars[i];
    return NULL;
}

/* ---- generic scanner (three read-only modes over the checked AST) ----

   PRESCAN  : collect candidates + binder counts + hard exclusions.
   LOOP     : mark candidates read / declared inside a loop subtree.
   READMARK : conservatively mark every candidate IDENT as used (opaque
              constructs the backward walker does not model, e.g. comptime).

   All modes follow the codegen-effective tree: binary.lowered replaces
   left/right and for_stmt.desugared replaces iter/body when present. */

typedef enum { SCAN_PRESCAN, SCAN_LOOP, SCAN_READMARK } ScanMode;

typedef struct {
    Elide *e;
    ScanMode mode;
    AstNode *cur_block; /* PRESCAN: innermost enclosing AST_BLOCK */
} EScan;

static void elide_scan(EScan *s, AstNode *n);

static void escan_ident_read(EScan *s, const char *name)
{
    EVar *v = evar_find(s->e, name);
    if (v == NULL) return;
    if (s->mode == SCAN_LOOP)          v->loop_read = true;
    else if (s->mode == SCAN_READMARK) v->used = true;
}

static void escan_pattern(EScan *s, AstNode *pat)
{
    if (pat == NULL) return;
    switch (pat->kind)
    {
    case AST_IDENT:
        /* Pattern IDENT = binder (same convention as capture_walk). Counting
           it as a binder shadow-excludes any same-named candidate; marking it
           as a read keeps the conservative side if it is really a compare. */
        if (s->mode == SCAN_PRESCAN) ename_bind(s->e, pat->as.ident.name);
        else                         escan_ident_read(s, pat->as.ident.name);
        return;
    case AST_CALL: /* variant ctor: payload binders in arg position */
        for (int i = 0; i < pat->as.call.arg_count; i++)
            escan_pattern(s, pat->as.call.args[i]);
        return;
    case AST_MATCH_OR_PATTERN:
        escan_pattern(s, pat->as.or_pattern.left);
        escan_pattern(s, pat->as.or_pattern.right);
        return;
    case AST_MATCH_BIT_PATTERN:
        if (pat->as.bit_pattern.name)
        {
            if (s->mode == SCAN_PRESCAN)
                ename_bind(s->e, pat->as.bit_pattern.name);
        }
        return;
    case AST_MATCH_BIT_PATTERN_SEQ:
        for (int i = 0; i < pat->as.bit_pattern_seq.count; i++)
            escan_pattern(s, pat->as.bit_pattern_seq.items[i]);
        return;
    default:
        /* literal / wildcard / range: no binders; scan for reads anyway */
        elide_scan(s, pat);
        return;
    }
}

static void elide_scan(EScan *s, AstNode *n)
{
    if (n == NULL) return;
    switch (n->kind)
    {
    case AST_INT_LIT: case AST_FLOAT_LIT: case AST_STRING_LIT:
    case AST_BOOL_LIT: case AST_NIL_LIT: case AST_BREAK: case AST_CONTINUE:
    case AST_SIZEOF: case AST_TYPENAME: case AST_LOAD_LIB:
        return;
    case AST_IDENT:
        escan_ident_read(s, n->as.ident.name);
        return;
    case AST_UNARY:
        /* Exclusion predicate: `&x` roots a borrow whose alias can outlive
           any later by-value use of x (named borrow locals `&T r = &x` pin
           their referent) — moving x from under it would dangle. Transient
           call-site auto-borrows are over-excluded too: conservative. */
        if (s->mode == SCAN_PRESCAN && n->as.unary.op == TOKEN_AMP &&
            n->as.unary.operand && n->as.unary.operand->kind == AST_IDENT)
            ename_exclude(s->e, n->as.unary.operand->as.ident.name);
        elide_scan(s, n->as.unary.operand);
        return;
    case AST_MUT_BORROW:
        /* Exclusion predicate: `&!x` — same aliasing rationale as `&x`. */
        if (s->mode == SCAN_PRESCAN && n->as.mut_borrow.operand &&
            n->as.mut_borrow.operand->kind == AST_IDENT)
            ename_exclude(s->e, n->as.mut_borrow.operand->as.ident.name);
        elide_scan(s, n->as.mut_borrow.operand);
        return;
    case AST_BINARY:
        if (n->as.binary.lowered)
            elide_scan(s, n->as.binary.lowered);
        else
        {
            elide_scan(s, n->as.binary.left);
            elide_scan(s, n->as.binary.right);
        }
        return;
    case AST_CALL:
        /* Exclusion predicate: intrinsic place ops (@take / @dispose) manage
           slot liveness manually — their operands must never be re-tagged by
           this pass. (@move args are already checker-tracked; excluding them
           too is harmless since their later use is rejected anyway.) */
        if (s->mode == SCAN_PRESCAN &&
            n->as.call.callee && n->as.call.callee->kind == AST_IDENT &&
            intrinsic_lookup(n->as.call.callee->as.ident.name) != NULL)
        {
            for (int i = 0; i < n->as.call.arg_count; i++)
            {
                AstNode *a = n->as.call.args[i];
                if (a && a->kind == AST_IDENT)
                    ename_exclude(s->e, a->as.ident.name);
            }
        }
        elide_scan(s, n->as.call.callee);
        for (int i = 0; i < n->as.call.arg_count; i++)
            elide_scan(s, n->as.call.args[i]);
        return;
    case AST_FFI_CALL:
        elide_scan(s, n->as.ffi_call.lib_expr);
        for (int i = 0; i < n->as.ffi_call.arg_count; i++)
            elide_scan(s, n->as.ffi_call.args[i]);
        return;
    case AST_INDEX:
        elide_scan(s, n->as.index_expr.object);
        elide_scan(s, n->as.index_expr.index);
        for (int i = 0; i < n->as.index_expr.index_count; i++)
            if (n->as.index_expr.indices)
                elide_scan(s, n->as.index_expr.indices[i]);
        return;
    case AST_FIELD:
        elide_scan(s, n->as.field_access.object);
        return;
    case AST_COMPTIME_FIELD:
        elide_scan(s, n->as.comptime_field.object);
        return;
    case AST_FORMAT_STRING:
        for (int i = 0; i < n->as.format_string.expr_count; i++)
            elide_scan(s, n->as.format_string.exprs[i]);
        return;
    case AST_ARRAY_LIT:
        for (int i = 0; i < n->as.array_lit.count; i++)
            elide_scan(s, n->as.array_lit.elements[i]);
        return;
    case AST_MAP_LIT:
        for (int i = 0; i < n->as.map_lit.pair_count; i++)
        {
            elide_scan(s, n->as.map_lit.keys[i]);
            elide_scan(s, n->as.map_lit.vals[i]);
        }
        return;
    case AST_CAST:
        elide_scan(s, n->as.cast.expr);
        return;
    case AST_RANGE:
        elide_scan(s, n->as.range.start);
        elide_scan(s, n->as.range.end);
        return;
    case AST_TRY:
        elide_scan(s, n->as.try_expr.expr);
        return;
    case AST_FORCE_UNWRAP:
        elide_scan(s, n->as.force_unwrap.expr);
        elide_scan(s, n->as.force_unwrap.message);
        return;
    case AST_AT_TIME:
        elide_scan(s, n->as.at_time.expr);
        return;
    case AST_AT_BENCH:
        elide_scan(s, n->as.at_bench.expr);
        return;
    case AST_NEW_EXPR:
        for (int i = 0; i < n->as.new_expr.field_init_count; i++)
            elide_scan(s, n->as.new_expr.field_inits[i].value);
        return;
    case AST_CLOSURE:
        /* Exclusion predicate: every captured name is off-limits — the
           closure env snapshots / moves the value at the literal, and the
           body may run at any later time; a candidate read inside a closure
           is by definition a capture, so the captures list is exhaustive. */
        if (s->mode == SCAN_PRESCAN)
        {
            for (int i = 0; i < n->as.closure.capture_count; i++)
                ename_exclude(s->e, n->as.closure.captures[i].name);
            for (int i = 0; i < n->as.closure.param_count; i++)
                ename_bind(s->e, n->as.closure.param_names[i]);
            elide_scan(s, n->as.closure.body); /* count body binders too */
        }
        else
        {
            for (int i = 0; i < n->as.closure.capture_count; i++)
                escan_ident_read(s, n->as.closure.captures[i].name);
        }
        return;
    case AST_MATCH:
        elide_scan(s, n->as.match.subject);
        for (int i = 0; i < n->as.match.arm_count; i++)
        {
            escan_pattern(s, n->as.match.arms[i].pattern);
            elide_scan(s, n->as.match.arms[i].body);
        }
        return;
    case AST_VAR_DECL:
        if (s->mode == SCAN_PRESCAN)
        {
            ename_bind(s->e, n->as.var_decl.name);
            /* Candidate predicate: a bare named local of an eligible type,
               declared directly in a block (globals, REPL-replayed globals,
               borrow/slice locals and for_c-init decls never qualify). */
            if (s->cur_block != NULL && !n->as.var_decl.is_repl_extern &&
                elide_type_eligible(n->resolved_type))
            {
                Elide *e = s->e;
                if (e->var_count >= e->var_cap)
                {
                    e->var_cap = e->var_cap < 8 ? 8 : e->var_cap * 2;
                    e->vars = (EVar *)realloc_safe(
                        e->vars, (size_t)e->var_cap * sizeof(EVar));
                }
                EVar *v = &e->vars[e->var_count++];
                memset(v, 0, sizeof(EVar));
                v->name = n->as.var_decl.name;
                v->decl = n;
                v->decl_block = s->cur_block;
                v->type = n->resolved_type;
            }
        }
        else if (s->mode == SCAN_LOOP)
        {
            EVar *v = evar_find(s->e, n->as.var_decl.name);
            if (v && v->decl == n) v->loop_decl = true;
        }
        elide_scan(s, n->as.var_decl.init);
        return;
    case AST_ASSIGN:
        elide_scan(s, n->as.assign.target);
        elide_scan(s, n->as.assign.value);
        return;
    case AST_RETURN:
        elide_scan(s, n->as.return_stmt.value);
        return;
    case AST_IF:
        elide_scan(s, n->as.if_stmt.cond);
        elide_scan(s, n->as.if_stmt.then_block);
        elide_scan(s, n->as.if_stmt.else_block);
        return;
    case AST_COMPTIME_IF:
        elide_scan(s, n->as.comptime_if.cond);
        elide_scan(s, n->as.comptime_if.then_block);
        elide_scan(s, n->as.comptime_if.else_block);
        return;
    case AST_WHILE:
        elide_scan(s, n->as.while_stmt.cond);
        elide_scan(s, n->as.while_stmt.body);
        return;
    case AST_FOR:
        if (n->as.for_stmt.desugared)
        {
            elide_scan(s, n->as.for_stmt.desugared);
            return;
        }
        if (s->mode == SCAN_PRESCAN) ename_bind(s->e, n->as.for_stmt.var);
        elide_scan(s, n->as.for_stmt.iter);
        elide_scan(s, n->as.for_stmt.body);
        return;
    case AST_FOR_C:
        elide_scan(s, n->as.for_c_stmt.init);
        elide_scan(s, n->as.for_c_stmt.cond);
        elide_scan(s, n->as.for_c_stmt.update);
        elide_scan(s, n->as.for_c_stmt.body);
        return;
    case AST_COMPTIME_FOR:
        if (s->mode == SCAN_PRESCAN) ename_bind(s->e, n->as.comptime_for.var);
        elide_scan(s, n->as.comptime_for.body);
        return;
    case AST_COMPTIME_MATCH:
        if (s->mode == SCAN_PRESCAN)
        {
            ename_bind(s->e, n->as.comptime_match.handle);
            ename_bind(s->e, n->as.comptime_match.binder);
        }
        elide_scan(s, n->as.comptime_match.subject);
        elide_scan(s, n->as.comptime_match.body);
        return;
    case AST_COMPTIME_CONST:
        if (s->mode == SCAN_PRESCAN) ename_bind(s->e, n->as.comptime_const.name);
        elide_scan(s, n->as.comptime_const.value);
        return;
    case AST_COMPTIME_BLOCK:
        elide_scan(s, n->as.comptime_block.block);
        return;
    case AST_BLOCK:
    {
        AstNode *saved = s->cur_block;
        if (s->mode == SCAN_PRESCAN) s->cur_block = n;
        for (int i = 0; i < n->as.block.stmt_count; i++)
            elide_scan(s, n->as.block.stmts[i]);
        s->cur_block = saved;
        return;
    }
    case AST_EXPR_STMT:
        elide_scan(s, n->as.expr_stmt.expr);
        return;
    default:
        /* Declaration-ish / unknown nodes: nothing to scan. Nested fn/struct
           decls are rejected by the checker (A-1), so a fn body never holds
           further bodies. */
        return;
    }
}

/* ---- backward walk ---- */

typedef struct {
    Elide *e;
} EWalk;

static void ew_stmt(Elide *e, AstNode *n);
static void ew_expr(Elide *e, AstNode *n);

/* used[]-state snapshot helpers for control-flow merges. */
static bool *ev_snapshot(Elide *e)
{
    bool *snap = (bool *)malloc_safe((size_t)(e->var_count ? e->var_count : 1)
                                     * sizeof(bool));
    for (int i = 0; i < e->var_count; i++) snap[i] = e->vars[i].used;
    return snap;
}

static void ev_restore(Elide *e, const bool *snap)
{
    for (int i = 0; i < e->var_count; i++) e->vars[i].used = snap[i];
}

static void ev_or_into(Elide *e, const bool *snap)
{
    for (int i = 0; i < e->var_count; i++)
        e->vars[i].used = e->vars[i].used || snap[i];
}

/* Opaque subtree: mark every candidate read inside as used (conservative). */
static void ew_mark_all_reads(Elide *e, AstNode *n)
{
    EScan s = { e, SCAN_READMARK, NULL };
    elide_scan(&s, n);
}

/* Loop back-edge preset (§3.2): any candidate read anywhere inside the loop
   whose decl is NOT inside the loop is alive across iterations → used. */
static void ew_loop_preset(Elide *e, AstNode *loop)
{
    for (int i = 0; i < e->var_count; i++)
    {
        e->vars[i].loop_read = false;
        e->vars[i].loop_decl = false;
    }
    EScan s = { e, SCAN_LOOP, NULL };
    elide_scan(&s, loop);
    for (int i = 0; i < e->var_count; i++)
        if (e->vars[i].loop_read && !e->vars[i].loop_decl)
            e->vars[i].used = true;
}

/* A whitelisted consumption of `raw`: if it is a bare candidate IDENT with no
   later use, tag it moved_out (idempotent — an existing checker mark from
   @move / binding-move / force-unwrap is left alone). Either way the variable
   then counts as used for everything earlier in the program. */
static void ew_consume_or_read(Elide *e, AstNode *raw)
{
    if (raw == NULL) return;
    AstNode *uw = elide_unwrap_move(raw);
    if (uw != NULL && uw->kind == AST_IDENT)
    {
        EVar *v = evar_find(e, uw->as.ident.name);
        if (v != NULL && v->in_scope)
        {
            if (!v->excluded && !v->used && !uw->moved_out &&
                uw->resolved_type == v->type)
                uw->moved_out = true;
            v->used = true;
            return;
        }
        return; /* not this candidate's binding (global sibling etc.) */
    }
    ew_expr(e, raw);
}

static void ew_expr(Elide *e, AstNode *n)
{
    if (n == NULL) return;
    switch (n->kind)
    {
    case AST_IDENT:
    {
        EVar *v = evar_find(e, n->as.ident.name);
        if (v && v->in_scope) v->used = true;
        return;
    }
    case AST_CALL:
    {
        /* Whitelist (a): by-value has_drop args of a direct call whose LS
           function type is known. Args are processed in REVERSE evaluation
           order so `twin(w, w)` sees the trailing use first (elide) and the
           leading one as still-used (clone). */
        Type *ft = n->as.call.callee ? n->as.call.callee->resolved_type : NULL;
        if (ft && ft->kind != TYPE_FUNCTION) ft = NULL;
        int argc = n->as.call.arg_count;
        /* Instance methods carry self as param 0 of the LS fn type. */
        int off = (ft && n->as.call.callee->kind == AST_FIELD &&
                   ft->as.function.param_count == argc + 1) ? 1 : 0;
        for (int i = argc - 1; i >= 0; i--)
        {
            Type *pt = NULL;
            if (ft && i + off < ft->as.function.param_count)
                pt = ft->as.function.params[i + off];
            /* Param must be an owned by-value has_drop struct/enum — a &T /
               &!T (TYPE_REFERENCE) or slice param is pointer-ABI and never
               cloned, so never a consumption. */
            if (pt != NULL && elide_type_eligible(pt))
                ew_consume_or_read(e, n->as.call.args[i]);
            else
                ew_expr(e, n->as.call.args[i]);
        }
        ew_expr(e, n->as.call.callee); /* receiver / callee reads */
        return;
    }
    case AST_FIELD:
        ew_expr(e, n->as.field_access.object);
        return;
    case AST_INDEX:
        for (int i = n->as.index_expr.index_count - 1; i >= 0; i--)
            if (n->as.index_expr.indices)
                ew_expr(e, n->as.index_expr.indices[i]);
        ew_expr(e, n->as.index_expr.index);
        ew_expr(e, n->as.index_expr.object);
        return;
    case AST_BINARY:
        if (n->as.binary.lowered)
            ew_expr(e, n->as.binary.lowered);
        else
        {
            ew_expr(e, n->as.binary.right);
            ew_expr(e, n->as.binary.left);
        }
        return;
    case AST_UNARY:
        ew_expr(e, n->as.unary.operand);
        return;
    case AST_MUT_BORROW:
        ew_expr(e, n->as.mut_borrow.operand);
        return;
    case AST_FORMAT_STRING:
        for (int i = n->as.format_string.expr_count - 1; i >= 0; i--)
            ew_expr(e, n->as.format_string.exprs[i]);
        return;
    case AST_ARRAY_LIT:
        for (int i = n->as.array_lit.count - 1; i >= 0; i--)
            ew_expr(e, n->as.array_lit.elements[i]);
        return;
    case AST_MAP_LIT:
        for (int i = n->as.map_lit.pair_count - 1; i >= 0; i--)
        {
            ew_expr(e, n->as.map_lit.vals[i]);
            ew_expr(e, n->as.map_lit.keys[i]);
        }
        return;
    case AST_NEW_EXPR:
        for (int i = n->as.new_expr.field_init_count - 1; i >= 0; i--)
            ew_expr(e, n->as.new_expr.field_inits[i].value);
        return;
    case AST_CAST:
        ew_expr(e, n->as.cast.expr);
        return;
    case AST_RANGE:
        ew_expr(e, n->as.range.end);
        ew_expr(e, n->as.range.start);
        return;
    case AST_TRY:
        /* Early return on the error path only SHRINKS later uses — walking
           the inner expr with the fall-through state is conservative. */
        ew_expr(e, n->as.try_expr.expr);
        return;
    case AST_FORCE_UNWRAP:
        ew_expr(e, n->as.force_unwrap.message);
        ew_expr(e, n->as.force_unwrap.expr);
        return;
    case AST_AT_TIME:
        ew_expr(e, n->as.at_time.expr);
        return;
    case AST_AT_BENCH:
        ew_expr(e, n->as.at_bench.expr);
        return;
    case AST_FFI_CALL:
        for (int i = n->as.ffi_call.arg_count - 1; i >= 0; i--)
            ew_expr(e, n->as.ffi_call.args[i]);
        ew_expr(e, n->as.ffi_call.lib_expr);
        return;
    case AST_CLOSURE:
        /* Captures were hard-excluded in the prescan; still mark them as
           reads so nothing earlier elides from under the env build. */
        for (int i = 0; i < n->as.closure.capture_count; i++)
        {
            EVar *v = evar_find(e, n->as.closure.captures[i].name);
            if (v && v->in_scope) v->used = true;
        }
        return;
    case AST_MATCH:
    {
        /* Value-position match: arms merge like if/else. */
        bool *after = ev_snapshot(e);
        bool *merged = ev_snapshot(e);
        for (int i = 0; i < e->var_count; i++) merged[i] = false;
        for (int i = 0; i < n->as.match.arm_count; i++)
        {
            ev_restore(e, after);
            ew_stmt(e, n->as.match.arms[i].body);
            {
                EScan ps = { e, SCAN_READMARK, NULL };
                escan_pattern(&ps, n->as.match.arms[i].pattern);
            }
            for (int k = 0; k < e->var_count; k++)
                merged[k] = merged[k] || e->vars[k].used;
        }
        ev_restore(e, merged);
        ew_expr(e, n->as.match.subject);
        free(after);
        free(merged);
        return;
    }
    case AST_INT_LIT: case AST_FLOAT_LIT: case AST_STRING_LIT:
    case AST_BOOL_LIT: case AST_NIL_LIT: case AST_SIZEOF: case AST_TYPENAME:
        return;
    default:
        /* Anything not modeled precisely (comptime forms, future nodes):
           conservatively mark all candidate reads inside. */
        ew_mark_all_reads(e, n);
        return;
    }
}

static void ew_block(Elide *e, AstNode *n)
{
    /* Backward entry = the block's END: locals declared here are dead beyond
       it, so they enter scope with no later use; any same-named read outside
       (a global sibling) must not leak into their state. */
    for (int i = 0; i < e->var_count; i++)
    {
        if (e->vars[i].decl_block == n)
        {
            e->vars[i].in_scope = true;
            e->vars[i].used = false;
        }
    }
    for (int i = n->as.block.stmt_count - 1; i >= 0; i--)
        ew_stmt(e, n->as.block.stmts[i]);
    for (int i = 0; i < e->var_count; i++)
        if (e->vars[i].decl_block == n)
            e->vars[i].in_scope = false; /* belt + suspenders */
}

static void ew_stmt(Elide *e, AstNode *n)
{
    if (n == NULL) return;
    switch (n->kind)
    {
    case AST_BLOCK:
        ew_block(e, n);
        return;
    case AST_VAR_DECL:
    {
        /* Close the declared candidate's region: earlier same-name reads
           belong to another binding (unique-binder rule ⇒ a global). */
        EVar *v = evar_find(e, n->as.var_decl.name);
        if (v && v->decl == n) v->in_scope = false;
        /* Whitelist (b): `T x = e` — binding source. For has_drop structs
           the checker's binding-move (checker_try_mark_moved) already tagged
           it; the new coverage is has_drop ENUM sources, which bindings
           clone today. */
        ew_consume_or_read(e, n->as.var_decl.init);
        return;
    }
    case AST_ASSIGN:
        if (n->as.assign.op == TOKEN_ASSIGN &&
            n->as.assign.target && n->as.assign.target->kind == AST_IDENT)
        {
            /* Whitelist (b): `x = e`. Writing x is not a read of x (its old
               value is dropped moved_flag-conditionally), so the target
               contributes nothing; guard the degenerate self-assign. */
            AstNode *uw = elide_unwrap_move(n->as.assign.value);
            bool self_assign = uw && uw->kind == AST_IDENT &&
                strcmp(uw->as.ident.name,
                       n->as.assign.target->as.ident.name) == 0;
            if (self_assign)
                ew_expr(e, n->as.assign.value);
            else
                ew_consume_or_read(e, n->as.assign.value);
            return;
        }
        if (n->as.assign.op == TOKEN_ASSIGN &&
            n->as.assign.target && n->as.assign.target->kind == AST_FIELD)
        {
            /* Whitelist (b): `s.f = e` (codegen's field-assign already
               consults moved_out). The base object is read as an lvalue. */
            ew_consume_or_read(e, n->as.assign.value);
            ew_expr(e, n->as.assign.target->as.field_access.object);
            return;
        }
        /* Index targets / compound ops: plain reads (v[i]=e routes through
           __index_set at the CALL site when it is a user container). */
        ew_expr(e, n->as.assign.value);
        ew_expr(e, n->as.assign.target);
        return;
    case AST_RETURN:
        /* Whitelist (c): `return e`. Control leaves the function — nothing
           later on THIS path can read anything, so the state resets before
           the value walk (an enclosing if/loop merge re-ORs other paths). */
        for (int i = 0; i < e->var_count; i++) e->vars[i].used = false;
        ew_consume_or_read(e, n->as.return_stmt.value);
        return;
    case AST_IF:
    {
        bool *after = ev_snapshot(e);
        ew_stmt(e, n->as.if_stmt.then_block);
        bool *then_state = ev_snapshot(e);
        ev_restore(e, after);
        if (n->as.if_stmt.else_block)
            ew_stmt(e, n->as.if_stmt.else_block);
        /* merged = else-or-fallthrough state | then state */
        ev_or_into(e, then_state);
        ew_expr(e, n->as.if_stmt.cond);
        free(after);
        free(then_state);
        return;
    }
    case AST_WHILE:
        ew_loop_preset(e, n);
        ew_stmt(e, n->as.while_stmt.body);
        ew_expr(e, n->as.while_stmt.cond);
        return;
    case AST_FOR:
        if (n->as.for_stmt.desugared)
        {
            ew_stmt(e, n->as.for_stmt.desugared);
            return;
        }
        ew_loop_preset(e, n);
        ew_stmt(e, n->as.for_stmt.body);
        ew_expr(e, n->as.for_stmt.iter);
        return;
    case AST_FOR_C:
        ew_loop_preset(e, n);
        ew_stmt(e, n->as.for_c_stmt.body);
        ew_expr(e, n->as.for_c_stmt.update);
        ew_expr(e, n->as.for_c_stmt.cond);
        ew_stmt(e, n->as.for_c_stmt.init);
        return;
    case AST_MATCH:
        ew_expr(e, n); /* statement-position match: same arm merge */
        return;
    case AST_EXPR_STMT:
        ew_expr(e, n->as.expr_stmt.expr);
        return;
    case AST_BREAK: case AST_CONTINUE:
        /* Loop preset + the S_after the body walk started from already cover
           both exits conservatively. */
        return;
    default:
        ew_expr(e, n); /* expression-bodied arm / opaque stmt */
        return;
    }
}

/* ---- entry point ----

   Called once per fully-checked function body, at every body-check
   convergence site (free fns, methods, trait methods, module fns, and all
   generic instantiations — the cloned instance ASTs are exactly what codegen
   later consumes). Idempotent: moved_out only ever goes false→true and
   existing checker marks are skipped. */
void checker_elide_last_use(Checker *c, AstNode *fn_decl)
{
    if (c == NULL || !c->elide_pass_enabled || c->had_error) return;
    if (fn_decl == NULL || fn_decl->kind != AST_FN_DECL) return;
    AstNode *body = fn_decl->as.fn_decl.body;
    if (body == NULL || body->kind != AST_BLOCK) return;

    Elide e;
    memset(&e, 0, sizeof(e));

    /* Fn params (and self) are binders: a same-named local is a shadow. */
    for (int i = 0; i < fn_decl->as.fn_decl.param_count; i++)
        ename_bind(&e, fn_decl->as.fn_decl.param_names
                       ? fn_decl->as.fn_decl.param_names[i] : NULL);
    ename_bind(&e, "self");

    EScan pre = { &e, SCAN_PRESCAN, NULL };
    elide_scan(&pre, body);

    /* Fold name-level facts into the candidates; drop hopeless ones early. */
    int live = 0;
    for (int i = 0; i < e.var_count; i++)
    {
        EName *nm = ename_get(&e, e.vars[i].name);
        /* Exclusion predicate: shadowed / re-bound names — with 2+ binders a
           name-keyed backward scan cannot attribute reads to bindings. */
        if (nm->binds != 1 || nm->excluded)
            e.vars[i].excluded = true;
        if (!e.vars[i].excluded) live++;
    }

    if (live > 0)
        ew_block(&e, body);

    free(e.vars);
    free(e.names);
}
