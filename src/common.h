/* common.h — Public types, macros, and error codes */
#ifndef LS_COMMON_H
#define LS_COMMON_H

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdarg.h>

/* Dynamic array growth */
#define GROW_CAPACITY(cap) ((cap) < 8 ? 8 : (cap) * 2)
#define GROW_ARRAY(type, ptr, new_count) \
    (type *)realloc_safe((ptr), sizeof(type) * (new_count))

/* LsString minimum capacity — all dynamic strings allocate at least this many bytes */
#define LS_MIN_STR_CAP 16

/* LsString cap sentinel values (stored in the i32 cap field).
   These must be kept in sync with the cap checks in codegen.c and runtime/builtins.c.

   LS_CAP_STATIC   (0)  — points to .rodata / string literal; never free, never clone.
   LS_CAP_MOVED    (-1) — ownership was transferred; skip drop (the new owner frees it).
   LS_CAP_BORROWED (-2) — caller-owned, callee borrows; must clone before storing into
                           owned structures (enum/struct field), must NOT free on scope exit.
                           Introduced in M-2 to disambiguate static-literal from borrowed. */
#define LS_CAP_STATIC   0
#define LS_CAP_MOVED    (-1)
#define LS_CAP_BORROWED (-2)

/* CG_DEBUG: compile-time flag for *codegen-internal* memory tracing.
   When enabled (=1), the codegen layer injects runtime printf calls into the
   generated LLVM IR at every automatic alloc / clone / free / drop point.
   Output format examples:
     [cg] str.clone  cap=32 len=5 ptr=0x...
     [cg] str.free   cap=32 ptr=0x...
     [cg] str.skip   (static/moved)
     [cg] str.moved  cap=-1
     [cg] struct.clone  type=Person
     [cg] struct.drop   type=Person moved=0
     [cg] scope.drop  var=s  type=string
     [cg] scope.drop  var=p  type=struct
     [cg] vec.grow   old_cap=4 new_cap=8
     [cg] vec.clone  len=3 cap=8
     [cg] arr.clone  size=5

   FOR ORDINARY LEAK / DOUBLE-FREE / USE-AFTER-FREE DEBUGGING — DO NOT enable
   CG_DEBUG. Use the runtime memcheck instead:

     ls run --memcheck file.ls                          # leak/dfree report
     LS_MEMCHECK_VERBOSE=1 ls run --memcheck file.ls    # full alloc/free trace
     LS_MEMCHECK_STRICT=1  ls run --memcheck file.ls    # exit 2 on violation

   The runtime path covers every actual heap operation with full site info
   (kind + file:line:col) plus a captured LS call-stack backtrace, and adds
   zero noise to user binaries when off.

   CG_DEBUG is reserved for debugging the codegen layer ITSELF — verifying
   move tracking (str.skip / str.moved), scope cleanup decisions, and
   non-heap events (vec.grow size announcements) that ls_mc_* doesn't see.

   Default OFF — opt in via `cmake -DLS_CG_DEBUG=ON` when developing on
   codegen.c. New codegen code that manages memory should still ADD a
   CG_DEBUG trace block (the macro compiles to nothing when off, so the
   trace is free in normal builds). */
#ifndef CG_DEBUG
#define CG_DEBUG 0
#endif

static inline void *realloc_safe(void *ptr, size_t size) {
    void *result = realloc(ptr, size);
    if (result == NULL && size != 0) {
        fprintf(stderr, "fatal: out of memory\n");
        exit(1);
    }
    return result;
}

static inline void *malloc_safe(size_t size) {
    void *result = malloc(size);
    if (result == NULL && size != 0) {
        fprintf(stderr, "fatal: out of memory\n");
        exit(1);
    }
    return result;
}

#endif /* LS_COMMON_H */
