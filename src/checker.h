/* checker.h — Type checker interface */
#ifndef LS_CHECKER_H
#define LS_CHECKER_H

#include "ast.h"
#include "types.h"
#include "symtable.h"

#define CHECKER_MAX_ERRORS 20

/* Forward declaration (full definition in module.h) */
struct ModuleRegistry;

typedef struct Checker {
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

    /* Enum type registry (mangled-name keyed; e.g. "Option(int)" and
       "Option(string)" are distinct entries). User-declared enums register
       under their plain name; builtin Option/Result instantiations register
       under their mangled name. */
    struct { const char *name; Type *type; } *enum_types;
    int enum_type_count;
    int enum_type_cap;

    /* Enum templates (Option / Result).  Each variant payload slot is either
       a concrete type or a type-parameter index (param_idx >= 0). */
    struct {
        const char *base_name;       /* "Option" or "Result" */
        int type_param_count;
        struct {
            const char *name;
            int payload_count;
            struct { int param_idx; Type *concrete; } *payload;
        } *variants;
        int variant_count;
    } *enum_templates;
    int enum_template_count;
    int enum_template_cap;

    /* Type-context hint for variant ctor disambiguation.  Set by check_var_decl
       (and similar) before checking an initializer that targets a known type;
       used by find_variant to prefer matches in this enum. */
    Type *expected_type;

    /* Method registry (struct_name -> methods) */
    struct {
        const char *struct_name;
        struct { const char *name; Type *type; bool is_static; int self_borrow_kind; } *methods;
        int method_count;
        int method_cap;
    } *impl_registry;
    int impl_count;
    int impl_cap;

    /* Move semantics tracking */
    bool in_return_expr;       /* true if currently checking a return expression */
    bool silent_move_errors;   /* Phase B: suppress move errors during loop discovery pass */

    /* __drop detection: true if currently checking a user-defined __drop() method */
    bool in_user_defined_drop;
} Checker;

/* Type-check an AST_PROGRAM node. Returns true if no errors.
   Fills in resolved_type on expression nodes.
   registry may be NULL (no module support). */
bool checker_check(AstNode *program, const char *source_path,
                   struct ModuleRegistry *registry);

/* ---- Public API for cross-TU built-in stdlib modules (e.g. `io`).
   These let module-builders (builtins_io.c) reuse the checker's struct/enum
   registries and template instantiation logic. Safe to call only during
   `checker_check` (i.e. while a Checker exists for the importing program). */

/* Register a struct type in the checker so it's reachable by name lookup. */
void checker_register_struct(Checker *c, const char *name, Type *type);

/* Register an enum type in the checker (key is the type's mangled name). */
void checker_register_enum(Checker *c, const char *name, Type *type);

/* Look up an enum type by its mangled name (e.g. "Result(string,string)"). */
Type *checker_find_enum(Checker *c, const char *name);

/* Instantiate `Result(T, E)` with the given concrete types. Returns the
   cached/freshly-built TYPE_ENUM. NULL on failure. */
Type *checker_instantiate_result(Checker *c, Type *t, Type *e);

/* Instantiate `Option(T)`. NULL on failure. */
Type *checker_instantiate_option(Checker *c, Type *t);

#endif /* LS_CHECKER_H */
