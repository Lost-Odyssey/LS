/* checker_comptime.c — compile-time constant evaluation
   (docs/plan_comptime_consteval.md).

   Moved verbatim out of checker.c (Batch 4, Task 4.1,
   docs/plan_arch_round2_backlog.md): a self-contained subsystem covering
   `comptime for f in fields(T) { BODY }` field-iteration unroll (Stage 3b),
   a small AST interpreter for compile-time-constant scalar/array expressions
   (`comptime { ... }` blocks and bare const-folded expressions), and the
   `comptime const` declaration handler's supporting evaluator entry points.

   External call sites (both remain in checker.c): check_expr's and
   check_stmt's AST_BLOCK arms call comptime_expand_block before checking a
   block's statements; check_stmt's AST_COMPTIME_CONST arm drives CtEval /
   CtScalar / CtFlow directly to evaluate a `comptime const` declaration's
   initializer. The export surface in checker_internal.h is exactly those
   types plus the functions check_stmt reaches into. */
#include "checker_internal.h"
#include "builtins_math.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <math.h>

/* ---- Stage 3b: comptime field-iteration unroll ----
   `comptime for f in fields(T) { BODY }` is expanded here, at the point a block
   is checked (so the type alias for a generic T is already active and resolves to
   the concrete struct). For each field of T, BODY is deep-cloned and the comptime
   handles are rewritten to concrete leaves:
       f.name       -> "<field name>"   (string literal)
       f.index      -> <i>              (int literal)
       f.type_name  -> "<type name>"   (string literal)
       v.(f)        -> v.<field name>   (ordinary field access)
   Each field's clone is kept as its own nested block (a fresh scope, so any
   body-local declarations do not collide across iterations). The expanded
   statements replace the comptime-for in the parent block in place, so the
   checker and codegen only ever see ordinary AST (zero new codegen — mirrors the
   @derive / for-in / combinator desugaring打法). v1: read-only iteration; the
   comptime-if predicate evaluation lands in step 3. */

static AstNode *ct_str_lit(const char *s, int line, int col) {
    AstNode *n = ast_new(AST_STRING_LIT, line, col);
    n->as.string_lit.value  = chk_strdup(s);
    n->as.string_lit.length = (int)strlen(s);
    return n;
}

static AstNode *ct_int_lit(long long v, int line, int col) {
    AstNode *n = ast_new(AST_INT_LIT, line, col);
    n->as.int_lit.value   = v;
    n->as.int_lit.is_char = false;
    return n;
}

static AstNode *ct_bool_lit(bool v, int line, int col) {
    AstNode *n = ast_new(AST_BOOL_LIT, line, col);
    n->as.bool_lit.value = v;
    return n;
}

/* comptime v2 — `f.type` as a type value: lower the handle to a bare identifier
   naming the field's concrete type, so it works in type position — chiefly as a
   static-call receiver `f.type.from_value(x)` → `Int.from_value(x)` (same as the
   `T.from_value()` type-param dispatch). Using `f.type` as a runtime value is a
   clean "undefined" error from the resulting type-name identifier. */
static AstNode *ct_ident(const char *s, int line, int col) {
    AstNode *n = ast_new(AST_IDENT, line, col);
    n->as.ident.name = chk_strdup(s);
    return n;
}

/* Per-iteration rewrite context: the comptime handle (`f` / `vr`) bound to one
   concrete field or enum variant. `payload_count` is -1 in a field (fields(T))
   context and >= 0 in a variant (variants(T) / comptime match) context, where it
   also enables `<handle>.has_payload` / `<handle>.payload_count`. */
typedef struct {
    const char *handle;     /* loop var: "f" (field) / "vr" (variant) */
    const char *name;       /* field/variant name */
    long long   index;      /* field/variant index */
    const char *type_name;  /* field type / variant first-payload type ("" if none) */
    int         payload_count; /* -1 = field; >= 0 = variant payload arity */
} CtRw;

/* Recursively rewrite *slot for one bound field/variant (cx->handle). */
static void ct_rewrite(AstNode **slot, const CtRw *cx)
{
    AstNode *n = *slot;
    if (n == NULL) return;

    /* f.name / f.index / f.type_name / f.type (and, for variants, .has_payload /
       .payload_count) → literal / type-name identifier */
    if (n->kind == AST_FIELD && n->as.field_access.object &&
        n->as.field_access.object->kind == AST_IDENT &&
        strcmp(n->as.field_access.object->as.ident.name, cx->handle) == 0)
    {
        const char *m = n->as.field_access.field;
        AstNode *repl = NULL;
        if      (strcmp(m, "name") == 0)      repl = ct_str_lit(cx->name, n->line, n->column);
        else if (strcmp(m, "index") == 0)     repl = ct_int_lit(cx->index, n->line, n->column);
        else if (strcmp(m, "type_name") == 0) repl = ct_str_lit(cx->type_name, n->line, n->column);
        else if (strcmp(m, "type") == 0)      repl = ct_ident(cx->type_name, n->line, n->column);
        else if (cx->payload_count >= 0 && strcmp(m, "has_payload") == 0)
            repl = ct_bool_lit(cx->payload_count > 0, n->line, n->column);
        else if (cx->payload_count >= 0 && strcmp(m, "payload_count") == 0)
            repl = ct_int_lit(cx->payload_count, n->line, n->column);
        if (repl) { ast_free(n); *slot = repl; return; }
        /* unknown member on the handle — leave; the checker reports it as undefined */
    }

    /* v.(f) → v.<name> (ordinary field access). Rewrite inside the object first. */
    if (n->kind == AST_COMPTIME_FIELD &&
        strcmp(n->as.comptime_field.handle, cx->handle) == 0)
    {
        ct_rewrite(&n->as.comptime_field.object, cx);
        AstNode *fld = ast_new(AST_FIELD, n->line, n->column);
        fld->as.field_access.object = n->as.comptime_field.object;
        fld->as.field_access.field  = chk_strdup(cx->name);
        n->as.comptime_field.object = NULL; /* ownership transferred */
        ast_free(n);
        *slot = fld;
        return;
    }

    switch (n->kind) {
    case AST_UNARY:      ct_rewrite(&n->as.unary.operand, cx); break;
    case AST_MUT_BORROW: ct_rewrite(&n->as.mut_borrow.operand, cx); break;
    case AST_BINARY:
        ct_rewrite(&n->as.binary.left, cx);
        ct_rewrite(&n->as.binary.right, cx);
        ct_rewrite(&n->as.binary.lowered, cx);
        break;
    case AST_CALL:
        ct_rewrite(&n->as.call.callee, cx);
        for (int i = 0; i < n->as.call.arg_count; i++)
            ct_rewrite(&n->as.call.args[i], cx);
        break;
    case AST_INDEX:
        ct_rewrite(&n->as.index_expr.object, cx);
        if (n->as.index_expr.indices) {
            for (int i = 0; i < n->as.index_expr.index_count; i++)
                ct_rewrite(&n->as.index_expr.indices[i], cx);
        } else {
            ct_rewrite(&n->as.index_expr.index, cx);
        }
        break;
    case AST_FIELD:
        ct_rewrite(&n->as.field_access.object, cx);
        break;
    case AST_COMPTIME_FIELD: /* a different handle — recurse into the object */
        ct_rewrite(&n->as.comptime_field.object, cx);
        break;
    case AST_CAST:          ct_rewrite(&n->as.cast.expr, cx); break;
    case AST_TRY:           ct_rewrite(&n->as.try_expr.expr, cx); break;
    case AST_FORCE_UNWRAP:
        ct_rewrite(&n->as.force_unwrap.expr, cx);
        ct_rewrite(&n->as.force_unwrap.message, cx);
        break;
    case AST_RANGE:
        ct_rewrite(&n->as.range.start, cx);
        ct_rewrite(&n->as.range.end, cx);
        break;
    case AST_FORMAT_STRING:
        for (int i = 0; i < n->as.format_string.expr_count; i++)
            ct_rewrite(&n->as.format_string.exprs[i], cx);
        break;
    case AST_ARRAY_LIT:
        for (int i = 0; i < n->as.array_lit.count; i++)
            ct_rewrite(&n->as.array_lit.elements[i], cx);
        break;
    case AST_MAP_LIT:
        for (int i = 0; i < n->as.map_lit.pair_count; i++) {
            ct_rewrite(&n->as.map_lit.keys[i], cx);
            ct_rewrite(&n->as.map_lit.vals[i], cx);
        }
        break;
    case AST_CLOSURE:    ct_rewrite(&n->as.closure.body, cx); break;
    case AST_MATCH:
        ct_rewrite(&n->as.match.subject, cx);
        for (int i = 0; i < n->as.match.arm_count; i++) {
            ct_rewrite(&n->as.match.arms[i].pattern, cx);
            ct_rewrite(&n->as.match.arms[i].body, cx);
        }
        break;
    case AST_MATCH_OR_PATTERN:
        ct_rewrite(&n->as.or_pattern.left, cx);
        ct_rewrite(&n->as.or_pattern.right, cx);
        break;
    case AST_NEW_EXPR:
        for (int i = 0; i < n->as.new_expr.field_init_count; i++)
            ct_rewrite(&n->as.new_expr.field_inits[i].value, cx);
        break;
    case AST_VAR_DECL:   ct_rewrite(&n->as.var_decl.init, cx); break;
    case AST_ASSIGN:
        ct_rewrite(&n->as.assign.target, cx);
        ct_rewrite(&n->as.assign.value, cx);
        break;
    case AST_RETURN:     ct_rewrite(&n->as.return_stmt.value, cx); break;
    case AST_IF:
        ct_rewrite(&n->as.if_stmt.cond, cx);
        ct_rewrite(&n->as.if_stmt.then_block, cx);
        ct_rewrite(&n->as.if_stmt.else_block, cx);
        break;
    case AST_WHILE:
        ct_rewrite(&n->as.while_stmt.cond, cx);
        ct_rewrite(&n->as.while_stmt.body, cx);
        break;
    case AST_FOR:
        ct_rewrite(&n->as.for_stmt.iter, cx);
        ct_rewrite(&n->as.for_stmt.body, cx);
        break;
    case AST_FOR_C:
        ct_rewrite(&n->as.for_c_stmt.init, cx);
        ct_rewrite(&n->as.for_c_stmt.cond, cx);
        ct_rewrite(&n->as.for_c_stmt.update, cx);
        ct_rewrite(&n->as.for_c_stmt.body, cx);
        break;
    case AST_BLOCK:
        for (int i = 0; i < n->as.block.stmt_count; i++)
            ct_rewrite(&n->as.block.stmts[i], cx);
        break;
    case AST_EXPR_STMT:  ct_rewrite(&n->as.expr_stmt.expr, cx); break;
    case AST_COMPTIME_FOR:
        /* nested comptime for: rewrite its body unless its var shadows the handle */
        if (n->as.comptime_for.var == NULL ||
            strcmp(n->as.comptime_for.var, cx->handle) != 0)
            ct_rewrite(&n->as.comptime_for.body, cx);
        break;
    case AST_COMPTIME_IF:
        ct_rewrite(&n->as.comptime_if.cond, cx);
        ct_rewrite(&n->as.comptime_if.then_block, cx);
        ct_rewrite(&n->as.comptime_if.else_block, cx);
        break;
    case AST_COMPTIME_MATCH:
        /* nested comptime match: rewrite subject; rewrite body unless its handle
           or payload binder shadows the outer handle. */
        ct_rewrite(&n->as.comptime_match.subject, cx);
        if ((n->as.comptime_match.handle == NULL ||
             strcmp(n->as.comptime_match.handle, cx->handle) != 0) &&
            (n->as.comptime_match.binder == NULL ||
             strcmp(n->as.comptime_match.binder, cx->handle) != 0))
            ct_rewrite(&n->as.comptime_match.body, cx);
        break;
    default: break; /* literals, ident, sizeof, typename, break/continue: no f.* children */
    }
}

/* ---- Compile-time constant value evaluation (docs/plan_comptime_consteval.md) ----
   A small AST interpreter over the const subset: int/f64/bool/char literals,
   arithmetic / comparison / logical / bitwise / shift ops, `as` casts, references
   to other comptime constants, and math.* (constants + functions via host libm).
   Returns false (caller reports "not a compile-time constant") on anything outside
   the subset — a runtime variable, an unsupported op, a heap type, div-by-zero. */
double cts_to_f(const CtScalar *v) { return v->is_float ? v->f : (double)v->i; }

/* Comptime evaluation context: block-local variable bindings (Step 3) plus a step
   budget that bounds total work (guards against runaway compile-time loops). Names
   point into the AST (not owned). NULL `ev` is allowed for a pure scalar expression
   (Step 2) — then identifiers resolve only to outer comptime constants. */

static bool ct_env_get(const CtEval *ev, const char *name, CtScalar *out)
{
    if (ev == NULL) return false;
    for (int i = ev->count - 1; i >= 0; i--)  /* last binding wins (shadowing) */
        if (strcmp(ev->names[i], name) == 0) { *out = ev->vals[i]; return true; }
    return false;
}
static void ct_env_set(CtEval *ev, const char *name, const CtScalar *v)
{
    for (int i = ev->count - 1; i >= 0; i--)  /* update in place if already bound */
        if (strcmp(ev->names[i], name) == 0) { ev->vals[i] = *v; return; }
    if (ev->count >= ev->cap) {
        ev->cap = ev->cap < 8 ? 8 : ev->cap * 2;
        ev->names = realloc_safe((void *)ev->names, (size_t)ev->cap * sizeof(char *));
        ev->vals  = realloc_safe(ev->vals,          (size_t)ev->cap * sizeof(CtScalar));
    }
    ev->names[ev->count] = name;
    ev->vals[ev->count]  = *v;
    ev->count++;
}

/* Array-local bindings (Step 4): a zero-initialized scalar array bound by name. */
int ct_aenv_find(const CtEval *ev, const char *name)
{
    if (ev == NULL) return -1;
    for (int i = ev->acount - 1; i >= 0; i--)
        if (strcmp(ev->anames[i], name) == 0) return i;
    return -1;
}
static void ct_aenv_decl(CtEval *ev, const char *name, int len, bool is_float)
{
    if (ev->acount >= ev->acap) {
        ev->acap = ev->acap < 4 ? 4 : ev->acap * 2;
        ev->anames = realloc_safe((void *)ev->anames, (size_t)ev->acap * sizeof(char *));
        ev->arrs   = realloc_safe(ev->arrs,           (size_t)ev->acap * sizeof(CtScalar *));
        ev->alens  = realloc_safe(ev->alens,          (size_t)ev->acap * sizeof(int));
        ev->afloat = realloc_safe(ev->afloat,         (size_t)ev->acap * sizeof(bool));
    }
    CtScalar *a = malloc_safe((size_t)len * sizeof(CtScalar));
    for (int k = 0; k < len; k++) { a[k].is_float = is_float; a[k].i = 0; a[k].f = 0.0; }
    ev->anames[ev->acount] = name;
    ev->arrs[ev->acount]   = a;
    ev->alens[ev->acount]  = len;
    ev->afloat[ev->acount] = is_float;
    ev->acount++;
}
void ct_env_free(CtEval *ev)
{
    free((void *)ev->names); free(ev->vals);
    for (int i = 0; i < ev->acount; i++) free(ev->arrs[i]);
    free((void *)ev->anames); free(ev->arrs); free(ev->alens); free(ev->afloat);
}

/* `math.<name>` recognition: object must be the literal identifier `math`. */
static bool ct_field_is_math(const AstNode *e, const char **out_name)
{
    if (e == NULL || e->kind != AST_FIELD) return false;
    AstNode *obj = e->as.field_access.object;
    if (obj == NULL || obj->kind != AST_IDENT) return false;
    if (strcmp(obj->as.ident.name, "math") != 0) return false;
    *out_name = e->as.field_access.field;
    return true;
}


/* Evaluate `math.fn(args...)` at compile time using host libm. Returns false if the
   callee is not a recognized math function or an argument is not const. */
static bool ct_eval_math_call(Checker *c, AstNode *call, CtEval *ev, CtScalar *out)
{
    const char *fn = NULL;
    if (!ct_field_is_math(call->as.call.callee, &fn)) return false;
    int arity = 0; MathEmitKind mk; const char *en; MathPolyKind mp; const char *ip;
    if (!builtin_math_lookup_fn(fn, &arity, &mk, &en, &mp, &ip)) return false;
    if (call->as.call.arg_count != arity || arity < 1 || arity > 2) return false;

    CtScalar a, b = {0};  /* b is only assigned when arity==2; zero-init keeps the
                             min/max path (which is always arity 2) defined even if
                             the arity table were ever wrong. Silences C4701. */
    if (!ct_eval_scalar(c, call->as.call.args[0], ev, &a)) return false;
    if (arity == 2 && !ct_eval_scalar(c, call->as.call.args[1], ev, &b)) return false;

    /* abs/min/max are int-or-float polymorphic: keep integer-ness when all-integer. */
    if (strcmp(fn, "abs") == 0) {
        if (!a.is_float) { out->is_float = false; out->i = llabs(a.i); }
        else             { out->is_float = true;  out->f = fabs(a.f); }
        return true;
    }
    if (strcmp(fn, "min") == 0 || strcmp(fn, "max") == 0) {
        bool is_min = (fn[1] == 'i');
        if (!a.is_float && !b.is_float) {
            out->is_float = false;
            out->i = is_min ? (a.i < b.i ? a.i : b.i) : (a.i > b.i ? a.i : b.i);
        } else {
            double x = cts_to_f(&a), y = cts_to_f(&b);
            out->is_float = true;
            out->f = is_min ? (x < y ? x : y) : (x > y ? x : y);
        }
        return true;
    }

    double x = cts_to_f(&a), y = (arity == 2) ? cts_to_f(&b) : 0.0, r;
    if      (strcmp(fn, "sqrt")  == 0) r = sqrt(x);
    else if (strcmp(fn, "sin")   == 0) r = sin(x);
    else if (strcmp(fn, "cos")   == 0) r = cos(x);
    else if (strcmp(fn, "tan")   == 0) r = tan(x);
    else if (strcmp(fn, "asin")  == 0) r = asin(x);
    else if (strcmp(fn, "acos")  == 0) r = acos(x);
    else if (strcmp(fn, "atan")  == 0) r = atan(x);
    else if (strcmp(fn, "atan2") == 0) r = atan2(x, y);
    else if (strcmp(fn, "exp")   == 0) r = exp(x);
    else if (strcmp(fn, "log")   == 0) r = log(x);
    else if (strcmp(fn, "log2")  == 0) r = log2(x);
    else if (strcmp(fn, "log10") == 0) r = log10(x);
    else if (strcmp(fn, "pow")   == 0) r = pow(x, y);
    else if (strcmp(fn, "floor") == 0) r = floor(x);
    else if (strcmp(fn, "ceil")  == 0) r = ceil(x);
    else return false;  /* a math fn we don't evaluate at comptime yet */
    out->is_float = true; out->f = r;
    return true;
}

bool ct_eval_scalar(Checker *c, AstNode *e, CtEval *ev, CtScalar *out)
{
    if (e == NULL) return false;
    switch (e->kind)
    {
    case AST_INT_LIT:   out->is_float = false; out->i = e->as.int_lit.value;          return true;
    case AST_FLOAT_LIT: out->is_float = true;  out->f = e->as.float_lit.value;         return true;
    case AST_BOOL_LIT:  out->is_float = false; out->i = e->as.bool_lit.value ? 1 : 0;  return true;

    case AST_IDENT: {
        if (ct_env_get(ev, e->as.ident.name, out)) return true; /* block-local comptime var */
        Symbol *s = scope_resolve(c->current_scope, e->as.ident.name);
        if (s && s->is_comptime_const) {
            out->is_float = s->ct_is_float; out->i = s->ct_i; out->f = s->ct_f;
            return true;
        }
        return false;  /* runtime variable / undefined → not a comptime constant */
    }

    case AST_FIELD: {  /* math constant: math.PI / math.E / ... */
        const char *nm = NULL; double cv;
        if (ct_field_is_math(e, &nm) && builtin_math_lookup_const(nm, &cv)) {
            out->is_float = true; out->f = cv; return true;
        }
        return false;
    }

    case AST_INDEX: {  /* read a comptime array-local element: t[i] (Step 4) */
        if (ev == NULL || e->as.index_expr.index == NULL) return false;
        AstNode *obj = e->as.index_expr.object;
        if (obj == NULL || obj->kind != AST_IDENT) return false;
        int ai = ct_aenv_find(ev, obj->as.ident.name);
        if (ai < 0) return false;
        CtScalar idx;
        if (!ct_eval_scalar(c, e->as.index_expr.index, ev, &idx) || idx.is_float) return false;
        if (idx.i < 0 || idx.i >= ev->alens[ai]) return false;  /* out of bounds */
        *out = ev->arrs[ai][idx.i];
        return true;
    }

    case AST_CALL: return ct_eval_math_call(c, e, ev, out);

    case AST_CAST: {
        CtScalar v;
        if (!ct_eval_scalar(c, e->as.cast.expr, ev, &v)) return false;
        Type *tt = resolve_type_node(c, e->as.cast.target_type, e->line, e->column);
        if (tt == NULL) return false;
        if (type_is_float(tt)) { out->is_float = true; out->f = cts_to_f(&v); return true; }
        if (type_is_integer(tt) || tt->kind == TYPE_CHAR) {
            out->is_float = false; out->i = v.is_float ? (long long)v.f : v.i; return true;
        }
        if (tt->kind == TYPE_BOOL) {
            out->is_float = false; out->i = (cts_to_f(&v) != 0.0) ? 1 : 0; return true;
        }
        return false;
    }

    case AST_UNARY: {
        CtScalar v;
        if (!ct_eval_scalar(c, e->as.unary.operand, ev, &v)) return false;
        switch (e->as.unary.op) {
        case TOKEN_MINUS:
            if (v.is_float) { out->is_float = true; out->f = -v.f; }
            else            { out->is_float = false; out->i = -v.i; }
            return true;
        case TOKEN_BANG:
            out->is_float = false; out->i = (cts_to_f(&v) != 0.0) ? 0 : 1; return true;
        case TOKEN_TILDE:
            if (v.is_float) return false;
            out->is_float = false; out->i = ~v.i; return true;
        default: return false;
        }
    }

    case AST_BINARY: {
        CtScalar L, R;
        if (!ct_eval_scalar(c, e->as.binary.left,  ev, &L)) return false;
        if (!ct_eval_scalar(c, e->as.binary.right, ev, &R)) return false;
        TokenType op = e->as.binary.op;

        /* comparisons and logical → boolean (stored as int 0/1) */
        switch (op) {
        case TOKEN_LT:  out->is_float = false; out->i = (cts_to_f(&L) <  cts_to_f(&R)) ? 1 : 0; return true;
        case TOKEN_GT:  out->is_float = false; out->i = (cts_to_f(&L) >  cts_to_f(&R)) ? 1 : 0; return true;
        case TOKEN_LEQ: out->is_float = false; out->i = (cts_to_f(&L) <= cts_to_f(&R)) ? 1 : 0; return true;
        case TOKEN_GEQ: out->is_float = false; out->i = (cts_to_f(&L) >= cts_to_f(&R)) ? 1 : 0; return true;
        case TOKEN_EQ:  out->is_float = false; out->i = (cts_to_f(&L) == cts_to_f(&R)) ? 1 : 0; return true;
        case TOKEN_NEQ: out->is_float = false; out->i = (cts_to_f(&L) != cts_to_f(&R)) ? 1 : 0; return true;
        case TOKEN_AND: out->is_float = false; out->i = (cts_to_f(&L) != 0.0 && cts_to_f(&R) != 0.0) ? 1 : 0; return true;
        case TOKEN_OR:  out->is_float = false; out->i = (cts_to_f(&L) != 0.0 || cts_to_f(&R) != 0.0) ? 1 : 0; return true;
        default: break;
        }

        if (L.is_float || R.is_float) {
            double a = cts_to_f(&L), b = cts_to_f(&R), r;
            switch (op) {
            case TOKEN_PLUS:  r = a + b; break;
            case TOKEN_MINUS: r = a - b; break;
            case TOKEN_STAR:  r = a * b; break;
            case TOKEN_SLASH: if (b == 0.0) return false; r = a / b; break;
            default: return false;  /* %, bit/shift not valid on floats */
            }
            out->is_float = true; out->f = r; return true;
        } else {
            long long a = L.i, b = R.i, r;
            switch (op) {
            case TOKEN_PLUS:    r = a + b; break;
            case TOKEN_MINUS:   r = a - b; break;
            case TOKEN_STAR:    r = a * b; break;
            case TOKEN_SLASH:   if (b == 0) return false; r = a / b; break;
            case TOKEN_PERCENT: if (b == 0) return false; r = a % b; break;
            case TOKEN_AMP:     r = a & b; break;
            case TOKEN_PIPE:    r = a | b; break;
            case TOKEN_CARET:   r = a ^ b; break;
            case TOKEN_LSHIFT:  r = a << b; break;
            case TOKEN_RSHIFT:  r = a >> b; break;
            default: return false;
            }
            out->is_float = false; out->i = r; return true;
        }
    }

    default: return false;
    }
}

/* ---- Comptime block interpreter (Step 3) ---------------------------------------
   Executes a `comptime { ... return v }` block: scalar local decls/assignments,
   if/else, bounded `for i in 0..N`, and `return <expr>`. Mutates `ev` (block-local
   bindings) and decrements ev->budget per step. CT_RETURNED → `*ret` holds the
   value; CT_NORMAL → fell off the end (caller errors: must return); CT_FAIL → not
   compile-time-constant / unsupported stmt / div0 / budget exceeded. */


static CtFlow ct_exec_stmt(Checker *c, AstNode *s, CtEval *ev, CtScalar *ret)
{
    if (s == NULL) return CT_NORMAL;
    if (--ev->budget <= 0) return CT_FAIL;

    switch (s->kind)
    {
    case AST_VAR_DECL: {
        Type *dt = s->as.var_decl.var_type
                       ? resolve_type_node(c, s->as.var_decl.var_type, s->line, s->column)
                       : NULL;
        if (dt && dt->kind == TYPE_ARRAY) {
            /* array local `array(T,N) t = {}` — zero-initialized; the loop fills it.
               The init expression's contents are ignored in v1 (idiom is `= {}`). */
            int n = dt->as.array.size;
            Type *el = dt->as.array.elem;
            bool es = el && (type_is_numeric(el) || el->kind == TYPE_BOOL || el->kind == TYPE_CHAR);
            if (n <= 0 || !es) return CT_FAIL;
            ct_aenv_decl(ev, s->as.var_decl.name, n, type_is_float(el));
            return CT_NORMAL;
        }
        if (s->as.var_decl.init == NULL) return CT_FAIL;
        CtScalar v;
        if (!ct_eval_scalar(c, s->as.var_decl.init, ev, &v)) return CT_FAIL;
        if (dt) {
            if (type_is_float(dt)) { v.f = cts_to_f(&v); v.is_float = true; }
            else if (v.is_float)   return CT_FAIL;  /* float init for int local */
        }
        ct_env_set(ev, s->as.var_decl.name, &v);
        return CT_NORMAL;
    }

    case AST_ASSIGN: {
        AstNode *tgt = s->as.assign.target;
        if (tgt->kind == AST_INDEX) {
            /* array element assignment: t[i] = expr (Step 4); op must be plain `=` */
            AstNode *obj = tgt->as.index_expr.object;
            if (s->as.assign.op != TOKEN_ASSIGN || tgt->as.index_expr.index == NULL ||
                obj == NULL || obj->kind != AST_IDENT)
                return CT_FAIL;
            int ai = ct_aenv_find(ev, obj->as.ident.name);
            if (ai < 0) return CT_FAIL;
            CtScalar idx, val;
            if (!ct_eval_scalar(c, tgt->as.index_expr.index, ev, &idx) || idx.is_float) return CT_FAIL;
            if (idx.i < 0 || idx.i >= ev->alens[ai]) return CT_FAIL;  /* out of bounds */
            if (!ct_eval_scalar(c, s->as.assign.value, ev, &val)) return CT_FAIL;
            if (ev->afloat[ai]) { val.f = cts_to_f(&val); val.is_float = true; }
            else if (val.is_float) return CT_FAIL;  /* float into int array */
            ev->arrs[ai][idx.i] = val;
            return CT_NORMAL;
        }
        if (tgt->kind != AST_IDENT) return CT_FAIL;
        const char *nm = tgt->as.ident.name;
        CtScalar rhs;
        if (!ct_eval_scalar(c, s->as.assign.value, ev, &rhs)) return CT_FAIL;
        TokenType op = s->as.assign.op;
        if (op != TOKEN_ASSIGN) {  /* compound: cur <op> rhs */
            CtScalar cur;
            if (!ct_env_get(ev, nm, &cur)) return CT_FAIL;
            if (cur.is_float || rhs.is_float) {
                double a = cts_to_f(&cur), b = cts_to_f(&rhs), r;
                switch (op) {
                case TOKEN_PLUS_ASSIGN:  r = a + b; break;
                case TOKEN_MINUS_ASSIGN: r = a - b; break;
                case TOKEN_STAR_ASSIGN:  r = a * b; break;
                case TOKEN_SLASH_ASSIGN: if (b == 0.0) return CT_FAIL; r = a / b; break;
                default: return CT_FAIL;
                }
                rhs.is_float = true; rhs.f = r;
            } else {
                long long a = cur.i, b = rhs.i, r;
                switch (op) {
                case TOKEN_PLUS_ASSIGN:  r = a + b; break;
                case TOKEN_MINUS_ASSIGN: r = a - b; break;
                case TOKEN_STAR_ASSIGN:  r = a * b; break;
                case TOKEN_SLASH_ASSIGN: if (b == 0) return CT_FAIL; r = a / b; break;
                default: return CT_FAIL;
                }
                rhs.is_float = false; rhs.i = r;
            }
        }
        ct_env_set(ev, nm, &rhs);
        return CT_NORMAL;
    }

    case AST_IF: {
        CtScalar cond;
        if (!ct_eval_scalar(c, s->as.if_stmt.cond, ev, &cond)) return CT_FAIL;
        if (cts_to_f(&cond) != 0.0)
            return ct_exec_block(c, s->as.if_stmt.then_block, ev, ret);
        if (s->as.if_stmt.else_block)
            return ct_exec_block(c, s->as.if_stmt.else_block, ev, ret);
        return CT_NORMAL;
    }

    case AST_FOR: {
        /* bounded `for i in lo..hi` only (range iter; no user containers) */
        AstNode *it = s->as.for_stmt.iter;
        if (it == NULL || it->kind != AST_RANGE) return CT_FAIL;
        CtScalar lo, hi;
        if (!ct_eval_scalar(c, it->as.range.start, ev, &lo)) return CT_FAIL;
        if (!ct_eval_scalar(c, it->as.range.end,   ev, &hi)) return CT_FAIL;
        if (lo.is_float || hi.is_float) return CT_FAIL;
        for (long long iv = lo.i; iv < hi.i; iv++) {
            if (--ev->budget <= 0) return CT_FAIL;
            CtScalar civ; civ.is_float = false; civ.i = iv; civ.f = 0.0;
            ct_env_set(ev, s->as.for_stmt.var, &civ);
            CtFlow f = ct_exec_block(c, s->as.for_stmt.body, ev, ret);
            if (f != CT_NORMAL) return f;  /* RETURNED / FAIL propagate out of loop */
        }
        return CT_NORMAL;
    }

    case AST_BLOCK:
        return ct_exec_block(c, s, ev, ret);

    case AST_EXPR_STMT: {
        CtScalar tmp;  /* a bare expression statement: must still be const-evaluable */
        if (!ct_eval_scalar(c, s->as.expr_stmt.expr, ev, &tmp)) return CT_FAIL;
        return CT_NORMAL;
    }

    case AST_RETURN: {
        AstNode *rv = s->as.return_stmt.value;
        if (rv == NULL) return CT_FAIL;
        if (rv->kind == AST_IDENT && ct_aenv_find(ev, rv->as.ident.name) >= 0) {
            ev->ret_array = rv->as.ident.name;  /* array result (Step 4) */
            return CT_RETURNED;
        }
        if (!ct_eval_scalar(c, rv, ev, ret)) return CT_FAIL;
        return CT_RETURNED;
    }

    default:
        return CT_FAIL;  /* while / break / continue / nested decls: unsupported in v1 */
    }
}

CtFlow ct_exec_block(Checker *c, AstNode *blk, CtEval *ev, CtScalar *ret)
{
    if (blk == NULL) return CT_NORMAL;
    if (blk->kind != AST_BLOCK) return ct_exec_stmt(c, blk, ev, ret);  /* single stmt */
    for (int i = 0; i < blk->as.block.stmt_count; i++) {
        CtFlow f = ct_exec_stmt(c, blk->as.block.stmts[i], ev, ret);
        if (f != CT_NORMAL) return f;
    }
    return CT_NORMAL;
}

/* Evaluate a comptime-constant boolean predicate (step 3). After ct_rewrite has
   substituted f.name/f.index/f.type_name to literals, a `comptime if` condition is
   an expression over string/int/bool literals + comparisons + && || !. Sets
   *ok=false (caller reports) if it is not a compile-time constant boolean. */
static bool ct_eval_bool(AstNode *e, bool *ok)
{
    if (e == NULL) { *ok = false; return false; }
    if (e->kind == AST_BOOL_LIT) return e->as.bool_lit.value;
    if (e->kind == AST_UNARY && e->as.unary.op == TOKEN_BANG)
        return !ct_eval_bool(e->as.unary.operand, ok);
    if (e->kind == AST_BINARY) {
        TokenType op = e->as.binary.op;
        AstNode *L = e->as.binary.left, *R = e->as.binary.right;
        if (op == TOKEN_AND) { bool a = ct_eval_bool(L, ok); bool b = ct_eval_bool(R, ok); return a && b; }
        if (op == TOKEN_OR)  { bool a = ct_eval_bool(L, ok); bool b = ct_eval_bool(R, ok); return a || b; }
        if (L && R && L->kind == AST_STRING_LIT && R->kind == AST_STRING_LIT) {
            int cmp = strcmp(L->as.string_lit.value, R->as.string_lit.value);
            switch (op) {
            case TOKEN_EQ:  return cmp == 0;   case TOKEN_NEQ: return cmp != 0;
            case TOKEN_LT:  return cmp < 0;    case TOKEN_GT:  return cmp > 0;
            case TOKEN_LEQ: return cmp <= 0;   case TOKEN_GEQ: return cmp >= 0;
            default: *ok = false; return false;
            }
        }
        if (L && R && L->kind == AST_INT_LIT && R->kind == AST_INT_LIT) {
            long long a = L->as.int_lit.value, b = R->as.int_lit.value;
            switch (op) {
            case TOKEN_EQ:  return a == b;     case TOKEN_NEQ: return a != b;
            case TOKEN_LT:  return a < b;      case TOKEN_GT:  return a > b;
            case TOKEN_LEQ: return a <= b;     case TOKEN_GEQ: return a >= b;
            default: *ok = false; return false;
            }
        }
        *ok = false; return false;
    }
    *ok = false; return false;
}

/* Expand every AST_COMPTIME_FOR / AST_COMPTIME_IF statement directly in `block`,
   in place. */
void comptime_expand_block(Checker *c, AstNode *block)
{
    if (block == NULL || block->kind != AST_BLOCK) return;
    bool any = false;
    for (int i = 0; i < block->as.block.stmt_count; i++) {
        AstNode *s = block->as.block.stmts[i];
        if (s && (s->kind == AST_COMPTIME_FOR || s->kind == AST_COMPTIME_IF ||
                  s->kind == AST_COMPTIME_MATCH)) {
            any = true; break;
        }
    }
    if (!any) return;

    AstNode **out = NULL; int nc = 0, ncap = 0;
    #define CT_PUSH(node) do { \
        if (nc >= ncap) { ncap = ncap ? ncap * 2 : 8; \
            out = realloc_safe(out, (size_t)ncap * sizeof(AstNode *)); } \
        out[nc++] = (node); } while (0)

    for (int i = 0; i < block->as.block.stmt_count; i++) {
        AstNode *s = block->as.block.stmts[i];
        if (s == NULL) continue;

        /* comptime if: evaluate the (now-constant) predicate and keep only the
           taken branch. The dropped branch is freed without ever being checked —
           so code that only type-checks for some fields does not poison others.
           Iterate through `else comptime if` chains. */
        if (s->kind == AST_COMPTIME_IF) {
            AstNode *cur = s, *result = NULL;
            while (cur && cur->kind == AST_COMPTIME_IF) {
                bool ok = true;
                bool cond = ct_eval_bool(cur->as.comptime_if.cond, &ok);
                if (!ok) {
                    checker_error(c, cur->line, cur->column,
                        "comptime if condition must be a compile-time constant "
                        "(over f.name / f.index / f.type_name)");
                    ast_free(cur);
                    cur = NULL;
                    break;
                }
                if (cond) {
                    result = cur->as.comptime_if.then_block;
                    cur->as.comptime_if.then_block = NULL;
                    ast_free(cur);
                    cur = NULL;
                } else {
                    AstNode *els = cur->as.comptime_if.else_block;
                    cur->as.comptime_if.else_block = NULL;
                    ast_free(cur);
                    cur = els;  /* block (else {}), comptime_if (chain), or NULL */
                    if (cur && cur->kind != AST_COMPTIME_IF) { result = cur; cur = NULL; }
                }
            }
            if (result) CT_PUSH(result);  /* a fresh-scope nested block */
            continue;
        }

        /* comptime match v { vr(p) => body }: expand the single generic arm into a
           real `match v { Variant(p) => body, ... }` — one arm per enum variant,
           binding the active variant's first payload to the user's binder (the rest
           to `_`), and substituting vr.name / vr.index / vr.has_payload /
           vr.payload_count / vr.type_name in the cloned body. Reuses the mature
           match check + codegen (drop/move/exhaustiveness) — zero new ownership
           code, same打法 as the for-in / combinator desugaring. */
        if (s->kind == AST_COMPTIME_MATCH) {
            Type *subj_t = check_expr(c, s->as.comptime_match.subject);
            Type *en = subj_t;
            while (en && (en->kind == TYPE_REFERENCE || en->kind == TYPE_POINTER) &&
                   en->as.pointer_to)
                en = en->as.pointer_to;
            if (en == NULL || en->kind != TYPE_ENUM) {
                checker_error(c, s->line, s->column,
                    "comptime match requires an enum subject, got '%s'",
                    subj_t ? type_name(subj_t) : "<error>");
                ast_free(s);
                continue;
            }
            const char *vhandle = s->as.comptime_match.handle;
            const char *binder  = s->as.comptime_match.binder;
            int vc = en->as.enom.variant_count;
            MatchArm *arms = (MatchArm *)malloc_safe((size_t)vc * sizeof(MatchArm));
            for (int vi = 0; vi < vc; vi++) {
                const char *vname = en->as.enom.variants[vi].name;
                int pc = en->as.enom.variants[vi].payload_count;
                /* pattern: bare `Variant` (no payload) or `Variant(p, _, ...)`. */
                AstNode *pat;
                if (pc <= 0) {
                    pat = ct_ident(vname, s->line, s->column);
                } else {
                    pat = ast_new(AST_CALL, s->line, s->column);
                    pat->as.call.callee = ct_ident(vname, s->line, s->column);
                    pat->as.call.args = (AstNode **)malloc_safe((size_t)pc * sizeof(AstNode *));
                    pat->as.call.arg_count = pc;
                    for (int b = 0; b < pc; b++)
                        pat->as.call.args[b] =
                            ct_ident((b == 0 && binder) ? binder : "_", s->line, s->column);
                }
                /* body: clone + substitute the variant handle (vr.*). */
                const char *vtn = "";
                if (pc > 0 && en->as.enom.variants[vi].payload_types &&
                    en->as.enom.variants[vi].payload_types[0])
                    vtn = type_name(en->as.enom.variants[vi].payload_types[0]);
                char *vtn_owned = chk_strdup(vtn);
                AstNode *bcopy = ast_clone_deep(s->as.comptime_match.body);
                CtRw cx = { vhandle, vname, (long long)vi, vtn_owned, pc };
                ct_rewrite(&bcopy, &cx);
                free(vtn_owned);
                arms[vi].pattern = pat;
                arms[vi].body    = bcopy;
            }
            AstNode *mt = ast_new(AST_MATCH, s->line, s->column);
            mt->as.match.subject   = s->as.comptime_match.subject;  /* transfer ownership */
            mt->as.match.arms      = arms;
            mt->as.match.arm_count = vc;
            s->as.comptime_match.subject = NULL;
            AstNode *es = ast_new(AST_EXPR_STMT, s->line, s->column);
            es->as.expr_stmt.expr = mt;
            ast_free(s);
            CT_PUSH(es);
            continue;
        }

        if (s->kind != AST_COMPTIME_FOR) { CT_PUSH(s); continue; }

        Type *st = resolve_type_node(c, s->as.comptime_for.over_type, s->line, s->column);
        if (st == NULL) { ast_free(s); continue; }  /* resolve already reported */
        const char *fvar = s->as.comptime_for.var;

        if (s->as.comptime_for.over_variants) {
            /* comptime v2 — variants(T): unroll the body once per enum variant,
               exposing vr.name (Str) / vr.index (int) / vr.type_name (payload type
               name, "" when the variant has no payload). Metadata-only (mirrors
               fields(T)); value dispatch over the active variant still needs a
               runtime `match self { ... }`. */
            if (st->kind != TYPE_ENUM) {
                checker_error(c, s->line, s->column,
                    "comptime for requires an enum type in variants(...), got '%s'",
                    type_name(st));
                ast_free(s);
                continue;
            }
            int vc = st->as.enom.variant_count;
            for (int vi = 0; vi < vc; vi++) {
                const char *vname = st->as.enom.variants[vi].name;
                int pc = st->as.enom.variants[vi].payload_count;
                /* payload type name: first payload's type, "" when no payload. */
                const char *vtn = "";
                if (pc > 0 && st->as.enom.variants[vi].payload_types &&
                    st->as.enom.variants[vi].payload_types[0])
                    vtn = type_name(st->as.enom.variants[vi].payload_types[0]);
                char *vtn_owned = chk_strdup(vtn); /* type_name() reuses a pooled buffer */
                AstNode *copy = ast_clone_deep(s->as.comptime_for.body);  /* AST_BLOCK */
                CtRw cx = { fvar, vname, (long long)vi, vtn_owned, pc };
                ct_rewrite(&copy, &cx);
                free(vtn_owned);
                CT_PUSH(copy);  /* one nested block per variant = its own scope */
            }
            ast_free(s);
            continue;
        }

        if (st->kind != TYPE_STRUCT) {
            checker_error(c, s->line, s->column,
                "comptime for requires a struct type in fields(...), got '%s'",
                type_name(st));
            ast_free(s);
            continue;
        }
        int fc = st->as.strukt.field_count;
        for (int fi = 0; fi < fc; fi++) {
            const char *fname = st->as.strukt.fields[fi].name;
            /* v1: type_name() as-is (may carry a module prefix for imported generic
               instances — refine later; local/primitive field types are clean). */
            const char *ftn = st->as.strukt.fields[fi].type
                ? type_name(st->as.strukt.fields[fi].type) : "void";
            char *ftn_owned = chk_strdup(ftn); /* type_name() reuses a pooled buffer */
            AstNode *copy = ast_clone_deep(s->as.comptime_for.body);  /* AST_BLOCK */
            CtRw cx = { fvar, fname, (long long)fi, ftn_owned, -1 };
            ct_rewrite(&copy, &cx);
            free(ftn_owned);
            CT_PUSH(copy);  /* one nested block per field = its own scope */
        }
        ast_free(s);
    }
    #undef CT_PUSH

    free(block->as.block.stmts);
    block->as.block.stmts = out;
    block->as.block.stmt_count = nc;
}

