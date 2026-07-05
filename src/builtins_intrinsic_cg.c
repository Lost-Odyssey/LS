/* builtins_intrinsic_cg.c — registry + emitters for the AST_CALL compiler
 * intrinsics (sync / atomic / simd / bytecopy). See the header for the
 * "adding an intrinsic" how-to and the dispatch contract.
 *
 * ZERO-BEHAVIOR TRANSPLANT (S2 Phase 1): every emit body below is moved
 * verbatim from codegen_expr.c — instruction order, result-label strings and
 * error texts are load-bearing (emit-ir byte-diff against the pre-refactor
 * baselines is the acceptance oracle). Do not "clean up" emission code here
 * without re-running the intrinsic smoke corpora diffs.
 */
#include "builtins_intrinsic_cg.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <llvm-c/Core.h>

#include "codegen_internal.h"

/* ============================== lookup ================================== */

/* Exact-name rows (sync family). Prefix families (atomic/simd) are handled
   as branches in builtin_intrinsic_lookup below. */
static const struct {
    const char *name;
    IntrinKind kind;
} sync_names[] = {
    { "__mutex_init",      INTRIN_MUTEX_INIT },
    { "__mutex_lock",      INTRIN_MUTEX_LOCK },
    { "__mutex_trylock",   INTRIN_MUTEX_TRYLOCK },
    { "__mutex_unlock",    INTRIN_MUTEX_UNLOCK },
    { "__mutex_destroy",   INTRIN_MUTEX_DESTROY },
    { "__rwlock_init",     INTRIN_RWLOCK_INIT },
    { "__rwlock_rdlock",   INTRIN_RWLOCK_RDLOCK },
    { "__rwlock_wrlock",   INTRIN_RWLOCK_WRLOCK },
    { "__rwlock_rdunlock", INTRIN_RWLOCK_RDUNLOCK },
    { "__rwlock_wrunlock", INTRIN_RWLOCK_WRUNLOCK },
    { "__rwlock_destroy",  INTRIN_RWLOCK_DESTROY },
    { "__cond_init",       INTRIN_COND_INIT },
    { "__cond_wait",       INTRIN_COND_WAIT },
    { "__cond_signal",     INTRIN_COND_SIGNAL },
    { "__cond_broadcast",  INTRIN_COND_BROADCAST },
    { "__cond_destroy",    INTRIN_COND_DESTROY },
    { "__cpu_relax",       INTRIN_CPU_RELAX },
    { "__cpu_yield",       INTRIN_CPU_YIELD },
};

IntrinKind builtin_intrinsic_lookup(const char *name)
{
    if (name == NULL || name[0] != '_')
        return INTRIN_NONE;

    /* sync family: exact names under three prefixes + two bare cpu hints.
       The prefix check decides family membership (an unknown __mutex_xyz is
       consumed as INTRIN_SYNC_UNKNOWN, same as the old inline chain). */
    if (strncmp(name, "__mutex_", 8) == 0 ||
        strncmp(name, "__rwlock_", 9) == 0 ||
        strncmp(name, "__cond_", 7) == 0 ||
        strcmp(name, "__cpu_relax") == 0 ||
        strcmp(name, "__cpu_yield") == 0)
    {
        for (size_t i = 0; i < sizeof(sync_names) / sizeof(sync_names[0]); i++)
            if (strcmp(name, sync_names[i].name) == 0)
                return sync_names[i].kind;
        return INTRIN_SYNC_UNKNOWN;
    }

    /* atomic family: exact rows first, then the load/store PREFIX rows that
       absorb the ordering suffixes (_acquire/_release/_relaxed). Precedence-
       equivalent to the retired chain: no exact row is a load/store prefix
       extension, and the two prefixes are mutually exclusive. */
    if (strncmp(name, "__atomic_", 9) == 0)
    {
        if (strcmp(name, "__atomic_fence") == 0) return INTRIN_ATOMIC_FENCE;
        if (strcmp(name, "__atomic_add") == 0)   return INTRIN_ATOMIC_ADD;
        if (strcmp(name, "__atomic_sub") == 0)   return INTRIN_ATOMIC_SUB;
        if (strcmp(name, "__atomic_swap") == 0)  return INTRIN_ATOMIC_SWAP;
        if (strcmp(name, "__atomic_cas") == 0)   return INTRIN_ATOMIC_CAS;
        if (strncmp(name, "__atomic_load", 13) == 0)  return INTRIN_ATOMIC_LOAD;
        if (strncmp(name, "__atomic_store", 14) == 0) return INTRIN_ATOMIC_STORE;
        return INTRIN_ATOMIC_UNKNOWN;
    }

    /* simd family: exact names only ("__simd_load" vs "__simd_load_masked"
       are distinct exact rows — strcmp, no prefix shadowing). */
    if (strncmp(name, "__simd_", 7) == 0)
    {
        static const struct { const char *name; IntrinKind kind; } rows[] = {
            { "__simd_zero",         INTRIN_SIMD_ZERO },
            { "__simd_splat",        INTRIN_SIMD_SPLAT },
            { "__simd_lane",         INTRIN_SIMD_LANE },
            { "__simd_fma",          INTRIN_SIMD_FMA },
            { "__simd_max",          INTRIN_SIMD_MAX },
            { "__simd_min",          INTRIN_SIMD_MIN },
            { "__simd_reduce_add",   INTRIN_SIMD_REDUCE_ADD },
            { "__simd_reduce_max",   INTRIN_SIMD_REDUCE_MAX },
            { "__simd_reduce_min",   INTRIN_SIMD_REDUCE_MIN },
            { "__simd_load",         INTRIN_SIMD_LOAD },
            { "__simd_store",        INTRIN_SIMD_STORE },
            { "__simd_load_masked",  INTRIN_SIMD_LOAD_MASKED },
            { "__simd_store_masked", INTRIN_SIMD_STORE_MASKED },
            { "__simd_cast",         INTRIN_SIMD_CAST },
            { "__simd_floor",        INTRIN_SIMD_FLOOR },
            { "__simd_bitcast",      INTRIN_SIMD_BITCAST },
        };
        for (size_t i = 0; i < sizeof(rows) / sizeof(rows[0]); i++)
            if (strcmp(name, rows[i].name) == 0)
                return rows[i].kind;
        return INTRIN_SIMD_UNKNOWN;
    }

    /* module-qualified: only ever dispatched from the module-call site
       (std.sys.c guard lives there); never consumed at the bare-ident probe. */
    if (strcmp(name, "__ls_bytecopy") == 0)
        return INTRIN_BYTECOPY;

    return INTRIN_NONE;
}

bool builtin_intrinsic_is_global(IntrinKind kind)
{
    return kind != INTRIN_NONE && kind != INTRIN_BYTECOPY;
}

/* ============================ sync family =============================== */

/* One row per runtime call: OS-backend symbol, return type, opaque-pointer
   arg count. Mirrors the retired if-chain in codegen_expr.c exactly. */
typedef enum { SYNC_RET_VOID, SYNC_RET_PTR, SYNC_RET_I32 } SyncRet;

static const struct {
    IntrinKind kind;
    const char *sym;
    SyncRet ret;
    int nargs;
} sync_rows[] = {
    { INTRIN_MUTEX_INIT,      "ls_mutex_init",      SYNC_RET_PTR,  0 },
    { INTRIN_MUTEX_LOCK,      "ls_mutex_lock",      SYNC_RET_I32,  1 },
    { INTRIN_MUTEX_TRYLOCK,   "ls_mutex_trylock",   SYNC_RET_I32,  1 },
    { INTRIN_MUTEX_UNLOCK,    "ls_mutex_unlock",    SYNC_RET_I32,  1 },
    { INTRIN_MUTEX_DESTROY,   "ls_mutex_destroy",   SYNC_RET_VOID, 1 },
    { INTRIN_RWLOCK_INIT,     "ls_rwlock_init",     SYNC_RET_PTR,  0 },
    { INTRIN_RWLOCK_RDLOCK,   "ls_rwlock_rdlock",   SYNC_RET_I32,  1 },
    { INTRIN_RWLOCK_WRLOCK,   "ls_rwlock_wrlock",   SYNC_RET_I32,  1 },
    { INTRIN_RWLOCK_RDUNLOCK, "ls_rwlock_rdunlock", SYNC_RET_I32,  1 },
    { INTRIN_RWLOCK_WRUNLOCK, "ls_rwlock_wrunlock", SYNC_RET_I32,  1 },
    { INTRIN_RWLOCK_DESTROY,  "ls_rwlock_destroy",  SYNC_RET_VOID, 1 },
    /* condition variables (std.chan). __cond_wait is the only 2-arg sync
       intrinsic (cond handle, mutex handle — both opaque pointers). */
    { INTRIN_COND_INIT,       "ls_cond_init",       SYNC_RET_PTR,  0 },
    { INTRIN_COND_WAIT,       "ls_cond_wait",       SYNC_RET_VOID, 2 },
    { INTRIN_COND_SIGNAL,     "ls_cond_signal",     SYNC_RET_VOID, 1 },
    { INTRIN_COND_BROADCAST,  "ls_cond_broadcast",  SYNC_RET_VOID, 1 },
    { INTRIN_COND_DESTROY,    "ls_cond_destroy",    SYNC_RET_VOID, 1 },
    { INTRIN_CPU_RELAX,       "ls_cpu_relax",       SYNC_RET_VOID, 0 },
    { INTRIN_CPU_YIELD,       "ls_cpu_yield",       SYNC_RET_VOID, 0 },
};

/* Mutex + spin runtime intrinsics (std.sync). Emit a call to the OS-backend
   runtime function on an opaque handle. These are GLOBAL intrinsics (not
   import aliases), so — like __task_* — they survive generic-method
   instantiation in a consumer module that hasn't imported std.c. They know
   nothing about Mutex(T): an opaque handle in/out is the whole interface
   (the same clean boundary as __atomic_* over scalars). */
static LLVMValueRef intrin_sync_emit(CodegenContext *ctx, const char *mname,
                                     IntrinKind kind, AstNode *node)
{
    LLVMTypeRef ptr_t = LLVMPointerTypeInContext(ctx->context, 0);
    LLVMTypeRef void_t = LLVMVoidTypeInContext(ctx->context);
    LLVMTypeRef i32_t = LLVMInt32TypeInContext(ctx->context);

    const char *sym = NULL;
    LLVMTypeRef ret_t = void_t;
    int nargs = 1; /* default: one opaque handle argument */
    for (size_t i = 0; i < sizeof(sync_rows) / sizeof(sync_rows[0]); i++)
    {
        if (sync_rows[i].kind != kind)
            continue;
        sym = sync_rows[i].sym;
        ret_t = sync_rows[i].ret == SYNC_RET_PTR ? ptr_t
              : sync_rows[i].ret == SYNC_RET_I32 ? i32_t : void_t;
        nargs = sync_rows[i].nargs;
        break;
    }
    if (sym == NULL)
    {
        cg_error(ctx, node->line, node->column,
                 "internal: unknown sync intrinsic '%s'", mname);
        return NULL;
    }

    /* Build 0, 1, or 2 opaque-pointer arguments. */
    LLVMValueRef call_args[2];
    LLVMTypeRef  param_tys[2] = { ptr_t, ptr_t };
    for (int i = 0; i < nargs; i++)
    {
        LLVMValueRef a = codegen_expr(ctx, node->as.call.args[i]);
        if (a == NULL) return NULL;
        call_args[i] = a;
    }
    LLVMTypeRef fn_ty = LLVMFunctionType(ret_t, nargs ? param_tys : NULL,
                                         (unsigned)nargs, 0);

    LLVMValueRef fn = LLVMGetNamedFunction(ctx->module, sym);
    if (fn == NULL) fn = LLVMAddFunction(ctx->module, sym, fn_ty);
    bool is_void = (ret_t == void_t);
    LLVMValueRef rv = LLVMBuildCall2(ctx->builder, fn_ty, fn,
                                     nargs ? call_args : NULL, (unsigned)nargs,
                                     is_void ? "" : mname);
    return is_void ? NULL : rv;
}

/* ============================ atomic family ============================= */

/* Atomic intrinsics (std.atomic) — emit a single inline LLVM atomic
   instruction, SequentiallyConsistent (full barrier). arg0 is an lvalue
   place (self.value); the rest are by-value operands. T must be a
   lock-free scalar (≤8 bytes) — larger types get a clean error pointing
   at Mutex. This is the whole point of Atomic: one machine instruction,
   no lock, no call.

   Structure preserved from the retired inline chain: fence returns BEFORE
   the place is evaluated; every other row (including an unknown member)
   evaluates the place and runs the scalar check first. */
static LLVMValueRef intrin_atomic_emit(CodegenContext *ctx, const char *aname,
                                       IntrinKind kind, AstNode *node)
{
    /* Memory order from the name suffix; default SeqCst (full barrier).
       _acquire/_release/_relaxed enable the cheaper orderings the SPSC
       ring fast path uses (acq/rel ~ plain mov on x64 vs SeqCst's locked
       xchg). The 4 ordering suffixes are all 8 chars. */
    LLVMAtomicOrdering ord = LLVMAtomicOrderingSequentiallyConsistent;
    {
        size_t alen = strlen(aname);
        if (alen > 8) {
            const char *suf = aname + alen - 8;
            if (strcmp(suf, "_acquire") == 0)      ord = LLVMAtomicOrderingAcquire;
            else if (strcmp(suf, "_release") == 0) ord = LLVMAtomicOrderingRelease;
            else if (strcmp(suf, "_relaxed") == 0) ord = LLVMAtomicOrderingMonotonic;
        }
    }

    if (kind == INTRIN_ATOMIC_FENCE)
    {
        LLVMBuildFence(ctx->builder, ord, 0, "");
        return NULL; /* void */
    }

    AstNode *place = node->as.call.args[0];
    LLVMValueRef ptr = codegen_lvalue_ptr(ctx, place);
    if (ptr == NULL)
    {
        cg_error(ctx, node->line, node->column,
                 "%s: argument is not an addressable place", aname);
        return NULL;
    }
    Type *at = place->resolved_type;
    /* Lock-free byte-sized scalars only. bool (i1) is excluded: LLVM
       atomics must be byte-sized — use Atomic(int) for flags. Anything
       larger than a scalar goes through Mutex. */
    bool scalar_ok = at && (at->kind == TYPE_INT || at->kind == TYPE_I8 ||
        at->kind == TYPE_I16 || at->kind == TYPE_I32 || at->kind == TYPE_I64 ||
        at->kind == TYPE_U8 || at->kind == TYPE_U16 || at->kind == TYPE_U32 ||
        at->kind == TYPE_U64 || at->kind == TYPE_F32 || at->kind == TYPE_F64 ||
        at->kind == TYPE_CHAR ||
        at->kind == TYPE_POINTER || at->kind == TYPE_OBJECT);
    if (!scalar_ok)
    {
        cg_error(ctx, node->line, node->column,
                 "atomic requires a lock-free byte-sized scalar "
                 "(int/i64/u64/f64/char/pointer); use Atomic(int) for a "
                 "flag, or Mutex for larger types");
        return NULL;
    }
    LLVMTypeRef elt = type_to_llvm(ctx, at);
    LLVMTargetDataRef td = LLVMGetModuleDataLayout(ctx->module);
    unsigned align = LLVMABIAlignmentOfType(td, elt);
    bool is_float = (at->kind == TYPE_F32 || at->kind == TYPE_F64);

    if (kind == INTRIN_ATOMIC_LOAD)
    {
        LLVMValueRef ld = LLVMBuildLoad2(ctx->builder, elt, ptr, "atom.load");
        LLVMSetOrdering(ld, ord);
        LLVMSetAlignment(ld, align);
        return ld;
    }
    if (kind == INTRIN_ATOMIC_STORE)
    {
        LLVMValueRef v = codegen_expr(ctx, node->as.call.args[1]);
        if (v == NULL) return NULL;
        LLVMValueRef st = LLVMBuildStore(ctx->builder, v, ptr);
        LLVMSetOrdering(st, ord);
        LLVMSetAlignment(st, align);
        return NULL; /* void */
    }
    if (kind == INTRIN_ATOMIC_ADD || kind == INTRIN_ATOMIC_SUB)
    {
        LLVMValueRef v = codegen_expr(ctx, node->as.call.args[1]);
        if (v == NULL) return NULL;
        LLVMAtomicRMWBinOp op;
        if (kind == INTRIN_ATOMIC_ADD)
            op = is_float ? LLVMAtomicRMWBinOpFAdd : LLVMAtomicRMWBinOpAdd;
        else
            op = is_float ? LLVMAtomicRMWBinOpFSub : LLVMAtomicRMWBinOpSub;
        return LLVMBuildAtomicRMW(ctx->builder, op, ptr, v, ord, 0); /* old value */
    }
    if (kind == INTRIN_ATOMIC_SWAP)
    {
        LLVMValueRef v = codegen_expr(ctx, node->as.call.args[1]);
        if (v == NULL) return NULL;
        return LLVMBuildAtomicRMW(ctx->builder, LLVMAtomicRMWBinOpXchg,
                                  ptr, v, ord, 0); /* old value */
    }
    if (kind == INTRIN_ATOMIC_CAS)
    {
        LLVMValueRef expected = codegen_expr(ctx, node->as.call.args[1]);
        LLVMValueRef desired = codegen_expr(ctx, node->as.call.args[2]);
        if (expected == NULL || desired == NULL) return NULL;
        /* strong CAS; SeqCst on both success and failure paths */
        LLVMValueRef cx = LLVMBuildAtomicCmpXchg(ctx->builder, ptr,
                                  expected, desired, ord, ord, 0);
        return LLVMBuildExtractValue(ctx->builder, cx, 1, "cas.ok"); /* i1 success */
    }
    cg_error(ctx, node->line, node->column,
             "internal: unknown atomic intrinsic '%s'", aname);
    return NULL;
}

/* ============================= simd family ============================== */
/* Helpers moved verbatim from codegen_expr.c (they had no other callers). */

/* Coerce a scalar to a target numeric element type (widen OR narrow):
   float<->float, int->float, float->int, int<->int. Used by __simd_splat so a
   literal (e.g. f64 3.0) broadcasts into a Simd of a different element type. */
static LLVMValueRef cg_simd_coerce(CodegenContext *ctx, LLVMValueRef v,
                                   Type *from, Type *to)
{
    if (v == NULL || from == NULL || to == NULL || type_equals(from, to)) return v;
    LLVMTypeRef tt = type_to_llvm(ctx, to);
    bool ff = type_is_float(from), tf = type_is_float(to);
    if (ff && tf) {
        int fb = from->kind == TYPE_F64 ? 64 : from->kind == TYPE_F32 ? 32 : 16;
        int tb = to->kind   == TYPE_F64 ? 64 : to->kind   == TYPE_F32 ? 32 : 16;
        if (tb > fb) return LLVMBuildFPExt(ctx->builder, v, tt, "simd.fpext");
        if (tb < fb) return LLVMBuildFPTrunc(ctx->builder, v, tt, "simd.fptrunc");
        return v;  /* same width (f16<->bf16 not reachable from splat scalars) */
    }
    if (!ff && tf)
        return type_is_unsigned(from)
            ? LLVMBuildUIToFP(ctx->builder, v, tt, "simd.uitofp")
            : LLVMBuildSIToFP(ctx->builder, v, tt, "simd.sitofp");
    if (ff && !tf)
        return type_is_unsigned(to)
            ? LLVMBuildFPToUI(ctx->builder, v, tt, "simd.fptoui")
            : LLVMBuildFPToSI(ctx->builder, v, tt, "simd.fptosi");
    int fb = type_int_bits(from), tb = type_int_bits(to);
    if (tb > fb) return type_is_unsigned(from)
        ? LLVMBuildZExt(ctx->builder, v, tt, "simd.zext")
        : LLVMBuildSExt(ctx->builder, v, tt, "simd.sext");
    if (tb < fb) return LLVMBuildTrunc(ctx->builder, v, tt, "simd.trunc");
    return v;  /* same width (e.g. signedness only) — value bits unchanged */
}

/* LLVM overloaded-intrinsic mangle for a Simd type: "v16f32"/"v8f64"/"v16i32". */
static void cg_simd_mangle(Type *simd, char *buf, size_t n)
{
    Type *e = simd->as.simd.elem;
    const char *ec;
    switch (e->kind) {
    case TYPE_F32: ec = "f32"; break;
    case TYPE_F64: ec = "f64"; break;
    case TYPE_F16: ec = "f16"; break;
    case TYPE_BF16: ec = "bf16"; break;
    case TYPE_I8: case TYPE_U8:  ec = "i8";  break;
    case TYPE_I16: case TYPE_U16: ec = "i16"; break;
    case TYPE_I64: case TYPE_U64: ec = "i64"; break;
    default: ec = "i32"; break;  /* int / i32 / u32 / char */
    }
    snprintf(buf, n, "v%d%s", simd->as.simd.lanes, ec);
}

/* Build a <lanes x i1> mask with the first n lanes set: icmp ult(iota, splat n).
   Hides the i1 vector from the surface — masked load/store take a lane count. */
static LLVMValueRef cg_simd_lane_mask(CodegenContext *ctx, LLVMValueRef n, unsigned lanes)
{
    LLVMTypeRef i32 = LLVMInt32TypeInContext(ctx->context);
    LLVMValueRef ncast = LLVMBuildIntCast2(ctx->builder, n, i32, 0, "simd.n32");
    LLVMValueRef *ic = malloc(sizeof(LLVMValueRef) * lanes);
    for (unsigned j = 0; j < lanes; j++) ic[j] = LLVMConstInt(i32, j, 0);
    LLVMValueRef iota = LLVMConstVector(ic, lanes);
    free(ic);
    LLVMTypeRef i32v = LLVMVectorType(i32, lanes);
    LLVMValueRef undef = LLVMGetUndef(i32v);
    LLVMValueRef ins = LLVMBuildInsertElement(ctx->builder, undef, ncast,
                           LLVMConstInt(i32, 0, 0), "n.ins");
    LLVMValueRef zmask = LLVMConstNull(LLVMVectorType(i32, lanes));
    LLVMValueRef nsplat = LLVMBuildShuffleVector(ctx->builder, ins, undef, zmask, "n.splat");
    return LLVMBuildICmp(ctx->builder, LLVMIntULT, iota, nsplat, "simd.mask");
}

/* Get-or-declare an LLVM intrinsic (or any external) by exact name + signature. */
static LLVMValueRef cg_get_or_declare(LLVMModuleRef mod, const char *name,
                                      LLVMTypeRef ret, LLVMTypeRef *params, unsigned np)
{
    LLVMValueRef fn = LLVMGetNamedFunction(mod, name);
    if (fn) return fn;
    return LLVMAddFunction(mod, name, LLVMFunctionType(ret, params, np, 0));
}

/* SIMD intrinsics __simd_* — lower to a single <N x T> IR instruction
   (docs/plan_simd.md §4.2), mirroring the __atomic_* name-dispatch. The
   checker set node->resolved_type (Simd for producers/ops, the element
   type for lane/reduce). */
static LLVMValueRef intrin_simd_emit(CodegenContext *ctx, const char *sname,
                                     IntrinKind kind, AstNode *node)
{
    AstNode **sa = node->as.call.args;
    Type *rt = node->resolved_type;

    switch (kind)
    {
    case INTRIN_SIMD_ZERO:
        return LLVMConstNull(type_to_llvm(ctx, rt));

    case INTRIN_SIMD_SPLAT:
    {
        LLVMTypeRef vt = type_to_llvm(ctx, rt);
        LLVMValueRef s = codegen_expr(ctx, sa[0]);
        if (s == NULL) return NULL;
        s = cg_simd_coerce(ctx, s, sa[0]->resolved_type, rt->as.simd.elem);
        LLVMTypeRef i32 = LLVMInt32TypeInContext(ctx->context);
        LLVMValueRef undef = LLVMGetUndef(vt);
        LLVMValueRef ins = LLVMBuildInsertElement(ctx->builder, undef, s,
                               LLVMConstInt(i32, 0, 0), "splat.ins");
        LLVMValueRef zmask = LLVMConstNull(
            LLVMVectorType(i32, (unsigned)rt->as.simd.lanes));
        return LLVMBuildShuffleVector(ctx->builder, ins, undef, zmask, "splat");
    }

    case INTRIN_SIMD_LANE:
    {
        LLVMValueRef v = codegen_expr(ctx, sa[0]);
        LLVMValueRef idx = codegen_expr(ctx, sa[1]);
        if (v == NULL || idx == NULL) return NULL;
        return LLVMBuildExtractElement(ctx->builder, v, idx, "simd.lane");
    }

    case INTRIN_SIMD_FMA:
    {
        LLVMValueRef a = codegen_expr(ctx, sa[0]);
        LLVMValueRef b = codegen_expr(ctx, sa[1]);
        LLVMValueRef cc = codegen_expr(ctx, sa[2]);
        if (a == NULL || b == NULL || cc == NULL) return NULL;
        LLVMTypeRef vt = type_to_llvm(ctx, rt);
        char mg[24], nm[40];
        cg_simd_mangle(rt, mg, sizeof mg);
        snprintf(nm, sizeof nm, "llvm.fma.%s", mg);
        LLVMTypeRef ps[3] = { vt, vt, vt };
        LLVMValueRef fn = cg_get_or_declare(ctx->module, nm, vt, ps, 3);
        LLVMTypeRef fty = LLVMGlobalGetValueType(fn);
        LLVMValueRef av[3] = { a, b, cc };
        return LLVMBuildCall2(ctx->builder, fty, fn, av, 3, "simd.fma");
    }

    case INTRIN_SIMD_MAX:
    case INTRIN_SIMD_MIN:
    {
        LLVMValueRef a = codegen_expr(ctx, sa[0]);
        LLVMValueRef b = codegen_expr(ctx, sa[1]);
        if (a == NULL || b == NULL) return NULL;
        LLVMTypeRef vt = type_to_llvm(ctx, rt);
        Type *et = rt->as.simd.elem;
        bool is_max = (kind == INTRIN_SIMD_MAX);
        const char *base = type_is_float(et) ? (is_max ? "maxnum" : "minnum")
                         : type_is_unsigned(et) ? (is_max ? "umax" : "umin")
                         : (is_max ? "smax" : "smin");
        char mg[24], nm[48];
        cg_simd_mangle(rt, mg, sizeof mg);
        snprintf(nm, sizeof nm, "llvm.%s.%s", base, mg);
        LLVMTypeRef ps[2] = { vt, vt };
        LLVMValueRef fn = cg_get_or_declare(ctx->module, nm, vt, ps, 2);
        LLVMTypeRef fty = LLVMGlobalGetValueType(fn);
        LLVMValueRef av[2] = { a, b };
        return LLVMBuildCall2(ctx->builder, fty, fn, av, 2, "simd.mm");
    }

    case INTRIN_SIMD_REDUCE_ADD:
    {
        LLVMValueRef v = codegen_expr(ctx, sa[0]);
        if (v == NULL) return NULL;
        Type *st = sa[0]->resolved_type;    /* Simd(T,N) */
        Type *et = st->as.simd.elem;
        LLVMTypeRef etl = type_to_llvm(ctx, et);
        LLVMTypeRef vt = type_to_llvm(ctx, st);
        char mg[24], nm[48];
        cg_simd_mangle(st, mg, sizeof mg);
        if (type_is_float(et)) {
            /* T @llvm.vector.reduce.fadd.vNfT(T start, <N x T> v) */
            snprintf(nm, sizeof nm, "llvm.vector.reduce.fadd.%s", mg);
            LLVMTypeRef ps[2] = { etl, vt };
            LLVMValueRef fn = cg_get_or_declare(ctx->module, nm, etl, ps, 2);
            LLVMTypeRef fty = LLVMGlobalGetValueType(fn);
            LLVMValueRef av[2] = { LLVMConstNull(etl), v };
            return LLVMBuildCall2(ctx->builder, fty, fn, av, 2, "simd.radd");
        }
        /* T @llvm.vector.reduce.add.vNiT(<N x T> v) */
        snprintf(nm, sizeof nm, "llvm.vector.reduce.add.%s", mg);
        LLVMTypeRef ps[1] = { vt };
        LLVMValueRef fn = cg_get_or_declare(ctx->module, nm, etl, ps, 1);
        LLVMTypeRef fty = LLVMGlobalGetValueType(fn);
        LLVMValueRef av[1] = { v };
        return LLVMBuildCall2(ctx->builder, fty, fn, av, 1, "simd.radd");
    }

    case INTRIN_SIMD_REDUCE_MAX:
    case INTRIN_SIMD_REDUCE_MIN:
    {
        /* Horizontal max/min of <N x T> to a scalar T. The fmax/fmin
           reduce intrinsics take just the vector (no start value). */
        LLVMValueRef v = codegen_expr(ctx, sa[0]);
        if (v == NULL) return NULL;
        Type *st = sa[0]->resolved_type;    /* Simd(T,N) */
        Type *et = st->as.simd.elem;
        LLVMTypeRef etl = type_to_llvm(ctx, et);
        LLVMTypeRef vt = type_to_llvm(ctx, st);
        bool is_max = (kind == INTRIN_SIMD_REDUCE_MAX);
        const char *base = type_is_float(et) ? (is_max ? "fmax" : "fmin")
                         : type_is_unsigned(et) ? (is_max ? "umax" : "umin")
                         : (is_max ? "smax" : "smin");
        char mg[24], nm[48];
        cg_simd_mangle(st, mg, sizeof mg);
        snprintf(nm, sizeof nm, "llvm.vector.reduce.%s.%s", base, mg);
        LLVMTypeRef ps[1] = { vt };
        LLVMValueRef fn = cg_get_or_declare(ctx->module, nm, etl, ps, 1);
        LLVMTypeRef fty = LLVMGlobalGetValueType(fn);
        LLVMValueRef av[1] = { v };
        return LLVMBuildCall2(ctx->builder, fty, fn, av, 1, "simd.rmm");
    }

    case INTRIN_SIMD_LOAD:
    {
        /* Load N contiguous elements starting at ptr[off] as a <N x T>.
           GEP by element offset, then a vector load (element-aligned =
           unaligned vector access, safe for any pointer). */
        LLVMValueRef ptr = codegen_expr(ctx, sa[0]);
        LLVMValueRef off = codegen_expr(ctx, sa[1]);
        if (ptr == NULL || off == NULL) return NULL;
        LLVMTypeRef etl = type_to_llvm(ctx, rt->as.simd.elem);
        LLVMTypeRef vt = type_to_llvm(ctx, rt);
        LLVMValueRef idx[1] = { off };
        LLVMValueRef ep = LLVMBuildGEP2(ctx->builder, etl, ptr, idx, 1, "simd.ep");
        LLVMValueRef ld = LLVMBuildLoad2(ctx->builder, vt, ep, "simd.load");
        LLVMTargetDataRef td = LLVMGetModuleDataLayout(ctx->module);
        LLVMSetAlignment(ld, LLVMABIAlignmentOfType(td, etl));
        return ld;
    }

    case INTRIN_SIMD_STORE:
    {
        LLVMValueRef ptr = codegen_expr(ctx, sa[0]);
        LLVMValueRef off = codegen_expr(ctx, sa[1]);
        LLVMValueRef v = codegen_expr(ctx, sa[2]);
        if (ptr == NULL || off == NULL || v == NULL) return NULL;
        Type *st = sa[2]->resolved_type;   /* Simd(T,N) */
        LLVMTypeRef etl = type_to_llvm(ctx, st->as.simd.elem);
        LLVMValueRef idx[1] = { off };
        LLVMValueRef ep = LLVMBuildGEP2(ctx->builder, etl, ptr, idx, 1, "simd.ep");
        LLVMValueRef sst = LLVMBuildStore(ctx->builder, v, ep);
        LLVMTargetDataRef td = LLVMGetModuleDataLayout(ctx->module);
        LLVMSetAlignment(sst, LLVMABIAlignmentOfType(td, etl));
        return NULL;
    }

    case INTRIN_SIMD_LOAD_MASKED:
    {
        /* Load the first n lanes (rest = 0) via @llvm.masked.load. */
        LLVMValueRef ptr = codegen_expr(ctx, sa[0]);
        LLVMValueRef off = codegen_expr(ctx, sa[1]);
        LLVMValueRef n   = codegen_expr(ctx, sa[2]);
        if (ptr == NULL || off == NULL || n == NULL) return NULL;
        unsigned lanes = (unsigned)rt->as.simd.lanes;
        LLVMTypeRef etl = type_to_llvm(ctx, rt->as.simd.elem);
        LLVMTypeRef vt  = type_to_llvm(ctx, rt);
        LLVMTypeRef i32 = LLVMInt32TypeInContext(ctx->context);
        LLVMTypeRef i1v = LLVMVectorType(LLVMInt1TypeInContext(ctx->context), lanes);
        LLVMValueRef idx[1] = { off };
        LLVMValueRef ep = LLVMBuildGEP2(ctx->builder, etl, ptr, idx, 1, "simd.mep");
        LLVMValueRef mask = cg_simd_lane_mask(ctx, n, lanes);
        LLVMTargetDataRef td = LLVMGetModuleDataLayout(ctx->module);
        LLVMValueRef align = LLVMConstInt(i32, LLVMABIAlignmentOfType(td, etl), 0);
        char mg[24], nm[56];
        cg_simd_mangle(rt, mg, sizeof mg);
        snprintf(nm, sizeof nm, "llvm.masked.load.%s.p0", mg);
        LLVMTypeRef ps[4] = { LLVMTypeOf(ep), i32, i1v, vt };
        LLVMValueRef fn = cg_get_or_declare(ctx->module, nm, vt, ps, 4);
        LLVMTypeRef fty = LLVMGlobalGetValueType(fn);
        LLVMValueRef av[4] = { ep, align, mask, LLVMConstNull(vt) };
        return LLVMBuildCall2(ctx->builder, fty, fn, av, 4, "simd.mload");
    }

    case INTRIN_SIMD_STORE_MASKED:
    {
        /* Store the first n lanes via @llvm.masked.store. */
        LLVMValueRef ptr = codegen_expr(ctx, sa[0]);
        LLVMValueRef off = codegen_expr(ctx, sa[1]);
        LLVMValueRef v   = codegen_expr(ctx, sa[2]);
        LLVMValueRef n   = codegen_expr(ctx, sa[3]);
        if (ptr == NULL || off == NULL || v == NULL || n == NULL) return NULL;
        Type *st = sa[2]->resolved_type;
        unsigned lanes = (unsigned)st->as.simd.lanes;
        LLVMTypeRef etl = type_to_llvm(ctx, st->as.simd.elem);
        LLVMTypeRef vt  = type_to_llvm(ctx, st);
        LLVMTypeRef i32 = LLVMInt32TypeInContext(ctx->context);
        LLVMTypeRef i1v = LLVMVectorType(LLVMInt1TypeInContext(ctx->context), lanes);
        LLVMTypeRef voidty = LLVMVoidTypeInContext(ctx->context);
        LLVMValueRef idx[1] = { off };
        LLVMValueRef ep = LLVMBuildGEP2(ctx->builder, etl, ptr, idx, 1, "simd.mep");
        LLVMValueRef mask = cg_simd_lane_mask(ctx, n, lanes);
        LLVMTargetDataRef td = LLVMGetModuleDataLayout(ctx->module);
        LLVMValueRef align = LLVMConstInt(i32, LLVMABIAlignmentOfType(td, etl), 0);
        char mg[24], nm[56];
        cg_simd_mangle(st, mg, sizeof mg);
        snprintf(nm, sizeof nm, "llvm.masked.store.%s.p0", mg);
        LLVMTypeRef ps[4] = { vt, LLVMTypeOf(ep), i32, i1v };
        LLVMValueRef fn = cg_get_or_declare(ctx->module, nm, voidty, ps, 4);
        LLVMTypeRef fty = LLVMGlobalGetValueType(fn);
        LLVMValueRef av[4] = { v, ep, align, mask };
        LLVMBuildCall2(ctx->builder, fty, fn, av, 4, "");
        return NULL;
    }

    case INTRIN_SIMD_CAST:
    {
        /* Element-wise numeric conversion to <N x U> (same N). */
        LLVMValueRef v = codegen_expr(ctx, sa[0]);
        if (v == NULL) return NULL;
        Type *st = sa[0]->resolved_type;    /* Simd(T,N) */
        Type *se = st->as.simd.elem;
        Type *de = rt->as.simd.elem;
        LLVMTypeRef vt = type_to_llvm(ctx, rt);  /* <N x U> */
        if (se->kind == de->kind) return v;
        bool sf = type_is_float(se), df = type_is_float(de);
        if (sf && df) {
            int sb = se->kind==TYPE_F64?64:se->kind==TYPE_F32?32:16;
            int db = de->kind==TYPE_F64?64:de->kind==TYPE_F32?32:16;
            if (db > sb) return LLVMBuildFPExt(ctx->builder, v, vt, "simd.cast.fpext");
            if (db < sb) return LLVMBuildFPTrunc(ctx->builder, v, vt, "simd.cast.fptrunc");
            /* same width, different 16-bit format (f16<->bf16): via f32 */
            LLVMTypeRef f32v = LLVMVectorType(
                LLVMFloatTypeInContext(ctx->context), (unsigned)st->as.simd.lanes);
            LLVMValueRef up = LLVMBuildFPExt(ctx->builder, v, f32v, "simd.cast.up");
            return LLVMBuildFPTrunc(ctx->builder, up, vt, "simd.cast.dn");
        }
        if (!sf && df)
            return type_is_unsigned(se)
                ? LLVMBuildUIToFP(ctx->builder, v, vt, "simd.cast.uitofp")
                : LLVMBuildSIToFP(ctx->builder, v, vt, "simd.cast.sitofp");
        if (sf && !df)
            return type_is_unsigned(de)
                ? LLVMBuildFPToUI(ctx->builder, v, vt, "simd.cast.fptoui")
                : LLVMBuildFPToSI(ctx->builder, v, vt, "simd.cast.fptosi");
        int sb = type_int_bits(se), db = type_int_bits(de);
        if (db > sb) return type_is_unsigned(se)
            ? LLVMBuildZExt(ctx->builder, v, vt, "simd.cast.zext")
            : LLVMBuildSExt(ctx->builder, v, vt, "simd.cast.sext");
        if (db < sb) return LLVMBuildTrunc(ctx->builder, v, vt, "simd.cast.trunc");
        return v;
    }

    case INTRIN_SIMD_FLOOR:
    {
        LLVMValueRef v = codegen_expr(ctx, sa[0]);
        if (v == NULL) return NULL;
        LLVMTypeRef vt = type_to_llvm(ctx, rt);
        char mg[24], nm[40];
        cg_simd_mangle(rt, mg, sizeof mg);
        snprintf(nm, sizeof nm, "llvm.floor.%s", mg);
        LLVMTypeRef ps[1] = { vt };
        LLVMValueRef fn = cg_get_or_declare(ctx->module, nm, vt, ps, 1);
        LLVMTypeRef fty = LLVMGlobalGetValueType(fn);
        LLVMValueRef av[1] = { v };
        return LLVMBuildCall2(ctx->builder, fty, fn, av, 1, "simd.floor");
    }

    case INTRIN_SIMD_BITCAST:
    {
        /* Reinterpret the lane bits (i32 <-> f32, same total width). */
        LLVMValueRef v = codegen_expr(ctx, sa[0]);
        if (v == NULL) return NULL;
        LLVMTypeRef vt = type_to_llvm(ctx, rt);  /* <N x U> */
        return LLVMBuildBitCast(ctx->builder, v, vt, "simd.bitcast");
    }

    default:
        cg_error(ctx, node->line, node->column,
                 "internal: unknown simd intrinsic '%s'", sname);
        return NULL;
    }
}

/* ============================ bytecopy row ============================== */
/* Moved verbatim from codegen_expr.c (S2 P1 4/4). */

/* __ls_bytecopy prim switch — cached like the other LS_NO_* toggles. */
bool builtin_intrinsic_bytecopy_enabled(void)
{
    static int cached = -1;
    if (cached < 0)
    {
        const char *e = getenv("LS_NO_MEMCPY_PRIM");
        cached = (e != NULL && e[0] != '\0' && strcmp(e, "0") != 0) ? 0 : 1;
    }
    return cached == 1;
}

/* Lower `c.__ls_bytecopy(dst, doff, src, soff, n)` to
       memcpy(dst + doff, src + soff, n)   as @llvm.memcpy.p0.p0.i64.
   Offsets/len are LS `int` (i32) — sign-extend to i64. GEPs are plain
   (non-inbounds) i8 element steps so a nil base + 0 offset is not poison;
   llvm.memcpy len==0 is a defined no-op, matching the C helper's guard.
   Returns the memcpy call value (callers treat the expr as void). */
static LLVMValueRef intrin_bytecopy_emit(CodegenContext *ctx, AstNode *node)
{
    LLVMValueRef a[5];
    for (int i = 0; i < 5; i++)
    {
        a[i] = codegen_expr(ctx, node->as.call.args[i]);
        if (a[i] == NULL)
            return NULL;
    }
    LLVMTypeRef i8t  = LLVMInt8TypeInContext(ctx->context);
    LLVMTypeRef i64t = LLVMInt64TypeInContext(ctx->context);
    LLVMValueRef idx[3] = { a[1], a[3], a[4] };  /* doff, soff, n */
    for (int i = 0; i < 3; i++)
    {
        if (idx[i] != NULL &&
            LLVMGetTypeKind(LLVMTypeOf(idx[i])) == LLVMIntegerTypeKind &&
            LLVMGetIntTypeWidth(LLVMTypeOf(idx[i])) < 64)
            idx[i] = LLVMBuildSExt(ctx->builder, idx[i], i64t, "bc.i64");
    }
    LLVMValueRef dst = LLVMBuildGEP2(ctx->builder, i8t, a[0], &idx[0], 1, "bc.dst");
    LLVMValueRef src = LLVMBuildGEP2(ctx->builder, i8t, a[2], &idx[1], 1, "bc.src");
    return LLVMBuildMemCpy(ctx->builder, dst, 1, src, 1, idx[2]);
}

/* ============================== dispatch ================================ */

LLVMValueRef builtin_intrinsic_emit_call(CodegenContext *ctx, const char *name,
                                         AstNode *node)
{
    IntrinKind kind = builtin_intrinsic_lookup(name);
    switch (kind)
    {
    case INTRIN_NONE:
        return NULL;
    case INTRIN_ATOMIC_FENCE:
    case INTRIN_ATOMIC_LOAD:
    case INTRIN_ATOMIC_STORE:
    case INTRIN_ATOMIC_ADD:
    case INTRIN_ATOMIC_SUB:
    case INTRIN_ATOMIC_SWAP:
    case INTRIN_ATOMIC_CAS:
    case INTRIN_ATOMIC_UNKNOWN:
        return intrin_atomic_emit(ctx, name, kind, node);
    case INTRIN_SIMD_ZERO:
    case INTRIN_SIMD_SPLAT:
    case INTRIN_SIMD_LANE:
    case INTRIN_SIMD_FMA:
    case INTRIN_SIMD_MAX:
    case INTRIN_SIMD_MIN:
    case INTRIN_SIMD_REDUCE_ADD:
    case INTRIN_SIMD_REDUCE_MAX:
    case INTRIN_SIMD_REDUCE_MIN:
    case INTRIN_SIMD_LOAD:
    case INTRIN_SIMD_STORE:
    case INTRIN_SIMD_LOAD_MASKED:
    case INTRIN_SIMD_STORE_MASKED:
    case INTRIN_SIMD_CAST:
    case INTRIN_SIMD_FLOOR:
    case INTRIN_SIMD_BITCAST:
    case INTRIN_SIMD_UNKNOWN:
        return intrin_simd_emit(ctx, name, kind, node);
    case INTRIN_BYTECOPY:
        return intrin_bytecopy_emit(ctx, node);
    default:
        return intrin_sync_emit(ctx, name, kind, node);
    }
}
