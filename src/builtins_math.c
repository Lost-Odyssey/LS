/* builtins_math.c — Type & lookup side of the built-in `math` module.
 *
 * Pure type-system code: lists the exported symbols and constructs a
 * TYPE_MODULE used by the type-checker. No LLVM dependency, so it can
 * link into pure-checker test binaries (test_types) without dragging in
 * the LLVM C library. Codegen-side emission lives in builtins_math_cg.c.
 */
#include "builtins_math.h"
#include "builtins_perf.h"
#include <string.h>
#include <math.h>

/* ---- Function table ---- */

typedef struct {
    const char *ls_name;     /* user-visible name (e.g. "sqrt") */
    int arity;               /* 1 or 2 */
    MathEmitKind kind;
    const char *emit_name;   /* "llvm.sqrt.f64" or "tan" — float path */
    MathPolyKind poly;
    const char *int_prefix;  /* "llvm.abs.i" / "llvm.smin.i" / "llvm.smax.i"
                                — codegen appends bit width */
} MathFn;

/* All non-polymorphic take f64 → f64. abs/min/max are polymorphic: int
   args dispatch to LLVM integer intrinsics (no implicit f64 widening),
   keeping integer precision. */
static const MathFn MATH_FUNCS[] = {
    /* basic — POLYMORPHIC */
    {"abs",    1, MATH_EMIT_INTRINSIC, "llvm.fabs.f64",   MATH_POLY_INT_OR_FLOAT, "llvm.abs.i"},
    {"min",    2, MATH_EMIT_INTRINSIC, "llvm.minnum.f64", MATH_POLY_INT_OR_FLOAT, "llvm.smin.i"},
    {"max",    2, MATH_EMIT_INTRINSIC, "llvm.maxnum.f64", MATH_POLY_INT_OR_FLOAT, "llvm.smax.i"},
    /* rounding (float-only) */
    {"floor",  1, MATH_EMIT_INTRINSIC, "llvm.floor.f64",  MATH_POLY_NONE, NULL},
    {"ceil",   1, MATH_EMIT_INTRINSIC, "llvm.ceil.f64",   MATH_POLY_NONE, NULL},
    {"round",  1, MATH_EMIT_INTRINSIC, "llvm.round.f64",  MATH_POLY_NONE, NULL},
    {"trunc",  1, MATH_EMIT_INTRINSIC, "llvm.trunc.f64",  MATH_POLY_NONE, NULL},
    /* powers / logs (float-only) */
    {"sqrt",   1, MATH_EMIT_INTRINSIC, "llvm.sqrt.f64",   MATH_POLY_NONE, NULL},
    {"pow",    2, MATH_EMIT_INTRINSIC, "llvm.pow.f64",    MATH_POLY_NONE, NULL},
    {"exp",    1, MATH_EMIT_INTRINSIC, "llvm.exp.f64",    MATH_POLY_NONE, NULL},
    {"exp2",   1, MATH_EMIT_INTRINSIC, "llvm.exp2.f64",   MATH_POLY_NONE, NULL},
    {"log",    1, MATH_EMIT_INTRINSIC, "llvm.log.f64",    MATH_POLY_NONE, NULL},
    {"log2",   1, MATH_EMIT_INTRINSIC, "llvm.log2.f64",   MATH_POLY_NONE, NULL},
    {"log10",  1, MATH_EMIT_INTRINSIC, "llvm.log10.f64",  MATH_POLY_NONE, NULL},
    /* trig (float-only) */
    {"sin",    1, MATH_EMIT_INTRINSIC, "llvm.sin.f64",    MATH_POLY_NONE, NULL},
    {"cos",    1, MATH_EMIT_INTRINSIC, "llvm.cos.f64",    MATH_POLY_NONE, NULL},
    {"tan",    1, MATH_EMIT_LIBM,      "tan",             MATH_POLY_NONE, NULL},
    {"asin",   1, MATH_EMIT_LIBM,      "asin",            MATH_POLY_NONE, NULL},
    {"acos",   1, MATH_EMIT_LIBM,      "acos",            MATH_POLY_NONE, NULL},
    {"atan",   1, MATH_EMIT_LIBM,      "atan",            MATH_POLY_NONE, NULL},
    {"atan2",  2, MATH_EMIT_LIBM,      "atan2",           MATH_POLY_NONE, NULL},
    /* hyperbolic (float-only) */
    {"sinh",   1, MATH_EMIT_LIBM,      "sinh",            MATH_POLY_NONE, NULL},
    {"cosh",   1, MATH_EMIT_LIBM,      "cosh",            MATH_POLY_NONE, NULL},
    {"tanh",   1, MATH_EMIT_LIBM,      "tanh",            MATH_POLY_NONE, NULL},
};
#define MATH_FUNC_COUNT ((int)(sizeof(MATH_FUNCS) / sizeof(MATH_FUNCS[0])))

typedef struct {
    const char *name;
    double value;
} MathConst;

static const MathConst MATH_CONSTS[] = {
    {"PI",  3.14159265358979323846},
    {"E",   2.71828182845904523536},
    {"TAU", 6.28318530717958647692},
};
#define MATH_CONST_COUNT ((int)(sizeof(MATH_CONSTS) / sizeof(MATH_CONSTS[0])))

/* ---- Lookup ---- */

bool builtin_math_lookup_fn(const char *name, int *out_arity,
                            MathEmitKind *out_kind,
                            const char **out_emit_name,
                            MathPolyKind *out_poly,
                            const char **out_int_prefix) {
    if (name == NULL) return false;
    for (int i = 0; i < MATH_FUNC_COUNT; i++) {
        if (strcmp(MATH_FUNCS[i].ls_name, name) == 0) {
            if (out_arity)      *out_arity      = MATH_FUNCS[i].arity;
            if (out_kind)       *out_kind       = MATH_FUNCS[i].kind;
            if (out_emit_name)  *out_emit_name  = MATH_FUNCS[i].emit_name;
            if (out_poly)       *out_poly       = MATH_FUNCS[i].poly;
            if (out_int_prefix) *out_int_prefix = MATH_FUNCS[i].int_prefix;
            return true;
        }
    }
    return false;
}

bool builtin_math_lookup_const(const char *name, double *out_value) {
    if (name == NULL) return false;
    for (int i = 0; i < MATH_CONST_COUNT; i++) {
        if (strcmp(MATH_CONSTS[i].name, name) == 0) {
            if (out_value) *out_value = MATH_CONSTS[i].value;
            return true;
        }
    }
    if (strcmp(name, "INF") == 0) {
        if (out_value) *out_value = INFINITY;
        return true;
    }
    if (strcmp(name, "NAN") == 0) {
        if (out_value) *out_value = NAN;
        return true;
    }
    return false;
}

/* ---- Public API: existence + type construction ---- */

/* Phase E.4: `io` migrated to pure-LS std/io.ls. The compiler no longer
   carries a built-in `io` module — `import io` resolves to LS_HOME/std/io.ls. */

bool builtin_module_exists(const char *name) {
    if (name == NULL) return false;
    return strcmp(name, "math") == 0 ||
           strcmp(name, "perf") == 0;
}

Type *builtin_module_make_type(Checker *c, const char *name) {
    (void)c;
    if (name == NULL) return NULL;
    if (strcmp(name, "math") == 0) return builtin_math_make_type();
    if (strcmp(name, "perf") == 0) return builtin_perf_make_type();
    return NULL;
}

Type *builtin_math_make_type(void) {
    Type *mod = type_module_new("math");
    mod->as.module.is_builtin = true;

    Type *f64 = type_f64();
    for (int i = 0; i < MATH_FUNC_COUNT; i++) {
        const MathFn *m = &MATH_FUNCS[i];
        Type **params = (Type **)malloc_safe((size_t)m->arity * sizeof(Type *));
        for (int k = 0; k < m->arity; k++) params[k] = f64;
        Type *fn_t = type_function(params, m->arity, f64, false);
        type_module_add_export(mod, m->ls_name, fn_t);
    }
    for (int i = 0; i < MATH_CONST_COUNT; i++) {
        type_module_add_export(mod, MATH_CONSTS[i].name, f64);
    }
    type_module_add_export(mod, "INF", f64);
    type_module_add_export(mod, "NAN", f64);

    return mod;
}
