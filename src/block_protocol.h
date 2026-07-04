/* block_protocol.h — Block container-ownership protocol: METHOD-NAME registry.
   SINGLE AUTHORITY (audit B-2, plan_footgun_remediation stage 5).

   Whether a Block argument / result changes ownership at a container method
   call is decided by MATCHING THE METHOD'S NAME against the two tables below.
   This is a pure-LS-container convention (Vec/Map/Set/... are ordinary
   structs; codegen cannot see "this method stores its argument"), so the
   names ARE the protocol. Consequences of the string match:

     - store sink   : caller-side env ownership is RELINQUISHED (named var's
                      env nulled / literal temp popped / rvalue temp claimed)
                      because the container's raw `self.data[i] = x` store
                      takes the env. Consumer: codegen_expr.c Block-arg
                      handling (F5 / VR-LIM-017).
     - alias source : the returned Block ALIASES the container's env, so a
                      BIND out of it deep-clones (cg_emit_block_env_clone) at
                      the binding site; a discarded rvalue call `v[i](..)`
                      borrows and is NOT cloned (leak-free). Consumer:
                      codegen_own.c cg_block_source_is_aliased (F5/Phase G).

   A USER struct that happens to define one of these names with a Block in
   the signature gets container semantics whether it wants them or not —
   that exposure is linted (checker_decl.c block-protocol lint, warning
   only) and tracked in docs/known_limitations.md. Long-term direction is a
   marker interface / attribute instead of a name list (recorded there; not
   built in this round).

   Receiver-conditional member NOT in these tables:
     - "run" on a Task(T) receiver is a store sink (std.task forwards the
       closure into the worker thread which frees the env). The receiver
       type check lives at the codegen_expr.c call site — an unrelated
       method named `run` that merely borrows a closure is unaffected.

   Changing either table changes ownership behavior at every call site of
   these names on every struct — treat as a semantic change (corpus +
   memcheck + value-probe evidence), never as a rename convenience. */
#ifndef LS_BLOCK_PROTOCOL_H
#define LS_BLOCK_PROTOCOL_H

#include <stdbool.h>
#include <string.h>

/* Copy-out readers whose returned Block aliases the container's env.
   Why each name is present:
     get / get!   : Vec/Map read accessors (F5 — bind-site clone keeps
                    discarded copy-out rvalues leak-free)
     __index      : `v[i]` desugar (same read path as get)
     first / last : Vec end readers (same aliasing shape) */
static inline bool cg_block_method_is_alias_source(const char *name)
{
    return name != NULL &&
           (strcmp(name, "get") == 0 || strcmp(name, "get!") == 0 ||
            strcmp(name, "__index") == 0 || strcmp(name, "first") == 0 ||
            strcmp(name, "last") == 0);
}

/* Container-storing methods that take ownership of a Block argument's env.
   Why each name is present:
     push / insert / set : Vec/Map storing mutators (F5 / VR-LIM-017 origin)
     __index_set         : `v[i] = x` desugar
     __from_list         : `[..]` literal ctor lowering
     extend              : bulk append (moves each element in)
     _insert_no_grow     : Map._grow rehash internal insert (fc741bf O2 —
                           relocated entries are @take'n into a local and
                           moved into the new table; without the transfer
                           the caller also drops the local Block env). */
static inline bool cg_block_method_is_store_sink(const char *name)
{
    return name != NULL &&
           (strcmp(name, "push") == 0 || strcmp(name, "insert") == 0 ||
            strcmp(name, "set") == 0 || strcmp(name, "__index_set") == 0 ||
            strcmp(name, "__from_list") == 0 || strcmp(name, "extend") == 0 ||
            strcmp(name, "_insert_no_grow") == 0);
}

#endif /* LS_BLOCK_PROTOCOL_H */
