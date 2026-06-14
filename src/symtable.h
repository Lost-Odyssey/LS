/* symtable.h — Symbol table with scoped chains */
#ifndef LS_SYMTABLE_H
#define LS_SYMTABLE_H

#include "types.h"

typedef struct Symbol {
    const char *name;       /* Owned copy of the name */
    Type *type;             /* Semantic type (not owned — points into type registry) */
    bool is_mutable;        /* All variables mutable for now */
    int scope_depth;
    bool is_moved;          /* true if value has been moved on ALL paths (cannot be used) */
    bool is_maybe_moved;    /* Phase B: true if value moved on SOME path (MAYBE_MOVED = death) */
    bool is_returning;      /* true if variable is in a return expression (skip drop) */
    bool is_borrow;         /* true for &T function parameters — cannot be moved, reassigned,
                               or mutated. Sym->type holds the pointee,
                               NOT TYPE_REFERENCE — this flag is the sole marker. */
    bool is_mut_borrow;     /* true for &!T function parameters — cannot be moved, but
                               CAN be reassigned and mutated. Same type-unwrap convention
                               as is_borrow (type is the pointee T). Mutually exclusive
                               with is_borrow. */
    bool is_borrow_src;     /* Phase 1 (borrow extension): true while this owned variable
                               is the referent of a live named local borrow (`&T r = &x`).
                               Moving it would dangle the borrow, so checker_try_mark_moved
                               rejects moves of a borrow source. Conservative: set on the
                               first borrow and never cleared within the function. */
} Symbol;

typedef struct Scope {
    Symbol *symbols;        /* Dynamic array of symbols */
    int count;
    int capacity;
    struct Scope *parent;   /* Enclosing scope (NULL for global) */
    int depth;              /* Nesting depth (0 = global) */
} Scope;

/* Create a new scope, optionally with a parent */
Scope *scope_new(Scope *parent);

/* Free a scope and its symbol entries (does NOT free parent) */
void scope_free(Scope *scope);

/* Define a symbol in the current scope. Returns the symbol, or NULL if already defined in this scope */
Symbol *scope_define(Scope *s, const char *name, Type *type);

/* Resolve a symbol by name, searching up the scope chain. Returns NULL if not found */
Symbol *scope_resolve(Scope *s, const char *name);

/* Resolve a symbol only in the current scope (no parent lookup) */
Symbol *scope_resolve_local(Scope *s, const char *name);

#endif /* LS_SYMTABLE_H */
