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

    /* G1: User-defined generic struct templates.
       Registered when check_struct_decl sees type_param_count > 0.
       Instantiated lazily in resolve_type_node. */
    struct {
        const char *base_name;      /* "Pair", "Stack" — points into AST (not owned) */
        char      **type_params;    /* ["T", "U"] — points into AST (not owned) */
        int         type_param_count;
        AstNode    *decl_node;      /* AST_STRUCT_DECL (not owned) */
        AstNode    *impl_node;      /* AST_IMPL_DECL (not owned), NULL initially; G1.5 */
        const char *module_name;    /* owning module's import path; NULL = root/same-file.
                                       Used to detect same-name generics from 2+ modules
                                       (cross-module ambiguity) and to validate
                                       `mod.Stack(int)` qualifiers. */
    } *struct_templates;
    int struct_template_count;
    int struct_template_cap;

    /* G2: User-defined generic function templates.
       Registered when check_fn_decl sees type_param_count > 0.
       Instantiated lazily at call sites like identity(int)(42). */
    struct {
        const char *name;           /* "identity" — points into AST (not owned) */
        char      **type_params;    /* ["T"] — points into AST (not owned) */
        int         type_param_count;
        AstNode    *decl_node;      /* AST_FN_DECL (not owned) */
    } *fn_templates;
    int fn_template_count;
    int fn_template_cap;

    /* Type-context hint for variant ctor disambiguation.  Set by check_var_decl
       (and similar) before checking an initializer that targets a known type;
       used by find_variant to prefer matches in this enum. */
    Type *expected_type;

    /* Type alias registry (Phase A closure prerequisite). Each entry maps a
       user-declared name (`type Adder = Block(int) -> int`) to its resolved
       target Type. resolve_type_node consults this before falling back to
       struct/enum lookups. */
    struct { const char *name; Type *type; } *type_aliases;
    int type_alias_count;
    int type_alias_cap;

    /* Method registry (struct_name -> methods) */
    struct {
        const char *struct_name;
        struct { const char *name; Type *type; bool is_static; int self_borrow_kind;
                 /* L-002: NULL = inherent method; else the interface name that
                    provided it (`methods T: Iface`). Cross-origin same-name methods
                    coexist (inherent priority on bare dispatch; qualified call
                    `Iface.m(recv)` selects an interface one). */
                 const char *origin_iface;
                 /* The method's AST_FN_DECL (NULL for the auto-generated __drop).
                    Used to flag iface_method_contended for codegen symbol mangling. */
                 AstNode *decl_node; } *methods;
        int method_count;
        int method_cap;
    } *impl_registry;
    int impl_count;
    int impl_cap;

    /* Trait registry — trait declarations with method signatures.
       Registered in forward_pass from AST_TRAIT_DECL nodes. */
    struct {
        const char *name;           /* trait name, owned (strdup'd) */
        struct {
            const char *name;       /* method name, owned */
            Type *type;             /* TYPE_FUNCTION: full method signature */
            bool is_static;         /* true = static (no self) trait method */
            int self_borrow_kind;   /* 0=none, 1=&self, 2=&!self */
        } *methods;
        int method_count;
    } *trait_registry;
    int trait_count;
    int trait_cap;

    /* Trait implementation registry — records which structs implement which traits. */
    struct {
        const char *trait_name;
        const char *struct_name;
    } *trait_impls;
    int trait_impl_count;
    int trait_impl_cap;

    /* G1.5: Pending generic method instantiations — cloned fn_decl AST nodes
       that have been type-checked and are ready for codegen. */
    struct {
        AstNode *cloned_fn;     /* owned — ast_free after codegen */
        char    *mangled_name;  /* e.g. "Pair(int,string).get_first", owned */
        Type    *struct_type;   /* the instantiated struct Type, not owned */
    } *pending_generic_methods;
    int pending_gm_count;
    int pending_gm_cap;

    /* Lazy generic impl methods. Generic struct instantiation registers concrete
       method signatures immediately, but ordinary method bodies are checked and
       queued for codegen only when a call site actually uses them. */
    struct {
        char    *mangled_name;      /* "RawVec(int).contains", owned */
        AstNode *template_method;   /* original AST_FN_DECL, not owned */
        Type    *method_type;       /* concrete TYPE_FUNCTION, not owned */
        Type    *struct_type;       /* concrete TYPE_STRUCT, not owned */
        char   **tp_names;          /* generic impl param names, not owned */
        Type   **type_args;         /* concrete args array, owned (Type* not owned) */
        int      tp_count;
        int      state;             /* 0=pending, 1=checking, 2=done */
    } *lazy_generic_methods;
    int lazy_gm_count;
    int lazy_gm_cap;

    /* Method-level generic method templates. Methods inside a generic impl
       block that have their own type params (e.g. fn map<U>(...)) are not
       registered in impl_registry during instantiate_impl_method_types.
       Instead, they are stored here and instantiated on demand at the call
       site when v.map(string)(args) provides the method-level type args. */
    struct {
        char     *method_name;     /* "map" — points into AST (not owned) */
        char     *impl_key;        /* "RawVec(int)" — strdup, owned */
        AstNode  *method_ast;      /* AST_FN_DECL — points into impl_decl (not owned) */
        char    **impl_tp_names;   /* ["T"] — points into struct template (not owned) */
        Type    **impl_tp_types;   /* [int] — Type* from struct instantiation, not owned */
        int       impl_tp_count;
    } *generic_impl_method_templates;
    int generic_impl_mt_count;
    int generic_impl_mt_cap;

    /* Self type context: set during check_impl_decl / check_impl_trait_decl
       so that resolve_type_node can resolve 'Self' to the implementing struct
       or enum. Only one is set at a time (mutually exclusive). */
    Type *current_impl_struct_type;
    Type *current_impl_enum_type;

    /* Move semantics tracking */
    bool in_return_expr;       /* true if currently checking a return expression */
    bool silent_move_errors;   /* Phase B: suppress move errors during loop discovery pass */
    bool silent_type_errors;   /* suppress type errors during transitive trait re-registration
                                  (the trait was already validated in its home module; resolving
                                  its signatures in an importer's scope can spuriously fail when a
                                  signature references a same-module type the importer hasn't
                                  bound yet — e.g. interface Reflect { reflect() -> TypeInfo }) */

    /* __drop detection: true if currently checking a user-defined __drop() method */
    bool in_user_defined_drop;

    /* map() closure return-type inference: when non-NULL, AST_RETURN writes the
       inferred return type here instead of erroring about NULL current_fn_return. */
    Type **closure_infer_return_slot;

    /* L-009.1 / A2: name of the module this checker is type-checking, e.g.
       "mod_a" or "std.json"; NULL for the root/main program. Used to module-
       prefix generic instantiation symbol names so same-named generic functions
       in different modules don't collide (silent-wrong) at codegen. */
    const char *module_name;

    /* A1 clone-elision (checker_elide.c): master switch for the last-use
       analysis pass that downgrades provably-final by-value clones to moves.
       Default true; set from checker_elide_env_enabled() at Checker creation
       (LS_NO_ELIDE=1 disables — A/B comparison + escape hatch). */
    bool elide_pass_enabled;

    /* B-4: struct/enum type names that are exported by 2+ imported modules.
       Bare (unqualified) reference to such a name is an error — the user must
       qualify it as `mod.Type` / `alias.Type`. Qualified references resolve
       precisely via the module's export table and ignore this set. */
    const char **ambiguous_types;
    int ambiguous_type_count;
    int ambiguous_type_cap;
} Checker;

/* G1.5: Output struct for pending generic method instantiations.
   Ownership of cloned_fn and mangled_name transfers to the caller. */
typedef struct {
    struct {
        AstNode *cloned_fn;
        char    *mangled_name;
        Type    *struct_type;
    } *methods;
    int count;
} CheckerGenericMethods;

/* Type-check an AST_PROGRAM node. Returns true if no errors.
   Fills in resolved_type on expression nodes.
   registry may be NULL (no module support).
   out_gm may be NULL; if non-NULL, pending generic method instantiations
   are transferred here (caller must free after codegen). */
bool checker_check(AstNode *program, const char *source_path,
                   struct ModuleRegistry *registry,
                   CheckerGenericMethods *out_gm);

/* `ls inspect`: type-check `program`, then print the fields + methods of the
   named concrete struct/enum to stdout (static reflection, Stage 1.5 of
   docs/plan_static_reflection.md). Returns 0 on success, 1 if the file fails to
   type-check or the type is not found. */
int checker_inspect(AstNode *program, const char *source_path,
                    struct ModuleRegistry *registry, const char *type_query);

/* Like checker_inspect but gates which sections print (REPL :fields / :methods /
   :inspect). want_fields prints fields/variants; want_methods prints methods. */
int checker_inspect_ex(AstNode *program, const char *source_path,
                       struct ModuleRegistry *registry, const char *type_query,
                       bool want_fields, bool want_methods);

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

/* G1: Instantiate a user-defined generic struct type with concrete type args.
   Returns the cached/freshly-built TYPE_STRUCT.  NULL if base_name is not a
   registered struct template. */
Type *checker_instantiate_struct(Checker *c,
                                 const char *base_name,
                                 Type **type_args, int type_arg_count,
                                 int line, int col);

#endif /* LS_CHECKER_H */
