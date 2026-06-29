/* checker_borrow.c
   借用 / 移动 / 逃逸分析 + 闭包捕获分析（move 快照、borrow 拒绝、capture_walk）

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
static AstNode *borrow_call_provenance_node(AstNode *call);
static bool cap_already(CaptureScan *s, const char *name);
static bool cap_is_bound(CaptureScan *s, const char *name);
static void cap_record(CaptureScan *s, AstNode *site, const char *name, Type *t);
static bool capture_type_is_by_move(const Type *t);
static bool capture_type_is_pod(const Type *t);
static bool capture_type_is_pod_array(const Type *t);
static bool capture_type_supported(const Type *t);
static void capture_walk_arms(CaptureScan *s, AstNode *node);
static bool checker_reject_borrow_move(Checker *c, AstNode *arg, const char *what);
static bool fn_decl_borrow_return_eligible(AstNode *fn);
static void move_snap_init(MoveSnapshot *snap);
static void move_snap_push(MoveSnapshot *snap, Symbol *sym);

/* Returns true if a type requires move tracking (has heap ownership). */
bool type_is_movable(Type *t)
{
    if (!t) return false;
    switch (t->kind)
    {
    case TYPE_STRUCT: return t->as.strukt.has_drop;
    case TYPE_BLOCK:  return true;  /* F.2: Block owns its env heap */
    default:          return false;
    }
}

/* Attempt to mark an IDENT arg as MOVED for any movable type
   (string, vec, map, struct-with-drop — see type_is_movable).
   - Non-IDENT nodes (temporaries, literals, field accesses) are silently skipped.
   - Already-moved variables are skipped (error already reported by check_expr).
   Call AFTER check_expr() has been called on the arg so that:
     (a) type info is resolved, and
     (b) "use of moved variable" is already reported if applicable. */
void checker_try_mark_moved(Checker *c, AstNode *arg)
{
    (void)c;
    if (!arg || arg->kind != AST_IDENT) return;
    Symbol *sym = scope_resolve(c->current_scope, arg->as.ident.name);
    if (!sym || !sym->type) return;
    if (!type_is_movable(sym->type)) return;
    /* Already moved/maybe-moved → error was already reported by check_expr AST_IDENT */
    if (sym->is_moved || sym->is_maybe_moved) return;
    /* Borrow parameters (&T / &!T) hold no ownership — never marked moved.
       Move-site rejection is surfaced separately by checker_reject_borrow_move. */
    if (sym->is_borrow || sym->is_mut_borrow) return;
    /* Phase 1 (borrow extension): the referent of a live named local borrow
       cannot be moved out — that would dangle the borrow. Reject instead of
       marking moved (conservative: the pin is never lifted within the function). */
    if (sym->is_borrow_src)
    {
        checker_move_error(c, arg->line, arg->column,
            "cannot move '%s': it is borrowed by a live local borrow",
            arg->as.ident.name);
        return;
    }
    sym->is_moved = true;
    /* Move-elision (Q4): record on the node that this use transferred ownership,
       so codegen can move (not clone) the heap and invalidate the source.
       Only reached for owned, non-borrow, non-static-string movable IDENTs. */
    arg->moved_out = true;
}

/* Phase 5: reject attempts to move a borrowed variable (e.g. vec.push(s)
   where s is an &string parameter). Called BEFORE the generic
   checker_try_mark_moved at known move sites. Returns true on error. */
/* Phase 5.5: reject using a writable borrow as the source of a move-style
   copy (e.g. `string t = s` where s is a &!string parameter). Read-only
   borrows (&T) are not rejected here — their ABI guarantees cap==0 on the
   callee side, so a subsequent `string t = s` produces a harmless alias.
   Returns true if an error was reported. */
bool checker_reject_mut_borrow_copy_source(Checker *c, AstNode *src, const char *what)
{
    if (!src || src->kind != AST_IDENT) return false;
    Symbol *sym = scope_resolve(c->current_scope, src->as.ident.name);
    if (!sym || !sym->is_mut_borrow) return false;
    checker_move_error(c, src->line, src->column,
                       "cannot %s: '%s' is a writable borrow — content cannot leave",
                       what, src->as.ident.name);
    return true;
}

/* Phase 5.8: reject copy-out of a struct borrow (both &struct and &!struct).
   Same rationale: an owned struct value would be a memcpy alias to caller's
   storage; if it had drop-fields they would double-free, and even POD copies
   could violate the read-only contract. */
bool checker_reject_struct_borrow_copy_source(Checker *c, AstNode *src, const char *what)
{
    if (!src || src->kind != AST_IDENT) return false;
    Symbol *sym = scope_resolve(c->current_scope, src->as.ident.name);
    if (!sym) return false;
    if (!sym->type || sym->type->kind != TYPE_STRUCT) return false;
    if (!sym->is_borrow && !sym->is_mut_borrow) return false;
    checker_move_error(c, src->line, src->column,
                       "cannot %s: '%s' is a %sborrow of struct — value cannot be copied out",
                       what, src->as.ident.name,
                       sym->is_mut_borrow ? "writable " : "read-only ");
    return true;
}

/* Phase 1 (borrow extension, docs/plan_borrow_extension.md §3): check a named
   local borrow declaration `&T r = &x` / `&!T r = &!x` / `&T r2 = r1` (re-borrow).
   `declared` is the resolved TYPE_REFERENCE. The borrow local is registered with
   the pointee type + is_borrow/is_mut_borrow (same convention as a borrow
   parameter), so all existing escape rejections (copy-out, capture) apply
   automatically. The unique new soundness concern — the referent lives in THIS
   function and could be moved out from under the borrow — is closed by marking
   the owned source `is_borrow_src` (checker_try_mark_moved then rejects moving
   it). Borrows do not escape: return is blocked by Phase 0 (`-> &T` rejected)
   and by struct-borrow copy-out; field/payload storage by Phase 0 + copy-out. */
void check_local_borrow_decl(Checker *c, AstNode *node, Type *declared)
{
    const char *vname = node->as.var_decl.name;
    Type *pointee = declared->as.pointer_to;
    bool want_mut = declared->is_mut;

    if (pointee == NULL) return;  /* resolve_type_node already reported */

    /* Phase 1 restriction: struct pointees only. The struct borrow-copy-out
       machinery (checker_reject_struct_borrow_copy_source) is mature; enum local
       borrows are deferred to keep the spike provably sound. */
    if (pointee->kind != TYPE_STRUCT)
    {
        checker_error(c, node->line, node->column,
            "named local borrow '%s': only &struct / &!struct are supported yet "
            "(got &%s%s)", vname, want_mut ? "!" : "", type_name(pointee));
        return;
    }

    AstNode *init = node->as.var_decl.init;
    if (init == NULL)
    {
        checker_error(c, node->line, node->column,
            "named local borrow '%s' must be initialized (e.g. `&%sFoo %s = &%sx`)",
            vname, want_mut ? "!" : "", vname, want_mut ? "!" : "");
        return;
    }

    /* Type-check the initializer (also reports use-of-moved on the source). */
    (void)check_expr(c, init);

    /* Phase 2 (borrow extension): the initializer is a borrow-returning call
       `recv.method()` whose result is &T (single-input lifetime elision). The
       result borrows the receiver, so pin the receiver's root (it must outlive
       the borrow). Register the local with the call's pointee type. */
    if (init->resolved_type && init->resolved_type->kind == TYPE_REFERENCE &&
        init->kind == AST_CALL)
    {
        Type *rt = init->resolved_type;
        if (!type_equals(rt->as.pointer_to, pointee))
        {
            checker_error(c, node->line, node->column,
                "named local borrow '%s': call returns '%s', expected '&%s%s'",
                vname, type_name(rt), want_mut ? "!" : "", type_name(pointee));
            return;
        }
        if (want_mut && !rt->is_mut)
        {
            checker_error(c, node->line, node->column,
                "named writable borrow '%s' cannot bind a read-only borrow result",
                vname);
            return;
        }
        /* The result borrows its provenance (method receiver / borrow argument),
           which must be a STABLE named place that outlives the borrow. A
           temporary provenance (`make().get()`) would be dropped at statement
           end, dangling the bound borrow — reject it (immediate use
           `make().get().field` is fine; only binding escapes). */
        AstNode *prov = borrow_call_provenance_node(init);
        Symbol *rsym = prov ? checker_place_root_symbol(c, prov) : NULL;
        if (rsym == NULL)
        {
            checker_error(c, node->line, node->column,
                "named local borrow '%s': cannot bind a borrow whose source is "
                "a temporary (it would dangle); borrow from a named variable, "
                "or use the result immediately", vname);
            return;
        }
        if (rsym->is_moved || rsym->is_maybe_moved)
        {
            checker_error(c, node->line, node->column,
                "named local borrow '%s': receiver '%s' has been moved",
                vname, rsym->name);
            return;
        }
        if (scope_resolve_local(c->current_scope, vname))
        {
            checker_error(c, node->line, node->column,
                "variable '%s' already defined in this scope", vname);
            return;
        }
        Symbol *bsym = scope_define(c->current_scope, vname, pointee);
        if (bsym)
        {
            if (want_mut) bsym->is_mut_borrow = true;
            else          bsym->is_borrow = true;
        }
        /* Pin the receiver root (the borrow's provenance). */
        if (!rsym->is_borrow && !rsym->is_mut_borrow)
            rsym->is_borrow_src = true;
        return;
    }

    /* Identify the source IDENT. Accepted initializer shapes:
       - `&x`  → AST_UNARY(TOKEN_AMP, IDENT)   (read-only borrow of owned/borrow)
       - `&!x` → AST_MUT_BORROW(IDENT)         (writable borrow of owned struct)
       - `r1`  → AST_IDENT that is itself a borrow (re-borrow). */
    AstNode *src_ident = NULL;
    bool init_via_amp = false;       /* `&x`  read-only address-of */
    bool init_via_mutamp = false;    /* `&!x` writable borrow */
    if (init->kind == AST_UNARY && init->as.unary.op == TOKEN_AMP &&
        init->as.unary.operand && init->as.unary.operand->kind == AST_IDENT)
    {
        src_ident = init->as.unary.operand;
        init_via_amp = true;
    }
    else if (init->kind == AST_MUT_BORROW &&
             init->as.mut_borrow.operand &&
             init->as.mut_borrow.operand->kind == AST_IDENT)
    {
        src_ident = init->as.mut_borrow.operand;
        init_via_mutamp = true;
    }
    else if (init->kind == AST_IDENT)
    {
        src_ident = init;  /* candidate re-borrow */
    }
    else
    {
        checker_error(c, node->line, node->column,
            "named local borrow '%s' must be initialized from `&var` / `&!var` "
            "or another borrow variable", vname);
        return;
    }

    Symbol *src = scope_resolve(c->current_scope, src_ident->as.ident.name);
    if (src == NULL)
    {
        checker_error(c, src_ident->line, src_ident->column,
            "borrow source '%s' is not defined", src_ident->as.ident.name);
        return;
    }
    bool src_is_borrow = src->is_borrow || src->is_mut_borrow;

    if (src->is_moved || src->is_maybe_moved)
    {
        checker_error(c, src_ident->line, src_ident->column,
            "cannot borrow '%s': it has been moved", src_ident->as.ident.name);
        return;
    }

    /* A bare IDENT initializer is only valid as a re-borrow (source already a
       borrow). Borrowing an owned variable must be explicit (`&x` / `&!x`) so the
       intent — and the read-only vs writable kind — is visible. */
    if (!init_via_amp && !init_via_mutamp && !src_is_borrow)
    {
        checker_error(c, node->line, node->column,
            "named local borrow '%s': use `&%s` to borrow owned variable '%s'",
            vname, src_ident->as.ident.name, src_ident->as.ident.name);
        return;
    }

    /* Pointee type must match the source's effective type. */
    if (src->type == NULL || !type_equals(src->type, pointee))
    {
        checker_error(c, node->line, node->column,
            "named local borrow '%s': source '%s' has type '%s', expected '%s'",
            vname, src_ident->as.ident.name,
            src->type ? type_name(src->type) : "?", type_name(pointee));
        return;
    }

    /* Writable borrow requires a writable source and an explicit `&!`. */
    if (want_mut)
    {
        if (src->is_borrow)
        {
            checker_error(c, node->line, node->column,
                "cannot take writable borrow '%s' of read-only borrow '%s'",
                vname, src_ident->as.ident.name);
            return;
        }
        if (init_via_amp)
        {
            checker_error(c, node->line, node->column,
                "named writable borrow '%s' must use `&!%s`, not `&%s`",
                vname, src_ident->as.ident.name, src_ident->as.ident.name);
            return;
        }
    }

    /* Register the borrow local (effective type = pointee). */
    if (scope_resolve_local(c->current_scope, vname))
    {
        checker_error(c, node->line, node->column,
            "variable '%s' already defined in this scope", vname);
        return;
    }
    Symbol *bsym = scope_define(c->current_scope, vname, pointee);
    if (bsym)
    {
        if (want_mut) bsym->is_mut_borrow = true;
        else          bsym->is_borrow = true;
    }
    /* Pin the owned referent: it must not be moved while the borrow is alive.
       A re-borrow's source is already a non-movable borrow, so nothing to mark. */
    if (!src_is_borrow)
        src->is_borrow_src = true;
}

/* Slice local binding `&array(T) s = v[a..b]`: register the slice local (a
   {ptr,len} value) and pin the source the view borrows, so it cannot be moved
   while the slice is alive. Slice locals cannot escape (return / struct field /
   enum payload / capture are all rejected by type), so no further marking is
   needed. `declared` is the resolved TYPE_SLICE. */
void check_local_slice_decl(Checker *c, AstNode *node, Type *declared)
{
    const char *vname = node->as.var_decl.name;
    AstNode *init = node->as.var_decl.init;
    if (init == NULL)
    {
        checker_error(c, node->line, node->column,
            "slice local '%s' must be initialized (e.g. `&array(T) %s = v[a..b]`)",
            vname, vname);
        return;
    }
    Type *it = check_expr(c, init);
    if (it == NULL)
        return;
    if (it->kind != TYPE_SLICE ||
        !type_equals(it->as.array.elem, declared->as.array.elem))
    {
        checker_error(c, node->line, node->column,
            "cannot initialize slice '%s' (type '%s') with value of type '%s'",
            vname, type_name(declared), type_name(it));
        return;
    }
    /* A writable slice `&!array(T) s = v[a..b]` may be created from a fresh slice
       expression over a MUTABLE source (the {ptr,len} value is identical; `&!`
       is a checker-level write permission). Binding `&!` from an existing
       read-only slice value is not allowed. */
    bool fresh_creation = (init->kind == AST_INDEX &&
                           init->as.index_expr.index &&
                           init->as.index_expr.index->kind == AST_RANGE);
    if (declared->is_mut && !it->is_mut && !fresh_creation)
    {
        checker_error(c, node->line, node->column,
            "writable slice '%s' cannot bind a read-only slice", vname);
        return;
    }
    /* The view borrows a contiguous source; pin its root so it outlives the
       slice. A temporary source (`make_vec()[a..b]`) has no root → reject. */
    Symbol *src = checker_place_root_symbol(c, init);
    if (src == NULL)
    {
        checker_error(c, node->line, node->column,
            "slice local '%s': cannot bind a view of a temporary (it would "
            "dangle); slice a named variable", vname);
        return;
    }
    /* A writable slice requires a writable source: an owned local or an `&!`
       borrow — never a read-only `&` borrow. */
    if (declared->is_mut && src->is_borrow)
    {
        checker_error(c, node->line, node->column,
            "writable slice '%s' needs a writable source; '%s' is a read-only "
            "borrow", vname, src->name);
        return;
    }
    if (src->is_moved || src->is_maybe_moved)
    {
        checker_error(c, node->line, node->column,
            "slice local '%s': source '%s' has been moved", vname, src->name);
        return;
    }
    if (scope_resolve_local(c->current_scope, vname))
    {
        checker_error(c, node->line, node->column,
            "variable '%s' already defined in this scope", vname);
        return;
    }
    scope_define(c->current_scope, vname, declared);
    if (!src->is_borrow && !src->is_mut_borrow)
        src->is_borrow_src = true;
}

/* Phase 2 (borrow extension): given a borrow-returning call, return the AST
   expression whose place the result borrows — the method receiver (`o` in
   `o.get()`) or the single borrow argument (the arg passed to the one `&U`
   parameter of an eligible free function). Returns NULL if it can't be
   determined (conservatively treated as a non-place root → rejected). */
static AstNode *borrow_call_provenance_node(AstNode *call)
{
    if (call == NULL || call->kind != AST_CALL)
        return NULL;
    AstNode *callee = call->as.call.callee;
    if (callee == NULL)
        return NULL;
    if (callee->kind == AST_FIELD)
        return callee->as.field_access.object;   /* method receiver */
    /* Free function: the argument at the single reference-param index. */
    Type *fty = callee->resolved_type;
    if (fty && fty->kind == TYPE_FUNCTION)
    {
        for (int i = 0; i < fty->as.function.param_count &&
                        i < call->as.call.arg_count; i++)
        {
            Type *pt = fty->as.function.params[i];
            if (pt && pt->kind == TYPE_REFERENCE)
                return call->as.call.args[i];
        }
    }
    return NULL;
}

/* Phase 2 (borrow extension): walk a place expression (IDENT / field / index /
   &place / &!place chain) to its root symbol. Also recurses through a
   borrow-returning call (`self.child.get()`) via its provenance node, so a
   transitively-chained borrow return is rooted at the original borrow input.
   Returns NULL for non-place expressions (owned-value calls, literals,
   arithmetic). Used to prove a returned borrow derives from a borrow input. */
Symbol *checker_place_root_symbol(Checker *c, AstNode *e)
{
    while (e != NULL)
    {
        switch (e->kind)
        {
        case AST_IDENT:
            return scope_resolve(c->current_scope, e->as.ident.name);
        case AST_FIELD:
            e = e->as.field_access.object;
            break;
        case AST_INDEX:
            e = e->as.index_expr.object;
            break;
        case AST_MUT_BORROW:
            e = e->as.mut_borrow.operand;
            break;
        case AST_UNARY:
            if (e->as.unary.op == TOKEN_AMP) { e = e->as.unary.operand; break; }
            return NULL;
        case AST_CALL:
            /* A borrow/slice-returning call: recurse into what it borrows. A call
               that returns an OWNED value (resolved_type not a reference/slice) is
               a fresh temporary — not a place — so stop (NULL). */
            if (e->resolved_type && (e->resolved_type->kind == TYPE_REFERENCE ||
                                     e->resolved_type->kind == TYPE_SLICE))
            {
                e = borrow_call_provenance_node(e);
                break;
            }
            return NULL;
        default:
            return NULL;
        }
    }
    return NULL;
}

/* F.2: reject moving a Block parameter (is_borrow=true).
   Block pass-by-value is a shallow copy: both caller and callee share env_ptr.
   Moving from a Block param would drop the env the caller still holds → double-free. */
bool checker_reject_block_param_move(Checker *c, AstNode *src, const char *what)
{
    if (!src || src->kind != AST_IDENT) return false;
    Symbol *sym = scope_resolve(c->current_scope, src->as.ident.name);
    if (!sym) return false;
    if (!sym->type || sym->type->kind != TYPE_BLOCK) return false;
    if (!sym->is_borrow) return false;
    checker_move_error(c, src->line, src->column,
                       "cannot %s: '%s' is a Block parameter — call it directly; "
                       "Block parameters cannot be moved (they share env with the caller)",
                       what, src->as.ident.name);
    return true;
}

static bool checker_reject_borrow_move(Checker *c, AstNode *arg, const char *what)
{
    if (!arg || arg->kind != AST_IDENT) return false;
    Symbol *sym = scope_resolve(c->current_scope, arg->as.ident.name);
    if (!sym) return false;
    if (sym->is_borrow) {
        checker_move_error(c, arg->line, arg->column,
                           "cannot %s: variable '%s' is a read-only borrow",
                           what, arg->as.ident.name);
        return true;
    }
    if (sym->is_mut_borrow) {
        checker_move_error(c, arg->line, arg->column,
                           "cannot %s: variable '%s' is a writable borrow "
                           "(mutation allowed, but ownership cannot leave)",
                           what, arg->as.ident.name);
        return true;
    }
    return false;
}

static void move_snap_init(MoveSnapshot *snap) {
    snap->entries = NULL;
    snap->count = 0;
    snap->capacity = 0;
}

void move_snap_free(MoveSnapshot *snap) {
    free(snap->entries);
    snap->entries = NULL;
    snap->count = 0;
    snap->capacity = 0;
}

static void move_snap_push(MoveSnapshot *snap, Symbol *sym) {
    if (snap->count >= snap->capacity) {
        snap->capacity = GROW_CAPACITY(snap->capacity);
        snap->entries = realloc_safe(snap->entries,
                                     (size_t)snap->capacity * sizeof(MoveSnapEntry));
    }
    snap->entries[snap->count].sym = sym;
    snap->entries[snap->count].is_moved = sym->is_moved;
    snap->entries[snap->count].is_maybe_moved = sym->is_maybe_moved;
    snap->count++;
}

/* Capture the current move state of every movable symbol in the scope chain
   starting at c->current_scope (walks up through parents). */
void move_snap_capture(Checker *c, MoveSnapshot *snap) {
    move_snap_init(snap);
    for (Scope *s = c->current_scope; s != NULL; s = s->parent) {
        for (int i = 0; i < s->count; i++) {
            Symbol *sym = &s->symbols[i];
            if (type_is_movable(sym->type))
                move_snap_push(snap, sym);
        }
    }
}

/* Write a snapshot back to its symbols (restore). */
void move_snap_restore(const MoveSnapshot *snap) {
    for (int i = 0; i < snap->count; i++) {
        snap->entries[i].sym->is_moved = snap->entries[i].is_moved;
        snap->entries[i].sym->is_maybe_moved = snap->entries[i].is_maybe_moved;
    }
}

/* Merge two branch-end snapshots into live symbol state (if/else join).
   Rule: for each symbol,
     MOVED in both branches  → MOVED
     MOVED in exactly one    → MAYBE_MOVED
     MAYBE_MOVED in either   → MAYBE_MOVED
     otherwise               → LIVE
   Assumes `a` and `b` describe the same set of symbols (captured from the same
   scope chain). */
void move_snap_merge_into_symbols(const MoveSnapshot *a, const MoveSnapshot *b) {
    for (int i = 0; i < a->count; i++) {
        Symbol *sym = a->entries[i].sym;
        /* Find corresponding entry in b (same ordering usually, but search by pointer
           to be safe — scope chain hasn't changed between captures). */
        bool a_moved = a->entries[i].is_moved;
        bool a_maybe = a->entries[i].is_maybe_moved;
        bool b_moved = false;
        bool b_maybe = false;
        for (int j = 0; j < b->count; j++) {
            if (b->entries[j].sym == sym) {
                b_moved = b->entries[j].is_moved;
                b_maybe = b->entries[j].is_maybe_moved;
                break;
            }
        }
        if (a_moved && b_moved) {
            sym->is_moved = true;
            sym->is_maybe_moved = false;
        } else if (a_moved || b_moved || a_maybe || b_maybe) {
            sym->is_moved = false;
            sym->is_maybe_moved = true;
        } else {
            sym->is_moved = false;
            sym->is_maybe_moved = false;
        }
    }
}

/* Elevate: compare the `before` snapshot against current symbol state; any symbol
   that transitioned LIVE → MOVED is demoted to MAYBE_MOVED. Used for:
     - if-without-else:   then-branch MOVED → MAYBE_MOVED after the if
     - after loop body:   anything moved in the loop becomes MAYBE_MOVED
       (since the loop may execute 0 times).
   MAYBE_MOVED stays MAYBE_MOVED; already-MOVED entries in `before` stay MOVED. */
void move_elevate_moves_to_maybe(const MoveSnapshot *before) {
    for (int i = 0; i < before->count; i++) {
        Symbol *sym = before->entries[i].sym;
        bool was_moved = before->entries[i].is_moved;
        bool was_maybe = before->entries[i].is_maybe_moved;
        if (was_moved) continue; /* already MOVED before — leave alone */
        /* LIVE or MAYBE before */
        if (sym->is_moved) {
            sym->is_moved = false;
            sym->is_maybe_moved = true;
        } else if (sym->is_maybe_moved) {
            /* already MAYBE_MOVED — leave */
        } else if (was_maybe) {
            sym->is_maybe_moved = true;
        }
    }
}

/* For 2-pass loops: given the "before loop" snapshot and the "after pass-1"
   snapshot, pre-seed MAYBE_MOVED on any symbol whose state worsened during
   pass 1. Then pass 2 will correctly report errors. */
void move_preseed_maybe_from_pass1(const MoveSnapshot *before,
                                          const MoveSnapshot *after_pass1) {
    for (int i = 0; i < before->count; i++) {
        Symbol *sym = before->entries[i].sym;
        bool before_moved = before->entries[i].is_moved;
        bool before_maybe = before->entries[i].is_maybe_moved;
        if (before_moved) continue; /* already MOVED — nothing worse possible */
        /* find matching entry in after_pass1 */
        for (int j = 0; j < after_pass1->count; j++) {
            if (after_pass1->entries[j].sym != sym) continue;
            bool p1_moved = after_pass1->entries[j].is_moved;
            bool p1_maybe = after_pass1->entries[j].is_maybe_moved;
            if (p1_moved || (p1_maybe && !before_maybe)) {
                sym->is_moved = false;
                sym->is_maybe_moved = true;
            }
            break;
        }
    }
}

static bool cap_is_bound(CaptureScan *s, const char *name) {
    for (int i = 0; i < s->bound_count; i++)
        if (strcmp(s->bound[i], name) == 0) return true;
    return false;
}

static bool cap_already(CaptureScan *s, const char *name) {
    for (int i = 0; i < s->capture_count; i++)
        if (strcmp(s->captures[i].name, name) == 0) return true;
    return false;
}

void cap_push_bound(CaptureScan *s, const char *name) {
    if (name == NULL) return;
    if (s->bound_count >= s->bound_cap) {
        s->bound_cap = GROW_CAPACITY(s->bound_cap);
        s->bound = (const char **)realloc_safe(
            (void*)s->bound, sizeof(const char *) * (size_t)s->bound_cap);
    }
    s->bound[s->bound_count++] = name;
}

/* POD types eligible for by-copy capture (no drop needed). */
static bool capture_type_is_pod(const Type *t) {
    if (t == NULL) return false;
    switch (t->kind) {
    case TYPE_INT: case TYPE_I8: case TYPE_I16: case TYPE_I32: case TYPE_I64:
    case TYPE_U8:  case TYPE_U16: case TYPE_U32: case TYPE_U64:
    case TYPE_F32: case TYPE_F64:
    case TYPE_BOOL: case TYPE_CHAR:
    case TYPE_OBJECT: case TYPE_POINTER:
        return true;
    default:
        return false;
    }
}

/* Phase C.5/C.7+: by-move capture types — env owns the value, outer is
   marked moved.
     C.7: TYPE_STRUCT(has_drop)
   Enum captures remain unsupported (Phase C.8 — needs box / payload
   walk inside env_drop). */
static bool capture_type_is_by_move(const Type *t) {
    if (t == NULL) return false;
    switch (t->kind) {
    case TYPE_STRUCT: return t->as.strukt.has_drop;
    case TYPE_ENUM:   return t->as.enom.has_drop;   /* F.5: has_drop enum → by-move */
    default:          return false;
    }
}

/* array(T, N) with POD element type is captured by value (full copy into env).
   String-element or struct-element arrays would need deep clone — reject for now. */
static bool capture_type_is_pod_array(const Type *t) {
    if (t == NULL || t->kind != TYPE_ARRAY) return false;
    return capture_type_is_pod(t->as.array.elem);
}

static bool capture_type_supported(const Type *t) {
    if (t == NULL) return false;
    /* F.5: non-has_drop enum (disc-only or POD payloads) is by-copy — no drop. */
    if (t->kind == TYPE_ENUM && !t->as.enom.has_drop) return true;
    /* Closure-foundation Phase A: capture another Block by-clone (deep-copy its
       env into this closure's env; source Block stays live). Deliberately NOT
       added to capture_type_is_by_move — the source is not moved, so it does not
       go through the by-move outer-mark / borrow-reject path below; it falls into
       the plain-record path like POD. See docs/plan_closure_foundation.md §2.3. */
    if (t->kind == TYPE_BLOCK) return true;
    return capture_type_is_pod(t) ||
           capture_type_is_pod_array(t) ||
           capture_type_is_by_move(t);
}

static void cap_record(CaptureScan *s, AstNode *site, const char *name, Type *t) {
    /* Closure-foundation Phase B: transitive by-move capture is rejected in v1.
       When nested_depth>0 we are recording a free variable referenced from inside
       a nested closure that resolves to a function-scope symbol BEYOND the
       enclosing closure (cap_record at depth>0 is only reached for names not bound
       within the enclosing closure — its params/locals are in `bound` and short-
       circuit in AST_IDENT). Propagating such a variable means threading its
       ownership through MULTIPLE env layers (outer env → inner env), a transfer
       chain that is error-prone (double-free) and deferred to v2. Only by-copy
       (POD) and by-clone (Block, capture_type_is_by_move==false) may cross a
       closure layer. Check the TYPE before cap_already so the both-referenced
       case (a by-move var used directly in the outer body AND in a nested closure)
       is still caught rather than silently double-moved.
       See docs/plan_closure_foundation.md §4.4. */
    if (s->nested_depth > 0 && capture_type_is_by_move(t)) {
        checker_move_error(s->c, site->line, site->column,
            "cannot capture '%s' (type '%s') from a nested closure: transitive "
            "by-move capture across closure layers is not yet supported (v1). "
            "Only POD (by-copy) and Block (by-clone) variables may be referenced "
            "from an inner closure across the enclosing closure. Workaround: pass "
            "it in as a closure parameter, or restructure to avoid the nesting.",
            name, type_name(t));
        s->had_error = true;
        return;
    }
    if (cap_already(s, name)) return;
    if (!capture_type_supported(t)) {
        checker_error(s->c, site->line, site->column,
                      "capturing variable '%s' of type '%s' in a closure is "
                      "not yet implemented (supported: POD types, "
                      "array(POD,N) (by-copy), string (by-move), "
                      "vec(T)/map(K,V) (by-ref), struct(has_drop) (by-move), "
                      "enum (by-copy or by-move depending on has_drop))",
                      name, type_name(t));
        s->had_error = true;
        return;
    }
    /* Phase C.5: by-move capture (string in v1) transfers ownership from
       the outer variable into the closure's env. Run the same outer-symbol
       checks vec.push uses, then mark the outer as moved so subsequent
       outer uses are caught at compile time. Borrows are silently rejected
       (closure cannot promise a borrow lives long enough yet). */
    if (capture_type_is_by_move(t)) {
        Symbol *outer = scope_resolve(s->outer_scope, name);
        if (outer == NULL) {
            /* Defensive — capture_walk only records if outer-scope hit.   */
            return;
        }
        if (outer->is_borrow || outer->is_mut_borrow) {
            checker_move_error(s->c, site->line, site->column,
                "cannot capture '%s' by-move into a closure: it is a "
                "borrow parameter (no transferable ownership)", name);
            s->had_error = true;
            return;
        }
        /* Phase 1 (borrow extension): moving the referent of a live named local
           borrow into a closure env would dangle the borrow once the closure
           (and its env) outlives or drops before the borrow's last use. The
           generic move site goes through checker_try_mark_moved; closure
           by-move capture is a separate path, so guard it here too. */
        if (outer->is_borrow_src) {
            checker_move_error(s->c, site->line, site->column,
                "cannot capture '%s' by-move into a closure: it is borrowed by "
                "a live local borrow", name);
            s->had_error = true;
            return;
        }
        if (outer->is_moved || outer->is_maybe_moved) {
            checker_move_error(s->c, site->line, site->column,
                "cannot capture '%s' by-move: already moved on a prior path",
                name);
            s->had_error = true;
            return;
        }
        outer->is_moved = true;
    }
    if (s->capture_count >= s->capture_cap) {
        s->capture_cap = GROW_CAPACITY(s->capture_cap);
        s->captures = realloc_safe(s->captures,
            sizeof(s->captures[0]) * (size_t)s->capture_cap);
    }
    /* deep-copy name so AST ownership is self-contained */
    size_t nl = strlen(name);
    char *dup = (char*)malloc_safe(nl + 1);
    memcpy(dup, name, nl + 1);
    s->captures[s->capture_count].name = dup;
    s->captures[s->capture_count].type = t;
    s->captures[s->capture_count].is_explicit_move = false;
    s->capture_count++;
}

static void capture_walk_arms(CaptureScan *s, AstNode *node) {
    /* MATCH arms each introduce a fresh binding scope. */
    int n = node->as.match.arm_count;
    capture_walk(s, node->as.match.subject);
    for (int i = 0; i < n; i++) {
        int saved = s->bound_count;
        /* Pattern may bind names (variant constructors with payloads).
           For POD-only v1 we conservatively treat AST_IDENT in pattern
           position as a binding. */
        AstNode *pat = node->as.match.arms[i].pattern;
        if (pat && pat->kind == AST_IDENT) {
            cap_push_bound(s, pat->as.ident.name);
        } else if (pat && pat->kind == AST_CALL) {
            /* Variant ctor with payload binders: each arg ident → binding. */
            for (int k = 0; k < pat->as.call.arg_count; k++) {
                AstNode *a = pat->as.call.args[k];
                if (a && a->kind == AST_IDENT)
                    cap_push_bound(s, a->as.ident.name);
            }
        }
        capture_walk(s, node->as.match.arms[i].body);
        s->bound_count = saved;
    }
}

void capture_walk(CaptureScan *s, AstNode *node) {
    if (node == NULL || s->had_error) return;
    switch (node->kind) {
    case AST_INT_LIT: case AST_FLOAT_LIT: case AST_STRING_LIT:
    case AST_BOOL_LIT: case AST_NIL_LIT: case AST_BREAK: case AST_CONTINUE:
        return;
    case AST_IDENT: {
        const char *name = node->as.ident.name;
        if (cap_is_bound(s, name)) return;
        Symbol *sym = scope_resolve(s->outer_scope, name);
        if (sym == NULL) return;  /* will error in normal type-check pass */
        /* Global functions (TYPE_FUNCTION) are never captured in the env —
           they are accessed directly by the lifted closure body just like
           any top-level name in a normal function. */
        if (sym->type && sym->type->kind == TYPE_FUNCTION) return;
        /* Global variables (scope depth 0) are likewise NOT captured: they have
           a fixed program-lifetime address, so the lifted closure body names
           them directly — exactly like a global function. This is what makes a
           global Atomic/Mutex shareable across worker threads: every closure
           references the SAME global, instead of each capturing a private
           by-copy snapshot (which would be both wrong for shared mutation and,
           for owned globals, a spurious move of the global). */
        if (sym->scope_depth == 0) return;
        cap_record(s, node, name, sym->type);
        return;
    }
    case AST_UNARY:
        capture_walk(s, node->as.unary.operand);
        return;
    case AST_MUT_BORROW:
        capture_walk(s, node->as.mut_borrow.operand);
        return;
    case AST_BINARY:
        /* Operator-overloaded binaries move their operands into binary.lowered
           (left/right become NULL). Walk lowered to reach those operands. */
        if (node->as.binary.lowered)
            capture_walk(s, node->as.binary.lowered);
        else
        {
            capture_walk(s, node->as.binary.left);
            capture_walk(s, node->as.binary.right);
        }
        return;
    case AST_CALL:
        capture_walk(s, node->as.call.callee);
        for (int i = 0; i < node->as.call.arg_count; i++)
            capture_walk(s, node->as.call.args[i]);
        return;
    case AST_INDEX:
        capture_walk(s, node->as.index_expr.object);
        capture_walk(s, node->as.index_expr.index);
        return;
    case AST_FIELD:
        capture_walk(s, node->as.field_access.object);
        return;
    case AST_FORMAT_STRING:
        for (int i = 0; i < node->as.format_string.expr_count; i++)
            capture_walk(s, node->as.format_string.exprs[i]);
        return;
    case AST_ARRAY_LIT:
        for (int i = 0; i < node->as.array_lit.count; i++)
            capture_walk(s, node->as.array_lit.elements[i]);
        return;
    case AST_MAP_LIT:
        for (int i = 0; i < node->as.map_lit.pair_count; i++) {
            capture_walk(s, node->as.map_lit.keys[i]);
            capture_walk(s, node->as.map_lit.vals[i]);
        }
        return;
    case AST_CAST:
        capture_walk(s, node->as.cast.expr);
        return;
    case AST_SIZEOF:
        return; /* operand is a type, no sub-expression to walk */
    case AST_RANGE:
        capture_walk(s, node->as.range.start);
        capture_walk(s, node->as.range.end);
        return;
    case AST_TRY:
        capture_walk(s, node->as.try_expr.expr);
        return;
    case AST_FORCE_UNWRAP:
        capture_walk(s, node->as.force_unwrap.expr);
        capture_walk(s, node->as.force_unwrap.message);  /* C1: expect(msg) */
        return;
    case AST_AT_TIME:
        capture_walk(s, node->as.at_time.expr);
        return;
    case AST_AT_BENCH:
        capture_walk(s, node->as.at_bench.expr);
        return;
    case AST_NEW_EXPR:
        for (int i = 0; i < node->as.new_expr.field_init_count; i++)
            capture_walk(s, node->as.new_expr.field_inits[i].value);
        return;
    case AST_VAR_DECL:
        capture_walk(s, node->as.var_decl.init);
        cap_push_bound(s, node->as.var_decl.name);
        return;
    case AST_ASSIGN:
        capture_walk(s, node->as.assign.target);
        capture_walk(s, node->as.assign.value);
        return;
    case AST_RETURN:
        capture_walk(s, node->as.return_stmt.value);
        return;
    case AST_IF: {
        capture_walk(s, node->as.if_stmt.cond);
        int sv = s->bound_count;
        capture_walk(s, node->as.if_stmt.then_block);
        s->bound_count = sv;
        capture_walk(s, node->as.if_stmt.else_block);
        s->bound_count = sv;
        return;
    }
    case AST_WHILE: {
        capture_walk(s, node->as.while_stmt.cond);
        int sv = s->bound_count;
        capture_walk(s, node->as.while_stmt.body);
        s->bound_count = sv;
        return;
    }
    case AST_FOR: {
        capture_walk(s, node->as.for_stmt.iter);
        int sv = s->bound_count;
        cap_push_bound(s, node->as.for_stmt.var);
        capture_walk(s, node->as.for_stmt.body);
        s->bound_count = sv;
        return;
    }
    case AST_FOR_C: {
        int sv = s->bound_count;
        capture_walk(s, node->as.for_c_stmt.init);  /* may add a var */
        capture_walk(s, node->as.for_c_stmt.cond);
        capture_walk(s, node->as.for_c_stmt.update);
        capture_walk(s, node->as.for_c_stmt.body);
        s->bound_count = sv;
        return;
    }
    case AST_BLOCK: {
        int sv = s->bound_count;
        for (int i = 0; i < node->as.block.stmt_count; i++)
            capture_walk(s, node->as.block.stmts[i]);
        s->bound_count = sv;
        return;
    }
    case AST_EXPR_STMT:
        capture_walk(s, node->as.expr_stmt.expr);
        return;
    case AST_MATCH:
        capture_walk_arms(s, node);
        return;
    case AST_CLOSURE: {
        /* Closure-foundation Phase B: transitive capture propagation.
           An inner closure literal `|x| ...` may reference variables from
           beyond the ENCLOSING closure (function scope). Those must be captured
           by the enclosing closure too (threaded through its env), so the inner
           closure can later resolve them from the enclosing closure's body scope.

           We walk the inner closure's body with the SAME scan (so any beyond-
           closure free variable is recorded onto THIS closure's captures via
           cap_record), seeding `bound` with the inner closure's own parameters
           so they are not mistaken for captures. Names already bound in the
           enclosing closure (its params/locals, tracked in `bound`) short-circuit
           in AST_IDENT and are NOT re-propagated — the inner closure captures
           those directly from the enclosing closure when its own capture scan
           runs later (during the enclosing body's type-check).

           nested_depth marks cap_record calls as transitive so by-move transitive
           captures are rejected (v1 limit, §4.4). The recursion handles arbitrary
           nesting depth uniformly: each enclosing layer propagates the beyond-its-
           scope vars referenced anywhere in its body, including deeper closures. */
        int saved_bound = s->bound_count;
        for (int i = 0; i < node->as.closure.param_count; i++)
            cap_push_bound(s, node->as.closure.param_names[i]);
        s->nested_depth++;
        capture_walk(s, node->as.closure.body);
        s->nested_depth--;
        s->bound_count = saved_bound;
        return;
    }
    default:
        /* Decls / FFI / module nodes — should not appear inside closure body
           in well-formed input; just skip. */
        return;
    }
}

/* Phase 2 (borrow extension): a function may return a borrow (&T) only under
   single-input lifetime elision — there must be exactly ONE borrow input whose
   lifetime the result unambiguously inherits. Covered: an `&self`/`&!self`
   method with no other borrow parameter, OR a free function with exactly one
   `&U`/`&!U` parameter. (Multi-borrow-input elision, generics, and closures are
   deferred.) The body's AST_RETURN then proves the returned place actually
   derives from that input (checker_place_root_symbol). */
static bool fn_decl_borrow_return_eligible(AstNode *fn)
{
    if (fn == NULL || fn->kind != AST_FN_DECL)
        return false;
    int borrow_inputs = (fn->as.fn_decl.self_borrow_kind != 0) ? 1 : 0;
    for (int i = 0; i < fn->as.fn_decl.param_count; i++)
    {
        TypeNode *pt = fn->as.fn_decl.param_types[i];
        if (pt && pt->kind == TYPE_NODE_REFERENCE)
            borrow_inputs++;
    }
    return borrow_inputs == 1;
}

/* Phase 0/2 (borrow extension, docs/plan_borrow_extension.md §3): returning a
   borrow (&T / &!T). Previously (Phase 0) ALL `-> &T` were rejected because the
   checker silently accepted them and codegen emitted invalid IR. Phase 2 carves
   out the sound case: an eligible `&self` method (single-input elision). For any
   other shape (free fn, multi-borrow input, generic instantiation — `fn`==NULL),
   keep rejecting clearly. `ret` is the already-resolved return Type. */
void checker_reject_borrow_return(Checker *c, Type *ret, AstNode *fn,
                                         int line, int col)
{
    /* A slice result (&array(T)) carries a borrowed *T — it may only escape via
       return under single-input lifetime elision (same rule as &T), so the
       result borrows the one input. The body's AST_RETURN proves the returned
       view is rooted at that input. Otherwise it would dangle → reject. */
    if (ret != NULL && ret->kind == TYPE_SLICE)
    {
        if (fn_decl_borrow_return_eligible(fn))
            return;
        checker_error(c, line, col,
                      "cannot return a slice '%s' here: only a function with "
                      "exactly one borrow input (an `&self` method, or a single "
                      "`&T` parameter) may return a borrowed view",
                      type_name(ret));
        return;
    }
    if (ret != NULL && ret->kind == TYPE_REFERENCE)
    {
        if (fn_decl_borrow_return_eligible(fn))
            return;  /* Phase 2 / generic: allowed. Body proves provenance at
                        AST_RETURN, which also screens POD-scalar pointees (only
                        fires when the method is actually instantiated, so an
                        uncalled `get_ref(&self)->&T` on a POD instance is fine). */
        checker_error(c, line, col,
                      "borrows cannot escape via return here: cannot return "
                      "&%s%s (only a function with exactly one borrow input — an "
                      "`&self`/`&!self` method, or a single `&T` parameter — may "
                      "return a borrow)",
                      ret->is_mut ? "!" : "",
                      ret->as.pointer_to ? type_name(ret->as.pointer_to) : "T");
    }
}

/* Phase 0 (borrow extension): a borrow type (&T / &!T) cannot be used as a
   generic type argument — it would let a borrow be stored inside a container /
   Option / Result and outlive its referent (the same dangling landmine as a
   borrow field). Borrows are "function parameters only" (types.h). Reject e.g.
   Option(&Foo), Vec(&Foo). Returns true if it rejected. */
bool checker_reject_borrow_type_arg(Checker *c, Type *arg, const char *base,
                                           int line, int col)
{
    if (arg != NULL && arg->kind == TYPE_REFERENCE)
    {
        checker_error(c, line, col,
                      "a borrow type cannot be a generic type argument: '%s' was "
                      "given &%s%s (borrows are function-parameter-only; use a "
                      "value-offset view instead)",
                      base, arg->is_mut ? "!" : "",
                      arg->as.pointer_to ? type_name(arg->as.pointer_to) : "T");
        return true;
    }
    return false;
}
