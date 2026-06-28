/* emit_c.h — emit reusable C (Intel intrinsics) for the numeric / SIMD kernel
   subset of LS. Walks a type-checked AST and writes a self-contained .c file
   (#include <immintrin.h>) so LS-authored kernels (std.sci.nn / std.sci.simd
   style) can be dropped into a C/C++ codebase without the LS runtime.

   Subset only: free functions over scalars / pointers / arrays / Simd(T,N) +
   control flow + __simd_* intrinsics. Str / Vec / Map / closures / enums /
   ownership-move / trait methods are out of subset and produce a clear error.
   See docs/plan_simd.md (C emitter) and "emit-c" in main.c.

   Simd(T,N) maps to the width-determined intrinsic register: Simd(f32,16) ->
   __m512, Simd(f32,8) -> __m256, etc.; ops to _mm{W}_{op}_{ps,pd,epiNN}. */
#ifndef LS_EMIT_C_H
#define LS_EMIT_C_H

#include "ast.h"

/* Options for emit_c (all optional; zero-initialize for defaults). */
typedef struct {
    const char **only;       /* if non-NULL, emit only these function names */
    int only_count;
    bool skip_unsupported;   /* emit all in-subset fns; warn-skip the rest
                                instead of failing the whole file (ignored when
                                `only` is set — a named out-of-subset fn errors) */
} EmitCOpts;

/* Emit C for the in-subset top-level functions of `program` (already parsed and
   type-checked) to `out_path`. `src_path` is used only in diagnostics. With the
   default options, any out-of-subset function aborts the whole emit (nothing is
   written). See EmitCOpts for selective / skip modes. Returns 0 on success. */
int emit_c(AstNode *program, const char *out_path, const char *src_path,
           const EmitCOpts *opts);

#endif /* LS_EMIT_C_H */
