/* checker_stmt.c — statement-checking cluster, split out of checker.c
   (Batch 7 Task 7.5, docs/plan_arch_round2_backlog.md §7.5): the
   check_stmt_var_decl / check_stmt_assign / check_stmt_return helpers
   (S3b case extractions) and the check_stmt dispatcher.
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

/* ---- Statement checking ---- */

/* S3b: extracted from the check_stmt dispatcher switch. Each function is the
   verbatim case body wrapped in `do { ... } while (0)` so the original
   switch-level `break;` statements flow to the end unchanged (no loops inside
   these cases use `break`/`continue`, so the wrapper is behavior-preserving). */
static void check_stmt_var_decl(Checker *c, AstNode *node)
{
    do {
        /* Type-inferred local (var_type == NULL).  Only synthesized by the
           for-in desugarer (the parser always requires an explicit type); infer
           the type from the initializer and bind. */
        if (node->as.var_decl.var_type == NULL)
        {
            Type *it = node->as.var_decl.init
                           ? check_expr(c, node->as.var_decl.init)
                           : NULL;
            if (it == NULL)
                break;
            /* Inferred BORROW (the for-in borrow desugar `x = v.get_ref(i)` infers
               &T): route through the named-local-borrow machinery so x is marked
               is_borrow + its source is pinned, exactly like an explicit
               `&T x = v.get_ref(i)`. Without this, x is bound as a bare reference
               type with no borrow flag → field/method access misresolves (e.g.
               `x.len()` hits the Str.len field instead of the method). */
            if (it->kind == TYPE_REFERENCE)
            {
                check_local_borrow_decl(c, node, it);
                node->resolved_type = it;
                break;
            }
            node->resolved_type = it;
            scope_define(c->current_scope, node->as.var_decl.name, it);
            break;
        }

        Type *declared = resolve_type_node(c, node->as.var_decl.var_type,
                                           node->line, node->column);
        if (declared == NULL)
            break;

        /* Phase 1 (borrow extension): a named local borrow `&T r = &x`. Handled
           in full by a dedicated path (registers the symbol with the pointee
           type + is_borrow, pins the referent); skip the owned-value machinery. */
        if (declared->kind == TYPE_REFERENCE)
        {
            check_local_borrow_decl(c, node, declared);
            node->resolved_type = declared;
            break;
        }

        /* A slice local `&array(T) s = v[a..b]` — a borrowed {ptr,len} value that
           pins its source. Handled by a dedicated path; skip owned machinery. */
        if (declared->kind == TYPE_SLICE)
        {
            check_local_slice_decl(c, node, declared);
            node->resolved_type = declared;
            break;
        }

        /* REPL Phase 2: a replayed pre-existing global (container/has_drop).
           Register the symbol so later statements resolve it, but skip M-DEF
           (which would re-synthesize `= {}` → re-construct + reset the live
           global each snippet), init handling, and move-tracking. The storage
           lives in the introducing snippet; jit.c strips this re-emitted copy
           to an external reference. Only the REPL sets this flag, so AOT/JIT
           file execution never takes this path. */
        if (node->as.var_decl.is_repl_extern)
        {
            if (!scope_resolve_local(c->current_scope, node->as.var_decl.name))
                scope_define(c->current_scope, node->as.var_decl.name, declared);
            node->resolved_type = declared;
            break;
        }

        /* M-DEF: implicit empty/default init — `T v` ≡ `T v = {}` for any type
           where `= {}` is already a legal initializer (user containers like
           Vec/Map and struct zero-init via the empty-brace branch below).
           Synthesize an empty brace
           literal so the existing `= {}` paths run unchanged. POD/string/enum
           keep their current no-init behavior (their `{}` is not a legal init). */
        if (node->as.var_decl.init == NULL &&
            declared->kind == TYPE_STRUCT)
        {
            AstNode *empty = ast_new(AST_MAP_LIT, node->line, node->column);
            empty->as.map_lit.keys = NULL;
            empty->as.map_lit.vals = NULL;
            empty->as.map_lit.pair_count = 0;
            node->as.var_decl.init = empty;
        }

        if (node->as.var_decl.init)
        {
            if (declared && declared->kind == TYPE_STRUCT &&
                node->as.var_decl.init->kind == AST_ARRAY_LIT &&
                find_method(c, impl_key_of_type(declared), "__from_list") != NULL)
            {
                checker_tag_user_from_list_literal(c, declared,
                    node->as.var_decl.init, "list-literal");
            }
            /* M-LIT: `Map(K,V) m = { k: v, ... }` (non-empty) → user key-value
               literal via the __from_pairs protocol. */
            else if (declared && declared->kind == TYPE_STRUCT &&
                node->as.var_decl.init->kind == AST_MAP_LIT &&
                node->as.var_decl.init->as.map_lit.pair_count > 0 &&
                find_method(c, impl_key_of_type(declared), "__from_pairs") != NULL)
            {
                checker_tag_user_from_pairs_literal(c, declared,
                    node->as.var_decl.init, "map-literal");
            }
            else if (declared && declared->kind == TYPE_STRUCT &&
                node->as.var_decl.init->kind == AST_MAP_LIT &&
                node->as.var_decl.init->as.map_lit.pair_count == 0)
            {
                /* Inferred aggregate init: `Type v = {}` zero-initializes a struct
                   (C++-style), inferring the struct type from the declared LHS.
                   Reinterpret the empty brace literal (parsed as an empty map) as a
                   zero-init struct literal of `declared`. Unspecified fields are
                   zero (AST_NEW_EXPR codegen ConstNull's the whole struct first).
                   Lets `RawVec(string) v = {}` replace `new_rawvec(string)()`,
                   matching the builtin `vec(T) v = []`. */
                AstNode *ml = node->as.var_decl.init;
                /* free the (empty) map-lit arrays before repurposing the node */
                free(ml->as.map_lit.keys);
                free(ml->as.map_lit.vals);
                ml->kind = AST_NEW_EXPR;
                size_t snl = strlen(declared->as.strukt.name);
                char *sdup = (char *)malloc_safe(snl + 1);
                memcpy(sdup, declared->as.strukt.name, snl + 1);
                ml->as.new_expr.struct_name = sdup;
                ml->as.new_expr.module = NULL;
                ml->as.new_expr.field_inits = NULL;
                ml->as.new_expr.field_init_count = 0;
                ml->as.new_expr.on_stack = true;
                ml->as.new_expr.type_args = NULL;
                ml->as.new_expr.type_arg_count = 0;
                ml->resolved_type = declared;
            }
            else
            {
                /* Plumb expected_type so variant-ctor disambiguation can pick the
                   right enum (e.g. Some(42) when both Option(int) & Option(string)
                   are instantiated). */
                Type *saved_expected = c->expected_type;
                c->expected_type = declared;
                Type *init_type = check_expr(c, node->as.var_decl.init);
                c->expected_type = saved_expected;
                /* Phase 2 (borrow extension): forbid silently copying a
                   borrow-returning call result into an OWNED variable
                   (`Inner b = o.get()`). The `T ← &T` auto-reborrow would store
                   the pointer as a value / alias a has_drop referent. Bind a
                   borrow (`&Inner r = o.get()`) or copy a specific field. */
                if (init_type != NULL && init_type->kind == TYPE_REFERENCE &&
                    declared->kind != TYPE_REFERENCE &&
                    node->as.var_decl.init->kind == AST_CALL)
                {
                    checker_error(c, node->line, node->column,
                        "cannot copy a value out of a borrow result; bind it to "
                        "a borrow (`&%s %s = ...`) to alias it instead",
                        type_name(declared), node->as.var_decl.name);
                }
                else if (init_type != NULL && !type_assignable(declared, init_type))
                {
                    checker_error(c, node->line, node->column,
                                  "cannot initialize '%s' (type '%s') with value of type '%s'",
                                  node->as.var_decl.name, type_name(declared), type_name(init_type));
                }
            }

            /* Move tracking (Phase 3 — struct): structs with has_drop follow move
               semantics just like strings. Source IDENT is marked below via
               checker_try_mark_moved; codegen still deep-clones, but the checker
               now rejects any subsequent use of the source. */
        }

        if (scope_resolve_local(c->current_scope, node->as.var_decl.name))
        {
            checker_error(c, node->line, node->column,
                          "variable '%s' already defined in this scope",
                          node->as.var_decl.name);
        }
        else
        {
            scope_define(c->current_scope, node->as.var_decl.name, declared);
            /* Phase 5.5: copying out of a writable borrow leaks ownership the
               caller still holds. Reject before move-tracking runs. */
            checker_reject_mut_borrow_copy_source(c, node->as.var_decl.init,
                                                 "initialize new variable from writable borrow");
            /* Phase 5.8: same for struct borrows. */
            checker_reject_struct_borrow_copy_source(c, node->as.var_decl.init,
                                                 "initialize new variable from struct borrow");
            /* F.2: Block parameters are shallow-copy borrows; cannot be moved out. */
            checker_reject_block_param_move(c, node->as.var_decl.init,
                                            "move Block into new variable");
            /* Phase G: copying a Block out of a struct field / vec element / map
               value is now allowed — codegen deep-clones the env (see
               cg_emit_block_env_clone), so the new variable owns an independent
               env with no shared-env double-free. (Former F.3/F.4A rejections.) */
            /* Move tracking: if the initializer is a dynamic string IDENT, the source is moved.
               Static strings, borrow params, and non-string types are left untouched
               (checker_try_mark_moved skips them). Reading a borrow into a new local
               yields a shallow copy with cap==0 at codegen — safe, no move. */
            checker_try_mark_moved(c, node->as.var_decl.init);
        }
        node->resolved_type = declared;
    } while (0);
}

static void check_stmt_assign(Checker *c, AstNode *node)
{
    do {
        /* IndexMut protocol: `v[i] = x` where v is a struct opting in via
           `__index_set(&!self, int, E)` desugars to `v.__index_set(i, x)`. Must
           run BEFORE check_expr(target) (which would read-rewrite v[i] to
           __index). Reuses tobj/idxn/valn; rewrites the assign node into a call. */
        if (node->as.assign.target->kind == AST_INDEX &&
            node->as.assign.target->as.index_expr.index_count >= 2)
        {
            /* Multi-subscript store t[i,j,..] = v -> __index_set{N}(i,j,..,v). */
            AstNode *tgt = node->as.assign.target;
            AstNode *tobj = tgt->as.index_expr.object;
            Type *to = check_expr(c, tobj);
            int nidx = tgt->as.index_expr.index_count;
            char mname[28];
            snprintf(mname, sizeof(mname), "__index_set%d", nidx);
            if (to && to->kind == TYPE_STRUCT && find_method_ensured(c, to, mname) != NULL)
            {
                AstNode *valn = node->as.assign.value;
                AstNode *call = make_multi_index_call(node->line, node->column,
                    tobj, tgt->as.index_expr.indices, nidx, valn, mname);
                node->kind = AST_EXPR_STMT;
                node->as.expr_stmt.expr = call;
                check_expr(c, call);
                break;
            }
            checker_error(c, node->line, node->column,
                "type '%s' does not support %d-D index assignment (no method '%s')",
                to ? type_name(to) : "?", nidx, mname);
            break;
        }
        if (node->as.assign.target->kind == AST_INDEX)
        {
            AstNode *tobj = node->as.assign.target->as.index_expr.object;
            Type *to = check_expr(c, tobj);
            /* `s[i] = x` on a writable slice — store into the borrowed range.
               Read-only slices reject; has_drop elements deferred (the store
               would need drop-old-element semantics). */
            if (to && to->kind == TYPE_SLICE)
            {
                if (!to->is_mut)
                {
                    checker_error(c, node->line, node->column,
                        "cannot assign through a read-only slice; bind it as "
                        "`&!array(T)` for a writable view");
                    break;
                }
                Type *et = to->as.array.elem;
                Type *idxt = check_expr(c, node->as.assign.target->as.index_expr.index);
                if (idxt && !type_is_integer(idxt))
                    checker_error(c, node->line, node->column,
                                  "slice index must be integer, got '%s'", type_name(idxt));
                Type *valt = check_expr(c, node->as.assign.value);
                if (valt && et && !type_assignable(et, valt))
                    checker_error(c, node->line, node->column,
                        "cannot store '%s' into slice of '%s'",
                        type_name(valt), type_name(et));
                node->resolved_type = type_void();
                break;
            }
            if (to && to->kind == TYPE_STRUCT &&
                find_method_ensured(c, to, "__index_set") != NULL)
            {
                AstNode *idxn = node->as.assign.target->as.index_expr.index;
                AstNode *valn = node->as.assign.value;
                /* (the small AST_INDEX shell is intentionally leaked, not freed,
                   to avoid any aliasing with the union we overwrite below) */
                AstNode *call = make_index_protocol_call(node->line, node->column,
                                                         tobj, idxn, valn,
                                                         "__index_set");
                node->kind = AST_EXPR_STMT;
                node->as.expr_stmt.expr = call;
                check_expr(c, call);
                break;
            }
        }
        /* A comptime constant is immutable. Detect this BEFORE check_expr folds the
           target IDENT into a literal (which would otherwise yield a confusing
           "invalid assignment target"). */
        if (node->as.assign.target->kind == AST_IDENT) {
            Symbol *cc_sym = scope_resolve(c->current_scope,
                                           node->as.assign.target->as.ident.name);
            if (cc_sym && cc_sym->is_comptime_const) {
                checker_error(c, node->line, node->column,
                              "cannot assign to comptime constant '%s'",
                              node->as.assign.target->as.ident.name);
                break;
            }
        }
        Type *target = check_expr(c, node->as.assign.target);
        /* Plumb expected_type = target type so a bare variant ctor on the RHS
           (`x = None`, `self.buf[i] = Some(v)`) disambiguates when several enum
           instantiations share the variant name (e.g. Option(int)+Option(string)). */
        Type *saved_exp_assign = c->expected_type;
        if (target) c->expected_type = target;
        Type *value = check_expr(c, node->as.assign.value);
        c->expected_type = saved_exp_assign;
        if (target == NULL || value == NULL)
            break;

        /* Phase 5: reassignment to a borrow parameter is forbidden — it holds
           no ownership, so overwriting would either leak (if new value is owned)
           or produce confusing "mutation visible to caller?" semantics. */
        if (node->as.assign.target->kind == AST_IDENT)
        {
            Symbol *tsym = scope_resolve(c->current_scope,
                                         node->as.assign.target->as.ident.name);
            if (tsym && tsym->is_borrow)
            {
                checker_move_error(c, node->line, node->column,
                                   "cannot assign to borrowed variable '%s' (read-only reference)",
                                   node->as.assign.target->as.ident.name);
                break;
            }
        }
        /* Phase 5.8: field assign `s.field = x` is forbidden when the base
           is a read-only struct borrow. Writable borrows pass through. */
        if (node->as.assign.target->kind == AST_FIELD)
        {
            AstNode *base = node->as.assign.target->as.field_access.object;
            if (base && base->kind == AST_IDENT)
            {
                Symbol *bsym = scope_resolve(c->current_scope, base->as.ident.name);
                if (bsym && bsym->is_borrow && bsym->type &&
                    bsym->type->kind == TYPE_STRUCT)
                {
                    checker_move_error(c, node->line, node->column,
                                       "cannot assign to '%s.%s': '%s' is a read-only borrow",
                                       base->as.ident.name,
                                       node->as.assign.target->as.field_access.field,
                                       base->as.ident.name);
                    break;
                }
            }
        }
        /* For compound assignments (+=, -=, etc.), check operand types */
        if (node->as.assign.op != TOKEN_ASSIGN)
        {
            if (!type_is_numeric(target))
            {
                checker_error(c, node->line, node->column,
                              "compound assignment requires numeric type, got '%s'",
                              type_name(target));
                break;
            }
        }

        if (!type_assignable(target, value))
        {
            checker_error(c, node->line, node->column,
                          "cannot assign '%s' to '%s'",
                          type_name(value), type_name(target));
        }

        /* Move tracking for simple assignment (=) only.
           Compound assignments (+=, etc.) do not transfer ownership. */
        if (node->as.assign.op == TOKEN_ASSIGN)
        {
            /* Phase 5.5: reject RHS being a writable borrow (same rationale as
               var_decl — content cannot leave the borrow). */
            checker_reject_mut_borrow_copy_source(c, node->as.assign.value,
                                                  "assign writable borrow contents to another variable");
            /* Phase 5.8: same for struct borrows. */
            checker_reject_struct_borrow_copy_source(c, node->as.assign.value,
                                                 "assign struct borrow contents to another variable");
            /* F.2: Block parameters are shallow-copy borrows; cannot be moved out. */
            checker_reject_block_param_move(c, node->as.assign.value,
                                            "assign Block parameter to another variable");
            /* Phase G: assigning a Block out of a struct field / vec element / map
               value is now allowed — codegen deep-clones the env. (Former
               F.3/F.4A rejections.) */
            /* If RHS is a dynamic string/Block/movable IDENT, mark it as moved */
            checker_try_mark_moved(c, node->as.assign.value);
        }

        /* Struct assignment (Phase 3): structs with has_drop are treated as
           movable. Source IDENT is marked moved above via checker_try_mark_moved
           when the target is an AST_IDENT (full-variable reassignment).
           Field-level assignment (p.name = ...) has target->kind == AST_FIELD,
           which does NOT mark the surrounding struct 'p' as moved — only the
           rhs identifier (if any) is moved. */
    } while (0);
}

static void check_stmt_return(Checker *c, AstNode *node)
{
    do {
        if (c->current_fn_return == NULL)
        {
            /* Inference mode (map() return type): capture type instead of erroring. */
            if (c->closure_infer_return_slot && node->as.return_stmt.value)
            {
                bool saved_in_return = c->in_return_expr;
                c->in_return_expr = true;
                Type *val = check_expr(c, node->as.return_stmt.value);
                c->in_return_expr = saved_in_return;
                if (*c->closure_infer_return_slot == NULL)
                    *c->closure_infer_return_slot = val;
                break;
            }
            checker_error(c, node->line, node->column, "return outside of function");
            break;
        }
        if (node->as.return_stmt.value)
        {
            /* Mark as being in return expression - prevents move semantics on the returned var */
            bool saved_in_return = c->in_return_expr;
            c->in_return_expr = true;

            /* Plumb expected_type so bare variant ctors (e.g. `Err("msg")`) in
               `return` can disambiguate against the function's declared return
               type when several Result/Option instantiations are in scope. */
            Type *saved_expected = c->expected_type;
            c->expected_type = c->current_fn_return;
            Type *val = check_expr(c, node->as.return_stmt.value);
            c->expected_type = saved_expected;
            if (c->current_fn_return->kind == TYPE_SLICE)
            {
                /* Slice return under single-input elision: the returned view must
                   be a slice of matching element type, rooted at the one borrow
                   input (`self.field[a..b]` → self). A view of a local/temporary
                   would dangle. */
                Type *want = c->current_fn_return;
                if (val != NULL && (val->kind != TYPE_SLICE ||
                    !type_equals(val->as.array.elem, want->as.array.elem)))
                {
                    checker_error(c, node->line, node->column,
                                  "return type mismatch: expected '%s', got '%s'",
                                  type_name(want), type_name(val));
                }
                Symbol *root = node->as.return_stmt.value
                    ? checker_place_root_symbol(c, node->as.return_stmt.value) : NULL;
                if (root == NULL || !(root->is_borrow || root->is_mut_borrow))
                {
                    checker_error(c, node->line, node->column,
                        "a returned slice must derive from the `&self` / borrow "
                        "parameter (e.g. `self.field[a..b]`); cannot return a view "
                        "of a local or temporary — it would dangle");
                }
            }
            else if (c->current_fn_return->kind == TYPE_REFERENCE)
            {
                /* Phase 2 (borrow extension): the function returns a borrow (&T
                   / &!T). The returned expression must be a PLACE rooted at the
                   single borrow input (`self` / a borrow parameter) whose pointee
                   matches — escape analysis: a borrow of a LOCAL or temporary
                   would dangle once the function returns. The generic
                   type_assignable path is skipped here: `&!T ← T` is not a normal
                   auto-borrow, but returning the place of a `&!self` IS sound. */
                Type *pointee = c->current_fn_return->as.pointer_to;
                Type *vp = (val && val->kind == TYPE_REFERENCE)
                               ? val->as.pointer_to : val;
                if (vp != NULL && pointee != NULL && !type_equals(vp, pointee))
                {
                    checker_error(c, node->line, node->column,
                                  "return type mismatch: expected '%s', got '%s'",
                                  type_name(c->current_fn_return), type_name(val));
                }
                /* v1 scope: borrow returns are AGGREGATE-only (struct/enum, pointer
                   ABI + field-access auto-deref). A POD-scalar borrow return
                   (`-> &int`) has no wired value-context auto-deref (`x == 7` would
                   see `&int`), so reject it clearly. Screened HERE (body check) not
                   at signature registration so an uncalled generic `get_ref(&self)
                   ->&T` on a POD instance (e.g. Vec(int)) does not poison the whole
                   instantiation — only an actual scalar instantiation errors.
                   Reading a POD element needs no borrow: return by value / `get!`.
                   (docs/plan_borrow_extension.md "下一步") */
                if (pointee != NULL && pointee->kind != TYPE_STRUCT &&
                    pointee->kind != TYPE_ENUM)
                {
                    checker_error(c, node->line, node->column,
                        "cannot return a borrow of a POD scalar (&%s%s): borrow "
                        "returns are supported for struct/enum elements only — a POD "
                        "value needs no borrow, return it by value (or use `get!`)",
                        c->current_fn_return->is_mut ? "!" : "", type_name(pointee));
                }
                Symbol *root = node->as.return_stmt.value
                    ? checker_place_root_symbol(c, node->as.return_stmt.value) : NULL;
                if (root == NULL || !(root->is_borrow || root->is_mut_borrow))
                {
                    checker_error(c, node->line, node->column,
                        "a returned borrow must derive from the `&self` / borrow "
                        "parameter (a place like `self` or `self.field`); cannot "
                        "return a borrow of a local or temporary — it would dangle");
                }
                else if (c->current_fn_return->is_mut && root->is_borrow)
                {
                    /* Returning &!T but the input is only a read-only borrow. */
                    checker_error(c, node->line, node->column,
                        "cannot return a writable borrow `&!` derived from the "
                        "read-only borrow '%s'", root->name);
                }
            }
            else if (val != NULL && !type_assignable(c->current_fn_return, val))
            {
                checker_error(c, node->line, node->column,
                              "return type mismatch: expected '%s', got '%s'",
                              type_name(c->current_fn_return), type_name(val));
            }

            /* Mark returned identifier as is_returning (skip destructor) */
            if (node->as.return_stmt.value->kind == AST_IDENT)
            {
                Symbol *sym = scope_resolve(c->current_scope,
                                            node->as.return_stmt.value->as.ident.name);
                if (sym != NULL)
                {
                    sym->is_returning = true;
                }
            }

            c->in_return_expr = saved_in_return;
        }
        else
        {
            if (c->current_fn_return->kind != TYPE_VOID)
            {
                checker_error(c, node->line, node->column,
                              "return without value in function returning '%s'",
                              type_name(c->current_fn_return));
            }
        }
    } while (0);
}

void check_stmt(Checker *c, AstNode *node)
{
    if (node == NULL)
        return;

    switch (node->kind)
    {
    case AST_VAR_DECL:
        check_stmt_var_decl(c, node);
        break;

    case AST_ASSIGN:
        check_stmt_assign(c, node);
        break;

    case AST_RETURN:
        check_stmt_return(c, node);
        break;

    case AST_IF:
    {
        Type *cond = check_expr(c, node->as.if_stmt.cond);
        if (cond != NULL && cond->kind != TYPE_BOOL)
        {
            checker_error(c, node->as.if_stmt.cond->line, node->as.if_stmt.cond->column,
                          "if condition must be bool, got '%s'", type_name(cond));
        }

        /* Phase B: snapshot move state before each branch so we can merge at the join. */
        MoveSnapshot before_if;
        move_snap_capture(c, &before_if);

        check_stmt(c, node->as.if_stmt.then_block);

        if (node->as.if_stmt.else_block)
        {
            MoveSnapshot after_then;
            move_snap_capture(c, &after_then);

            /* Reset to pre-if state and check the else branch */
            move_snap_restore(&before_if);
            check_stmt(c, node->as.if_stmt.else_block);

            MoveSnapshot after_else;
            move_snap_capture(c, &after_else);

            /* Merge: MOVED∧MOVED → MOVED; otherwise any move contributes → MAYBE_MOVED */
            move_snap_merge_into_symbols(&after_then, &after_else);

            move_snap_free(&after_then);
            move_snap_free(&after_else);
        }
        else
        {
            /* No else branch: any move in the then-branch is only possible,
               so elevate LIVE→MOVED transitions to MAYBE_MOVED. */
            move_elevate_moves_to_maybe(&before_if);
        }
        move_snap_free(&before_if);
        break;
    }

    case AST_WHILE:
    {
        Type *cond = check_expr(c, node->as.while_stmt.cond);
        if (cond != NULL && cond->kind != TYPE_BOOL)
        {
            checker_error(c, node->as.while_stmt.cond->line, node->as.while_stmt.cond->column,
                          "while condition must be bool, got '%s'", type_name(cond));
        }

        /* Phase B: 2-pass analysis for loops.
           Pass 1 silently discovers all moves in the body; pass 2 pre-seeds those
           variables as MAYBE_MOVED and re-checks to report real errors. The loop
           may execute 0 times, so post-loop state is at best MAYBE_MOVED. */
        MoveSnapshot before_loop;
        move_snap_capture(c, &before_loop);

        bool saved_silent = c->silent_move_errors;
        c->silent_move_errors = true;
        check_stmt(c, node->as.while_stmt.body);
        c->silent_move_errors = saved_silent;

        MoveSnapshot after_pass1;
        move_snap_capture(c, &after_pass1);

        /* Restore to pre-loop, then pre-seed MAYBE_MOVED for anything moved in pass 1 */
        move_snap_restore(&before_loop);
        move_preseed_maybe_from_pass1(&before_loop, &after_pass1);

        /* Pass 2: real error reporting */
        check_stmt(c, node->as.while_stmt.body);

        /* Post-loop: anything that became MOVED in pass 2 is really MAYBE_MOVED
           since the loop may not execute at all. */
        move_elevate_moves_to_maybe(&before_loop);

        move_snap_free(&before_loop);
        move_snap_free(&after_pass1);
        break;
    }

    case AST_FOR:
    {
        /* Borrowing for-in: `for x in &v` — bind x as a zero-copy borrow &T of
           each element (vs the owning `for x in v` which clones on read). v must
           be a Vec-like exposing get_ref(i)->&T and len(). Desugars to an index
           loop over get_ref (see build_foreach_borrow_desugar). */
        {
            AstNode *itn = node->as.for_stmt.iter;
            if (itn->kind == AST_UNARY && itn->as.unary.op == TOKEN_AMP)
            {
                Type *ct = check_expr(c, itn->as.unary.operand);
                if (ct != NULL && ct->kind == TYPE_STRUCT)
                {
                    Type *getref  = find_method_ensured(c, ct, "get_ref");
                    bool has_len    = find_method_ensured(c, ct, "len") != NULL;
                    if (getref != NULL && has_len)
                    {
                        /* Screen POD-scalar elements HERE, before desugaring.
                           Otherwise the synthesized get_ref() call trips the
                           aggregate-only borrow-return rule inside the
                           CONTAINER's source: the user gets the template's
                           `return self.data[i]` (a stdlib line they never
                           wrote) plus a cascaded "undefined variable" for the
                           loop binder.  find_method_ensured only registers the
                           instance's methods -- it does not body-check them --
                           so the signature is available this early.
                           Scalars are whitelisted rather than aggregates
                           blacklisted: an element type that is still abstract
                           takes the normal path instead of being rejected. */
                        Type *elem = NULL;
                        if (getref->kind == TYPE_FUNCTION &&
                            getref->as.function.return_type != NULL &&
                            getref->as.function.return_type->kind == TYPE_REFERENCE)
                            elem = getref->as.function.return_type->as.pointer_to;
                        if (elem != NULL && (type_is_numeric(elem) ||
                                             elem->kind == TYPE_BOOL ||
                                             elem->kind == TYPE_CHAR))
                        {
                            checker_error(c, node->line, node->column,
                                "cannot borrow elements of '%s': it yields '&%s', and "
                                "element borrows are supported for struct/enum elements "
                                "only — drop the '&' and write `for %s in ...` (copying "
                                "a POD element costs nothing)",
                                type_name(ct), type_name(elem), node->as.for_stmt.var);
                            break;
                        }
                        AstNode *d = build_foreach_borrow_desugar(node);
                        node->as.for_stmt.desugared = d;
                        check_stmt(c, d);
                        break;
                    }
                }
                /* not a borrowable container — fall through; check_expr below
                   re-runs on the borrow and emits the "cannot iterate" error. */
            }
        }

        Type *iter = check_expr(c, node->as.for_stmt.iter);

        /* Iterator-protocol path: a struct that exposes iter()->I or is itself an
           iterator (has next()) is desugared into the equivalent while/match loop
           and that subtree is checked instead (docs/plan_userdef_for_in.md). */
        if (iter != NULL && iter->kind == TYPE_STRUCT)
        {
            /* find_method_ensured (not find_method) so an imported generic
               instance — e.g. Vec(FieldInfo) from a generic type's reflect() —
               has its iter()/next() registered on demand (VR-LIM-018), letting
               `for x in genericThing.method()` work, not just direct calls. */
            bool has_iter = find_method_ensured(c, iter, "iter") != NULL;
            bool has_next = find_method_ensured(c, iter, "next") != NULL;
            if (has_iter || has_next)
            {
                bool src_is_ident = (node->as.for_stmt.iter->kind == AST_IDENT);
                AstNode *d = build_foreach_desugar(node, has_iter, src_is_ident);
                node->as.for_stmt.desugared = d;
                check_stmt(c, d);
                break;
            }
            /* fall through to the generic "not iterable" error below */
        }

        chk_push_scope(c);
        if (iter != NULL)
        {
            if (node->as.for_stmt.iter->kind == AST_RANGE)
            {
                /* Range iteration: loop variable is int */
                scope_define(c->current_scope, node->as.for_stmt.var, type_int());
            }
            else if (iter->kind == TYPE_ARRAY)
            {
                /* Array iteration: loop variable is element type */
                scope_define(c->current_scope, node->as.for_stmt.var, iter->as.array.elem);
            }
            else if (iter->kind == TYPE_SLICE)
            {
                /* Slice iteration: loop variable is the element type. */
                scope_define(c->current_scope, node->as.for_stmt.var, iter->as.array.elem);
            }
            else if (type_is_integer(iter))
            {
                /* Single integer: iterate 0..n */
                scope_define(c->current_scope, node->as.for_stmt.var, type_int());
            }
            else
            {
                checker_error(c, node->as.for_stmt.iter->line,
                              node->as.for_stmt.iter->column,
                              "cannot iterate over '%s'; expected range (a..b), array, "
                              "integer, or a type with an iter()->Iterator(T) / next()->Option(T) method",
                              type_name(iter));
            }
        }
        /* Phase B: 2-pass analysis for the foreach body */
        {
            MoveSnapshot before_loop;
            move_snap_capture(c, &before_loop);

            bool saved_silent = c->silent_move_errors;
            c->silent_move_errors = true;
            check_stmt(c, node->as.for_stmt.body);
            c->silent_move_errors = saved_silent;

            MoveSnapshot after_pass1;
            move_snap_capture(c, &after_pass1);

            move_snap_restore(&before_loop);
            move_preseed_maybe_from_pass1(&before_loop, &after_pass1);

            check_stmt(c, node->as.for_stmt.body);

            move_elevate_moves_to_maybe(&before_loop);

            move_snap_free(&before_loop);
            move_snap_free(&after_pass1);
        }
        chk_pop_scope(c);
        break;
    }

    case AST_FOR_C:
    {
        /* C-style for: for (init; cond; update) { body }
           All three clauses are optional. */
        chk_push_scope(c);
        if (node->as.for_c_stmt.init)
        {
            check_stmt(c, node->as.for_c_stmt.init);
        }
        if (node->as.for_c_stmt.cond)
        {
            Type *cond = check_expr(c, node->as.for_c_stmt.cond);
            if (cond != NULL && cond->kind != TYPE_BOOL)
            {
                checker_error(c, node->as.for_c_stmt.cond->line,
                              node->as.for_c_stmt.cond->column,
                              "for condition must be bool, got '%s'", type_name(cond));
            }
        }

        /* Phase B: 2-pass analysis for the body + update (both repeat). */
        {
            MoveSnapshot before_loop;
            move_snap_capture(c, &before_loop);

            bool saved_silent = c->silent_move_errors;
            c->silent_move_errors = true;
            check_stmt(c, node->as.for_c_stmt.body);
            if (node->as.for_c_stmt.update)
                check_stmt(c, node->as.for_c_stmt.update);
            c->silent_move_errors = saved_silent;

            MoveSnapshot after_pass1;
            move_snap_capture(c, &after_pass1);

            move_snap_restore(&before_loop);
            move_preseed_maybe_from_pass1(&before_loop, &after_pass1);

            check_stmt(c, node->as.for_c_stmt.body);
            if (node->as.for_c_stmt.update)
                check_stmt(c, node->as.for_c_stmt.update);

            move_elevate_moves_to_maybe(&before_loop);

            move_snap_free(&before_loop);
            move_snap_free(&after_pass1);
        }
        chk_pop_scope(c);
        break;
    }

    case AST_BLOCK:
    {
        chk_push_scope(c);
        comptime_expand_block(c, node);  /* Stage 3b: unroll comptime for in place */
        for (int i = 0; i < node->as.block.stmt_count; i++)
        {
            check_stmt(c, node->as.block.stmts[i]);
        }
        chk_pop_scope(c);
        break;
    }

    case AST_EXPR_STMT:
        check_expr(c, node->as.expr_stmt.expr);
        break;

    case AST_BREAK:
    case AST_CONTINUE:
        break;

    case AST_COMPTIME_FOR:
        /* Step 2: comptime_expand_block unrolls these in place before the block's
           statements are checked, so reaching here means a comptime for appeared
           outside an expandable statement position. */
        checker_error(c, node->line, node->column,
            "comptime for must appear at statement position inside a block");
        break;
    case AST_COMPTIME_IF:
        /* comptime_expand_block evaluates these in place before a block's
           statements are checked; reaching here means a misplaced comptime if. */
        checker_error(c, node->line, node->column,
            "comptime if must appear at statement position inside a block");
        break;
    case AST_COMPTIME_MATCH:
        /* comptime_expand_block expands these into a real match before a block's
           statements are checked; reaching here means a misplaced comptime match. */
        checker_error(c, node->line, node->column,
            "comptime match must appear at statement position inside a block");
        break;

    case AST_COMPTIME_CONST:
    {
        /* docs/plan_comptime_consteval.md: evaluate a comptime constant at compile
           time. Scalars (Steps 2+3) fold into a literal at every use. Arrays (Step 4)
           are rewritten into a normal `array(T,N) X = [lits]` var-decl so codegen
           emits a constant-initialized global (→ .rodata at -O2). */
        Type *dt = resolve_type_node(c, node->as.comptime_const.decl_type,
                                     node->line, node->column);
        if (dt == NULL) break;

        if (dt->kind == TYPE_ARRAY) {
            Type *el = dt->as.array.elem;
            int n = dt->as.array.size;
            bool el_scalar = el && (type_is_numeric(el) || el->kind == TYPE_BOOL || el->kind == TYPE_CHAR);
            if (!el_scalar || n <= 0) {
                checker_error(c, node->line, node->column,
                    "comptime array constant '%s' must have a scalar element type and a "
                    "positive length", node->as.comptime_const.name);
                break;
            }
            AstNode *arhs = node->as.comptime_const.value;
            if (!(arhs && arhs->kind == AST_COMPTIME_BLOCK)) {
                checker_error(c, node->line, node->column,
                    "comptime array constant '%s' requires a `comptime { ... return arr }` "
                    "block", node->as.comptime_const.name);
                break;
            }
            CtEval ev; memset(&ev, 0, sizeof ev); ev.budget = CT_BUDGET_DEFAULT;
            CtScalar dummy;
            CtFlow f = ct_exec_block(c, arhs->as.comptime_block.block, &ev, &dummy);
            int ai = (f == CT_RETURNED && ev.ret_array) ? ct_aenv_find(&ev, ev.ret_array) : -1;
            if (ai < 0) {
                ct_env_free(&ev);
                checker_error(c, node->line, node->column,
                    "comptime array constant '%s': the block must build an array local "
                    "(`array(T,N) t = {}`), fill it, and `return` it",
                    node->as.comptime_const.name);
                break;
            }
            if (ev.alens[ai] != n) {
                int got = ev.alens[ai];
                ct_env_free(&ev);
                checker_error(c, node->line, node->column,
                    "comptime array constant '%s': returned array length %d does not match "
                    "declared length %d", node->as.comptime_const.name, got, n);
                break;
            }
            bool el_float = type_is_float(el);
            TypeNode *elem_tn = node->as.comptime_const.decl_type->as.array.elem;
            CtScalar *arr = ev.arrs[ai];
            AstNode *lit = ast_new(AST_ARRAY_LIT, node->line, node->column);
            lit->as.array_lit.count = n;
            lit->as.array_lit.elements = malloc_safe((size_t)n * sizeof(AstNode *));
            for (int k = 0; k < n; k++) {
                AstNode *e;
                if (el_float) {
                    e = ast_new(AST_FLOAT_LIT, node->line, node->column);
                    e->as.float_lit.value = cts_to_f(&arr[k]);
                } else {
                    e = ast_new(AST_INT_LIT, node->line, node->column);
                    e->as.int_lit.value   = arr[k].i;
                    e->as.int_lit.is_char  = (el->kind == TYPE_CHAR);
                }
                /* Wrap in `(value) as <elem-type>` so the element's static type exactly
                   matches the declared array element type. LS array literals don't
                   coerce to the declared type, and int literals default to 'int' — so
                   an i64/f32/sized-int element array would otherwise mismatch. */
                AstNode *cast = ast_new(AST_CAST, node->line, node->column);
                cast->as.cast.expr = e;
                cast->as.cast.target_type = type_node_clone(elem_tn);
                lit->as.array_lit.elements[k] = cast;
            }
            ct_env_free(&ev);
            /* rewrite this node into a normal array var-decl and re-check it */
            TypeNode *aty = node->as.comptime_const.decl_type;
            char *nm = node->as.comptime_const.name;
            ast_free(node->as.comptime_const.value);
            node->kind = AST_VAR_DECL;
            node->resolved_type = NULL;
            node->as.var_decl.var_type = aty;
            node->as.var_decl.name = nm;
            node->as.var_decl.init = lit;
            node->as.var_decl.is_repl_extern = false;
            check_stmt(c, node);
            break;
        }

        bool is_scalar = type_is_numeric(dt) || dt->kind == TYPE_BOOL || dt->kind == TYPE_CHAR;
        if (!is_scalar) {
            checker_error(c, node->line, node->column,
                "comptime constant '%s': only scalar or array(T,N) types are supported "
                "(got a struct/other type)", node->as.comptime_const.name);
            break;
        }
        AstNode *rhs = node->as.comptime_const.value;
        CtScalar v;
        if (rhs && rhs->kind == AST_COMPTIME_BLOCK) {
            CtEval ev; memset(&ev, 0, sizeof ev);
            ev.budget = CT_BUDGET_DEFAULT;
            CtFlow f = ct_exec_block(c, rhs->as.comptime_block.block, &ev, &v);
            ct_env_free(&ev);
            if (f != CT_RETURNED) {
                checker_error(c, node->line, node->column,
                    "comptime block for '%s' could not be evaluated: it must be a "
                    "compile-time-constant computation ending in `return` (allowed: "
                    "scalar locals, assignments, if/else, bounded `for i in 0..N`)",
                    node->as.comptime_const.name);
                break;
            }
        } else if (!ct_eval_scalar(c, rhs, NULL, &v)) {
            checker_error(c, node->line, node->column,
                "comptime constant '%s' is not a compile-time constant (allowed: "
                "int/float literals, other comptime constants, math.*, and "
                "+ - * / %% bitwise/shift/compare/logical operators)",
                node->as.comptime_const.name);
            break;
        }
        if (!type_is_float(dt) && v.is_float) {
            checker_error(c, node->line, node->column,
                "comptime constant '%s' evaluates to a floating-point value but is "
                "declared as an integer type", node->as.comptime_const.name);
            break;
        }
        node->as.comptime_const.is_global = (c->current_scope->depth == 0);
        node->resolved_type = dt;
        Symbol *s = scope_define(c->current_scope, node->as.comptime_const.name, dt);
        if (s == NULL) {
            checker_error(c, node->line, node->column,
                "'%s' is already defined in this scope", node->as.comptime_const.name);
            break;
        }
        s->is_comptime_const = true;
        if (type_is_float(dt)) { s->ct_is_float = true;  s->ct_f = cts_to_f(&v); s->ct_i = 0; }
        else if (dt->kind == TYPE_BOOL) { s->ct_is_float = false; s->ct_i = (v.i != 0) ? 1 : 0; }
        else { s->ct_is_float = false; s->ct_i = v.i; }
        break;
    }

    default:
        /* Declarations or expressions — dispatch */
        check_decl(c, node);
        break;
    }
}
