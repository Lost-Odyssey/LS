/* checker.h — Type checker interface */
#ifndef LS_CHECKER_H
#define LS_CHECKER_H

#include "ast.h"
#include "types.h"
#include "symtable.h"

#define CHECKER_MAX_ERRORS 20

/* Forward declaration (full definition in module.h) */
struct ModuleRegistry;

typedef struct {
    Scope *current_scope;       /* Current symbol scope */
    Type *current_fn_return;    /* Expected return type of current function */
    int error_count;
    bool had_error;
    const char *source_path;
    struct ModuleRegistry *registry; /* Module registry (may be NULL) */

    /* Struct type registry (for looking up struct types by name) */
    struct { const char *name; Type *type; } *struct_types;
    int struct_type_count;
    int struct_type_cap;

    /* Method registry (struct_name -> methods) */
    struct {
        const char *struct_name;
        struct { const char *name; Type *type; bool is_static; } *methods;
        int method_count;
        int method_cap;
    } *impl_registry;
    int impl_count;
    int impl_cap;

    /* Move semantics tracking */
    bool in_return_expr;       /* true if currently checking a return expression */

    /* __drop detection: true if currently checking a user-defined __drop() method */
    bool in_user_defined_drop;
} Checker;

/* Type-check an AST_PROGRAM node. Returns true if no errors.
   Fills in resolved_type on expression nodes.
   registry may be NULL (no module support). */
bool checker_check(AstNode *program, const char *source_path,
                   struct ModuleRegistry *registry);

#endif /* LS_CHECKER_H */
