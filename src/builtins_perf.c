/* builtins_perf.c — Type & lookup side of the built-in `perf` module.
 *
 * Pure type-system code: no LLVM dependency, safe for type-checker tests.
 * Codegen-side IR emission lives in builtins_perf_cg.c.
 */
#include "builtins_perf.h"
#include "builtins_math.h"   /* for builtin_module_exists / make_type entry */
#include <string.h>

/* ---- Function table ---- */

typedef struct {
    const char   *ls_name;
    int           arity;
    PerfEmitKind  kind;
} PerfFn;

/* Return types by kind:
 *   NOW / RDTSC / RDTSCP / ELAPSED_NS  → i64
 *   ELAPSED_MS / ELAPSED_S             → f64
 */
static const PerfFn PERF_FUNCS[] = {
    {"now",        0, PERF_EMIT_NOW},
    {"rdtsc",      0, PERF_EMIT_RDTSC},
    {"rdtscp",     0, PERF_EMIT_RDTSCP},
    {"elapsed_ns", 1, PERF_EMIT_ELAPSED_NS},
    {"elapsed_ms", 1, PERF_EMIT_ELAPSED_MS},
    {"elapsed_s",  1, PERF_EMIT_ELAPSED_S},
};
#define PERF_FUNC_COUNT ((int)(sizeof(PERF_FUNCS) / sizeof(PERF_FUNCS[0])))

/* ---- Lookup ---- */

bool builtin_perf_lookup_fn(const char *name, int *out_arity,
                            PerfEmitKind *out_kind) {
    if (name == NULL) return false;
    for (int i = 0; i < PERF_FUNC_COUNT; i++) {
        if (strcmp(PERF_FUNCS[i].ls_name, name) == 0) {
            if (out_arity) *out_arity = PERF_FUNCS[i].arity;
            if (out_kind)  *out_kind  = PERF_FUNCS[i].kind;
            return true;
        }
    }
    return false;
}

/* ---- Type construction ---- */

Type *builtin_perf_make_type(void) {
    Type *mod = type_module_new("perf");
    mod->as.module.is_builtin = true;

    Type *i64_t = type_i64();
    Type *f64_t = type_f64();

    for (int i = 0; i < PERF_FUNC_COUNT; i++) {
        const PerfFn *p = &PERF_FUNCS[i];

        /* Determine return type. */
        Type *ret;
        switch (p->kind) {
            case PERF_EMIT_ELAPSED_MS:
            case PERF_EMIT_ELAPSED_S:
                ret = f64_t;
                break;
            default:
                ret = i64_t;
                break;
        }

        /* All parameters are i64 (the timestamp value from perf.now()). */
        Type **params = NULL;
        if (p->arity > 0) {
            params = (Type **)malloc_safe((size_t)p->arity * sizeof(Type *));
            for (int k = 0; k < p->arity; k++) params[k] = i64_t;
        }

        Type *fn_t = type_function(params, p->arity, ret, false);
        type_module_add_export(mod, p->ls_name, fn_t);
    }

    return mod;
}
