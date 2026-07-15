/* mangle.h — single authority for the "<mod>__<name>" module-prefix symbol
   scheme (L-009). Both the checker and codegen must produce byte-identical
   strings for the same (module_path, name) pair — this header is the shared
   contract that used to be duplicated across 3 hand-rolled implementations
   (checker_module_type_llvmname and the generic-method instantiation
   prefixing in checker.c, cg_module_fn_symbol in codegen.c). See
   docs/plan_arch_round2_backlog.md Task 2.1.

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
   (free() it). Never returns NULL: allocation failure aborts via
   malloc_safe (common.h discipline) — callers must not add NULL checks. */
char *mangle_module_symbol(const char *module_name, const char *name);

/* ---- growable generic instance-name builder (Task 2.2) ----

   The other half of the "instance name" text contract: `Base(arg1,arg2)`
   strings for generic struct/enum instantiation (e.g. "Vec(int)",
   "Option(mod__Node)"). This used to be duplicated as fixed 256/512-byte
   `char buf[...]` + snprintf-loop pairs at each instantiation call site in
   checker.c, with silent truncation risk on deep nesting (two call sites
   could truncate at different depths and def/use symbols would then
   mismatch). MangleBuf replaces the fixed buffer; the "which name a Type*
   contributes" logic (module llvm_name preferred over the bare type name)
   stays a single function (mangle_type_arg_name), same as before the move —
   only the buffer growability changed.

   Forward-declares Type rather than including types.h to keep this header's
   dependency surface minimal (same rationale as the file header above);
   mangle.c includes types.h to implement the two Type-aware functions. Any
   TU already including types.h (checker.c, codegen*.c) gets an identical
   repeated `typedef struct Type Type;` — legal in C11, and already the
   pattern ast.h/types.h use between themselves. */
typedef struct Type Type;

typedef struct MangleBuf {
    char *p;     /* malloc'd, NUL-terminated; NULL before the first append */
    size_t len;  /* strlen(p); 0 when p == NULL */
    size_t cap;  /* bytes allocated at p, including the NUL slot; 0 when p == NULL */
} MangleBuf;

/* Zero-initializes *b. Safe to mangle_buf_append into directly afterward. */
void mangle_buf_init(MangleBuf *b);
/* Frees b->p (if any) and resets *b to the zero state. Safe to call more
   than once and safe on a never-appended-to buffer. */
void mangle_buf_free(MangleBuf *b);
/* Appends s (may be "", must not be NULL) to *b, growing the backing
   allocation as needed (geometric growth). *b remains NUL-terminated after
   the call. */
void mangle_buf_append(MangleBuf *b, const char *s);
/* Transfers ownership of b->p to the caller (who must free() it) and resets
   *b to the zero state (mangle_buf_free is then a no-op / *b is reusable).
   Returns a malloc'd copy of "" (never NULL) if nothing was ever appended,
   matching mangle_module_symbol's "never returns NULL" discipline. */
char *mangle_buf_take(MangleBuf *b);

/* F6b: the name a concrete type arg contributes to a generic instance key
   ("Vec(...)", "Option(...)"). Module-defined struct/enum args use their
   module-prefixed `llvm_name` (e.g. "ma__Node") instead of the bare name —
   two modules each defining `Node` would otherwise both mangle to
   "Option(Node)" and collide (the second instantiation cache-hits the
   first, conflating distinct layouts). Primitives/non-module types keep
   their bare `type_name`. Single authority shared by the struct- and
   enum-template instantiation paths AND the textual Self substitution in
   checker.c's type_equals_with_self (which compares instance names, so it
   must render `Self` exactly the way the instance key was built); mirrors
   impl_key_of_type's llvm_name ?? name (checker.c, different Type* -> key
   function, deliberately not merged with this one — see its own comment).
   Returns a pointer with the same lifetime as `at` (a Type field or a
   type_name() string) — never a buffer owned by the caller. */
const char *mangle_type_arg_name(const Type *at);
/* mangle_buf_append(b, mangle_type_arg_name(at)) — convenience wrapper for
   the "Base(arg1,arg2)" build loops. */
void mangle_append_type_arg(MangleBuf *b, const Type *at);

#endif /* LS_MANGLE_H */
