/* codegen_noalias.c — A4: function-scoped `noalias` recovery.
 * (docs/plan_opt_noalias_recovery.md; history: docs/plan_borrow_noalias.md §1)
 *
 * The LS borrow checker proves `&!T` exclusivity, but only as a SINGLE-THREAD
 * property: the concurrency primitives (std.sync Guard / std.chan / Ring)
 * deliberately share &!self-pointed memory across threads, so emitting
 * `noalias` unconditionally lets LLVM hoist a spin/recv-loop load past the
 * other thread's write -> deadlock (observed and reverted; the gold-standard
 * regression is tests/samples/noalias_guard.lls).
 *
 * This pass recovers `noalias` per function, for exactly the functions whose
 * whole visible call tree provably stays on the current thread:
 *
 *   seed disqualifiers (per function body, one instruction scan):
 *     - any atomic instruction (atomicrmw / cmpxchg / fence, or load/store
 *       with an atomic ordering)          -> sync primitive territory
 *     - any indirect call (Block/closure invocation, fn pointer)
 *     - any inline-asm call
 *     - any call to an external declaration NOT on the safe-primitive list
 *       below (unknown externals — FFI, OS threads, mutex/condvar runtime,
 *       future primitives — disqualify BY DEFAULT)
 *   propagation:
 *     - calling a disqualified function disqualifies the caller (optimistic
 *       greatest fixpoint over the module call graph: recursion cycles with
 *       no seed inside stay qualified — the property is "no bad operation
 *       reachable", so this direction is sound)
 *
 * Candidate parameters are pre-marked by codegen (cg_attach_borrow_attrs)
 * with the string attribute "ls-noalias-cand" — only writable borrows (&!T /
 * &!self) whose function cannot return a borrow (the existing nocapture_ok
 * predicate; returning a derived pointer is excluded conservatively). The
 * pass strips every marker and stamps `noalias` on the survivors, so it is
 * idempotent (a second run finds no markers and does nothing).
 *
 * Escape analysis is NOT re-done here: the borrow checker already forbids
 * storing a borrow into fields/globals/captures (the Guard soundness base),
 * and the return-value escape hatch is excluded via nocapture_ok above.
 *
 * Runs from ls_opt_run_passes (AOT + JIT file mode + optimized emit-ir/asm)
 * and once from cmd_emit_ir so unoptimized IR dumps show the result. REPL
 * incremental modules reference previous snippets as unknown externals, so
 * every cross-snippet path disqualifies itself by default (rule 3 for free).
 *
 * Switches: LS_NO_NOALIAS=1 disables just this pass (markers stripped, no
 * noalias). LS_NO_BORROW_ATTRS=1 already suppresses the markers at the
 * source. LS_FORCE_NOALIAS=1 (codegen_decl.c) force-emits noalias everywhere
 * — the unsound diagnostic used to reproduce the historical deadlock.
 */
#include "codegen_noalias.h"

#include <stdlib.h>
#include <string.h>
#include <stdbool.h>

#include <llvm-c/Core.h>

#define CAND_ATTR "ls-noalias-cand"
#define CAND_ATTR_LEN (sizeof(CAND_ATTR) - 1)

/* ---- safe external primitives ------------------------------------------- */

/* External declarations that do NOT disqualify a caller. Criterion: the
 * routine never RETAINS a pointer it is handed and never hands one to another
 * thread — it cannot create a second access path to a borrow's pointee that
 * is live during the caller's execution. (Interior pointers like Vec's
 * self.data are separate objects in the provenance sense anyway; they are
 * loaded from the struct, not derived from the borrow parameter.)
 *
 * Anything NOT listed here — __mutex_* / __cond_* / __thread_* / __cpu_* /
 * __atomic helpers, FFI (LoadLibrary/GetProcAddress), qsort (calls back
 * through a fn pointer), system, file IO, and every FUTURE runtime primitive
 * — disqualifies by default. Extend deliberately, never generously. */
static const char *const NA_SAFE_EXTERNALS[] = {
    /* memory (memcheck-tracked CRT wrappers land on these names too) */
    "malloc", "calloc", "realloc", "free",
    "memcpy", "memmove", "memset", "memcmp",
    /* C string / formatting (pure w.r.t. our memory) */
    "strlen", "strcmp", "strncmp", "strstr", "sprintf", "snprintf",
    /* libm (scalar math the LLVM intrinsics occasionally lower to) */
    "sqrt", "sin", "cos", "tan", "exp", "log", "log10", "pow", "fmod",
    "floor", "ceil", "round", "trunc", "fabs", "atan2", "sinh", "cosh",
    "tanh", "asin", "acos", "atan", "exp2", "log2",
    /* process exit + stdout plumbing (@print path; abort ends execution) */
    "abort", "exit", "printf", "puts", "putchar", "fflush",
    "__ls_proc_exit", "__ls_flush_out", "__ls_printf",
    "__ls_stdout", "__ls_stderr", "__ls_sink_stream", "__ls_sink_set",
    /* pure LS runtime helpers (search/hash/copy/byte loads/pointer arith) */
    "__ls_str_find", "__ls_fxhash_bytes", "__ls_bytecopy", "__ls_ptr_at",
    "__ls_load_u8", "__ls_load_le_u16", "__ls_load_le_u32", "__ls_load_le_u64",
    "__ls_load_be_u16", "__ls_load_be_u32", "__ls_load_be_u64",
    "__ls_float_fixed_exec", "__ls_float_fixed_ptr",
    /* pure host queries */
    "__ls_cpu_count", "__ls_cache_kb", "__ls_cpu_has_avx512",
    "__ls_get_argc", "__ls_get_argv",
    NULL,
};

static bool na_safe_external(const char *name)
{
    if (name == NULL || name[0] == '\0') return false;
    if (strncmp(name, "llvm.", 5) == 0) return true;   /* LLVM intrinsics */
    for (int i = 0; NA_SAFE_EXTERNALS[i] != NULL; i++)
        if (strcmp(name, NA_SAFE_EXTERNALS[i]) == 0) return true;
    return false;
}

/* ---- module function table ---------------------------------------------- */

typedef struct {
    LLVMValueRef fn;
    int *callees;        /* indices into the table (defined functions only) */
    int callee_count;
    int callee_cap;
    bool disq;           /* disqualified (seed or propagated) */
} NaNode;

static int na_find(NaNode *nodes, int n, LLVMValueRef fn)
{
    for (int i = 0; i < n; i++)
        if (nodes[i].fn == fn) return i;
    return -1;
}

static void na_add_callee(NaNode *node, int callee)
{
    for (int i = 0; i < node->callee_count; i++)
        if (node->callees[i] == callee) return;
    if (node->callee_count == node->callee_cap) {
        int cap = node->callee_cap ? node->callee_cap * 2 : 8;
        int *grown = realloc(node->callees, (size_t)cap * sizeof(int));
        if (grown == NULL) {
            node->disq = true;   /* OOM: can't track the edge — fail CLOSED */
            return;
        }
        node->callees = grown;
        node->callee_cap = cap;
    }
    node->callees[node->callee_count++] = callee;
}

/* Scan one defined function: record seed disqualifiers + direct call edges. */
static void na_scan_fn(NaNode *nodes, int n, int self)
{
    LLVMValueRef fn = nodes[self].fn;
    for (LLVMBasicBlockRef bb = LLVMGetFirstBasicBlock(fn); bb != NULL;
         bb = LLVMGetNextBasicBlock(bb)) {
        for (LLVMValueRef inst = LLVMGetFirstInstruction(bb); inst != NULL;
             inst = LLVMGetNextInstruction(inst)) {
            LLVMOpcode op = LLVMGetInstructionOpcode(inst);
            switch (op) {
            case LLVMAtomicRMW:
            case LLVMAtomicCmpXchg:
            case LLVMFence:
                nodes[self].disq = true;
                return;
            case LLVMLoad:
            case LLVMStore:
                if (LLVMGetOrdering(inst) != LLVMAtomicOrderingNotAtomic) {
                    nodes[self].disq = true;
                    return;
                }
                break;
            case LLVMCall: {
                LLVMValueRef callee = LLVMGetCalledValue(inst);
                if (LLVMIsAInlineAsm(callee)) {          /* e.g. pause hints */
                    nodes[self].disq = true;
                    return;
                }
                if (!LLVMIsAFunction(callee)) {          /* indirect: Block /
                                                            fn-pointer call */
                    nodes[self].disq = true;
                    return;
                }
                if (LLVMIsDeclaration(callee)) {         /* external */
                    size_t len = 0;
                    const char *name = LLVMGetValueName2(callee, &len);
                    if (!na_safe_external(name)) {
                        nodes[self].disq = true;
                        return;
                    }
                } else {                                  /* defined: edge */
                    int idx = na_find(nodes, n, callee);
                    if (idx < 0) {                        /* shouldn't happen */
                        nodes[self].disq = true;
                        return;
                    }
                    na_add_callee(&nodes[self], idx);
                }
                break;
            }
            default:
                break;
            }
        }
    }
}

/* Strip every candidate marker from fn; return a bitmask-free note of which
 * param indices carried one by filling `marked` (caller-sized). */
static int na_collect_markers(LLVMValueRef fn, bool *marked, unsigned nparams)
{
    int found = 0;
    for (unsigned p = 0; p < nparams; p++) {
        LLVMAttributeRef a = LLVMGetStringAttributeAtIndex(
            fn, p + 1, CAND_ATTR, CAND_ATTR_LEN);
        marked[p] = (a != NULL);
        if (a != NULL) {
            LLVMRemoveStringAttributeAtIndex(fn, p + 1, CAND_ATTR, CAND_ATTR_LEN);
            found++;
        }
    }
    return found;
}

void ls_noalias_recover(LLVMModuleRef module)
{
    bool disabled = (getenv("LS_NO_NOALIAS") != NULL) ||
                    (getenv("LS_NO_BORROW_ATTRS") != NULL);

    /* Index the defined functions. */
    int n = 0;
    for (LLVMValueRef f = LLVMGetFirstFunction(module); f != NULL;
         f = LLVMGetNextFunction(f))
        if (!LLVMIsDeclaration(f)) n++;

    NaNode *nodes = NULL;
    if (n > 0 && !disabled) {
        nodes = calloc((size_t)n, sizeof(NaNode));
        if (nodes == NULL) disabled = true;   /* OOM: strip markers, emit nothing */
    }

    if (!disabled && nodes != NULL) {
        int i = 0;
        for (LLVMValueRef f = LLVMGetFirstFunction(module); f != NULL;
             f = LLVMGetNextFunction(f))
            if (!LLVMIsDeclaration(f)) nodes[i++].fn = f;

        /* Seed disqualifiers + call edges. */
        for (int k = 0; k < n; k++)
            na_scan_fn(nodes, n, k);

        /* Optimistic greatest fixpoint: keep removing callers of the
         * disqualified until stable (bounded by the longest call chain). */
        bool changed = true;
        while (changed) {
            changed = false;
            for (int k = 0; k < n; k++) {
                if (nodes[k].disq) continue;
                for (int e = 0; e < nodes[k].callee_count; e++) {
                    if (nodes[nodes[k].callees[e]].disq) {
                        nodes[k].disq = true;
                        changed = true;
                        break;
                    }
                }
            }
        }
    }

    /* Strip markers everywhere (declarations included); stamp survivors. */
    unsigned kind = LLVMGetEnumAttributeKindForName("noalias", 7);
    LLVMContextRef ctx = LLVMGetModuleContext(module);
    for (LLVMValueRef f = LLVMGetFirstFunction(module); f != NULL;
         f = LLVMGetNextFunction(f)) {
        unsigned nparams = LLVMCountParams(f);
        if (nparams == 0) continue;
        bool marked_buf[64];
        bool *marked = marked_buf;
        if (nparams > 64) {
            marked = calloc(nparams, sizeof(bool));
            if (marked == NULL) continue;     /* strip next time; no noalias */
        }
        int found = na_collect_markers(f, marked, nparams);
        if (found > 0 && !disabled && !LLVMIsDeclaration(f) && nodes != NULL) {
            int idx = na_find(nodes, n, f);
            if (idx >= 0 && !nodes[idx].disq && kind != 0) {
                for (unsigned p = 0; p < nparams; p++)
                    if (marked[p])
                        LLVMAddAttributeAtIndex(
                            f, p + 1, LLVMCreateEnumAttribute(ctx, kind, 0));
            }
        }
        if (marked != marked_buf) free(marked);
    }

    if (nodes != NULL) {
        for (int k = 0; k < n; k++) free(nodes[k].callees);
        free(nodes);
    }
}
