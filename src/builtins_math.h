/* builtins_math.h — Compiler-level built-in `math` module.
 *
 * Exposes a stdlib `math` module that does NOT correspond to a source file.
 * Entries are emitted directly as LLVM intrinsics or libm extern calls,
 * giving zero-overhead math access without FFI.
 *
 * User-priority shadowing is enforced by the import handler: if the user
 * has `math.ls` next to the importing file, that wins; the built-in is
 * only returned when no user file is present.
 */
#ifndef LS_BUILTINS_MATH_H
#define LS_BUILTINS_MATH_H

#include "ast.h"
#include "types.h"

/* Forward declaration for the public dispatcher; full def in checker.h. */
typedef struct Checker Checker;

/* ---- Type / lookup API (no LLVM deps; safe for type-checker tests) ---- */

/* Returns true if `name` matches a compiler built-in stdlib module. */
bool builtin_module_exists(const char *name);

/* Build the TYPE_MODULE for the named built-in module, fully populated
   with exports. Caller owns the returned Type. Returns NULL for unknown
   built-in names. `c` must be the active Checker (some modules — e.g. io —
   need template instantiation against the checker's enum registry). May
   pass NULL only for modules that don't need it (math). */
Type *builtin_module_make_type(Checker *c, const char *name);

/* Math-only constructor (kept for tests/test_types which don't have a
   Checker). Equivalent to builtin_module_make_type(NULL, "std.core.math"). */
Type *builtin_math_make_type(void);

/* Math function info, used by codegen to pick an emit strategy. */
typedef enum {
    MATH_EMIT_INTRINSIC,  /* "llvm.<name>.f64" — LLVM lowers automatically */
    MATH_EMIT_LIBM,       /* libm extern — linked via C runtime */
} MathEmitKind;

/* Polymorphism kind for the function. NONE = always f64; INT_OR_FLOAT =
   dispatch by arg type to either int intrinsic (abs/min/max) or float. */
typedef enum {
    MATH_POLY_NONE,            /* always f64 (sqrt/sin/log/...) */
    MATH_POLY_INT_OR_FLOAT,    /* abs/min/max: int args → int result */
} MathPolyKind;

/* Lookup info for a math function. Returns true on hit. Out params are
   filled only on hit. The *_iN_prefix is the prefix appended with bit
   width by codegen for integer dispatch (e.g. "llvm.abs.i" → "llvm.abs.i32").
   For min/max the *signed* prefix is used; the unsigned variant is the
   same prefix with the leading `s` replaced by `u`. NULL out pointers are
   tolerated. */
bool builtin_math_lookup_fn(const char *name, int *out_arity,
                            MathEmitKind *out_kind,
                            const char **out_emit_name,
                            MathPolyKind *out_poly,
                            const char **out_int_prefix);

/* Lookup a math constant. Returns true if `name` is a known constant.
   *out_value receives the value (with INFINITY/NAN for the special names). */
bool builtin_math_lookup_const(const char *name, double *out_value);

/* ---- Codegen API (separate translation unit; LLVM-dependent) ---- */
#ifdef LS_INCLUDE_CODEGEN
#include "codegen.h"

/* Emit IR for a call `math.<fn_name>(args...)`. Returns the LLVM value
   produced, or NULL on error. */
LLVMValueRef builtin_math_emit_call(CodegenContext *ctx, const char *fn_name,
                                    AstNode **args, int arg_count);

/* Emit IR for reading the constant `math.<name>`. */
LLVMValueRef builtin_math_emit_const(CodegenContext *ctx, const char *name);
#endif

#endif /* LS_BUILTINS_MATH_H */
