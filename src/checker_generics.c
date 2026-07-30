/* checker_generics.c — generics / type-resolution cluster, split out of
   checker.c (Batch 7 Task 7.5, docs/plan_arch_round2_backlog.md §7.5):
   G1 struct/enum template registry, type aliases, variant lookup,
   instantiate_template / checker_instantiate_struct, resolve_type_node
   (+ _with_substitution), impl/method registry, module-type builders for
   generic bodies, and the method-level generic instantiation family.
   Bodies moved verbatim — cross-TU surface is in checker_internal.h. */
#include "checker.h"
#include "checker_internal.h"
#include "builtins_math.h"
#include "mangle.h"
#include "diag.h"
#include "module.h"
#include "builtins_perf.h"
#include <stdio.h>
#include <string.h>
#include <stdarg.h>
#include <stdlib.h>
#include <math.h>

/* File-local helpers (single-TU). */
static void bind_generic_defining_module_imports(Checker *c, const char *module_path);
static Type *build_module_type_with_exports(Checker *c, const char *path);
static bool check_and_queue_generic_method(Checker *c, Type *struct_type, const char *mangled_name, AstNode *method, Type *mtype, char **tp_names, Type **type_args, int tp_count, int line, int col);
static bool check_method_where_bounds(Checker *c, AstNode *method, const char *qualified_name, char **tp_names, Type **type_args, int tp_count);
static int find_struct_template_idx_pull(Checker *c, const char *base_name);
static bool generic_method_is_eager(const char *name);
static void instantiate_impl_method_types(Checker *c, Type *struct_type, const char *mangled_name, AstNode *impl_node, char **tp_names, Type **type_args, int tp_count);
static Type *lookup_impl_type_arg(char **tp_names, Type **type_args, int tp_count, const char *name);
static void pending_generic_method_add(Checker *c, AstNode *cloned, char *owned_mangled, Type *struct_type);
static char *generic_method_symbol(const char *mangled_name, AstNode *method);
static void register_lazy_generic_method(Checker *c, const char *mfn_name, AstNode *method, Type *mtype, Type *struct_type, char **tp_names, Type **type_args, int tp_count);
static void register_template(Checker *c, const char *base_name, int type_param_count, int variant_count, const char *const *variant_names, const int *variant_payload_counts, const int *variant_payload_param_idxs);
static Type *resolve_type_node_with_substitution( Checker *c, TypeNode *node, char **tp_names, Type **type_args, int tp_count);
static Type *checker_instantiate_struct_inner(Checker *c, const char *base_name, Type **type_args, int type_arg_count, int line, int col);

/* Source file that `st`'s generic template was DEFINED in, or NULL when that
   cannot be determined (template defined in the root file, or the module is not
   in the registry). checker_error stamps diagnostics with c->source_path, which
   during monomorphization is the CONSUMER's file -- but a template's method AST
   nodes carry line numbers from the defining file. Callers reporting against
   those nodes must swap source_path over for the call, or the diagnostic points
   into the wrong file at a line that may not even exist there. */
static const char *generic_template_source_file(Checker *c, Type *st)
{
    if (st == NULL || st->kind != TYPE_STRUCT) return NULL;
    const char *def_mod = st->as.strukt.generic_module;
    if (def_mod == NULL || c->registry == NULL) return NULL;
    if (c->module_name != NULL && strcmp(def_mod, c->module_name) == 0)
        return NULL;   /* already checking the defining module */
    ModuleInfo *mi = module_find(c->registry, def_mod);
    return mi ? mi->file_path : NULL;
}

/* ---- G1: Generic struct template registry ---- */


int find_struct_template_idx(Checker *c, const char *base_name)
{
    for (int i = 0; i < c->struct_template_count; i++)
    {
        if (strcmp(c->struct_templates[i].base_name, base_name) == 0)
            return i;
    }
    return -1;
}

/* F6 (transitive generics): like find_struct_template_idx, but on a local miss
   it PULLS the template from any fully-checked loaded module that defines it —
   e.g. the consumer imports std.json (which imports std.vec) but never imported
   std.vec directly, so its checker lacks the "Vec"/"VecIter" templates needed to
   instantiate Vec(JsonValue)'s methods. ONLY for instantiation sites — never for
   registration/duplicate/ambiguity checks (those must stay local so a module's
   own same-name generic isn't shadowed by an imported one). Idempotent. */
static int find_struct_template_idx_pull(Checker *c, const char *base_name)
{
    int local = find_struct_template_idx(c, base_name);
    if (local >= 0) return local;
    if (c->registry != NULL)
    {
        ModuleRegistry *reg = c->registry;
        for (int m = 0; m < reg->count; m++)
        {
            /* Only pull from FULLY-CHECKED modules. A not-yet-checked module
               (including the one currently being checked) will register its own
               templates through the normal same-file path; pulling early would
               trip register_struct_template's duplicate rejection. */
            if (!reg->modules[m].checked) continue;
            AstNode *mast = reg->modules[m].ast;
            if (mast == NULL || mast->kind != AST_PROGRAM) continue;
            for (int d = 0; d < mast->as.program.decl_count; d++)
            {
                AstNode *decl = mast->as.program.decls[d];
                if (decl->kind != AST_STRUCT_DECL ||
                    decl->as.struct_decl.type_param_count <= 0 ||
                    decl->as.struct_decl.name == NULL ||
                    strcmp(decl->as.struct_decl.name, base_name) != 0)
                    continue;
                int tidx = register_imported_struct_template(c, base_name,
                    decl->as.struct_decl.type_params,
                    decl->as.struct_decl.type_param_count, decl,
                    reg->modules[m].name);
                if (tidx >= 0 && c->struct_templates[tidx].impl_node == NULL)
                {
                    for (int k = 0; k < mast->as.program.decl_count; k++)
                    {
                        AstNode *id = mast->as.program.decls[k];
                        if (id->kind == AST_IMPL_DECL &&
                            id->as.impl_decl.type_param_count > 0 &&
                            id->as.impl_decl.name &&
                            strcmp(id->as.impl_decl.name, base_name) == 0)
                        {
                            c->struct_templates[tidx].impl_node = id;
                            break;
                        }
                    }
                }
                return tidx;
            }
        }
    }
    return -1;
}

void register_struct_template(Checker *c, const char *base_name,
                                     char **type_params, int type_param_count,
                                     AstNode *decl_node)
{
    /* Duplicate check: same-name generic struct */
    if (find_struct_template_idx(c, base_name) >= 0)
    {
        checker_error(c, decl_node->line, decl_node->column,
                      "generic struct '%s' already declared", base_name);
        return;
    }
    /* Also check non-generic struct name collision */
    if (find_struct_type(c, base_name))
    {
        checker_error(c, decl_node->line, decl_node->column,
                      "struct '%s' already declared (non-generic)", base_name);
        return;
    }

    if (c->struct_template_count >= c->struct_template_cap)
    {
        c->struct_template_cap = c->struct_template_cap < 4 ? 4
                                 : c->struct_template_cap * 2;
        c->struct_templates = realloc_safe(c->struct_templates,
            (size_t)c->struct_template_cap * sizeof(c->struct_templates[0]));
    }
    int idx = c->struct_template_count++;
    c->struct_templates[idx].base_name        = base_name;
    c->struct_templates[idx].type_params      = type_params;
    c->struct_templates[idx].type_param_count = type_param_count;
    c->struct_templates[idx].decl_node        = decl_node;
    c->struct_templates[idx].impl_node        = NULL;
    /* Owning module: the module currently being checked (NULL for the root/main
       file). Lets cross-module ambiguity detection tell same-name generics apart. */
    c->struct_templates[idx].module_name      = c->module_name;
}

/* Step 0 / B-4-for-generics: register a generic struct template that comes from
   an IMPORTED module. Unlike register_struct_template (same-file, errors on a
   duplicate), this is idempotent across transitive re-imports and tolerant of
   the same generic name appearing in two different modules:
     - same base_name + same owning module  → skip (already registered);
     - same base_name + DIFFERENT module     → register AND mark the bare name
       ambiguous, so any unqualified use errors clearly instead of silently
       binding to whichever module was imported first;
     - new base_name                         → register.
   Returns the template index (or the existing one). */
int register_imported_struct_template(Checker *c, const char *base_name,
                                             char **type_params, int type_param_count,
                                             AstNode *decl_node, const char *module_path)
{
    int existing = -1, conflict = -1;
    for (int i = 0; i < c->struct_template_count; i++)
    {
        if (strcmp(c->struct_templates[i].base_name, base_name) != 0) continue;
        const char *mn = c->struct_templates[i].module_name;
        if (mn && module_path && strcmp(mn, module_path) == 0) { existing = i; break; }
        conflict = i; /* same name, different (or root) module */
    }
    if (existing >= 0) return existing; /* transitive re-import — idempotent */

    if (c->struct_template_count >= c->struct_template_cap)
    {
        c->struct_template_cap = c->struct_template_cap < 4 ? 4
                                 : c->struct_template_cap * 2;
        c->struct_templates = realloc_safe(c->struct_templates,
            (size_t)c->struct_template_cap * sizeof(c->struct_templates[0]));
    }
    int idx = c->struct_template_count++;
    c->struct_templates[idx].base_name        = base_name;
    c->struct_templates[idx].type_params      = type_params;
    c->struct_templates[idx].type_param_count = type_param_count;
    c->struct_templates[idx].decl_node        = decl_node;
    c->struct_templates[idx].impl_node        = NULL;
    c->struct_templates[idx].module_name      = module_path;

    if (conflict >= 0)
        checker_mark_ambiguous_type(c, c->struct_templates[idx].base_name);

    return idx;
}

/* ---- Type alias registry ---- */

void register_type_alias(Checker *c, const char *name, Type *type)
{
    if (c->type_alias_count >= c->type_alias_cap)
    {
        c->type_alias_cap = GROW_CAPACITY(c->type_alias_cap);
        c->type_aliases = realloc_safe(c->type_aliases,
                                       (size_t)c->type_alias_cap * sizeof(c->type_aliases[0]));
    }
    c->type_aliases[c->type_alias_count].name = name;
    c->type_aliases[c->type_alias_count].type = type;
    c->type_alias_count++;
}

Type *find_type_alias(Checker *c, const char *name)
{
    /* VR-LIM-013: type aliases form a stack — generic instantiations push their
       type-param bindings and pop (restore count) on exit. Nested generics that
       reuse the same param name (e.g. struct Slots(T) with a `Vec(Option(T))`
       field: Slots's `T→int` is pushed, then Vec/impl(T)'s `T→Option(int)` is
       pushed while instantiating Vec's methods) must resolve to the INNERMOST
       (last-registered) binding. Iterate backwards so the inner `T` shadows the
       outer one; forward iteration returned the outer `T→int` → wrong element
       type inside Vec's method bodies. */
    for (int i = c->type_alias_count - 1; i >= 0; i--)
    {
        if (strcmp(c->type_aliases[i].name, name) == 0)
            return c->type_aliases[i].type;
    }
    return NULL;
}

/* ---- Enum type registry ---- */

void register_enum_type(Checker *c, const char *name, Type *type)
{
    if (c->enum_type_count >= c->enum_type_cap)
    {
        c->enum_type_cap = GROW_CAPACITY(c->enum_type_cap);
        c->enum_types = realloc_safe(c->enum_types,
                                     (size_t)c->enum_type_cap * sizeof(c->enum_types[0]));
    }
    c->enum_types[c->enum_type_count].name = name;
    c->enum_types[c->enum_type_count].type = type;
    c->enum_type_count++;
    type_tab_insert(&c->enum_tab, &c->enum_tab_cap, &c->enum_tab_count, name, type);
}

Type *find_enum_type(Checker *c, const char *name)
{
    if (!type_tab_disabled())
        return type_tab_find(c->enum_tab, c->enum_tab_cap, name);
    for (int i = 0; i < c->enum_type_count; i++)
    {
        if (strcmp(c->enum_types[i].name, name) == 0)
        {
            return c->enum_types[i].type;
        }
    }
    return NULL;
}

/* Forward decls for helpers used before their definitions. */
/* Operator overloading: try to lower `a OP b` to a user operator-method call.
   Returns true if `a` is a struct/enum overload site (handled — possibly with an
   error). Returns false to let the builtin numeric/string path proceed. */

/* Search all registered enums for a variant matching `vname`.
   Returns the number of matches (0 = none, 1 = unique, >1 = ambiguous).
   On unique match, fills *out_enum and *out_variant_idx.
   If c->expected_type is a TYPE_ENUM containing this variant, it is preferred
   regardless of how many other matches exist (treated as unambiguous). */
int find_variant(Checker *c, const char *vname,
                        Type **out_enum, int *out_variant_idx)
{
    /* Context-driven disambiguation: if expected_type is a known enum and it
       has this variant, choose it directly. */
    if (c->expected_type && c->expected_type->kind == TYPE_ENUM)
    {
        Type *et = c->expected_type;
        for (int v = 0; v < et->as.enom.variant_count; v++)
        {
            const char *vnm = et->as.enom.variants[v].name;
            if (vnm && strcmp(vnm, vname) == 0)
            {
                *out_enum = et;
                *out_variant_idx = v;
                return 1;
            }
        }
    }

    int matches = 0;
    for (int i = 0; i < c->enum_type_count; i++)
    {
        Type *et = c->enum_types[i].type;
        for (int v = 0; v < et->as.enom.variant_count; v++)
        {
            const char *vnm = et->as.enom.variants[v].name;
            if (vnm && strcmp(vnm, vname) == 0)
            {
                if (matches == 0)
                {
                    *out_enum = et;
                    *out_variant_idx = v;
                }
                matches++;
            }
        }
    }
    return matches;
}

/* Validate a variant constructor expression and return the produced enum type.
   For payload variants, type-checks each argument against the declared payload type. */
Type *check_variant_ctor(Checker *c, AstNode *node, Type *enum_type, int variant_idx,
                                AstNode **args, int arg_count)
{
    int expected = enum_type->as.enom.variants[variant_idx].payload_count;
    if (arg_count != expected)
    {
        checker_error(c, node->line, node->column,
                      "variant '%s' expects %d argument(s), got %d",
                      enum_type->as.enom.variants[variant_idx].name,
                      expected, arg_count);
        return NULL;
    }
    for (int i = 0; i < expected; i++)
    {
        Type *want = enum_type->as.enom.variants[variant_idx].payload_types[i];
        /* Plumb the payload type as expected_type so context-driven coercions
           fire in this position too — notably a string literal -> `Str`
           (docs/plan_string_to_stdlib.md §5.1), e.g. `Err("msg")` where the
           payload is Str. */
        Type *saved_exp = c->expected_type;
        c->expected_type = want;
        Type *got = check_expr(c, args[i]);
        c->expected_type = saved_exp;
        if (got && want && !type_equals(got, want))
        {
            checker_error(c, args[i]->line, args[i]->column,
                          "variant '%s' arg %d: expected '%s', got '%s'",
                          enum_type->as.enom.variants[variant_idx].name,
                          i + 1, type_name(want), type_name(got));
        }
    }
    node->resolved_type = enum_type;
    return enum_type;
}

/* ---- Builtin enum templates (Option / Result) ---- */

/* Look up a template by base name. */
int find_template_idx(Checker *c, const char *base)
{
    for (int i = 0; i < c->enum_template_count; i++)
    {
        if (strcmp(c->enum_templates[i].base_name, base) == 0)
            return i;
    }
    return -1;
}

/* S4: single authority for the comma-joined concrete type-arg names stashed on a
   generic call node so codegen mangles the call to the instantiated symbol (e.g.
   `identity(int)` not `identity(T)` when called with an abstract type param inside
   a generic body — codegen has no alias context). Idempotent: no-op if the node
   already carries a stash. Shared by the free-fn / method-level / closure-infer
   generic call sites; these three drifted apart once (2026-06-30) and produced
   abstract-`T` mangling bugs, so the format lives in exactly one place.
   `args[i] == NULL` renders as "?" (matches the method-level path's guard). */
void checker_stash_resolved_type_args(Checker *c, AstNode *call,
                                             Type **args, int n)
{
    (void)c;
    if (call == NULL || call->kind != AST_CALL) return;
    if (call->as.call.resolved_type_args != NULL) return;
    /* Bare `type_name` (not mangle_type_arg_name) and no wrapping "Base(...)"
       — just a comma-joined arg list, a different format from the instance-
       name sites above (preserved, not unified; see resolve_type_node's
       pre-check comment). MangleBuf replaces the old fixed 512-byte taj. */
    MangleBuf tb; mangle_buf_init(&tb);
    for (int ti = 0; ti < n; ti++) {
        if (ti > 0) mangle_buf_append(&tb, ",");
        mangle_buf_append(&tb, args[ti] ? type_name(args[ti]) : "?");
    }
    char *taj = mangle_buf_take(&tb);
    call->as.call.resolved_type_args = chk_strdup(taj);
    free(taj);
}

/* Instantiate a registered template with concrete type args.  Returns the
   resulting TYPE_ENUM (cached on second call). */
Type *instantiate_template(Checker *c, int template_idx,
                                  Type **type_args, int type_arg_count,
                                  int line, int col)
{
    if (template_idx < 0 || template_idx >= c->enum_template_count) return NULL;

    if (type_arg_count != c->enum_templates[template_idx].type_param_count)
    {
        checker_error(c, line, col,
                      "%s expects %d type argument(s), got %d",
                      c->enum_templates[template_idx].base_name,
                      c->enum_templates[template_idx].type_param_count, type_arg_count);
        return NULL;
    }

    /* Build mangled name. Type args keyed via mangle_type_arg_name (module
       llvm_name preferred) — keeping bare `type_name` here made two modules'
       same-named `Node` collide on one "Option(Node)" instance, so the second
       module's payload was read through the first's layout (silent garbage).
       MangleBuf (Task 2.2) replaces the old fixed 256-byte buf — deep enough
       nesting could previously truncate here (and possibly at a different
       depth than the struct-template path below), producing mismatched
       def/use symbols instead of a clean error. */
    MangleBuf nb; mangle_buf_init(&nb);
    mangle_buf_append(&nb, c->enum_templates[template_idx].base_name);
    mangle_buf_append(&nb, "(");
    for (int i = 0; i < type_arg_count; i++)
    {
        if (i > 0) mangle_buf_append(&nb, ",");
        mangle_append_type_arg(&nb, type_args[i]);
    }
    mangle_buf_append(&nb, ")");
    char *buf = mangle_buf_take(&nb);

    /* Cache hit? */
    Type *cached = find_enum_type(c, buf);
    if (cached) { free(buf); return cached; }

    /* Instantiate */
    int vc = c->enum_templates[template_idx].variant_count;
    Type *et = type_enum(buf, vc);
    free(buf); /* type_enum() copies the name into its arena */
    bool has_drop = false;
    for (int v = 0; v < vc; v++)
    {
        const char *vn = c->enum_templates[template_idx].variants[v].name;
        size_t vlen = strlen(vn);
        char *vn_copy = (char *)malloc_safe(vlen + 1);
        memcpy(vn_copy, vn, vlen + 1);
        et->as.enom.variants[v].name = vn_copy;

        int pc = c->enum_templates[template_idx].variants[v].payload_count;
        et->as.enom.variants[v].payload_count = pc;
        if (pc > 0)
        {
            et->as.enom.variants[v].payload_types =
                (Type **)malloc_safe((size_t)pc * sizeof(Type *));
            for (int j = 0; j < pc; j++)
            {
                int pi = c->enum_templates[template_idx].variants[v].payload[j].param_idx;
                Type *pt = (pi >= 0) ? type_args[pi]
                                     : c->enum_templates[template_idx].variants[v].payload[j].concrete;
                et->as.enom.variants[v].payload_types[j] = pt;
                /* Self-recursive enum types heap-box themselves → always has_drop.
                   type_owns_heap_for_enum() queries et->has_drop which hasn't been
                   set yet (happens after this loop), so handle it explicitly. */
                if (pt == et || type_owns_heap_for_enum(pt)) has_drop = true;
            }
        }
    }
    et->as.enom.has_drop = has_drop;

    register_enum_type(c, et->as.enom.name, et);
    return et;
}

/* Add a template to the checker's registry.  `variant_specs` is a flat array of
   {variant_name, payload_count, [(param_idx, concrete)]*} entries. */
static void register_template(Checker *c, const char *base_name, int type_param_count,
                              int variant_count, const char *const *variant_names,
                              const int *variant_payload_counts,
                              const int *variant_payload_param_idxs)
{
    if (c->enum_template_count >= c->enum_template_cap)
    {
        c->enum_template_cap = GROW_CAPACITY(c->enum_template_cap);
        c->enum_templates = realloc_safe(c->enum_templates,
            (size_t)c->enum_template_cap * sizeof(c->enum_templates[0]));
    }
    int idx = c->enum_template_count++;
    c->enum_templates[idx].base_name = base_name;
    c->enum_templates[idx].type_param_count = type_param_count;
    c->enum_templates[idx].variant_count = variant_count;
    c->enum_templates[idx].variants = malloc_safe((size_t)variant_count *
                                                  sizeof(c->enum_templates[idx].variants[0]));

    int payload_cursor = 0;
    for (int v = 0; v < variant_count; v++)
    {
        c->enum_templates[idx].variants[v].name = variant_names[v];
        int pc = variant_payload_counts[v];
        c->enum_templates[idx].variants[v].payload_count = pc;
        if (pc > 0)
        {
            c->enum_templates[idx].variants[v].payload =
                malloc_safe((size_t)pc * sizeof(c->enum_templates[idx].variants[v].payload[0]));
            for (int j = 0; j < pc; j++)
            {
                c->enum_templates[idx].variants[v].payload[j].param_idx =
                    variant_payload_param_idxs[payload_cursor++];
                c->enum_templates[idx].variants[v].payload[j].concrete = NULL;
            }
        }
        else
        {
            c->enum_templates[idx].variants[v].payload = NULL;
        }
    }
}

void register_builtin_enums(Checker *c)
{
    /* Option(T) { None; Some(T) } */
    {
        static const char *vnames[2] = { "None", "Some" };
        static const int   vpcs[2]   = { 0, 1 };
        static const int   vpidx[1]  = { 0 };  /* Some(T) -> param 0 */
        register_template(c, "Option", 1, 2, vnames, vpcs, vpidx);
    }
    /* Result(T, E) { Ok(T); Err(E) } */
    {
        static const char *vnames[2] = { "Ok", "Err" };
        static const int   vpcs[2]   = { 1, 1 };
        static const int   vpidx[2]  = { 0, 1 };  /* Ok(T)=param0; Err(E)=param1 */
        register_template(c, "Result", 2, 2, vnames, vpcs, vpidx);
    }
}

/* ---- Impl/method registry ---- */

/* B-4.1: the impl_registry key for a struct/enum Type. Module-defined types use
   their module-prefixed `llvm_name` (B-2) so two modules' same-named `impl Widget`
   register under distinct keys (mod_a__Widget vs mod_b__Widget) and don't collide.
   Root/non-module types have llvm_name == NULL → key is the bare name (unchanged). */
const char *impl_key_of_type(const Type *t)
{
    if (t == NULL) return NULL;
    if (t->kind == TYPE_STRUCT)
        return t->as.strukt.llvm_name ? t->as.strukt.llvm_name : t->as.strukt.name;
    if (t->kind == TYPE_ENUM)
        return t->as.enom.llvm_name ? t->as.enom.llvm_name : t->as.enom.name;
    return NULL;
}

int find_or_create_impl(Checker *c, const char *struct_name)
{
    int existing = find_impl_idx(c, struct_name);
    if (existing >= 0)
        return existing;
    if (c->impl_count >= c->impl_cap)
    {
        c->impl_cap = GROW_CAPACITY(c->impl_cap);
        c->impl_registry = realloc_safe(c->impl_registry,
                                        (size_t)c->impl_cap * sizeof(c->impl_registry[0]));
    }
    int idx = c->impl_count++;
    c->impl_registry[idx].struct_name = struct_name;
    c->impl_registry[idx].methods = NULL;
    c->impl_registry[idx].method_count = 0;
    c->impl_registry[idx].method_cap = 0;
    impl_tab_insert(c, struct_name, idx);
    return idx;
}

/* Returns true on success. Returns false if a method with the same name
   already exists for this struct (LS does not support method overloading).
   Error already reported in that case. */
bool register_method(Checker *c, int impl_idx, const char *name,
                            Type *type, bool is_static, int self_borrow_kind,
                            const char *origin_iface, AstNode *decl_node,
                            int line, int col)
{
    /* Reject duplicate method names -- LS does not support method overloading.
       Two exceptions:
       1. __drop is an origin-agnostic singleton: any __drop (auto-generated,
          inherent, or Destroy's `~` lowered to __drop) REPLACES the existing one
          rather than coexisting (a type has exactly one destructor).
       2. L-002: same-name methods from DIFFERENT origins (inherent vs interface,
          or two distinct interfaces) coexist. Only same-origin duplicates conflict
          (two inherent `m`, or the same interface providing `m` twice). */
    for (int j = 0; j < c->impl_registry[impl_idx].method_count; j++) {
        if (strcmp(c->impl_registry[impl_idx].methods[j].name, name) != 0)
            continue;

        if (strcmp(name, "__drop") == 0) {
            /* Replace the existing __drop entry. The old compiler-generated
               function type (`old`) and its pointer param (params[0], from
               type_pointer(st)) now live in the type arena (C1 §3.2) — never
               free Type nodes, or free() corrupts the CRT heap. They become
               controlled arena residency instead (subsumed by L-014's model).
               The params ARRAY itself is a plain malloc_safe Type* array
               (checker.c ~2348), owned by nobody else, so free that to avoid a
               per-Destroy-type compile-time leak. */
            Type *old = c->impl_registry[impl_idx].methods[j].type;
            if (old && old->kind == TYPE_FUNCTION)
                free(old->as.function.params);
            c->impl_registry[impl_idx].methods[j].type = type;
            c->impl_registry[impl_idx].methods[j].is_static = is_static;
            c->impl_registry[impl_idx].methods[j].self_borrow_kind = self_borrow_kind;
            c->impl_registry[impl_idx].methods[j].origin_iface = origin_iface;
            c->impl_registry[impl_idx].methods[j].decl_node = decl_node;
            return true;
        }

        const char *eo = c->impl_registry[impl_idx].methods[j].origin_iface;
        bool same_origin = (eo == NULL && origin_iface == NULL) ||
                           (eo != NULL && origin_iface != NULL &&
                            strcmp(eo, origin_iface) == 0);
        if (same_origin) {
            const char *tname = c->impl_registry[impl_idx].struct_name;
            bool is_enum = (find_enum_type(c, tname) != NULL);
            checker_error(c, line, col,
                "conflicting method '%s': already defined for %s '%s'",
                name, is_enum ? "enum" : "struct", tname);
            return false;
        }
        /* Cross-origin coexistence (L-002): both the existing and the new entry
           are "contended". Mark the INTERFACE-origin nodes so codegen mangles them
           to `T.<Iface>.m` (the inherent one — origin NULL — always stays `T.m`). */
        AstNode *en = c->impl_registry[impl_idx].methods[j].decl_node;
        if (eo != NULL && en != NULL && en->kind == AST_FN_DECL)
            en->as.fn_decl.iface_method_contended = true;
        if (origin_iface != NULL && decl_node != NULL &&
            decl_node->kind == AST_FN_DECL)
            decl_node->as.fn_decl.iface_method_contended = true;
        /* keep scanning: a later entry may be a same-origin true duplicate */
    }

    int mc = c->impl_registry[impl_idx].method_count;
    int cap = c->impl_registry[impl_idx].method_cap;
    if (mc >= cap)
    {
        cap = GROW_CAPACITY(cap);
        c->impl_registry[impl_idx].method_cap = cap;
        c->impl_registry[impl_idx].methods = realloc_safe(
            c->impl_registry[impl_idx].methods,
            (size_t)cap * sizeof(c->impl_registry[impl_idx].methods[0]));
    }
    c->impl_registry[impl_idx].methods[mc].name = name;
    c->impl_registry[impl_idx].methods[mc].type = type;
    c->impl_registry[impl_idx].methods[mc].is_static = is_static;
    c->impl_registry[impl_idx].methods[mc].self_borrow_kind = self_borrow_kind;
    c->impl_registry[impl_idx].methods[mc].origin_iface = origin_iface;
    c->impl_registry[impl_idx].methods[mc].decl_node = decl_node;
    c->impl_registry[impl_idx].method_count++;
    return true;
}

/* Phase A1: returns the registered method's self_borrow_kind, or 0 if not found
   (legacy implicit). 0 = none/legacy, 1 = &self, 2 = &!self. */
int method_self_borrow_kind(Checker *c, const char *struct_name,
                                   const char *method_name)
{
    int i = find_impl_idx(c, struct_name);
    if (i < 0) return 0;
    int fallback = 0; bool found = false;
    for (int j = 0; j < c->impl_registry[i].method_count; j++)
    {
        if (strcmp(c->impl_registry[i].methods[j].name, method_name) != 0)
            continue;
        if (c->impl_registry[i].methods[j].origin_iface == NULL)  /* inherent */
            return c->impl_registry[i].methods[j].self_borrow_kind;
        if (!found) { fallback = c->impl_registry[i].methods[j].self_borrow_kind; found = true; }
    }
    return fallback;
}

/* Local strdup (checker-owned). */
char *chk_strdup(const char *s)
{
    size_t n = strlen(s);
    char *d = (char *)malloc_safe(n + 1);
    memcpy(d, s, n + 1);
    return d;
}




Type *find_method(Checker *c, const char *struct_name, const char *method_name)
{
    /* L-002: with same-name cross-origin coexistence, a (type, name) pair may have
       multiple entries. Prefer the INHERENT one (origin == NULL) — "inherent
       priority" for bare dispatch. Otherwise return the first (interface) match
       (a bare call that is genuinely ambiguous is rejected earlier at the call
       site; non-contended names have exactly one entry → unchanged). */
    int i = find_impl_idx(c, struct_name);
    if (i < 0) return NULL;
    Type *fallback = NULL;
    for (int j = 0; j < c->impl_registry[i].method_count; j++)
    {
        if (strcmp(c->impl_registry[i].methods[j].name, method_name) != 0)
            continue;
        if (c->impl_registry[i].methods[j].origin_iface == NULL)
            return c->impl_registry[i].methods[j].type;  /* inherent wins */
        if (fallback == NULL)
            fallback = c->impl_registry[i].methods[j].type;
    }
    return fallback;
}

/* L-002: find a method on `struct_name` whose origin matches `origin`
   (NULL = inherent; else interface name). Returns its type or NULL. Used by the
   interface-qualified call `Iface.method(recv)` to select the right overload. */
Type *find_method_origin(Checker *c, const char *struct_name,
                         const char *method_name, const char *origin)
{
    int i = find_impl_idx(c, struct_name);
    if (i < 0) return NULL;
    for (int j = 0; j < c->impl_registry[i].method_count; j++)
    {
        if (strcmp(c->impl_registry[i].methods[j].name, method_name) != 0)
            continue;
        const char *o = c->impl_registry[i].methods[j].origin_iface;
        if (origin == NULL) {
            if (o == NULL) return c->impl_registry[i].methods[j].type;
        } else if (o != NULL && strcmp(o, origin) == 0) {
            return c->impl_registry[i].methods[j].type;
        }
    }
    return NULL;
}

/* L-002: count inherent (origin NULL) and interface (origin != NULL) providers of
   `method_name` on `struct_name`. Optionally returns the first two interface names
   (for the ambiguity diagnostic). */
void method_providers(Checker *c, const char *struct_name, const char *method_name,
                      int *inherent_count, int *iface_count,
                      const char **ia, const char **ib)
{
    int inh = 0, ifc = 0; const char *a = NULL, *b = NULL;
    int i = find_impl_idx(c, struct_name);
    if (i >= 0)
    {
        for (int j = 0; j < c->impl_registry[i].method_count; j++)
        {
            if (strcmp(c->impl_registry[i].methods[j].name, method_name) != 0)
                continue;
            const char *o = c->impl_registry[i].methods[j].origin_iface;
            if (o == NULL) inh++;
            else { ifc++; if (!a) a = o; else if (!b) b = o; }
        }
    }
    if (inherent_count) *inherent_count = inh;
    if (iface_count)    *iface_count = ifc;
    if (ia) *ia = a;
    if (ib) *ib = b;
}

/* L-002: true if `name` is a known interface (user-declared trait or a builtin
   operator trait). Used to recognize `Iface.method(recv)` qualified calls. */
bool checker_is_known_interface(Checker *c, const char *name)
{
    for (int i = 0; i < c->trait_count; i++)
        if (strcmp(c->trait_registry[i].name, name) == 0)
            return true;
    return is_builtin_operator_trait(name);
}



/* Check if a registered method is static. Returns -1 if not found, 0 if instance, 1 if static */
int method_is_static(Checker *c, const char *struct_name, const char *method_name)
{
    int i = find_impl_idx(c, struct_name);
    if (i < 0) return -1;
    int fallback = -1;
    for (int j = 0; j < c->impl_registry[i].method_count; j++)
    {
        if (strcmp(c->impl_registry[i].methods[j].name, method_name) != 0)
            continue;
        if (c->impl_registry[i].methods[j].origin_iface == NULL)  /* inherent */
            return c->impl_registry[i].methods[j].is_static ? 1 : 0;
        if (fallback < 0) fallback = c->impl_registry[i].methods[j].is_static ? 1 : 0;
    }
    return fallback;
}

/* ---- G1: Generic struct instantiation ---- */

/* Forward declarations for mutual recursion */

/* Resolve a TypeNode to a Type, replacing type parameter names with concrete types.
   tp_names[i] ↔ type_args[i].  Falls back to resolve_type_node for non-parameterized paths. */
static Type *resolve_type_node_with_substitution(
    Checker *c, TypeNode *node,
    char **tp_names, Type **type_args, int tp_count)
{
    if (node == NULL) return type_void();
    int line = node->line, col = node->column;

    switch (node->kind) {
    case TYPE_NODE_PRIMITIVE:
        return resolve_type_node(c, node, line, col);

    case TYPE_NODE_NAMED: {
        const char *name = node->as.named.name;

        /* Check if this is a type parameter name */
        for (int i = 0; i < tp_count; i++) {
            if (strcmp(name, tp_names[i]) == 0) {
                if (node->as.named.arg_count > 0) {
                    checker_error(c, line, col,
                        "type parameter '%s' cannot have type arguments", name);
                    return type_int();
                }
                return type_args[i];
            }
        }

        /* Not a type parameter: if it has args, recursively resolve them */
        if (node->as.named.arg_count > 0) {
            int nargs = node->as.named.arg_count;
            Type **resolved_args = (Type **)malloc_safe(
                (size_t)nargs * sizeof(Type *));
            for (int i = 0; i < nargs; i++) {
                resolved_args[i] = resolve_type_node_with_substitution(
                    c, node->as.named.args[i], tp_names, type_args, tp_count);
                if (resolved_args[i] == NULL) {
                    free(resolved_args);
                    return NULL;
                }
            }

            Type *result = NULL;

            /* Try user generic struct first (including imported modules) */
            if (find_struct_template_idx_pull(c, name) >= 0) {
                result = checker_instantiate_struct(c, name,
                    resolved_args, nargs, line, col);
            }
            /* Try builtin enum templates (Option, Result) */
            if (result == NULL) {
                int tmpl_idx = find_template_idx(c, name);
                if (tmpl_idx >= 0) {
                    result = instantiate_template(c, tmpl_idx,
                        resolved_args, nargs, line, col);
                }
            }
            /* Fallback: try existing enum/struct lookup */
            if (result == NULL) {
                result = resolve_type_node(c, node, line, col);
            }

            free(resolved_args);
            return result;
        }

        /* No args: plain named type, delegate */
        return resolve_type_node(c, node, line, col);
    }

    case TYPE_NODE_ARRAY: {
        Type *elem = resolve_type_node_with_substitution(
            c, node->as.array.elem, tp_names, type_args, tp_count);
        return type_array(elem, node->as.array.size);
    }

    case TYPE_NODE_SIMD: {
        Type *elem = resolve_type_node_with_substitution(
            c, node->as.array.elem, tp_names, type_args, tp_count);
        if (elem && !type_is_numeric(elem)) {
            checker_error(c, node->line, node->column,
                "Simd element type must be a numeric scalar (got '%s')",
                type_name(elem));
        }
        return type_simd(elem, node->as.array.size);
    }

    case TYPE_NODE_SLICE: {
        Type *elem = resolve_type_node_with_substitution(
            c, node->as.array.elem, tp_names, type_args, tp_count);
        return type_slice(elem, node->is_mut);
    }

    case TYPE_NODE_POINTER: {
        Type *pointee = resolve_type_node_with_substitution(
            c, node->as.pointee, tp_names, type_args, tp_count);
        return type_pointer(pointee);
    }

    case TYPE_NODE_REFERENCE: {
        Type *pointee = resolve_type_node_with_substitution(
            c, node->as.pointee, tp_names, type_args, tp_count);
        return node->is_mut ? type_mut_reference(pointee)
                            : type_reference(pointee);
    }

    case TYPE_NODE_FN:
    case TYPE_NODE_BLOCK: {
        int n = node->as.fn.param_count;
        Type **params = NULL;
        if (n > 0) {
            params = (Type **)malloc_safe((size_t)n * sizeof(Type *));
            for (int i = 0; i < n; i++) {
                params[i] = resolve_type_node_with_substitution(
                    c, node->as.fn.params[i], tp_names, type_args, tp_count);
            }
        }
        Type *ret = resolve_type_node_with_substitution(
            c, node->as.fn.ret, tp_names, type_args, tp_count);
        if (node->kind == TYPE_NODE_BLOCK)
            return type_block(params, n, ret);
        else
            return type_function(params, n, ret, false);
    }

    default:
        return resolve_type_node(c, node, line, col);
    }
}

/* Instantiate a user-defined generic struct with concrete type arguments.
   Returns cached/freshly-built TYPE_STRUCT.  NULL if base_name is not a template. */
/* G1.5: Forward declarations */

/* Recursion limit for generic instantiation. Mirrors parser.c's
   LS_MAX_PARSE_DEPTH. 64 is far above anything real (the deepest nesting in the
   whole tree is test_mangle_deep_nest's Vec(Map(Str,Vec(Pair(int,int)))) at 5)
   while still bounding the pathological case cheaply.
   The recursion runs through the instantiated struct's own field types, so the
   guard has to wrap the whole function -- hence the thin-shell split, which also
   guarantees the counter is restored on every return path. */
#define LS_MAX_INST_DEPTH 64

Type *checker_instantiate_struct(Checker *c,
                                 const char *base_name,
                                 Type **type_args, int type_arg_count,
                                 int line, int col)
{
    if (c->inst_depth >= LS_MAX_INST_DEPTH)
    {
        checker_error(c, line, col,
                      "generic instantiation too deep (limit %d) while instantiating '%s' "
                      "-- is the template self-referential?",
                      LS_MAX_INST_DEPTH, base_name ? base_name : "<anonymous>");
        return NULL;
    }
    c->inst_depth++;
    Type *r = checker_instantiate_struct_inner(c, base_name, type_args,
                                               type_arg_count, line, col);
    c->inst_depth--;
    return r;
}

static Type *checker_instantiate_struct_inner(Checker *c,
                                 const char *base_name,
                                 Type **type_args, int type_arg_count,
                                 int line, int col)
{
    int tmpl_idx = find_struct_template_idx_pull(c, base_name);
    if (tmpl_idx < 0) return NULL;

    int expected_tpc = c->struct_templates[tmpl_idx].type_param_count;
    if (type_arg_count != expected_tpc) {
        checker_error(c, line, col,
                      "generic struct '%s' expects %d type argument(s), got %d",
                      base_name, expected_tpc, type_arg_count);
        return NULL;
    }

    /* Step 13: Check trait bounds on type parameters */
    {
        AstNode *decl = c->struct_templates[tmpl_idx].decl_node;
        TypeParamBound *bounds = decl->as.struct_decl.type_param_bounds;
        if (bounds) {
            bool bounds_ok = true;
            for (int ti = 0; ti < expected_tpc && bounds_ok; ti++) {
                for (int bi = 0; bi < bounds[ti].count; bi++) {
                    if (!checker_type_satisfies_trait(c, type_args[ti],
                                                      bounds[ti].trait_names[bi])) {
                        checker_error(c, line, col,
                            "type '%s' does not satisfy interface '%s' "
                            "(required by type parameter '%s' of '%s')",
                            type_name(type_args[ti]),
                            bounds[ti].trait_names[bi],
                            c->struct_templates[tmpl_idx].type_params[ti],
                            base_name);
                        bounds_ok = false;
                        break;
                    }
                }
            }
            if (!bounds_ok) return NULL;
        }
    }

    /* Build mangled name: "Pair(int,string)". Type args keyed via
       mangle_type_arg_name (F6b, module llvm_name preferred) — see its
       header comment for the collision rationale. MangleBuf (Task 2.2)
       replaces the old fixed 512-byte buf — deep enough nesting could
       previously truncate here (possibly at a different depth than the
       enum-template path above), producing mismatched def/use symbols
       instead of a clean error. */
    MangleBuf nb; mangle_buf_init(&nb);
    mangle_buf_append(&nb, base_name);
    mangle_buf_append(&nb, "(");
    for (int i = 0; i < type_arg_count; i++) {
        if (i > 0) mangle_buf_append(&nb, ",");
        mangle_append_type_arg(&nb, type_args[i]);
    }
    mangle_buf_append(&nb, ")");
    char *buf = mangle_buf_take(&nb);

    /* Cache hit? */
    Type *cached = find_struct_type(c, buf);
    if (cached) { free(buf); return cached; }

    /* Instantiate: create new TYPE_STRUCT with concrete field types */
    AstNode *decl = c->struct_templates[tmpl_idx].decl_node;
    int fc = decl->as.struct_decl.field_count;
    char **tp_names = c->struct_templates[tmpl_idx].type_params;

    /* Mangled name (owned by the Type): buf is already a malloc'd, right-
       sized allocation courtesy of MangleBuf, so hand it off directly
       instead of the old malloc_safe+memcpy duplicate that existed only
       because the fixed stack buffer couldn't be owned by anything. */
    char *mangled = buf;

    /* Pre-register empty shell to handle self-recursive generics */
    Type *st = type_struct(mangled, fc);
    /* VR-LIM-018/F6: stamp generic-instantiation metadata so a consumer
       module's checker (which never ran this instantiation locally) can
       re-register impl methods on demand when it meets `st` via an imported
       enum payload / function signature. */
    {
        size_t bl = strlen(base_name);
        char *gb = (char *)malloc_safe(bl + 1);
        memcpy(gb, base_name, bl + 1);
        st->as.strukt.generic_base = gb;
        st->as.strukt.generic_arg_count = type_arg_count;
        if (type_arg_count > 0) {
            st->as.strukt.generic_args =
                (Type **)malloc_safe((size_t)type_arg_count * sizeof(Type *));
            for (int gi = 0; gi < type_arg_count; gi++)
                st->as.strukt.generic_args[gi] = type_args[gi];
        }
        /* Stamp the impl template + tp names so a consumer checker without the
           local template (didn't import the defining module directly) can still
           re-register methods. impl_node/tp_names point into persistent module
           ASTs (not owned). */
        st->as.strukt.generic_impl_node = c->struct_templates[tmpl_idx].impl_node;
        st->as.strukt.generic_tp_names  = tp_names;
        /* Phase 2: remember the defining module so its import aliases can be bound
           when this instance's generic method bodies are checked in a consumer. */
        st->as.strukt.generic_module    = c->struct_templates[tmpl_idx].module_name;
    }
    register_struct_type(c, st->as.strukt.name, st);

    /* Fill fields with type substitution */
    bool has_drop = false;
    for (int i = 0; i < fc; i++) {
        TypeNode *ft_node = decl->as.struct_decl.field_types[i];
        Type *ft = resolve_type_node_with_substitution(
            c, ft_node, tp_names, type_args, type_arg_count);
        if (ft == NULL) {
            checker_error(c, ft_node ? ft_node->line : line,
                          ft_node ? ft_node->column : col,
                          "cannot resolve field type in '%s'", buf);
            ft = type_int();
        }
        /* Phase 0 (borrow extension): borrow fields are a latent dangling
           landmine. Non-generic structs are caught in check_struct_decl, but
           generic templates skip field checking — catch the borrow here when
           the template is instantiated (e.g. `Wrap(T){ &T item }`). */
        if (ft->kind == TYPE_REFERENCE) {
            checker_error(c, ft_node ? ft_node->line : line,
                          ft_node ? ft_node->column : col,
                          "struct fields cannot be borrows yet: field '%s' of "
                          "'%s' has borrow type &%s%s (use a value-offset view "
                          "instead)",
                          decl->as.struct_decl.field_names[i], buf,
                          ft->is_mut ? "!" : "",
                          ft->as.pointer_to ? type_name(ft->as.pointer_to) : "T");
        }
        if (ft->kind == TYPE_SLICE) {
            checker_error(c, ft_node ? ft_node->line : line,
                          ft_node ? ft_node->column : col,
                          "struct fields cannot be slices: field '%s' of '%s' "
                          "has slice type '%s' (a borrowed view cannot be stored)",
                          decl->as.struct_decl.field_names[i], buf, type_name(ft));
        }
        st->as.strukt.fields[i].name = decl->as.struct_decl.field_names[i];
        st->as.strukt.fields[i].type = ft;
        st->as.strukt.fields[i].default_expr =
            decl->as.struct_decl.field_defaults ? decl->as.struct_decl.field_defaults[i] : NULL;
        st->as.strukt.fields[i].is_private =
            decl->as.struct_decl.field_private ? decl->as.struct_decl.field_private[i] : false;

        if (type_owns_heap_for_enum(ft)) has_drop = true;
    }
    st->as.strukt.has_drop = has_drop;

    /* G1.5: instantiate associated impl methods (type signatures only) */
    if (c->struct_templates[tmpl_idx].impl_node != NULL) {
        instantiate_impl_method_types(c, st, st->as.strukt.name,
            c->struct_templates[tmpl_idx].impl_node,
            tp_names, type_args, type_arg_count);
    }

    return st;
}

static bool generic_method_is_eager(const char *name)
{
    return strcmp(name, "__drop") == 0 ||
           strcmp(name, "__clone") == 0 ||
           strcmp(name, "__from_list") == 0 ||
           strcmp(name, "__from_pairs") == 0;  /* M-LIT: `{k:v}` literal protocol */
}

static void pending_generic_method_add(Checker *c, AstNode *cloned,
                                       char *owned_mangled, Type *struct_type)
{
    if (c->pending_gm_count >= c->pending_gm_cap) {
        c->pending_gm_cap = c->pending_gm_cap < 8 ? 8 : c->pending_gm_cap * 2;
        c->pending_generic_methods = realloc_safe(c->pending_generic_methods,
            (size_t)c->pending_gm_cap * sizeof(c->pending_generic_methods[0]));
    }
    int idx = c->pending_gm_count++;
    c->pending_generic_methods[idx].cloned_fn = cloned;
    c->pending_generic_methods[idx].mangled_name = owned_mangled;
    c->pending_generic_methods[idx].struct_type = struct_type;
}

static Type *lookup_impl_type_arg(char **tp_names, Type **type_args, int tp_count,
                                  const char *name)
{
    for (int i = 0; i < tp_count; i++)
        if (strcmp(tp_names[i], name) == 0)
            return type_args[i];
    return NULL;
}

static bool check_method_where_bounds(Checker *c, AstNode *method,
                                      const char *qualified_name,
                                      char **tp_names, Type **type_args,
                                      int tp_count)
{
    int wc = method->as.fn_decl.where_bound_count;
    for (int wi = 0; wi < wc; wi++) {
        WhereBound *wb = &method->as.fn_decl.where_bounds[wi];
        Type *concrete = lookup_impl_type_arg(tp_names, type_args, tp_count,
                                              wb->type_param_name);
        if (concrete == NULL) {
            checker_error(c, method->line, method->column,
                          "unknown type parameter '%s' in where clause of '%s'",
                          wb->type_param_name, qualified_name);
            return false;
        }
        for (int bi = 0; bi < wb->bounds.count; bi++) {
            const char *trait = wb->bounds.trait_names[bi];
            if (!checker_type_satisfies_trait(c, concrete, trait)) {
                checker_error(c, method->line, method->column,
                              "method '%s' requires %s: %s, but '%s' does not implement %s",
                              qualified_name, wb->type_param_name, trait,
                              type_name(concrete), trait);
                return false;
            }
        }
    }
    return true;
}

/* Merged built-in math: the compiler module `std.core.math` carries only
   primitives that need codegen (intrinsics/libm/int-poly/comptime). Pure-
   arithmetic derived helpers (radians/degrees/...) live in the LS file
   lib/std/core/math.ls, loaded under the registry name "std.core.math" so they
   emit `std_core_math__<fn>` symbols. This helper loads + checks that file
   (idempotent) and folds its free-function exports into the given math module
   Type, so `math.radians` type-checks like any primitive. The derived file is
   zero-import, so the recursive check cannot re-enter here. */
static void merge_math_derived_exports(Checker *c, Type *mod_type)
{
    if (c == NULL || c->registry == NULL || mod_type == NULL) return;
    ModuleInfo *m = module_load(c->registry, "std.core.math", c->source_path);
    if (m == NULL || m->ast == NULL) return;
    if (!m->checked)
    {
        /* Guard: while checking the derived file, an `import std.core.math`
           inside it resolves to primitives-only (no nested merge → no recursion),
           so derived helpers like to_db can call math.log10/math.pow. We do NOT
           push "std.core.math" onto the import stack — that self-import is
           intentional and must bypass the circular-import check; merging_math is
           what guarantees termination. */
        bool saved_mm = c->registry->merging_math;
        c->registry->merging_math = true;
        CheckerGenericMethods sub_gm = {0};
        bool ok = checker_check(m->ast, m->file_path, c->registry, &sub_gm);
        c->registry->merging_math = saved_mm;
        free(sub_gm.methods);  /* derived file has no generics to bubble up */
        /* Re-find: the recursive check may have realloc'd reg->modules. */
        m = module_find(c->registry, "std.core.math");
        if (m == NULL || m->ast == NULL || !ok) return;
        m->checked = true;
    }
    AstNode *ma = m->ast;
    for (int j = 0; j < ma->as.program.decl_count; j++)
    {
        AstNode *d = ma->as.program.decls[j];
        if (d->kind == AST_FN_DECL && d->resolved_type)
            type_module_add_export(mod_type, d->as.fn_decl.name, d->resolved_type);
    }
}

/* Make a built-in module Type, folding in LS-derived exports for std.core.math.
   Drop-in replacement for builtin_module_make_type at every site that
   synthesises a user-visible built-in module reference. */
Type *builtin_module_make_type_merged(Checker *c, const char *name)
{
    Type *mt = builtin_module_make_type(c, name);
    /* Skip the merge while the derived file is itself being checked (its own
       `import std.core.math` must see primitives only — see merging_math). */
    if (mt && name && strcmp(name, "std.core.math") == 0 &&
        !(c && c->registry && c->registry->merging_math))
        merge_math_derived_exports(c, mt);
    return mt;
}

/* Phase 2 (docs/plan_module_fn_resolution.md): build a module type with its
   exported fn/struct/enum/extern symbols for an already-checked module on `path`.
   Mirrors the import handler's export-collection; used to bind a generic template's
   defining-module imports during method-body instantiation. NULL if unloadable. */
static Type *build_module_type_with_exports(Checker *c, const char *path)
{
    if (path == NULL) return NULL;
    if (builtin_module_exists(path) &&
        !module_user_file_exists(path, c->source_path))
        return builtin_module_make_type_merged(c, path);
    ModuleInfo *mod = module_load(c->registry, path, c->source_path);
    if (mod == NULL || mod->ast == NULL) return NULL;
    Type *mt = type_module_new(path);
    AstNode *ma = mod->ast;
    for (int j = 0; j < ma->as.program.decl_count; j++) {
        AstNode *d = ma->as.program.decls[j];
        if (d->kind == AST_FN_DECL && d->resolved_type)
            type_module_add_export(mt, d->as.fn_decl.name, d->resolved_type);
        else if (d->kind == AST_STRUCT_DECL && d->resolved_type)
            type_module_add_export(mt, d->as.struct_decl.name, d->resolved_type);
        else if (d->kind == AST_ENUM_DECL && d->resolved_type)
            type_module_add_export(mt, d->as.enum_decl.name, d->resolved_type);
        else if (d->kind == AST_EXTERN_FN && d->resolved_type)
            type_module_add_export(mt, d->as.extern_fn.name, d->resolved_type);
    }
    return mt;
}

/* Phase 2: when checking a generic method body whose template was DEFINED in
   module `module_path`, bind that module's import aliases into the current scope
   so qualified calls in the body (`sc.fn(...)`, `std.x.fn(...)`) resolve even
   though the consumer checker never imported them. Additive: only DEFINES extra
   module symbols (guarded against clobbering ones already in scope), never alters
   existing resolution. NULL module_path (root/same-file) is a no-op. */
static void bind_generic_defining_module_imports(Checker *c, const char *module_path)
{
    if (module_path == NULL || c->registry == NULL) return;
    ModuleInfo *mod = module_load(c->registry, module_path, c->source_path);
    if (mod == NULL || mod->ast == NULL) return;
    AstNode *ma = mod->ast;
    for (int j = 0; j < ma->as.program.decl_count; j++) {
        AstNode *d = ma->as.program.decls[j];
        if (d->kind != AST_IMPORT_DECL) continue;
        const char *ip = d->as.import_decl.path;
        const char *alias = d->as.import_decl.alias ? d->as.import_decl.alias : ip;
        if (alias == NULL) continue;
        if (scope_resolve_local(c->current_scope, alias) != NULL) continue;
        Type *mt = build_module_type_with_exports(c, ip);
        if (mt == NULL) continue;
        scope_define(c->current_scope, alias, mt);
        /* Phase 1 parity: also bind the full dotted path for `std.x.fn()` form. */
        if (d->as.import_decl.alias &&
            scope_resolve_local(c->current_scope, ip) == NULL)
            scope_define(c->current_scope, ip, mt);
    }
}

static bool check_and_queue_generic_method(Checker *c, Type *struct_type,
                                           const char *mangled_name,
                                           AstNode *method, Type *mtype,
                                           char **tp_names, Type **type_args,
                                           int tp_count, int line, int col)
{
    /* L-002 v2: contended interface methods get `T.<Iface>.m` (the flag was
       pre-set on `method` by the instantiation loop). malloc'd — freed on
       every exit below. */
    char *mfn_name = generic_method_symbol(mangled_name, method);

    if (!check_method_where_bounds(c, method, mfn_name, tp_names, type_args, tp_count))
    {
        free(mfn_name);
        return false;
    }

    bool is_static = method->as.fn_decl.is_static;
    int sbk = method->as.fn_decl.self_borrow_kind;
    int pc = method->as.fn_decl.param_count;

    AstNode *cloned = ast_clone_deep(method);
    cloned->as.fn_decl.impl_struct_name = mangled_name; /* not owned */

    int saved_alias_count = c->type_alias_count;
    for (int i = 0; i < tp_count; i++)
        register_type_alias(c, tp_names[i], type_args[i]);

    chk_push_scope(c);
    if (!is_static) {
        if (sbk == 0) {
            scope_define(c->current_scope, "self", type_pointer(struct_type));
        } else {
            Symbol *self_sym = scope_define(c->current_scope, "self", struct_type);
            if (self_sym) {
                if (sbk == 1) self_sym->is_borrow = true;
                else if (sbk == 2) self_sym->is_mut_borrow = true;
            }
        }
    }
    for (int j = 0; j < pc; j++) {
        Type *pt = is_static ? mtype->as.function.params[j]
                             : mtype->as.function.params[j + 1];
        if (pt) {
            bool is_borrow = false, is_mut_borrow = false;
            Type *sym_type = pt;
            if (sym_type->kind == TYPE_REFERENCE) {
                if (sym_type->is_mut) is_mut_borrow = true;
                else                  is_borrow = true;
                sym_type = sym_type->as.pointer_to;
            }
            Symbol *psym = scope_define(c->current_scope,
                cloned->as.fn_decl.param_names[j], sym_type);
            if (psym) {
                psym->is_borrow = is_borrow;
                psym->is_mut_borrow = is_mut_borrow;
                /* F5 (VR-LIM-017): an explicit `Block(..) f` param is a shallow
                   shared-env borrow (F.2: can't be moved). But a generic type
                   parameter `T x` that happens to monomorphize to Block (e.g.
                   `Vec(Block).push(T x)`) is an OWNED value the method moves into
                   storage — don't mark it is_borrow, or the body's
                   `self.data[i] = x` is wrongly rejected. Distinguish by the
                   ORIGINAL param type node: a bare type-param name → owned. */
                if (sym_type->kind == TYPE_BLOCK) {
                    bool is_tparam = false;
                    TypeNode *ptn = cloned->as.fn_decl.param_types
                                    ? cloned->as.fn_decl.param_types[j] : NULL;
                    if (ptn && ptn->kind == TYPE_NODE_NAMED &&
                        ptn->as.named.arg_count == 0) {
                        for (int t = 0; t < tp_count; t++)
                            if (strcmp(ptn->as.named.name, tp_names[t]) == 0) {
                                is_tparam = true; break;
                            }
                    }
                    if (!is_tparam) psym->is_borrow = true;
                }
            }
        }
    }
    /* Phase 2: bind the defining module's import aliases so qualified calls in the
       body resolve in this (consumer) checker. No-op for root/same-file generics. */
    if (struct_type && struct_type->kind == TYPE_STRUCT)
        bind_generic_defining_module_imports(c, struct_type->as.strukt.generic_module);

    Type *saved_ret = c->current_fn_return;
    c->current_fn_return = mtype->as.function.return_type;
    /* Make private-field access work inside generic method bodies: mark the
       struct being impl'd as the current impl context (this path does not go
       through check_impl_decl which normally sets it). */
    Type *saved_impl_st = c->current_impl_struct_type;
    if (struct_type && struct_type->kind == TYPE_STRUCT)
        c->current_impl_struct_type = struct_type;
    check_stmt(c, cloned->as.fn_decl.body);
    /* Stamp the instantiated fn type BEFORE the elide pass: its v2 param
       candidates read fn_decl->resolved_type for the concrete param types
       (the assignment further down is now a no-op re-stamp; Type is shared,
       so the error path's ast_free(cloned) is unaffected). */
    cloned->resolved_type = mtype;
    checker_elide_last_use(c, cloned); /* A1/v2 clone-elision (instance body) */
    c->current_impl_struct_type = saved_impl_st;
    c->current_fn_return = saved_ret;
    chk_pop_scope(c);
    c->type_alias_count = saved_alias_count;

    if (c->had_error) {
        ast_free(cloned);
        free(mfn_name);
        return false;
    }

    cloned->resolved_type = mtype;
    /* mfn_name is already an exclusively-owned malloc'd string — hand it to
       the pending queue directly (it used to be copied out of a stack
       buffer here). */
    pending_generic_method_add(c, cloned, mfn_name, struct_type);
    (void)line;
    (void)col;
    return true;
}

static void register_lazy_generic_method(Checker *c, const char *mfn_name,
                                         AstNode *method, Type *mtype,
                                         Type *struct_type, char **tp_names,
                                         Type **type_args, int tp_count)
{
    for (int i = 0; i < c->lazy_gm_count; i++)
        if (strcmp(c->lazy_generic_methods[i].mangled_name, mfn_name) == 0)
            return;
    if (c->lazy_gm_count >= c->lazy_gm_cap) {
        c->lazy_gm_cap = c->lazy_gm_cap < 8 ? 8 : c->lazy_gm_cap * 2;
        c->lazy_generic_methods = realloc_safe(c->lazy_generic_methods,
            (size_t)c->lazy_gm_cap * sizeof(c->lazy_generic_methods[0]));
    }
    int idx = c->lazy_gm_count++;
    c->lazy_generic_methods[idx].mangled_name =
        (char *)malloc_safe(strlen(mfn_name) + 1);
    memcpy(c->lazy_generic_methods[idx].mangled_name, mfn_name,
           strlen(mfn_name) + 1);
    c->lazy_generic_methods[idx].template_method = method;
    c->lazy_generic_methods[idx].method_type = mtype;
    c->lazy_generic_methods[idx].struct_type = struct_type;
    c->lazy_generic_methods[idx].tp_names = tp_names;
    c->lazy_generic_methods[idx].type_args =
        (Type **)malloc_safe((size_t)tp_count * sizeof(Type *));
    for (int i = 0; i < tp_count; i++)
        c->lazy_generic_methods[idx].type_args[i] = type_args[i];
    c->lazy_generic_methods[idx].tp_count = tp_count;
    c->lazy_generic_methods[idx].state = 0;
}

/* L-002 v2: instantiate by the FULL lazy symbol (e.g. "Box(int).Show3.tag" for a
   contended interface method, or "Box(int).tag" otherwise). The plain-name wrapper
   below builds "T.m"; the interface-qualified call path builds "T.<Iface>.m". */
bool ensure_generic_method_instantiated_sym(Checker *c,
                                                   const char *mangled_struct,
                                                   const char *mfn_name,
                                                   int line, int col)
{
    for (int i = 0; i < c->lazy_gm_count; i++) {
        if (strcmp(c->lazy_generic_methods[i].mangled_name, mfn_name) != 0)
            continue;
        if (c->lazy_generic_methods[i].state == 2 ||
            c->lazy_generic_methods[i].state == 1)
            return true;
        c->lazy_generic_methods[i].state = 1;
        bool ok = check_and_queue_generic_method(
            c,
            c->lazy_generic_methods[i].struct_type,
            mangled_struct,
            c->lazy_generic_methods[i].template_method,
            c->lazy_generic_methods[i].method_type,
            c->lazy_generic_methods[i].tp_names,
            c->lazy_generic_methods[i].type_args,
            c->lazy_generic_methods[i].tp_count,
            line, col);
        c->lazy_generic_methods[i].state = ok ? 2 : 0;
        return ok;
    }
    return true;
}

bool ensure_generic_method_instantiated(Checker *c,
                                               const char *mangled_struct,
                                               const char *method_name,
                                               int line, int col)
{
    char *mfn_name = mangle_method_symbol(mangled_struct, NULL, method_name);
    bool ok = ensure_generic_method_instantiated_sym(c, mangled_struct, mfn_name,
                                                     line, col);
    free(mfn_name); /* _sym only strcmp's against the lazy table */
    return ok;
}

/* Try to instantiate a method-level generic impl method.
   Called when find_method returns NULL but the call site provides
   explicit type arguments. Looks up generic_impl_method_templates,
   resolves the method-level type params, combines them with the
   impl-level params, builds the concrete signature, and queues
   the body for lazy codegen. Returns the concrete TYPE_FUNCTION
   on success, NULL on failure (caller should issue the usual "no
   such method" error). */
Type *try_instantiate_method_level_generic(Checker *c,
    const char *impl_key, const char *method_name,
    TypeNode **call_type_args, int call_type_arg_count,
    Type **pre_resolved_args,
    int line, int col)
{
    /* Step 1: find matching template */
    int tmpl_idx = -1;
    for (int i = 0; i < c->generic_impl_mt_count; i++) {
        if (strcmp(c->generic_impl_method_templates[i].method_name, method_name) != 0)
            continue;
        if (strcmp(c->generic_impl_method_templates[i].impl_key, impl_key) != 0)
            continue;
        tmpl_idx = i;
        break;
    }
    if (tmpl_idx < 0)
        return NULL;

    AstNode *method_ast = c->generic_impl_method_templates[tmpl_idx].method_ast;
    if (method_ast->kind != AST_FN_DECL)
        return NULL;

    int mtp_count = method_ast->as.fn_decl.type_param_count;
    /* pre_resolved_args (already-resolved Type*, from closure-return inference)
       bypasses the TypeNode resolve below; call_type_args is unused then. */
    if (!pre_resolved_args && call_type_arg_count != mtp_count) {
        checker_error(c, line, col,
            "method '%s' expects %d type argument(s), got %d",
            method_name, mtp_count, call_type_arg_count);
        return NULL;
    }

    /* Step 2: resolve method-level type args */
    Type **mtp_type_args = (Type **)malloc_safe((size_t)mtp_count * sizeof(Type *));
    bool ok = true;
    for (int ti = 0; ti < mtp_count; ti++) {
        mtp_type_args[ti] = pre_resolved_args
            ? pre_resolved_args[ti]
            : resolve_type_node(c, call_type_args[ti], line, col);
        if (!mtp_type_args[ti]) { ok = false; break; }
    }
    if (!ok) { free(mtp_type_args); return NULL; }

    /* Step 3: combine impl-level + method-level type params */
    int impl_tp_count = c->generic_impl_method_templates[tmpl_idx].impl_tp_count;
    char **impl_tp_names = c->generic_impl_method_templates[tmpl_idx].impl_tp_names;
    Type **impl_tp_types = c->generic_impl_method_templates[tmpl_idx].impl_tp_types;
    int total_tp_count = impl_tp_count + mtp_count;

    char **all_tp_names = NULL;
    Type **all_tp_types = NULL;
    if (total_tp_count > 0) {
        all_tp_names = (char **)malloc_safe((size_t)total_tp_count * sizeof(char *));
        all_tp_types = (Type **)malloc_safe((size_t)total_tp_count * sizeof(Type *));
        for (int i = 0; i < impl_tp_count; i++) {
            all_tp_names[i] = impl_tp_names[i];
            all_tp_types[i] = impl_tp_types[i];
        }
        for (int i = 0; i < mtp_count; i++) {
            /* Method-level type param names from the AST */
            all_tp_names[impl_tp_count + i] = method_ast->as.fn_decl.type_params[i];
            all_tp_types[impl_tp_count + i] = mtp_type_args[i];
        }
    }

    /* Step 4: build mangled call name: "RawVec(int).map(string)". Bare
       `type_name` for the method-level type args (not mangle_type_arg_name)
       — preserves this site's pre-existing behavior; the receiver part
       (impl_key) is already module-prefix-aware via impl_key_of_type.
       MangleBuf (Task 2.2) replaces the old fixed 512-byte buffer. */
    MangleBuf mb; mangle_buf_init(&mb);
    mangle_buf_append(&mb, impl_key);
    mangle_buf_append(&mb, ".");
    mangle_buf_append(&mb, method_name);
    mangle_buf_append(&mb, "(");
    for (int ti = 0; ti < mtp_count; ti++) {
        if (ti > 0) mangle_buf_append(&mb, ",");
        mangle_buf_append(&mb, type_name(mtp_type_args[ti]));
    }
    mangle_buf_append(&mb, ")");
    char *mangled = mangle_buf_take(&mb);

    /* Step 5: set up type aliases for combined params and build concrete signature */
    int saved_alias_count = c->type_alias_count;
    for (int i = 0; i < total_tp_count; i++)
        register_type_alias(c, all_tp_names[i], all_tp_types[i]);

    /* Build self + param types */
    bool is_static = method_ast->as.fn_decl.is_static;
    int pc = method_ast->as.fn_decl.param_count;
    int total_param_count = is_static ? pc : pc + 1;
    Type **params = (Type **)malloc_safe((size_t)total_param_count * sizeof(Type *));
    int offset = 0;
    if (!is_static) {
        Type *self_type = find_struct_type(c, impl_key);
        if (!self_type) {
            checker_error(c, line, col,
                "internal error: cannot find struct type '%s' for method-level generic",
                impl_key);
            c->type_alias_count = saved_alias_count;
            free(params); free(all_tp_names); free(all_tp_types);
            free(mtp_type_args); free(mangled);
            return NULL;
        }
        params[0] = type_pointer(self_type);
        offset = 1;
    }

    for (int j = 0; j < pc; j++) {
        Type *pt = resolve_type_node_with_substitution(
            c, method_ast->as.fn_decl.param_types[j],
            all_tp_names, all_tp_types, total_tp_count);
        params[offset + j] = pt ? pt : type_int();
        /* Policy A: by-value array(T,N) params rejected on every path;
           reported at the instantiating call site (line/col params). */
        reject_array_by_value_param(c, params[offset + j],
            method_ast->as.fn_decl.param_names[j], line, col);
    }

    Type *ret = method_ast->as.fn_decl.return_type
        ? resolve_type_node_with_substitution(
            c, method_ast->as.fn_decl.return_type,
            all_tp_names, all_tp_types, total_tp_count)
        : type_void();
    checker_reject_borrow_return(c, ret, NULL, method_ast->line, method_ast->column);  /* Phase 0/2: generic, defer */

    Type *concrete_type = type_function(params, total_param_count, ret, false);

    /* Step 6: check where bounds on method-level type params */
    int wc = method_ast->as.fn_decl.where_bound_count;
    for (int wi = 0; wi < wc; wi++) {
        WhereBound *wb = &method_ast->as.fn_decl.where_bounds[wi];
        /* Look up the concrete type for this bound's type param */
        Type *bound_type = NULL;
        for (int i = 0; i < total_tp_count; i++) {
            if (strcmp(all_tp_names[i], wb->type_param_name) == 0) {
                bound_type = all_tp_types[i];
                break;
            }
        }
        if (!bound_type) {
            checker_error(c, line, col,
                "unknown type parameter '%s' in where clause of '%s'",
                wb->type_param_name, mangled);
            c->type_alias_count = saved_alias_count;
            free(params); free(all_tp_names); free(all_tp_types);
            free(mtp_type_args); free(mangled);
            return NULL;
        }
        for (int bi = 0; bi < wb->bounds.count; bi++) {
            if (!checker_type_satisfies_trait(c, bound_type, wb->bounds.trait_names[bi])) {
                checker_error(c, line, col,
                    "type '%s' does not satisfy interface '%s' "
                    "(required by method '%s')",
                    type_name(bound_type), wb->bounds.trait_names[bi], mangled);
                c->type_alias_count = saved_alias_count;
                free(params); free(all_tp_names); free(all_tp_types);
                free(mtp_type_args); free(mangled);
                return NULL;
            }
        }
    }

    /* Step 7: clone AST and type-check body, queue for codegen */
    AstNode *cloned = ast_clone_deep(method_ast);
    cloned->as.fn_decl.type_param_count = 0; /* now concrete */
    cloned->as.fn_decl.type_params = NULL; /* safety */
    /* impl_struct_name drives is_instance_method in codegen_fn_decl.
       Without it param_offset stays 0 and self/arg mapping is wrong. */
    cloned->as.fn_decl.impl_struct_name = impl_key; /* points into template, not owned */

    /* Type-check the method body in a fresh scope */
    chk_push_scope(c);

    /* Set up the return type for body checking */
    Type *saved_return = c->current_fn_return;
    c->current_fn_return = ret;

    /* Register combined type aliases in the new scope */
    int scope_saved_alias = c->type_alias_count;
    for (int i = 0; i < total_tp_count; i++)
        register_type_alias(c, all_tp_names[i], all_tp_types[i]);

    /* Register self param (if instance method) in scope.
       sbk=0: params[0] is *Struct → register as pointer (checker sees obj_type=*Struct).
       sbk=1/2 (&self/&!self): codegen registers sym->type=Struct (bare), so checker
       must also see Struct — otherwise obj_node->resolved_type=*Struct triggers the
       wrong is_ptr_deref path in codegen field access (double-load crash). */
    int sbk_ml = method_ast->as.fn_decl.self_borrow_kind;
    if (!is_static) {
        Type *self_scope_type = (sbk_ml == 0)
            ? concrete_type->as.function.params[0]          /* *Struct */
            : concrete_type->as.function.params[0]->as.pointer_to; /* Struct */
        Symbol *self_sym = scope_define(c->current_scope, "self", self_scope_type);
        if (self_sym && sbk_ml == 1) self_sym->is_borrow = true;
        if (self_sym && sbk_ml == 2) self_sym->is_mut_borrow = true;
    }

    /* Register remaining params (param_names excludes the implicit self) */
    for (int j = 0; j < pc; j++) {
        const char *pname = method_ast->as.fn_decl.param_names
            ? method_ast->as.fn_decl.param_names[j]
            : "?";
        scope_define(c->current_scope, pname,
                     concrete_type->as.function.params[is_static ? j : j + 1]);
    }

    /* Make private-field access work inside method-level generic bodies:
       mark the struct being impl'd as the current impl context. */
    Type *saved_impl_st_ml = c->current_impl_struct_type;
    if (!is_static && concrete_type->as.function.param_count > 0) {
        Type *st0 = concrete_type->as.function.params[0];
        if (st0 && st0->kind == TYPE_POINTER) st0 = st0->as.pointer_to;
        if (st0 && st0->kind == TYPE_STRUCT)
            c->current_impl_struct_type = st0;
    }

    /* Check the body */
    if (cloned->as.fn_decl.body) {
        bool old_silent = c->silent_move_errors;
        bool old_return = c->in_return_expr;
        c->in_return_expr = false;

        check_stmt(c, cloned->as.fn_decl.body);
        /* Same early stamp as the struct-level instance path: the elide v2
           param candidates need the instantiated fn type on the decl node. */
        cloned->resolved_type = concrete_type;
        checker_elide_last_use(c, cloned); /* A1/v2 clone-elision (instance body) */

        c->in_return_expr = old_return;
        c->silent_move_errors = old_silent;
    }

    c->current_impl_struct_type = saved_impl_st_ml;

    c->type_alias_count = scope_saved_alias;
    c->current_fn_return = saved_return;
    chk_pop_scope(c);

    /* Set resolved_type so codegen can find the function */
    cloned->resolved_type = concrete_type;

    /* Queue for codegen */
    pending_generic_method_add(c, cloned, strdup(mangled),
        /* struct_type: find from impl_key */
        find_struct_type(c, impl_key));

    /* Restore type aliases */
    c->type_alias_count = saved_alias_count;

    free(all_tp_names);
    free(all_tp_types);
    free(mtp_type_args);
    free(mangled); /* pending_generic_method_add took its own strdup() copy above */
    /* params ownership transferred to concrete_type via type_function() — do NOT free */

    return concrete_type;
}

/* Infer a method-level generic's single type param from a closure argument's
   return type, so `v.map(|x| x + 1)` works without the explicit
   `v.map(int)(|x| ...)`. v1 scope: exactly one method type param U that appears
   as the *return* type of one `Block(...)->U` value argument (covers map /
   reduce). The closure's PARAM types come from the (already-known) impl-level
   type params, so only the return (U) needs inferring — captured via the
   `closure_infer_return_slot` mechanism in AST_RETURN. Returns the concrete
   method Type* (body checked+queued) on success, NULL to fall back to the
   explicit-args path / error. */
Type *try_infer_method_generic_from_closure(Checker *c,
    const char *impl_key, const char *method_name, AstNode *call,
    int line, int col)
{
    int tmpl_idx = -1;
    for (int i = 0; i < c->generic_impl_mt_count; i++) {
        if (strcmp(c->generic_impl_method_templates[i].method_name, method_name) != 0) continue;
        if (strcmp(c->generic_impl_method_templates[i].impl_key, impl_key) != 0) continue;
        tmpl_idx = i; break;
    }
    if (tmpl_idx < 0) return NULL;
    AstNode *method_ast = c->generic_impl_method_templates[tmpl_idx].method_ast;
    if (method_ast->kind != AST_FN_DECL) return NULL;
    if (method_ast->as.fn_decl.type_param_count != 1) return NULL;  /* v1: single U */
    const char *u_name = method_ast->as.fn_decl.type_params[0];

    /* Find the Block(...)->U value parameter (param_types excludes self). */
    int pc = method_ast->as.fn_decl.param_count;
    int closure_idx = -1;
    TypeNode *block_node = NULL;
    for (int j = 0; j < pc; j++) {
        TypeNode *pt = method_ast->as.fn_decl.param_types[j];
        if (pt && pt->kind == TYPE_NODE_BLOCK && pt->as.fn.ret &&
            pt->as.fn.ret->kind == TYPE_NODE_NAMED &&
            pt->as.fn.ret->as.named.name &&
            strcmp(pt->as.fn.ret->as.named.name, u_name) == 0) {
            closure_idx = j; block_node = pt; break;
        }
    }
    if (closure_idx < 0 || closure_idx >= call->as.call.arg_count) return NULL;
    AstNode *closure_arg = call->as.call.args[closure_idx];
    if (closure_arg->kind != AST_CLOSURE) return NULL;

    /* Resolve the Block's param types with impl-level type params substituted
       (e.g. Block(&T) on Vec(int) → Block(&int)). */
    int impl_tp_count   = c->generic_impl_method_templates[tmpl_idx].impl_tp_count;
    char **impl_tp_names = c->generic_impl_method_templates[tmpl_idx].impl_tp_names;
    Type **impl_tp_types = c->generic_impl_method_templates[tmpl_idx].impl_tp_types;
    int bpn = block_node->as.fn.param_count;
    Type **block_params = bpn > 0 ? (Type **)malloc_safe((size_t)bpn * sizeof(Type *)) : NULL;
    for (int k = 0; k < bpn; k++) {
        block_params[k] = resolve_type_node_with_substitution(
            c, block_node->as.fn.params[k], impl_tp_names, impl_tp_types, impl_tp_count);
        if (!block_params[k]) { free(block_params); return NULL; }
    }
    Type *expected_block = type_block(block_params, bpn, NULL /* return inferred */);

    /* Trial-check the closure with known param types + NULL return so the
       AST_RETURN inference slot captures U from `return EXPR`. */
    Type *inferred_u = NULL;
    Type *saved_exp = c->expected_type;
    Type **saved_slot = c->closure_infer_return_slot;
    c->expected_type = expected_block;
    c->closure_infer_return_slot = &inferred_u;
    check_expr(c, closure_arg);
    c->closure_infer_return_slot = saved_slot;
    c->expected_type = saved_exp;
    if (!inferred_u) return NULL;

    /* Re-instantiate with the inferred U. The real arg-check re-runs the
       closure against the concrete Block(&T)->U afterwards. */
    closure_arg->resolved_type = NULL;
    Type *resolved[1] = { inferred_u };
    Type *concrete = try_instantiate_method_level_generic(c, impl_key, method_name,
                                                          NULL, 0, resolved, line, col);
    if (concrete) {
        /* Record the inferred type-arg name so codegen mangles the call as
           `Type.method(U)` (matching the instantiated symbol), since the call
           node carries no explicit type_args. Guard on a non-NULL rendering to
           preserve the original "skip stash if name is NULL" behavior (the
           helper would otherwise emit "?"). */
        if (type_name(inferred_u)) {
            Type *one[1] = { inferred_u };
            checker_stash_resolved_type_args(c, call, one, 1);
        }
    }
    return concrete;
}

/* L-002 v2: count methods named `mname` in a (folded) generic impl_node. */
static int impl_node_same_name_count(AstNode *impl_node, const char *mname)
{
    int n = 0;
    for (int m = 0; m < impl_node->as.impl_decl.method_count; m++)
    {
        AstNode *mm = impl_node->as.impl_decl.methods[m];
        if (mm && mm->kind == AST_FN_DECL && mm->as.fn_decl.name &&
            strcmp(mm->as.fn_decl.name, mname) == 0)
            n++;
    }
    return n;
}

/* L-002 v2: is this folded generic method an interface provider of a CONTENDED
   name (>=2 providers on the type)? Compiler-reserved hooks (`__drop`/`__clone`/
   `__from_*`, incl. Destroy's folded `~`→`__drop`) and operator methods (`$op_*`)
   are singletons and NEVER mangled — they must keep the plain `T.m` symbol. */
static bool generic_method_is_contended(AstNode *impl_node, AstNode *method)
{
    const char *mname = method->as.fn_decl.name;
    if (method->as.fn_decl.origin_iface == NULL) return false;   /* inherent */
    if (mname == NULL) return false;
    if (mname[0] == '_' && mname[1] == '_') return false;        /* protocol hook */
    if (mname[0] == '$') return false;                           /* operator */
    return impl_node_same_name_count(impl_node, mname) >= 2;
}

/* L-002 v2: build the LLVM symbol for a generic instance method —
   `T.<Iface>.m` for a contended interface provider, else `T.m`. Reads the
   `iface_method_contended` flag pre-set on the method node by the instantiation
   loop (so check_and_queue's clone carries it too). emit (codegen reads the
   pending mangled_name) and dispatch (codegen_expr.c builds `T.<Iface>.m` from
   node.qualified_iface) both land on this name. */
static char *generic_method_symbol(const char *mangled_name, AstNode *method)
{
    /* Task 7.2: exact malloc'd symbol via the mangle.h single authority
       (was an out-buffer ABI with char[512] at every caller — silent
       truncation for deep generic instance names). Caller frees. */
    const char *mname = method->as.fn_decl.name;
    const char *iface = (method->as.fn_decl.iface_method_contended &&
                         method->as.fn_decl.origin_iface)
                            ? method->as.fn_decl.origin_iface : NULL;
    return mangle_method_symbol(mangled_name, iface, mname);
}

/* G1.5: For each method in a generic impl, resolve its param/return types
   with the concrete type arguments and register the method signature. Ordinary
   method bodies are checked lazily at call sites; compiler-reserved hooks that
   codegen calls by name are still checked and queued eagerly. */
static void instantiate_impl_method_types(
    Checker *c, Type *struct_type, const char *mangled_name,
    AstNode *impl_node,
    char **tp_names, Type **type_args, int tp_count)
{
    int impl_idx = find_or_create_impl(c, mangled_name);
    /* Idempotent per-checker: if this checker already registered this
       instantiation's methods (by mangled name), don't re-run the method loop
       (would trip register_method's duplicate rejection). VR-LIM-018's ensure_*
       path plus the normal instantiation can both fire for the same generic
       instantiation — possibly via DIFFERENT Type* instances (a sibling instance
       from an imported module's payload vs the local one). */
    if (c->impl_registry[impl_idx].method_count > 0)
    {
        /* …but `has_drop`/`has_user_drop` are set INSIDE the (now-skipped) method
           loop's __drop case, and they live on this *struct_type instance*. A
           fresh instance (e.g. the consumer's own Vec(string), distinct from the
           imported-module one whose method registration we're reusing) would miss
           them → its values silently never drop (memory leak). Propagate them
           from the impl template here so every instance is marked correctly. */
        for (int m = 0; m < impl_node->as.impl_decl.method_count; m++)
        {
            AstNode *mm = impl_node->as.impl_decl.methods[m];
            if (mm->kind != AST_FN_DECL || mm->as.fn_decl.name == NULL)
                continue;
            if (strcmp(mm->as.fn_decl.name, "__drop") == 0)
            {
                struct_type->as.strukt.has_drop = true;
                struct_type->as.strukt.has_user_drop = true;
            }
            else if (strcmp(mm->as.fn_decl.name, "__clone") == 0)
            {
                struct_type->as.strukt.has_user_clone = true;
            }
        }
        return;
    }

    /* Temporarily register type aliases so resolve_type_node("T") → concrete type */
    int saved_alias_count = c->type_alias_count;
    for (int i = 0; i < tp_count; i++)
        register_type_alias(c, tp_names[i], type_args[i]);

    /* G1.5+: also register generic impl type params (e.g. impl(W) → W=int).
       The impl's N-th type param maps to the N-th struct type arg because
       the impl signature is `impl(Param) StructName(Param)`.  This ensures
       method-level generics can resolve the impl's type param names when
       they appear in method signatures (e.g. `def map(U)(&self, Block(W)->U f)`). */
    if (impl_node->as.impl_decl.type_param_count > 0) {
        for (int i = 0; i < impl_node->as.impl_decl.type_param_count && i < tp_count; i++) {
            if (strcmp(impl_node->as.impl_decl.type_params[i], tp_names[i]) == 0)
                continue;  /* already registered above */
            register_type_alias(c, impl_node->as.impl_decl.type_params[i], type_args[i]);
        }
    }

    int mc = impl_node->as.impl_decl.method_count;
    for (int m = 0; m < mc; m++) {
        AstNode *method = impl_node->as.impl_decl.methods[m];
        if (method->kind != AST_FN_DECL) continue;

        const char *mname = method->as.fn_decl.name;
        bool is_static = method->as.fn_decl.is_static;
        int sbk = method->as.fn_decl.self_borrow_kind;
        int pc = method->as.fn_decl.param_count;

        /* L-002 v2: which interface (if any) provided this folded method, and
           whether its name is contended on this type. Pre-compute the flag now
           (order-independent: scans the whole impl_node) so symbol construction
           below — and check_and_queue's clone — agree regardless of which of the
           same-name overloads registers first. */
        const char *origin = method->as.fn_decl.origin_iface;
        method->as.fn_decl.iface_method_contended =
            generic_method_is_contended(impl_node, method);

        /* Method-level type parameters: skip eager/lazy registration.
           Store as template for on-demand instantiation at call site.
           Also register a placeholder in impl_registry so that
           find_method / method_is_static / method_self_borrow_kind
           work (they only need existence + is_static + sbk). */
        if (method->as.fn_decl.type_param_count > 0) {
            if (c->generic_impl_mt_count >= c->generic_impl_mt_cap) {
                int new_cap = c->generic_impl_mt_cap ? c->generic_impl_mt_cap * 2 : 8;
                c->generic_impl_method_templates = realloc_safe(
                    c->generic_impl_method_templates,
                    (size_t)new_cap * sizeof(c->generic_impl_method_templates[0]));
                c->generic_impl_mt_cap = new_cap;
            }
            int idx = c->generic_impl_mt_count++;
            c->generic_impl_method_templates[idx].method_name = (char *)mname; /* borrowed into AST, read-only */
            c->generic_impl_method_templates[idx].impl_key = strdup(mangled_name);
            c->generic_impl_method_templates[idx].method_ast = method;
            c->generic_impl_method_templates[idx].impl_tp_names =
                impl_node->as.impl_decl.type_param_count > 0
                    ? impl_node->as.impl_decl.type_params
                    : tp_names;
            c->generic_impl_method_templates[idx].impl_tp_types =
                (Type **)malloc_safe((size_t)tp_count * sizeof(Type *));
            for (int ti = 0; ti < tp_count; ti++)
                c->generic_impl_method_templates[idx].impl_tp_types[ti] = type_args[ti];
            c->generic_impl_method_templates[idx].impl_tp_count = tp_count;
            /* Register placeholder in impl_registry for borrow/static checks */
            register_method(c, impl_idx, mname, type_void(), is_static, sbk,
                            origin, method,  /* L-002 v2: carry fold origin */
                            method->line, method->column);
            continue;
        }

        /* A user-defined __drop forces has_drop on this monomorphized instance
           (mirrors the non-generic path, checker ~L7670). Without this, a generic
           container whose fields are all POD/raw-pointer (e.g. RawVec(int) with a
           *T buffer) would not be marked has_drop, so scope-exit would skip its
           __drop and leak the buffer. It also enables emit_struct_clone_val's
           user-__clone dispatch (which early-returns when !has_drop). */
        if (strcmp(mname, "__drop") == 0)
        {
            struct_type->as.strukt.has_drop = true;
            struct_type->as.strukt.has_user_drop = true;
        }
        if (strcmp(mname, "__clone") == 0)
        {
            struct_type->as.strukt.has_user_clone = true;
        }

        /* Build concrete method type: self ptr (if instance) + user params */
        int total = is_static ? pc : pc + 1;
        Type **params = (Type **)malloc_safe((size_t)total * sizeof(Type *));
        int offset = 0;
        if (!is_static) {
            params[0] = type_pointer(struct_type);
            offset = 1;
        }

        /* Resolve the signature types under error suppression: a generic method
           whose signature names a type not visible in THIS checker — e.g. a
           derived `reflect() -> TypeInfo` instantiated in a consumer that never
           imported std.core.reflect — is un-callable here, so skip it silently
           rather than emitting a spurious "unknown type" error. The defining
           module validated the template; any real call site necessarily has the
           referenced types in scope (you cannot write `TypeInfo t = m.reflect()`
           without importing them). Eager protocol methods (__drop/__clone/
           __from_*) only reference T or basic types, so this never drops them. */
        bool saved_silent_sig = c->silent_type_errors;
        c->silent_type_errors = true;
        bool sig_resolvable = true;

        for (int j = 0; j < pc; j++) {
            Type *pt = resolve_type_node_with_substitution(
                c, method->as.fn_decl.param_types[j],
                tp_names, type_args, tp_count);
            if (pt == NULL) sig_resolvable = false;
            params[offset + j] = pt ? pt : type_int(); /* fallback */
        }

        Type *ret = method->as.fn_decl.return_type
            ? resolve_type_node_with_substitution(
                c, method->as.fn_decl.return_type,
                tp_names, type_args, tp_count)
            : type_void();
        if (method->as.fn_decl.return_type && ret == NULL) sig_resolvable = false;

        c->silent_type_errors = saved_silent_sig;

        if (!sig_resolvable && !generic_method_is_eager(mname)) {
            free(params);
            continue;   /* un-callable in this scope — register nothing, no error */
        }
        if (ret == NULL) ret = type_void();   /* eager safety: never NULL-deref below */

        /* Generic return-borrow elision: pass the real method AST so an eligible
           single-input `&self` method (e.g. `Vec.get_ref(&self,i)->&T`) is allowed
           rather than blanket-rejected. The substituted `ret` carries the concrete
           pointee (&Inner, &Str, ...); the body's AST_RETURN proves provenance at
           lazy instantiation (checker.c return-stmt handling).
           (docs/plan_borrow_extension.md "下一步") */
        checker_reject_borrow_return(c, ret, method, method->line, method->column);

        /* Policy A: by-value array(T,N) params rejected on every path. Placed
           after the sig_resolvable skip so un-callable signatures stay
           silently unregistered; method-level generic templates (own type
           params) resolve NULL here and are rejected at call instantiation
           instead. Attribution follows the borrow-return line above. */
        for (int j = 0; j < pc; j++)
            reject_array_by_value_param(c, params[offset + j],
                method->as.fn_decl.param_names[j], method->line, method->column);

        /* Phase B: the half of interface validation that needs a concrete Self.
           Shape (arity / static-ness / self borrow kind / presence) was already
           checked once at fold time in check_impl_trait_decl; here we compare the
           param and return TYPES now that T is bound and `struct_type` IS the
           concrete Self.
           Placement matters: after the sig_resolvable early-continue (a signature
           this consumer cannot resolve is deliberately skipped silently -- e.g.
           Vec(T): Reflect in a module that never imported std.core.reflect), and
           after ret's NULL normalization.
           Deduped via the flag on the SHARED template node, so Vec(int) /
           Vec(Str) / Vec(f64) report a mismatch once, not three times.

           Attribution: checker_error stamps diagnostics with the CURRENT
           checker's source_path, but `method` is the template's node and its
           line numbers belong to the DEFINING module. Left alone, a consumer
           instantiating an imported generic reports the defining file's line
           against the consumer's file -- an injected fault at
           lib/std/core/vec.lls:626 came out as "str_core.lls:626", a line that
           file does not have (it is 483 lines long). So swap source_path to the
           defining module's file for the call. Gating on "only check in the
           defining module" instead would be wrong: std.core.vec never
           instantiates a concrete Vec(int) itself, so the check would never run
           for stdlib templates at all (verified by injection). */
        if (origin != NULL && !method->as.fn_decl.iface_sig_types_checked)
        {
            int b_tidx = find_trait(c, origin);
            if (b_tidx >= 0)
            {
                int b_mi = find_trait_method(c, b_tidx, mname);
                if (b_mi >= 0)
                {
                    const char *def_file = generic_template_source_file(c, struct_type);
                    const char *saved_path = c->source_path;
                    if (def_file != NULL) c->source_path = def_file;
                    method->as.fn_decl.iface_sig_types_checked = true;
                    iface_check_method_types(c, b_tidx, b_mi, method, origin, mname,
                                             params + offset, pc, ret, struct_type);
                    c->source_path = saved_path;
                }
            }
        }

        Type *mtype = type_function(params, total, ret, false);

        register_method(c, impl_idx, mname, mtype, is_static, sbk,
                        origin, method,  /* L-002 v2: carry fold origin */
                        method->line, method->column);

        /* Build mangled function name: "Pair(int,string).get_first"
           L-002 v2: a contended interface method becomes "T.<Iface>.m". */
        char *mfn_name = generic_method_symbol(mangled_name, method);
        if (generic_method_is_eager(mname))
            check_and_queue_generic_method(c, struct_type, mangled_name, method,
                                           mtype, tp_names, type_args, tp_count,
                                           method->line, method->column);
        else
            register_lazy_generic_method(c, mfn_name, method, mtype, struct_type,
                                         tp_names, type_args, tp_count);
        free(mfn_name); /* register_lazy copies; eager path never read it */
    }

    /* Remove temporary type aliases */
    c->type_alias_count = saved_alias_count;
}

/* VR-LIM-018/F6: a CONSUMER module's checker can meet a generic struct
   instantiation (e.g. Vec(int)) via an imported enum payload binder or function
   signature WITHOUT ever instantiating it through its own checker — so its
   impl_registry holds no methods for that type and method calls fail with
   "no field or method". Imported modules are checked by separate Checker
   instances, so the defining module's registrations don't carry over.
   This re-runs impl-method registration locally using the generic metadata
   stamped on the type by checker_instantiate_struct. Idempotent. */
void ensure_generic_struct_impls_local(Checker *c, Type *st)
{
    if (st == NULL || st->kind != TYPE_STRUCT ||
        st->as.strukt.name == NULL || st->as.strukt.generic_base == NULL)
        return;
    const char *name = st->as.strukt.name;
    {
        int i = find_impl_idx(c, name);
        if (i >= 0 && c->impl_registry[i].method_count > 0)
            return; /* already registered locally */
    }
    /* Prefer the impl template stamped on the type (works even when this
       consumer checker never imported the defining module). Fall back to a
       local template lookup. */
    AstNode *impl_node = (AstNode *)st->as.strukt.generic_impl_node;
    char **tp_names = st->as.strukt.generic_tp_names;
    if (impl_node == NULL) {
        int tmpl_idx = find_struct_template_idx_pull(c, st->as.strukt.generic_base);
        if (tmpl_idx < 0) return;
        impl_node = c->struct_templates[tmpl_idx].impl_node;
        tp_names  = c->struct_templates[tmpl_idx].type_params;
    }
    if (impl_node == NULL || tp_names == NULL) return;
    instantiate_impl_method_types(c, st, name, impl_node,
        tp_names, st->as.strukt.generic_args, st->as.strukt.generic_arg_count);
}

/* find_method with the VR-LIM-018 on-demand registration fallback: an imported
   generic instantiation (e.g. Vec(std_str__Str) stamped by another module's
   checker) has no impl methods in THIS checker's registry until first method
   dispatch. The method-call path already retries via
   ensure_generic_struct_impls_local; protocol gates (__index/__index_set) that
   probe with a bare find_method must use this wrapper or they miss (the old
   "cannot index non-array type 'Vec(...)'" on borrow-match binders from
   imported enums, plan_std_map §13). */
Type *find_method_ensured(Checker *c, Type *st, const char *mname)
{
    const char *key = impl_key_of_type(st);
    if (key == NULL) return NULL;
    Type *m = find_method(c, key, mname);
    if (m == NULL && st->kind == TYPE_STRUCT && st->as.strukt.generic_base)
    {
        ensure_generic_struct_impls_local(c, st);
        m = find_method(c, key, mname);
    }
    return m;
}

/* ---- Resolve TypeNode -> Type ---- */

Type *resolve_type_node(Checker *c, TypeNode *tn, int line, int col)
{
    if (tn == NULL)
        return type_void();

    switch (tn->kind)
    {
    case TYPE_NODE_PRIMITIVE:
        switch (tn->as.primitive)
        {
        case TOKEN_TYPE_INT:
            return type_int();
        case TOKEN_TYPE_I8:
            return type_i8();
        case TOKEN_TYPE_I16:
            return type_i16();
        case TOKEN_TYPE_I32:
            return type_i32();
        case TOKEN_TYPE_I64:
            return type_i64();
        case TOKEN_TYPE_U8:
            return type_u8();
        case TOKEN_TYPE_U16:
            return type_u16();
        case TOKEN_TYPE_U32:
            return type_u32();
        case TOKEN_TYPE_U64:
            return type_u64();
        case TOKEN_TYPE_F32:
            return type_f32();
        case TOKEN_TYPE_F64:
            return type_f64();
        case TOKEN_TYPE_F16:
            return type_f16();
        case TOKEN_TYPE_BF16:
            return type_bf16();
        case TOKEN_TYPE_BOOL:
            return type_bool();
        case TOKEN_TYPE_CHAR:
            return type_char();
        case TOKEN_TYPE_VOID:
            return type_void();
        case TOKEN_TYPE_LIB:
            return type_lib();
        case TOKEN_TYPE_OBJECT:
            return type_object();
        default:
            checker_error(c, line, col, "unknown primitive type");
            return NULL;
        }
    case TYPE_NODE_POINTER:
        return type_pointer(resolve_type_node(c, tn->as.pointee, line, col));
    case TYPE_NODE_REFERENCE:
    {
        Type *pointee = resolve_type_node(c, tn->as.pointee, line, col);
        if (pointee == NULL) return NULL;
        /* Phase 5.8/9 + P4(string→Str): supported borrow pointees are
           struct / enum. `&string`/`&!string` were removed in P4 — builtin
           string borrows are superseded by `&Str`/`&!Str` (pointer ABI). */
        bool ok_kind = (pointee->kind == TYPE_STRUCT ||
                        pointee->kind == TYPE_ENUM);   /* Phase 9: enum borrow */
        if (!ok_kind)
        {
            checker_error(c, line, col,
                              "&%s%s is not supported yet; only "
                              "&struct / &!struct / &enum are implemented",
                              tn->is_mut ? "!" : "",
                              type_name(pointee));
            return NULL;
        }
        /* Phase B: drop struct borrow now allowed. */
        return tn->is_mut ? type_mut_reference(pointee) : type_reference(pointee);
    }
    case TYPE_NODE_ARRAY:
        return type_array(resolve_type_node(c, tn->as.array.elem, line, col),
                          tn->as.array.size);
    case TYPE_NODE_SIMD: {
        Type *selem = resolve_type_node(c, tn->as.array.elem, line, col);
        if (selem && !type_is_numeric(selem)) {
            checker_error(c, line, col,
                "Simd element type must be a numeric scalar (got '%s')",
                type_name(selem));
        }
        return type_simd(selem, tn->as.array.size);
    }
    case TYPE_NODE_SLICE:
        return type_slice(resolve_type_node(c, tn->as.array.elem, line, col),
                          tn->is_mut);
    case TYPE_NODE_FN:
    {
        int n = tn->as.fn.param_count;
        Type **params = NULL;
        if (n > 0)
        {
            params = (Type **)malloc_safe((size_t)n * sizeof(Type *));
            for (int i = 0; i < n; i++)
            {
                params[i] = resolve_type_node(c, tn->as.fn.params[i], line, col);
            }
        }
        Type *ret = resolve_type_node(c, tn->as.fn.ret, line, col);
        return type_function(params, n, ret, false);
    }
    case TYPE_NODE_BLOCK:
    {
        int n = tn->as.fn.param_count;
        Type **params = NULL;
        if (n > 0)
        {
            params = (Type **)malloc_safe((size_t)n * sizeof(Type *));
            for (int i = 0; i < n; i++)
            {
                params[i] = resolve_type_node(c, tn->as.fn.params[i], line, col);
            }
        }
        Type *ret = resolve_type_node(c, tn->as.fn.ret, line, col);
        return type_block(params, n, ret);
    }
    case TYPE_NODE_NAMED:
    {
        /* B-4: module-qualified type `mod.Type` / `alias.Type` / `std.json.Value`.
           Resolve the qualifier (module path or import alias) to its TYPE_MODULE
           via the scope, then look up the type in that module's export table.
           This is precise and ignores bare-name ambiguity. */
        if (tn->as.named.module != NULL && tn->as.named.arg_count == 0)
        {
            Symbol *modsym = scope_resolve(c->current_scope, tn->as.named.module);
            if (modsym == NULL || modsym->type == NULL ||
                modsym->type->kind != TYPE_MODULE)
            {
                checker_error(c, line, col,
                    "unknown module '%s' in qualified type '%s.%s'",
                    tn->as.named.module, tn->as.named.module, tn->as.named.name);
                return NULL;
            }
            Type *ex = type_module_find_export(modsym->type, tn->as.named.name);
            if (ex && (ex->kind == TYPE_STRUCT || ex->kind == TYPE_ENUM))
                return ex;
            checker_error(c, line, col,
                "module '%s' has no type '%s'",
                tn->as.named.module, tn->as.named.name);
            return NULL;
        }

        /* Qualified GENERIC instantiation: `mod.Stack(int)`. Validate the
           qualifier resolves to a module that owns a generic named `name`, then
           instantiate. Two modules defining the same generic name → ambiguous;
           using more than one simultaneously is not yet supported (would need
           module-prefixed instance names), so error clearly instead of silently
           binding whichever was imported first. */
        if (tn->as.named.module != NULL && tn->as.named.arg_count > 0)
        {
            Symbol *modsym = scope_resolve(c->current_scope, tn->as.named.module);
            if (modsym == NULL || modsym->type == NULL ||
                modsym->type->kind != TYPE_MODULE)
            {
                checker_error(c, line, col,
                    "unknown module '%s' in qualified type '%s.%s(...)'",
                    tn->as.named.module, tn->as.named.module, tn->as.named.name);
                return NULL;
            }
            const char *qbase = tn->as.named.name;
            if (checker_type_is_ambiguous(c, qbase))
            {
                checker_error(c, line, col,
                    "generic type '%s' is defined in multiple imported modules; "
                    "using more than one of them simultaneously is not yet supported",
                    qbase);
                return NULL;
            }
            int qtidx = find_struct_template_idx(c, qbase);
            if (qtidx < 0)
            {
                checker_error(c, line, col,
                    "module '%s' has no generic type '%s'",
                    tn->as.named.module, qbase);
                return NULL;
            }
            /* Validate the qualifier actually names the owning module (alias ok:
               modsym->type->as.module.name is the resolved import path). */
            const char *owner = c->struct_templates[qtidx].module_name;
            const char *qmod  = modsym->type->as.module.name;
            if (owner && qmod && strcmp(owner, qmod) != 0)
            {
                checker_error(c, line, col,
                    "module '%s' has no generic type '%s'",
                    tn->as.named.module, qbase);
                return NULL;
            }
            int qn = tn->as.named.arg_count;
            Type **qta = (Type **)malloc_safe((size_t)qn * sizeof(Type *));
            for (int i = 0; i < qn; i++)
            {
                qta[i] = resolve_type_node(c, tn->as.named.args[i], line, col);
                if (qta[i] == NULL) { free(qta); return NULL; }
            }
            Type *qinst = checker_instantiate_struct(c, qbase, qta, qn, line, col);
            free(qta);
            return qinst;
        }

        /* Plain named type: try Self, then alias, then struct, then enum. */
        if (tn->as.named.arg_count == 0)
        {
            /* Self resolves to the current impl struct or enum type (trait/impl context) */
            if (strcmp(tn->as.named.name, "Self") == 0)
            {
                if (c->current_impl_struct_type != NULL)
                    return c->current_impl_struct_type;
                if (c->current_impl_enum_type != NULL)
                    return c->current_impl_enum_type;
            }
            Type *al = find_type_alias(c, tn->as.named.name);
            if (al) return al;
            /* B-4: bare reference to a name exported by 2+ modules → ambiguous. */
            if (checker_type_is_ambiguous(c, tn->as.named.name))
            {
                checker_error(c, line, col,
                    "type '%s' is defined in multiple imported modules; "
                    "qualify it as `mod.%s` (e.g. with `import mod as M` then `M.%s`)",
                    tn->as.named.name, tn->as.named.name, tn->as.named.name);
                return NULL;
            }
            Type *st = find_struct_type(c, tn->as.named.name);
            if (st) return st;
            Type *et = find_enum_type(c, tn->as.named.name);
            if (et) return et;
            {
                char helpbuf[256];
                DiagTypeIter it = { c, 0, 0 };
                const char *help = diag_help_suggestion(
                    helpbuf, sizeof(helpbuf), tn->as.named.name,
                    diag_type_iter_next, &it);
                checker_error_help(c, line, col,
                                   (int)strlen(tn->as.named.name), help,
                                   "unknown type '%s'", tn->as.named.name);
            }
            return NULL;
        }

        /* Generic-style instantiation: build mangled name "Name(arg1,arg2)"
           and look up an enum instance. Step 8 will add Option/Result template
           instantiation here when the lookup misses.
           ⭐ Deliberately bare `type_name` here, NOT mangle_type_arg_name —
           this is only a pre-check against already-instantiated types keyed
           by whatever name their instantiation site used; the real
           instantiation calls below (instantiate_template /
           checker_instantiate_struct) build the module-prefix-aware key
           themselves on a miss. Keep this difference (see mangle.h's
           mangle_type_arg_name comment / Task 2.2 brief) — unifying the
           *construction* (fixed buf -> MangleBuf) must not unify the
           *semantics*. MangleBuf still replaces the fixed 256-byte buf to
           remove this site's independent truncation risk. */
        const char *base = tn->as.named.name;
        MangleBuf nb; mangle_buf_init(&nb);
        mangle_buf_append(&nb, base);
        mangle_buf_append(&nb, "(");
        for (int i = 0; i < tn->as.named.arg_count; i++)
        {
            Type *at = resolve_type_node(c, tn->as.named.args[i], line, col);
            if (at == NULL) { mangle_buf_free(&nb); return NULL; }
            if (i > 0) mangle_buf_append(&nb, ",");
            mangle_buf_append(&nb, type_name(at));
        }
        mangle_buf_append(&nb, ")");
        char *buf = mangle_buf_take(&nb);

        /* Cache hit for already-instantiated type? */
        Type *st_cached = find_struct_type(c, buf);
        if (st_cached) { free(buf); return st_cached; }
        Type *et = find_enum_type(c, buf);
        if (et) { free(buf); return et; }

        /* B-4-for-generics: a bare generic name owned by 2+ imported modules is
           ambiguous — refuse to silently pick one. (Single-owner names are never
           marked, so the common case is unaffected.) */
        if (checker_type_is_ambiguous(c, base))
        {
            free(buf);
            checker_error(c, line, col,
                "generic type '%s' is defined in multiple imported modules; "
                "qualify it as `mod.%s(...)` (note: using more than one "
                "simultaneously is not yet supported)",
                base, base);
            return NULL;
        }

        /* G1: Try user generic struct instantiation. Use the transitive _pull
           gate so a consumer that meets Vec(T)/VecIter(T) only via a deep import
           (never imported the defining module directly) can still instantiate.
           _pull only fires on a LOCAL miss, so same-name cross-module ambiguity
           (handled by the import handler registering the name locally) is
           unaffected. */
        if (find_struct_template_idx_pull(c, base) >= 0)
        {
            int n = tn->as.named.arg_count;
            Type **ta = (Type **)malloc_safe((size_t)n * sizeof(Type *));
            for (int i = 0; i < n; i++)
            {
                ta[i] = resolve_type_node(c, tn->as.named.args[i], line, col);
                if (ta[i] == NULL) { free(ta); free(buf); return NULL; }
                if (checker_reject_borrow_type_arg(c, ta[i], base, line, col))
                    { free(ta); free(buf); return NULL; }
            }
            Type *inst = checker_instantiate_struct(c, base, ta, n, line, col);
            free(ta);
            if (inst) { free(buf); return inst; }
        }

        /* Try enum template instantiation (Option/Result, etc.). */
        int tidx = find_template_idx(c, base);
        if (tidx >= 0)
        {
            int n = tn->as.named.arg_count;
            Type **ta = NULL;
            if (n > 0)
            {
                ta = (Type **)malloc_safe((size_t)n * sizeof(Type *));
                for (int i = 0; i < n; i++)
                {
                    ta[i] = resolve_type_node(c, tn->as.named.args[i], line, col);
                    if (ta[i] == NULL) { free(ta); free(buf); return NULL; }
                    if (checker_reject_borrow_type_arg(c, ta[i], base, line, col))
                        { free(ta); free(buf); return NULL; }
                }
            }
            Type *inst = instantiate_template(c, tidx, ta, n, line, col);
            free(ta);
            free(buf);
            return inst;
        }

        checker_error(c, line, col, "unknown generic type '%s'", buf);
        free(buf);
        return NULL;
    }
    }
    return NULL;
}
