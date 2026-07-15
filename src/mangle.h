/* mangle.h — single authority for the "<mod>__<name>" module-prefix symbol
   scheme (L-009). Both the checker and codegen must produce byte-identical
   strings for the same (module_path, name) pair — this header is the shared
   contract that used to be duplicated across 3 hand-rolled implementations
   (src/checker.c:~334 checker_module_type_llvmname, src/checker.c:~4319
   generic-method instantiation prefixing, src/codegen.c:49
   cg_module_fn_symbol). See docs/plan_arch_round2_backlog.md Task 2.1.

   Deliberately minimal dependency surface (stdlib only) so both checker.c
   and codegen.c (and anything below them) can include it without pulling in
   types.h/ast.h. */
#ifndef LS_MANGLE_H
#define LS_MANGLE_H

#include <stddef.h>

/* Builds "<mod>__<name>" with every '.' in module_name replaced by '_',
   followed by the literal separator "__", followed by name verbatim.
   When module_name is NULL or empty, returns a malloc'd copy of name
   unchanged (root/main-file symbols stay unmangled).
   Returns a malloc'd, NUL-terminated string; the caller takes ownership
   (free() it). Never returns NULL (malloc_safe-style abort-on-OOM is the
   caller's business — this uses plain malloc/exits via the same discipline
   as the rest of the frontend). */
char *mangle_module_symbol(const char *module_name, const char *name);

#endif /* LS_MANGLE_H */
