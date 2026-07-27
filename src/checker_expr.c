/* checker_expr.c — expression-checking cluster, split out of checker.c
   (Batch 7 Task 7.5, docs/plan_arch_round2_backlog.md §7.5): the
   check_expr_* helpers and the check_expr dispatcher. The call-expression
   side (check_builtin_* families, intrinsic registry, check_call_* helpers,
   check_expr_call) lives in checker_call.c.
   Bodies moved verbatim — cross-TU surface is in checker_internal.h. */
#include "checker.h"
#include "checker_internal.h"
#include "builtins_math.h"
#include "mangle.h"
#include "diag.h"
#include "module.h"
#include "builtins_perf.h"
#include <stdio.h>
#include <string.h>
#include <stdarg.h>
#include <stdlib.h>
#include <math.h>

static Type *check_expr_ident(Checker *c, AstNode *node)
{
    Type *result = NULL;
    do {
        /* If the identifier is a builtin function, don't report "undefined variable" */
        if (is_builtin_function(node->as.ident.name))
        {
            result = NULL; /* Signal to caller to check for builtin */
            break;
        }
        Symbol *sym = scope_resolve(c->current_scope, node->as.ident.name);
        if (sym == NULL)
        {
            /* Ambient builtin module: a generic method body (e.g. std.tensor's
               exp/sigmoid/tanh using `math.exp`) is re-checked at the CONSUMER
               site, where an `import math` alias is not in scope — it would
               otherwise fail with "undefined variable 'math'". A builtin module
               name (math/perf/...) that is NOT shadowed by a local symbol and
               has no overriding user .ls file resolves on demand here, mirroring
               the ambient std.sys.c.* canonical path (match_stdc_prim). A user
               variable of the same name is found by scope_resolve above, so it
               always wins. */
            if (builtin_module_exists(node->as.ident.name) &&
                !module_user_file_exists(node->as.ident.name, c->source_path))
            {
                Type *mt = builtin_module_make_type_merged(c, node->as.ident.name);
                if (mt)
                {
                    node->resolved_type = mt;
                    result = mt;
                    break;
                }
            }
            /* Try variant-ctor recognition for no-payload variants (e.g. `Red`, `None`) */
            Type *enum_type = NULL;
            int variant_idx = -1;
            int matches = find_variant(c, node->as.ident.name, &enum_type, &variant_idx);
            if (matches == 1 && enum_type->as.enom.variants[variant_idx].payload_count == 0)
            {
                node->resolved_type = enum_type;
                result = enum_type;
                break;
            }
            if (matches > 1)
            {
                /* Disambiguate by a type hint when available (the node's own prior
                   resolution, or the expected type — e.g. a typed LHS or a
                   combinator's pushed result type). */
                Type *eet = NULL; int evi = -1;
                if (disambig_variant_by_hint(c, node, node->as.ident.name, &eet, &evi) &&
                    eet->as.enom.variants[evi].payload_count == 0)
                {
                    node->resolved_type = eet;
                    result = eet;
                    break;
                }
                checker_error(c, node->line, node->column,
                              "ambiguous variant name '%s' (matches multiple enums; "
                              "explicit construction or type annotation required)",
                              node->as.ident.name);
                result = NULL;
                break;
            }
            if (matches == 1)
            {
                checker_error(c, node->line, node->column,
                              "variant '%s' expects %d payload argument(s); use '%s(...)' to construct",
                              node->as.ident.name,
                              enum_type->as.enom.variants[variant_idx].payload_count,
                              node->as.ident.name);
                result = NULL;
                break;
            }
            /* #3 hint: a bare dotted import (`import std.text.csv`) binds only
               the full path, not the short segment. If `name` is the last
               segment of an imported module's path, the user almost certainly
               meant a namespaced call (`csv.parse`) — point them at the alias
               form instead of a bare "undefined variable". */
            const char *modpath = NULL;
            {
                size_t nl = strlen(node->as.ident.name);
                for (Scope *sc = c->current_scope; sc && !modpath; sc = sc->parent)
                {
                    for (int si = 0; si < sc->count; si++)
                    {
                        Type *st = sc->symbols[si].type;
                        const char *sn = sc->symbols[si].name;
                        if (st && st->kind == TYPE_MODULE && sn)
                        {
                            size_t snl = strlen(sn);
                            if (snl > nl + 1 && sn[snl - nl - 1] == '.' &&
                                strcmp(sn + snl - nl, node->as.ident.name) == 0)
                            {
                                modpath = sn;
                                break;
                            }
                        }
                    }
                }
            }
            if (modpath)
                checker_error(c, node->line, node->column,
                              "undefined variable '%s'; for module '%s' add an alias: "
                              "`import %s as %s` (a plain dotted import binds only the "
                              "full path)", node->as.ident.name, modpath, modpath,
                              node->as.ident.name);
            else
            {
                char helpbuf[256];
                DiagScopeIter it = { c->current_scope, 0 };
                const char *help = diag_help_suggestion(
                    helpbuf, sizeof(helpbuf), node->as.ident.name,
                    diag_scope_iter_next, &it);
                checker_error_help(c, node->line, node->column,
                                   (int)strlen(node->as.ident.name), help,
                                   "undefined variable '%s'", node->as.ident.name);
            }
            result = NULL;
        }
        else if (sym->is_comptime_const)
        {
            /* docs/plan_comptime_consteval.md: fold the reference into a literal in
               place — codegen never sees the name, only the constant value (zero
               runtime storage / zero codegen). The IDENT and the target literal
               share the union, so free the ident payload before overwriting. */
            Type *ct = sym->type;
            free(node->as.ident.name);
            if (node->as.ident.type_args) {
                for (int ti = 0; ti < node->as.ident.type_arg_count; ti++)
                    type_node_free(node->as.ident.type_args[ti]);
                free(node->as.ident.type_args);
            }
            if (sym->ct_is_float) {
                node->kind = AST_FLOAT_LIT;
                node->as.float_lit.value = sym->ct_f;
            } else if (ct->kind == TYPE_BOOL) {
                node->kind = AST_BOOL_LIT;
                node->as.bool_lit.value = (sym->ct_i != 0);
            } else {
                node->kind = AST_INT_LIT;
                node->as.int_lit.value   = sym->ct_i;
                node->as.int_lit.is_char = (ct->kind == TYPE_CHAR);
            }
            node->resolved_type = ct;
            result = ct;
        }
        else
        {
            /* Check for use of moved / maybe-moved variable (Phase A/B).
               MAYBE_MOVED = death: a variable possibly moved on some path is
               considered unusable even on paths where it would technically be live. */
            if (sym->is_moved)
            {
                checker_move_error(c, node->line, node->column,
                                   "use of moved variable '%s'", node->as.ident.name);
            }
            else if (sym->is_maybe_moved)
            {
                checker_move_error(c, node->line, node->column,
                                   "use of maybe-moved variable '%s' (moved on some control-flow path)",
                                   node->as.ident.name);
            }
            result = sym->type;
        }
        break;
    } while (0);
    return result;
}

static Type *check_expr_mut_borrow(Checker *c, AstNode *node)
{
    Type *result = NULL;
    do {
        /* &!x — explicit writable borrow. Operand is either an IDENT of an
           owned, non-moved, non-borrow struct, or a field access `base.field`
           (writable borrow of a struct field — arg-only, non-escaping). */
        AstNode *op = node->as.mut_borrow.operand;
        if (op == NULL || (op->kind != AST_IDENT && op->kind != AST_FIELD))
        {
            checker_error(c, node->line, node->column,
                          "&! requires a variable name or field access "
                          "(got a non-lvalue expression)");
            result = NULL;
            break;
        }
        if (op->kind == AST_FIELD)
        {
            /* &!base.field — writable borrow of a field. Sound contained
               subset: the field outlives the call and `&!` is arg-only
               (borrow extension guards), so it cannot escape. The field may be
               a struct (Vec/Map/Str/...) or a POD scalar (int/f64/...) — both
               are valid &!T payloads for a Block(&!T) (e.g. SpinGuard(int)).
               The access root must be mutable (not a read-only borrow). */
            Type *ft = check_expr(c, op);
            if (ft == NULL) { result = NULL; break; }
            AstNode *root = op;
            while (root->kind == AST_FIELD)
                root = root->as.field_access.object;
            if (root->kind == AST_IDENT)
            {
                Symbol *rs = scope_resolve(c->current_scope,
                                           root->as.ident.name);
                if (rs != NULL && rs->is_borrow && !rs->is_mut_borrow)
                {
                    checker_error(c, node->line, node->column,
                                  "&!: cannot take writable borrow through "
                                  "read-only borrow '%s'", root->as.ident.name);
                    result = NULL;
                    break;
                }
            }
            op->resolved_type = ft;
            result = type_mut_reference(ft);
            break;
        }
        Symbol *sym = scope_resolve(c->current_scope, op->as.ident.name);
        if (sym == NULL)
        {
            checker_error(c, node->line, node->column,
                          "&!: undefined variable '%s'", op->as.ident.name);
            result = NULL;
            break;
        }
        if (sym->type == NULL || sym->type->kind != TYPE_STRUCT)
        {
            checker_error(c, node->line, node->column,
                              "&!: only &!struct is supported, got &!%s",
                              sym->type ? type_name(sym->type) : "?");
            result = NULL;
            break;
        }
        /* Phase B: drop struct mutable borrow now allowed. */
        if (sym->is_borrow)
        {
            checker_error(c, node->line, node->column,
                          "&!: cannot take writable borrow of read-only borrow '%s'",
                          op->as.ident.name);
            result = NULL;
            break;
        }
        if (sym->is_moved || sym->is_maybe_moved)
        {
            checker_error(c, node->line, node->column,
                          "&!: variable '%s' has been moved", op->as.ident.name);
            result = NULL;
            break;
        }
        /* Write the resolved ident type for downstream use, then return &!T. */
        op->resolved_type = sym->type;
        result = type_mut_reference(sym->type);
        break;
    } while (0);
    return result;
}

static Type *check_expr_binary(Checker *c, AstNode *node)
{
    Type *result = NULL;
    do {
        /* Memoize: a binary node is type-checked exactly once during the normal
           tree walk. The only re-entry is operator-overload lowering, which REUSES
           the already-checked operands as the lowered call's object/arg (see
           try_operator_overload). Returning the cached type here makes that re-entry
           O(1); without it, re-checking would re-lower and recurse into the left
           subtree — O(2^n) over a `a + b + c + ...` chain. Generic-body re-checks are
           unaffected (those run on freshly cloned, unresolved nodes). */
        if (node->resolved_type != NULL)
        {
            result = node->resolved_type;
            break;
        }
        Type *left = check_expr(c, node->as.binary.left);
        Type *right = check_expr(c, node->as.binary.right);
        if (left == NULL || right == NULL)
        {
            result = NULL;
            break;
        }

        /* Operator overloading: if the left operand is a struct/enum, try to
           lower `a OP b` to a user-defined operator-method call. Must run BEFORE
           the builtin op switch so that struct `==` does not fall into the
           builtin type_equals path (which would emit invalid IR). */
        {
            Type *ov_result = NULL;
            if (try_operator_overload(c, node, left, right, &ov_result))
            {
                result = ov_result;
                break;
            }
        }

        /* SIMD elementwise arithmetic: Simd(T,N) OP Simd(T,N) -> Simd(T,N), for
           + - * /. Intercepted before the scalar op switch (type_is_numeric is
           false for Simd, so the scalar path would reject it). */
        if (left->kind == TYPE_SIMD || right->kind == TYPE_SIMD)
        {
            TokenType sop = node->as.binary.op;
            bool arith = (sop == TOKEN_PLUS || sop == TOKEN_MINUS ||
                          sop == TOKEN_STAR || sop == TOKEN_SLASH);
            if (!arith)
            {
                checker_error(c, node->line, node->column,
                    "Simd vectors support only + - * /, got '%s' and '%s'",
                    type_name(left), type_name(right));
                result = NULL;
            }
            else if (!type_equals(left, right))
            {
                checker_error(c, node->line, node->column,
                    "Simd arithmetic requires matching vector types, got '%s' and '%s'",
                    type_name(left), type_name(right));
                result = NULL;
            }
            else if (left->kind == TYPE_SIMD && left->as.simd.elem->kind == TYPE_BF16)
            {
                /* bf16 has no native vector arithmetic on x86 (it would silently
                   promote to f32). Per design, reject and steer to f32 accumulation
                   (load/convert via __simd_cast(f32, v)). */
                checker_error(c, node->line, node->column,
                    "bf16 vectors are storage/convert only — no native arithmetic; "
                    "convert to Simd(f32, N) (e.g. __simd_cast(f32, v)) to compute");
                result = NULL;
            }
            else
            {
                result = type_clone(left);
            }
            break;
        }

        switch (node->as.binary.op)
        {
        /* Arithmetic: +, -, *, /, % */
        case TOKEN_PLUS:
            /* fall through to numeric check */
        case TOKEN_MINUS:
        case TOKEN_STAR:
        case TOKEN_SLASH:
            if (!type_is_numeric(left) || !type_is_numeric(right))
            {
                checker_error(c, node->line, node->column,
                              "arithmetic operator requires numeric types, got '%s' and '%s'",
                              type_name(left), type_name(right));
                result = NULL;
            }
            else
            {
                Type *common = type_numeric_common(left, right);
                if (common == NULL)
                {
                    checker_error(c, node->line, node->column,
                                  "type mismatch in arithmetic: '%s' vs '%s' (no implicit widening; use 'as')",
                                  type_name(left), type_name(right));
                    result = NULL;
                }
                else
                {
                    result = common;
                }
            }
            break;

        case TOKEN_PERCENT:
            if (!type_is_integer(left) || !type_is_integer(right))
            {
                checker_error(c, node->line, node->column,
                              "'%%' requires integer types, got '%s' and '%s'",
                              type_name(left), type_name(right));
                result = NULL;
            }
            else
            {
                Type *common = type_numeric_common(left, right);
                if (common == NULL)
                {
                    checker_error(c, node->line, node->column,
                                  "type mismatch in '%%': '%s' vs '%s' (no implicit widening; use 'as')",
                                  type_name(left), type_name(right));
                    result = NULL;
                }
                else
                {
                    result = common;
                }
            }
            break;

        /* Bitwise: &, |, ^, <<, >> */
        case TOKEN_AMP:
        case TOKEN_PIPE:
        case TOKEN_CARET:
            if (!type_is_integer(left) || !type_is_integer(right))
            {
                checker_error(c, node->line, node->column,
                              "bitwise operator requires integer types, got '%s' and '%s'",
                              type_name(left), type_name(right));
                result = NULL;
            }
            else
            {
                Type *common = type_numeric_common(left, right);
                if (common == NULL)
                {
                    checker_error(c, node->line, node->column,
                                  "type mismatch in bitwise op: '%s' vs '%s' (no implicit widening; use 'as')",
                                  type_name(left), type_name(right));
                    result = NULL;
                }
                else
                {
                    result = common;
                }
            }
            break;

        case TOKEN_LSHIFT:
        case TOKEN_RSHIFT:
            if (!type_is_integer(left) || !type_is_integer(right))
            {
                checker_error(c, node->line, node->column,
                              "shift operator requires integer types, got '%s' and '%s'",
                              type_name(left), type_name(right));
                result = NULL;
            }
            else
            {
                result = left;
            }
            break;

        /* Comparison: ==, !=, <, >, <=, >= */
        case TOKEN_EQ:
        case TOKEN_NEQ:
            if (type_equals(left, right))
            {
                result = type_bool();
            }
            else if (type_is_pointer_like(left) && type_is_pointer_like(right))
            {
                /* Allow: *T == nil, object == nil, *T == object, etc. */
                result = type_bool();
            }
            else if (type_numeric_common(left, right) != NULL)
            {
                /* Allow mixed numeric/char comparisons: 'A' == 65, char vs int, etc. */
                result = type_bool();
            }
            else
            {
                checker_error(c, node->line, node->column,
                              "cannot compare '%s' and '%s' for equality",
                              type_name(left), type_name(right));
                result = NULL;
            }
            break;

        case TOKEN_LT:
        case TOKEN_GT:
        case TOKEN_LEQ:
        case TOKEN_GEQ:
            if (!type_is_numeric(left) || !type_is_numeric(right))
            {
                checker_error(c, node->line, node->column,
                              "comparison requires numeric or string types, got '%s' and '%s'",
                              type_name(left), type_name(right));
                result = NULL;
            }
            else if (type_numeric_common(left, right) == NULL)
            {
                checker_error(c, node->line, node->column,
                              "type mismatch in comparison: '%s' vs '%s' (no implicit widening; use 'as')",
                              type_name(left), type_name(right));
                result = NULL;
            }
            else
            {
                result = type_bool();
            }
            break;

        /* Logical: &&, || */
        case TOKEN_AND:
        case TOKEN_OR:
            if (left->kind != TYPE_BOOL || right->kind != TYPE_BOOL)
            {
                checker_error(c, node->line, node->column,
                              "logical operator requires bool, got '%s' and '%s'",
                              type_name(left), type_name(right));
                result = NULL;
            }
            else
            {
                result = type_bool();
            }
            break;

        default:
            checker_error(c, node->line, node->column, "unknown binary operator");
            result = NULL;
            break;
        }
        break;
    } while (0);
    return result;
}

static Type *check_expr_index(Checker *c, AstNode *node)
{
    Type *result = NULL;
    do {
        /* Multi-subscript t[i, j, ...] -> the arity-specific reserved protocol
           method __index{N} (a generalization of v[i] -> __index). Resolved by
           subscript count, known at parse time; each __index{N} is a fixed-arity
           method (scalar args, no container) so the offset arithmetic inlines —
           the low-overhead, container-free path (cf. Julia's per-arity getindex).
           Single-subscript (count<=1) falls through to the legacy logic below,
           byte-unchanged. */
        if (node->as.index_expr.index_count >= 2)
        {
            AstNode *objn = node->as.index_expr.object;
            Type *obj = check_expr(c, objn);
            if (obj == NULL) { result = NULL; break; }
            int nidx = node->as.index_expr.index_count;
            char mname[24];
            snprintf(mname, sizeof(mname), "__index%d", nidx);
            if (obj->kind == TYPE_STRUCT && find_method_ensured(c, obj, mname) != NULL)
            {
                AstNode *call = make_multi_index_call(node->line, node->column,
                    objn, node->as.index_expr.indices, nidx, NULL, mname);
                node->kind = AST_CALL;
                node->as.call = call->as.call;
                free(call);
                result = check_expr(c, node);
            }
            else
            {
                checker_error(c, node->line, node->column,
                    "type '%s' does not support %d-D indexing (no method '%s')",
                    type_name(obj), nidx, mname);
                result = NULL;
            }
            break;
        }

        /* Slice creation `v[a..b]` — a borrowed &[T] view over a Vec(T) range.
           Intercept the AST_RANGE index before check_expr (ranges are not
           stand-alone expressions). Spike scope: source is a Vec(T). */
        if (node->as.index_expr.index &&
            node->as.index_expr.index->kind == AST_RANGE)
        {
            AstNode *objn = node->as.index_expr.object;
            Type *obj = check_expr(c, objn);
            if (obj == NULL) { result = NULL; break; }
            Type *elem = NULL;
            if (obj->kind == TYPE_STRUCT && obj->as.strukt.generic_base &&
                strcmp(obj->as.strukt.generic_base, "Vec") == 0 &&
                obj->as.strukt.generic_arg_count >= 1)
                elem = obj->as.strukt.generic_args[0];
            else if (obj->kind == TYPE_SLICE)
                elem = obj->as.array.elem;  /* sub-slice of a slice */
            else if (obj->kind == TYPE_STRUCT && obj->as.strukt.name &&
                     strcmp(obj->as.strukt.name, "Str") == 0 &&
                     obj->as.strukt.field_count >= 1 &&
                     obj->as.strukt.fields[0].type &&
                     obj->as.strukt.fields[0].type->kind == TYPE_POINTER)
                /* Str `{*u8 data; int len; int cap}` → a byte view `&array(u8)`
                   (same SoA layout as Vec: field0=data ptr, field1=len). */
                elem = obj->as.strukt.fields[0].type->as.pointer_to;
            if (elem == NULL)
            {
                checker_error(c, node->line, node->column,
                    "slice `v[a..b]` requires a Vec(T), Str, or &array(T) source "
                    "(got '%s')", type_name(obj));
                result = NULL;
                break;
            }
            AstNode *rng = node->as.index_expr.index;
            Type *lo = rng->as.range.start ? check_expr(c, rng->as.range.start) : NULL;
            Type *hi = rng->as.range.end   ? check_expr(c, rng->as.range.end)   : NULL;
            if ((lo && !type_is_integer(lo)) || (hi && !type_is_integer(hi)))
                checker_error(c, node->line, node->column,
                              "slice bounds must be integers");
            /* Mutability is driven by the expected type (a `&!array(T)` target /
               parameter), so a fresh `v[a..b]` can be passed directly to a
               writable-slice parameter. A writable view needs a writable source. */
            bool want_mut = (c->expected_type &&
                             c->expected_type->kind == TYPE_SLICE &&
                             c->expected_type->is_mut &&
                             type_equals(c->expected_type->as.array.elem, elem));
            if (want_mut)
            {
                Symbol *root = checker_place_root_symbol(c, objn);
                if (root != NULL && root->is_borrow)
                {
                    checker_error(c, node->line, node->column,
                        "cannot take a writable slice of read-only borrow '%s'",
                        root->name);
                    want_mut = false;
                }
            }
            result = type_slice(elem, want_mut);
            break;
        }

        Type *obj = check_expr(c, node->as.index_expr.object);
        Type *idx = check_expr(c, node->as.index_expr.index);
        if (obj == NULL || idx == NULL)
        {
            result = NULL;
            break;
        }

        /* `slice[i]` — element read of a borrowed slice (bounds-checked in codegen). */
        if (obj->kind == TYPE_SLICE)
        {
            if (!type_is_integer(idx))
            {
                checker_error(c, node->line, node->column,
                              "slice index must be integer, got '%s'", type_name(idx));
                result = NULL;
            }
            else
            {
                result = obj->as.array.elem;
            }
            break;
        }

        if (obj->kind == TYPE_ARRAY)
        {
            if (!type_is_integer(idx))
            {
                checker_error(c, node->line, node->column,
                              "array index must be integer, got '%s'", type_name(idx));
                result = NULL;
            }
            else
            {
                result = obj->as.array.elem;
            }
        }
        else if (obj->kind == TYPE_POINTER)
        {
            /* p[i] on a raw *T pointer — element access, no bounds check (unsafe
               layer). Result is the pointee type. The store form (p[i] = x) is a
               RAW store: it does NOT drop the old slot (the slot may be
               uninitialized memory), unlike vec/array element assignment. */
            if (!type_is_integer(idx))
            {
                checker_error(c, node->line, node->column,
                              "pointer index must be integer, got '%s'", type_name(idx));
                result = NULL;
            }
            else if (obj->as.pointer_to == NULL)
            {
                checker_error(c, node->line, node->column,
                              "cannot index opaque pointer");
                result = NULL;
            }
            else
            {
                result = obj->as.pointer_to;
            }
        }
        else if (obj->kind == TYPE_STRUCT &&
                 find_method_ensured(c, obj, "__index") != NULL)
        {
            /* Index protocol: `v[i]` on a struct that opts in via
               `__index(&self, int) -> T` desugars to `v.__index(i)`. Rewrite the
               node in place to the method call and re-check (reuses all call
               machinery: monomorphization, return-value ownership, etc.). */
            AstNode *objn = node->as.index_expr.object;
            AstNode *idxn = node->as.index_expr.index;
            rewrite_index_to_call(node, objn, idxn, "__index");
            result = check_expr(c, node);
        }
        else
        {
            checker_error(c, node->line, node->column,
                          "cannot index non-array type '%s'", type_name(obj));
            result = NULL;
        }
        break;
    } while (0);
    return result;
}

static Type *check_expr_field(Checker *c, AstNode *node)
{
    Type *result = NULL;
    do {
        Type *obj = check_expr(c, node->as.field_access.object);
        if (obj == NULL)
        {
            result = NULL;
            break;
        }
        /* Phase 2 (borrow extension): field access auto-dereferences a borrow
           result (&T → T), e.g. `obj.get_ref().field` where get_ref returns &T.
           (Borrow *parameters* already register their symbol with the pointee
           type, so `self.field` needs no unwrap; this covers reference-typed
           sub-expressions like a borrow-returning call result.) */
        if (obj->kind == TYPE_REFERENCE && obj->as.pointer_to)
            obj = obj->as.pointer_to;

        const char *field_name = node->as.field_access.field;

        /* Module-qualified access (e.g., math.add) */
        if (obj->kind == TYPE_MODULE)
        {
            for (int i = 0; i < obj->as.module.export_count; i++)
            {
                if (strcmp(obj->as.module.exports[i].name, field_name) == 0)
                {
                    result = obj->as.module.exports[i].type;
                    break;
                }
            }
            if (result == NULL)
            {
                checker_error(c, node->line, node->column,
                              "module '%s' has no export '%s'",
                              obj->as.module.name ? obj->as.module.name : "<unknown>",
                              field_name);
            }
            break;
        }

        /* Array .length — compile-time constant */
        if (obj->kind == TYPE_ARRAY)
        {
            if (strcmp(field_name, "length") == 0)
            {
                result = type_int();
            }
            else
            {
                checker_error(c, node->line, node->column,
                              "array has no field '%s' (only 'length')", field_name);
                result = NULL;
            }
            break;
        }

        /* Auto-dereference: *Struct → Struct or *Enum → Enum for field/method access */
        if (obj->kind == TYPE_POINTER && obj->as.pointer_to &&
            (obj->as.pointer_to->kind == TYPE_STRUCT ||
             obj->as.pointer_to->kind == TYPE_ENUM))
        {
            obj = obj->as.pointer_to;
        }

        if (obj->kind == TYPE_STRUCT)
        {
            bool priv_rejected = false;
            /* Search struct fields */
            for (int i = 0; i < obj->as.strukt.field_count; i++)
            {
                if (strcmp(obj->as.strukt.fields[i].name, field_name) == 0)
                {
                    if (obj->as.strukt.fields[i].is_private)
                    {
                        /* priv field: accessible only inside the owning struct's
                           own impl methods. Identity by generic_base else name
                           (so Mutex(Vec) inside impl(T) Mutex(T) matches). */
                        Type *cur = c->current_impl_struct_type;
                        const char *want = obj->as.strukt.generic_base
                            ? obj->as.strukt.generic_base : obj->as.strukt.name;
                        const char *have = (cur && cur->kind == TYPE_STRUCT)
                            ? (cur->as.strukt.generic_base
                               ? cur->as.strukt.generic_base : cur->as.strukt.name)
                            : NULL;
                        if (have == NULL || want == NULL ||
                            strcmp(have, want) != 0)
                        {
                            checker_error(c, node->line, node->column,
                                "field '%s' of struct '%s' is private "
                                "(accessible only inside its own methods methods)",
                                field_name, want ? want : "<anon>");
                            result = NULL;
                            priv_rejected = true;
                            break;
                        }
                    }
                    result = obj->as.strukt.fields[i].type;
                    break;
                }
            }
            /* Search methods if not found as field (skip if priv-rejected — the
               error is already reported; don't cascade "no field or method"). */
            if (result == NULL && !priv_rejected && obj->as.strukt.name)
            {
                result = find_method(c, impl_key_of_type(obj), field_name);  /* B-4.1 */
            }
            if (result == NULL && !priv_rejected)
            {
                char helpbuf[256];
                DiagMethodIter it = { c, obj, impl_key_of_type(obj),
                                      0, 0, 0, false };
                const char *help = diag_help_suggestion(
                    helpbuf, sizeof(helpbuf), field_name,
                    diag_method_iter_next, &it);
                checker_error_help(c, node->line, node->column, 1, help,
                              "struct '%s' has no field or method '%s'",
                              obj->as.strukt.name ? obj->as.strukt.name : "<anon>",
                              field_name);
            }
        }
        else if (obj->kind == TYPE_ENUM)
        {
            /* Enum has no fields — only methods */
            if (obj->as.enom.name)
            {
                result = find_method(c, impl_key_of_type(obj), field_name);  /* B-4.1 */
            }
            if (result == NULL)
            {
                char helpbuf[256];
                DiagMethodIter it = { c, NULL, impl_key_of_type(obj),
                                      0, 0, 0, false };
                const char *help = diag_help_suggestion(
                    helpbuf, sizeof(helpbuf), field_name,
                    diag_method_iter_next, &it);
                checker_error_help(c, node->line, node->column, 1, help,
                              "enum '%s' has no method '%s'",
                              obj->as.enom.name ? obj->as.enom.name : "<anon>",
                              field_name);
            }
        }
        else
        {
            checker_error(c, node->line, node->column,
                          "field access on non-struct/enum type '%s'", type_name(obj));
            result = NULL;
        }
        break;
    } while (0);
    return result;
}

static Type *check_expr_closure(Checker *c, AstNode *node)
{
    Type *result = NULL;
    do {
        int n = node->as.closure.param_count;
        /* Ruby-style literals (`|x| body`, `|| body`) carry is_ruby_form=true
           and inherit their param/return types from the call-site's
           expected_type (a TYPE_BLOCK / TYPE_FUNCTION with matching arity).
           The legacy `fn(int x) -> R { ... }` form supplies its own types. */
        bool ruby_form = node->as.closure.is_ruby_form;
        /* Save outer scope BEFORE we push the closure body scope below, so
           the Phase C capture scan can resolve free variables against the
           caller's environment. */
        Scope *outer_scope_for_caps = c->current_scope;
        (void)outer_scope_for_caps;
        Type **params = NULL;
        Type *ret = NULL;

        if (ruby_form) {
            /* Phase B: pull param/return types from c->expected_type. */
            Type *exp = c->expected_type;
            if (exp == NULL ||
                (exp->kind != TYPE_BLOCK && exp->kind != TYPE_FUNCTION) ||
                exp->as.function.param_count != n)
            {
                checker_error(c, node->line, node->column,
                              "cannot infer closure parameter types: %s "
                              "(declare a typed `Block(...)` parameter at the "
                              "call site, or capture the closure into a typed "
                              "variable: `Adder f = |x| ...`)",
                              exp == NULL ? "no expected type at this position"
                                          : "expected type does not match closure shape");
                result = NULL;
                break;
            }
            if (n > 0) {
                params = (Type **)malloc_safe((size_t)n * sizeof(Type *));
                for (int i = 0; i < n; i++) {
                    params[i] = exp->as.function.params[i];
                }
            }
            ret = exp->as.function.return_type;
        } else {
            if (n > 0) {
                params = (Type **)malloc_safe((size_t)n * sizeof(Type *));
                for (int i = 0; i < n; i++) {
                    params[i] = resolve_type_node(c, node->as.closure.param_types[i],
                                                  node->line, node->column);
                }
            }
            ret = resolve_type_node(c, node->as.closure.return_type,
                                    node->line, node->column);
            checker_reject_borrow_return(c, ret, NULL, node->line, node->column);  /* Phase 0/2: closure, defer */
        }

        /* Phase C: scan body for free variables → record captures on the
           AST node so codegen can build the env struct. Walk runs against
           the OUTER scope (still current at this point) and seeds `bound`
           with the closure's parameter names. */
        if (ruby_form && node->as.closure.captures == NULL) {
            CaptureScan cs;
            memset(&cs, 0, sizeof(cs));
            cs.c = c;
            cs.outer_scope = outer_scope_for_caps;
            cs.closure_node = node;
            for (int i = 0; i < n; i++) {
                cap_push_bound(&cs, node->as.closure.param_names[i]);
            }
            capture_walk(&cs, node->as.closure.body);
            free((void*)cs.bound);
            node->as.closure.captures = (void*)cs.captures;
            node->as.closure.capture_count = cs.capture_count;

            /* F.1: Process [move v1, v2] capture spec.
               For each name in move_names:
               - Find the matching capture and set is_explicit_move=true.
               - If not found in captures, report an error. */
            for (int mi = 0; mi < node->as.closure.move_count; mi++) {
                const char *mname = node->as.closure.move_names[mi];
                bool found = false;
                for (int ci = 0; ci < node->as.closure.capture_count; ci++) {
                    if (strcmp(node->as.closure.captures[ci].name, mname) == 0) {
                        node->as.closure.captures[ci].is_explicit_move = true;
                        found = true;
                        break;
                    }
                }
                if (!found) {
                    checker_error(c, node->line, node->column,
                        "'%s' in [move ...] list is not referenced inside "
                        "the closure body and cannot be captured", mname);
                }
            }
        }

        /* Check body in new scope */
        chk_push_scope(c);
        for (int i = 0; i < n; i++)
        {
            if (params[i])
            {
                /* M5-002: a closure param declared with a reference type (e.g.
                   `Block(&P)` → param `&P`) must be unwrapped to the pointee `P`
                   with is_borrow set, exactly like normal function params — else
                   `pp.x` / `pp.method()` in the body fails with "field access on
                   non-struct type '&P'". Keep params[i] (the &P) for the Block
                   signature; only the body-local symbol uses the unwrapped type. */
                Type *pt = params[i];
                bool is_borrow = false, is_mut_borrow = false;
                if (pt->kind == TYPE_REFERENCE)
                {
                    if (pt->is_mut) is_mut_borrow = true;
                    else            is_borrow     = true;
                    pt = pt->as.pointer_to;
                }
                Symbol *psym = scope_define(c->current_scope,
                                            node->as.closure.param_names[i], pt);
                if (psym)
                {
                    psym->is_borrow = is_borrow;
                    psym->is_mut_borrow = is_mut_borrow;
                    /* F.2: Block closure params share env_ptr with caller — borrow */
                    if (pt->kind == TYPE_BLOCK)
                        psym->is_borrow = true;
                }
            }
        }
        /* Define captures inside the closure scope so the body type-checker
           treats them as locally-bound (with the captured types from outer
           scope). Codegen will materialise these as alloca-of-loaded-from-env
           inside the synthesised __closure_<N> body. */
        for (int i = 0; i < node->as.closure.capture_count; i++) {
            scope_define(c->current_scope,
                         node->as.closure.captures[i].name,
                         node->as.closure.captures[i].type);
        }

        Type *saved_ret = c->current_fn_return;
        Type *saved_exp = c->expected_type;
        c->current_fn_return = ret;
        c->expected_type = NULL;  /* don't leak Block expected to body */
        check_stmt(c, node->as.closure.body);
        c->current_fn_return = saved_ret;
        c->expected_type = saved_exp;
        chk_pop_scope(c);

        /* A Ruby-form literal materialises as a TYPE_BLOCK (closure value).
           Legacy fn(...) literals stay TYPE_FUNCTION for backward compat. */
        if (ruby_form) {
            result = type_block(params, n, ret);
        } else {
            result = type_function(params, n, ret, false);
        }
        break;
    } while (0);
    return result;
}

static Type *check_expr_match(Checker *c, AstNode *node)
{
    Type *result = NULL;
    do {
        /* The subject's type is intrinsic to the subject expression and must not
           be coerced by the match's own expected result type — otherwise a bare
           ctor subject (e.g. lowered `Some(3).map(Str)(...)`) would be checked
           against the wrong instantiation. Clear expected_type for the subject. */
        Type *saved_subj_exp = c->expected_type;
        c->expected_type = NULL;
        Type *subject = check_expr(c, node->as.match.subject);
        c->expected_type = saved_subj_exp;
        if (subject == NULL)
        {
            result = NULL;
            break;
        }

        /* Phase B: detect if the match subject is a borrowed enum variable.
           When true, owned payload binders (string/vec/map/struct/enum) are
           marked is_borrow=true so checker prevents moves and mutations. */
        bool subj_is_enum_borrow = false;
        if (node->as.match.subject->kind == AST_IDENT && subject &&
            subject->kind == TYPE_ENUM)
        {
            Symbol *ssym = scope_resolve(c->current_scope,
                                         node->as.match.subject->as.ident.name);
            if (ssym && ssym->is_borrow) subj_is_enum_borrow = true;
        }

        /* Enum subjects: variant patterns + exhaustiveness check */
        if (subject->kind == TYPE_ENUM)
        {
            int vc = subject->as.enom.variant_count;
            bool *covered = (bool *)calloc_safe((size_t)vc, sizeof(bool));
            bool catchall = false;
            Type *arm_type = NULL;

            for (int i = 0; i < node->as.match.arm_count; i++)
            {
                MatchArm *arm = &node->as.match.arms[i];
                AstNode *pat = arm->pattern;
                const char *vname = NULL;
                AstNode **binders = NULL;
                int binder_count = 0;

                if (pat->kind == AST_IDENT && strcmp(pat->as.ident.name, "_") == 0)
                {
                    catchall = true;
                }
                else if (pat->kind == AST_IDENT)
                {
                    vname = pat->as.ident.name;
                }
                else if (pat->kind == AST_CALL && pat->as.call.callee->kind == AST_IDENT)
                {
                    vname = pat->as.call.callee->as.ident.name;
                    binders = pat->as.call.args;
                    binder_count = pat->as.call.arg_count;
                }
                else
                {
                    checker_error(c, pat->line, pat->column,
                                  "invalid pattern for enum '%s'", type_name(subject));
                }

                int variant_idx = -1;
                if (vname)
                {
                    for (int v = 0; v < vc; v++)
                    {
                        if (strcmp(subject->as.enom.variants[v].name, vname) == 0)
                        {
                            variant_idx = v;
                            break;
                        }
                    }
                    if (variant_idx < 0)
                    {
                        checker_error(c, pat->line, pat->column,
                                      "'%s' is not a variant of enum '%s'",
                                      vname, type_name(subject));
                        continue;
                    }
                    int expected = subject->as.enom.variants[variant_idx].payload_count;
                    if (binder_count != expected)
                    {
                        checker_error(c, pat->line, pat->column,
                                      "variant '%s' expects %d binder(s), got %d",
                                      vname, expected, binder_count);
                        continue;
                    }
                    covered[variant_idx] = true;
                }

                /* Push binder scope for arm body */
                chk_push_scope(c);
                for (int b = 0; b < binder_count; b++)
                {
                    AstNode *bnode = binders[b];
                    if (bnode->kind != AST_IDENT)
                    {
                        checker_error(c, bnode->line, bnode->column,
                                      "variant pattern binder must be an identifier");
                        continue;
                    }
                    const char *bname = bnode->as.ident.name;
                    if (strcmp(bname, "_") == 0) continue;  /* skip wildcard binder */
                    Type *bt = subject->as.enom.variants[variant_idx].payload_types[b];
                    Symbol *bsym = scope_define(c->current_scope, bname, bt);
                    bnode->resolved_type = bt;
                    /* Phase B: for borrowed enum subject, mark owned payload binders
                       as read-only borrows — prevents moves and mutating methods. */
                    if (bsym && subj_is_enum_borrow && bt &&
                        ((bt->kind == TYPE_STRUCT && bt->as.strukt.has_drop) ||
                         (bt->kind == TYPE_ENUM   && bt->as.enom.has_drop)))
                    {
                        bsym->is_borrow = true;
                    }
                }

                Type *body_type = check_expr(c, arm->body);
                chk_pop_scope(c);

                if (body_type == NULL) continue;
                if (arm_type == NULL) arm_type = body_type;
                else if (!type_equals(arm_type, body_type))
                {
                    checker_error(c, arm->body->line, arm->body->column,
                                  "match arm type mismatch: expected '%s', got '%s'",
                                  type_name(arm_type), type_name(body_type));
                }
            }

            /* Exhaustiveness check */
            if (!catchall)
            {
                char missing[256];
                int pos = 0;
                bool any_missing = false;
                for (int v = 0; v < vc; v++)
                {
                    if (!covered[v])
                    {
                        if (any_missing && pos < (int)sizeof(missing) - 3)
                            pos += snprintf(missing + pos, sizeof(missing) - (size_t)pos, ", ");
                        pos += snprintf(missing + pos, sizeof(missing) - (size_t)pos, "%s",
                                        subject->as.enom.variants[v].name);
                        any_missing = true;
                    }
                }
                if (any_missing)
                {
                    checker_error(c, node->line, node->column,
                                  "non-exhaustive match on enum '%s': missing variant(s): %s",
                                  type_name(subject), missing);
                }
            }

            free(covered);
            result = arm_type;
            break;
        }

        /* V1 bit-pattern arms: `bits[w:name][w:0xVAL][w:_]...` Detect whether any
           arm uses bit patterns; if so, take a dedicated path (integer subject +
           strict total-width check + per-arm binder scope). */
        bool match_has_bit = false;
        for (int i = 0; i < node->as.match.arm_count; i++)
            if (pattern_has_bit_seq(node->as.match.arms[i].pattern)) { match_has_bit = true; break; }

        if (match_has_bit)
        {
            int subj_bits = bit_pattern_type_bits(subject);
            if (subj_bits == 0)
            {
                checker_error(c, node->as.match.subject->line,
                              node->as.match.subject->column,
                              "bit-pattern match subject must be an integer type "
                              "(int / i8-i64 / u8-u64), got '%s'", type_name(subject));
                result = NULL;
                break;
            }
            Type *bit_arm_type = NULL;
            for (int i = 0; i < node->as.match.arm_count; i++)
            {
                MatchArm *arm = &node->as.match.arms[i];
                AstNode *pat = arm->pattern;
                bool is_wild = pat->kind == AST_IDENT &&
                               strcmp(pat->as.ident.name, "_") == 0;

                chk_push_scope(c);
                if (!is_wild)
                {
                    /* Collect OR-tree leaves left-to-right; binders come from the
                       first leaf only (V1: OR branches should bind the same names). */
                    AstNode *leaves[64]; int nleaves = 0;
                    AstNode *stk[64]; int sp = 0;
                    stk[sp++] = pat;
                    while (sp > 0 && nleaves < 64)
                    {
                        AstNode *cur = stk[--sp];
                        if (cur->kind == AST_MATCH_OR_PATTERN)
                        {
                            if (sp + 2 <= 64) {
                                stk[sp++] = cur->as.or_pattern.right;
                                stk[sp++] = cur->as.or_pattern.left;
                            }
                        }
                        else if (cur->kind == AST_MATCH_BIT_PATTERN_SEQ)
                            leaves[nleaves++] = cur;
                        else
                            checker_error(c, cur->line, cur->column,
                                          "cannot mix bit-pattern with non-bit patterns "
                                          "in the same match arm");
                    }
                    for (int L = 0; L < nleaves; L++)
                        check_bit_pattern_seq(c, leaves[L], subj_bits,
                                              /*define_binders=*/L == 0);
                }

                Type *body_type = check_expr(c, arm->body);
                chk_pop_scope(c);
                if (body_type == NULL) continue;
                if (bit_arm_type == NULL) bit_arm_type = body_type;
                else if (!type_equals(bit_arm_type, body_type))
                    checker_error(c, arm->body->line, arm->body->column,
                                  "match arm type mismatch: expected '%s', got '%s'",
                                  type_name(bit_arm_type), type_name(body_type));
            }
            result = bit_arm_type;
            break;
        }

        /* Non-enum, non-bit subjects lower to scalar compares (an LLVM switch
           or an icmp/fcmp chain) — only scalar subjects are matchable. Reject
           aggregates (Str / Vec / struct / ...) here: they used to slip through
           the pattern type-equality check below and reach codegen, which built
           an invalid icmp on a struct value (module verification failure). */
        if (!type_is_numeric(subject) &&
            subject->kind != TYPE_BOOL && subject->kind != TYPE_CHAR)
        {
            checker_error(c, node->as.match.subject->line,
                          node->as.match.subject->column,
                          "match subject type '%s' is not matchable: expected "
                          "an enum or a scalar (integer / float / bool / char); "
                          "compare '%s' values with == in if/else instead",
                          type_name(subject), type_name(subject));
            result = NULL;
            break;
        }

        /* Non-enum subjects: literal/ident/wildcard/OR-pattern handling.
           Walk the (possibly nested) AST_MATCH_OR_PATTERN tree to type-check
           every leaf pattern against the subject type. */
        Type *arm_type = NULL;
        bool scalar_has_catchall = false;      /* M-7 (12b): any `_` arm */
        bool scalar_saw_true = false;          /* bool subject: `true` literal arm */
        bool scalar_saw_false = false;         /* bool subject: `false` literal arm */
        for (int i = 0; i < node->as.match.arm_count; i++)
        {
            MatchArm *arm = &node->as.match.arms[i];

            /* Iterative tree walk (avoids deep recursion; max ~32 alternatives). */
            AstNode *stack[64];
            int sp = 0;
            stack[sp++] = arm->pattern;
            while (sp > 0)
            {
                AstNode *cur = stack[--sp];
                if (cur->kind == AST_MATCH_OR_PATTERN)
                {
                    /* Push both branches for later processing */
                    if (sp + 2 <= 64) {
                        stack[sp++] = cur->as.or_pattern.right;
                        stack[sp++] = cur->as.or_pattern.left;
                    }
                }
                else if (cur->kind == AST_IDENT &&
                         strcmp(cur->as.ident.name, "_") == 0)
                {
                    /* Wildcard — no type check needed */
                    scalar_has_catchall = true;
                }
                else
                {
                    if (cur->kind == AST_BOOL_LIT)
                    {
                        if (cur->as.bool_lit.value) scalar_saw_true = true;
                        else                        scalar_saw_false = true;
                    }
                    Type *pat_type = check_expr(c, cur);
                    if (pat_type && !type_equals(pat_type, subject) &&
                        type_numeric_common(pat_type, subject) == NULL)
                    {
                        checker_error(c, cur->line, cur->column,
                                      "match pattern type '%s' doesn't match subject type '%s'",
                                      type_name(pat_type), type_name(subject));
                    }
                }
            }

            /* Check body */
            Type *body_type = check_expr(c, arm->body);
            if (body_type == NULL)
                continue;

            if (arm_type == NULL)
            {
                arm_type = body_type;
            }
            else if (!type_equals(arm_type, body_type))
            {
                checker_error(c, arm->body->line, arm->body->column,
                              "match arm type mismatch: expected '%s', got '%s'",
                              type_name(arm_type), type_name(body_type));
            }
        }
        /* M-7 (stage 12b, error since L-020 2026-07-05): a VALUE-producing
           scalar match with no catch-all would silently yield a ZEROED result
           for an unmatched subject — codegen's result_alloca zero-init is a
           drop-safety net (L-013), not a semantic default (`Str s = match i
           { 1 => "a" }` gave an empty Str for i != 1). Enum subjects get the
           variant exhaustiveness error above; scalar domains are unenumerable
           (except bool), so a `_` arm is REQUIRED. Shipped as a warning first
           (zero hits across lib/ + tests at the time), upgraded to an error
           by user decision. A bool subject covering both literals is
           exhaustive. Void-yielding matches are pure control flow: an
           unmatched subject just does nothing. */
        if (arm_type != NULL && arm_type->kind != TYPE_VOID &&
            !scalar_has_catchall &&
            !(subject->kind == TYPE_BOOL && scalar_saw_true && scalar_saw_false))
            checker_error(c, node->line, node->column,
                          "value-producing match on '%s' has no '_' arm: an "
                          "unmatched subject would yield a zeroed '%s'; "
                          "add a wildcard arm",
                          type_name(subject), type_name(arm_type));
        result = arm_type;
        break;
    } while (0);
    return result;
}

static Type *check_expr_new_expr(Checker *c, AstNode *node)
{
    Type *result = NULL;
    do {
        /* Look up the struct type. B-4: module-qualified literal `mod.Type{...}`
           resolves through the imported module's export table. */
        Type *st = NULL;
        /* Anonymous struct literal `{ field: val, ... }` (no type prefix): the
           parser left struct_name NULL. Infer the struct type from the expected
           type (LHS of a var-decl / return / arg slot). */
        if (node->as.new_expr.struct_name == NULL)
        {
            if (c->expected_type == NULL || c->expected_type->kind != TYPE_STRUCT)
            {
                checker_error(c, node->line, node->column,
                              "cannot infer struct type for `{...}` literal here "
                              "(no expected struct type in this context)");
                result = NULL;
                break;
            }
            st = c->expected_type;
            /* adopt the inferred name so downstream field lookup / codegen work */
            size_t snl = strlen(st->as.strukt.name);
            char *sdup = (char *)malloc_safe(snl + 1);
            memcpy(sdup, st->as.strukt.name, snl + 1);
            node->as.new_expr.struct_name = sdup;
        }
        else if (node->as.new_expr.module != NULL)
        {
            Symbol *modsym = scope_resolve(c->current_scope, node->as.new_expr.module);
            if (modsym == NULL || modsym->type == NULL ||
                modsym->type->kind != TYPE_MODULE)
            {
                checker_error(c, node->line, node->column,
                              "unknown module '%s' in '%s.%s{...}'",
                              node->as.new_expr.module, node->as.new_expr.module,
                              node->as.new_expr.struct_name);
                result = NULL;
                break;
            }
            Type *ex = type_module_find_export(modsym->type, node->as.new_expr.struct_name);
            if (ex == NULL || ex->kind != TYPE_STRUCT)
            {
                checker_error(c, node->line, node->column,
                              "module '%s' has no struct '%s'",
                              node->as.new_expr.module, node->as.new_expr.struct_name);
                result = NULL;
                break;
            }
            st = ex;
        }
        else
        {
            st = find_struct_type(c, node->as.new_expr.struct_name);
            /* comptime v2 — generic construction: `T{}` / `T{x:..}` where the
               "struct name" is a type parameter aliased to a concrete struct
               during instantiation (e.g. `def mk(T)() -> T { return T{} }`).
               Resolve it through the type-alias table, same as resolve_type_node
               does for a bare named type. Unlocks write-once construction
               (generic from_value / builder / transform / zero-init). */
            if (!st && node->as.new_expr.type_arg_count == 0)
            {
                Type *alias = find_type_alias(c, node->as.new_expr.struct_name);
                if (alias && alias->kind == TYPE_STRUCT)
                    st = alias;
            }
        }

        /* G1: If the parser provided explicit type_args (e.g. Pair(int,string){...}),
           resolve each arg and instantiate the generic struct template. */
        if (!st && node->as.new_expr.type_arg_count > 0)
        {
            int tac = node->as.new_expr.type_arg_count;
            Type **resolved_args = malloc_safe(sizeof(Type *) * tac);
            bool args_ok = true;
            for (int i = 0; i < tac; i++) {
                resolved_args[i] = resolve_type_node(c, node->as.new_expr.type_args[i],
                    node->line, node->column);
                if (!resolved_args[i]) args_ok = false;
            }
            if (args_ok) {
                st = checker_instantiate_struct(c,
                    node->as.new_expr.struct_name,
                    resolved_args, tac,
                    node->line, node->column);
            }
            free(resolved_args);
        }

        /* G1: struct_name is the base name ("Pair"), but the instantiated type
           is registered under its mangled name ("Pair(int,string)").  Fall back
           to expected_type if the base name matches a generic template. */
        if (!st && c->expected_type && c->expected_type->kind == TYPE_STRUCT)
        {
            const char *sname = node->as.new_expr.struct_name;
            size_t slen = strlen(sname);
            const char *mangled = c->expected_type->as.strukt.name;
            /* Check: mangled starts with "sname(" */
            if (strncmp(mangled, sname, slen) == 0 && mangled[slen] == '(')
            {
                st = c->expected_type;
            }
        }

        if (!st)
        {
            checker_error(c, node->line, node->column,
                          "unknown struct type '%s'", node->as.new_expr.struct_name);
            result = NULL;
            break;
        }
        /* Type-check each field initializer */
        int ninits = node->as.new_expr.field_init_count;
        for (int i = 0; i < ninits; i++)
        {
            const char *fname = node->as.new_expr.field_inits[i].name;
            /* Check for duplicates */
            for (int j = 0; j < i; j++)
            {
                if (strcmp(node->as.new_expr.field_inits[j].name, fname) == 0)
                {
                    checker_error(c, node->line, node->column,
                                  "duplicate field initializer '%s'", fname);
                    goto new_expr_done;
                }
            }
            /* Find field in struct */
            int field_idx = -1;
            for (int j = 0; j < st->as.strukt.field_count; j++)
            {
                if (strcmp(st->as.strukt.fields[j].name, fname) == 0)
                {
                    field_idx = j;
                    break;
                }
            }
            if (field_idx < 0)
            {
                checker_error(c, node->line, node->column,
                              "struct '%s' has no field '%s'",
                              node->as.new_expr.struct_name, fname);
                goto new_expr_done;
            }
            /* priv field: a struct literal may set it only inside the owning
               struct's own impl (so external `Guard{value: aliased}` cannot
               bypass the guard). `{}` zero-init has no field inits → unaffected. */
            if (st->as.strukt.fields[field_idx].is_private)
            {
                Type *cur = c->current_impl_struct_type;
                const char *want = st->as.strukt.generic_base
                    ? st->as.strukt.generic_base : st->as.strukt.name;
                const char *have = (cur && cur->kind == TYPE_STRUCT)
                    ? (cur->as.strukt.generic_base
                       ? cur->as.strukt.generic_base : cur->as.strukt.name)
                    : NULL;
                if (have == NULL || want == NULL || strcmp(have, want) != 0)
                {
                    checker_error(c, node->line, node->column,
                        "field '%s' of struct '%s' is private "
                        "(cannot be set in a struct literal outside its methods)",
                        fname, want ? want : "<anon>");
                    goto new_expr_done;
                }
            }
            /* Type-check the value; set expected_type so closure literals can
               infer their param/return types from the field's Block type. */
            Type *field_expected = st->as.strukt.fields[field_idx].type;
            checker_tag_user_from_list_literal(c, field_expected,
                node->as.new_expr.field_inits[i].value, "field list-literal");
            Type *saved_expected2 = c->expected_type;
            if (field_expected && (field_expected->kind == TYPE_BLOCK ||
                                   field_expected->kind == TYPE_STRUCT))
                c->expected_type = field_expected;
            Type *vt = check_expr(c, node->as.new_expr.field_inits[i].value);
            c->expected_type = saved_expected2;
            if (vt && !type_equals(vt, field_expected))
            {
                checker_error(c, node->as.new_expr.field_inits[i].value->line,
                              node->as.new_expr.field_inits[i].value->column,
                              "field '%s': expected '%s', got '%s'",
                              fname,
                              type_name(field_expected),
                              type_name(vt));
            }
            /* F.3: Block field value ownership transfers into the struct.
               Mark source identifier as moved so it cannot be used again. */
            if (vt && vt->kind == TYPE_BLOCK)
                checker_try_mark_moved(c, node->as.new_expr.field_inits[i].value);
        }
        /* Struct field defaults (v1): an omitted field with a declared default
           takes that default; an omitted field WITHOUT a default keeps LS's
           existing zero-initialization (struct literals never required all
           fields). Here we only type-check the defaults that exist. */
        for (int j = 0; j < st->as.strukt.field_count; j++)
        {
            bool provided = false;
            for (int i = 0; i < ninits; i++)
            {
                if (strcmp(node->as.new_expr.field_inits[i].name,
                           st->as.strukt.fields[j].name) == 0)
                {
                    provided = true;
                    break;
                }
            }
            if (provided)
                continue;
            AstNode *deflt = (AstNode *)st->as.strukt.fields[j].default_expr;
            if (deflt == NULL)
                continue; /* omitted, no default -> zero-init (existing semantics) */
            Type *fexp = st->as.strukt.fields[j].type;
            const char *jfn = st->as.strukt.fields[j].name;
            if (fexp && fexp->kind == TYPE_STRUCT &&
                     deflt->kind == AST_ARRAY_LIT &&
                     deflt->resolved_type == NULL)
            {
                checker_tag_user_from_list_literal(c, fexp, deflt,
                                                   "default list-literal");
            }
            Type *saved_def_exp = c->expected_type;
            if (fexp && (fexp->kind == TYPE_STRUCT || fexp->kind == TYPE_BLOCK))
                c->expected_type = fexp;
            Type *dt = check_expr(c, deflt);
            c->expected_type = saved_def_exp;
            if (dt && fexp && !type_equals(dt, fexp))
            {
                checker_error(c, node->line, node->column,
                              "default for field '%s': expected '%s', got '%s'",
                              jfn, type_name(fexp), type_name(dt));
            }
        }
    new_expr_done:
        /* on_stack = struct value literal  S1{...} → resolves to TYPE_STRUCT
           !on_stack = new S1{...} (heap) → resolves to *TYPE_STRUCT */
        result = node->as.new_expr.on_stack ? st : type_pointer(st);
        break;
    } while (0);
    return result;
}

Type *check_expr(Checker *c, AstNode *node)
{
    if (node == NULL)
        return NULL;

    Type *result = NULL;

    switch (node->kind)
    {
    case AST_INT_LIT:
        if (node->as.int_lit.is_char) {
            result = type_char();
        } else {
            /* An int literal that does not fit in i32 is typed i64, otherwise
               codegen (which emits int literals as i32) would truncate it.
               e.g. `i64 a = 9000000000`. */
            long long v = node->as.int_lit.value;
            result = (v > 2147483647LL || v < -2147483648LL)
                         ? type_i64() : type_int();
        }
        break;

    case AST_FLOAT_LIT:
        result = type_f64();
        break;

    case AST_STRING_LIT:
        /* P5-4 S-2: a string literal IS a (static) Str — the builtin string
           type is gone. Codegen emits a static Str struct value. */
        {
            Type *strt = str_target_of_expected(c->expected_type);
            if (strt == NULL) strt = checker_str_type(c);
            if (strt == NULL)
            {
                checker_error(c, node->line, node->column,
                              "string literal requires the Str type from std.core.str "
                              "(add `import std.core.str`)");
                result = NULL;
                break;
            }
            result = strt;
        }
        break;

    case AST_FORMAT_STRING:
    {
        /* The outer f-string's expected type (e.g. Str) must NOT leak into the
           interpolated exprs — an inner literal would otherwise coerce to Str and
           fail the printable check below. Clear it for the loop, consult it after. */
        Type *fstr_expected = c->expected_type;
        c->expected_type = NULL;
        /* Type-check each interpolated expression */
        for (int i = 0; i < node->as.format_string.expr_count; i++)
        {
            Type *et = check_expr(c, node->as.format_string.exprs[i]);
            if (et == NULL)
                continue;
            /* Stage D: a Show struct/enum interpolates via Show — rewrite to
               to_str(expr) (Str), then fall through to the printable check (Str
               passes). Mirrors @print's C-2 rewrite. */
            if (type_is_show_aggregate(c, et))
            {
                wrap_arg_in_to_str(&node->as.format_string.exprs[i]);
                et = check_expr(c, node->as.format_string.exprs[i]);
                if (et == NULL)
                    continue;
            }
            /* Ensure the expression is a printable type. The pure-LS `Str` is
               printable too (interpolated via "%.*s" by codegen). */
            if (!type_is_numeric(et) && et->kind != TYPE_BOOL && et->kind != TYPE_POINTER && et->kind != TYPE_OBJECT && !type_is_str_struct(et))
            {
                checker_error(c, node->as.format_string.exprs[i]->line,
                              node->as.format_string.exprs[i]->column,
                              "cannot interpolate type '%s' in format string",
                              type_name(et));
            }
        }
        c->expected_type = fstr_expected;
        /* P5-4 S-2: an f-string IS an OWNED Str rvalue (the formatted heap
           buffer wrapped as Str, cap>0), routed through the unified has_drop
           temp/drop path. In a read-only `&Str` position the owned rvalue is
           auto-borrowed via the generic struct-arg spill. */
        {
            Type *strt = str_target_of_expected(fstr_expected);
            if (strt == NULL) strt = checker_str_type(c);
            if (strt == NULL)
            {
                checker_error(c, node->line, node->column,
                              "f-string requires the Str type from std.core.str "
                              "(add `import std.core.str`)");
                result = NULL;
                break;
            }
            result = strt;
        }
        break;
    }

    case AST_MAP_LIT:
    {
        /* If resolved_type was already set (by check_stmt VAR_DECL special-case),
           just return it — the pairs were already checked against declared K,V. */
        if (node->resolved_type)
        {
            result = node->resolved_type;
            break;
        }

        int count = node->as.map_lit.pair_count;
        if (count == 0)
        {
            /* `{}` only infers its type in a typed declaration / field / arg
               position. In an assignment (`v = {}`) there is no type to infer
               here — point at the two real options instead of the bare error. */
            checker_error(c, node->line, node->column,
                          "empty `{}` has no inferable type here; use it in a typed "
                          "declaration (e.g. `Vec(T) v = {}` / `Map(K,V) m = {}`), "
                          "or to empty an existing container call `v.clear()` "
                          "instead of `v = {}`");
            result = NULL;
            break;
        }
        checker_error(c, node->line, node->column,
                      "key-value literal requires an expected type with __from_pairs");
        result = NULL;
        break;
    }

    case AST_ARRAY_LIT:
    {
        /* If resolved_type was already set (by check_stmt VAR_DECL special-case
           for vec(T) v = [..]), just return it — elements were already checked. */
        if (node->resolved_type)
        {
            result = node->resolved_type;
            break;
        }

        /* Infer element type from first element, check all others match */
        int count = node->as.array_lit.count;
        if (count == 0)
        {
            checker_error(c, node->line, node->column,
                          "empty array literal (cannot infer element type)");
            result = NULL;
            break;
        }
        Type *elem_type = check_expr(c, node->as.array_lit.elements[0]);
        if (elem_type == NULL)
        {
            result = NULL;
            break;
        }
        for (int i = 1; i < count; i++)
        {
            Type *et = check_expr(c, node->as.array_lit.elements[i]);
            if (et == NULL)
                continue;
            if (!type_equals(elem_type, et))
            {
                checker_error(c, node->as.array_lit.elements[i]->line,
                              node->as.array_lit.elements[i]->column,
                              "array element type mismatch: expected '%s', got '%s'",
                              type_name(elem_type), type_name(et));
            }
        }
        result = type_array(elem_type, count);
        break;
    }

    case AST_BOOL_LIT:
        result = type_bool();
        break;

    case AST_NIL_LIT:
        result = type_nil();
        break;

    case AST_IDENT:
        result = check_expr_ident(c, node);
        break;

    case AST_MUT_BORROW:
        result = check_expr_mut_borrow(c, node);
        break;

    case AST_UNARY:
    {
        Type *operand = check_expr(c, node->as.unary.operand);
        if (operand == NULL)
        {
            result = NULL;
            break;
        }

        switch (node->as.unary.op)
        {
        case TOKEN_MINUS:
            if (!type_is_numeric(operand))
            {
                checker_error(c, node->line, node->column,
                              "unary '-' requires numeric type, got '%s'", type_name(operand));
                result = NULL;
            }
            else
            {
                result = operand;
            }
            break;
        case TOKEN_BANG:
            if (operand->kind != TYPE_BOOL)
            {
                checker_error(c, node->line, node->column,
                              "unary '!' requires bool, got '%s'", type_name(operand));
                result = NULL;
            }
            else
            {
                result = type_bool();
            }
            break;
        case TOKEN_TILDE:
            if (!type_is_integer(operand))
            {
                checker_error(c, node->line, node->column,
                              "unary '~' requires integer type, got '%s'", type_name(operand));
                result = NULL;
            }
            else
            {
                result = operand;
            }
            break;
        case TOKEN_AMP:
            /* &x -> *T */
            result = type_pointer(operand);
            break;
        case TOKEN_STAR:
            /* *ptr -> dereference */
            if (operand->kind != TYPE_POINTER)
            {
                checker_error(c, node->line, node->column,
                              "cannot dereference non-pointer type '%s'", type_name(operand));
                result = NULL;
            }
            else
            {
                result = operand->as.pointer_to;
            }
            break;
        default:
            checker_error(c, node->line, node->column, "unknown unary operator");
            result = NULL;
            break;
        }
        break;
    }

    case AST_BINARY:
        result = check_expr_binary(c, node);
        break;

    case AST_CALL:
        result = check_expr_call(c, node);
        break;

    case AST_INDEX:
        result = check_expr_index(c, node);
        break;

    case AST_FIELD:
        result = check_expr_field(c, node);
        break;

    case AST_CLOSURE:
        result = check_expr_closure(c, node);
        break;

    case AST_MATCH:
        result = check_expr_match(c, node);
        break;

    case AST_CAST:
    {
        Type *expr = check_expr(c, node->as.cast.expr);
        Type *target = resolve_type_node(c, node->as.cast.target_type,
                                         node->line, node->column);
        if (expr == NULL || target == NULL)
        {
            result = NULL;
            break;
        }

        /* Allow numeric<->numeric casts, pointer casts, and object casts */
        if (type_is_numeric(expr) && type_is_numeric(target))
        {
            result = target;
        }
        else if (expr->kind == TYPE_POINTER && target->kind == TYPE_POINTER)
        {
            result = target;
        }
        else if (type_is_integer(expr) && target->kind == TYPE_POINTER)
        {
            result = target;
        }
        else if (expr->kind == TYPE_POINTER && type_is_integer(target))
        {
            result = target;
            /* object <-> pointer: explicit cast */
        }
        else if (expr->kind == TYPE_OBJECT && target->kind == TYPE_POINTER)
        {
            result = target;
        }
        else if (expr->kind == TYPE_POINTER && target->kind == TYPE_OBJECT)
        {
            result = target;
            /* object <-> integer: explicit cast (like void* <-> intptr_t) */
        }
        else if (expr->kind == TYPE_OBJECT && type_is_integer(target))
        {
            result = target;
        }
        else if (type_is_integer(expr) && target->kind == TYPE_OBJECT)
        {
            result = target;
        }
        else
        {
            checker_error(c, node->line, node->column,
                          "invalid cast from '%s' to '%s'",
                          type_name(expr), type_name(target));
            result = NULL;
        }
        break;
    }

    case AST_SIZEOF:
    {
        /* sizeof(Type) -> i64, compile-time byte size. Resolve the operand type
           (type-param `T` is resolved via the active type-alias substitution
           registered during generic instantiation, same as cast). */
        Type *st = resolve_type_node(c, node->as.sizeof_expr.type_node,
                                     node->line, node->column);
        if (st == NULL)
        {
            result = NULL;
            break;
        }
        node->as.sizeof_expr.sized_type = st;
        result = type_i64();
        break;
    }

    case AST_TYPENAME:
    {
        /* __type_name(Type) -> Str, a compile-time type name. Resolve the operand
           type (type-param `T` is substituted via the active type-alias table
           during generic instantiation, exactly like sizeof / cast). The result is
           a static Str built in codegen from the resolved type's name. */
        Type *nt = resolve_type_node(c, node->as.typename_expr.type_node,
                                     node->line, node->column);
        if (nt == NULL)
        {
            result = NULL;
            break;
        }
        node->as.typename_expr.named_type = nt;
        result = checker_str_type(c);
        break;
    }

    case AST_TRY:
    {
        if (c->current_fn_return == NULL)
        {
            checker_error(c, node->line, node->column,
                          "try expression outside of function");
            result = NULL;
            break;
        }
        Type *inner = check_expr(c, node->as.try_expr.expr);
        if (inner == NULL) { result = NULL; break; }

        bool is_result = (inner->kind == TYPE_ENUM &&
                          strncmp(inner->as.enom.name, "Result(", 7) == 0);
        bool is_option = (inner->kind == TYPE_ENUM &&
                          strncmp(inner->as.enom.name, "Option(", 7) == 0);
        if (!is_result && !is_option)
        {
            checker_error(c, node->line, node->column,
                          "try requires Result(T,E) or Option(T), got '%s'",
                          type_name(inner));
            result = NULL;
            break;
        }

        /* Extract success type (T) and Err type (E, if Result) */
        Type *success_t = NULL;
        Type *err_t = NULL;
        for (int i = 0; i < inner->as.enom.variant_count; i++)
        {
            const char *vn = inner->as.enom.variants[i].name;
            int pc = inner->as.enom.variants[i].payload_count;
            if (is_result && strcmp(vn, "Ok") == 0 && pc > 0)
                success_t = inner->as.enom.variants[i].payload_types[0];
            else if (is_result && strcmp(vn, "Err") == 0 && pc > 0)
                err_t = inner->as.enom.variants[i].payload_types[0];
            else if (is_option && strcmp(vn, "Some") == 0 && pc > 0)
                success_t = inner->as.enom.variants[i].payload_types[0];
        }

        /* Validate current function return type can absorb the failure path */
        Type *fn_ret = c->current_fn_return;
        if (is_result)
        {
            if (fn_ret->kind != TYPE_ENUM ||
                strncmp(fn_ret->as.enom.name, "Result(", 7) != 0)
            {
                checker_error(c, node->line, node->column,
                              "try on Result requires function to return Result(_, E), got '%s'",
                              type_name(fn_ret));
                result = NULL;
                break;
            }
            Type *fn_err_t = NULL;
            for (int i = 0; i < fn_ret->as.enom.variant_count; i++)
            {
                if (strcmp(fn_ret->as.enom.variants[i].name, "Err") == 0 &&
                    fn_ret->as.enom.variants[i].payload_count > 0)
                {
                    fn_err_t = fn_ret->as.enom.variants[i].payload_types[0];
                    break;
                }
            }
            if (err_t != NULL && fn_err_t != NULL && !type_equals(err_t, fn_err_t))
            {
                checker_error(c, node->line, node->column,
                              "try Err type mismatch: expression has Err '%s' but function returns Err '%s'",
                              type_name(err_t), type_name(fn_err_t));
                result = NULL;
                break;
            }
        }
        else /* is_option */
        {
            if (fn_ret->kind != TYPE_ENUM ||
                strncmp(fn_ret->as.enom.name, "Option(", 7) != 0)
            {
                checker_error(c, node->line, node->column,
                              "try on Option requires function to return Option(_), got '%s'",
                              type_name(fn_ret));
                result = NULL;
                break;
            }
        }

        node->as.try_expr.fn_return_type = fn_ret;
        result = success_t;
        break;
    }

    case AST_FORCE_UNWRAP:
    {
        Type *inner = check_expr(c, node->as.force_unwrap.expr);
        if (inner == NULL) { result = NULL; break; }

        bool is_result = (inner->kind == TYPE_ENUM &&
                          strncmp(inner->as.enom.name, "Result(", 7) == 0);
        bool is_option = (inner->kind == TYPE_ENUM &&
                          strncmp(inner->as.enom.name, "Option(", 7) == 0);
        if (!is_result && !is_option)
        {
            checker_error(c, node->line, node->column,
                          "force-unwrap '!' requires Option(T) or Result(T,E), got '%s'",
                          type_name(inner));
            result = NULL;
            break;
        }

        /* C1 `.expect(msg)`: type the message expr (P5-4 S-3: codegen no longer
           has an untyped-literal fallback — the literal must resolve to Str). */
        if (node->as.force_unwrap.message != NULL)
            check_expr(c, node->as.force_unwrap.message);

        /* Extract success type T */
        Type *success_t = NULL;
        for (int i = 0; i < inner->as.enom.variant_count; i++)
        {
            const char *vn = inner->as.enom.variants[i].name;
            int pc = inner->as.enom.variants[i].payload_count;
            if (is_result && strcmp(vn, "Ok") == 0 && pc > 0)
                success_t = inner->as.enom.variants[i].payload_types[0];
            else if (is_option && strcmp(vn, "Some") == 0 && pc > 0)
                success_t = inner->as.enom.variants[i].payload_types[0];
        }

        /* Move-elision (Q4): force-unwrapping consumes the operand's payload —
           mark the source IDENT moved so (a) re-use is rejected and (b) codegen
           invalidates the source enum's scope-drop (no double-free).
           L-019 (2026-07-05): type_is_movable now covers has_drop enums, so the
           former inline marking patch collapses into the generic move site
           (which also rejects moving a pinned borrow source). POD Option(int) /
           borrows / rvalues are left live by its own guards. */
        checker_try_mark_moved(c, node->as.force_unwrap.expr);

        result = success_t;
        break;
    }

    case AST_AT_TIME:
    {
        Type *inner = check_expr(c, node->as.at_time.expr);
        result = inner;
        break;
    }

    case AST_AT_BENCH:
    {
        check_expr(c, node->as.at_bench.expr);
        result = type_f64();
        break;
    }

    case AST_RANGE:
    {
        Type *start = check_expr(c, node->as.range.start);
        Type *end = check_expr(c, node->as.range.end);
        if (start == NULL || end == NULL)
        {
            result = NULL;
            break;
        }
        if (!type_is_integer(start))
        {
            checker_error(c, node->as.range.start->line, node->as.range.start->column,
                          "range start must be integer, got '%s'", type_name(start));
            result = NULL;
            break;
        }
        if (!type_is_integer(end))
        {
            checker_error(c, node->as.range.end->line, node->as.range.end->column,
                          "range end must be integer, got '%s'", type_name(end));
            result = NULL;
            break;
        }
        /* Range expression's resolved_type is int (the element type) */
        result = type_int();
        break;
    }

    case AST_NEW_EXPR:
        result = check_expr_new_expr(c, node);
        break;

    /* Statements that can appear as expressions in match arms */
    case AST_BLOCK:
    {
        chk_push_scope(c);
        comptime_expand_block(c, node); /* expand comptime for/if/match in arm/expr blocks too */
        Type *last = type_void();
        for (int i = 0; i < node->as.block.stmt_count; i++)
        {
            AstNode *s = node->as.block.stmts[i];
            if (i == node->as.block.stmt_count - 1 && s->kind == AST_EXPR_STMT)
            {
                last = check_expr(c, s->as.expr_stmt.expr);
            }
            else
            {
                check_stmt(c, s);
            }
        }
        chk_pop_scope(c);
        result = last;
        break;
    }

    case AST_COMPTIME_FIELD:
        /* Step 1: `v.(f)` is lowered to a concrete field access during the comptime
           unroll (step 2). Reject cleanly here (and break the default→check_stmt
           recursion) until then; outside a comptime for it is always an error. */
        checker_error(c, node->line, node->column,
            "comptime field access 'v.(f)' is only valid inside a 'comptime for'");
        result = type_void();
        break;

    case AST_COMPTIME_BLOCK:
        /* Step 1 (docs/plan_comptime_consteval.md): a `comptime { ... }` block is
           evaluated by the compile-time evaluator (Step 3). Reject cleanly here
           (and break the check_expr→check_stmt recursion) until then. */
        checker_error(c, node->line, node->column,
            "comptime block evaluation is not yet implemented");
        result = type_void();
        break;

    default:
        /* For non-expression nodes used as expressions, try treating as statement */
        check_stmt(c, node);
        result = type_void();
        break;
    }

    if (node)
    {
        if (c->expected_type && c->expected_type->kind == TYPE_BLOCK &&
            result && result->kind == TYPE_FUNCTION &&
            type_assignable(c->expected_type, result))
        {
            node->coerce_fn_to_block = true;
            type_free(node->coerce_block_type);
            node->coerce_block_type = type_clone(c->expected_type);
        }
        node->resolved_type = result;
    }
    return result;
}
