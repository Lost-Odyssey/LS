/* checker_call.c — call-expression checking, sub-split of checker_expr.c
   (Batch 7 Task 7.5, docs/plan_arch_round2_backlog.md §7.5 "若体量过大可再分
   checker_call.c"): the check_builtin_* families (task/sync/atomic/simd +
   dispatcher, Task 7.7), the intrinsic registry (intrinsic_lookup), the
   fn-call arg helpers, the check_call_* helpers (Task 3.3), and the
   check_expr_call entry called from check_expr's dispatcher.
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

/* File-local helpers (single-TU). */
static Type *check_builtin_call(Checker *c, const char *name, AstNode *call_node);
static bool check_call_variant_ctor(Checker *c, AstNode *node, Type **out_result);
static bool check_call_static_method(Checker *c, AstNode *node, AstNode **inout_obj_node,
                                      const char *method_name,
                                      bool *out_is_static_call,
                                      const char **out_method_struct);
static bool check_call_interface_qualified(Checker *c, AstNode *node, AstNode **inout_obj_node,
                                            const char *method_name);
static bool check_call_instance_method(Checker *c, AstNode *node,
                                        bool is_method_call, bool is_static_call,
                                        const char *method_struct,
                                        const char *method_name,
                                        Type **out_callee_type);
static bool check_call_generic_free_fn(Checker *c, AstNode *node, Type **out_result);
static Type *check_call_arguments(Checker *c, AstNode *node, Type *callee_type, int self_offset);
static const char *intrinsic_retired_spelling(const char *name);

/* Check builtin function calls that don't belong to a type */
/* __task_spawn / __task_join (structured concurrency). [Task 7.7 split] */
static Type *check_builtin_task(Checker *c, const char *name, AstNode *call_node)
{
    int argc = call_node->as.call.arg_count;
    AstNode **args = call_node->as.call.args;
    (void)argc; (void)args;

    /* __task_spawn(Block()->T, *T box) -> object — GENERIC structured-concurrency
       intrinsic (std.task). Runs the closure on a worker; codegen synthesises a
       per-T thunk that stores the by-value result into the `*T box` slot. T is
       read by codegen from arg0's Block return type; checker only validates the
       arg shapes and returns the opaque handle type. */
    if (strcmp(name, "__task_spawn") == 0)
    {
        if (argc != 2)
        {
            checker_error(c, call_node->line, call_node->column,
                          "__task_spawn() takes exactly 2 arguments, got %d", argc);
            return NULL;
        }
        Type *bt = check_expr(c, args[0]);
        if (bt == NULL) return NULL;
        if (bt->kind != TYPE_BLOCK)
        {
            checker_error(c, args[0]->line, args[0]->column,
                          "__task_spawn() requires a Block argument, got '%s'",
                          type_name(bt));
            return NULL;
        }
        Type *boxt = check_expr(c, args[1]);
        if (boxt == NULL) return NULL;
        if (boxt->kind != TYPE_POINTER)
        {
            checker_error(c, args[1]->line, args[1]->column,
                          "__task_spawn() requires a pointer box argument, got '%s'",
                          type_name(boxt));
            return NULL;
        }
        return type_object();
    }

    /* __task_join(object) -> void — wait for the worker. The result is read out
       of the Task's box by the LS join() via __take; this only joins the thread. */
    if (strcmp(name, "__task_join") == 0)
    {
        if (argc != 1)
        {
            checker_error(c, call_node->line, call_node->column,
                          "__task_join() takes exactly 1 argument, got %d", argc);
            return NULL;
        }
        Type *h = check_expr(c, args[0]);
        if (h == NULL) return NULL;
        return type_void();
    }

    /* Mutex + spin runtime intrinsics (std.sync) — opaque-handle FFI to the OS
       backend. Global intrinsics (like __task_*) so they survive generic-method
       instantiation in a consumer module without an `import std.c` alias. They
       know nothing about Mutex(T): a handle in/out. */

    return NULL;
}

/* __mutex_* / __rwlock_* / __cond_* / __cpu_* (OS lock + relax/yield
   primitives). [Task 7.7 split] */
static Type *check_builtin_sync(Checker *c, const char *name, AstNode *call_node)
{
    int argc = call_node->as.call.arg_count;
    AstNode **args = call_node->as.call.args;
    (void)argc; (void)args;

    if (strncmp(name, "__mutex_", 8) == 0 || strncmp(name, "__rwlock_", 9) == 0 ||
        strncmp(name, "__cond_", 7) == 0 ||
        strcmp(name, "__cpu_relax") == 0 || strcmp(name, "__cpu_yield") == 0)
    {
        int want = 1; /* handle arg */
        Type *ret = type_void();
        if (strcmp(name, "__mutex_init") == 0)         { want = 0; ret = type_object(); }
        else if (strcmp(name, "__mutex_lock") == 0)    { ret = type_int(); }
        else if (strcmp(name, "__mutex_trylock") == 0) { ret = type_int(); }
        else if (strcmp(name, "__mutex_unlock") == 0)  { ret = type_int(); }
        else if (strcmp(name, "__mutex_destroy") == 0) { ret = type_void(); }
        else if (strcmp(name, "__rwlock_init") == 0)     { want = 0; ret = type_object(); }
        else if (strcmp(name, "__rwlock_rdlock") == 0)   { ret = type_int(); }
        else if (strcmp(name, "__rwlock_wrlock") == 0)   { ret = type_int(); }
        else if (strcmp(name, "__rwlock_rdunlock") == 0) { ret = type_int(); }
        else if (strcmp(name, "__rwlock_wrunlock") == 0) { ret = type_int(); }
        else if (strcmp(name, "__rwlock_destroy") == 0)  { ret = type_void(); }
        /* condition variables (std.chan): init()->object, wait(cond,mtx) is the
           only 2-arg sync intrinsic, signal/broadcast/destroy take one handle. */
        else if (strcmp(name, "__cond_init") == 0)      { want = 0; ret = type_object(); }
        else if (strcmp(name, "__cond_wait") == 0)      { want = 2; ret = type_void(); }
        else if (strcmp(name, "__cond_signal") == 0)    { ret = type_void(); }
        else if (strcmp(name, "__cond_broadcast") == 0) { ret = type_void(); }
        else if (strcmp(name, "__cond_destroy") == 0)   { ret = type_void(); }
        else if (strcmp(name, "__cpu_relax") == 0)     { want = 0; ret = type_void(); }
        else if (strcmp(name, "__cpu_yield") == 0)     { want = 0; ret = type_void(); }
        else
        {
            checker_error(c, call_node->line, call_node->column,
                          "unknown sync intrinsic '%s'", name);
            return NULL;
        }
        if (argc != want)
        {
            checker_error(c, call_node->line, call_node->column,
                          "%s() takes %d argument(s), got %d", name, want, argc);
            return NULL;
        }
        for (int i = 0; i < want; i++)
            if (check_expr(c, args[i]) == NULL) return NULL;
        return ret;
    }

    /* Atomic intrinsics (std.atomic) — place-based, SeqCst. arg0 is an lvalue
       place (e.g. self.value); codegen takes its address and emits a single
       inline LLVM atomic instruction. T is the place's scalar type. The
       lock-free-scalar restriction on T is enforced at codegen (a clean
       cg_error pointing users at Mutex for larger types). */

    return NULL;
}

/* __atomic_* (Atomic(T) lock-free scalar ops). [Task 7.7 split] */
static Type *check_builtin_atomic(Checker *c, const char *name, AstNode *call_node)
{
    int argc = call_node->as.call.arg_count;
    AstNode **args = call_node->as.call.args;
    (void)argc; (void)args;

    if (strncmp(name, "__atomic_", 9) == 0)
    {
        if (strcmp(name, "__atomic_fence") == 0)
        {
            if (argc != 0)
            {
                checker_error(c, call_node->line, call_node->column,
                              "__atomic_fence() takes no arguments, got %d", argc);
                return NULL;
            }
            return type_void();
        }
        if (argc < 1)
        {
            checker_error(c, call_node->line, call_node->column,
                          "%s() requires a place expression argument", name);
            return NULL;
        }
        Type *pt = check_expr(c, args[0]);
        if (pt == NULL) return NULL;
        if (args[0]->kind != AST_INDEX && args[0]->kind != AST_FIELD &&
            args[0]->kind != AST_IDENT &&
            !(args[0]->kind == AST_UNARY && args[0]->as.unary.op == TOKEN_STAR))
        {
            checker_error(c, args[0]->line, args[0]->column,
                          "%s() requires a place expression (p[i], field, or *p)", name);
            return NULL;
        }
        for (int i = 1; i < argc; i++)
            if (check_expr(c, args[i]) == NULL) return NULL;

        /* load / load_acquire / load_relaxed — all return the place type */
        if (strncmp(name, "__atomic_load", 13) == 0)
        {
            if (argc != 1) { checker_error(c, call_node->line, call_node->column,
                "%s() takes exactly 1 argument, got %d", name, argc); return NULL; }
            return pt;
        }
        /* store / store_release / store_relaxed — void */
        if (strncmp(name, "__atomic_store", 14) == 0)
        {
            if (argc != 2) { checker_error(c, call_node->line, call_node->column,
                "%s() takes exactly 2 arguments, got %d", name, argc); return NULL; }
            return type_void();
        }
        if (strcmp(name, "__atomic_add") == 0 ||
            strcmp(name, "__atomic_sub") == 0 ||
            strcmp(name, "__atomic_swap") == 0)
        {
            if (argc != 2) { checker_error(c, call_node->line, call_node->column,
                "%s() takes exactly 2 arguments, got %d", name, argc); return NULL; }
            return pt; /* the prior value */
        }
        if (strcmp(name, "__atomic_cas") == 0)
        {
            if (argc != 3) { checker_error(c, call_node->line, call_node->column,
                "__atomic_cas() takes exactly 3 arguments, got %d", argc); return NULL; }
            return type_bool(); /* success */
        }
        checker_error(c, call_node->line, call_node->column,
                      "unknown atomic intrinsic '%s'", name);
        return NULL;
    }

    /* SIMD intrinsics __simd_* — portable vector ops lowering to a single <N x T>
       IR instruction (docs/plan_simd.md §4.2). Producers (zero/splat) take their
       result Simd(T,N) from the expected type (a `Simd(T,N) x = __simd_*(...)`
       context); operand-driven ops derive it from their Simd arguments. */

    return NULL;
}

/* __simd_* (Simd(T,N) vector intrinsics — the largest family). [Task 7.7 split] */
static Type *check_builtin_simd(Checker *c, const char *name, AstNode *call_node)
{
    int argc = call_node->as.call.arg_count;
    AstNode **args = call_node->as.call.args;
    (void)argc; (void)args;

    if (strncmp(name, "__simd_", 7) == 0)
    {
        for (int i = 0; i < argc; i++)
            if (check_expr(c, args[i]) == NULL) return NULL;

        /* __simd_zero() / __simd_splat(scalar) -> Simd(T,N) from expected type */
        if (strcmp(name, "__simd_zero") == 0 || strcmp(name, "__simd_splat") == 0)
        {
            int want = (strcmp(name, "__simd_splat") == 0) ? 1 : 0;
            if (argc != want) { checker_error(c, call_node->line, call_node->column,
                "%s() takes exactly %d argument(s), got %d", name, want, argc); return NULL; }
            Type *exp = c->expected_type;
            if (exp == NULL || exp->kind != TYPE_SIMD) {
                checker_error(c, call_node->line, call_node->column,
                    "%s() result type cannot be inferred here; use it where a "
                    "Simd(T, N) is expected (e.g. `Simd(f32, 16) v = %s(...)`)",
                    name, name);
                return NULL;
            }
            if (want == 1 && args[0]->resolved_type &&
                !type_is_numeric(args[0]->resolved_type)) {
                checker_error(c, args[0]->line, args[0]->column,
                    "__simd_splat() requires a numeric scalar, got '%s'",
                    type_name(args[0]->resolved_type));
                return NULL;
            }
            return type_simd(type_clone(exp->as.simd.elem), exp->as.simd.lanes);
        }

        /* __simd_lane(v, i) -> element type of v ; reductions -> element type.
           reduce_add/reduce_max/reduce_min collapse <N x T> to a scalar T. */
        if (strcmp(name, "__simd_lane") == 0 || strcmp(name, "__simd_reduce_add") == 0 ||
            strcmp(name, "__simd_reduce_max") == 0 || strcmp(name, "__simd_reduce_min") == 0)
        {
            int want = (strcmp(name, "__simd_lane") == 0) ? 2 : 1;
            if (argc != want) { checker_error(c, call_node->line, call_node->column,
                "%s() takes exactly %d argument(s), got %d", name, want, argc); return NULL; }
            Type *vt = args[0]->resolved_type;
            if (vt == NULL || vt->kind != TYPE_SIMD) {
                checker_error(c, args[0]->line, args[0]->column,
                    "%s() requires a Simd value as the first argument", name); return NULL; }
            return type_clone(vt->as.simd.elem);
        }

        /* __simd_fma(a, b, c) -> Simd(T,N) (all three operands must match) */
        if (strcmp(name, "__simd_fma") == 0)
        {
            if (argc != 3) { checker_error(c, call_node->line, call_node->column,
                "__simd_fma() takes exactly 3 arguments, got %d", argc); return NULL; }
            Type *t0 = args[0]->resolved_type;
            if (t0 == NULL || t0->kind != TYPE_SIMD) {
                checker_error(c, args[0]->line, args[0]->column,
                    "__simd_fma() requires Simd arguments"); return NULL; }
            for (int i = 1; i < 3; i++) {
                if (!type_equals(t0, args[i]->resolved_type)) {
                    checker_error(c, args[i]->line, args[i]->column,
                        "__simd_fma() argument types must all match '%s'",
                        type_name(t0)); return NULL; }
            }
            return type_clone(t0);
        }

        /* __simd_max(a, b) / __simd_min(a, b) -> Simd(T,N) (both operands match).
           Element-wise vmaxps/vminps (float) or smax/umax/... (int). */
        if (strcmp(name, "__simd_max") == 0 || strcmp(name, "__simd_min") == 0)
        {
            if (argc != 2) { checker_error(c, call_node->line, call_node->column,
                "%s() takes exactly 2 arguments, got %d", name, argc); return NULL; }
            Type *t0 = args[0]->resolved_type;
            if (t0 == NULL || t0->kind != TYPE_SIMD) {
                checker_error(c, args[0]->line, args[0]->column,
                    "%s() requires Simd arguments", name); return NULL; }
            if (!type_equals(t0, args[1]->resolved_type)) {
                checker_error(c, args[1]->line, args[1]->column,
                    "%s() argument types must match '%s'", name, type_name(t0)); return NULL; }
            return type_clone(t0);
        }

        /* __simd_load(ptr, off) -> Simd(T,N) from expected type (ptr is *T) */
        if (strcmp(name, "__simd_load") == 0)
        {
            if (argc != 2) { checker_error(c, call_node->line, call_node->column,
                "__simd_load() takes exactly 2 arguments (pointer, offset), got %d", argc); return NULL; }
            Type *pt = args[0]->resolved_type;
            if (pt == NULL || pt->kind != TYPE_POINTER) {
                checker_error(c, args[0]->line, args[0]->column,
                    "__simd_load() first argument must be a pointer *T"); return NULL; }
            if (args[1]->resolved_type == NULL || !type_is_integer(args[1]->resolved_type)) {
                checker_error(c, args[1]->line, args[1]->column,
                    "__simd_load() offset must be an integer"); return NULL; }
            Type *exp = c->expected_type;
            if (exp == NULL || exp->kind != TYPE_SIMD) {
                checker_error(c, call_node->line, call_node->column,
                    "__simd_load() result type cannot be inferred here; use it where "
                    "a Simd(T, N) is expected (e.g. `Simd(f32, 16) v = __simd_load(p, i)`)");
                return NULL;
            }
            return type_simd(type_clone(exp->as.simd.elem), exp->as.simd.lanes);
        }

        /* __simd_store(ptr, off, vec) -> void (ptr is *T) */
        if (strcmp(name, "__simd_store") == 0)
        {
            if (argc != 3) { checker_error(c, call_node->line, call_node->column,
                "__simd_store() takes exactly 3 arguments (pointer, offset, vector), got %d", argc); return NULL; }
            Type *pt = args[0]->resolved_type;
            if (pt == NULL || pt->kind != TYPE_POINTER) {
                checker_error(c, args[0]->line, args[0]->column,
                    "__simd_store() first argument must be a pointer *T"); return NULL; }
            if (args[1]->resolved_type == NULL || !type_is_integer(args[1]->resolved_type)) {
                checker_error(c, args[1]->line, args[1]->column,
                    "__simd_store() offset must be an integer"); return NULL; }
            if (args[2]->resolved_type == NULL || args[2]->resolved_type->kind != TYPE_SIMD) {
                checker_error(c, args[2]->line, args[2]->column,
                    "__simd_store() third argument must be a Simd value"); return NULL; }
            return type_void();
        }

        /* __simd_load_masked(ptr, off, n) -> Simd(T,N): load the first n lanes
           (rest zero). Fringe handling for non-multiple-of-N tails — the mask
           (icmp iota<n) is built internally, hiding the i1 vector. */
        if (strcmp(name, "__simd_load_masked") == 0)
        {
            if (argc != 3) { checker_error(c, call_node->line, call_node->column,
                "__simd_load_masked() takes exactly 3 arguments (pointer, offset, n), got %d", argc); return NULL; }
            Type *pt = args[0]->resolved_type;
            if (pt == NULL || pt->kind != TYPE_POINTER) {
                checker_error(c, args[0]->line, args[0]->column,
                    "__simd_load_masked() first argument must be a pointer *T"); return NULL; }
            if (args[1]->resolved_type == NULL || !type_is_integer(args[1]->resolved_type)) {
                checker_error(c, args[1]->line, args[1]->column,
                    "__simd_load_masked() offset must be an integer"); return NULL; }
            if (args[2]->resolved_type == NULL || !type_is_integer(args[2]->resolved_type)) {
                checker_error(c, args[2]->line, args[2]->column,
                    "__simd_load_masked() lane count must be an integer"); return NULL; }
            Type *exp = c->expected_type;
            if (exp == NULL || exp->kind != TYPE_SIMD) {
                checker_error(c, call_node->line, call_node->column,
                    "__simd_load_masked() result type cannot be inferred here; use it where "
                    "a Simd(T, N) is expected (e.g. `Simd(f32, 16) v = __simd_load_masked(p, i, n)`)");
                return NULL;
            }
            return type_simd(type_clone(exp->as.simd.elem), exp->as.simd.lanes);
        }

        /* __simd_store_masked(ptr, off, vec, n) -> void: store the first n lanes. */
        if (strcmp(name, "__simd_store_masked") == 0)
        {
            if (argc != 4) { checker_error(c, call_node->line, call_node->column,
                "__simd_store_masked() takes exactly 4 arguments (pointer, offset, vector, n), got %d", argc); return NULL; }
            Type *pt = args[0]->resolved_type;
            if (pt == NULL || pt->kind != TYPE_POINTER) {
                checker_error(c, args[0]->line, args[0]->column,
                    "__simd_store_masked() first argument must be a pointer *T"); return NULL; }
            if (args[1]->resolved_type == NULL || !type_is_integer(args[1]->resolved_type)) {
                checker_error(c, args[1]->line, args[1]->column,
                    "__simd_store_masked() offset must be an integer"); return NULL; }
            if (args[2]->resolved_type == NULL || args[2]->resolved_type->kind != TYPE_SIMD) {
                checker_error(c, args[2]->line, args[2]->column,
                    "__simd_store_masked() third argument must be a Simd value"); return NULL; }
            if (args[3]->resolved_type == NULL || !type_is_integer(args[3]->resolved_type)) {
                checker_error(c, args[3]->line, args[3]->column,
                    "__simd_store_masked() lane count must be an integer"); return NULL; }
            return type_void();
        }

        /* __simd_cast(v) -> Simd(U,N) from expected type; v is Simd(T,N) (same N).
           Element-wise numeric conversion (f16<->f32, int<->float, etc.). The
           mixed-precision bridge: load f16, cast to f32, compute, cast back. */
        if (strcmp(name, "__simd_cast") == 0)
        {
            if (argc != 1) { checker_error(c, call_node->line, call_node->column,
                "__simd_cast() takes exactly 1 argument, got %d", argc); return NULL; }
            Type *vt = args[0]->resolved_type;
            if (vt == NULL || vt->kind != TYPE_SIMD) {
                checker_error(c, args[0]->line, args[0]->column,
                    "__simd_cast() requires a Simd value"); return NULL; }
            Type *exp = c->expected_type;
            if (exp == NULL || exp->kind != TYPE_SIMD) {
                checker_error(c, call_node->line, call_node->column,
                    "__simd_cast() result type cannot be inferred here; use it where "
                    "a Simd(U, N) is expected (e.g. `Simd(f32, 16) f = __simd_cast(h)`)");
                return NULL;
            }
            if (exp->as.simd.lanes != vt->as.simd.lanes) {
                checker_error(c, call_node->line, call_node->column,
                    "__simd_cast() cannot change the lane count (%d -> %d)",
                    vt->as.simd.lanes, exp->as.simd.lanes); return NULL; }
            return type_simd(type_clone(exp->as.simd.elem), exp->as.simd.lanes);
        }

        /* __simd_floor(v) -> same Simd(float, N) (round toward -inf). */
        if (strcmp(name, "__simd_floor") == 0)
        {
            if (argc != 1) { checker_error(c, call_node->line, call_node->column,
                "__simd_floor() takes exactly 1 argument, got %d", argc); return NULL; }
            Type *vt = args[0]->resolved_type;
            if (vt == NULL || vt->kind != TYPE_SIMD || !type_is_float(vt->as.simd.elem)) {
                checker_error(c, args[0]->line, args[0]->column,
                    "__simd_floor() requires a float Simd value"); return NULL; }
            return type_simd(type_clone(vt->as.simd.elem), vt->as.simd.lanes);
        }

        /* __simd_bitcast(v) -> Simd(U, N): reinterpret bits, same lane count and
           same element bit-width (e.g. i32 <-> f32). Result type from expected. */
        if (strcmp(name, "__simd_bitcast") == 0)
        {
            if (argc != 1) { checker_error(c, call_node->line, call_node->column,
                "__simd_bitcast() takes exactly 1 argument, got %d", argc); return NULL; }
            Type *vt = args[0]->resolved_type;
            if (vt == NULL || vt->kind != TYPE_SIMD) {
                checker_error(c, args[0]->line, args[0]->column,
                    "__simd_bitcast() requires a Simd value"); return NULL; }
            Type *exp = c->expected_type;
            if (exp == NULL || exp->kind != TYPE_SIMD) {
                checker_error(c, call_node->line, call_node->column,
                    "__simd_bitcast() result type cannot be inferred here; use it where "
                    "a Simd(U, N) is expected (e.g. `Simd(f32, 16) f = __simd_bitcast(i)`)");
                return NULL; }
            if (exp->as.simd.lanes != vt->as.simd.lanes) {
                checker_error(c, call_node->line, call_node->column,
                    "__simd_bitcast() cannot change the lane count (%d -> %d)",
                    vt->as.simd.lanes, exp->as.simd.lanes); return NULL; }
            return type_simd(type_clone(exp->as.simd.elem), exp->as.simd.lanes);
        }

        checker_error(c, call_node->line, call_node->column,
                      "unknown simd intrinsic '%s'", name);
        return NULL;
    }

    return NULL;
}

static Type *check_builtin_call(Checker *c, const char *name, AstNode *call_node)
{
    int argc = call_node->as.call.arg_count;
    AstNode **args = call_node->as.call.args;

    /* Phase 2: the legacy __take/__drop_at/__dup/__move spellings are retired.
       Reject them with a clear pointer to the @-sigil replacement. */
    const char *retired = intrinsic_retired_spelling(name);
    if (retired != NULL)
    {
        checker_error(c, call_node->line, call_node->column,
                      "'%s' is retired; use '%s'", name, retired);
        return NULL;
    }

    /* Phase E.3.1: errno() -> int  — read C runtime errno (thread-local).
       On Windows uses _errno(), on POSIX uses __errno_location(). The codegen
       emits the platform-specific dereference inline. */
    if (strcmp(name, "errno") == 0)
    {
        if (argc != 0)
        {
            checker_error(c, call_node->line, call_node->column,
                          "errno() takes no arguments, got %d", argc);
            return NULL;
        }
        return type_int();
    }

    /* Phase E.3.3 / P5-4 S-2: from_cstr(object) -> Str
       Copies a C-style NUL-terminated char* (received via FFI as `object`)
       into an OWNED Str. Critical glue for getenv/strerror/readdir. */
    if (strcmp(name, "from_cstr") == 0)
    {
        if (argc != 1)
        {
            checker_error(c, call_node->line, call_node->column,
                          "from_cstr() takes 1 argument, got %d", argc);
            return NULL;
        }
        Type *arg_type = check_expr(c, args[0]);
        if (arg_type == NULL) return NULL;
        if (arg_type->kind != TYPE_OBJECT && arg_type->kind != TYPE_POINTER &&
            arg_type->kind != TYPE_NIL)
        {
            checker_error(c, args[0]->line, args[0]->column,
                          "from_cstr() requires object/pointer type, got '%s'",
                          type_name(arg_type));
            return NULL;
        }
        Type *strt = checker_str_type(c);
        if (strt == NULL)
        {
            checker_error(c, call_node->line, call_node->column,
                          "from_cstr() requires the Str type from std.core.str "
                          "(add `import std.core.str`)");
            return NULL;
        }
        return strt;
    }

    /* @move(var) -> T  — explicit move annotation.
       Marks the argument variable as MOVED and returns its type transparently.
       Works on any movable type; also force-moves static Strs (unlike implicit moves).
       Diagnostics print `name` (always the canonical @-spelling here — the legacy
       `__move` form is rejected by intrinsic_retired_spelling above), so they cannot
       drift back to naming a spelling the user is not allowed to write. */
    if (intrinsic_lookup(name) && intrinsic_lookup(name)->kind == INTR_VAR_MOVE)
    {
        if (argc != 1)
        {
            checker_error(c, call_node->line, call_node->column,
                          "%s() takes exactly 1 argument, got %d", name, argc);
            return NULL;
        }
        AstNode *arg = args[0];
        if (arg->kind != AST_IDENT)
        {
            checker_error(c, arg->line, arg->column,
                          "%s() requires a variable identifier, not an expression", name);
            /* Still type-check for error recovery */
            return check_expr(c, arg);
        }
        Type *arg_type = check_expr(c, arg); /* also reports use-of-moved if already moved */
        if (!arg_type) return NULL;
        Symbol *sym = scope_resolve(c->current_scope, arg->as.ident.name);
        if (sym)
        {
            if (sym->is_moved || sym->is_maybe_moved)
            {
                /* Already moved/maybe-moved — check_expr already reported; no double-report needed */
            }
            else if (sym->is_borrow)
            {
                /* Phase 5: __move() cannot transfer ownership of a borrow — it holds none. */
                checker_move_error(c, arg->line, arg->column,
                                   "cannot %s(): variable '%s' is a read-only borrow",
                                   name, arg->as.ident.name);
            }
            else if (sym->is_mut_borrow)
            {
                /* Phase 5.5: writable borrow can mutate but not transfer ownership. */
                checker_move_error(c, arg->line, arg->column,
                                   "cannot %s(): variable '%s' is a writable borrow "
                                   "(mutation allowed, but ownership cannot leave)",
                                   name, arg->as.ident.name);
            }
            else if (type_is_movable(sym->type))
            {
                /* Force-mark as moved, even for static strings */
                sym->is_moved = true;
                /* Move-elision (Q4): explicit __move(x) transfers ownership; let
                   codegen move instead of clone. Tag the inner IDENT (the value
                   that flows into the dst is __move(x), but codegen inspects the
                   unwrapped source via ast_unwrap_move). */
                arg->moved_out = true;
            }
            else
            {
                checker_error(c, arg->line, arg->column,
                              "%s() applied to non-movable type '%s'; "
                              "only has_drop struct (incl. Str/Vec/Map), "
                              "has_drop enum, and Block can be moved",
                              name, type_name(arg_type));
            }
        }
        return arg_type; /* transparent: __move(s) has the same type as s */
    }

    /* __drop_at(place) -> void — run the recursive destructor on the value at an
       lvalue place (e.g. a raw pointer slot p[i]). POD is a no-op. Lets a
       self-managed container (RawVec) drop owned elements in __drop/set/clear
       WITHOUT freeing the backing buffer. The slot is left logically dead;
       liveness is the container's responsibility (its `len` bound). */
    if (intrinsic_lookup(name) && intrinsic_lookup(name)->kind == INTR_PLACE_DISPOSE)
    {
        if (argc != 1)
        {
            checker_error(c, call_node->line, call_node->column,
                          "%s() takes exactly 1 argument, got %d", name, argc);
            return NULL;
        }
        Type *arg_type = check_expr(c, args[0]);
        if (arg_type == NULL) return NULL;
        if (args[0]->kind != AST_INDEX && args[0]->kind != AST_FIELD &&
            args[0]->kind != AST_IDENT &&
            !(args[0]->kind == AST_UNARY && args[0]->as.unary.op == TOKEN_STAR))
        {
            checker_error(c, args[0]->line, args[0]->column,
                          "%s() requires a place expression (p[i], field, or *p)", name);
            return NULL;
        }
        return type_void();
    }

    /* __take(place) -> T — move-OUT of an lvalue slot: bit-read the value WITHOUT
       cloning; the caller takes ownership and the slot is logically vacated (the
       container must drop its `len`/track liveness). The move-out counterpart of
       `__drop_at`; used by RawVec.pop / remove / insert / swap to relocate elements
       without a clone. Returns the element (pointee) type. */
    if (intrinsic_lookup(name) && intrinsic_lookup(name)->kind == INTR_PLACE_TAKE)
    {
        if (argc != 1)
        {
            checker_error(c, call_node->line, call_node->column,
                          "%s() takes exactly 1 argument, got %d", name, argc);
            return NULL;
        }
        Type *arg_type = check_expr(c, args[0]);
        if (arg_type == NULL) return NULL;
        if (args[0]->kind != AST_INDEX && args[0]->kind != AST_FIELD &&
            args[0]->kind != AST_IDENT &&
            !(args[0]->kind == AST_UNARY && args[0]->as.unary.op == TOKEN_STAR))
        {
            checker_error(c, args[0]->line, args[0]->column,
                          "%s() requires a place expression (p[i], field, or *p)", name);
            return NULL;
        }
        return arg_type; /* the element type read out of the slot */
    }

    /* __dup(place) -> T — DEEP COPY of the value at a place, WITHOUT consuming it
       (the source stays live). The generic value-duplication primitive: codegen
       loads the value and runs emit_clone_value — a bit-copy for POD T, a deep
       clone (__clone) for has_drop T (Str/Vec/Map/struct/enum). The counterpart of
       __take (which moves out): use __dup when you need an independent copy of a
       value you still own — e.g. Vec.fill(x) writes N copies of x; Map.get_or_insert
       returns a copy of the default it also inserts. Returns the value type. */
    if (intrinsic_lookup(name) && intrinsic_lookup(name)->kind == INTR_PLACE_DUP)
    {
        if (argc != 1)
        {
            checker_error(c, call_node->line, call_node->column,
                          "%s() takes exactly 1 argument, got %d", name, argc);
            return NULL;
        }
        Type *arg_type = check_expr(c, args[0]);
        if (arg_type == NULL) return NULL;
        if (args[0]->kind != AST_INDEX && args[0]->kind != AST_FIELD &&
            args[0]->kind != AST_IDENT &&
            !(args[0]->kind == AST_UNARY && args[0]->as.unary.op == TOKEN_STAR))
        {
            checker_error(c, args[0]->line, args[0]->column,
                          "%s() requires a place expression (p[i], field, or *p)", name);
            return NULL;
        }
        return arg_type; /* an independent copy of the value's type */
    }

    /* __rawstr("literal") -> *u8 — a raw pointer to a baked .rodata string,
       WITHOUT going through Str. Needed by std.core.reflect_core (a leaf module
       below Str/Vec that cannot import str), whose RawType stores names/signatures
       as *u8+len. The arg must be a string literal; codegen emits its
       GlobalStringPtr directly (same bytes Str's .data points at). Pair it with a
       compile-time length (strlen of the literal) at the call site. */
    if (strcmp(name, "__rawstr") == 0)
    {
        if (argc != 1 || args[0]->kind != AST_STRING_LIT)
        {
            checker_error(c, call_node->line, call_node->column,
                          "__rawstr() takes exactly 1 string-literal argument");
            return NULL;
        }
        return type_pointer(type_u8());
    }

    /* Task 7.7: family dispatch — bodies moved verbatim (incl. their own
       guards) into check_builtin_task/sync/atomic/simd above. */
    if (strcmp(name, "__task_spawn") == 0 || strcmp(name, "__task_join") == 0)
        return check_builtin_task(c, name, call_node);
    if (strncmp(name, "__mutex_", 8) == 0 || strncmp(name, "__rwlock_", 9) == 0 ||
        strncmp(name, "__cond_", 7) == 0 || strncmp(name, "__cpu_", 6) == 0)
        return check_builtin_sync(c, name, call_node);
    if (strncmp(name, "__atomic_", 9) == 0)
        return check_builtin_atomic(c, name, call_node);
    if (strncmp(name, "__simd_", 7) == 0)
        return check_builtin_simd(c, name, call_node);

    return NULL;
}

/* Intrinsic registry — single source of truth for the @-sigil place/ownership
   builtins. Each entry accepts both the canonical @-name and (during migration)
   the legacy __ spelling. */
static const IntrinsicDef k_intrinsics[] = {
    { "@take",    "__take",    INTR_PLACE_TAKE,    1 },
    { "@dispose", "__drop_at", INTR_PLACE_DISPOSE, 1 },
    { "@dup",     "__dup",     INTR_PLACE_DUP,     1 },
    { "@move",    "__move",    INTR_VAR_MOVE,      1 },
};

const IntrinsicDef *intrinsic_lookup(const char *name)
{
    if (name == NULL) return NULL;
    for (size_t i = 0; i < sizeof(k_intrinsics) / sizeof(k_intrinsics[0]); i++) {
        const IntrinsicDef *d = &k_intrinsics[i];
        if (strcmp(name, d->canonical) == 0) return d;
        if (d->legacy != NULL && strcmp(name, d->legacy) == 0) return d;
    }
    return NULL;
}

/* A legacy __ spelling of a now-@ intrinsic. Returns the canonical @-name, or
   NULL. Used to give a clear "retired; use @name" diagnostic (Phase 2). */
static const char *intrinsic_retired_spelling(const char *name)
{
    if (name == NULL) return NULL;
    for (size_t i = 0; i < sizeof(k_intrinsics) / sizeof(k_intrinsics[0]); i++)
        if (k_intrinsics[i].legacy != NULL &&
            strcmp(name, k_intrinsics[i].legacy) == 0)
            return k_intrinsics[i].canonical;
    return NULL;
}

/* Check if a name is a builtin function (so we don't report "undefined variable") */
bool is_builtin_function(const char *name)
{
    if (intrinsic_lookup(name) != NULL) return true;
    return strcmp(name, "from_cstr") == 0 ||
           strcmp(name, "errno") == 0 ||
           strcmp(name, "__rawstr") == 0 ||
           strcmp(name, "__task_spawn") == 0 ||
           strcmp(name, "__task_join") == 0 ||
           strncmp(name, "__atomic_", 9) == 0 ||
           strncmp(name, "__mutex_", 8) == 0 ||
           strncmp(name, "__rwlock_", 9) == 0 ||
           strncmp(name, "__cond_", 7) == 0 ||
           strncmp(name, "__simd_", 7) == 0 ||
           strcmp(name, "__cpu_relax") == 0 ||
           strcmp(name, "__cpu_yield") == 0;
}

/* ---- Phase C closure capture analysis ----
   Walks an AST_CLOSURE body to collect free variables (names referenced in
   the body that aren't bound by the closure's params or by inner local
   declarations). Each free variable that resolves to a symbol in the outer
   scope is recorded as a capture on the closure node. POD-only in v1 —
   non-POD captures (string/vec/map/struct/enum) are rejected with a
   "not yet implemented" diagnostic so users get a clean message instead of
   silent corruption.

   `bound[]` tracks names currently in scope WITHIN the closure body. We
   pre-populate it with the closure's parameter names. Block boundaries
   snapshot/restore the bound count so var decls don't leak across siblings.
*/
/* CaptureScan moved to checker_internal.h (shared across TUs). */












/* ---- Expression checking ---- */




/* ---- C1: Option/Result combinators (docs/plan_container_access_safety.md §5.3) ----
   unwrap / expect / unwrap_or / is_some? / is_none? / is_ok? / is_err? are lowered
   by the compiler, mirroring `try` and force-unwrap `!` (which are also not library
   methods). `impl` on the builtin Option/Result enum templates is unsupported, and
   generic free functions would need explicit type args at each call site — so the
   checker intercepts `opt.METHOD(args)` and rewrites the AST_CALL in place to either
   a force-unwrap (the two panic combinators) or a 2-arm match expression (the rest),
   then re-checks. This reuses the mature match drop/move machinery and the
   force-unwrap discriminant lowering, introducing NO new ownership code. */
/* OptCombinator moved to checker_internal.h; g_optc_uid + lowering moved to checker_lower.c. */


/* ---- C2b: closure combinators (map / and_then / unwrap_or_else / map_err) ----
   These take a closure argument. To sidestep the closure-as-callee inference gap
   (a bare `|x| body` literal in callee position has no Block expected type), the
   lower INLINES the closure body directly into the lowered match arm, reusing the
   closure's parameter name as the arm binder. No closure value, env, or call is
   created — captured variables resolve in the enclosing scope as ordinary reads.
   The new result type param U is supplied explicitly (`opt.map(U)(|x| ...)`),
   matching the language's existing method-level generic convention
   (`vec.map(int)(...)`); it builds the result Option(U)/Result(...) pushed as the
   expected type so the bare ctors in the arm bodies resolve. */





/* ---- V1 bit-pattern match helpers ---- */




/* ---- Generic free-function value-arg inference + borrow params (Gap 1/2) ---- */

/* Gap 1: does param TypeNode `tn` *directly* name the type parameter `tp_name` —
   either as a bare `T`, or under one reference shell `&T` / `&!T`? Deeper
   structural positions (`Vec(T)`, `Block(T)->U`, `*T`, ...) are out of scope for
   v1 inference; explicit type args still cover those. When the param is a
   reference shell, *is_ref is set so the caller can strip an explicit `&x`
   before probing the argument's value type. */
static bool fn_param_directly_names_tp(const TypeNode *tn, const char *tp_name,
                                       bool *is_ref)
{
    if (is_ref) *is_ref = false;
    if (tn == NULL || tp_name == NULL) return false;
    if (tn->kind == TYPE_NODE_REFERENCE) {
        const TypeNode *inner = tn->as.pointee;
        if (inner && inner->kind == TYPE_NODE_NAMED &&
            inner->as.named.arg_count == 0 && inner->as.named.name &&
            strcmp(inner->as.named.name, tp_name) == 0) {
            if (is_ref) *is_ref = true;
            return true;
        }
        return false;
    }
    return tn->kind == TYPE_NODE_NAMED && tn->as.named.arg_count == 0 &&
           tn->as.named.name && strcmp(tn->as.named.name, tp_name) == 0;
}

/* Reading through a borrow yields the same value type — peel one reference shell
   so the inferred type param is the pointee value (T from a `&T` argument). */
static Type *fn_infer_peel_borrow(Type *t)
{
    return (t && t->kind == TYPE_REFERENCE) ? t->as.pointer_to : t;
}

/* Gap 2 (§13 twin for the generic free-function path): when `pt` is a read-only
   `&T` parameter and the argument is an explicit `&x` / `&obj.f` / `&v[i]`, strip
   the address-of shell so the call takes the proven auto-borrow path (identical
   to passing the lvalue bare). Without this, `&x` types as a raw `*T` and
   mismatches the `&T` formal. `&!x` (AST_MUT_BORROW) and non-place operands keep
   their existing semantics (untouched here). */
static void fn_call_strip_amp_shell(AstNode *call, int ai, const Type *pt)
{
    if (pt == NULL || pt->kind != TYPE_REFERENCE || pt->is_mut) return;
    AstNode *argn = call->as.call.args[ai];
    if (argn && argn->kind == AST_UNARY && argn->as.unary.op == TOKEN_AMP &&
        argn->as.unary.operand &&
        (argn->as.unary.operand->kind == AST_IDENT ||
         argn->as.unary.operand->kind == AST_FIELD ||
         argn->as.unary.operand->kind == AST_INDEX))
        call->as.call.args[ai] = argn->as.unary.operand; /* shell intentionally leaked */
}

/* Stage C-2 / D (docs/plan_print_sink.md): a struct/enum that impls Show renders
   via Show in print() and f-string interpolation. These two helpers detect such a
   type and rewrite an arg slot `x` -> `to_str(x)` (an owned Str) in place. Str is
   excluded (its raw-text form is the desired output); POD/array aren't aggregates.
   The rewrite only fires when the type satisfies Show, which means std.core.show
   is imported (to define/derive the impl), so the bare `to_str` resolves. */
bool type_is_show_aggregate(Checker *c, Type *t)
{
    if (t == NULL) return false;
    if (t->kind == TYPE_STRUCT)
    {
        if (t->as.strukt.name && strcmp(t->as.strukt.name, "Str") == 0) return false;
        return checker_type_satisfies_trait(c, t, "Show");
    }
    if (t->kind == TYPE_ENUM)
        return checker_type_satisfies_trait(c, t, "Show");
    return false;
}

void wrap_arg_in_to_str(AstNode **slot)
{
    AstNode *orig = *slot;
    AstNode *callee = ast_new(AST_IDENT, orig->line, orig->column);
    callee->as.ident.name = (char *)malloc_safe(7); /* "to_str"+NUL */
    memcpy(callee->as.ident.name, "to_str", 7);
    AstNode *call = ast_new(AST_CALL, orig->line, orig->column);
    call->as.call.callee = callee;
    call->as.call.args = (AstNode **)malloc_safe(sizeof(AstNode *));
    call->as.call.args[0] = orig;
    call->as.call.arg_count = 1;
    *slot = call;
}

/* S4: check_expr_call call-form dispatch extracted as static helpers (verbatim
   moves — see docs task 3.3). Each returns true when it fully handled the call
   (result stashed in *out_result, possibly NULL on error) so the caller should
   `break` out of the outer do/while immediately; false means "not this call
   form", so check_expr_call should keep falling through to the next check. */

/* Variant ctor short-circuit: callee is an IDENT matching a registered enum
   variant. Handles `RGB(1,2,3)`, `Some(x)`, etc. */
static bool check_call_variant_ctor(Checker *c, AstNode *node, Type **out_result)
{
    if (node->as.call.callee->kind != AST_IDENT)
        return false;

    Type *enum_type = NULL;
    int variant_idx = -1;
    int matches = find_variant(c,
        node->as.call.callee->as.ident.name, &enum_type, &variant_idx);
    if (matches == 1)
    {
        *out_result = check_variant_ctor(c, node, enum_type, variant_idx,
                                    node->as.call.args, node->as.call.arg_count);
        return true;
    }
    if (matches > 1)
    {
        /* Disambiguate a payload variant ctor (e.g. `Some(x)`/`Ok(x)`/
           `Err(e)`) by a type hint (prior resolution, then expected). */
        Type *eet = NULL; int evi = -1;
        if (disambig_variant_by_hint(c, node,
                node->as.call.callee->as.ident.name, &eet, &evi))
        {
            *out_result = check_variant_ctor(c, node, eet, evi,
                                        node->as.call.args,
                                        node->as.call.arg_count);
            return true;
        }
        checker_error(c, node->line, node->column,
                      "ambiguous variant name '%s' (matches multiple enums)",
                      node->as.call.callee->as.ident.name);
        *out_result = NULL;
        return true;
    }
    return false;
}

/* Static-by-typename call detection: `StructName.method(args)` /
   `EnumName.method(args)` / a type-alias parameter `T.zero()` / a builtin
   primitive `int.from_value(v)`, plus the two AST rewrites that feed a
   parameterized generic instance written directly as the receiver
   (`Box(Str).reflect()` / `Box(int).reflect()`) into the same dispatch.
   *inout_obj_node may be rewritten in place (kept in sync with
   node->as.call.callee->as.field_access.object, mirroring the original
   inline code). Returns true on a terminal error (caller must treat this
   as `result = NULL; break;`); on false, *out_is_static_call /
   *out_method_struct reflect whatever was detected (possibly nothing). */
static bool check_call_static_method(Checker *c, AstNode *node, AstNode **inout_obj_node,
                                      const char *method_name,
                                      bool *out_is_static_call,
                                      const char **out_method_struct)
{
    AstNode *obj_node = *inout_obj_node;
    bool is_static_call = false;
    const char *method_struct = NULL;

    /* ③ case B: `Box(Str).reflect()` with a USER-TYPE arg parses as a call
       `Box(Str)` (Str is an IDENT, ambiguous with a value at parse time) —
       object is AST_CALL(callee=IDENT, args=[type-name idents]). Disambiguate
       HERE with type info: if the callee names a GENERIC STRUCT TEMPLATE (you
       can't "call" a struct, so this can only be an instantiation) and every
       arg is a bare type name, REWRITE the object into an AST_IDENT carrying
       type args — then the case-A branch below instantiates + dispatches it.
       A real `make_box(cfg).render()` call-chain is untouched (make_box is not
       a generic struct template → find_struct_template_idx returns -1). */
    if (obj_node->kind == AST_CALL &&
        obj_node->as.call.callee &&
        obj_node->as.call.callee->kind == AST_IDENT &&
        obj_node->as.call.arg_count > 0 &&
        obj_node->as.call.type_arg_count == 0 &&
        find_struct_template_idx(c, obj_node->as.call.callee->as.ident.name) >= 0)
    {
        bool all_type_idents = true;
        for (int ai = 0; ai < obj_node->as.call.arg_count; ai++)
            if (obj_node->as.call.args[ai] == NULL ||
                obj_node->as.call.args[ai]->kind != AST_IDENT)
                { all_type_idents = false; break; }
        if (all_type_idents)
        {
            const char *gname = obj_node->as.call.callee->as.ident.name;
            int ac = obj_node->as.call.arg_count;
            AstNode *idn = ast_new(AST_IDENT, obj_node->line, obj_node->column);
            size_t gl = strlen(gname) + 1;
            idn->as.ident.name = (char *)malloc_safe(gl);
            memcpy(idn->as.ident.name, gname, gl);
            idn->as.ident.type_args =
                (TypeNode **)malloc_safe((size_t)ac * sizeof(TypeNode *));
            idn->as.ident.type_arg_count = ac;
            for (int ai = 0; ai < ac; ai++)
            {
                const char *an = obj_node->as.call.args[ai]->as.ident.name;
                TypeNode *atn = (TypeNode *)malloc_safe(sizeof(TypeNode));
                memset(atn, 0, sizeof(TypeNode));
                atn->kind = TYPE_NODE_NAMED;
                size_t al = strlen(an) + 1;
                atn->as.named.name = (char *)malloc_safe(al);
                memcpy(atn->as.named.name, an, al);
                idn->as.ident.type_args[ai] = atn;
            }
            ast_free(obj_node);
            node->as.call.callee->as.field_access.object = idn;
            obj_node = idn;
        }
    }

    /* ③: static call on a parameterized generic instance written directly,
       `Box(int).reflect()` / `Box(int).from_value(v)`. The parser produced an
       AST_IDENT carrying type args (for type-keyword args). Instantiate
       name(type_args) into the concrete struct/enum type and dispatch the
       static method on it, mirroring the `type BI = Box(int); BI.reflect()`
       alias path (which find_type_alias resolves below). Stamp resolved_type
       so codegen derives the instance's symbol. */
    if (obj_node->kind == AST_IDENT && obj_node->as.ident.type_arg_count > 0)
    {
        TypeNode tn;
        memset(&tn, 0, sizeof(tn));
        tn.kind = TYPE_NODE_NAMED;
        tn.as.named.name = (char *)obj_node->as.ident.name;
        tn.as.named.args = obj_node->as.ident.type_args;
        tn.as.named.arg_count = obj_node->as.ident.type_arg_count;
        Type *inst = resolve_type_node(c, &tn, node->line, node->column);
        if (inst && (inst->kind == TYPE_STRUCT || inst->kind == TYPE_ENUM))
        {
            const char *inst_key = impl_key_of_type(inst);
            if (inst_key)
            {
                int si = method_is_static(c, inst_key, method_name);
                if (si < 0 && inst->kind == TYPE_STRUCT &&
                    inst->as.strukt.generic_base)
                {
                    ensure_generic_struct_impls_local(c, inst);
                    si = method_is_static(c, inst_key, method_name);
                }
                if (si >= 0)
                {
                    method_struct = inst_key;
                    is_static_call = true;
                    obj_node->resolved_type = inst; /* codegen symbol source */
                    if (si == 0)
                    {
                        checker_error(c, node->line, node->column,
                            "cannot call instance method '%s' on type '%s'; use an instance",
                            method_name, inst_key);
                        *inout_obj_node = obj_node;
                        *out_is_static_call = is_static_call;
                        *out_method_struct = method_struct;
                        return true;
                    }
                }
            }
        }
    }

    /* Check if obj is a struct type name (static call: Point.origin()) */
    if (obj_node->kind == AST_IDENT && !is_static_call)
    {
        Type *st = find_struct_type(c, obj_node->as.ident.name);
        if (st && st->kind == TYPE_STRUCT)
        {
            const char *st_key = impl_key_of_type(st);  /* B-4.1 */
            int si = method_is_static(c, st_key, method_name);
            if (si >= 0)
            {
                method_struct = st_key;
                is_static_call = true;
                if (si == 0)
                {
                    /* Calling instance method via type name — error */
                    checker_error(c, node->line, node->column,
                                  "cannot call instance method '%s' on type '%s'; use an instance",
                                  method_name, method_struct);
                    *inout_obj_node = obj_node;
                    *out_is_static_call = is_static_call;
                    *out_method_struct = method_struct;
                    return true;
                }
            }
            }
        }
        /* Check if obj is an enum type name (static call: JsonValue.parse()) */
        if (obj_node->kind == AST_IDENT && !is_static_call)
        {
            Type *et = find_enum_type(c, obj_node->as.ident.name);
            if (et && et->kind == TYPE_ENUM)
            {
                const char *et_key = impl_key_of_type(et);  /* B-4.1 */
                int si = method_is_static(c, et_key, method_name);
                if (si >= 0)
                {
                    method_struct = et_key;
                    is_static_call = true;
                    if (si == 0)
                    {
                        checker_error(c, node->line, node->column,
                                      "cannot call instance method '%s' on type '%s'; use an instance",
                                      method_name, method_struct);
                        *inout_obj_node = obj_node;
                        *out_is_static_call = is_static_call;
                        *out_method_struct = method_struct;
                        return true;
                    }
                }
            }
        }

        /* Static call via a generic type parameter: `T.zero()` where T is a
           type alias bound during monomorphization (T → Complex(f64) / int).
           find_struct_type/find_enum_type miss on the bare param name; resolve
           it through the type-alias table, then dispatch the static method on
           the concrete type. Stamp obj_node->resolved_type so codegen derives
           the right symbol (Struct.llvm_name.method / int.method). */
        if (obj_node->kind == AST_IDENT && !is_static_call)
        {
            Type *al = find_type_alias(c, obj_node->as.ident.name);
            if (al)
            {
                const char *al_key = (al->kind == TYPE_STRUCT || al->kind == TYPE_ENUM)
                                         ? impl_key_of_type(al)
                                         : type_impl_name(al);
                if (al_key)
                {
                    int si = method_is_static(c, al_key, method_name);
                    if (si < 0 && al->kind == TYPE_STRUCT && al->as.strukt.generic_base)
                    {
                        ensure_generic_struct_impls_local(c, al);
                        si = method_is_static(c, al_key, method_name);
                    }
                    if (si >= 0)
                    {
                        method_struct = al_key;
                        is_static_call = true;
                        obj_node->resolved_type = al; /* codegen symbol source */
                        if (si == 0)
                        {
                            checker_error(c, node->line, node->column,
                                          "cannot call instance method '%s' on type parameter; use an instance",
                                          method_name);
                            *inout_obj_node = obj_node;
                            *out_is_static_call = is_static_call;
                            *out_method_struct = method_struct;
                            return true;
                        }
                    }
                }
            }
        }

        /* Static call on a literal primitive type name: `int.from_value(v)`
           / `bool.show()`. Arises from the comptime `f.type` handle lowering
           to the field's concrete type name (and is a reasonable spelling on
           its own). find_struct_type / find_enum_type / find_type_alias all
           miss a bare primitive keyword-name; resolve it via the builtin-type
           table (same key as the T-alias-to-primitive path: type_impl_name). */
        if (obj_node->kind == AST_IDENT && !is_static_call)
        {
            Type *bt = resolve_builtin_type_by_name(obj_node->as.ident.name);
            if (bt)
            {
                const char *bt_key = type_impl_name(bt);
                if (bt_key)
                {
                    int si = method_is_static(c, bt_key, method_name);
                    if (si >= 0)
                    {
                        method_struct = bt_key;
                        is_static_call = true;
                        obj_node->resolved_type = bt; /* codegen symbol source */
                        if (si == 0)
                        {
                            checker_error(c, node->line, node->column,
                                          "cannot call instance method '%s' on type '%s'; use an instance",
                                          method_name, bt_key);
                            *inout_obj_node = obj_node;
                            *out_is_static_call = is_static_call;
                            *out_method_struct = method_struct;
                            return true;
                        }
                    }
                }
            }
        }

    *inout_obj_node = obj_node;
    *out_is_static_call = is_static_call;
    *out_method_struct = method_struct;
    return false;
}

/* L-002: interface-qualified call `Iface.method(recv, args...)`. The leading
   IDENT names a known interface, not a value/type. The receiver is args[0];
   REWRITE into an ordinary instance call so all the existing
   receiver-resolution / borrow-gating / arg-checking below applies:
       Iface.m(recv, a1, ...)  ==>  recv.m(a1, ...)
   and stamp node.qualified_iface so the resolution step picks the interface
   overload (and codegen mangles the symbol when contended). (Zero parser
   changes — the token shape `Ident.Ident(args)` is identical to a static
   call; we recognize the interface name here.) *inout_obj_node is rewritten
   in place when the interface-qualified form is detected (kept in sync with
   node->as.call.callee->as.field_access.object, mirroring the original
   inline code). Returns true on a terminal error (caller must treat this as
   `result = NULL; break;`). */
static bool check_call_interface_qualified(Checker *c, AstNode *node, AstNode **inout_obj_node,
                                            const char *method_name)
{
    AstNode *obj_node = *inout_obj_node;

    if (obj_node->kind == AST_IDENT &&
        checker_is_known_interface(c, obj_node->as.ident.name))
    {
        if (node->as.call.arg_count < 1)
        {
            checker_error(c, node->line, node->column,
                "interface-qualified call '%s.%s' requires a receiver "
                "argument, e.g. '%s.%s(recv)'",
                obj_node->as.ident.name, method_name,
                obj_node->as.ident.name, method_name);
            return true;
        }
        /* Stamp the interface name (owned). The resolution step frees it
           again if the method turns out not to be contended (plain `T.m`). */
        free(node->as.call.qualified_iface);
        node->as.call.qualified_iface = chk_strdup(obj_node->as.ident.name);
        /* recv = args[0]. Strip an explicit borrow shell `&x` / `&!x`: the
           instance-method machinery auto-borrows the receiver (takes its
           address for self), so a borrow wrapper would make self a
           pointer-to-pointer. Mirrors fn_call_strip_amp_shell. */
        AstNode *recv = node->as.call.args[0];
        if (recv->kind == AST_MUT_BORROW && recv->as.mut_borrow.operand)
        {
            AstNode *inner = recv->as.mut_borrow.operand;
            recv->as.mut_borrow.operand = NULL;
            ast_free(recv);
            recv = inner;
        }
        else if (recv->kind == AST_UNARY && recv->as.unary.op == TOKEN_AMP &&
                 recv->as.unary.operand &&
                 (recv->as.unary.operand->kind == AST_IDENT ||
                  recv->as.unary.operand->kind == AST_FIELD ||
                  recv->as.unary.operand->kind == AST_INDEX))
        {
            AstNode *inner = recv->as.unary.operand;
            recv->as.unary.operand = NULL;
            ast_free(recv);
            recv = inner;
        }
        /* install recv as the field object, shift the remaining args left. */
        for (int ai = 1; ai < node->as.call.arg_count; ai++)
            node->as.call.args[ai - 1] = node->as.call.args[ai];
        node->as.call.arg_count--;
        node->as.call.callee->as.field_access.object = recv;
        ast_free(obj_node);   /* detached interface IDENT */
        obj_node = recv;
    }

    *inout_obj_node = obj_node;
    return false;
}

/* Resolve callee type for a dispatched instance/static method call
   (is_method_call || is_static_call already established by the caller —
   see the struct/enum method-detection block above this in check_expr_call).
   Handles the L-002 interface-qualified overload selection, the bare-call
   ambiguity check, plain find_method resolution, method-level generic
   instantiation (explicit type args or closure-arg inference), and the
   explicit __drop() rejection. *out_callee_type receives the resolved
   function type. Returns true on a terminal error (caller must treat this
   as `result = NULL; break;`). */
static bool check_call_instance_method(Checker *c, AstNode *node,
                                        bool is_method_call, bool is_static_call,
                                        const char *method_struct,
                                        const char *method_name,
                                        Type **out_callee_type)
{
    Type *callee_type = NULL;

    /* L-002: interface-qualified call `Iface.m(recv)` (rewritten above to
       `recv.m(...)` with qualified_iface stamped). Select the interface
       overload by origin rather than the inherent-preferring find_method,
       and keep qualified_iface set ONLY if the method is contended (so
       codegen mangles to `T.<Iface>.m`; a single-provider interface keeps
       the plain `T.m`). */
    if (node->as.call.qualified_iface && is_method_call)
    {
        const char *qi = node->as.call.qualified_iface;
        callee_type = find_method_origin(c, method_struct, method_name, qi);
        if (callee_type == NULL)
        {
            checker_error(c, node->line, node->column,
                "interface '%s' has no method '%s' for type '%s'",
                qi, method_name, method_struct);
            return true;
        }
        node->as.call.callee->resolved_type = callee_type;
        int inh = 0, ifc = 0;
        method_providers(c, method_struct, method_name, &inh, &ifc, NULL, NULL);
        if (inh + ifc < 2)
        {
            free(node->as.call.qualified_iface);  /* not contended → plain T.m */
            node->as.call.qualified_iface = NULL;
        }
        /* L-002 v2: for a generic instance, force-instantiate THIS overload's
           body, keyed by the iface-aware lazy symbol (`T.<Iface>.m` when
           contended, else `T.m`). No-op for non-generic types (no lazy entry).
           Without this the qualified path would skip instantiation → JIT
           "Symbols not found". */
        {
            char *qsym = mangle_method_symbol(
                method_struct, node->as.call.qualified_iface, method_name);
            ensure_generic_method_instantiated_sym(c, method_struct, qsym,
                                                   node->line, node->column);
            free(qsym);
        }
        goto after_method_check;
    }

    /* L-002: bare instance dispatch `obj.m()` where `m` is ambiguous —
       no inherent provider and >=2 interfaces provide it. The user must
       disambiguate with a qualified call `Iface.m(recv)`. (Inherent
       priority resolves the "inherent + interface" overlap silently;
       only the "all-interface, >=2" case is irresolvable here.) */
    if (is_method_call && !is_static_call)
    {
        int inh = 0, ifc = 0; const char *ia = NULL, *ib = NULL;
        method_providers(c, method_struct, method_name, &inh, &ifc, &ia, &ib);
        if (inh == 0 && ifc >= 2)
        {
            checker_error(c, node->line, node->column,
                "ambiguous method '%s' on type '%s': provided by interfaces "
                "'%s' and '%s'; disambiguate with a qualified call, "
                "e.g. '%s.%s(recv)'",
                method_name, method_struct, ia, ib, ia, method_name);
            return true;
        }
    }

    callee_type = find_method(c, method_struct, method_name);
    if (callee_type == NULL)
    {
        char helpbuf[256];
        DiagMethodIter it = { c, NULL, method_struct, 0, 0, 0, false };
        const char *help = diag_help_suggestion(
            helpbuf, sizeof(helpbuf), method_name,
            diag_method_iter_next, &it);
        checker_error_help(c, node->line, node->column, 1, help,
                           "type '%s' has no method '%s'",
                           method_struct, method_name);
        return true;
    }

    /* Method-level generic: if the call site provides type args, try
       to build the concrete signature on-the-fly.  The placeholder
       returned by find_method has type_void() — the real type is built
       and body-checked here. */
    if (node->as.call.type_arg_count > 0) {
        Type *concrete = try_instantiate_method_level_generic(
            c, method_struct, method_name,
            node->as.call.type_args, node->as.call.type_arg_count,
            NULL, node->line, node->column);
        if (concrete) {
            callee_type = concrete;
            node->as.call.callee->resolved_type = callee_type;
            /* Stash the resolved (concrete) method-level type-arg names so
               codegen mangles the call as `Type.method(int)` not
               `Type.method(T)` when called with an abstract type param
               inside a generic body. The alias (T→int) is active here, so
               resolve_type_node yields the concrete type; codegen has no
               alias context and would otherwise re-mangle the raw `T`.
               Mirrors the closure-inference and free-function paths. */
            if (node->as.call.resolved_type_args == NULL) {
                int tac = node->as.call.type_arg_count;
                Type **rargs = (Type **)malloc_safe((size_t)(tac > 0 ? tac : 1)
                                                    * sizeof(Type *));
                for (int ti = 0; ti < tac; ti++)
                    rargs[ti] = resolve_type_node(c, node->as.call.type_args[ti],
                                                  node->line, node->column);
                checker_stash_resolved_type_args(c, node, rargs, tac);
                free(rargs);
            }
            /* Body already checked+queued by try_instantiate; skip lazy path */
            goto after_method_check;
        }
    } else {
        /* No explicit type args: try to infer a single method-level
           type param from a closure arg's return type, so
           `v.map(|x| x+1)` works like `v.map(int)(|x| x+1)`. */
        Type *concrete = try_infer_method_generic_from_closure(
            c, method_struct, method_name, node,
            node->line, node->column);
        if (concrete) {
            callee_type = concrete;
            node->as.call.callee->resolved_type = callee_type;
            goto after_method_check;
        }
    }

    if (!ensure_generic_method_instantiated(c, method_struct, method_name,
                                             node->line, node->column))
    {
        return true;
    }
    /* Set resolved_type on the callee node so codegen can find it */
    node->as.call.callee->resolved_type = callee_type;

    after_method_check: ;

    /* A-2 (docs/bugs_deferred_p5_4.md §2): explicit `.__drop()` calls in
       source are rejected. The compiler manages destruction automatically
       (RAII at scope exit); an explicit call is always a double-free
       footgun, and for a compiler-generated member __drop the symbol may
       not even be emitted (JIT "Symbols not found"). Block it cleanly at
       the checker rather than crashing/double-freeing at runtime. */
    if (strcmp(method_name, "__drop") == 0 && is_method_call)
    {
        checker_error(c, node->line, node->column,
                      "cannot call __drop() explicitly; the compiler "
                      "destroys values automatically at scope exit "
                      "(an explicit call would double-free)");
        return true;
    }

    *out_callee_type = callee_type;
    return false;
}

/* G2: generic free-function call.
     - explicit type args:  identity(int)(42)
     - inferred type args:  to_csv(p)   (Gap 1) — when the call omits the
       `(T)` list, unify each type param against the value-argument types.
   Enter whenever the callee names a known fn-template, OR type args were
   given explicitly (so a stray `foo(int)(...)` on a non-template name
   still reports "not a generic function"). Returns false (leaving
   *out_result untouched) when the call doesn't match this form at all —
   caller falls through to the next dispatch form. Returns true once
   matched (success or terminal error alike; *out_result is the checked
   type, or NULL on error) — caller does `result = *out_result; break;`. */
static bool check_call_generic_free_fn(Checker *c, AstNode *node, Type **out_result)
{
    if (!(node->as.call.callee->kind == AST_IDENT &&
          (node->as.call.type_arg_count > 0 ||
           find_fn_template(c, node->as.call.callee->as.ident.name) >= 0)))
        return false;

    const char *fn_name = node->as.call.callee->as.ident.name;
    int tmpl_idx = find_fn_template(c, fn_name);
    if (tmpl_idx < 0) {
        checker_error(c, node->line, node->column,
            "'%s' is not a generic function", fn_name);
        *out_result = NULL;
        return true;
    }
    int tp_count = c->fn_templates[tmpl_idx].type_param_count;
    bool inferring = (node->as.call.type_arg_count == 0);
    if (!inferring && node->as.call.type_arg_count != tp_count) {
        checker_error(c, node->line, node->column,
            "'%s' expects %d type argument(s), got %d",
            fn_name, tp_count, node->as.call.type_arg_count);
        *out_result = NULL;
        return true;
    }

    AstNode *tmpl_decl0 = c->fn_templates[tmpl_idx].decl_node;
    char **tp_names0 = c->fn_templates[tmpl_idx].type_params;

    /* Resolve type arguments — explicit (resolve TypeNodes) or inferred
       (Gap 1: unify each type param against the corresponding value arg). */
    Type **type_args = (Type **)malloc_safe((size_t)tp_count * sizeof(Type *));
    bool type_args_ok = true;
    if (inferring) {
        int pc0 = tmpl_decl0->as.fn_decl.param_count;
        int argc0 = node->as.call.arg_count;
        for (int ti = 0; ti < tp_count; ti++) type_args[ti] = NULL;
        for (int ti = 0; ti < tp_count && type_args_ok; ti++) {
            const char *tname = tp_names0[ti];
            for (int pi = 0; pi < pc0 && pi < argc0; pi++) {
                bool is_ref = false;
                if (!fn_param_directly_names_tp(
                        tmpl_decl0->as.fn_decl.param_types[pi], tname, &is_ref))
                    continue;
                /* Gap 2 pre-strip: explicit `&x` against a read-only `&T`
                   param — drop the address-of shell so the probe reads the
                   lvalue's value type (mirrors §13). */
                if (is_ref) {
                    AstNode *argn = node->as.call.args[pi];
                    if (argn->kind == AST_UNARY &&
                        argn->as.unary.op == TOKEN_AMP &&
                        argn->as.unary.operand &&
                        (argn->as.unary.operand->kind == AST_IDENT ||
                         argn->as.unary.operand->kind == AST_FIELD ||
                         argn->as.unary.operand->kind == AST_INDEX))
                        node->as.call.args[pi] = argn->as.unary.operand;
                }
                Type *at = check_expr(c, node->as.call.args[pi]);
                if (at) type_args[ti] = fn_infer_peel_borrow(at);
                break;
            }
            if (!type_args[ti]) {
                checker_error(c, node->line, node->column,
                    "cannot infer type parameter '%s' of generic function "
                    "'%s' from the arguments; pass it explicitly as "
                    "%s(<type>)(...)", tname, fn_name, fn_name);
                type_args_ok = false;
            }
        }
    } else {
        for (int ti = 0; ti < tp_count; ti++) {
            type_args[ti] = resolve_type_node(c, node->as.call.type_args[ti],
                node->line, node->column);
            if (!type_args[ti]) { type_args_ok = false; break; }
        }
    }
    if (!type_args_ok) { free(type_args); *out_result = NULL; return true; }

    /* Stash the resolved (concrete) type-arg names so codegen mangles the
       call to the instantiated symbol (mirrors the method-generic
       `resolved_type_args` mechanism). Inferred calls carry no `type_args`
       at all and MUST use this. Explicit calls also need it whenever the
       type args were resolved through aliases — e.g. `make(T)(..)` inside a
       generic body, where the alias T→int is checker-transient: codegen has
       no alias context, so re-mangling from the raw TypeNode would emit the
       abstract `make(T)` instead of the instantiated `make(int)`. The
       clone is checked fresh each instantiation, so this node starts NULL. */
    checker_stash_resolved_type_args(c, node, type_args, tp_count);

    /* Check trait bounds (if any) */
    {
        AstNode *tmpl = c->fn_templates[tmpl_idx].decl_node;
        TypeParamBound *bounds = tmpl->as.fn_decl.type_param_bounds;
        if (bounds) {
            bool bounds_ok = true;
            for (int ti = 0; ti < tp_count && bounds_ok; ti++) {
                for (int bi = 0; bi < bounds[ti].count; bi++) {
                    if (!checker_type_satisfies_trait(c, type_args[ti],
                                                      bounds[ti].trait_names[bi])) {
                        checker_error(c, node->line, node->column,
                            "type '%s' does not satisfy interface '%s' "
                            "(required by type parameter '%s' of '%s')",
                            type_name(type_args[ti]),
                            bounds[ti].trait_names[bi],
                            c->fn_templates[tmpl_idx].type_params[ti],
                            fn_name);
                        bounds_ok = false;
                        break;
                    }
                }
            }
            if (!bounds_ok) { free(type_args); *out_result = NULL; return true; }
        }
    }

    /* Build mangled name: "identity(int)". Bare `type_name` (not
       mangle_type_arg_name) — this site's pre-existing behavior,
       preserved (see resolve_type_node's pre-check comment); the
       module prefix is applied later via mangle_module_symbol on
       the codegen symbol, while this checker-internal cache key
       stays unprefixed. MangleBuf (Task 2.2) replaces the old
       fixed 512-byte buffer. */
    MangleBuf fb; mangle_buf_init(&fb);
    mangle_buf_append(&fb, fn_name);
    mangle_buf_append(&fb, "(");
    for (int ti = 0; ti < tp_count; ti++) {
        if (ti > 0) mangle_buf_append(&fb, ",");
        mangle_buf_append(&fb, type_name(type_args[ti]));
    }
    mangle_buf_append(&fb, ")");
    char *mangled = mangle_buf_take(&fb);

    /* Check if already instantiated (look up in scope) */
    Symbol *existing = scope_resolve(c->current_scope, mangled);
    if (existing) {
        /* Already instantiated — use existing type */
        node->as.call.callee->resolved_type = existing->type;
        /* Type-check arguments */
        Type *fn_t = existing->type;
        int argc = node->as.call.arg_count;
        int expected = fn_t->as.function.param_count;
        if (argc != expected) {
            checker_error(c, node->line, node->column,
                "'%s' expects %d argument(s), got %d", mangled, expected, argc);
            free(type_args);
            free(mangled);
            *out_result = NULL;
            return true;
        }
        for (int ai = 0; ai < argc; ai++) {
            Type *pt = fn_t->as.function.params[ai];
            /* Gap 2: auto-borrow + explicit `&x` for read-only `&T` params,
               matching the normal call path (type_assignable covers the
               `&T ← T` auto-borrow and widening). */
            fn_call_strip_amp_shell(node, ai, pt);
            checker_tag_user_from_list_literal(c, pt,
                node->as.call.args[ai], "argument list-literal");
            Type *saved_exp = c->expected_type;
            c->expected_type = pt;
            Type *at = check_expr(c, node->as.call.args[ai]);
            c->expected_type = saved_exp;
            if (at && pt && !type_assignable(pt, at)) {
                checker_error(c, node->as.call.args[ai]->line,
                    node->as.call.args[ai]->column,
                    "argument %d: expected '%s', got '%s'",
                    ai + 1, type_name(pt), type_name(at));
            }
        }
        *out_result = fn_t->as.function.return_type;
        free(type_args);
        free(mangled);
        return true;
    }

    /* Not yet instantiated — clone, substitute, type-check, push to pending */
    AstNode *tmpl_decl = c->fn_templates[tmpl_idx].decl_node;
    char **tp_names = c->fn_templates[tmpl_idx].type_params;

    /* Temporarily register type aliases (T→int, U→string, ...) */
    int saved_alias_count = c->type_alias_count;
    for (int ti = 0; ti < tp_count; ti++)
        register_type_alias(c, tp_names[ti], type_args[ti]);

    /* Resolve concrete param types and return type */
    int pc = tmpl_decl->as.fn_decl.param_count;
    Type **params = (Type **)malloc_safe((size_t)pc * sizeof(Type *));
    for (int pi = 0; pi < pc; pi++) {
        params[pi] = resolve_type_node(c, tmpl_decl->as.fn_decl.param_types[pi],
            node->line, node->column);
        if (!params[pi]) params[pi] = type_int(); /* fallback */
        /* Policy A: by-value array(T,N) params rejected on every path;
           reported at the instantiating call site (this file/line). */
        reject_array_by_value_param(c, params[pi],
            tmpl_decl->as.fn_decl.param_names[pi], node->line, node->column);
    }
    Type *ret = tmpl_decl->as.fn_decl.return_type
        ? resolve_type_node(c, tmpl_decl->as.fn_decl.return_type,
            node->line, node->column)
        : type_void();
    checker_reject_borrow_return(c, ret, NULL, node->line, node->column);  /* Phase 0/2: generic, defer */
    Type *fn_type = type_function(params, pc, ret, false);

    /* Register in scope so subsequent calls reuse */
    scope_define(c->current_scope, mangled, fn_type);

    /* Clone the fn body and type-check it */
    AstNode *cloned = ast_clone_deep(tmpl_decl);
    cloned->resolved_type = fn_type;
    cloned->as.fn_decl.type_param_count = 0; /* concrete now */
    cloned->as.fn_decl.type_param_bounds = NULL; /* don't double-free template bounds */

    chk_push_scope(c);
    for (int pi = 0; pi < pc; pi++) {
        /* Mirror the non-generic / method-generic body-param registration:
           unwrap a `&T` / `&!T` param to its pointee for the body-local
           symbol and flag the borrow. Without this the symbol carries the
           bare reference type, so field access resolves the object IDENT to
           `&Struct` and codegen takes the is_ref_value path (load + GEP on a
           struct value) instead of GEP-ing the borrow pointer directly —
           the Gap-2 codegen miscompile for generic free-function `&T`. */
        Type *sym_type = params[pi];
        bool is_borrow = false, is_mut_borrow = false;
        if (sym_type && sym_type->kind == TYPE_REFERENCE) {
            if (sym_type->is_mut) is_mut_borrow = true;
            else                  is_borrow = true;
            sym_type = sym_type->as.pointer_to;
        }
        Symbol *psym = scope_define(c->current_scope,
            cloned->as.fn_decl.param_names[pi], sym_type);
        if (psym) {
            psym->is_borrow = is_borrow;
            psym->is_mut_borrow = is_mut_borrow;
            /* F.2: an explicit Block param is a shallow-copy borrow; a bare
               type-param `T` that monomorphizes to Block is owned (moved). */
            if (sym_type && sym_type->kind == TYPE_BLOCK) {
                bool is_tparam = false;
                TypeNode *ptn = cloned->as.fn_decl.param_types
                                ? cloned->as.fn_decl.param_types[pi] : NULL;
                if (ptn && ptn->kind == TYPE_NODE_NAMED &&
                    ptn->as.named.arg_count == 0) {
                    for (int t = 0; t < tp_count; t++)
                        if (strcmp(ptn->as.named.name, tp_names[t]) == 0) {
                            is_tparam = true; break;
                        }
                }
                if (!is_tparam) psym->is_borrow = true;
            }
        }
    }
    Type *saved_ret = c->current_fn_return;
    c->current_fn_return = ret;
    check_stmt(c, cloned->as.fn_decl.body);
    checker_elide_last_use(c, cloned); /* A1 clone-elision */
    c->current_fn_return = saved_ret;
    chk_pop_scope(c);

    /* Restore type aliases */
    c->type_alias_count = saved_alias_count;

    /* Push to pending generic methods queue (reusing the same mechanism).
       A2: when this instantiation belongs to an imported module, prefix
       the symbol with "<modpath>__" (matching codegen's cg_module_fn_symbol
       and current_emit_module) so two modules' same-named generics get
       distinct LLVM symbols. The checker-internal cache key `mangled`
       stays unprefixed (each module has its own checker/scope). */
    char *owned_mangled = mangle_module_symbol(c->module_name, mangled);

    if (c->pending_gm_count >= c->pending_gm_cap) {
        c->pending_gm_cap = c->pending_gm_cap < 8 ? 8 : c->pending_gm_cap * 2;
        c->pending_generic_methods = realloc_safe(c->pending_generic_methods,
            (size_t)c->pending_gm_cap * sizeof(c->pending_generic_methods[0]));
    }
    int gm_idx = c->pending_gm_count++;
    c->pending_generic_methods[gm_idx].cloned_fn = cloned;
    c->pending_generic_methods[gm_idx].mangled_name = owned_mangled;
    c->pending_generic_methods[gm_idx].struct_type = NULL; /* not a method */

    /* Set callee resolved_type and check call arguments */
    node->as.call.callee->resolved_type = fn_type;
    int argc = node->as.call.arg_count;
    if (argc != pc) {
        checker_error(c, node->line, node->column,
            "'%s' expects %d argument(s), got %d", mangled, pc, argc);
    }
    for (int ai = 0; ai < argc && ai < pc; ai++) {
        Type *pt = params[ai];
        /* Gap 2: auto-borrow + explicit `&x` for read-only `&T` params,
           matching the normal call path. */
        fn_call_strip_amp_shell(node, ai, pt);
        checker_tag_user_from_list_literal(c, pt,
            node->as.call.args[ai], "argument list-literal");
        Type *saved_exp = c->expected_type;
        c->expected_type = pt;
        Type *at = check_expr(c, node->as.call.args[ai]);
        c->expected_type = saved_exp;
        if (at && pt && !type_assignable(pt, at)) {
            checker_error(c, node->as.call.args[ai]->line,
                node->as.call.args[ai]->column,
                "argument %d: expected '%s', got '%s'",
                ai + 1, type_name(pt), type_name(at));
        }
    }
    *out_result = ret;
    free(type_args);
    free(mangled); /* scope_define and mangle_module_symbol both copied */
    return true;
}

/* Shared call-site validation tail, run once callee_type has been resolved
   by any of the call forms above (instance/static method, generic free
   function, variant ctor, or a plain callee expression): non-function-type
   rejection, arity checking (vararg / param-defaults min_required), per-arg
   type checking (with the §13 `&x` auto-borrow shell strip and the
   from-list-literal tag), vararg arg resolution (with the C-2 Show-struct
   print rewrite), trailing param-default expansion, and the Phase 5.5 Step 4
   writable-borrow aliasing check. self_offset is 1 for an instance method
   call (implicit self occupies params[0]) and 0 otherwise — mirrors the
   original inline `is_method_call ? 1 : 0`. Returns the call's result type,
   or NULL on any checked error (matching the original 'result = ...; break;'
   at the end of check_expr_call's do-while — the caller here does exactly
   that with this function's return value). */
static Type *check_call_arguments(Checker *c, AstNode *node, Type *callee_type, int self_offset)
{
    /* Phase B: Block-typed callees use the same param/return layout as
       TYPE_FUNCTION (the only difference is ABI — codegen lowers as an
       indirect call through a fat pointer). The arity / arg-type checks
       below treat both kinds identically. */
    if (callee_type->kind != TYPE_FUNCTION &&
        callee_type->kind != TYPE_BLOCK)
    {
        checker_error(c, node->line, node->column,
                      "cannot call non-function type '%s'", type_name(callee_type));
        return NULL;
    }

    int expected = callee_type->as.function.param_count;
    int actual = node->as.call.arg_count;

    /* For instance method calls, the first param is the implicit self pointer.
       The user provides (expected - 1) arguments. */
    int user_expected = expected - self_offset;
    int param_offset = self_offset;

    /* Special case: print() requires at least 1 argument */
    if (callee_type->as.function.is_vararg && user_expected == 0 && actual == 0 && node->as.call.callee->kind == AST_IDENT && strcmp(node->as.call.callee->as.ident.name, "@print") == 0)
    {
        checker_error(c, node->line, node->column,
                      "@print() requires at least 1 argument");
        return NULL;
    }

    if (callee_type->as.function.is_vararg)
    {
        if (actual < user_expected)
        {
            checker_error(c, node->line, node->column,
                          "too few arguments: expected at least %d, got %d", user_expected, actual);
            return NULL;
        }
    }
    else
    {
        /* Param defaults (档1): trailing params with a default may be omitted.
           min_required = count of user params without a default. */
        int min_required = user_expected;
        if (callee_type->as.function.param_defaults)
        {
            min_required = 0;
            for (int i = param_offset; i < expected; i++)
                if (callee_type->as.function.param_defaults[i] == NULL)
                    min_required++;
        }
        if (actual < min_required || actual > user_expected)
        {
            if (min_required == user_expected)
                checker_error(c, node->line, node->column,
                              "wrong number of arguments: expected %d, got %d",
                              user_expected, actual);
            else
                checker_error(c, node->line, node->column,
                              "wrong number of arguments: expected %d..%d, got %d",
                              min_required, user_expected, actual);
            return NULL;
        }
    }

    /* Check argument types for non-vararg params (skip self param for instance methods) */
    bool args_ok = true;

    /* LS uses clone semantics: struct/string arguments are deep-copied on every call.
       No move tracking needed — the caller retains ownership of its variables. */
    for (int i = 0; i < user_expected && i < actual; i++)
    {
        /* Phase B closure: propagate the declared param type as expected_type
           so a Ruby-style closure literal (`|x| body`) at this position can
           infer its untyped params from the callee's `Block(...)` signature. */
        Type *param_type = callee_type->as.function.params[i + param_offset];
        /* §13: explicit `&x` / `&obj.field` argument to a read-only `&T`
           parameter — strip the address-of shell so the call takes the
           proven auto-borrow path (identical to passing the lvalue bare).
           Without this, `&x` types as a raw `*T` and mismatches the `&T`
           formal. A field operand (`&self.value`) is the read-only twin of
           the `&!self.value` field borrow (AST_MUT_BORROW) — it lends a
           read-only `&T` of the field, zero-copy. Writable borrows stay
           explicit `&!x` (AST_MUT_BORROW — untouched here). */
        {
            AstNode *argn = node->as.call.args[i];
            if (param_type && param_type->kind == TYPE_REFERENCE &&
                !param_type->is_mut &&
                argn->kind == AST_UNARY && argn->as.unary.op == TOKEN_AMP &&
                (argn->as.unary.operand->kind == AST_IDENT ||
                 argn->as.unary.operand->kind == AST_FIELD ||
                 argn->as.unary.operand->kind == AST_INDEX))
            {
                /* shell intentionally leaked, same as the index-protocol rewrite */
                node->as.call.args[i] = argn->as.unary.operand;
            }
        }
        /* Array-literal argument to a user-container param (Vec etc. with
           __from_list): tag it so codegen emits the from_list value, just
           like the var-decl / struct-field positions. Lets `f(["a","b"])`
           work where f takes Vec(Str), not only `Vec(Str) v=[..]; f(v)`.
           Self-guarded: no-op unless param is a from_list struct and the
           arg is an array literal. */
        checker_tag_user_from_list_literal(c, param_type,
            node->as.call.args[i], "argument list-literal");
        Type *saved_exp = c->expected_type;
        c->expected_type = param_type;
        Type *arg_type = check_expr(c, node->as.call.args[i]);
        c->expected_type = saved_exp;
        if (arg_type == NULL)
        {
            args_ok = false;
            continue;
        }
        if (!type_assignable(param_type, arg_type))
        {
            checker_error(c, node->as.call.args[i]->line, node->as.call.args[i]->column,
                          "argument %d: expected '%s', got '%s'",
                          i + 1,
                          type_name(param_type),
                          type_name(arg_type));
            args_ok = false;
        }
    }
    /* Check vararg args (just resolve types, no checking) */
    bool is_print_call = node->as.call.callee->kind == AST_IDENT &&
                         strcmp(node->as.call.callee->as.ident.name, "@print") == 0;
    for (int i = user_expected; i < actual; i++)
    {
        Type *at = check_expr(c, node->as.call.args[i]);
        /* C-2: print(x) for a Show struct/enum renders via Show — rewrite the
           arg to to_str(x) (Str), which print prints as raw text. */
        if (is_print_call && type_is_show_aggregate(c, at))
        {
            wrap_arg_in_to_str(&node->as.call.args[i]);
            check_expr(c, node->as.call.args[i]);
        }
    }

    /* Param defaults (档1): append cloned default exprs for omitted trailing
       params so codegen sees a complete arg list (no codegen changes).
       Idempotent: after appending, arg_count == user_expected. */
    if (args_ok && callee_type->as.function.param_defaults && actual < user_expected)
    {
        node->as.call.args = (AstNode **)realloc_safe(
            node->as.call.args, (size_t)user_expected * sizeof(AstNode *));
        for (int i = actual; i < user_expected; i++)
        {
            AstNode *pd = (AstNode *)callee_type->as.function.param_defaults[i + param_offset];
            AstNode *clone = ast_clone_deep(pd);
            Type *pt = callee_type->as.function.params[i + param_offset];
            Type *se = c->expected_type;
            if (pt && (pt->kind == TYPE_STRUCT || pt->kind == TYPE_BLOCK))
                c->expected_type = pt;
            check_expr(c, clone);
            c->expected_type = se;
            node->as.call.args[i] = clone;
        }
        node->as.call.arg_count = user_expected;
        actual = user_expected;
    }

    /* Phase 5.5 Step 4 — call-site aliasing check for writable borrows.
       Forbid passing the same variable in any of these conflicting combinations
       at a single call:
         f(&!x, &!x)   — two writable aliases
         f(&!x, x)     — writable + read-only auto-borrow (x -> &string param)
         f(&!x, &x)    — same, with explicit & (rare)
       Only check pairs where at least one side is a writable borrow. */
    if (args_ok)
    {
        int n = user_expected < actual ? user_expected : actual;
        for (int i = 0; i < n; i++)
        {
            AstNode *ai = node->as.call.args[i];
            if (ai == NULL || ai->kind != AST_MUT_BORROW) continue;
            AstNode *op_i = ai->as.mut_borrow.operand;
            if (op_i == NULL || op_i->kind != AST_IDENT) continue;
            const char *name_i = op_i->as.ident.name;

            for (int j = i + 1; j < n; j++)
            {
                AstNode *aj = node->as.call.args[j];
                if (aj == NULL) continue;

                const char *name_j = NULL;
                const char *j_kind = NULL;
                if (aj->kind == AST_MUT_BORROW &&
                    aj->as.mut_borrow.operand &&
                    aj->as.mut_borrow.operand->kind == AST_IDENT)
                {
                    name_j = aj->as.mut_borrow.operand->as.ident.name;
                    j_kind = "another writable borrow";
                }
                else if (aj->kind == AST_IDENT)
                {
                    /* Only flag when the other arg binds to a parameter that
                       shares state (&T/&!T, or string-by-value which clones
                       at runtime — BUT cloning happens AFTER the writable
                       borrow is already holding the pointer; ordering of
                       eval is left-to-right so later by-value clone would
                       see a possibly-mutated snapshot, which is confusing.
                       Conservatively flag TYPE_REFERENCE on the other side. */
                    Type *pj = callee_type->as.function.params[j + param_offset];
                    if (pj && pj->kind == TYPE_REFERENCE)
                    {
                        name_j = aj->as.ident.name;
                        j_kind = "read-only borrow";
                    }
                }
                if (name_j && strcmp(name_i, name_j) == 0)
                {
                    checker_error(c, aj->line, aj->column,
                                  "variable '%s' is already passed as writable borrow "
                                  "at argument %d; cannot also pass as %s here",
                                  name_i, i + 1, j_kind);
                    args_ok = false;
                }
            }
        }
    }

    return args_ok ? callee_type->as.function.return_type : NULL;
}

/* S3b: extracted verbatim from the check_expr AST_CALL case. The do/while(0)
   wrapper lets the original switch-level `break;` statements fall through to
   `return result;` unchanged (this case contains no loops that use break). */
Type *check_expr_call(Checker *c, AstNode *node)
{
    Type *result = NULL;
    do {
        /* Slice builtin `s.len()` — the borrowed view's element count. Intercept
           before struct/method dispatch (slices are not structs). */
        if (node->as.call.callee && node->as.call.callee->kind == AST_FIELD &&
            node->as.call.callee->as.field_access.field &&
            strcmp(node->as.call.callee->as.field_access.field, "len") == 0)
        {
            Type *recv = check_expr(c, node->as.call.callee->as.field_access.object);
            if (recv && recv->kind == TYPE_SLICE)
            {
                if (node->as.call.arg_count != 0)
                    checker_error(c, node->line, node->column,
                                  "slice 'len' takes no arguments");
                result = type_int();
                node->resolved_type = result;
                break;
            }
        }

        /* A-1: canonical-path call to a std.c primitive — std.c.malloc/realloc/
           free/abort. Resolved by spelling (not via a local import), so it works
           inside generic method bodies re-checked at the consumer site. Validate
           args against the fixed signatures; codegen recognises the same callee
           shape and lowers it. See docs/plan_runtime_primitives.md §5. */
        {
            int prim = match_stdc_prim(c, node->as.call.callee);
            if (prim >= 0)
            {
                /* Fixed signatures: malloc(i64)->*u8, realloc(*u8,i64)->*u8,
                   free(*u8)->void, abort()->void. */
                int want_argc = (prim == 0) ? 1 : (prim == 1) ? 2 : (prim == 2) ? 1 : 0;
                const char *pname = (prim == 0) ? "malloc" : (prim == 1) ? "realloc"
                                  : (prim == 2) ? "free" : "abort";
                int argc = node->as.call.arg_count;
                if (argc != want_argc)
                {
                    checker_error(c, node->line, node->column,
                                  "std.sys.c.%s expects %d argument(s), got %d",
                                  pname, want_argc, argc);
                    result = NULL;
                    break;
                }
                /* Resolve arg types (loose: these are raw-pointer/size primitives,
                   mirroring the old builtins which did no strict arg checking). */
                for (int ai = 0; ai < argc; ai++)
                    (void)check_expr(c, node->as.call.args[ai]);
                Type *ret = (prim == 0 || prim == 1) ? type_pointer(type_u8())
                                                     : type_void();
                /* Build the fn type so callee->resolved_type is well-formed. */
                Type *fty;
                if (prim == 0) { Type **p = malloc_safe(sizeof(Type*)); p[0]=type_i64();
                                 fty = type_function(p,1,type_pointer(type_u8()),false); }
                else if (prim == 1) { Type **p = malloc_safe(2*sizeof(Type*));
                                 p[0]=type_pointer(type_u8()); p[1]=type_i64();
                                 fty = type_function(p,2,type_pointer(type_u8()),false); }
                else if (prim == 2) { Type **p = malloc_safe(sizeof(Type*));
                                 p[0]=type_pointer(type_u8());
                                 fty = type_function(p,1,type_void(),false); }
                else { fty = type_function(NULL,0,type_void(),false); }
                node->as.call.callee->resolved_type = fty;
                result = ret;
                break;
            }
        }

        /* Phase 1 (docs/plan_module_fn_resolution.md): canonical module-path call
           `mod.path.fn(...)` (no alias). Collapse the prefix into a single IDENT so
           the normal module-call path resolves + emits it. Mutates callee in place;
           fall through to normal handling (which now sees an alias-shaped callee). */
        rewrite_canonical_module_call(c, node->as.call.callee);

        /* G2: generic free-function call — see check_call_generic_free_fn. */
        {
            Type *gf_result = NULL;
            if (check_call_generic_free_fn(c, node, &gf_result))
            {
                result = gf_result;
                break;
            }
        }

        /* Polymorphic built-in math dispatch: math.abs/min/max accept either
           int or float and pick the appropriate LLVM intrinsic at codegen.
           We intercept early to set the call's resolved_type to the args'
           common numeric type instead of the placeholder f64-only signature
           in the math module's exports.

           Only fires when obj_node is a bare identifier that resolves
           directly to a built-in math module in scope. This guard avoids
           triggering check_expr on struct type names (e.g. Point.origin())
           which would otherwise produce spurious "undefined variable" errors. */
        if (node->as.call.callee->kind == AST_FIELD &&
            node->as.call.callee->as.field_access.object->kind == AST_IDENT)
        {
            AstNode *obj_node = node->as.call.callee->as.field_access.object;
            const char *fn_name = node->as.call.callee->as.field_access.field;
            Symbol *mod_sym = scope_resolve(c->current_scope,
                                            obj_node->as.ident.name);
            Type *obj_type = mod_sym ? mod_sym->type : NULL;
            if (obj_type && obj_type->kind == TYPE_MODULE &&
                obj_type->as.module.is_builtin && obj_type->as.module.name &&
                strcmp(obj_type->as.module.name, "std.core.math") == 0)
            {
                /* Pin the obj IDENT's resolved_type so codegen sees a module
                   reference (matches what check_expr on the IDENT would do). */
                obj_node->resolved_type = obj_type;
                int arity = 0;
                MathPolyKind poly = MATH_POLY_NONE;
                if (builtin_math_lookup_fn(fn_name, &arity, NULL, NULL, &poly, NULL) &&
                    poly == MATH_POLY_INT_OR_FLOAT)
                {
                    int argc = node->as.call.arg_count;
                    if (argc != arity)
                    {
                        checker_error(c, node->line, node->column,
                                      "math.%s expects %d argument(s), got %d",
                                      fn_name, arity, argc);
                        result = NULL;
                        node->as.call.callee->resolved_type = NULL;
                        break;
                    }
                    Type *t0 = check_expr(c, node->as.call.args[0]);
                    Type *common = t0;
                    if (arity == 2)
                    {
                        Type *t1 = check_expr(c, node->as.call.args[1]);
                        common = type_numeric_common(t0, t1);
                        if (common == NULL)
                        {
                            checker_error(c, node->line, node->column,
                                          "math.%s arguments have incompatible numeric types '%s' and '%s'",
                                          fn_name, type_name(t0), type_name(t1));
                            result = NULL;
                            break;
                        }
                    }
                    if (common == NULL || !type_is_numeric(common))
                    {
                        checker_error(c, node->line, node->column,
                                      "math.%s requires numeric argument(s), got '%s'",
                                      fn_name, type_name(t0));
                        result = NULL;
                        break;
                    }
                    /* Set callee resolved_type to mirror the dispatched signature
                       so codegen widening at call site sees the right param types. */
                    Type **params = (Type **)malloc_safe((size_t)arity * sizeof(Type *));
                    for (int k = 0; k < arity; k++) params[k] = common;
                    Type *fn_t = type_function(params, arity, common, false);
                    node->as.call.callee->resolved_type = fn_t;
                    result = common;
                    break;
                }
            }
            /* Built-in `perf` module: arity check + fixed return type (no polymorphism). */
            if (obj_type && obj_type->kind == TYPE_MODULE &&
                obj_type->as.module.is_builtin && obj_type->as.module.name &&
                strcmp(obj_type->as.module.name, "perf") == 0)
            {
                obj_node->resolved_type = obj_type;
                int arity = 0;
                PerfEmitKind perf_kind = PERF_EMIT_NOW;
                if (builtin_perf_lookup_fn(fn_name, &arity, &perf_kind))
                {
                    int argc = node->as.call.arg_count;
                    if (argc != arity)
                    {
                        checker_error(c, node->line, node->column,
                                      "perf.%s expects %d argument(s), got %d",
                                      fn_name, arity, argc);
                        result = NULL;
                        node->as.call.callee->resolved_type = NULL;
                        break;
                    }
                    for (int k = 0; k < argc; k++)
                        check_expr(c, node->as.call.args[k]);
                    Type *ret_t = (perf_kind == PERF_EMIT_ELAPSED_MS ||
                                   perf_kind == PERF_EMIT_ELAPSED_S)
                                  ? type_f64() : type_i64();
                    Type *arg_t = type_i64();
                    Type **params = NULL;
                    if (arity > 0) {
                        params = (Type **)malloc_safe((size_t)arity * sizeof(Type *));
                        for (int k = 0; k < arity; k++) params[k] = arg_t;
                    }
                    node->as.call.callee->resolved_type =
                        type_function(params, arity, ret_t, false);
                    result = ret_t;
                    break;
                }
            }
        }

        /* Detect struct method calls: obj.method(args) or StructName.method(args) */
        bool is_method_call = false; /* instance method call: auto-pass self */
        bool is_static_call = false; /* static method call via type or instance */
        const char *method_struct = NULL;

        if (node->as.call.callee->kind == AST_FIELD)
        {
            AstNode *obj_node = node->as.call.callee->as.field_access.object;
            const char *method_name = node->as.call.callee->as.field_access.field;

            /* L-002: interface-qualified call `Iface.method(recv, args...)`
               rewrite — see check_call_interface_qualified. */
            if (check_call_interface_qualified(c, node, &obj_node, method_name))
            {
                result = NULL;
                break;
            }

            /* Static-by-typename detection (case B / case ③ rewrites + the four
               `StructName.m()` / `EnumName.m()` / type-alias-param / builtin-
               primitive checks) — see check_call_static_method. */
            if (check_call_static_method(c, node, &obj_node, method_name,
                                          &is_static_call, &method_struct))
            {
                result = NULL;
                break;
            }

                /* If not a struct-type static call, resolve the object expression */
            if (!is_static_call)
            {
                Type *obj_type = check_expr(c, obj_node);

                /* C1: Option/Result combinators are compiler-lowered (rewrite this
                   AST_CALL in place to force-unwrap / match, then re-check). Runs
                   before the generic enum method dispatch, which has no methods for
                   the builtin Option/Result templates. */
                if (obj_type && obj_type->kind == TYPE_ENUM && obj_type->as.enom.name &&
                    (strncmp(obj_type->as.enom.name, "Option(", 7) == 0 ||
                     strncmp(obj_type->as.enom.name, "Result(", 7) == 0))
                {
                    Type *oc_ty = NULL;
                    int lo = lower_opt_combinator(c, node, obj_node, obj_type,
                                                  method_name, &oc_ty);
                    if (lo == 1) { result = oc_ty; break; }
                    if (lo < 0)  { result = NULL; break; }
                    /* lo == 0: not a combinator → fall through to normal dispatch. */
                }

                /* Check if obj is an instance of a struct */
                if (obj_type)
                {
                    /* Auto-deref a pointer (*T) or reference (&T / &!T) receiver
                       to its pointee struct/enum, so a method call whose receiver
                       is a borrow-returning call result — e.g.
                       `v.get_ref(i).eq?(x)` where get_ref returns &T — dispatches
                       as an instance method (self auto-passed). Without unwrapping
                       the reference, `deref` stays TYPE_REFERENCE, neither the
                       struct/enum nor the builtin branch fires, is_method_call
                       stays false, and the call falls through to the generic
                       field-access path (line 4259) which counts self as an
                       explicit argument → "wrong number of arguments: expected N,
                       got N-1". Mirrors the AST_FIELD auto-deref (it already
                       unwraps a reference result for field/method access). */
                    Type *deref = obj_type;
                    if ((deref->kind == TYPE_POINTER ||
                         deref->kind == TYPE_REFERENCE) && deref->as.pointer_to &&
                        (deref->as.pointer_to->kind == TYPE_STRUCT ||
                         deref->as.pointer_to->kind == TYPE_ENUM))
                    {
                        deref = deref->as.pointer_to;
                    }
                    if (deref->kind == TYPE_STRUCT && deref->as.strukt.name)
                    {
                        const char *ds_key = impl_key_of_type(deref);  /* B-4.1 */
                        int si = method_is_static(c, ds_key, method_name);
                        /* VR-LIM-018: consumer met an imported generic
                           instantiation never registered locally — register its
                           impl methods on demand from the stamped metadata. */
                        if (si < 0 && deref->as.strukt.generic_base) {
                            ensure_generic_struct_impls_local(c, deref);
                            si = method_is_static(c, ds_key, method_name);
                        }
                        if (si == 0)
                        {
                            /* Phase A1: gate method calls on struct borrows by the
                               method's declared self-borrow kind.
                                 method sbk == 0 (legacy implicit) → mut self required
                                 method sbk == 1 (&self)           → any borrow OK
                                 method sbk == 2 (&!self)          → mut self required */
                            if (obj_node->kind == AST_IDENT)
                            {
                                Symbol *bsym = scope_resolve(c->current_scope,
                                                             obj_node->as.ident.name);
                                if (bsym && (bsym->is_borrow || bsym->is_mut_borrow))
                                {
                                    int msbk = method_self_borrow_kind(c,
                                        ds_key, method_name);  /* B-4.1 */
                                    if (msbk == 0)
                                    {
                                        /* Legacy method: cannot be called on any borrow.
                                           Encourage migration to &self/&!self. */
                                        checker_move_error(c, node->line, node->column,
                                            "cannot call method '%s.%s()' on '%s': "
                                            "method has no self-borrow annotation; "
                                            "declare it as 'def %s(&self ...)' or "
                                            "'def %s(&!self ...)' to allow calling on borrows",
                                            deref->as.strukt.name, method_name,
                                            obj_node->as.ident.name,
                                            method_name, method_name);
                                        result = NULL;
                                        break;
                                    }
                                    if (msbk == 2 && bsym->is_borrow)
                                    {
                                        /* &!self method on read-only borrow → reject */
                                        checker_move_error(c, node->line, node->column,
                                            "cannot call '%s.%s(&!self)' on '%s': "
                                            "method requires writable self, but "
                                            "'%s' is a read-only borrow",
                                            deref->as.strukt.name, method_name,
                                            obj_node->as.ident.name,
                                            obj_node->as.ident.name);
                                        result = NULL;
                                        break;
                                    }
                                    /* msbk == 1 (&self) → both borrow kinds OK
                                       msbk == 2 + mut_borrow obj → OK */
                                }
                            }
                            /* Instance method — auto self */
                            is_method_call = true;
                            method_struct = ds_key;  /* B-4.1 */
                        }
                        else if (si == 1)
                        {
                            /* Static method called via instance — allowed, ignore obj */
                            is_static_call = true;
                            method_struct = ds_key;  /* B-4.1 */
                        }
                    }

                    /* Check if obj is an instance of an enum */
                    if (deref->kind == TYPE_ENUM && deref->as.enom.name)
                    {
                        const char *de_key = impl_key_of_type(deref);  /* B-4.1 */
                        int si = method_is_static(c, de_key, method_name);
                        if (si == 0)
                        {
                            if (obj_node->kind == AST_IDENT)
                            {
                                Symbol *bsym = scope_resolve(c->current_scope,
                                                             obj_node->as.ident.name);
                                if (bsym && (bsym->is_borrow || bsym->is_mut_borrow))
                                {
                                    int msbk = method_self_borrow_kind(c,
                                        de_key, method_name);  /* B-4.1 */
                                    if (msbk == 0)
                                    {
                                        checker_move_error(c, node->line, node->column,
                                            "cannot call method '%s.%s()' on '%s': "
                                            "method has no self-borrow annotation; "
                                            "declare it as 'def %s(&self ...)' or "
                                            "'def %s(&!self ...)' to allow calling on borrows",
                                            deref->as.enom.name, method_name,
                                            obj_node->as.ident.name,
                                            method_name, method_name);
                                        result = NULL;
                                        break;
                                    }
                                    if (msbk == 2 && bsym->is_borrow)
                                    {
                                        checker_move_error(c, node->line, node->column,
                                            "cannot call '%s.%s(&!self)' on '%s': "
                                            "method requires writable self, but "
                                            "'%s' is a read-only borrow",
                                            deref->as.enom.name, method_name,
                                            obj_node->as.ident.name,
                                            obj_node->as.ident.name);
                                        result = NULL;
                                        break;
                                    }
                                }
                            }
                            is_method_call = true;
                            method_struct = de_key;  /* B-4.1 */
                        }
                        else if (si == 1)
                        {
                            is_static_call = true;
                            method_struct = de_key;  /* B-4.1 */
                        }
                    }

                    /* Step 11: check impl_registry for builtin types (int, f64, bool, ...) */
                    if (!is_method_call && !is_static_call)
                    {
                        const char *impl_name = type_impl_name(obj_type);
                        if (impl_name)
                        {
                            int si = method_is_static(c, impl_name, method_name);
                            if (si == 0)
                            {
                                is_method_call = true;
                                method_struct = impl_name;
                            }
                            else if (si == 1)
                            {
                                is_static_call = true;
                                method_struct = impl_name;
                            }
                        }
                    }
                }
            }
        }

        /* Resolve callee type */
        Type *callee_type = NULL;
        if (is_method_call || is_static_call)
        {
            const char *method_name = node->as.call.callee->as.field_access.field;

            /* Interface-overload selection / plain find_method / method-level
               generic instantiation / explicit __drop() rejection — see
               check_call_instance_method. */
            if (check_call_instance_method(c, node, is_method_call, is_static_call,
                                            method_struct, method_name, &callee_type))
            {
                result = NULL;
                break;
            }
        }
        else
        {
            /* Variant ctor short-circuit: callee is an IDENT matching a registered
               enum variant.  Handles `RGB(1,2,3)`, `Some(x)`, etc. */
            {
                Type *vc_result = NULL;
                if (check_call_variant_ctor(c, node, &vc_result))
                {
                    result = vc_result;
                    break;
                }
            }

            callee_type = check_expr(c, node->as.call.callee);

            /* Check builtin functions before checking function type */
            if (callee_type == NULL && node->as.call.callee->kind == AST_IDENT)
            {
                result = check_builtin_call(c, node->as.call.callee->as.ident.name, node);
                if (result != NULL)
                    break;
            }
        }
        if (callee_type == NULL)
        {
            result = NULL;
            break;
        }

        /* Shared call-site validation tail (arity, per-arg types, varargs,
           param defaults, writable-borrow aliasing) — see
           check_call_arguments. */
        result = check_call_arguments(c, node, callee_type, is_method_call ? 1 : 0);
        break;
    } while (0);
    return result;
}

/* S3b: check_expr big cases extracted as static helpers. Each is the verbatim
   case body wrapped in do/while(0) so switch-level `break;` flows to
   `return result;` unchanged (no case below uses break for an inner loop). */
