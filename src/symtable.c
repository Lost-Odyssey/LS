/* symtable.c — Scoped symbol table implementation */
#include "symtable.h"
#include <string.h>

/* Create a new scope with optional parent */
Scope *scope_new(Scope *parent) {
    Scope *s = (Scope *)malloc_safe(sizeof(Scope));
    s->symbols = NULL;
    s->count = 0;
    s->capacity = 0;
    s->parent = parent;
    s->depth = parent ? parent->depth + 1 : 0;
    return s;
}

/* Free a scope and all its symbol entries */
void scope_free(Scope *scope) {
    if (scope == NULL) return;
    for (int i = 0; i < scope->count; i++) {
        free((void *)scope->symbols[i].name);
    }
    free(scope->symbols);
    free(scope);
}

/* Define a new symbol in the current scope */
Symbol *scope_define(Scope *s, const char *name, Type *type) {
    /* Check for duplicate in current scope only */
    for (int i = 0; i < s->count; i++) {
        if (strcmp(s->symbols[i].name, name) == 0) {
            return NULL; /* Already defined */
        }
    }

    /* Grow array if needed */
    if (s->count >= s->capacity) {
        s->capacity = GROW_CAPACITY(s->capacity);
        s->symbols = GROW_ARRAY(Symbol, s->symbols, s->capacity);
    }

    /* Copy name */
    size_t len = strlen(name);
    char *name_copy = (char *)malloc_safe(len + 1);
    memcpy(name_copy, name, len + 1);

    Symbol *sym = &s->symbols[s->count++];
    sym->name = name_copy;
    sym->type = type;
    sym->is_mutable = true;
    sym->scope_depth = s->depth;
    sym->is_moved = false;
    sym->is_maybe_moved = false;
    sym->is_returning = false;
    sym->is_borrow = false;
    sym->is_mut_borrow = false;
    sym->is_borrow_src = false;
    return sym;
}

/* Resolve a symbol by walking up the scope chain */
Symbol *scope_resolve(Scope *s, const char *name) {
    for (Scope *cur = s; cur != NULL; cur = cur->parent) {
        for (int i = 0; i < cur->count; i++) {
            if (strcmp(cur->symbols[i].name, name) == 0) {
                return &cur->symbols[i];
            }
        }
    }
    return NULL;
}

/* Resolve only in the given scope (no parent traversal) */
Symbol *scope_resolve_local(Scope *s, const char *name) {
    for (int i = 0; i < s->count; i++) {
        if (strcmp(s->symbols[i].name, name) == 0) {
            return &s->symbols[i];
        }
    }
    return NULL;
}
