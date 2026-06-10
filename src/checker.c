/* checker.c — Type checker: walks AST, validates types, fills resolved_type */
#include "checker.h"
#include "module.h"
#include "builtins_math.h"
#include "builtins_perf.h"
#include <stdio.h>
#include <string.h>

/* ---- Stdlib gate ----
   Internal builtins (named with `__` prefix by convention) are only callable
   from files physically located under a `std/` or `stdlib/` directory — i.e.
   <LS_HOME>/std/ or <LS_HOME>/stdlib/. Detected by looking for a "/std/" or
   "/stdlib/" segment in the source path. Imperfect (a user could name their
   own directory "std"), but good enough to keep these footguns out of normal
   user code while staying allocator-policy-free. */
static bool path_is_under_stdlib(const char *path)
{
    if (path == NULL) return false;
    for (const char *p = path; *p; p++) {
        if ((p[0] == '/' || p[0] == '\\') &&
            p[1] == 's' && p[2] == 't' && p[3] == 'd')
        {
            /* /stdlib/ */
            if (p[4] == 'l' && p[5] == 'i' && p[6] == 'b' &&
                (p[7] == '/' || p[7] == '\\'))
                return true;
            /* /std/ (pure-LS stdlib modules like std/time.ls, std/proc.ls) */
            if (p[4] == '/' || p[4] == '\\')
                return true;
        }
    }
    return false;
}

/* ---- Error reporting ---- */

static void checker_error(Checker *c, int line, int col, const char *fmt, ...)
{
    if (c->error_count >= CHECKER_MAX_ERRORS)
        return;
    c->had_error = true;
    c->error_count++;

    fprintf(stderr, "[type error] %s:%d:%d: ",
            c->source_path ? c->source_path : "<unknown>", line, col);
    va_list args;
    va_start(args, fmt);
    vfprintf(stderr, fmt, args);
    va_end(args);
    fprintf(stderr, "\n");
}

static void checker_warning(Checker *c, int line, int col, const char *fmt, ...)
{
    fprintf(stderr, "[warning] %s:%d:%d: ",
            c->source_path ? c->source_path : "<unknown>", line, col);
    va_list args;
    va_start(args, fmt);
    vfprintf(stderr, fmt, args);
    va_end(args);
    fprintf(stderr, "\n");
}

/* Move-semantics error — separate from type errors so the user can distinguish them */
static void checker_move_error(Checker *c, int line, int col, const char *fmt, ...)
{
    /* Phase B: during the discovery pass of a loop we silently collect move state
       without reporting errors. The reporting pass re-runs with the flag off. */
    if (c->silent_move_errors)
        return;
    if (c->error_count >= CHECKER_MAX_ERRORS)
        return;
    c->had_error = true;
    c->error_count++;
    fprintf(stderr, "[move error] %s:%d:%d: ",
            c->source_path ? c->source_path : "<unknown>", line, col);
    va_list args;
    va_start(args, fmt);
    vfprintf(stderr, fmt, args);
    va_end(args);
    fprintf(stderr, "\n");
}

/* ---- Struct type registry ---- */

/* ---- Self placeholder for trait signatures ----
   Used as current_impl_struct_type during check_trait_decl so that
   resolve_type_node("Self") returns this sentinel instead of NULL.
   check_impl_trait_decl then substitutes it with the real struct type. */
static Type g_self_placeholder_type = {
    .kind = TYPE_STRUCT,
    .as = { .strukt = { .name = "Self", .fields = NULL, .field_count = 0, .has_drop = false } }
};

static bool is_self_placeholder(const Type *t) {
    return t == &g_self_placeholder_type;
}

/* type_equals variant that treats g_self_placeholder_type as equal to `concrete`.
   Handles TYPE_REFERENCE wrapping (e.g. &Self == &Vec2). */
static bool type_equals_with_self(const Type *trait_t, const Type *impl_t, const Type *concrete)
{
    if (trait_t == NULL || impl_t == NULL) return trait_t == impl_t;
    if (is_self_placeholder(trait_t)) return type_equals(concrete, impl_t);
    if (trait_t->kind == TYPE_REFERENCE && impl_t->kind == TYPE_REFERENCE) {
        if (trait_t->is_mut != impl_t->is_mut) return false;
        return type_equals_with_self(trait_t->as.pointer_to, impl_t->as.pointer_to, concrete);
    }
    if (trait_t->kind == TYPE_POINTER && impl_t->kind == TYPE_POINTER) {
        return type_equals_with_self(trait_t->as.pointer_to, impl_t->as.pointer_to, concrete);
    }
    return type_equals(trait_t, impl_t);
}

/* B-2: Compute the LLVM-level type name for a struct/enum defined in a module.
   Returns a malloc'd "<mod>__Name" string when c->module_name is non-NULL, else NULL.
   The caller is responsible for nothing — the returned pointer is stored in
   Type.strukt.llvm_name / Type.enom.llvm_name and intentionally leaked with the Type. */
static char *checker_module_type_llvmname(Checker *c, const char *bare_name)
{
    if (c->module_name == NULL || c->module_name[0] == '\0')
        return NULL;
    char buf[640];
    int pp = 0;
    for (const char *mp = c->module_name; *mp && pp < 600; mp++)
        buf[pp++] = (*mp == '.') ? '_' : *mp;
    buf[pp++] = '_'; buf[pp++] = '_';
    snprintf(buf + pp, sizeof(buf) - (size_t)pp, "%s", bare_name);
    char *result = (char *)malloc_safe(strlen(buf) + 1);
    memcpy(result, buf, strlen(buf) + 1);
    return result;
}

/* B-4: mark a bare type name as ambiguous (exported by 2+ imported modules). */
static void checker_mark_ambiguous_type(Checker *c, const char *name)
{
    for (int i = 0; i < c->ambiguous_type_count; i++)
        if (strcmp(c->ambiguous_types[i], name) == 0) return; /* already marked */
    if (c->ambiguous_type_count >= c->ambiguous_type_cap)
    {
        c->ambiguous_type_cap = c->ambiguous_type_cap < 8 ? 8 : c->ambiguous_type_cap * 2;
        c->ambiguous_types = realloc_safe(c->ambiguous_types,
            (size_t)c->ambiguous_type_cap * sizeof(c->ambiguous_types[0]));
    }
    c->ambiguous_types[c->ambiguous_type_count++] = name;
}

/* B-4: true if a bare type name is ambiguous across imported modules. */
static bool checker_type_is_ambiguous(Checker *c, const char *name)
{
    for (int i = 0; i < c->ambiguous_type_count; i++)
        if (strcmp(c->ambiguous_types[i], name) == 0) return true;
    return false;
}

static void register_struct_type(Checker *c, const char *name, Type *type)
{
    if (c->struct_type_count >= c->struct_type_cap)
    {
        c->struct_type_cap = GROW_CAPACITY(c->struct_type_cap);
        c->struct_types = realloc_safe(c->struct_types,
                                       (size_t)c->struct_type_cap * sizeof(c->struct_types[0]));
    }
    c->struct_types[c->struct_type_count].name = name;
    c->struct_types[c->struct_type_count].type = type;
    c->struct_type_count++;
}

static Type *find_struct_type(Checker *c, const char *name)
{
    for (int i = 0; i < c->struct_type_count; i++)
    {
        if (strcmp(c->struct_types[i].name, name) == 0)
        {
            return c->struct_types[i].type;
        }
    }
    return NULL;
}

/* Step 11: Resolve a builtin type name ("int", "i64", "f64", "bool", "string", "char")
   to its Type*.  Returns NULL for non-builtin names. */
static Type *resolve_builtin_type_by_name(const char *name)
{
    if (strcmp(name, "int") == 0)    return type_int();
    if (strcmp(name, "i64") == 0)    return type_i64();
    if (strcmp(name, "f64") == 0)    return type_f64();
    if (strcmp(name, "bool") == 0)   return type_bool();
    if (strcmp(name, "string") == 0) return type_string();
    if (strcmp(name, "char") == 0)   return type_char();
    return NULL;
}

/* True iff `t` is the pure-LS `Str` struct (std/str.ls). Recognized by name,
   like Vec/Map — the string-to-stdlib lowerings (docs/plan_string_to_stdlib.md)
   key off this. Str is non-generic, so a plain name match suffices. */
static bool type_is_str_struct(const Type *t)
{
    return t != NULL && t->kind == TYPE_STRUCT &&
           t->as.strukt.name != NULL && strcmp(t->as.strukt.name, "Str") == 0;
}

/* If `t` is `Str` or a read-only borrow `&Str`, return the underlying Str struct
   type; else NULL. A string literal coerces to a (static) Str in either slot —
   a direct `Str` position or an auto-borrowed `&Str` parameter (the resulting Str
   value is then auto-borrowed via `&Str ← Str`). */
static Type *str_target_of_expected(const Type *t)
{
    if (type_is_str_struct(t)) return (Type *)t;
    if (t != NULL && t->kind == TYPE_REFERENCE && !t->is_mut &&
        type_is_str_struct(t->as.pointer_to))
        return t->as.pointer_to;
    return NULL;
}

/* Step 11: Get the impl_registry key name for a type.
   For structs returns the struct name; for builtins returns "int", "f64" etc. */
static const char *type_impl_name(Type *t)
{
    if (t == NULL) return NULL;
    if (t->kind == TYPE_STRUCT && t->as.strukt.name) return t->as.strukt.name;
    switch (t->kind) {
    case TYPE_INT:    return "int";
    case TYPE_I64:    return "i64";
    case TYPE_F64:    return "f64";
    case TYPE_BOOL:   return "bool";
    case TYPE_STRING: return "string";
    case TYPE_CHAR:   return "char";
    default:          return NULL;
    }
}

/* ---- G1: Generic struct template registry ---- */

static int register_imported_struct_template(Checker *c, const char *base_name,
                                             char **type_params, int type_param_count,
                                             AstNode *decl_node, const char *module_path);

static int find_struct_template_idx(Checker *c, const char *base_name)
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

static void register_struct_template(Checker *c, const char *base_name,
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
static int register_imported_struct_template(Checker *c, const char *base_name,
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

static void register_type_alias(Checker *c, const char *name, Type *type)
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

static Type *find_type_alias(Checker *c, const char *name)
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

static void register_enum_type(Checker *c, const char *name, Type *type)
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
}

static Type *find_enum_type(Checker *c, const char *name)
{
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
static Type *check_expr(Checker *c, AstNode *node);
static bool type_assignable(const Type *dst, const Type *src);
static bool type_owns_heap_for_enum(const Type *t);
static bool checker_type_satisfies_trait(Checker *c, Type *type, const char *trait_name);
/* Operator overloading: try to lower `a OP b` to a user operator-method call.
   Returns true if `a` is a struct/enum overload site (handled — possibly with an
   error). Returns false to let the builtin numeric/string path proceed. */
static bool try_operator_overload(Checker *c, AstNode *node, Type *left, Type *right,
                                  Type **out_result);

/* Search all registered enums for a variant matching `vname`.
   Returns the number of matches (0 = none, 1 = unique, >1 = ambiguous).
   On unique match, fills *out_enum and *out_variant_idx.
   If c->expected_type is a TYPE_ENUM containing this variant, it is preferred
   regardless of how many other matches exist (treated as unambiguous). */
static int find_variant(Checker *c, const char *vname,
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
static Type *check_variant_ctor(Checker *c, AstNode *node, Type *enum_type, int variant_idx,
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
static int find_template_idx(Checker *c, const char *base)
{
    for (int i = 0; i < c->enum_template_count; i++)
    {
        if (strcmp(c->enum_templates[i].base_name, base) == 0)
            return i;
    }
    return -1;
}

/* Instantiate a registered template with concrete type args.  Returns the
   resulting TYPE_ENUM (cached on second call). */
static Type *instantiate_template(Checker *c, int template_idx,
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

    /* Build mangled name */
    char buf[256];
    int pos = snprintf(buf, sizeof(buf), "%s(", c->enum_templates[template_idx].base_name);
    for (int i = 0; i < type_arg_count && pos < (int)sizeof(buf) - 2; i++)
    {
        if (i > 0) pos += snprintf(buf + pos, sizeof(buf) - (size_t)pos, ",");
        pos += snprintf(buf + pos, sizeof(buf) - (size_t)pos, "%s", type_name(type_args[i]));
    }
    snprintf(buf + pos, sizeof(buf) - (size_t)pos, ")");

    /* Cache hit? */
    Type *cached = find_enum_type(c, buf);
    if (cached) return cached;

    /* Instantiate */
    int vc = c->enum_templates[template_idx].variant_count;
    Type *et = type_enum(buf, vc);
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

static void register_builtin_enums(Checker *c)
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
static const char *impl_key_of_type(const Type *t)
{
    if (t == NULL) return NULL;
    if (t->kind == TYPE_STRUCT)
        return t->as.strukt.llvm_name ? t->as.strukt.llvm_name : t->as.strukt.name;
    if (t->kind == TYPE_ENUM)
        return t->as.enom.llvm_name ? t->as.enom.llvm_name : t->as.enom.name;
    /* Phase 2.5: builtin types can carry user `impl` methods, keyed by their
       bare type name (global, not module-prefixed). Currently only string. */
    if (t->kind == TYPE_STRING)
        return "string";
    return NULL;
}

static int find_or_create_impl(Checker *c, const char *struct_name)
{
    for (int i = 0; i < c->impl_count; i++)
    {
        if (strcmp(c->impl_registry[i].struct_name, struct_name) == 0)
            return i;
    }
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
    return idx;
}

/* Returns true on success. Returns false if a method with the same name
   already exists for this struct (LS does not support method overloading).
   Error already reported in that case. */
static bool register_method(Checker *c, int impl_idx, const char *name,
                            Type *type, bool is_static, int self_borrow_kind,
                            int line, int col)
{
    /* Reject duplicate method names -- LS does not support method overloading.
       Exception: user-defined __drop overrides the compiler-generated one.
       (The auto-generated __drop from struct declaration conflicts when the user
       writes `impl Widget { fn __drop() { ... } }` — allow the replacement.) */
    for (int j = 0; j < c->impl_registry[impl_idx].method_count; j++) {
        if (strcmp(c->impl_registry[impl_idx].methods[j].name, name) == 0) {
            if (strcmp(name, "__drop") == 0) {
                /* Replace the auto-generated __drop entry with user-defined one.
                   Free the old compiler-generated function type and its param
                   array + pointer type to avoid a compile-time leak. */
                Type *old = c->impl_registry[impl_idx].methods[j].type;
                if (old && old->kind == TYPE_FUNCTION) {
                    free(old->as.function.params);
                    /* The pointer param (old->as.function.params[0]) was created
                       by type_pointer(st); it outlives use here so free it. */
                    if (old->as.function.params &&
                        old->as.function.params[0] &&
                        old->as.function.params[0]->kind == TYPE_POINTER)
                        free(old->as.function.params[0]);
                    free(old);
                }
                c->impl_registry[impl_idx].methods[j].type = type;
                c->impl_registry[impl_idx].methods[j].is_static = is_static;
                c->impl_registry[impl_idx].methods[j].self_borrow_kind = self_borrow_kind;
                return true;
            }
            const char *tname = c->impl_registry[impl_idx].struct_name;
            bool is_enum = (find_enum_type(c, tname) != NULL);
            checker_error(c, line, col,
                "conflicting method '%s': already defined for %s '%s'",
                name, is_enum ? "enum" : "struct", tname);
            return false;
        }
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
    c->impl_registry[impl_idx].method_count++;
    return true;
}

/* Phase A1: returns the registered method's self_borrow_kind, or 0 if not found
   (legacy implicit). 0 = none/legacy, 1 = &self, 2 = &!self. */
static int method_self_borrow_kind(Checker *c, const char *struct_name,
                                   const char *method_name)
{
    for (int i = 0; i < c->impl_count; i++)
    {
        if (strcmp(c->impl_registry[i].struct_name, struct_name) != 0)
            continue;
        for (int j = 0; j < c->impl_registry[i].method_count; j++)
        {
            if (strcmp(c->impl_registry[i].methods[j].name, method_name) == 0)
                return c->impl_registry[i].methods[j].self_borrow_kind;
        }
    }
    return 0;
}

/* Local strdup (checker-owned). */
static char *chk_strdup(const char *s)
{
    size_t n = strlen(s);
    char *d = (char *)malloc_safe(n + 1);
    memcpy(d, s, n + 1);
    return d;
}

/* Build the equivalent method call for the Index/IndexMut protocol:
   `v[i]` -> `v.<m>(i)`, `v[i]=x` -> `v.<m>(i, x)`. `obj`, `idx`, and optional
   `val` are reused (not cloned). */
static AstNode *make_index_protocol_call(int line, int column,
                                         AstNode *obj, AstNode *idx,
                                         AstNode *val, const char *method)
{
    AstNode *call = ast_new(AST_CALL, line, column);
    AstNode *callee = ast_new(AST_FIELD, line, column);
    callee->as.field_access.object = obj;
    callee->as.field_access.field  = chk_strdup(method);
    int argc = val ? 2 : 1;
    AstNode **args = (AstNode **)malloc_safe((size_t)argc * sizeof(AstNode *));
    args[0] = idx;
    if (val) args[1] = val;
    call->as.call.callee = callee;
    call->as.call.args = args;
    call->as.call.arg_count = argc;
    call->as.call.type_args = NULL;
    call->as.call.type_arg_count = 0;
    return call;
}

/* Rewrite an expression AST_INDEX node in place into an AST_CALL. */
static void rewrite_index_to_call(AstNode *node, AstNode *obj, AstNode *idx,
                                  const char *method)
{
    AstNode *call = make_index_protocol_call(node->line, node->column,
                                             obj, idx, NULL, method);
    node->kind = AST_CALL;
    node->as.call = call->as.call;
    free(call);
}

static Type *find_method(Checker *c, const char *struct_name, const char *method_name)
{
    for (int i = 0; i < c->impl_count; i++)
    {
        if (strcmp(c->impl_registry[i].struct_name, struct_name) != 0)
            continue;
        for (int j = 0; j < c->impl_registry[i].method_count; j++)
        {
            if (strcmp(c->impl_registry[i].methods[j].name, method_name) == 0)
            {
                return c->impl_registry[i].methods[j].type;
            }
        }
    }
    return NULL;
}

/* Tag `[..]` as a user container literal when the expected type is a struct
   that opts into the reserved __from_list(&!self, E) protocol. This mirrors the
   var-decl path and is reused for struct field defaults/overrides. */
static bool checker_tag_user_from_list_literal(Checker *c, Type *expected,
                                               AstNode *lit,
                                               const char *what)
{
    if (c == NULL || expected == NULL || lit == NULL)
        return false;
    if (expected->kind != TYPE_STRUCT || lit->kind != AST_ARRAY_LIT)
        return false;

    Type *fl = find_method(c, impl_key_of_type(expected), "__from_list");
    if (fl == NULL)
        return false;

    Type *elem_expected = (fl->kind == TYPE_FUNCTION &&
                           fl->as.function.param_count >= 2)
                          ? fl->as.function.params[1] : NULL;
    for (int i = 0; i < lit->as.array_lit.count; i++)
    {
        AstNode *elem = lit->as.array_lit.elements[i];
        if (elem_expected)
            checker_tag_user_from_list_literal(c, elem_expected, elem, what);

        Type *saved_expected = c->expected_type;
        c->expected_type = elem_expected;
        Type *et = check_expr(c, elem);
        c->expected_type = saved_expected;
        if (elem_expected && et && !type_assignable(elem_expected, et))
        {
            checker_error(c, elem->line, elem->column,
                          "%s element type mismatch: expected '%s', got '%s'",
                          what ? what : "list-literal",
                          type_name(elem_expected), type_name(et));
        }
    }
    lit->resolved_type = expected;
    return true;
}

/* M-LIT: tag `{ k: v, ... }` (AST_MAP_LIT) as a user key-value literal when the
   expected type is a struct opting into the reserved __from_pairs(&!self, K, V)
   protocol (e.g. std.map Map(K,V)). Type-checks each pair against K/V and routes
   nested container literals (a Vec `[..]` value, a nested `{..}` map value) to
   their own from_list/from_pairs taggers. Mirrors checker_tag_user_from_list_literal. */
static bool checker_tag_user_from_pairs_literal(Checker *c, Type *expected,
                                                AstNode *lit, const char *what)
{
    if (c == NULL || expected == NULL || lit == NULL)
        return false;
    if (expected->kind != TYPE_STRUCT || lit->kind != AST_MAP_LIT)
        return false;

    Type *fp = find_method(c, impl_key_of_type(expected), "__from_pairs");
    if (fp == NULL)
        return false;

    Type *kexp = (fp->kind == TYPE_FUNCTION && fp->as.function.param_count >= 3)
                 ? fp->as.function.params[1] : NULL;   /* params: [*Self, K, V] */
    Type *vexp = (fp->kind == TYPE_FUNCTION && fp->as.function.param_count >= 3)
                 ? fp->as.function.params[2] : NULL;

    for (int i = 0; i < lit->as.map_lit.pair_count; i++)
    {
        AstNode *kn = lit->as.map_lit.keys[i];
        AstNode *vn = lit->as.map_lit.vals[i];

        if (kexp) {
            checker_tag_user_from_pairs_literal(c, kexp, kn, what);
            checker_tag_user_from_list_literal(c, kexp, kn, what);
        }
        if (vexp) {
            checker_tag_user_from_pairs_literal(c, vexp, vn, what);
            checker_tag_user_from_list_literal(c, vexp, vn, what);
        }

        Type *saved = c->expected_type;
        c->expected_type = kexp;
        Type *kt = check_expr(c, kn);
        c->expected_type = vexp;
        Type *vt = check_expr(c, vn);
        c->expected_type = saved;

        if (kexp && kt && !type_assignable(kexp, kt))
            checker_error(c, kn->line, kn->column,
                          "%s key type mismatch: expected '%s', got '%s'",
                          what ? what : "map-literal", type_name(kexp), type_name(kt));
        if (vexp && vt && !type_assignable(vexp, vt))
            checker_error(c, vn->line, vn->column,
                          "%s value type mismatch: expected '%s', got '%s'",
                          what ? what : "map-literal", type_name(vexp), type_name(vt));
    }
    lit->resolved_type = expected;
    return true;
}

/* Check if a registered method is static. Returns -1 if not found, 0 if instance, 1 if static */
static int method_is_static(Checker *c, const char *struct_name, const char *method_name)
{
    for (int i = 0; i < c->impl_count; i++)
    {
        if (strcmp(c->impl_registry[i].struct_name, struct_name) != 0)
            continue;
        for (int j = 0; j < c->impl_registry[i].method_count; j++)
        {
            if (strcmp(c->impl_registry[i].methods[j].name, method_name) == 0)
            {
                return c->impl_registry[i].methods[j].is_static ? 1 : 0;
            }
        }
    }
    return -1;
}

/* ---- G1: Generic struct instantiation ---- */

/* Forward declarations for mutual recursion */
static Type *resolve_type_node(Checker *c, TypeNode *tn, int line, int col);

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
static void push_scope(Checker *c);
static void pop_scope(Checker *c);
static void check_stmt(Checker *c, AstNode *node);
static bool checker_type_satisfies_trait(Checker *c, Type *type, const char *trait_name);
static void instantiate_impl_method_types(
    Checker *c, Type *struct_type, const char *mangled_name,
    AstNode *impl_node,
    char **tp_names, Type **type_args, int tp_count);

Type *checker_instantiate_struct(Checker *c,
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
                            "type '%s' does not satisfy trait '%s' "
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

    /* Build mangled name: "Pair(int,string)".
       F6b: for struct/enum element types that come from a module, use the
       module-prefixed `llvm_name` (e.g. "ma__Node") instead of the bare name
       ("Node"). Two modules each defining `Node` would otherwise both mangle to
       "Vec(Node)" and collide (the second instantiation cache-hits the first,
       conflating distinct element types). Primitives/non-module types keep their
       bare `type_name`. This mirrors impl_key_of_type's llvm_name?? name. */
    char buf[512];
    int pos = snprintf(buf, sizeof(buf), "%s(", base_name);
    for (int i = 0; i < type_arg_count && pos < (int)sizeof(buf) - 2; i++) {
        if (i > 0) pos += snprintf(buf + pos, sizeof(buf) - (size_t)pos, ",");
        const char *an = NULL;
        Type *at = type_args[i];
        if (at && at->kind == TYPE_STRUCT && at->as.strukt.llvm_name)
            an = at->as.strukt.llvm_name;
        else if (at && at->kind == TYPE_ENUM && at->as.enom.llvm_name)
            an = at->as.enom.llvm_name;
        if (an == NULL) an = type_name(at);
        pos += snprintf(buf + pos, sizeof(buf) - (size_t)pos, "%s", an);
    }
    snprintf(buf + pos, sizeof(buf) - (size_t)pos, ")");

    /* Cache hit? */
    Type *cached = find_struct_type(c, buf);
    if (cached) return cached;

    /* Instantiate: create new TYPE_STRUCT with concrete field types */
    AstNode *decl = c->struct_templates[tmpl_idx].decl_node;
    int fc = decl->as.struct_decl.field_count;
    char **tp_names = c->struct_templates[tmpl_idx].type_params;

    /* Allocate mangled name (owned by the Type) */
    size_t namelen = strlen(buf);
    char *mangled = (char *)malloc_safe(namelen + 1);
    memcpy(mangled, buf, namelen + 1);

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
        st->as.strukt.fields[i].name = decl->as.struct_decl.field_names[i];
        st->as.strukt.fields[i].type = ft;
        st->as.strukt.fields[i].default_expr =
            decl->as.struct_decl.field_defaults ? decl->as.struct_decl.field_defaults[i] : NULL;

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

static bool check_and_queue_generic_method(Checker *c, Type *struct_type,
                                           const char *mangled_name,
                                           AstNode *method, Type *mtype,
                                           char **tp_names, Type **type_args,
                                           int tp_count, int line, int col)
{
    const char *mname = method->as.fn_decl.name;
    char mfn_name[512];
    snprintf(mfn_name, sizeof(mfn_name), "%s.%s", mangled_name, mname);

    if (!check_method_where_bounds(c, method, mfn_name, tp_names, type_args, tp_count))
        return false;

    bool is_static = method->as.fn_decl.is_static;
    int sbk = method->as.fn_decl.self_borrow_kind;
    int pc = method->as.fn_decl.param_count;

    AstNode *cloned = ast_clone_deep(method);
    cloned->as.fn_decl.impl_struct_name = mangled_name; /* not owned */

    int saved_alias_count = c->type_alias_count;
    for (int i = 0; i < tp_count; i++)
        register_type_alias(c, tp_names[i], type_args[i]);

    push_scope(c);
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
                if (sym_type->kind == TYPE_STRING) psym->is_static_string = false;
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
    Type *saved_ret = c->current_fn_return;
    c->current_fn_return = mtype->as.function.return_type;
    check_stmt(c, cloned->as.fn_decl.body);
    c->current_fn_return = saved_ret;
    pop_scope(c);
    c->type_alias_count = saved_alias_count;

    if (c->had_error) {
        ast_free(cloned);
        return false;
    }

    cloned->resolved_type = mtype;
    char *owned_mfn = (char *)malloc_safe(strlen(mfn_name) + 1);
    memcpy(owned_mfn, mfn_name, strlen(mfn_name) + 1);
    pending_generic_method_add(c, cloned, owned_mfn, struct_type);
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

static bool ensure_generic_method_instantiated(Checker *c,
                                               const char *mangled_struct,
                                               const char *method_name,
                                               int line, int col)
{
    char mfn_name[512];
    snprintf(mfn_name, sizeof(mfn_name), "%s.%s", mangled_struct, method_name);
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

/* Try to instantiate a method-level generic impl method.
   Called when find_method returns NULL but the call site provides
   explicit type arguments. Looks up generic_impl_method_templates,
   resolves the method-level type params, combines them with the
   impl-level params, builds the concrete signature, and queues
   the body for lazy codegen. Returns the concrete TYPE_FUNCTION
   on success, NULL on failure (caller should issue the usual "no
   such method" error). */
static Type *try_instantiate_method_level_generic(Checker *c,
    const char *impl_key, const char *method_name,
    TypeNode **call_type_args, int call_type_arg_count,
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
    if (call_type_arg_count != mtp_count) {
        checker_error(c, line, col,
            "method '%s' expects %d type argument(s), got %d",
            method_name, mtp_count, call_type_arg_count);
        return NULL;
    }

    /* Step 2: resolve method-level type args */
    Type **mtp_type_args = (Type **)malloc_safe((size_t)mtp_count * sizeof(Type *));
    bool ok = true;
    for (int ti = 0; ti < mtp_count; ti++) {
        mtp_type_args[ti] = resolve_type_node(c, call_type_args[ti], line, col);
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

    /* Step 4: build mangled call name: "RawVec(int).map(string)" */
    char mangled[512];
    int pos = snprintf(mangled, sizeof(mangled), "%s.%s(", impl_key, method_name);
    for (int ti = 0; ti < mtp_count && pos < (int)sizeof(mangled) - 2; ti++) {
        if (ti > 0) pos += snprintf(mangled + pos, sizeof(mangled) - (size_t)pos, ",");
        pos += snprintf(mangled + pos, sizeof(mangled) - (size_t)pos, "%s",
            type_name(mtp_type_args[ti]));
    }
    snprintf(mangled + pos, sizeof(mangled) - (size_t)pos, ")");

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
            free(mtp_type_args);
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
    }

    Type *ret = method_ast->as.fn_decl.return_type
        ? resolve_type_node_with_substitution(
            c, method_ast->as.fn_decl.return_type,
            all_tp_names, all_tp_types, total_tp_count)
        : type_void();

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
            free(mtp_type_args);
            return NULL;
        }
        for (int bi = 0; bi < wb->bounds.count; bi++) {
            if (!checker_type_satisfies_trait(c, bound_type, wb->bounds.trait_names[bi])) {
                checker_error(c, line, col,
                    "type '%s' does not satisfy trait '%s' "
                    "(required by method '%s')",
                    type_name(bound_type), wb->bounds.trait_names[bi], mangled);
                c->type_alias_count = saved_alias_count;
                free(params); free(all_tp_names); free(all_tp_types);
                free(mtp_type_args);
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
    push_scope(c);

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

    /* Check the body */
    if (cloned->as.fn_decl.body) {
        bool old_silent = c->silent_move_errors;
        bool old_return = c->in_return_expr;
        c->in_return_expr = false;

        check_stmt(c, cloned->as.fn_decl.body);

        c->in_return_expr = old_return;
        c->silent_move_errors = old_silent;
    }

    c->type_alias_count = scope_saved_alias;
    c->current_fn_return = saved_return;
    pop_scope(c);

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
    /* params ownership transferred to concrete_type via type_function() — do NOT free */

    return concrete_type;
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
            if (mm->kind == AST_FN_DECL && mm->as.fn_decl.name &&
                strcmp(mm->as.fn_decl.name, "__drop") == 0)
            {
                struct_type->as.strukt.has_drop = true;
                struct_type->as.strukt.has_user_drop = true;
                break;
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
       they appear in method signatures (e.g. `fn map(U)(&self, Block(W)->U f)`). */
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
            c->generic_impl_method_templates[idx].method_name = mname;
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

        /* Build concrete method type: self ptr (if instance) + user params */
        int total = is_static ? pc : pc + 1;
        Type **params = (Type **)malloc_safe((size_t)total * sizeof(Type *));
        int offset = 0;
        if (!is_static) {
            params[0] = type_pointer(struct_type);
            offset = 1;
        }

        for (int j = 0; j < pc; j++) {
            Type *pt = resolve_type_node_with_substitution(
                c, method->as.fn_decl.param_types[j],
                tp_names, type_args, tp_count);
            params[offset + j] = pt ? pt : type_int(); /* fallback */
        }

        Type *ret = method->as.fn_decl.return_type
            ? resolve_type_node_with_substitution(
                c, method->as.fn_decl.return_type,
                tp_names, type_args, tp_count)
            : type_void();

        Type *mtype = type_function(params, total, ret, false);

        register_method(c, impl_idx, mname, mtype, is_static, sbk,
                        method->line, method->column);

        /* Build mangled function name: "Pair(int,string).get_first" */
        char mfn_name[512];
        snprintf(mfn_name, sizeof(mfn_name), "%s.%s", mangled_name, mname);
        if (generic_method_is_eager(mname))
            check_and_queue_generic_method(c, struct_type, mangled_name, method,
                                           mtype, tp_names, type_args, tp_count,
                                           method->line, method->column);
        else
            register_lazy_generic_method(c, mfn_name, method, mtype, struct_type,
                                         tp_names, type_args, tp_count);
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
static void ensure_generic_struct_impls_local(Checker *c, Type *st)
{
    if (st == NULL || st->kind != TYPE_STRUCT ||
        st->as.strukt.name == NULL || st->as.strukt.generic_base == NULL)
        return;
    const char *name = st->as.strukt.name;
    for (int i = 0; i < c->impl_count; i++)
        if (strcmp(c->impl_registry[i].struct_name, name) == 0 &&
            c->impl_registry[i].method_count > 0)
            return; /* already registered locally */
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

/* ---- Resolve TypeNode -> Type ---- */

static Type *resolve_type_node(Checker *c, TypeNode *tn, int line, int col)
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
        case TOKEN_TYPE_BOOL:
            return type_bool();
        case TOKEN_TYPE_CHAR:
            return type_char();
        case TOKEN_TYPE_STRING:
            return type_string();
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
        /* Phase 5/5.5/5.8/9: supported borrow pointees are
           string / struct / enum. */
        bool ok_kind = (pointee->kind == TYPE_STRING ||
                        pointee->kind == TYPE_STRUCT ||
                        pointee->kind == TYPE_ENUM);   /* Phase 9: enum borrow */
        if (!ok_kind)
        {
            checker_error(c, line, col,
                          "&%s%s is not supported yet; only &string / &!string / "
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
            checker_error(c, line, col, "unknown type '%s'", tn->as.named.name);
            return NULL;
        }

        /* Generic-style instantiation: build mangled name "Name(arg1,arg2)"
           and look up an enum instance. Step 8 will add Option/Result template
           instantiation here when the lookup misses. */
        const char *base = tn->as.named.name;
        char buf[256];
        int pos = snprintf(buf, sizeof(buf), "%s(", base);
        for (int i = 0; i < tn->as.named.arg_count && pos < (int)sizeof(buf) - 2; i++)
        {
            Type *at = resolve_type_node(c, tn->as.named.args[i], line, col);
            if (at == NULL) return NULL;
            if (i > 0) pos += snprintf(buf + pos, sizeof(buf) - (size_t)pos, ",");
            pos += snprintf(buf + pos, sizeof(buf) - (size_t)pos, "%s", type_name(at));
        }
        snprintf(buf + pos, sizeof(buf) - (size_t)pos, ")");

        /* Cache hit for already-instantiated type? */
        Type *st_cached = find_struct_type(c, buf);
        if (st_cached) return st_cached;
        Type *et = find_enum_type(c, buf);
        if (et) return et;

        /* B-4-for-generics: a bare generic name owned by 2+ imported modules is
           ambiguous — refuse to silently pick one. (Single-owner names are never
           marked, so the common case is unaffected.) */
        if (checker_type_is_ambiguous(c, base))
        {
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
                if (ta[i] == NULL) { free(ta); return NULL; }
            }
            Type *inst = checker_instantiate_struct(c, base, ta, n, line, col);
            free(ta);
            if (inst) return inst;
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
                    if (ta[i] == NULL) { free(ta); return NULL; }
                }
            }
            Type *inst = instantiate_template(c, tidx, ta, n, line, col);
            free(ta);
            return inst;
        }

        checker_error(c, line, col, "unknown generic type '%s'", buf);
        return NULL;
    }
    }
    return NULL;
}

/* ---- Type compatibility ---- */

/* Check if src type can be assigned to dst type (includes implicit conversions).
   Returns true if compatible. This is stricter than type_equals: it also allows
   *T -> object (implicit), nil -> object, nil -> *T. */
static bool type_assignable(const Type *dst, const Type *src)
{
    if (type_equals(dst, src))
        return true;
    if (dst == NULL || src == NULL)
        return false;

    /* A named function can be coerced to a Block value with the same signature.
       The ABI adaptation is handled by codegen via a thunk that ignores env. */
    if (dst->kind == TYPE_BLOCK && src->kind == TYPE_FUNCTION)
    {
        if (dst->as.function.param_count != src->as.function.param_count)
            return false;
        if (dst->as.function.is_vararg != src->as.function.is_vararg)
            return false;
        if (!type_equals(dst->as.function.return_type, src->as.function.return_type))
            return false;
        for (int i = 0; i < dst->as.function.param_count; i++)
            if (!type_equals(dst->as.function.params[i], src->as.function.params[i]))
                return false;
        return true;
    }

    /* Implicit numeric widening (Zig-style): only when dst can represent
       every value of src. Narrowing, signed↔unsigned same-width, float→int,
       and i64↔f64 (mantissa overflow) all remain compile errors. */
    if (type_widens_to(src, dst))
        return true;

    /* nil -> *T or nil -> object */
    if (src->kind == TYPE_NIL && (dst->kind == TYPE_POINTER || dst->kind == TYPE_OBJECT))
        return true;

    /* *T -> object (implicit upcast) */
    if (dst->kind == TYPE_OBJECT && src->kind == TYPE_POINTER)
        return true;

    /* *T -> *u8 (any pointer to byte pointer, e.g. for free()) */
    if (dst->kind == TYPE_POINTER && src->kind == TYPE_POINTER &&
        dst->as.pointer_to && dst->as.pointer_to->kind == TYPE_U8)
        return true;

    /* &T ← T  (auto-borrow — only for READ-only references; &!T must be explicit) */
    if (dst->kind == TYPE_REFERENCE && !dst->is_mut &&
        type_equals(dst->as.pointer_to, src))
        return true;

    /* &T ← &T / &!T ← &!T — already covered by type_equals above (is_mut matches) */

    /* &T ← &!T  (mutable-to-readonly downgrade: callee sees it as read-only).
       The reverse &!T ← &T is FORBIDDEN — you can't upgrade a read-only
       borrow into a writable one. */
    if (dst->kind == TYPE_REFERENCE && !dst->is_mut &&
        src->kind == TYPE_REFERENCE &&
        type_equals(dst->as.pointer_to, src->as.pointer_to))
        return true;

    /* T ← &T / T ← &!T  (auto-reborrow: reading through a borrow yields same T).
       Safe because string ABI is pass-by-value regardless. */
    if (src->kind == TYPE_REFERENCE &&
        type_equals(src->as.pointer_to, dst))
        return true;

    /* object -> object (trivially via type_equals above, but explicit) */
    return false;
}

/* ---- Scope helpers ---- */

static void push_scope(Checker *c)
{
    c->current_scope = scope_new(c->current_scope);
}

static void pop_scope(Checker *c)
{
    Scope *old = c->current_scope;
    c->current_scope = old->parent;
    scope_free(old);
}

/* ---- Forward declarations ---- */

static Type *check_expr(Checker *c, AstNode *node);
static void check_stmt(Checker *c, AstNode *node);
static void check_decl(Checker *c, AstNode *node);
static int find_fn_template(Checker *c, const char *name);

/* ---- Helper functions ---- */

static bool struct_has_string_fields(Type *t)
{
    if (t == NULL || t->kind != TYPE_STRUCT)
        return false;
    for (int i = 0; i < t->as.strukt.field_count; i++)
    {
        Type *ft = t->as.strukt.fields[i].type;
        if (ft->kind == TYPE_STRING)
            return true;
        if (ft->kind == TYPE_STRUCT && struct_has_string_fields(ft))
            return true;
    }
    return false;
}

/* ---- Move semantics helpers (Phase A: linear, no control flow) ---- */

/* Returns true if a type requires move tracking (has heap ownership).
   Used by Phase A/B checkers — Phase A currently only handles TYPE_STRING. */
static bool type_is_movable(Type *t)
{
    if (!t) return false;
    switch (t->kind)
    {
    case TYPE_STRING: return true;
    case TYPE_STRUCT: return t->as.strukt.has_drop;
    case TYPE_BLOCK:  return true;  /* F.2: Block owns its env heap */
    default:          return false;
    }
}

/* Returns true if an expression statically evaluates to a string with cap==0 at runtime.
   Only string literals and identifiers previously marked as static qualify. */
static bool string_expr_is_static(Checker *c, AstNode *expr)
{
    if (!expr) return false;
    if (expr->kind == AST_STRING_LIT) return true;
    if (expr->kind == AST_IDENT)
    {
        Symbol *sym = scope_resolve(c->current_scope, expr->as.ident.name);
        if (sym && sym->type && sym->type->kind == TYPE_STRING)
            return sym->is_static_string;
    }
    return false;
}

/* Attempt to mark an IDENT arg as MOVED for any movable type
   (string, vec, map, struct-with-drop — see type_is_movable).
   - Non-IDENT nodes (temporaries, literals, field accesses) are silently skipped.
   - Static strings (is_static_string == true) are never implicitly moved.
   - Already-moved variables are skipped (error already reported by check_expr).
   Call AFTER check_expr() has been called on the arg so that:
     (a) type info is resolved, and
     (b) "use of moved variable" is already reported if applicable. */
static void checker_try_mark_moved(Checker *c, AstNode *arg)
{
    (void)c;
    if (!arg || arg->kind != AST_IDENT) return;
    Symbol *sym = scope_resolve(c->current_scope, arg->as.ident.name);
    if (!sym || !sym->type) return;
    if (!type_is_movable(sym->type)) return;
    /* Already moved/maybe-moved → error was already reported by check_expr AST_IDENT */
    if (sym->is_moved || sym->is_maybe_moved) return;
    /* Borrow parameters (&T / &!T) hold no ownership — never marked moved.
       Move-site rejection is surfaced separately by checker_reject_borrow_move. */
    if (sym->is_borrow || sym->is_mut_borrow) return;
    /* Static strings are freely shared — no heap ownership, no move.
       Only applies to TYPE_STRING (other movable types have no "static" variant). */
    if (sym->type->kind == TYPE_STRING && sym->is_static_string) return;
    sym->is_moved = true;
    /* Move-elision (Q4): record on the node that this use transferred ownership,
       so codegen can move (not clone) the heap and invalidate the source.
       Only reached for owned, non-borrow, non-static-string movable IDENTs. */
    arg->moved_out = true;
}

/* Phase 5: reject attempts to move a borrowed variable (e.g. vec.push(s)
   where s is an &string parameter). Called BEFORE the generic
   checker_try_mark_moved at known move sites. Returns true on error. */
/* Phase 5.5: reject using a writable borrow as the source of a move-style
   copy (e.g. `string t = s` where s is a &!string parameter). Read-only
   borrows (&T) are not rejected here — their ABI guarantees cap==0 on the
   callee side, so a subsequent `string t = s` produces a harmless alias.
   Returns true if an error was reported. */
static bool checker_reject_mut_borrow_copy_source(Checker *c, AstNode *src, const char *what)
{
    if (!src || src->kind != AST_IDENT) return false;
    Symbol *sym = scope_resolve(c->current_scope, src->as.ident.name);
    if (!sym || !sym->is_mut_borrow) return false;
    checker_move_error(c, src->line, src->column,
                       "cannot %s: '%s' is a writable borrow — content cannot leave",
                       what, src->as.ident.name);
    return true;
}

/* Phase 5.8: reject copy-out of a struct borrow (both &struct and &!struct).
   Same rationale: an owned struct value would be a memcpy alias to caller's
   storage; if it had drop-fields they would double-free, and even POD copies
   could violate the read-only contract. */
static bool checker_reject_struct_borrow_copy_source(Checker *c, AstNode *src, const char *what)
{
    if (!src || src->kind != AST_IDENT) return false;
    Symbol *sym = scope_resolve(c->current_scope, src->as.ident.name);
    if (!sym) return false;
    if (!sym->type || sym->type->kind != TYPE_STRUCT) return false;
    if (!sym->is_borrow && !sym->is_mut_borrow) return false;
    checker_move_error(c, src->line, src->column,
                       "cannot %s: '%s' is a %sborrow of struct — value cannot be copied out",
                       what, src->as.ident.name,
                       sym->is_mut_borrow ? "writable " : "read-only ");
    return true;
}

/* F.2: reject moving a Block parameter (is_borrow=true).
   Block pass-by-value is a shallow copy: both caller and callee share env_ptr.
   Moving from a Block param would drop the env the caller still holds → double-free. */
static bool checker_reject_block_param_move(Checker *c, AstNode *src, const char *what)
{
    if (!src || src->kind != AST_IDENT) return false;
    Symbol *sym = scope_resolve(c->current_scope, src->as.ident.name);
    if (!sym) return false;
    if (!sym->type || sym->type->kind != TYPE_BLOCK) return false;
    if (!sym->is_borrow) return false;
    checker_move_error(c, src->line, src->column,
                       "cannot %s: '%s' is a Block parameter — call it directly; "
                       "Block parameters cannot be moved (they share env with the caller)",
                       what, src->as.ident.name);
    return true;
}

/* Phase G note: the former F.3/F.4A rejections (copy a Block out of a struct
   field / vec element / map value) have been removed — codegen now deep-clones
   the closure env at the copy-out site (cg_emit_block_env_clone in codegen.c),
   so the destination owns an independent env with no shared-env double-free. */

static bool checker_reject_borrow_move(Checker *c, AstNode *arg, const char *what)
{
    if (!arg || arg->kind != AST_IDENT) return false;
    Symbol *sym = scope_resolve(c->current_scope, arg->as.ident.name);
    if (!sym) return false;
    if (sym->is_borrow) {
        checker_move_error(c, arg->line, arg->column,
                           "cannot %s: variable '%s' is a read-only borrow",
                           what, arg->as.ident.name);
        return true;
    }
    if (sym->is_mut_borrow) {
        checker_move_error(c, arg->line, arg->column,
                           "cannot %s: variable '%s' is a writable borrow "
                           "(mutation allowed, but ownership cannot leave)",
                           what, arg->as.ident.name);
        return true;
    }
    return false;
}

/* ---- Move semantics helpers (Phase B: control-flow aware) ---- */

/* A snapshot records the (is_moved, is_maybe_moved) pair for every movable symbol
   reachable from a scope chain at the moment of capture. Used by if/else merging
   and 2-pass loop analysis. */
typedef struct {
    Symbol *sym;
    bool is_moved;
    bool is_maybe_moved;
} MoveSnapEntry;

typedef struct {
    MoveSnapEntry *entries;
    int count;
    int capacity;
} MoveSnapshot;

static void move_snap_init(MoveSnapshot *snap) {
    snap->entries = NULL;
    snap->count = 0;
    snap->capacity = 0;
}

static void move_snap_free(MoveSnapshot *snap) {
    free(snap->entries);
    snap->entries = NULL;
    snap->count = 0;
    snap->capacity = 0;
}

static void move_snap_push(MoveSnapshot *snap, Symbol *sym) {
    if (snap->count >= snap->capacity) {
        snap->capacity = GROW_CAPACITY(snap->capacity);
        snap->entries = realloc_safe(snap->entries,
                                     (size_t)snap->capacity * sizeof(MoveSnapEntry));
    }
    snap->entries[snap->count].sym = sym;
    snap->entries[snap->count].is_moved = sym->is_moved;
    snap->entries[snap->count].is_maybe_moved = sym->is_maybe_moved;
    snap->count++;
}

/* Capture the current move state of every movable symbol in the scope chain
   starting at c->current_scope (walks up through parents). */
static void move_snap_capture(Checker *c, MoveSnapshot *snap) {
    move_snap_init(snap);
    for (Scope *s = c->current_scope; s != NULL; s = s->parent) {
        for (int i = 0; i < s->count; i++) {
            Symbol *sym = &s->symbols[i];
            if (type_is_movable(sym->type))
                move_snap_push(snap, sym);
        }
    }
}

/* Write a snapshot back to its symbols (restore). */
static void move_snap_restore(const MoveSnapshot *snap) {
    for (int i = 0; i < snap->count; i++) {
        snap->entries[i].sym->is_moved = snap->entries[i].is_moved;
        snap->entries[i].sym->is_maybe_moved = snap->entries[i].is_maybe_moved;
    }
}

/* Merge two branch-end snapshots into live symbol state (if/else join).
   Rule: for each symbol,
     MOVED in both branches  → MOVED
     MOVED in exactly one    → MAYBE_MOVED
     MAYBE_MOVED in either   → MAYBE_MOVED
     otherwise               → LIVE
   Assumes `a` and `b` describe the same set of symbols (captured from the same
   scope chain). */
static void move_snap_merge_into_symbols(const MoveSnapshot *a, const MoveSnapshot *b) {
    for (int i = 0; i < a->count; i++) {
        Symbol *sym = a->entries[i].sym;
        /* Find corresponding entry in b (same ordering usually, but search by pointer
           to be safe — scope chain hasn't changed between captures). */
        bool a_moved = a->entries[i].is_moved;
        bool a_maybe = a->entries[i].is_maybe_moved;
        bool b_moved = false;
        bool b_maybe = false;
        for (int j = 0; j < b->count; j++) {
            if (b->entries[j].sym == sym) {
                b_moved = b->entries[j].is_moved;
                b_maybe = b->entries[j].is_maybe_moved;
                break;
            }
        }
        if (a_moved && b_moved) {
            sym->is_moved = true;
            sym->is_maybe_moved = false;
        } else if (a_moved || b_moved || a_maybe || b_maybe) {
            sym->is_moved = false;
            sym->is_maybe_moved = true;
        } else {
            sym->is_moved = false;
            sym->is_maybe_moved = false;
        }
    }
}

/* Elevate: compare the `before` snapshot against current symbol state; any symbol
   that transitioned LIVE → MOVED is demoted to MAYBE_MOVED. Used for:
     - if-without-else:   then-branch MOVED → MAYBE_MOVED after the if
     - after loop body:   anything moved in the loop becomes MAYBE_MOVED
       (since the loop may execute 0 times).
   MAYBE_MOVED stays MAYBE_MOVED; already-MOVED entries in `before` stay MOVED. */
static void move_elevate_moves_to_maybe(const MoveSnapshot *before) {
    for (int i = 0; i < before->count; i++) {
        Symbol *sym = before->entries[i].sym;
        bool was_moved = before->entries[i].is_moved;
        bool was_maybe = before->entries[i].is_maybe_moved;
        if (was_moved) continue; /* already MOVED before — leave alone */
        /* LIVE or MAYBE before */
        if (sym->is_moved) {
            sym->is_moved = false;
            sym->is_maybe_moved = true;
        } else if (sym->is_maybe_moved) {
            /* already MAYBE_MOVED — leave */
        } else if (was_maybe) {
            sym->is_maybe_moved = true;
        }
    }
}

/* For 2-pass loops: given the "before loop" snapshot and the "after pass-1"
   snapshot, pre-seed MAYBE_MOVED on any symbol whose state worsened during
   pass 1. Then pass 2 will correctly report errors. */
static void move_preseed_maybe_from_pass1(const MoveSnapshot *before,
                                          const MoveSnapshot *after_pass1) {
    for (int i = 0; i < before->count; i++) {
        Symbol *sym = before->entries[i].sym;
        bool before_moved = before->entries[i].is_moved;
        bool before_maybe = before->entries[i].is_maybe_moved;
        if (before_moved) continue; /* already MOVED — nothing worse possible */
        /* find matching entry in after_pass1 */
        for (int j = 0; j < after_pass1->count; j++) {
            if (after_pass1->entries[j].sym != sym) continue;
            bool p1_moved = after_pass1->entries[j].is_moved;
            bool p1_maybe = after_pass1->entries[j].is_maybe_moved;
            if (p1_moved || (p1_maybe && !before_maybe)) {
                sym->is_moved = false;
                sym->is_maybe_moved = true;
            }
            break;
        }
    }
}

/* ---- String builtin method type checking ---- */

/* Type-check a string method call: s.method(args...).
   Returns the result type, or NULL on error. */
static Type *check_string_method(Checker *c, AstNode *call_node, Type *obj_type)
{
    (void)obj_type;
    const char *method = call_node->as.call.callee->as.field_access.field;
    int argc = call_node->as.call.arg_count;

    /* s.append(string|char|int) -> void: in-place append */
    if (strcmp(method, "append") == 0)
    {
        if (argc != 1)
        {
            checker_error(c, call_node->line, call_node->column,
                          "string.append() takes 1 argument, got %d", argc);
            return NULL;
        }
        /* Phase 5/5.5: receiver must be a mutable location. Reject read-only
           borrows (`&string`) — they may not mutate. `&!string` (writable borrow)
           and regular owned strings are fine. */
        AstNode *recv = call_node->as.call.callee->as.field_access.object;
        if (recv->kind == AST_IDENT)
        {
            Symbol *rsym = scope_resolve(c->current_scope, recv->as.ident.name);
            if (rsym && rsym->is_borrow)
            {
                checker_move_error(c, recv->line, recv->column,
                                   "cannot call string.append() on '%s': it is a read-only borrow",
                                   recv->as.ident.name);
                return NULL;
            }
        }
        Type *arg = check_expr(c, call_node->as.call.args[0]);
        if (arg && arg->kind != TYPE_STRING && arg->kind != TYPE_CHAR && !type_is_integer(arg))
        {
            checker_error(c, call_node->as.call.args[0]->line,
                          call_node->as.call.args[0]->column,
                          "string.append() argument must be string, char, or int, got '%s'",
                          type_name(arg));
            return NULL;
        }
        return type_void();
    }

    /* s.empty() -> bool */
    if (strcmp(method, "empty") == 0)
    {
        if (argc != 0)
        {
            checker_error(c, call_node->line, call_node->column,
                          "string.empty() takes no arguments, got %d", argc);
            return NULL;
        }
        return type_bool();
    }

    /* Phase E.3.3: s.to_cstr() -> object  — raw i8* for FFI calls.
       Always available (works on owned, static, borrowed). Caller must not
       free; LS retains ownership and frees on scope exit. */
    if (strcmp(method, "to_cstr") == 0)
    {
        if (argc != 0)
        {
            checker_error(c, call_node->line, call_node->column,
                          "string.to_cstr() takes no arguments, got %d", argc);
            return NULL;
        }
        return type_object();
    }

    /* s.at(int i) -> int */
    if (strcmp(method, "at") == 0 || strcmp(method, "at_unsafe") == 0 ||
        strcmp(method, "skip_ws") == 0 || strcmp(method, "scan_plain") == 0 ||
        strcmp(method, "scan_digits") == 0)
    {
        if (argc != 1)
        {
            checker_error(c, call_node->line, call_node->column,
                          "string.%s() takes 1 argument, got %d", method, argc);
            return NULL;
        }
        Type *arg = check_expr(c, call_node->as.call.args[0]);
        if (arg && !type_is_integer(arg))
        {
            checker_error(c, call_node->as.call.args[0]->line,
                          call_node->as.call.args[0]->column,
                          "string.%s() index must be integer, got '%s'", method, type_name(arg));
            return NULL;
        }
        return type_int();
    }

    /* s.find(string sub) -> int */
    if (strcmp(method, "find") == 0)
    {
        if (argc != 1)
        {
            checker_error(c, call_node->line, call_node->column,
                          "string.find() takes 1 argument, got %d", argc);
            return NULL;
        }
        Type *arg = check_expr(c, call_node->as.call.args[0]);
        if (arg && arg->kind != TYPE_STRING)
        {
            checker_error(c, call_node->as.call.args[0]->line,
                          call_node->as.call.args[0]->column,
                          "string.find() argument must be string, got '%s'", type_name(arg));
            return NULL;
        }
        return type_int();
    }

    /* s.contains(string sub) -> bool */
    if (strcmp(method, "contains") == 0)
    {
        if (argc != 1)
        {
            checker_error(c, call_node->line, call_node->column,
                          "string.contains() takes 1 argument, got %d", argc);
            return NULL;
        }
        Type *arg = check_expr(c, call_node->as.call.args[0]);
        if (arg && arg->kind != TYPE_STRING)
        {
            checker_error(c, call_node->as.call.args[0]->line,
                          call_node->as.call.args[0]->column,
                          "string.contains() argument must be string, got '%s'", type_name(arg));
            return NULL;
        }
        return type_bool();
    }

    /* s.starts_with(string prefix) -> bool */
    if (strcmp(method, "starts_with") == 0)
    {
        if (argc != 1)
        {
            checker_error(c, call_node->line, call_node->column,
                          "string.starts_with() takes 1 argument, got %d", argc);
            return NULL;
        }
        Type *arg = check_expr(c, call_node->as.call.args[0]);
        if (arg && arg->kind != TYPE_STRING)
        {
            checker_error(c, call_node->as.call.args[0]->line,
                          call_node->as.call.args[0]->column,
                          "string.starts_with() argument must be string, got '%s'",
                          type_name(arg));
            return NULL;
        }
        return type_bool();
    }

    /* s.ends_with(string suffix) -> bool */
    if (strcmp(method, "ends_with") == 0)
    {
        if (argc != 1)
        {
            checker_error(c, call_node->line, call_node->column,
                          "string.ends_with() takes 1 argument, got %d", argc);
            return NULL;
        }
        Type *arg = check_expr(c, call_node->as.call.args[0]);
        if (arg && arg->kind != TYPE_STRING)
        {
            checker_error(c, call_node->as.call.args[0]->line,
                          call_node->as.call.args[0]->column,
                          "string.ends_with() argument must be string, got '%s'",
                          type_name(arg));
            return NULL;
        }
        return type_bool();
    }

    /* s.compare(string other) -> int */
    if (strcmp(method, "compare") == 0)
    {
        if (argc != 1)
        {
            checker_error(c, call_node->line, call_node->column,
                          "string.compare() takes 1 argument, got %d", argc);
            return NULL;
        }
        Type *arg = check_expr(c, call_node->as.call.args[0]);
        if (arg && arg->kind != TYPE_STRING)
        {
            checker_error(c, call_node->as.call.args[0]->line,
                          call_node->as.call.args[0]->column,
                          "string.compare() argument must be string, got '%s'",
                          type_name(arg));
            return NULL;
        }
        return type_int();
    }

    /* ---- Batch 2: methods that allocate new strings ---- */

    /* s.upper() -> string */
    if (strcmp(method, "upper") == 0)
    {
        if (argc != 0)
        {
            checker_error(c, call_node->line, call_node->column,
                          "string.upper() takes no arguments, got %d", argc);
            return NULL;
        }
        return type_string();
    }

    /* s.lower() -> string */
    if (strcmp(method, "lower") == 0)
    {
        if (argc != 0)
        {
            checker_error(c, call_node->line, call_node->column,
                          "string.lower() takes no arguments, got %d", argc);
            return NULL;
        }
        return type_string();
    }

    /* s.substr(int start[, int len]) -> string */
    if (strcmp(method, "substr") == 0)
    {
        if (argc < 1 || argc > 2)
        {
            checker_error(c, call_node->line, call_node->column,
                          "string.substr() takes 1 or 2 arguments, got %d", argc);
            return NULL;
        }
        Type *arg0 = check_expr(c, call_node->as.call.args[0]);
        if (arg0 && !type_is_integer(arg0))
        {
            checker_error(c, call_node->as.call.args[0]->line,
                          call_node->as.call.args[0]->column,
                          "string.substr() start must be integer, got '%s'",
                          type_name(arg0));
            return NULL;
        }
        if (argc == 2)
        {
            Type *arg1 = check_expr(c, call_node->as.call.args[1]);
            if (arg1 && !type_is_integer(arg1))
            {
                checker_error(c, call_node->as.call.args[1]->line,
                              call_node->as.call.args[1]->column,
                              "string.substr() length must be integer, got '%s'",
                              type_name(arg1));
                return NULL;
            }
        }
        return type_string();
    }

    /* s.trim() -> string */
    if (strcmp(method, "trim") == 0)
    {
        if (argc != 0)
        {
            checker_error(c, call_node->line, call_node->column,
                          "string.trim() takes no arguments, got %d", argc);
            return NULL;
        }
        return type_string();
    }

    /* s.copy() -> string */
    if (strcmp(method, "copy") == 0)
    {
        if (argc != 0)
        {
            checker_error(c, call_node->line, call_node->column,
                          "string.copy() takes no arguments, got %d", argc);
            return NULL;
        }
        return type_string();
    }

    /* s.replace(string old, string new) -> string */
    if (strcmp(method, "replace") == 0)
    {
        if (argc != 2)
        {
            checker_error(c, call_node->line, call_node->column,
                          "string.replace() takes 2 arguments, got %d", argc);
            return NULL;
        }
        Type *arg0 = check_expr(c, call_node->as.call.args[0]);
        if (arg0 && arg0->kind != TYPE_STRING)
        {
            checker_error(c, call_node->as.call.args[0]->line,
                          call_node->as.call.args[0]->column,
                          "string.replace() first argument must be string, got '%s'",
                          type_name(arg0));
            return NULL;
        }
        Type *arg1 = check_expr(c, call_node->as.call.args[1]);
        if (arg1 && arg1->kind != TYPE_STRING)
        {
            checker_error(c, call_node->as.call.args[1]->line,
                          call_node->as.call.args[1]->column,
                          "string.replace() second argument must be string, got '%s'",
                          type_name(arg1));
            return NULL;
        }
        return type_string();
    }

    /* ---- Batch 3: rfind / count / split / join ---- */

    /* s.rfind(string sub) -> int: last occurrence index, or -1 */
    if (strcmp(method, "rfind") == 0)
    {
        if (argc != 1)
        {
            checker_error(c, call_node->line, call_node->column,
                          "string.rfind() takes 1 argument, got %d", argc);
            return NULL;
        }
        Type *arg = check_expr(c, call_node->as.call.args[0]);
        if (arg && arg->kind != TYPE_STRING)
        {
            checker_error(c, call_node->as.call.args[0]->line,
                          call_node->as.call.args[0]->column,
                          "string.rfind() argument must be string, got '%s'",
                          type_name(arg));
            return NULL;
        }
        return type_int();
    }

    /* s.count(string sub) -> int: number of non-overlapping occurrences */
    if (strcmp(method, "count") == 0)
    {
        if (argc != 1)
        {
            checker_error(c, call_node->line, call_node->column,
                          "string.count() takes 1 argument, got %d", argc);
            return NULL;
        }
        Type *arg = check_expr(c, call_node->as.call.args[0]);
        if (arg && arg->kind != TYPE_STRING)
        {
            checker_error(c, call_node->as.call.args[0]->line,
                          call_node->as.call.args[0]->column,
                          "string.count() argument must be string, got '%s'",
                          type_name(arg));
            return NULL;
        }
        return type_int();
    }

    /* s.split(string sep) -> vec(string) */
    /* Phase 2.5: split / join / lines / chars were moved out of the compiler
       into pure-LS `impl string` (std/string.ls), returning Vec(T). They are no
       longer builtin — calls fall through to the user impl lookup. */

    /* s.to_int() -> Result(int, string) */
    if (strcmp(method, "to_int") == 0)
    {
        if (argc != 0)
        {
            checker_error(c, call_node->line, call_node->column,
                          "string.to_int() takes no arguments, got %d", argc);
            return NULL;
        }
        return checker_instantiate_result(c, type_int(), type_string());
    }

    /* s.to_i64() -> Result(i64, string) */
    if (strcmp(method, "to_i64") == 0)
    {
        if (argc != 0)
        {
            checker_error(c, call_node->line, call_node->column,
                          "string.to_i64() takes no arguments, got %d", argc);
            return NULL;
        }
        return checker_instantiate_result(c, type_i64(), type_string());
    }

    /* s.to_float() -> Result(f64, string) */
    if (strcmp(method, "to_float") == 0)
    {
        if (argc != 0)
        {
            checker_error(c, call_node->line, call_node->column,
                          "string.to_float() takes no arguments, got %d", argc);
            return NULL;
        }
        return checker_instantiate_result(c, type_f64(), type_string());
    }

    /* s.to_bool() -> Result(bool, string) */
    if (strcmp(method, "to_bool") == 0)
    {
        if (argc != 0)
        {
            checker_error(c, call_node->line, call_node->column,
                          "string.to_bool() takes no arguments, got %d", argc);
            return NULL;
        }
        return checker_instantiate_result(c, type_bool(), type_string());
    }

    /* s.repeat(int n) -> string */
    if (strcmp(method, "repeat") == 0)
    {
        if (argc != 1)
        {
            checker_error(c, call_node->line, call_node->column,
                          "string.repeat() takes 1 argument (count), got %d", argc);
            return NULL;
        }
        Type *n_type = check_expr(c, call_node->as.call.args[0]);
        if (n_type && !type_is_integer(n_type))
            checker_error(c, call_node->line, call_node->column,
                          "string.repeat() count must be an integer");
        return type_string();
    }

    /* s.pad_left(int width, int fill_char) -> string */
    /* s.pad_right(int width, int fill_char) -> string */
    if (strcmp(method, "pad_left") == 0 || strcmp(method, "pad_right") == 0)
    {
        if (argc != 2)
        {
            checker_error(c, call_node->line, call_node->column,
                          "string.%s() takes 2 arguments (width, fill_char), got %d",
                          method, argc);
            return NULL;
        }
        Type *w = check_expr(c, call_node->as.call.args[0]);
        if (w && !type_is_integer(w))
            checker_error(c, call_node->line, call_node->column,
                          "string.%s() width must be an integer", method);
        Type *ch = check_expr(c, call_node->as.call.args[1]);
        if (ch && ch->kind != TYPE_CHAR && !type_is_integer(ch))
            checker_error(c, call_node->line, call_node->column,
                          "string.%s() fill_char must be char or integer", method);
        return type_string();
    }

    /* Phase 2.5: not a builtin string method — signal the caller to try a
       user-defined `impl string` method (Step 11) before reporting an error. */
    c->string_no_builtin_match = true;
    return NULL;
}

/* Check builtin function calls that don't belong to a type */
static Type *check_builtin_call(Checker *c, const char *name, AstNode *call_node)
{
    int argc = call_node->as.call.arg_count;
    AstNode **args = call_node->as.call.args;

    /* to_string(int/f64/etc) -> string */
    if (strcmp(name, "to_string") == 0)
    {
        if (argc != 1)
        {
            checker_error(c, call_node->line, call_node->column,
                          "to_string() takes 1 argument, got %d", argc);
            return NULL;
        }
        Type *arg_type = check_expr(c, args[0]);
        if (arg_type == NULL)
            return NULL;
        if (!type_is_numeric(arg_type) && arg_type->kind != TYPE_BOOL)
        {
            checker_error(c, args[0]->line, args[0]->column,
                          "to_string() requires numeric or bool type, got '%s'",
                          type_name(arg_type));
            return NULL;
        }
        return type_string();
    }

    /* from_int(string) -> int: parse string as integer */
    if (strcmp(name, "from_int") == 0)
    {
        if (argc != 1)
        {
            checker_error(c, call_node->line, call_node->column,
                          "from_int() takes 1 argument, got %d", argc);
            return NULL;
        }
        Type *arg_type = check_expr(c, args[0]);
        if (arg_type == NULL)
            return NULL;
        if (arg_type->kind != TYPE_STRING)
        {
            checker_error(c, args[0]->line, args[0]->column,
                          "from_int() requires string type, got '%s'",
                          type_name(arg_type));
            return NULL;
        }
        return type_int();
    }

    /* from_float(string) -> f64: parse string as float */
    if (strcmp(name, "from_float") == 0)
    {
        if (argc != 1)
        {
            checker_error(c, call_node->line, call_node->column,
                          "from_float() takes 1 argument, got %d", argc);
            return NULL;
        }
        Type *arg_type = check_expr(c, args[0]);
        if (arg_type == NULL)
            return NULL;
        if (arg_type->kind != TYPE_STRING)
        {
            checker_error(c, args[0]->line, args[0]->column,
                          "from_float() requires string type, got '%s'",
                          type_name(arg_type));
            return NULL;
        }
        return type_f64();
    }

    /* Phase E.3.1: errno() -> int  — read C runtime errno (thread-local).
       On Windows uses _errno(), on POSIX uses __errno_location(). The codegen
       emits the platform-specific dereference inline. */
    if (strcmp(name, "errno") == 0)
    {
        if (argc != 0)
        {
            checker_error(c, call_node->line, call_node->column,
                          "errno() takes no arguments, got %d", argc);
            return NULL;
        }
        return type_int();
    }

    /* __string_take_buffer(*u8 ptr, i64 len) -> string
       Wraps a malloc'd buffer in an LsString, transferring ownership. Zero-copy.
       INTERNAL: only safe when `ptr` was allocated via the LS-visible malloc
       (so memcheck wrappers see the eventual free) and the allocation is
       at least len+1 bytes. Used by stdlib io/fs to avoid the calloc+strlen+
       memcpy round-trip in from_cstr. Not for end-user code. */
    if (strcmp(name, "__string_take_buffer") == 0)
    {
        if (!path_is_under_stdlib(c->source_path))
        {
            checker_error(c, call_node->line, call_node->column,
                          "__string_take_buffer() is internal: callable only from stdlib/ files");
            return NULL;
        }
        if (argc != 2)
        {
            checker_error(c, call_node->line, call_node->column,
                          "__string_take_buffer() takes 2 arguments, got %d", argc);
            return NULL;
        }
        Type *p_type = check_expr(c, args[0]);
        Type *l_type = check_expr(c, args[1]);
        if (p_type == NULL || l_type == NULL) return NULL;
        if (p_type->kind != TYPE_OBJECT && p_type->kind != TYPE_POINTER &&
            p_type->kind != TYPE_NIL)
        {
            checker_error(c, args[0]->line, args[0]->column,
                          "__string_take_buffer() arg 1 requires *T/object, got '%s'",
                          type_name(p_type));
            return NULL;
        }
        if (!type_is_integer(l_type))
        {
            checker_error(c, args[1]->line, args[1]->column,
                          "__string_take_buffer() arg 2 requires integer length, got '%s'",
                          type_name(l_type));
            return NULL;
        }
        return type_string();
    }

    /* Phase E.3.3: from_cstr(object) -> string
       Copies a C-style NUL-terminated char* (received via FFI as `object`)
       into a managed LsString. Critical glue for getenv/strerror/readdir. */
    if (strcmp(name, "from_cstr") == 0)
    {
        if (argc != 1)
        {
            checker_error(c, call_node->line, call_node->column,
                          "from_cstr() takes 1 argument, got %d", argc);
            return NULL;
        }
        Type *arg_type = check_expr(c, args[0]);
        if (arg_type == NULL) return NULL;
        if (arg_type->kind != TYPE_OBJECT && arg_type->kind != TYPE_POINTER &&
            arg_type->kind != TYPE_NIL)
        {
            checker_error(c, args[0]->line, args[0]->column,
                          "from_cstr() requires object/pointer type, got '%s'",
                          type_name(arg_type));
            return NULL;
        }
        return type_string();
    }

    /* __move(var) -> T  — explicit move annotation.
       Marks the argument variable as MOVED and returns its type transparently.
       Works on any movable type; also force-moves static strings (unlike implicit moves). */
    if (strcmp(name, "__move") == 0)
    {
        if (argc != 1)
        {
            checker_error(c, call_node->line, call_node->column,
                          "__move() takes exactly 1 argument, got %d", argc);
            return NULL;
        }
        AstNode *arg = args[0];
        if (arg->kind != AST_IDENT)
        {
            checker_error(c, arg->line, arg->column,
                          "__move() requires a variable identifier, not an expression");
            /* Still type-check for error recovery */
            return check_expr(c, arg);
        }
        Type *arg_type = check_expr(c, arg); /* also reports use-of-moved if already moved */
        if (!arg_type) return NULL;
        Symbol *sym = scope_resolve(c->current_scope, arg->as.ident.name);
        if (sym)
        {
            if (sym->is_moved || sym->is_maybe_moved)
            {
                /* Already moved/maybe-moved — check_expr already reported; no double-report needed */
            }
            else if (sym->is_borrow)
            {
                /* Phase 5: __move() cannot transfer ownership of a borrow — it holds none. */
                checker_move_error(c, arg->line, arg->column,
                                   "cannot __move(): variable '%s' is a read-only borrow",
                                   arg->as.ident.name);
            }
            else if (sym->is_mut_borrow)
            {
                /* Phase 5.5: writable borrow can mutate but not transfer ownership. */
                checker_move_error(c, arg->line, arg->column,
                                   "cannot __move(): variable '%s' is a writable borrow "
                                   "(mutation allowed, but ownership cannot leave)",
                                   arg->as.ident.name);
            }
            else if (type_is_movable(sym->type))
            {
                /* Force-mark as moved, even for static strings */
                sym->is_moved = true;
                /* Move-elision (Q4): explicit __move(x) transfers ownership; let
                   codegen move instead of clone. Tag the inner IDENT (the value
                   that flows into the dst is __move(x), but codegen inspects the
                   unwrapped source via ast_unwrap_move). */
                arg->moved_out = true;
            }
            else
            {
                checker_error(c, arg->line, arg->column,
                              "__move() applied to non-movable type '%s'; "
                              "only string, vec, map, and struct-with-drop can be moved",
                              type_name(arg_type));
            }
        }
        return arg_type; /* transparent: __move(s) has the same type as s */
    }

    /* __drop_at(place) -> void — run the recursive destructor on the value at an
       lvalue place (e.g. a raw pointer slot p[i]). POD is a no-op. Lets a
       self-managed container (RawVec) drop owned elements in __drop/set/clear
       WITHOUT freeing the backing buffer. The slot is left logically dead;
       liveness is the container's responsibility (its `len` bound). */
    if (strcmp(name, "__drop_at") == 0)
    {
        if (argc != 1)
        {
            checker_error(c, call_node->line, call_node->column,
                          "__drop_at() takes exactly 1 argument, got %d", argc);
            return NULL;
        }
        Type *arg_type = check_expr(c, args[0]);
        if (arg_type == NULL) return NULL;
        if (args[0]->kind != AST_INDEX && args[0]->kind != AST_FIELD &&
            args[0]->kind != AST_IDENT &&
            !(args[0]->kind == AST_UNARY && args[0]->as.unary.op == TOKEN_STAR))
        {
            checker_error(c, args[0]->line, args[0]->column,
                          "__drop_at() requires a place expression (p[i], field, or *p)");
            return NULL;
        }
        return type_void();
    }

    /* __take(place) -> T — move-OUT of an lvalue slot: bit-read the value WITHOUT
       cloning; the caller takes ownership and the slot is logically vacated (the
       container must drop its `len`/track liveness). The move-out counterpart of
       `__drop_at`; used by RawVec.pop / remove / insert / swap to relocate elements
       without a clone. Returns the element (pointee) type. */
    if (strcmp(name, "__take") == 0)
    {
        if (argc != 1)
        {
            checker_error(c, call_node->line, call_node->column,
                          "__take() takes exactly 1 argument, got %d", argc);
            return NULL;
        }
        Type *arg_type = check_expr(c, args[0]);
        if (arg_type == NULL) return NULL;
        if (args[0]->kind != AST_INDEX && args[0]->kind != AST_FIELD &&
            args[0]->kind != AST_IDENT &&
            !(args[0]->kind == AST_UNARY && args[0]->as.unary.op == TOKEN_STAR))
        {
            checker_error(c, args[0]->line, args[0]->column,
                          "__take() requires a place expression (p[i], field, or *p)");
            return NULL;
        }
        return arg_type; /* the element type read out of the slot */
    }

    return NULL;
}

/* Check if a name is a builtin function (so we don't report "undefined variable") */
static bool is_builtin_function(const char *name)
{
    return strcmp(name, "to_string") == 0 ||
           strcmp(name, "from_int") == 0 ||
           strcmp(name, "from_float") == 0 ||
           strcmp(name, "from_cstr") == 0 ||
           strcmp(name, "__string_take_buffer") == 0 ||
           strcmp(name, "errno") == 0 ||
           strcmp(name, "__move") == 0 ||
           strcmp(name, "__drop_at") == 0 ||
           strcmp(name, "__take") == 0;
}

/* ---- Phase C closure capture analysis ----
   Walks an AST_CLOSURE body to collect free variables (names referenced in
   the body that aren't bound by the closure's params or by inner local
   declarations). Each free variable that resolves to a symbol in the outer
   scope is recorded as a capture on the closure node. POD-only in v1 —
   non-POD captures (string/vec/map/struct/enum) are rejected with a
   "not yet implemented" diagnostic so users get a clean message instead of
   silent corruption.

   `bound[]` tracks names currently in scope WITHIN the closure body. We
   pre-populate it with the closure's parameter names. Block boundaries
   snapshot/restore the bound count so var decls don't leak across siblings.
*/
typedef struct {
    /* Borrowed name pointers — owned by the AST itself. */
    const char **bound;
    int bound_count;
    int bound_cap;

    /* Output list (deep-owned names). Must match AstClosureNode.captures
       layout exactly — the pointer is transferred directly to the AST node. */
    struct {
        char *name;
        Type *type;
        bool  is_explicit_move;  /* F.1: set after capture_walk from move_names */
    } *captures;
    int capture_count;
    int capture_cap;

    Checker *c;
    Scope   *outer_scope;
    AstNode *closure_node;
    bool     had_error;
} CaptureScan;

static bool cap_is_bound(CaptureScan *s, const char *name) {
    for (int i = 0; i < s->bound_count; i++)
        if (strcmp(s->bound[i], name) == 0) return true;
    return false;
}

static bool cap_already(CaptureScan *s, const char *name) {
    for (int i = 0; i < s->capture_count; i++)
        if (strcmp(s->captures[i].name, name) == 0) return true;
    return false;
}

static void cap_push_bound(CaptureScan *s, const char *name) {
    if (name == NULL) return;
    if (s->bound_count >= s->bound_cap) {
        s->bound_cap = GROW_CAPACITY(s->bound_cap);
        s->bound = (const char **)realloc_safe(
            (void*)s->bound, sizeof(const char *) * (size_t)s->bound_cap);
    }
    s->bound[s->bound_count++] = name;
}

/* POD types eligible for by-copy capture (no drop needed). */
static bool capture_type_is_pod(const Type *t) {
    if (t == NULL) return false;
    switch (t->kind) {
    case TYPE_INT: case TYPE_I8: case TYPE_I16: case TYPE_I32: case TYPE_I64:
    case TYPE_U8:  case TYPE_U16: case TYPE_U32: case TYPE_U64:
    case TYPE_F32: case TYPE_F64:
    case TYPE_BOOL: case TYPE_CHAR:
    case TYPE_OBJECT: case TYPE_POINTER:
        return true;
    default:
        return false;
    }
}

/* Phase C.5/C.7+: by-move capture types — env owns the value, outer is
   marked moved.
     C.5: TYPE_STRING
     C.7: TYPE_STRUCT(has_drop)
   Enum captures remain unsupported (Phase C.8 — needs box / payload
   walk inside env_drop). */
static bool capture_type_is_by_move(const Type *t) {
    if (t == NULL) return false;
    switch (t->kind) {
    case TYPE_STRING: return true;
    case TYPE_STRUCT: return t->as.strukt.has_drop;
    case TYPE_ENUM:   return t->as.enom.has_drop;   /* F.5: has_drop enum → by-move */
    default:          return false;
    }
}

/* array(T, N) with POD element type is captured by value (full copy into env).
   String-element or struct-element arrays would need deep clone — reject for now. */
static bool capture_type_is_pod_array(const Type *t) {
    if (t == NULL || t->kind != TYPE_ARRAY) return false;
    return capture_type_is_pod(t->as.array.elem);
}

static bool capture_type_supported(const Type *t) {
    if (t == NULL) return false;
    /* F.5: non-has_drop enum (disc-only or POD payloads) is by-copy — no drop. */
    if (t->kind == TYPE_ENUM && !t->as.enom.has_drop) return true;
    return capture_type_is_pod(t) ||
           capture_type_is_pod_array(t) ||
           capture_type_is_by_move(t);
}

static void cap_record(CaptureScan *s, AstNode *site, const char *name, Type *t) {
    if (cap_already(s, name)) return;
    if (!capture_type_supported(t)) {
        checker_error(s->c, site->line, site->column,
                      "capturing variable '%s' of type '%s' in a closure is "
                      "not yet implemented (supported: POD types, "
                      "array(POD,N) (by-copy), string (by-move), "
                      "vec(T)/map(K,V) (by-ref), struct(has_drop) (by-move), "
                      "enum (by-copy or by-move depending on has_drop))",
                      name, type_name(t));
        s->had_error = true;
        return;
    }
    /* Phase C.5: by-move capture (string in v1) transfers ownership from
       the outer variable into the closure's env. Run the same outer-symbol
       checks vec.push uses, then mark the outer as moved so subsequent
       outer uses are caught at compile time. Borrows are silently rejected
       (closure cannot promise a borrow lives long enough yet). */
    if (capture_type_is_by_move(t)) {
        Symbol *outer = scope_resolve(s->outer_scope, name);
        if (outer == NULL) {
            /* Defensive — capture_walk only records if outer-scope hit.   */
            return;
        }
        if (outer->is_borrow || outer->is_mut_borrow) {
            checker_move_error(s->c, site->line, site->column,
                "cannot capture '%s' by-move into a closure: it is a "
                "borrow parameter (no transferable ownership)", name);
            s->had_error = true;
            return;
        }
        if (outer->is_moved || outer->is_maybe_moved) {
            checker_move_error(s->c, site->line, site->column,
                "cannot capture '%s' by-move: already moved on a prior path",
                name);
            s->had_error = true;
            return;
        }
        /* Static strings have no heap ownership and remain freely shareable
           (cap==0 at runtime); env's drop wrapper safely no-ops on them. */
        if (!(t->kind == TYPE_STRING && outer->is_static_string)) {
            outer->is_moved = true;
        }
    }
    if (s->capture_count >= s->capture_cap) {
        s->capture_cap = GROW_CAPACITY(s->capture_cap);
        s->captures = realloc_safe(s->captures,
            sizeof(s->captures[0]) * (size_t)s->capture_cap);
    }
    /* deep-copy name so AST ownership is self-contained */
    size_t nl = strlen(name);
    char *dup = (char*)malloc_safe(nl + 1);
    memcpy(dup, name, nl + 1);
    s->captures[s->capture_count].name = dup;
    s->captures[s->capture_count].type = t;
    s->captures[s->capture_count].is_explicit_move = false;
    s->capture_count++;
}

static void capture_walk(CaptureScan *s, AstNode *node);

static void capture_walk_arms(CaptureScan *s, AstNode *node) {
    /* MATCH arms each introduce a fresh binding scope. */
    int n = node->as.match.arm_count;
    capture_walk(s, node->as.match.subject);
    for (int i = 0; i < n; i++) {
        int saved = s->bound_count;
        /* Pattern may bind names (variant constructors with payloads).
           For POD-only v1 we conservatively treat AST_IDENT in pattern
           position as a binding. */
        AstNode *pat = node->as.match.arms[i].pattern;
        if (pat && pat->kind == AST_IDENT) {
            cap_push_bound(s, pat->as.ident.name);
        } else if (pat && pat->kind == AST_CALL) {
            /* Variant ctor with payload binders: each arg ident → binding. */
            for (int k = 0; k < pat->as.call.arg_count; k++) {
                AstNode *a = pat->as.call.args[k];
                if (a && a->kind == AST_IDENT)
                    cap_push_bound(s, a->as.ident.name);
            }
        }
        capture_walk(s, node->as.match.arms[i].body);
        s->bound_count = saved;
    }
}

static void capture_walk(CaptureScan *s, AstNode *node) {
    if (node == NULL || s->had_error) return;
    switch (node->kind) {
    case AST_INT_LIT: case AST_FLOAT_LIT: case AST_STRING_LIT:
    case AST_BOOL_LIT: case AST_NIL_LIT: case AST_BREAK: case AST_CONTINUE:
        return;
    case AST_IDENT: {
        const char *name = node->as.ident.name;
        if (cap_is_bound(s, name)) return;
        Symbol *sym = scope_resolve(s->outer_scope, name);
        if (sym == NULL) return;  /* will error in normal type-check pass */
        /* Global functions (TYPE_FUNCTION) are never captured in the env —
           they are accessed directly by the lifted closure body just like
           any top-level name in a normal function. */
        if (sym->type && sym->type->kind == TYPE_FUNCTION) return;
        cap_record(s, node, name, sym->type);
        return;
    }
    case AST_UNARY:
        capture_walk(s, node->as.unary.operand);
        return;
    case AST_MUT_BORROW:
        capture_walk(s, node->as.mut_borrow.operand);
        return;
    case AST_BINARY:
        capture_walk(s, node->as.binary.left);
        capture_walk(s, node->as.binary.right);
        return;
    case AST_CALL:
        capture_walk(s, node->as.call.callee);
        for (int i = 0; i < node->as.call.arg_count; i++)
            capture_walk(s, node->as.call.args[i]);
        return;
    case AST_INDEX:
        capture_walk(s, node->as.index_expr.object);
        capture_walk(s, node->as.index_expr.index);
        return;
    case AST_FIELD:
        capture_walk(s, node->as.field_access.object);
        return;
    case AST_FORMAT_STRING:
        for (int i = 0; i < node->as.format_string.expr_count; i++)
            capture_walk(s, node->as.format_string.exprs[i]);
        return;
    case AST_ARRAY_LIT:
        for (int i = 0; i < node->as.array_lit.count; i++)
            capture_walk(s, node->as.array_lit.elements[i]);
        return;
    case AST_MAP_LIT:
        for (int i = 0; i < node->as.map_lit.pair_count; i++) {
            capture_walk(s, node->as.map_lit.keys[i]);
            capture_walk(s, node->as.map_lit.vals[i]);
        }
        return;
    case AST_CAST:
        capture_walk(s, node->as.cast.expr);
        return;
    case AST_SIZEOF:
        return; /* operand is a type, no sub-expression to walk */
    case AST_RANGE:
        capture_walk(s, node->as.range.start);
        capture_walk(s, node->as.range.end);
        return;
    case AST_TRY:
        capture_walk(s, node->as.try_expr.expr);
        return;
    case AST_FORCE_UNWRAP:
        capture_walk(s, node->as.force_unwrap.expr);
        capture_walk(s, node->as.force_unwrap.message);  /* C1: expect(msg) */
        return;
    case AST_AT_TIME:
        capture_walk(s, node->as.at_time.expr);
        return;
    case AST_AT_BENCH:
        capture_walk(s, node->as.at_bench.expr);
        return;
    case AST_NEW_EXPR:
        for (int i = 0; i < node->as.new_expr.field_init_count; i++)
            capture_walk(s, node->as.new_expr.field_inits[i].value);
        return;
    case AST_VAR_DECL:
        capture_walk(s, node->as.var_decl.init);
        cap_push_bound(s, node->as.var_decl.name);
        return;
    case AST_ASSIGN:
        capture_walk(s, node->as.assign.target);
        capture_walk(s, node->as.assign.value);
        return;
    case AST_RETURN:
        capture_walk(s, node->as.return_stmt.value);
        return;
    case AST_IF: {
        capture_walk(s, node->as.if_stmt.cond);
        int sv = s->bound_count;
        capture_walk(s, node->as.if_stmt.then_block);
        s->bound_count = sv;
        capture_walk(s, node->as.if_stmt.else_block);
        s->bound_count = sv;
        return;
    }
    case AST_WHILE: {
        capture_walk(s, node->as.while_stmt.cond);
        int sv = s->bound_count;
        capture_walk(s, node->as.while_stmt.body);
        s->bound_count = sv;
        return;
    }
    case AST_FOR: {
        capture_walk(s, node->as.for_stmt.iter);
        int sv = s->bound_count;
        cap_push_bound(s, node->as.for_stmt.var);
        capture_walk(s, node->as.for_stmt.body);
        s->bound_count = sv;
        return;
    }
    case AST_FOR_C: {
        int sv = s->bound_count;
        capture_walk(s, node->as.for_c_stmt.init);  /* may add a var */
        capture_walk(s, node->as.for_c_stmt.cond);
        capture_walk(s, node->as.for_c_stmt.update);
        capture_walk(s, node->as.for_c_stmt.body);
        s->bound_count = sv;
        return;
    }
    case AST_BLOCK: {
        int sv = s->bound_count;
        for (int i = 0; i < node->as.block.stmt_count; i++)
            capture_walk(s, node->as.block.stmts[i]);
        s->bound_count = sv;
        return;
    }
    case AST_EXPR_STMT:
        capture_walk(s, node->as.expr_stmt.expr);
        return;
    case AST_MATCH:
        capture_walk_arms(s, node);
        return;
    case AST_CLOSURE:
        /* Phase C v1: nested closures are not supported — they would need
           transitive capture propagation. */
        checker_error(s->c, node->line, node->column,
                      "nested closure literals are not yet supported "
                      "(Phase C v1 limitation)");
        s->had_error = true;
        return;
    default:
        /* Decls / FFI / module nodes — should not appear inside closure body
           in well-formed input; just skip. */
        return;
    }
}

/* ---- Expression checking ---- */

/* A-1 (docs/plan_runtime_primitives.md): match a canonical-path call to a
   std.c memory/process primitive — literally `std.c.malloc/realloc/free/abort`.
   These four are reachable by FULL canonical path (not a local alias) so they
   resolve from anywhere, including generic method bodies (std.vec/std.map) that
   are re-checked at the consumer site where no `import std.c` alias is in scope.
   The compiler recognises the spelling and lowers to the same CRT/runtime call
   the bare builtins used to emit — see codegen cg_match_stdc_prim.
   Returns: 0=malloc 1=realloc 2=free 3=abort, or -1 if the callee is not one.
   Guards on `std` not being a local symbol so a user struct field chain
   `mystd.c.malloc` (mystd a variable) is left to normal field handling. */
static int match_stdc_prim(Checker *c, AstNode *callee)
{
    if (callee == NULL || callee->kind != AST_FIELD)
        return -1;
    AstNode *mid = callee->as.field_access.object; /* expect `std.c` */
    if (mid == NULL || mid->kind != AST_FIELD)
        return -1;
    AstNode *head = mid->as.field_access.object;   /* expect `std` */
    if (head == NULL || head->kind != AST_IDENT)
        return -1;
    if (strcmp(head->as.ident.name, "std") != 0)
        return -1;
    if (strcmp(mid->as.field_access.field, "c") != 0)
        return -1;
    if (c != NULL && scope_resolve(c->current_scope, "std") != NULL)
        return -1; /* `std` is a local symbol → not the std.c module path */
    const char *f = callee->as.field_access.field;
    if (strcmp(f, "malloc") == 0)  return 0;
    if (strcmp(f, "realloc") == 0) return 1;
    if (strcmp(f, "free") == 0)    return 2;
    if (strcmp(f, "abort") == 0)   return 3;
    return -1;
}

/* ---- C1: Option/Result combinators (docs/plan_container_access_safety.md §5.3) ----
   unwrap / expect / unwrap_or / is_some? / is_none? / is_ok? / is_err? are lowered
   by the compiler, mirroring `try` and force-unwrap `!` (which are also not library
   methods). `impl` on the builtin Option/Result enum templates is unsupported, and
   generic free functions would need explicit type args at each call site — so the
   checker intercepts `opt.METHOD(args)` and rewrites the AST_CALL in place to either
   a force-unwrap (the two panic combinators) or a 2-arm match expression (the rest),
   then re-checks. This reuses the mature match drop/move machinery and the
   force-unwrap discriminant lowering, introducing NO new ownership code. */
typedef enum {
    OPTC_NONE = 0,
    OPTC_UNWRAP, OPTC_EXPECT,    /* panic → force-unwrap (expect carries a message) */
    OPTC_UNWRAP_OR,             /* match desugar, 1 fallback arg */
    OPTC_IS_SOME, OPTC_IS_NONE, /* match desugar, Option only, → bool */
    OPTC_IS_OK,   OPTC_IS_ERR,  /* match desugar, Result only, → bool */
    /* C2a: Option↔Result conversions (match desugar; result type is built so the
       bare variant ctors in the arm bodies resolve regardless of call context). */
    OPTC_OK,      OPTC_ERR,     /* Result → Option(T) / Option(E) */
    OPTC_OK_OR                  /* Option → Result(T, E), 1 error-value arg */
} OptCombinator;

static OptCombinator opt_combinator_id(const char *name)
{
    if (strcmp(name, "unwrap")    == 0) return OPTC_UNWRAP;
    if (strcmp(name, "expect")    == 0) return OPTC_EXPECT;
    if (strcmp(name, "unwrap_or") == 0) return OPTC_UNWRAP_OR;
    if (strcmp(name, "is_some?")  == 0) return OPTC_IS_SOME;
    if (strcmp(name, "is_none?")  == 0) return OPTC_IS_NONE;
    if (strcmp(name, "is_ok?")    == 0) return OPTC_IS_OK;
    if (strcmp(name, "is_err?")   == 0) return OPTC_IS_ERR;
    if (strcmp(name, "ok")        == 0) return OPTC_OK;
    if (strcmp(name, "err")       == 0) return OPTC_ERR;
    if (strcmp(name, "ok_or")     == 0) return OPTC_OK_OR;
    return OPTC_NONE;
}

static int g_optc_uid = 0;

static AstNode *optc_mk_ident(const char *name, int line, int col)
{
    AstNode *n = ast_new(AST_IDENT, line, col);
    n->as.ident.name = chk_strdup(name);
    return n;
}
static AstNode *optc_mk_bool(bool v, int line, int col)
{
    AstNode *n = ast_new(AST_BOOL_LIT, line, col);
    n->as.bool_lit.value = v;
    return n;
}
/* variant pattern `Variant(binding)` — binding "_" yields a wildcard payload.
   The same AST_CALL shape doubles as a variant constructor expression. */
static AstNode *optc_mk_variant_pat(const char *variant, const char *binding,
                                    int line, int col)
{
    AstNode *call = ast_new(AST_CALL, line, col);
    call->as.call.callee = optc_mk_ident(variant, line, col);
    call->as.call.args = (AstNode **)malloc_safe(sizeof(AstNode *));
    call->as.call.args[0] = optc_mk_ident(binding, line, col);
    call->as.call.arg_count = 1;
    call->as.call.type_args = NULL;
    call->as.call.type_arg_count = 0;
    return call;
}
/* variant constructor `Variant(arg)` where arg is an arbitrary expression node. */
static AstNode *optc_mk_variant_call(const char *variant, AstNode *arg,
                                     int line, int col)
{
    AstNode *call = ast_new(AST_CALL, line, col);
    call->as.call.callee = optc_mk_ident(variant, line, col);
    call->as.call.args = (AstNode **)malloc_safe(sizeof(AstNode *));
    call->as.call.args[0] = arg;
    call->as.call.arg_count = 1;
    call->as.call.type_args = NULL;
    call->as.call.type_arg_count = 0;
    return call;
}
/* First payload type of variant `vname` in enum type `enom`, or NULL. */
static Type *optc_variant_payload(Type *enom, const char *vname)
{
    for (int i = 0; i < enom->as.enom.variant_count; i++)
        if (strcmp(enom->as.enom.variants[i].name, vname) == 0 &&
            enom->as.enom.variants[i].payload_count > 0)
            return enom->as.enom.variants[i].payload_types[0];
    return NULL;
}

/* If enum type `t` carries variant `vname`, return 1 with *out_idx set. */
static int enum_type_has_variant(Type *t, const char *vname, int *out_idx)
{
    if (t == NULL || t->kind != TYPE_ENUM) return 0;
    for (int i = 0; i < t->as.enom.variant_count; i++)
        if (strcmp(t->as.enom.variants[i].name, vname) == 0) {
            *out_idx = i;
            return 1;
        }
    return 0;
}

/* Disambiguate an otherwise-ambiguous bare variant ctor (e.g. `Some`/`None`/`Err`
   matching several Option/Result instantiations) by a type hint: the node's own
   already-resolved enum (idempotent re-check — the same ctor may be visited twice
   when an outer rewrite re-checks it as a match subject), else the expected type.
   Returns 1 with *out_enum/*out_idx set. Only consulted at the ambiguity sites,
   so it never overrides a unique match. */
static int disambig_variant_by_hint(Checker *c, AstNode *node, const char *vname,
                                    Type **out_enum, int *out_idx)
{
    if (node->resolved_type && enum_type_has_variant(node->resolved_type, vname, out_idx)) {
        *out_enum = node->resolved_type;
        return 1;
    }
    if (enum_type_has_variant(c->expected_type, vname, out_idx)) {
        *out_enum = c->expected_type;
        return 1;
    }
    return 0;
}
static AstNode *optc_mk_match2(AstNode *subject,
                               AstNode *pat0, AstNode *body0,
                               AstNode *pat1, AstNode *body1, int line, int col)
{
    AstNode *m = ast_new(AST_MATCH, line, col);
    m->as.match.subject = subject;
    m->as.match.arms = (MatchArm *)malloc_safe(2 * sizeof(MatchArm));
    m->as.match.arms[0].pattern = pat0;
    m->as.match.arms[0].body = body0;
    m->as.match.arms[1].pattern = pat1;
    m->as.match.arms[1].body = body1;
    m->as.match.arm_count = 2;
    return m;
}

/* Rewrite `recv.METHOD(args)` (an AST_CALL `node`) in place into the lowered form
   and re-check it. Returns 1 = rewrote (result type in *out_ty), -1 = error
   reported, 0 = METHOD is not a combinator (caller falls through to normal enum
   dispatch). */
static int lower_opt_combinator(Checker *c, AstNode *node, AstNode *recv,
                                Type *recv_type, const char *method_name,
                                Type **out_ty)
{
    OptCombinator id = opt_combinator_id(method_name);
    if (id == OPTC_NONE) return 0;

    bool is_result = (strncmp(recv_type->as.enom.name, "Result(", 7) == 0);
    bool is_option = (strncmp(recv_type->as.enom.name, "Option(", 7) == 0);
    if (!is_result && !is_option) return 0;   /* defensive: caller guarantees this */

    int line = node->line, col = node->column;
    int argc = node->as.call.arg_count;
    AstNode **args = node->as.call.args;
    AstNode *callee = node->as.call.callee;   /* AST_FIELD shell, discarded below */

    int want_args = (id == OPTC_EXPECT || id == OPTC_UNWRAP_OR ||
                     id == OPTC_OK_OR) ? 1 : 0;
    if (argc != want_args) {
        checker_error(c, line, col, "'%s' expects %d argument(s), got %d",
                      method_name, want_args, argc);
        return -1;
    }
    if ((id == OPTC_IS_SOME || id == OPTC_IS_NONE || id == OPTC_OK_OR) && !is_option) {
        checker_error(c, line, col,
                      "'%s' is an Option combinator, but got '%s'",
                      method_name, type_name(recv_type));
        return -1;
    }
    if ((id == OPTC_IS_OK || id == OPTC_IS_ERR ||
         id == OPTC_OK || id == OPTC_ERR) && !is_result) {
        checker_error(c, line, col,
                      "'%s' is a Result combinator, but got '%s'",
                      method_name, type_name(recv_type));
        return -1;
    }

    AstNode *arg0 = (argc >= 1) ? args[0] : NULL;
    /* Detach recv from the callee shell so freeing the shell won't free recv. */
    callee->as.field_access.object = NULL;

    char vb[32];
    snprintf(vb, sizeof vb, "__ocv$%d", g_optc_uid++);
    const char *succ = is_result ? "Ok" : "Some";
    const char *fail = is_result ? "Err" : "None";

    /* C2a conversion combinators build a fresh Option/Result result type and push
       it as the expected type during re-check, so the bare variant constructors in
       the arm bodies (`Some(x)` / `None` / `Err(e)`) resolve in any call context
       (chained, argument position, …) — not just where a typed LHS supplies it. */
    Type *result_ctx = NULL;

    AstNode *repl = NULL;
    switch (id) {
    case OPTC_UNWRAP:
        repl = ast_new(AST_FORCE_UNWRAP, line, col);
        repl->as.force_unwrap.expr = recv;
        repl->as.force_unwrap.message = NULL;
        break;
    case OPTC_EXPECT:
        repl = ast_new(AST_FORCE_UNWRAP, line, col);
        repl->as.force_unwrap.expr = recv;
        repl->as.force_unwrap.message = arg0;   /* user message expr (moved in) */
        arg0 = NULL;
        break;
    case OPTC_UNWRAP_OR: {
        /* match recv { succ(x) => x   fail(_) => arg0 } — fail payload (Result Err)
           is bound to `_` and dropped by the match arm; arg0 is the fallback. */
        AstNode *succ_pat = optc_mk_variant_pat(succ, vb, line, col);
        AstNode *succ_body = optc_mk_ident(vb, line, col);
        AstNode *fail_pat = is_result ? optc_mk_variant_pat(fail, "_", line, col)
                                      : optc_mk_ident(fail, line, col);
        repl = optc_mk_match2(recv, succ_pat, succ_body, fail_pat, arg0, line, col);
        arg0 = NULL;
        break;
    }
    case OPTC_IS_SOME: case OPTC_IS_NONE: {
        bool some_true = (id == OPTC_IS_SOME);
        repl = optc_mk_match2(recv,
                              optc_mk_variant_pat("Some", "_", line, col),
                              optc_mk_bool(some_true, line, col),
                              optc_mk_ident("None", line, col),
                              optc_mk_bool(!some_true, line, col), line, col);
        break;
    }
    case OPTC_IS_OK: case OPTC_IS_ERR: {
        bool ok_true = (id == OPTC_IS_OK);
        repl = optc_mk_match2(recv,
                              optc_mk_variant_pat("Ok", "_", line, col),
                              optc_mk_bool(ok_true, line, col),
                              optc_mk_variant_pat("Err", "_", line, col),
                              optc_mk_bool(!ok_true, line, col), line, col);
        break;
    }
    case OPTC_OK: {
        /* Result(T,E).ok() -> Option(T): match recv { Ok(x)=>Some(x) Err(_)=>None } */
        Type *t = optc_variant_payload(recv_type, "Ok");
        result_ctx = t ? instantiate_template(c, find_template_idx(c, "Option"),
                                              &t, 1, line, col) : NULL;
        repl = optc_mk_match2(recv,
                              optc_mk_variant_pat("Ok", vb, line, col),
                              optc_mk_variant_call("Some", optc_mk_ident(vb, line, col), line, col),
                              optc_mk_variant_pat("Err", "_", line, col),
                              optc_mk_ident("None", line, col), line, col);
        break;
    }
    case OPTC_ERR: {
        /* Result(T,E).err() -> Option(E): match recv { Ok(_)=>None Err(e)=>Some(e) } */
        Type *e = optc_variant_payload(recv_type, "Err");
        result_ctx = e ? instantiate_template(c, find_template_idx(c, "Option"),
                                              &e, 1, line, col) : NULL;
        repl = optc_mk_match2(recv,
                              optc_mk_variant_pat("Ok", "_", line, col),
                              optc_mk_ident("None", line, col),
                              optc_mk_variant_pat("Err", vb, line, col),
                              optc_mk_variant_call("Some", optc_mk_ident(vb, line, col), line, col),
                              line, col);
        break;
    }
    case OPTC_OK_OR: {
        /* Option(T).ok_or(e) -> Result(T, typeof(e)):
             match recv { Some(x)=>Ok(x)  None=>Err(e) }
           E is the error argument's type; build Result(T, E) for the arm ctors. */
        Type *t = optc_variant_payload(recv_type, "Some");
        Type *e_ty = arg0 ? check_expr(c, arg0) : NULL;
        if (t && e_ty) {
            Type *targs[2] = { t, e_ty };
            result_ctx = instantiate_template(c, find_template_idx(c, "Result"),
                                              targs, 2, line, col);
        }
        repl = optc_mk_match2(recv,
                              optc_mk_variant_pat("Some", vb, line, col),
                              optc_mk_variant_call("Ok", optc_mk_ident(vb, line, col), line, col),
                              optc_mk_ident("None", line, col),
                              optc_mk_variant_call("Err", arg0, line, col), line, col);
        arg0 = NULL;
        break;
    }
    default: return 0;
    }

    /* Steal repl's payload into node, then free the discarded AST_CALL shells. */
    node->kind = repl->kind;
    node->as = repl->as;
    node->resolved_type = NULL;
    free(repl);
    ast_free(callee);          /* recv detached above; frees the field name + shell */
    if (args) free(args);      /* the args array only; arg0 (if any) moved into repl */

    /* Re-check the rewritten node. Conversion combinators push their freshly-built
       result type as the expected type so bare ctors in the arm bodies resolve. */
    Type *saved_exp = c->expected_type;
    if (result_ctx) c->expected_type = result_ctx;
    *out_ty = check_expr(c, node);
    c->expected_type = saved_exp;
    return 1;
}

static Type *check_expr(Checker *c, AstNode *node)
{
    if (node == NULL)
        return NULL;

    Type *result = NULL;

    switch (node->kind)
    {
    case AST_INT_LIT:
        if (node->as.int_lit.is_char) {
            result = type_char();
        } else {
            /* An int literal that does not fit in i32 is typed i64, otherwise
               codegen (which emits int literals as i32) would truncate it.
               e.g. `i64 a = 9000000000`. */
            long long v = node->as.int_lit.value;
            result = (v > 2147483647LL || v < -2147483648LL)
                         ? type_i64() : type_int();
        }
        break;

    case AST_FLOAT_LIT:
        result = type_f64();
        break;

    case AST_STRING_LIT:
        result = type_string();
        /* P1 (docs/plan_string_to_stdlib.md §5.1): a string literal in a context
           expecting the pure-LS `Str` adopts the Str type — a static Str pointing
           at the same .rodata bytes (cap 0). Lets `Str s = "..."` work while the
           builtin string is still the default literal type. */
        {
            Type *strt = str_target_of_expected(c->expected_type);
            if (strt)
            {
                node->coerce_str_lit_to_str = true;
                result = strt;
            }
        }
        break;

    case AST_FORMAT_STRING:
    {
        /* The outer f-string's expected type (e.g. Str) must NOT leak into the
           interpolated exprs — an inner literal would otherwise coerce to Str and
           fail the printable check below. Clear it for the loop, consult it after. */
        Type *fstr_expected = c->expected_type;
        c->expected_type = NULL;
        /* Type-check each interpolated expression */
        for (int i = 0; i < node->as.format_string.expr_count; i++)
        {
            Type *et = check_expr(c, node->as.format_string.exprs[i]);
            if (et == NULL)
                continue;
            /* Ensure the expression is a printable type. The pure-LS `Str` is
               printable too (interpolated via "%.*s" by codegen). */
            if (!type_is_numeric(et) && et->kind != TYPE_BOOL && et->kind != TYPE_STRING && et->kind != TYPE_POINTER && et->kind != TYPE_OBJECT && !type_is_str_struct(et))
            {
                checker_error(c, node->as.format_string.exprs[i]->line,
                              node->as.format_string.exprs[i]->column,
                              "cannot interpolate type '%s' in format string",
                              type_name(et));
            }
        }
        c->expected_type = fstr_expected;
        result = type_string();
        /* P2 (docs/plan_string_to_stdlib.md §5.2): an f-string where a `Str` is
           expected produces an OWNED Str rvalue (the formatted heap buffer wrapped
           as Str, cap>0) — routed through the unified has_drop temp/drop path.
           v1 keeps the existing per-type formatting; only the output type changes.
           Default (no Str expected) stays builtin string. */
        if (type_is_str_struct(fstr_expected))
            result = fstr_expected;
        break;
    }

    case AST_MAP_LIT:
    {
        /* If resolved_type was already set (by check_stmt VAR_DECL special-case),
           just return it — the pairs were already checked against declared K,V. */
        if (node->resolved_type)
        {
            result = node->resolved_type;
            break;
        }

        int count = node->as.map_lit.pair_count;
        if (count == 0)
        {
            checker_error(c, node->line, node->column,
                          "empty map literal requires explicit type: map(K,V) m = {}");
            result = NULL;
            break;
        }
        checker_error(c, node->line, node->column,
                      "key-value literal requires an expected type with __from_pairs");
        result = NULL;
        break;
    }

    case AST_ARRAY_LIT:
    {
        /* If resolved_type was already set (by check_stmt VAR_DECL special-case
           for vec(T) v = [..]), just return it — elements were already checked. */
        if (node->resolved_type)
        {
            result = node->resolved_type;
            break;
        }

        /* Infer element type from first element, check all others match */
        int count = node->as.array_lit.count;
        if (count == 0)
        {
            checker_error(c, node->line, node->column,
                          "empty array literal (cannot infer element type)");
            result = NULL;
            break;
        }
        Type *elem_type = check_expr(c, node->as.array_lit.elements[0]);
        if (elem_type == NULL)
        {
            result = NULL;
            break;
        }
        for (int i = 1; i < count; i++)
        {
            Type *et = check_expr(c, node->as.array_lit.elements[i]);
            if (et == NULL)
                continue;
            if (!type_equals(elem_type, et))
            {
                checker_error(c, node->as.array_lit.elements[i]->line,
                              node->as.array_lit.elements[i]->column,
                              "array element type mismatch: expected '%s', got '%s'",
                              type_name(elem_type), type_name(et));
            }
        }
        result = type_array(elem_type, count);
        break;
    }

    case AST_BOOL_LIT:
        result = type_bool();
        break;

    case AST_NIL_LIT:
        result = type_nil();
        break;

    case AST_IDENT:
    {
        /* If the identifier is a builtin function, don't report "undefined variable" */
        if (is_builtin_function(node->as.ident.name))
        {
            result = NULL; /* Signal to caller to check for builtin */
            break;
        }
        Symbol *sym = scope_resolve(c->current_scope, node->as.ident.name);
        if (sym == NULL)
        {
            /* Try variant-ctor recognition for no-payload variants (e.g. `Red`, `None`) */
            Type *enum_type = NULL;
            int variant_idx = -1;
            int matches = find_variant(c, node->as.ident.name, &enum_type, &variant_idx);
            if (matches == 1 && enum_type->as.enom.variants[variant_idx].payload_count == 0)
            {
                node->resolved_type = enum_type;
                result = enum_type;
                break;
            }
            if (matches > 1)
            {
                /* Disambiguate by a type hint when available (the node's own prior
                   resolution, or the expected type — e.g. a typed LHS or a
                   combinator's pushed result type). */
                Type *eet = NULL; int evi = -1;
                if (disambig_variant_by_hint(c, node, node->as.ident.name, &eet, &evi) &&
                    eet->as.enom.variants[evi].payload_count == 0)
                {
                    node->resolved_type = eet;
                    result = eet;
                    break;
                }
                checker_error(c, node->line, node->column,
                              "ambiguous variant name '%s' (matches multiple enums; "
                              "explicit construction or type annotation required)",
                              node->as.ident.name);
                result = NULL;
                break;
            }
            if (matches == 1)
            {
                checker_error(c, node->line, node->column,
                              "variant '%s' expects %d payload argument(s); use '%s(...)' to construct",
                              node->as.ident.name,
                              enum_type->as.enom.variants[variant_idx].payload_count,
                              node->as.ident.name);
                result = NULL;
                break;
            }
            checker_error(c, node->line, node->column,
                          "undefined variable '%s'", node->as.ident.name);
            result = NULL;
        }
        else
        {
            /* Check for use of moved / maybe-moved variable (Phase A/B).
               MAYBE_MOVED = death: a variable possibly moved on some path is
               considered unusable even on paths where it would technically be live. */
            if (sym->is_moved)
            {
                checker_move_error(c, node->line, node->column,
                                   "use of moved variable '%s'", node->as.ident.name);
            }
            else if (sym->is_maybe_moved)
            {
                checker_move_error(c, node->line, node->column,
                                   "use of maybe-moved variable '%s' (moved on some control-flow path)",
                                   node->as.ident.name);
            }
            result = sym->type;
        }
        break;
    }

    case AST_MUT_BORROW:
    {
        /* &!x — explicit writable borrow. Must be an IDENT of an owned,
           non-moved, non-borrow variable of a reference-eligible type
           (Phase 5.5 only supports string). */
        AstNode *op = node->as.mut_borrow.operand;
        if (op == NULL || op->kind != AST_IDENT)
        {
            checker_error(c, node->line, node->column,
                          "&! requires a variable name (got a non-identifier expression)");
            result = NULL;
            break;
        }
        Symbol *sym = scope_resolve(c->current_scope, op->as.ident.name);
        if (sym == NULL)
        {
            checker_error(c, node->line, node->column,
                          "&!: undefined variable '%s'", op->as.ident.name);
            result = NULL;
            break;
        }
        if (sym->type == NULL ||
            (sym->type->kind != TYPE_STRING &&
             sym->type->kind != TYPE_STRUCT))
        {
            checker_error(c, node->line, node->column,
                          "&!: only &!string and &!struct are supported, got &!%s",
                          sym->type ? type_name(sym->type) : "?");
            result = NULL;
            break;
        }
        /* Phase B: drop struct mutable borrow now allowed. */
        if (sym->is_borrow)
        {
            checker_error(c, node->line, node->column,
                          "&!: cannot take writable borrow of read-only borrow '%s'",
                          op->as.ident.name);
            result = NULL;
            break;
        }
        if (sym->is_moved || sym->is_maybe_moved)
        {
            checker_error(c, node->line, node->column,
                          "&!: variable '%s' has been moved", op->as.ident.name);
            result = NULL;
            break;
        }
        /* Static strings (literals without .upper()/etc) are rejected: they
           live in .rodata, and append/reassign would force-own them on the
           fly — not implemented, and would surprise users. Require owned. */
        if (sym->is_static_string)
        {
            checker_error(c, node->line, node->column,
                          "&!: variable '%s' is a static string literal; "
                          "writable borrow requires an owned string",
                          op->as.ident.name);
            result = NULL;
            break;
        }
        /* Write the resolved ident type for downstream use, then return &!T. */
        op->resolved_type = sym->type;
        result = type_mut_reference(sym->type);
        break;
    }

    case AST_UNARY:
    {
        Type *operand = check_expr(c, node->as.unary.operand);
        if (operand == NULL)
        {
            result = NULL;
            break;
        }

        switch (node->as.unary.op)
        {
        case TOKEN_MINUS:
            if (!type_is_numeric(operand))
            {
                checker_error(c, node->line, node->column,
                              "unary '-' requires numeric type, got '%s'", type_name(operand));
                result = NULL;
            }
            else
            {
                result = operand;
            }
            break;
        case TOKEN_BANG:
            if (operand->kind != TYPE_BOOL)
            {
                checker_error(c, node->line, node->column,
                              "unary '!' requires bool, got '%s'", type_name(operand));
                result = NULL;
            }
            else
            {
                result = type_bool();
            }
            break;
        case TOKEN_TILDE:
            if (!type_is_integer(operand))
            {
                checker_error(c, node->line, node->column,
                              "unary '~' requires integer type, got '%s'", type_name(operand));
                result = NULL;
            }
            else
            {
                result = operand;
            }
            break;
        case TOKEN_AMP:
            /* &x -> *T */
            result = type_pointer(operand);
            break;
        case TOKEN_STAR:
            /* *ptr -> dereference */
            if (operand->kind != TYPE_POINTER)
            {
                checker_error(c, node->line, node->column,
                              "cannot dereference non-pointer type '%s'", type_name(operand));
                result = NULL;
            }
            else
            {
                result = operand->as.pointer_to;
            }
            break;
        default:
            checker_error(c, node->line, node->column, "unknown unary operator");
            result = NULL;
            break;
        }
        break;
    }

    case AST_BINARY:
    {
        Type *left = check_expr(c, node->as.binary.left);
        Type *right = check_expr(c, node->as.binary.right);
        if (left == NULL || right == NULL)
        {
            result = NULL;
            break;
        }

        /* Operator overloading: if the left operand is a struct/enum, try to
           lower `a OP b` to a user-defined operator-method call. Must run BEFORE
           the builtin op switch so that struct `==` does not fall into the
           builtin type_equals path (which would emit invalid IR). */
        {
            Type *ov_result = NULL;
            if (try_operator_overload(c, node, left, right, &ov_result))
            {
                result = ov_result;
                break;
            }
        }

        switch (node->as.binary.op)
        {
        /* Arithmetic: +, -, *, /, % */
        case TOKEN_PLUS:
            /* Allow string + string for concatenation */
            if (left->kind == TYPE_STRING && right->kind == TYPE_STRING)
            {
                result = type_string();
                break;
            }
            /* fall through to numeric check */
        case TOKEN_MINUS:
        case TOKEN_STAR:
        case TOKEN_SLASH:
            if (!type_is_numeric(left) || !type_is_numeric(right))
            {
                checker_error(c, node->line, node->column,
                              "arithmetic operator requires numeric types, got '%s' and '%s'",
                              type_name(left), type_name(right));
                result = NULL;
            }
            else
            {
                Type *common = type_numeric_common(left, right);
                if (common == NULL)
                {
                    checker_error(c, node->line, node->column,
                                  "type mismatch in arithmetic: '%s' vs '%s' (no implicit widening; use 'as')",
                                  type_name(left), type_name(right));
                    result = NULL;
                }
                else
                {
                    result = common;
                }
            }
            break;

        case TOKEN_PERCENT:
            if (!type_is_integer(left) || !type_is_integer(right))
            {
                checker_error(c, node->line, node->column,
                              "'%%' requires integer types, got '%s' and '%s'",
                              type_name(left), type_name(right));
                result = NULL;
            }
            else
            {
                Type *common = type_numeric_common(left, right);
                if (common == NULL)
                {
                    checker_error(c, node->line, node->column,
                                  "type mismatch in '%%': '%s' vs '%s' (no implicit widening; use 'as')",
                                  type_name(left), type_name(right));
                    result = NULL;
                }
                else
                {
                    result = common;
                }
            }
            break;

        /* Bitwise: &, |, ^, <<, >> */
        case TOKEN_AMP:
        case TOKEN_PIPE:
        case TOKEN_CARET:
            if (!type_is_integer(left) || !type_is_integer(right))
            {
                checker_error(c, node->line, node->column,
                              "bitwise operator requires integer types, got '%s' and '%s'",
                              type_name(left), type_name(right));
                result = NULL;
            }
            else
            {
                Type *common = type_numeric_common(left, right);
                if (common == NULL)
                {
                    checker_error(c, node->line, node->column,
                                  "type mismatch in bitwise op: '%s' vs '%s' (no implicit widening; use 'as')",
                                  type_name(left), type_name(right));
                    result = NULL;
                }
                else
                {
                    result = common;
                }
            }
            break;

        case TOKEN_LSHIFT:
        case TOKEN_RSHIFT:
            if (!type_is_integer(left) || !type_is_integer(right))
            {
                checker_error(c, node->line, node->column,
                              "shift operator requires integer types, got '%s' and '%s'",
                              type_name(left), type_name(right));
                result = NULL;
            }
            else
            {
                result = left;
            }
            break;

        /* Comparison: ==, !=, <, >, <=, >= */
        case TOKEN_EQ:
        case TOKEN_NEQ:
            if (type_equals(left, right))
            {
                result = type_bool();
            }
            else if (type_is_pointer_like(left) && type_is_pointer_like(right))
            {
                /* Allow: *T == nil, object == nil, *T == object, etc. */
                result = type_bool();
            }
            else if (type_numeric_common(left, right) != NULL)
            {
                /* Allow mixed numeric/char comparisons: 'A' == 65, char vs int, etc. */
                result = type_bool();
            }
            else
            {
                checker_error(c, node->line, node->column,
                              "cannot compare '%s' and '%s' for equality",
                              type_name(left), type_name(right));
                result = NULL;
            }
            break;

        case TOKEN_LT:
        case TOKEN_GT:
        case TOKEN_LEQ:
        case TOKEN_GEQ:
            if (left && right &&
                left->kind == TYPE_STRING && right->kind == TYPE_STRING)
            {
                result = type_bool();
            }
            else if (!type_is_numeric(left) || !type_is_numeric(right))
            {
                checker_error(c, node->line, node->column,
                              "comparison requires numeric or string types, got '%s' and '%s'",
                              type_name(left), type_name(right));
                result = NULL;
            }
            else if (type_numeric_common(left, right) == NULL)
            {
                checker_error(c, node->line, node->column,
                              "type mismatch in comparison: '%s' vs '%s' (no implicit widening; use 'as')",
                              type_name(left), type_name(right));
                result = NULL;
            }
            else
            {
                result = type_bool();
            }
            break;

        /* Logical: &&, || */
        case TOKEN_AND:
        case TOKEN_OR:
            if (left->kind != TYPE_BOOL || right->kind != TYPE_BOOL)
            {
                checker_error(c, node->line, node->column,
                              "logical operator requires bool, got '%s' and '%s'",
                              type_name(left), type_name(right));
                result = NULL;
            }
            else
            {
                result = type_bool();
            }
            break;

        default:
            checker_error(c, node->line, node->column, "unknown binary operator");
            result = NULL;
            break;
        }
        break;
    }

    case AST_CALL:
    {
        /* A-1: canonical-path call to a std.c primitive — std.c.malloc/realloc/
           free/abort. Resolved by spelling (not via a local import), so it works
           inside generic method bodies re-checked at the consumer site. Validate
           args against the fixed signatures; codegen recognises the same callee
           shape and lowers it. See docs/plan_runtime_primitives.md §5. */
        {
            int prim = match_stdc_prim(c, node->as.call.callee);
            if (prim >= 0)
            {
                /* Fixed signatures: malloc(i64)->*u8, realloc(*u8,i64)->*u8,
                   free(*u8)->void, abort()->void. */
                int want_argc = (prim == 0) ? 1 : (prim == 1) ? 2 : (prim == 2) ? 1 : 0;
                const char *pname = (prim == 0) ? "malloc" : (prim == 1) ? "realloc"
                                  : (prim == 2) ? "free" : "abort";
                int argc = node->as.call.arg_count;
                if (argc != want_argc)
                {
                    checker_error(c, node->line, node->column,
                                  "std.c.%s expects %d argument(s), got %d",
                                  pname, want_argc, argc);
                    result = NULL;
                    break;
                }
                /* Resolve arg types (loose: these are raw-pointer/size primitives,
                   mirroring the old builtins which did no strict arg checking). */
                for (int ai = 0; ai < argc; ai++)
                    (void)check_expr(c, node->as.call.args[ai]);
                Type *ret = (prim == 0 || prim == 1) ? type_pointer(type_u8())
                                                     : type_void();
                /* Build the fn type so callee->resolved_type is well-formed. */
                Type *fty;
                if (prim == 0) { Type **p = malloc_safe(sizeof(Type*)); p[0]=type_i64();
                                 fty = type_function(p,1,type_pointer(type_u8()),false); }
                else if (prim == 1) { Type **p = malloc_safe(2*sizeof(Type*));
                                 p[0]=type_pointer(type_u8()); p[1]=type_i64();
                                 fty = type_function(p,2,type_pointer(type_u8()),false); }
                else if (prim == 2) { Type **p = malloc_safe(sizeof(Type*));
                                 p[0]=type_pointer(type_u8());
                                 fty = type_function(p,1,type_void(),false); }
                else { fty = type_function(NULL,0,type_void(),false); }
                node->as.call.callee->resolved_type = fty;
                result = ret;
                break;
            }
        }

        /* G2: generic function call — identity(int)(42).
           When type_arg_count > 0 and callee is IDENT matching a fn_template,
           instantiate the template and resolve the call to the mangled function. */
        if (node->as.call.type_arg_count > 0 &&
            node->as.call.callee->kind == AST_IDENT)
        {
            const char *fn_name = node->as.call.callee->as.ident.name;
            int tmpl_idx = find_fn_template(c, fn_name);
            if (tmpl_idx < 0) {
                checker_error(c, node->line, node->column,
                    "'%s' is not a generic function", fn_name);
                result = NULL;
                break;
            }
            int tp_count = c->fn_templates[tmpl_idx].type_param_count;
            if (node->as.call.type_arg_count != tp_count) {
                checker_error(c, node->line, node->column,
                    "'%s' expects %d type argument(s), got %d",
                    fn_name, tp_count, node->as.call.type_arg_count);
                result = NULL;
                break;
            }
            /* Resolve type arguments */
            Type **type_args = (Type **)malloc_safe((size_t)tp_count * sizeof(Type *));
            bool type_args_ok = true;
            for (int ti = 0; ti < tp_count; ti++) {
                type_args[ti] = resolve_type_node(c, node->as.call.type_args[ti],
                    node->line, node->column);
                if (!type_args[ti]) { type_args_ok = false; break; }
            }
            if (!type_args_ok) { free(type_args); result = NULL; break; }

            /* Check trait bounds (if any) */
            {
                AstNode *tmpl = c->fn_templates[tmpl_idx].decl_node;
                TypeParamBound *bounds = tmpl->as.fn_decl.type_param_bounds;
                if (bounds) {
                    bool bounds_ok = true;
                    for (int ti = 0; ti < tp_count && bounds_ok; ti++) {
                        for (int bi = 0; bi < bounds[ti].count; bi++) {
                            if (!checker_type_satisfies_trait(c, type_args[ti],
                                                              bounds[ti].trait_names[bi])) {
                                checker_error(c, node->line, node->column,
                                    "type '%s' does not satisfy trait '%s' "
                                    "(required by type parameter '%s' of '%s')",
                                    type_name(type_args[ti]),
                                    bounds[ti].trait_names[bi],
                                    c->fn_templates[tmpl_idx].type_params[ti],
                                    fn_name);
                                bounds_ok = false;
                                break;
                            }
                        }
                    }
                    if (!bounds_ok) { free(type_args); result = NULL; break; }
                }
            }

            /* Build mangled name: "identity(int)" */
            char mangled[512];
            int pos = snprintf(mangled, sizeof(mangled), "%s(", fn_name);
            for (int ti = 0; ti < tp_count && pos < (int)sizeof(mangled) - 2; ti++) {
                if (ti > 0) pos += snprintf(mangled + pos, sizeof(mangled) - (size_t)pos, ",");
                pos += snprintf(mangled + pos, sizeof(mangled) - (size_t)pos, "%s",
                    type_name(type_args[ti]));
            }
            snprintf(mangled + pos, sizeof(mangled) - (size_t)pos, ")");

            /* Check if already instantiated (look up in scope) */
            Symbol *existing = scope_resolve(c->current_scope, mangled);
            if (existing) {
                /* Already instantiated — use existing type */
                node->as.call.callee->resolved_type = existing->type;
                /* Type-check arguments */
                Type *fn_t = existing->type;
                int argc = node->as.call.arg_count;
                int expected = fn_t->as.function.param_count;
                if (argc != expected) {
                    checker_error(c, node->line, node->column,
                        "'%s' expects %d argument(s), got %d", mangled, expected, argc);
                    free(type_args);
                    result = NULL;
                    break;
                }
                for (int ai = 0; ai < argc; ai++) {
                    Type *at = check_expr(c, node->as.call.args[ai]);
                    Type *pt = fn_t->as.function.params[ai];
                    if (at && pt && !type_equals(at, pt) && !type_widens_to(at, pt)) {
                        checker_error(c, node->as.call.args[ai]->line,
                            node->as.call.args[ai]->column,
                            "argument %d: expected '%s', got '%s'",
                            ai + 1, type_name(pt), type_name(at));
                    }
                }
                result = fn_t->as.function.return_type;
                free(type_args);
                break;
            }

            /* Not yet instantiated — clone, substitute, type-check, push to pending */
            AstNode *tmpl_decl = c->fn_templates[tmpl_idx].decl_node;
            char **tp_names = c->fn_templates[tmpl_idx].type_params;

            /* Temporarily register type aliases (T→int, U→string, ...) */
            int saved_alias_count = c->type_alias_count;
            for (int ti = 0; ti < tp_count; ti++)
                register_type_alias(c, tp_names[ti], type_args[ti]);

            /* Resolve concrete param types and return type */
            int pc = tmpl_decl->as.fn_decl.param_count;
            Type **params = (Type **)malloc_safe((size_t)pc * sizeof(Type *));
            for (int pi = 0; pi < pc; pi++) {
                params[pi] = resolve_type_node(c, tmpl_decl->as.fn_decl.param_types[pi],
                    node->line, node->column);
                if (!params[pi]) params[pi] = type_int(); /* fallback */
            }
            Type *ret = tmpl_decl->as.fn_decl.return_type
                ? resolve_type_node(c, tmpl_decl->as.fn_decl.return_type,
                    node->line, node->column)
                : type_void();
            Type *fn_type = type_function(params, pc, ret, false);

            /* Register in scope so subsequent calls reuse */
            scope_define(c->current_scope, mangled, fn_type);

            /* Clone the fn body and type-check it */
            AstNode *cloned = ast_clone_deep(tmpl_decl);
            cloned->resolved_type = fn_type;
            cloned->as.fn_decl.type_param_count = 0; /* concrete now */
            cloned->as.fn_decl.type_param_bounds = NULL; /* don't double-free template bounds */

            push_scope(c);
            for (int pi = 0; pi < pc; pi++) {
                Symbol *psym = scope_define(c->current_scope,
                    cloned->as.fn_decl.param_names[pi], params[pi]);
                if (psym && params[pi]->kind == TYPE_STRING)
                    psym->is_static_string = false;
            }
            Type *saved_ret = c->current_fn_return;
            c->current_fn_return = ret;
            check_stmt(c, cloned->as.fn_decl.body);
            c->current_fn_return = saved_ret;
            pop_scope(c);

            /* Restore type aliases */
            c->type_alias_count = saved_alias_count;

            /* Push to pending generic methods queue (reusing the same mechanism).
               A2: when this instantiation belongs to an imported module, prefix
               the symbol with "<modpath>__" (matching codegen's cg_module_fn_symbol
               and current_emit_module) so two modules' same-named generics get
               distinct LLVM symbols. The checker-internal cache key `mangled`
               stays unprefixed (each module has its own checker/scope). */
            char prefixed[640];
            if (c->module_name && c->module_name[0]) {
                int pp = 0;
                for (const char *mp = c->module_name; *mp && pp < 600; mp++)
                    prefixed[pp++] = (*mp == '.') ? '_' : *mp;
                prefixed[pp++] = '_'; prefixed[pp++] = '_';
                snprintf(prefixed + pp, sizeof(prefixed) - (size_t)pp, "%s", mangled);
            } else {
                snprintf(prefixed, sizeof(prefixed), "%s", mangled);
            }
            char *owned_mangled = (char *)malloc_safe(strlen(prefixed) + 1);
            memcpy(owned_mangled, prefixed, strlen(prefixed) + 1);

            if (c->pending_gm_count >= c->pending_gm_cap) {
                c->pending_gm_cap = c->pending_gm_cap < 8 ? 8 : c->pending_gm_cap * 2;
                c->pending_generic_methods = realloc_safe(c->pending_generic_methods,
                    (size_t)c->pending_gm_cap * sizeof(c->pending_generic_methods[0]));
            }
            int gm_idx = c->pending_gm_count++;
            c->pending_generic_methods[gm_idx].cloned_fn = cloned;
            c->pending_generic_methods[gm_idx].mangled_name = owned_mangled;
            c->pending_generic_methods[gm_idx].struct_type = NULL; /* not a method */

            /* Set callee resolved_type and check call arguments */
            node->as.call.callee->resolved_type = fn_type;
            int argc = node->as.call.arg_count;
            if (argc != pc) {
                checker_error(c, node->line, node->column,
                    "'%s' expects %d argument(s), got %d", mangled, pc, argc);
            }
            for (int ai = 0; ai < argc && ai < pc; ai++) {
                Type *at = check_expr(c, node->as.call.args[ai]);
                if (at && params[ai] && !type_equals(at, params[ai])
                    && !type_widens_to(at, params[ai])) {
                    checker_error(c, node->as.call.args[ai]->line,
                        node->as.call.args[ai]->column,
                        "argument %d: expected '%s', got '%s'",
                        ai + 1, type_name(params[ai]), type_name(at));
                }
            }
            result = ret;
            free(type_args);
            break;
        }

        /* Polymorphic built-in math dispatch: math.abs/min/max accept either
           int or float and pick the appropriate LLVM intrinsic at codegen.
           We intercept early to set the call's resolved_type to the args'
           common numeric type instead of the placeholder f64-only signature
           in the math module's exports.

           Only fires when obj_node is a bare identifier that resolves
           directly to a built-in math module in scope. This guard avoids
           triggering check_expr on struct type names (e.g. Point.origin())
           which would otherwise produce spurious "undefined variable" errors. */
        if (node->as.call.callee->kind == AST_FIELD &&
            node->as.call.callee->as.field_access.object->kind == AST_IDENT)
        {
            AstNode *obj_node = node->as.call.callee->as.field_access.object;
            const char *fn_name = node->as.call.callee->as.field_access.field;
            Symbol *mod_sym = scope_resolve(c->current_scope,
                                            obj_node->as.ident.name);
            Type *obj_type = mod_sym ? mod_sym->type : NULL;
            if (obj_type && obj_type->kind == TYPE_MODULE &&
                obj_type->as.module.is_builtin && obj_type->as.module.name &&
                strcmp(obj_type->as.module.name, "math") == 0)
            {
                /* Pin the obj IDENT's resolved_type so codegen sees a module
                   reference (matches what check_expr on the IDENT would do). */
                obj_node->resolved_type = obj_type;
                int arity = 0;
                MathPolyKind poly = MATH_POLY_NONE;
                if (builtin_math_lookup_fn(fn_name, &arity, NULL, NULL, &poly, NULL) &&
                    poly == MATH_POLY_INT_OR_FLOAT)
                {
                    int argc = node->as.call.arg_count;
                    if (argc != arity)
                    {
                        checker_error(c, node->line, node->column,
                                      "math.%s expects %d argument(s), got %d",
                                      fn_name, arity, argc);
                        result = NULL;
                        node->as.call.callee->resolved_type = NULL;
                        break;
                    }
                    Type *t0 = check_expr(c, node->as.call.args[0]);
                    Type *common = t0;
                    if (arity == 2)
                    {
                        Type *t1 = check_expr(c, node->as.call.args[1]);
                        common = type_numeric_common(t0, t1);
                        if (common == NULL)
                        {
                            checker_error(c, node->line, node->column,
                                          "math.%s arguments have incompatible numeric types '%s' and '%s'",
                                          fn_name, type_name(t0), type_name(t1));
                            result = NULL;
                            break;
                        }
                    }
                    if (common == NULL || !type_is_numeric(common))
                    {
                        checker_error(c, node->line, node->column,
                                      "math.%s requires numeric argument(s), got '%s'",
                                      fn_name, type_name(t0));
                        result = NULL;
                        break;
                    }
                    /* Set callee resolved_type to mirror the dispatched signature
                       so codegen widening at call site sees the right param types. */
                    Type **params = (Type **)malloc_safe((size_t)arity * sizeof(Type *));
                    for (int k = 0; k < arity; k++) params[k] = common;
                    Type *fn_t = type_function(params, arity, common, false);
                    node->as.call.callee->resolved_type = fn_t;
                    result = common;
                    break;
                }
            }
            /* Built-in `perf` module: arity check + fixed return type (no polymorphism). */
            if (obj_type && obj_type->kind == TYPE_MODULE &&
                obj_type->as.module.is_builtin && obj_type->as.module.name &&
                strcmp(obj_type->as.module.name, "perf") == 0)
            {
                obj_node->resolved_type = obj_type;
                int arity = 0;
                PerfEmitKind perf_kind = PERF_EMIT_NOW;
                if (builtin_perf_lookup_fn(fn_name, &arity, &perf_kind))
                {
                    int argc = node->as.call.arg_count;
                    if (argc != arity)
                    {
                        checker_error(c, node->line, node->column,
                                      "perf.%s expects %d argument(s), got %d",
                                      fn_name, arity, argc);
                        result = NULL;
                        node->as.call.callee->resolved_type = NULL;
                        break;
                    }
                    for (int k = 0; k < argc; k++)
                        check_expr(c, node->as.call.args[k]);
                    Type *ret_t = (perf_kind == PERF_EMIT_ELAPSED_MS ||
                                   perf_kind == PERF_EMIT_ELAPSED_S)
                                  ? type_f64() : type_i64();
                    Type *arg_t = type_i64();
                    Type **params = NULL;
                    if (arity > 0) {
                        params = (Type **)malloc_safe((size_t)arity * sizeof(Type *));
                        for (int k = 0; k < arity; k++) params[k] = arg_t;
                    }
                    node->as.call.callee->resolved_type =
                        type_function(params, arity, ret_t, false);
                    result = ret_t;
                    break;
                }
            }
        }

        /* Detect struct method calls: obj.method(args) or StructName.method(args) */
        bool is_method_call = false; /* instance method call: auto-pass self */
        bool is_static_call = false; /* static method call via type or instance */
        const char *method_struct = NULL;

        if (node->as.call.callee->kind == AST_FIELD)
        {
            AstNode *obj_node = node->as.call.callee->as.field_access.object;
            const char *method_name = node->as.call.callee->as.field_access.field;

            /* Check if obj is a struct type name (static call: Point.origin()) */
            if (obj_node->kind == AST_IDENT)
            {
                Type *st = find_struct_type(c, obj_node->as.ident.name);
                if (st && st->kind == TYPE_STRUCT)
                {
                    const char *st_key = impl_key_of_type(st);  /* B-4.1 */
                    int si = method_is_static(c, st_key, method_name);
                    if (si >= 0)
                    {
                        method_struct = st_key;
                        is_static_call = true;
                        if (si == 0)
                        {
                            /* Calling instance method via type name — error */
                            checker_error(c, node->line, node->column,
                                          "cannot call instance method '%s' on type '%s'; use an instance",
                                          method_name, method_struct);
                            result = NULL;
                            break;
                        }
                    }
                    }
                }
                /* Check if obj is an enum type name (static call: JsonValue.parse()) */
                if (obj_node->kind == AST_IDENT && !is_static_call)
                {
                    Type *et = find_enum_type(c, obj_node->as.ident.name);
                    if (et && et->kind == TYPE_ENUM)
                    {
                        const char *et_key = impl_key_of_type(et);  /* B-4.1 */
                        int si = method_is_static(c, et_key, method_name);
                        if (si >= 0)
                        {
                            method_struct = et_key;
                            is_static_call = true;
                            if (si == 0)
                            {
                                checker_error(c, node->line, node->column,
                                              "cannot call instance method '%s' on type '%s'; use an instance",
                                              method_name, method_struct);
                                result = NULL;
                                break;
                            }
                        }
                    }
                }

                /* If not a struct-type static call, resolve the object expression */
            if (!is_static_call)
            {
                Type *obj_type = check_expr(c, obj_node);

                /* C1: Option/Result combinators are compiler-lowered (rewrite this
                   AST_CALL in place to force-unwrap / match, then re-check). Runs
                   before the generic enum method dispatch, which has no methods for
                   the builtin Option/Result templates. */
                if (obj_type && obj_type->kind == TYPE_ENUM && obj_type->as.enom.name &&
                    (strncmp(obj_type->as.enom.name, "Option(", 7) == 0 ||
                     strncmp(obj_type->as.enom.name, "Result(", 7) == 0))
                {
                    Type *oc_ty = NULL;
                    int lo = lower_opt_combinator(c, node, obj_node, obj_type,
                                                  method_name, &oc_ty);
                    if (lo == 1) { result = oc_ty; break; }
                    if (lo < 0)  { result = NULL; break; }
                    /* lo == 0: not a combinator → fall through to normal dispatch. */
                }

                /* Intercept string builtin method calls: s.method(args...).
                   Phase 2.5: if the name matches no builtin method, fall through
                   to the user `impl string` lookup (Step 11) below. */
                if (obj_type && obj_type->kind == TYPE_STRING)
                {
                    c->string_no_builtin_match = false;
                    result = check_string_method(c, node, obj_type);
                    if (!c->string_no_builtin_match)
                        break;
                    /* Not a builtin: require a user `impl string` method, else
                       give a clear hint to import the stdlib that defines it. */
                    if (find_method(c, "string", method_name) == NULL)
                    {
                        checker_error(c, node->line, node->column,
                                      "string has no method '%s' "
                                      "(did you forget `import std.string`?)",
                                      method_name);
                        result = NULL;
                        break;
                    }
                    /* else: fall through (deref stays string, struct/enum blocks
                       skipped, Step 11 resolves the user method). */
                }

                /* Check if obj is an instance of a struct */
                if (obj_type)
                {
                    Type *deref = obj_type;
                    if (deref->kind == TYPE_POINTER && deref->as.pointer_to &&
                        deref->as.pointer_to->kind == TYPE_STRUCT)
                    {
                        deref = deref->as.pointer_to;
                    }
                    if (deref->kind == TYPE_STRUCT && deref->as.strukt.name)
                    {
                        const char *ds_key = impl_key_of_type(deref);  /* B-4.1 */
                        int si = method_is_static(c, ds_key, method_name);
                        /* VR-LIM-018: consumer met an imported generic
                           instantiation never registered locally — register its
                           impl methods on demand from the stamped metadata. */
                        if (si < 0 && deref->as.strukt.generic_base) {
                            ensure_generic_struct_impls_local(c, deref);
                            si = method_is_static(c, ds_key, method_name);
                        }
                        if (si == 0)
                        {
                            /* Phase A1: gate method calls on struct borrows by the
                               method's declared self-borrow kind.
                                 method sbk == 0 (legacy implicit) → mut self required
                                 method sbk == 1 (&self)           → any borrow OK
                                 method sbk == 2 (&!self)          → mut self required */
                            if (obj_node->kind == AST_IDENT)
                            {
                                Symbol *bsym = scope_resolve(c->current_scope,
                                                             obj_node->as.ident.name);
                                if (bsym && (bsym->is_borrow || bsym->is_mut_borrow))
                                {
                                    int msbk = method_self_borrow_kind(c,
                                        ds_key, method_name);  /* B-4.1 */
                                    if (msbk == 0)
                                    {
                                        /* Legacy method: cannot be called on any borrow.
                                           Encourage migration to &self/&!self. */
                                        checker_move_error(c, node->line, node->column,
                                            "cannot call method '%s.%s()' on '%s': "
                                            "method has no self-borrow annotation; "
                                            "declare it as 'fn %s(&self ...)' or "
                                            "'fn %s(&!self ...)' to allow calling on borrows",
                                            deref->as.strukt.name, method_name,
                                            obj_node->as.ident.name,
                                            method_name, method_name);
                                        result = NULL;
                                        break;
                                    }
                                    if (msbk == 2 && bsym->is_borrow)
                                    {
                                        /* &!self method on read-only borrow → reject */
                                        checker_move_error(c, node->line, node->column,
                                            "cannot call '%s.%s(&!self)' on '%s': "
                                            "method requires writable self, but "
                                            "'%s' is a read-only borrow",
                                            deref->as.strukt.name, method_name,
                                            obj_node->as.ident.name,
                                            obj_node->as.ident.name);
                                        result = NULL;
                                        break;
                                    }
                                    /* msbk == 1 (&self) → both borrow kinds OK
                                       msbk == 2 + mut_borrow obj → OK */
                                }
                            }
                            /* Instance method — auto self */
                            is_method_call = true;
                            method_struct = ds_key;  /* B-4.1 */
                        }
                        else if (si == 1)
                        {
                            /* Static method called via instance — allowed, ignore obj */
                            is_static_call = true;
                            method_struct = ds_key;  /* B-4.1 */
                        }
                    }

                    /* Check if obj is an instance of an enum */
                    if (deref->kind == TYPE_ENUM && deref->as.enom.name)
                    {
                        const char *de_key = impl_key_of_type(deref);  /* B-4.1 */
                        int si = method_is_static(c, de_key, method_name);
                        if (si == 0)
                        {
                            if (obj_node->kind == AST_IDENT)
                            {
                                Symbol *bsym = scope_resolve(c->current_scope,
                                                             obj_node->as.ident.name);
                                if (bsym && (bsym->is_borrow || bsym->is_mut_borrow))
                                {
                                    int msbk = method_self_borrow_kind(c,
                                        de_key, method_name);  /* B-4.1 */
                                    if (msbk == 0)
                                    {
                                        checker_move_error(c, node->line, node->column,
                                            "cannot call method '%s.%s()' on '%s': "
                                            "method has no self-borrow annotation; "
                                            "declare it as 'fn %s(&self ...)' or "
                                            "'fn %s(&!self ...)' to allow calling on borrows",
                                            deref->as.enom.name, method_name,
                                            obj_node->as.ident.name,
                                            method_name, method_name);
                                        result = NULL;
                                        break;
                                    }
                                    if (msbk == 2 && bsym->is_borrow)
                                    {
                                        checker_move_error(c, node->line, node->column,
                                            "cannot call '%s.%s(&!self)' on '%s': "
                                            "method requires writable self, but "
                                            "'%s' is a read-only borrow",
                                            deref->as.enom.name, method_name,
                                            obj_node->as.ident.name,
                                            obj_node->as.ident.name);
                                        result = NULL;
                                        break;
                                    }
                                }
                            }
                            is_method_call = true;
                            method_struct = de_key;  /* B-4.1 */
                        }
                        else if (si == 1)
                        {
                            is_static_call = true;
                            method_struct = de_key;  /* B-4.1 */
                        }
                    }

                    /* Step 11: check impl_registry for builtin types (int, f64, bool, ...) */
                    if (!is_method_call && !is_static_call)
                    {
                        const char *impl_name = type_impl_name(obj_type);
                        if (impl_name)
                        {
                            int si = method_is_static(c, impl_name, method_name);
                            if (si == 0)
                            {
                                is_method_call = true;
                                method_struct = impl_name;
                            }
                            else if (si == 1)
                            {
                                is_static_call = true;
                                method_struct = impl_name;
                            }
                        }
                    }
                }
            }
        }

        /* Resolve callee type */
        Type *callee_type = NULL;
        if (is_method_call || is_static_call)
        {
            const char *method_name = node->as.call.callee->as.field_access.field;
            callee_type = find_method(c, method_struct, method_name);
            if (callee_type == NULL)
            {
                checker_error(c, node->line, node->column,
                              "type '%s' has no method '%s'", method_struct, method_name);
                result = NULL;
                break;
            }

            /* Method-level generic: if the call site provides type args, try
               to build the concrete signature on-the-fly.  The placeholder
               returned by find_method has type_void() — the real type is built
               and body-checked here. */
            if (node->as.call.type_arg_count > 0) {
                Type *concrete = try_instantiate_method_level_generic(
                    c, method_struct, method_name,
                    node->as.call.type_args, node->as.call.type_arg_count,
                    node->line, node->column);
                if (concrete) {
                    callee_type = concrete;
                    node->as.call.callee->resolved_type = callee_type;
                    /* Body already checked+queued by try_instantiate; skip lazy path */
                    goto after_method_check;
                }
            }

            if (!ensure_generic_method_instantiated(c, method_struct, method_name,
                                                     node->line, node->column))
            {
                result = NULL;
                break;
            }
            /* Set resolved_type on the callee node so codegen can find it */
            node->as.call.callee->resolved_type = callee_type;

            after_method_check: ;

            /* Check for dangerous __drop call in user-defined __drop */
            if (c->in_user_defined_drop &&
                strcmp(method_name, "__drop") == 0 &&
                is_method_call)
            {
                /* Check if the target struct has compiler-generated __drop (no user impl) */
                Type *target_struct = find_struct_type(c, method_struct);
                if (target_struct && target_struct->kind == TYPE_STRUCT &&
                    target_struct->as.strukt.has_drop)
                {
                    /* The target has __drop. Check if it's compiler-generated (no user impl)
                       or user-defined (user wrote impl with __drop).
                       We can check if this is a compiler-generated __drop by seeing
                       if the struct was defined without an impl block.
                       Simpler: check if __drop method has no body in the source (hard to detect).
                       Alternative: don't warn if the caller struct also has compiler-generated __drop
                       (in that case user is just propagating the auto-cleanup). */

                    /* Only warn if we're in a user-defined __drop (not compiler-generated) */
                    /* We can't easily detect this, but we can warn about the pattern */
                    checker_warning(c, node->line, node->column,
                                    "explicitly calling __drop() in a __drop method may cause double-free if "
                                    "the target has compiler-generated destructor; "
                                    "the compiler will also call it automatically when the variable goes out of scope");
                }
            }
        }
        else
        {
            /* Variant ctor short-circuit: callee is an IDENT matching a registered
               enum variant.  Handles `RGB(1,2,3)`, `Some(x)`, etc. */
            if (node->as.call.callee->kind == AST_IDENT)
            {
                Type *enum_type = NULL;
                int variant_idx = -1;
                int matches = find_variant(c,
                    node->as.call.callee->as.ident.name, &enum_type, &variant_idx);
                if (matches == 1)
                {
                    result = check_variant_ctor(c, node, enum_type, variant_idx,
                                                node->as.call.args, node->as.call.arg_count);
                    break;
                }
                if (matches > 1)
                {
                    /* Disambiguate a payload variant ctor (e.g. `Some(x)`/`Ok(x)`/
                       `Err(e)`) by a type hint (prior resolution, then expected). */
                    Type *eet = NULL; int evi = -1;
                    if (disambig_variant_by_hint(c, node,
                            node->as.call.callee->as.ident.name, &eet, &evi))
                    {
                        result = check_variant_ctor(c, node, eet, evi,
                                                    node->as.call.args,
                                                    node->as.call.arg_count);
                        break;
                    }
                    checker_error(c, node->line, node->column,
                                  "ambiguous variant name '%s' (matches multiple enums)",
                                  node->as.call.callee->as.ident.name);
                    result = NULL;
                    break;
                }
            }

            callee_type = check_expr(c, node->as.call.callee);

            /* Check builtin functions before checking function type */
            if (callee_type == NULL && node->as.call.callee->kind == AST_IDENT)
            {
                result = check_builtin_call(c, node->as.call.callee->as.ident.name, node);
                if (result != NULL)
                    break;
            }
        }
        if (callee_type == NULL)
        {
            result = NULL;
            break;
        }

        /* Phase B: Block-typed callees use the same param/return layout as
           TYPE_FUNCTION (the only difference is ABI — codegen lowers as an
           indirect call through a fat pointer). The arity / arg-type checks
           below treat both kinds identically. */
        if (callee_type->kind != TYPE_FUNCTION &&
            callee_type->kind != TYPE_BLOCK)
        {
            checker_error(c, node->line, node->column,
                          "cannot call non-function type '%s'", type_name(callee_type));
            result = NULL;
            break;
        }

        int expected = callee_type->as.function.param_count;
        int actual = node->as.call.arg_count;

        /* For instance method calls, the first param is the implicit self pointer.
           The user provides (expected - 1) arguments. */
        int user_expected = is_method_call ? expected - 1 : expected;
        int param_offset = is_method_call ? 1 : 0;

        /* Special case: print() requires at least 1 argument */
        if (callee_type->as.function.is_vararg && user_expected == 0 && actual == 0 && node->as.call.callee->kind == AST_IDENT && strcmp(node->as.call.callee->as.ident.name, "print") == 0)
        {
            checker_error(c, node->line, node->column,
                          "print() requires at least 1 argument");
            result = NULL;
            break;
        }

        if (callee_type->as.function.is_vararg)
        {
            if (actual < user_expected)
            {
                checker_error(c, node->line, node->column,
                              "too few arguments: expected at least %d, got %d", user_expected, actual);
                result = NULL;
                break;
            }
        }
        else
        {
            /* Param defaults (档1): trailing params with a default may be omitted.
               min_required = count of user params without a default. */
            int min_required = user_expected;
            if (callee_type->as.function.param_defaults)
            {
                min_required = 0;
                for (int i = param_offset; i < expected; i++)
                    if (callee_type->as.function.param_defaults[i] == NULL)
                        min_required++;
            }
            if (actual < min_required || actual > user_expected)
            {
                if (min_required == user_expected)
                    checker_error(c, node->line, node->column,
                                  "wrong number of arguments: expected %d, got %d",
                                  user_expected, actual);
                else
                    checker_error(c, node->line, node->column,
                                  "wrong number of arguments: expected %d..%d, got %d",
                                  min_required, user_expected, actual);
                result = NULL;
                break;
            }
        }

        /* Check argument types for non-vararg params (skip self param for instance methods) */
        bool args_ok = true;

        /* LS uses clone semantics: struct/string arguments are deep-copied on every call.
           No move tracking needed — the caller retains ownership of its variables. */
        for (int i = 0; i < user_expected && i < actual; i++)
        {
            /* Phase B closure: propagate the declared param type as expected_type
               so a Ruby-style closure literal (`|x| body`) at this position can
               infer its untyped params from the callee's `Block(...)` signature. */
            Type *param_type = callee_type->as.function.params[i + param_offset];
            Type *saved_exp = c->expected_type;
            c->expected_type = param_type;
            Type *arg_type = check_expr(c, node->as.call.args[i]);
            c->expected_type = saved_exp;
            if (arg_type == NULL)
            {
                args_ok = false;
                continue;
            }
            /* Migration bridge (B-2): a builtin-string VARIABLE passed to a
               by-value `Str` parameter is deep-copied into an owned Str by codegen.
               Restricted to IDENT args: an owned string temp (e.g. sv.upper())
               tangles with string-temp-moved bookkeeping → require `Str t = ...`
               first. Literals already coerced zero-copy above. */
            if (arg_type->kind == TYPE_STRING && type_is_str_struct(param_type) &&
                node->as.call.args[i]->kind == AST_IDENT &&
                !node->as.call.args[i]->coerce_str_lit_to_str)
            {
                node->as.call.args[i]->coerce_string_to_str = true;
            }
            else if (!type_assignable(param_type, arg_type))
            {
                checker_error(c, node->as.call.args[i]->line, node->as.call.args[i]->column,
                              "argument %d: expected '%s', got '%s'",
                              i + 1,
                              type_name(param_type),
                              type_name(arg_type));
                args_ok = false;
            }
        }
        /* Check vararg args (just resolve types, no checking) */
        for (int i = user_expected; i < actual; i++)
        {
            check_expr(c, node->as.call.args[i]);
        }

        /* Param defaults (档1): append cloned default exprs for omitted trailing
           params so codegen sees a complete arg list (no codegen changes).
           Idempotent: after appending, arg_count == user_expected. */
        if (args_ok && callee_type->as.function.param_defaults && actual < user_expected)
        {
            node->as.call.args = (AstNode **)realloc_safe(
                node->as.call.args, (size_t)user_expected * sizeof(AstNode *));
            for (int i = actual; i < user_expected; i++)
            {
                AstNode *pd = (AstNode *)callee_type->as.function.param_defaults[i + param_offset];
                AstNode *clone = ast_clone_deep(pd);
                Type *pt = callee_type->as.function.params[i + param_offset];
                Type *se = c->expected_type;
                if (pt && (pt->kind == TYPE_STRUCT || pt->kind == TYPE_BLOCK))
                    c->expected_type = pt;
                check_expr(c, clone);
                c->expected_type = se;
                node->as.call.args[i] = clone;
            }
            node->as.call.arg_count = user_expected;
            actual = user_expected;
        }

        /* Phase 5.5 Step 4 — call-site aliasing check for writable borrows.
           Forbid passing the same variable in any of these conflicting combinations
           at a single call:
             f(&!x, &!x)   — two writable aliases
             f(&!x, x)     — writable + read-only auto-borrow (x -> &string param)
             f(&!x, &x)    — same, with explicit & (rare)
           Only check pairs where at least one side is a writable borrow. */
        if (args_ok)
        {
            int n = user_expected < actual ? user_expected : actual;
            for (int i = 0; i < n; i++)
            {
                AstNode *ai = node->as.call.args[i];
                if (ai == NULL || ai->kind != AST_MUT_BORROW) continue;
                AstNode *op_i = ai->as.mut_borrow.operand;
                if (op_i == NULL || op_i->kind != AST_IDENT) continue;
                const char *name_i = op_i->as.ident.name;

                for (int j = i + 1; j < n; j++)
                {
                    AstNode *aj = node->as.call.args[j];
                    if (aj == NULL) continue;

                    const char *name_j = NULL;
                    const char *j_kind = NULL;
                    if (aj->kind == AST_MUT_BORROW &&
                        aj->as.mut_borrow.operand &&
                        aj->as.mut_borrow.operand->kind == AST_IDENT)
                    {
                        name_j = aj->as.mut_borrow.operand->as.ident.name;
                        j_kind = "another writable borrow";
                    }
                    else if (aj->kind == AST_IDENT)
                    {
                        /* Only flag when the other arg binds to a parameter that
                           shares state (&T/&!T, or string-by-value which clones
                           at runtime — BUT cloning happens AFTER the writable
                           borrow is already holding the pointer; ordering of
                           eval is left-to-right so later by-value clone would
                           see a possibly-mutated snapshot, which is confusing.
                           Conservatively flag TYPE_REFERENCE on the other side. */
                        Type *pj = callee_type->as.function.params[j + param_offset];
                        if (pj && pj->kind == TYPE_REFERENCE)
                        {
                            name_j = aj->as.ident.name;
                            j_kind = "read-only borrow";
                        }
                    }
                    if (name_j && strcmp(name_i, name_j) == 0)
                    {
                        checker_error(c, aj->line, aj->column,
                                      "variable '%s' is already passed as writable borrow "
                                      "at argument %d; cannot also pass as %s here",
                                      name_i, i + 1, j_kind);
                        args_ok = false;
                    }
                }
            }
        }

        result = args_ok ? callee_type->as.function.return_type : NULL;
        break;
    }

    case AST_INDEX:
    {
        Type *obj = check_expr(c, node->as.index_expr.object);
        Type *idx = check_expr(c, node->as.index_expr.index);
        if (obj == NULL || idx == NULL)
        {
            result = NULL;
            break;
        }

        if (obj->kind == TYPE_ARRAY)
        {
            if (!type_is_integer(idx))
            {
                checker_error(c, node->line, node->column,
                              "array index must be integer, got '%s'", type_name(idx));
                result = NULL;
            }
            else
            {
                result = obj->as.array.elem;
            }
        }
        else if (obj->kind == TYPE_POINTER)
        {
            /* p[i] on a raw *T pointer — element access, no bounds check (unsafe
               layer). Result is the pointee type. The store form (p[i] = x) is a
               RAW store: it does NOT drop the old slot (the slot may be
               uninitialized memory), unlike vec/array element assignment. */
            if (!type_is_integer(idx))
            {
                checker_error(c, node->line, node->column,
                              "pointer index must be integer, got '%s'", type_name(idx));
                result = NULL;
            }
            else if (obj->as.pointer_to == NULL)
            {
                checker_error(c, node->line, node->column,
                              "cannot index opaque pointer");
                result = NULL;
            }
            else
            {
                result = obj->as.pointer_to;
            }
        }
        else if (obj->kind == TYPE_STRUCT &&
                 find_method(c, impl_key_of_type(obj), "__index") != NULL)
        {
            /* Index protocol: `v[i]` on a struct that opts in via
               `__index(&self, int) -> T` desugars to `v.__index(i)`. Rewrite the
               node in place to the method call and re-check (reuses all call
               machinery: monomorphization, return-value ownership, etc.). */
            AstNode *objn = node->as.index_expr.object;
            AstNode *idxn = node->as.index_expr.index;
            rewrite_index_to_call(node, objn, idxn, "__index");
            result = check_expr(c, node);
        }
        else
        {
            checker_error(c, node->line, node->column,
                          "cannot index non-array type '%s'", type_name(obj));
            result = NULL;
        }
        break;
    }

    case AST_FIELD:
    {
        Type *obj = check_expr(c, node->as.field_access.object);
        if (obj == NULL)
        {
            result = NULL;
            break;
        }

        const char *field_name = node->as.field_access.field;

        /* Module-qualified access (e.g., math.add) */
        if (obj->kind == TYPE_MODULE)
        {
            for (int i = 0; i < obj->as.module.export_count; i++)
            {
                if (strcmp(obj->as.module.exports[i].name, field_name) == 0)
                {
                    result = obj->as.module.exports[i].type;
                    break;
                }
            }
            if (result == NULL)
            {
                checker_error(c, node->line, node->column,
                              "module '%s' has no export '%s'",
                              obj->as.module.name ? obj->as.module.name : "<unknown>",
                              field_name);
            }
            break;
        }

        /* Array .length — compile-time constant */
        if (obj->kind == TYPE_ARRAY)
        {
            if (strcmp(field_name, "length") == 0)
            {
                result = type_int();
            }
            else
            {
                checker_error(c, node->line, node->column,
                              "array has no field '%s' (only 'length')", field_name);
                result = NULL;
            }
            break;
        }

        /* String .length — O(1) from LsString struct */
        if (obj->kind == TYPE_STRING)
        {
            if (strcmp(field_name, "length") == 0)
            {
                result = type_int();
            }
            else
            {
                checker_error(c, node->line, node->column,
                              "string has no field '%s'", field_name);
                result = NULL;
            }
            break;
        }

        /* Auto-dereference: *Struct → Struct or *Enum → Enum for field/method access */
        if (obj->kind == TYPE_POINTER && obj->as.pointer_to &&
            (obj->as.pointer_to->kind == TYPE_STRUCT ||
             obj->as.pointer_to->kind == TYPE_ENUM))
        {
            obj = obj->as.pointer_to;
        }

        if (obj->kind == TYPE_STRUCT)
        {
            /* Search struct fields */
            for (int i = 0; i < obj->as.strukt.field_count; i++)
            {
                if (strcmp(obj->as.strukt.fields[i].name, field_name) == 0)
                {
                    result = obj->as.strukt.fields[i].type;
                    break;
                }
            }
            /* Search methods if not found as field */
            if (result == NULL && obj->as.strukt.name)
            {
                result = find_method(c, impl_key_of_type(obj), field_name);  /* B-4.1 */
            }
            if (result == NULL)
            {
                checker_error(c, node->line, node->column,
                              "struct '%s' has no field or method '%s'",
                              obj->as.strukt.name ? obj->as.strukt.name : "<anon>",
                              field_name);
            }
        }
        else if (obj->kind == TYPE_ENUM)
        {
            /* Enum has no fields — only methods */
            if (obj->as.enom.name)
            {
                result = find_method(c, impl_key_of_type(obj), field_name);  /* B-4.1 */
            }
            if (result == NULL)
            {
                checker_error(c, node->line, node->column,
                              "enum '%s' has no method '%s'",
                              obj->as.enom.name ? obj->as.enom.name : "<anon>",
                              field_name);
            }
        }
        else
        {
            checker_error(c, node->line, node->column,
                          "field access on non-struct/enum type '%s'", type_name(obj));
            result = NULL;
        }
        break;
    }

    case AST_CLOSURE:
    {
        int n = node->as.closure.param_count;
        /* Ruby-style literals (`|x| body`, `|| body`) carry is_ruby_form=true
           and inherit their param/return types from the call-site's
           expected_type (a TYPE_BLOCK / TYPE_FUNCTION with matching arity).
           The legacy `fn(int x) -> R { ... }` form supplies its own types. */
        bool ruby_form = node->as.closure.is_ruby_form;
        /* Save outer scope BEFORE we push the closure body scope below, so
           the Phase C capture scan can resolve free variables against the
           caller's environment. */
        Scope *outer_scope_for_caps = c->current_scope;
        (void)outer_scope_for_caps;
        Type **params = NULL;
        Type *ret = NULL;

        if (ruby_form) {
            /* Phase B: pull param/return types from c->expected_type. */
            Type *exp = c->expected_type;
            if (exp == NULL ||
                (exp->kind != TYPE_BLOCK && exp->kind != TYPE_FUNCTION) ||
                exp->as.function.param_count != n)
            {
                checker_error(c, node->line, node->column,
                              "cannot infer closure parameter types: %s "
                              "(declare a typed `Block(...)` parameter at the "
                              "call site, or capture the closure into a typed "
                              "variable: `Adder f = |x| ...`)",
                              exp == NULL ? "no expected type at this position"
                                          : "expected type does not match closure shape");
                result = NULL;
                break;
            }
            if (n > 0) {
                params = (Type **)malloc_safe((size_t)n * sizeof(Type *));
                for (int i = 0; i < n; i++) {
                    params[i] = exp->as.function.params[i];
                }
            }
            ret = exp->as.function.return_type;
        } else {
            if (n > 0) {
                params = (Type **)malloc_safe((size_t)n * sizeof(Type *));
                for (int i = 0; i < n; i++) {
                    params[i] = resolve_type_node(c, node->as.closure.param_types[i],
                                                  node->line, node->column);
                }
            }
            ret = resolve_type_node(c, node->as.closure.return_type,
                                    node->line, node->column);
        }

        /* Phase C: scan body for free variables → record captures on the
           AST node so codegen can build the env struct. Walk runs against
           the OUTER scope (still current at this point) and seeds `bound`
           with the closure's parameter names. */
        if (ruby_form && node->as.closure.captures == NULL) {
            CaptureScan cs;
            memset(&cs, 0, sizeof(cs));
            cs.c = c;
            cs.outer_scope = outer_scope_for_caps;
            cs.closure_node = node;
            for (int i = 0; i < n; i++) {
                cap_push_bound(&cs, node->as.closure.param_names[i]);
            }
            capture_walk(&cs, node->as.closure.body);
            free((void*)cs.bound);
            node->as.closure.captures = (void*)cs.captures;
            node->as.closure.capture_count = cs.capture_count;

            /* F.1: Process [move v1, v2] capture spec.
               For each name in move_names:
               - Find the matching capture and set is_explicit_move=true.
               - If not found in captures, report an error. */
            for (int mi = 0; mi < node->as.closure.move_count; mi++) {
                const char *mname = node->as.closure.move_names[mi];
                bool found = false;
                for (int ci = 0; ci < node->as.closure.capture_count; ci++) {
                    if (strcmp(node->as.closure.captures[ci].name, mname) == 0) {
                        node->as.closure.captures[ci].is_explicit_move = true;
                        found = true;
                        break;
                    }
                }
                if (!found) {
                    checker_error(c, node->line, node->column,
                        "'%s' in [move ...] list is not referenced inside "
                        "the closure body and cannot be captured", mname);
                }
            }
        }

        /* Check body in new scope */
        push_scope(c);
        for (int i = 0; i < n; i++)
        {
            if (params[i])
            {
                /* M5-002: a closure param declared with a reference type (e.g.
                   `Block(&P)` → param `&P`) must be unwrapped to the pointee `P`
                   with is_borrow set, exactly like normal function params — else
                   `pp.x` / `pp.method()` in the body fails with "field access on
                   non-struct type '&P'". Keep params[i] (the &P) for the Block
                   signature; only the body-local symbol uses the unwrapped type. */
                Type *pt = params[i];
                bool is_borrow = false, is_mut_borrow = false;
                if (pt->kind == TYPE_REFERENCE)
                {
                    if (pt->is_mut) is_mut_borrow = true;
                    else            is_borrow     = true;
                    pt = pt->as.pointer_to;
                }
                Symbol *psym = scope_define(c->current_scope,
                                            node->as.closure.param_names[i], pt);
                if (psym)
                {
                    psym->is_borrow = is_borrow;
                    psym->is_mut_borrow = is_mut_borrow;
                    /* F.2: Block closure params share env_ptr with caller — borrow */
                    if (pt->kind == TYPE_BLOCK)
                        psym->is_borrow = true;
                }
            }
        }
        /* Define captures inside the closure scope so the body type-checker
           treats them as locally-bound (with the captured types from outer
           scope). Codegen will materialise these as alloca-of-loaded-from-env
           inside the synthesised __closure_<N> body. */
        for (int i = 0; i < node->as.closure.capture_count; i++) {
            scope_define(c->current_scope,
                         node->as.closure.captures[i].name,
                         node->as.closure.captures[i].type);
        }

        Type *saved_ret = c->current_fn_return;
        Type *saved_exp = c->expected_type;
        c->current_fn_return = ret;
        c->expected_type = NULL;  /* don't leak Block expected to body */
        check_stmt(c, node->as.closure.body);
        c->current_fn_return = saved_ret;
        c->expected_type = saved_exp;
        pop_scope(c);

        /* A Ruby-form literal materialises as a TYPE_BLOCK (closure value).
           Legacy fn(...) literals stay TYPE_FUNCTION for backward compat. */
        if (ruby_form) {
            result = type_block(params, n, ret);
        } else {
            result = type_function(params, n, ret, false);
        }
        break;
    }

    case AST_MATCH:
    {
        Type *subject = check_expr(c, node->as.match.subject);
        if (subject == NULL)
        {
            result = NULL;
            break;
        }

        /* Phase B: detect if the match subject is a borrowed enum variable.
           When true, owned payload binders (string/vec/map/struct/enum) are
           marked is_borrow=true so checker prevents moves and mutations. */
        bool subj_is_enum_borrow = false;
        if (node->as.match.subject->kind == AST_IDENT && subject &&
            subject->kind == TYPE_ENUM)
        {
            Symbol *ssym = scope_resolve(c->current_scope,
                                         node->as.match.subject->as.ident.name);
            if (ssym && ssym->is_borrow) subj_is_enum_borrow = true;
        }

        /* Enum subjects: variant patterns + exhaustiveness check */
        if (subject->kind == TYPE_ENUM)
        {
            int vc = subject->as.enom.variant_count;
            bool *covered = (bool *)calloc((size_t)vc, sizeof(bool));
            bool catchall = false;
            Type *arm_type = NULL;

            for (int i = 0; i < node->as.match.arm_count; i++)
            {
                MatchArm *arm = &node->as.match.arms[i];
                AstNode *pat = arm->pattern;
                const char *vname = NULL;
                AstNode **binders = NULL;
                int binder_count = 0;

                if (pat->kind == AST_IDENT && strcmp(pat->as.ident.name, "_") == 0)
                {
                    catchall = true;
                }
                else if (pat->kind == AST_IDENT)
                {
                    vname = pat->as.ident.name;
                }
                else if (pat->kind == AST_CALL && pat->as.call.callee->kind == AST_IDENT)
                {
                    vname = pat->as.call.callee->as.ident.name;
                    binders = pat->as.call.args;
                    binder_count = pat->as.call.arg_count;
                }
                else
                {
                    checker_error(c, pat->line, pat->column,
                                  "invalid pattern for enum '%s'", type_name(subject));
                }

                int variant_idx = -1;
                if (vname)
                {
                    for (int v = 0; v < vc; v++)
                    {
                        if (strcmp(subject->as.enom.variants[v].name, vname) == 0)
                        {
                            variant_idx = v;
                            break;
                        }
                    }
                    if (variant_idx < 0)
                    {
                        checker_error(c, pat->line, pat->column,
                                      "'%s' is not a variant of enum '%s'",
                                      vname, type_name(subject));
                        continue;
                    }
                    int expected = subject->as.enom.variants[variant_idx].payload_count;
                    if (binder_count != expected)
                    {
                        checker_error(c, pat->line, pat->column,
                                      "variant '%s' expects %d binder(s), got %d",
                                      vname, expected, binder_count);
                        continue;
                    }
                    covered[variant_idx] = true;
                }

                /* Push binder scope for arm body */
                push_scope(c);
                for (int b = 0; b < binder_count; b++)
                {
                    AstNode *bnode = binders[b];
                    if (bnode->kind != AST_IDENT)
                    {
                        checker_error(c, bnode->line, bnode->column,
                                      "variant pattern binder must be an identifier");
                        continue;
                    }
                    const char *bname = bnode->as.ident.name;
                    if (strcmp(bname, "_") == 0) continue;  /* skip wildcard binder */
                    Type *bt = subject->as.enom.variants[variant_idx].payload_types[b];
                    Symbol *bsym = scope_define(c->current_scope, bname, bt);
                    bnode->resolved_type = bt;
                    /* Phase B: for borrowed enum subject, mark owned payload binders
                       as read-only borrows — prevents moves and mutating methods. */
                    if (bsym && subj_is_enum_borrow && bt &&
                        (bt->kind == TYPE_STRING ||
                         (bt->kind == TYPE_STRUCT && bt->as.strukt.has_drop) ||
                         (bt->kind == TYPE_ENUM   && bt->as.enom.has_drop)))
                    {
                        bsym->is_borrow = true;
                    }
                }

                Type *body_type = check_expr(c, arm->body);
                pop_scope(c);

                if (body_type == NULL) continue;
                if (arm_type == NULL) arm_type = body_type;
                else if (!type_equals(arm_type, body_type))
                {
                    checker_error(c, arm->body->line, arm->body->column,
                                  "match arm type mismatch: expected '%s', got '%s'",
                                  type_name(arm_type), type_name(body_type));
                }
            }

            /* Exhaustiveness check */
            if (!catchall)
            {
                char missing[256];
                int pos = 0;
                bool any_missing = false;
                for (int v = 0; v < vc; v++)
                {
                    if (!covered[v])
                    {
                        if (any_missing && pos < (int)sizeof(missing) - 3)
                            pos += snprintf(missing + pos, sizeof(missing) - (size_t)pos, ", ");
                        pos += snprintf(missing + pos, sizeof(missing) - (size_t)pos, "%s",
                                        subject->as.enom.variants[v].name);
                        any_missing = true;
                    }
                }
                if (any_missing)
                {
                    checker_error(c, node->line, node->column,
                                  "non-exhaustive match on enum '%s': missing variant(s): %s",
                                  type_name(subject), missing);
                }
            }

            free(covered);
            result = arm_type;
            break;
        }

        /* Non-enum subjects: literal/ident/wildcard/OR-pattern handling.
           Walk the (possibly nested) AST_MATCH_OR_PATTERN tree to type-check
           every leaf pattern against the subject type. */
        Type *arm_type = NULL;
        for (int i = 0; i < node->as.match.arm_count; i++)
        {
            MatchArm *arm = &node->as.match.arms[i];

            /* Iterative tree walk (avoids deep recursion; max ~32 alternatives). */
            AstNode *stack[64];
            int sp = 0;
            stack[sp++] = arm->pattern;
            while (sp > 0)
            {
                AstNode *cur = stack[--sp];
                if (cur->kind == AST_MATCH_OR_PATTERN)
                {
                    /* Push both branches for later processing */
                    if (sp + 2 <= 64) {
                        stack[sp++] = cur->as.or_pattern.right;
                        stack[sp++] = cur->as.or_pattern.left;
                    }
                }
                else if (cur->kind == AST_IDENT &&
                         strcmp(cur->as.ident.name, "_") == 0)
                {
                    /* Wildcard — no type check needed */
                }
                else
                {
                    Type *pat_type = check_expr(c, cur);
                    if (pat_type && !type_equals(pat_type, subject) &&
                        type_numeric_common(pat_type, subject) == NULL)
                    {
                        checker_error(c, cur->line, cur->column,
                                      "match pattern type '%s' doesn't match subject type '%s'",
                                      type_name(pat_type), type_name(subject));
                    }
                }
            }

            /* Check body */
            Type *body_type = check_expr(c, arm->body);
            if (body_type == NULL)
                continue;

            if (arm_type == NULL)
            {
                arm_type = body_type;
            }
            else if (!type_equals(arm_type, body_type))
            {
                checker_error(c, arm->body->line, arm->body->column,
                              "match arm type mismatch: expected '%s', got '%s'",
                              type_name(arm_type), type_name(body_type));
            }
        }
        result = arm_type;
        break;
    }

    case AST_CAST:
    {
        Type *expr = check_expr(c, node->as.cast.expr);
        Type *target = resolve_type_node(c, node->as.cast.target_type,
                                         node->line, node->column);
        if (expr == NULL || target == NULL)
        {
            result = NULL;
            break;
        }

        /* Allow numeric<->numeric casts, pointer casts, and object casts */
        if (type_is_numeric(expr) && type_is_numeric(target))
        {
            result = target;
        }
        else if (expr->kind == TYPE_POINTER && target->kind == TYPE_POINTER)
        {
            result = target;
        }
        else if (type_is_integer(expr) && target->kind == TYPE_POINTER)
        {
            result = target;
        }
        else if (expr->kind == TYPE_POINTER && type_is_integer(target))
        {
            result = target;
            /* object <-> pointer: explicit cast */
        }
        else if (expr->kind == TYPE_OBJECT && target->kind == TYPE_POINTER)
        {
            result = target;
        }
        else if (expr->kind == TYPE_POINTER && target->kind == TYPE_OBJECT)
        {
            result = target;
            /* object <-> integer: explicit cast (like void* <-> intptr_t) */
        }
        else if (expr->kind == TYPE_OBJECT && type_is_integer(target))
        {
            result = target;
        }
        else if (type_is_integer(expr) && target->kind == TYPE_OBJECT)
        {
            result = target;
        }
        else
        {
            checker_error(c, node->line, node->column,
                          "invalid cast from '%s' to '%s'",
                          type_name(expr), type_name(target));
            result = NULL;
        }
        break;
    }

    case AST_SIZEOF:
    {
        /* sizeof(Type) -> i64, compile-time byte size. Resolve the operand type
           (type-param `T` is resolved via the active type-alias substitution
           registered during generic instantiation, same as cast). */
        Type *st = resolve_type_node(c, node->as.sizeof_expr.type_node,
                                     node->line, node->column);
        if (st == NULL)
        {
            result = NULL;
            break;
        }
        node->as.sizeof_expr.sized_type = st;
        result = type_i64();
        break;
    }

    case AST_TRY:
    {
        if (c->current_fn_return == NULL)
        {
            checker_error(c, node->line, node->column,
                          "try expression outside of function");
            result = NULL;
            break;
        }
        Type *inner = check_expr(c, node->as.try_expr.expr);
        if (inner == NULL) { result = NULL; break; }

        bool is_result = (inner->kind == TYPE_ENUM &&
                          strncmp(inner->as.enom.name, "Result(", 7) == 0);
        bool is_option = (inner->kind == TYPE_ENUM &&
                          strncmp(inner->as.enom.name, "Option(", 7) == 0);
        if (!is_result && !is_option)
        {
            checker_error(c, node->line, node->column,
                          "try requires Result(T,E) or Option(T), got '%s'",
                          type_name(inner));
            result = NULL;
            break;
        }

        /* Extract success type (T) and Err type (E, if Result) */
        Type *success_t = NULL;
        Type *err_t = NULL;
        for (int i = 0; i < inner->as.enom.variant_count; i++)
        {
            const char *vn = inner->as.enom.variants[i].name;
            int pc = inner->as.enom.variants[i].payload_count;
            if (is_result && strcmp(vn, "Ok") == 0 && pc > 0)
                success_t = inner->as.enom.variants[i].payload_types[0];
            else if (is_result && strcmp(vn, "Err") == 0 && pc > 0)
                err_t = inner->as.enom.variants[i].payload_types[0];
            else if (is_option && strcmp(vn, "Some") == 0 && pc > 0)
                success_t = inner->as.enom.variants[i].payload_types[0];
        }

        /* Validate current function return type can absorb the failure path */
        Type *fn_ret = c->current_fn_return;
        if (is_result)
        {
            if (fn_ret->kind != TYPE_ENUM ||
                strncmp(fn_ret->as.enom.name, "Result(", 7) != 0)
            {
                checker_error(c, node->line, node->column,
                              "try on Result requires function to return Result(_, E), got '%s'",
                              type_name(fn_ret));
                result = NULL;
                break;
            }
            Type *fn_err_t = NULL;
            for (int i = 0; i < fn_ret->as.enom.variant_count; i++)
            {
                if (strcmp(fn_ret->as.enom.variants[i].name, "Err") == 0 &&
                    fn_ret->as.enom.variants[i].payload_count > 0)
                {
                    fn_err_t = fn_ret->as.enom.variants[i].payload_types[0];
                    break;
                }
            }
            if (err_t != NULL && fn_err_t != NULL && !type_equals(err_t, fn_err_t))
            {
                checker_error(c, node->line, node->column,
                              "try Err type mismatch: expression has Err '%s' but function returns Err '%s'",
                              type_name(err_t), type_name(fn_err_t));
                result = NULL;
                break;
            }
        }
        else /* is_option */
        {
            if (fn_ret->kind != TYPE_ENUM ||
                strncmp(fn_ret->as.enom.name, "Option(", 7) != 0)
            {
                checker_error(c, node->line, node->column,
                              "try on Option requires function to return Option(_), got '%s'",
                              type_name(fn_ret));
                result = NULL;
                break;
            }
        }

        node->as.try_expr.fn_return_type = fn_ret;
        result = success_t;
        break;
    }

    case AST_FORCE_UNWRAP:
    {
        Type *inner = check_expr(c, node->as.force_unwrap.expr);
        if (inner == NULL) { result = NULL; break; }

        bool is_result = (inner->kind == TYPE_ENUM &&
                          strncmp(inner->as.enom.name, "Result(", 7) == 0);
        bool is_option = (inner->kind == TYPE_ENUM &&
                          strncmp(inner->as.enom.name, "Option(", 7) == 0);
        if (!is_result && !is_option)
        {
            checker_error(c, node->line, node->column,
                          "force-unwrap '!' requires Option(T) or Result(T,E), got '%s'",
                          type_name(inner));
            result = NULL;
            break;
        }

        /* Extract success type T */
        Type *success_t = NULL;
        for (int i = 0; i < inner->as.enom.variant_count; i++)
        {
            const char *vn = inner->as.enom.variants[i].name;
            int pc = inner->as.enom.variants[i].payload_count;
            if (is_result && strcmp(vn, "Ok") == 0 && pc > 0)
                success_t = inner->as.enom.variants[i].payload_types[0];
            else if (is_option && strcmp(vn, "Some") == 0 && pc > 0)
                success_t = inner->as.enom.variants[i].payload_types[0];
        }

        /* Move-elision (Q4): force-unwrapping consumes the operand's payload.
           For an owned-payload enum (has_drop, e.g. Option(string)/Result(_,str)/
           Option(Vec)/...) mark the source IDENT moved so (a) re-use is rejected
           and (b) codegen invalidates the source enum's scope-drop (no
           double-free). type_is_movable/checker_try_mark_moved don't cover enums
           (their move is tracked via moved_flag elsewhere), so mark inline here,
           mirroring those guards. POD Option(int)/borrows/rvalues are left live. */
        if (inner->as.enom.has_drop &&
            node->as.force_unwrap.expr->kind == AST_IDENT)
        {
            Symbol *osym = scope_resolve(
                c->current_scope, node->as.force_unwrap.expr->as.ident.name);
            if (osym && !osym->is_borrow && !osym->is_mut_borrow &&
                !osym->is_moved && !osym->is_maybe_moved)
            {
                osym->is_moved = true;
                node->as.force_unwrap.expr->moved_out = true;
            }
        }

        result = success_t;
        break;
    }

    case AST_AT_TIME:
    {
        Type *inner = check_expr(c, node->as.at_time.expr);
        result = inner;
        break;
    }

    case AST_AT_BENCH:
    {
        check_expr(c, node->as.at_bench.expr);
        result = type_f64();
        break;
    }

    case AST_RANGE:
    {
        Type *start = check_expr(c, node->as.range.start);
        Type *end = check_expr(c, node->as.range.end);
        if (start == NULL || end == NULL)
        {
            result = NULL;
            break;
        }
        if (!type_is_integer(start))
        {
            checker_error(c, node->as.range.start->line, node->as.range.start->column,
                          "range start must be integer, got '%s'", type_name(start));
            result = NULL;
            break;
        }
        if (!type_is_integer(end))
        {
            checker_error(c, node->as.range.end->line, node->as.range.end->column,
                          "range end must be integer, got '%s'", type_name(end));
            result = NULL;
            break;
        }
        /* Range expression's resolved_type is int (the element type) */
        result = type_int();
        break;
    }

    case AST_NEW_EXPR:
    {
        /* Look up the struct type. B-4: module-qualified literal `mod.Type{...}`
           resolves through the imported module's export table. */
        Type *st = NULL;
        /* Anonymous struct literal `{ field: val, ... }` (no type prefix): the
           parser left struct_name NULL. Infer the struct type from the expected
           type (LHS of a var-decl / return / arg slot). */
        if (node->as.new_expr.struct_name == NULL)
        {
            if (c->expected_type == NULL || c->expected_type->kind != TYPE_STRUCT)
            {
                checker_error(c, node->line, node->column,
                              "cannot infer struct type for `{...}` literal here "
                              "(no expected struct type in this context)");
                result = NULL;
                break;
            }
            st = c->expected_type;
            /* adopt the inferred name so downstream field lookup / codegen work */
            size_t snl = strlen(st->as.strukt.name);
            char *sdup = (char *)malloc_safe(snl + 1);
            memcpy(sdup, st->as.strukt.name, snl + 1);
            node->as.new_expr.struct_name = sdup;
        }
        else if (node->as.new_expr.module != NULL)
        {
            Symbol *modsym = scope_resolve(c->current_scope, node->as.new_expr.module);
            if (modsym == NULL || modsym->type == NULL ||
                modsym->type->kind != TYPE_MODULE)
            {
                checker_error(c, node->line, node->column,
                              "unknown module '%s' in '%s.%s{...}'",
                              node->as.new_expr.module, node->as.new_expr.module,
                              node->as.new_expr.struct_name);
                result = NULL;
                break;
            }
            Type *ex = type_module_find_export(modsym->type, node->as.new_expr.struct_name);
            if (ex == NULL || ex->kind != TYPE_STRUCT)
            {
                checker_error(c, node->line, node->column,
                              "module '%s' has no struct '%s'",
                              node->as.new_expr.module, node->as.new_expr.struct_name);
                result = NULL;
                break;
            }
            st = ex;
        }
        else
        {
            st = find_struct_type(c, node->as.new_expr.struct_name);
        }

        /* G1: If the parser provided explicit type_args (e.g. Pair(int,string){...}),
           resolve each arg and instantiate the generic struct template. */
        if (!st && node->as.new_expr.type_arg_count > 0)
        {
            int tac = node->as.new_expr.type_arg_count;
            Type **resolved_args = malloc(sizeof(Type *) * tac);
            bool args_ok = true;
            for (int i = 0; i < tac; i++) {
                resolved_args[i] = resolve_type_node(c, node->as.new_expr.type_args[i],
                    node->line, node->column);
                if (!resolved_args[i]) args_ok = false;
            }
            if (args_ok) {
                st = checker_instantiate_struct(c,
                    node->as.new_expr.struct_name,
                    resolved_args, tac,
                    node->line, node->column);
            }
            free(resolved_args);
        }

        /* G1: struct_name is the base name ("Pair"), but the instantiated type
           is registered under its mangled name ("Pair(int,string)").  Fall back
           to expected_type if the base name matches a generic template. */
        if (!st && c->expected_type && c->expected_type->kind == TYPE_STRUCT)
        {
            const char *sname = node->as.new_expr.struct_name;
            size_t slen = strlen(sname);
            const char *mangled = c->expected_type->as.strukt.name;
            /* Check: mangled starts with "sname(" */
            if (strncmp(mangled, sname, slen) == 0 && mangled[slen] == '(')
            {
                st = c->expected_type;
            }
        }

        if (!st)
        {
            checker_error(c, node->line, node->column,
                          "unknown struct type '%s'", node->as.new_expr.struct_name);
            result = NULL;
            break;
        }
        /* Type-check each field initializer */
        int ninits = node->as.new_expr.field_init_count;
        for (int i = 0; i < ninits; i++)
        {
            const char *fname = node->as.new_expr.field_inits[i].name;
            /* Check for duplicates */
            for (int j = 0; j < i; j++)
            {
                if (strcmp(node->as.new_expr.field_inits[j].name, fname) == 0)
                {
                    checker_error(c, node->line, node->column,
                                  "duplicate field initializer '%s'", fname);
                    goto new_expr_done;
                }
            }
            /* Find field in struct */
            int field_idx = -1;
            for (int j = 0; j < st->as.strukt.field_count; j++)
            {
                if (strcmp(st->as.strukt.fields[j].name, fname) == 0)
                {
                    field_idx = j;
                    break;
                }
            }
            if (field_idx < 0)
            {
                checker_error(c, node->line, node->column,
                              "struct '%s' has no field '%s'",
                              node->as.new_expr.struct_name, fname);
                goto new_expr_done;
            }
            /* Type-check the value; set expected_type so closure literals can
               infer their param/return types from the field's Block type. */
            Type *field_expected = st->as.strukt.fields[field_idx].type;
            checker_tag_user_from_list_literal(c, field_expected,
                node->as.new_expr.field_inits[i].value, "field list-literal");
            Type *saved_expected2 = c->expected_type;
            if (field_expected && (field_expected->kind == TYPE_BLOCK ||
                                   field_expected->kind == TYPE_STRUCT))
                c->expected_type = field_expected;
            Type *vt = check_expr(c, node->as.new_expr.field_inits[i].value);
            c->expected_type = saved_expected2;
            if (vt && !type_equals(vt, field_expected))
            {
                checker_error(c, node->as.new_expr.field_inits[i].value->line,
                              node->as.new_expr.field_inits[i].value->column,
                              "field '%s': expected '%s', got '%s'",
                              fname,
                              type_name(field_expected),
                              type_name(vt));
            }
            /* F.3: Block field value ownership transfers into the struct.
               Mark source identifier as moved so it cannot be used again. */
            if (vt && vt->kind == TYPE_BLOCK)
                checker_try_mark_moved(c, node->as.new_expr.field_inits[i].value);
        }
        /* Struct field defaults (v1): an omitted field with a declared default
           takes that default; an omitted field WITHOUT a default keeps LS's
           existing zero-initialization (struct literals never required all
           fields). Here we only type-check the defaults that exist. */
        for (int j = 0; j < st->as.strukt.field_count; j++)
        {
            bool provided = false;
            for (int i = 0; i < ninits; i++)
            {
                if (strcmp(node->as.new_expr.field_inits[i].name,
                           st->as.strukt.fields[j].name) == 0)
                {
                    provided = true;
                    break;
                }
            }
            if (provided)
                continue;
            AstNode *deflt = (AstNode *)st->as.strukt.fields[j].default_expr;
            if (deflt == NULL)
                continue; /* omitted, no default -> zero-init (existing semantics) */
            Type *fexp = st->as.strukt.fields[j].type;
            const char *jfn = st->as.strukt.fields[j].name;
            if (fexp && fexp->kind == TYPE_STRUCT &&
                     deflt->kind == AST_ARRAY_LIT &&
                     deflt->resolved_type == NULL)
            {
                checker_tag_user_from_list_literal(c, fexp, deflt,
                                                   "default list-literal");
            }
            Type *saved_def_exp = c->expected_type;
            if (fexp && (fexp->kind == TYPE_STRUCT || fexp->kind == TYPE_BLOCK))
                c->expected_type = fexp;
            Type *dt = check_expr(c, deflt);
            c->expected_type = saved_def_exp;
            if (dt && fexp && !type_equals(dt, fexp))
            {
                checker_error(c, node->line, node->column,
                              "default for field '%s': expected '%s', got '%s'",
                              jfn, type_name(fexp), type_name(dt));
            }
        }
    new_expr_done:
        /* on_stack = struct value literal  S1{...} → resolves to TYPE_STRUCT
           !on_stack = new S1{...} (heap) → resolves to *TYPE_STRUCT */
        result = node->as.new_expr.on_stack ? st : type_pointer(st);
        break;
    }

    /* Statements that can appear as expressions in match arms */
    case AST_BLOCK:
    {
        push_scope(c);
        Type *last = type_void();
        for (int i = 0; i < node->as.block.stmt_count; i++)
        {
            AstNode *s = node->as.block.stmts[i];
            if (i == node->as.block.stmt_count - 1 && s->kind == AST_EXPR_STMT)
            {
                last = check_expr(c, s->as.expr_stmt.expr);
            }
            else
            {
                check_stmt(c, s);
            }
        }
        pop_scope(c);
        result = last;
        break;
    }

    default:
        /* For non-expression nodes used as expressions, try treating as statement */
        check_stmt(c, node);
        result = type_void();
        break;
    }

    if (node)
    {
        if (c->expected_type && c->expected_type->kind == TYPE_BLOCK &&
            result && result->kind == TYPE_FUNCTION &&
            type_assignable(c->expected_type, result))
        {
            node->coerce_fn_to_block = true;
            type_free(node->coerce_block_type);
            node->coerce_block_type = type_clone(c->expected_type);
        }
        node->resolved_type = result;
    }
    return result;
}

/* ---- for-in iterator-protocol desugaring (docs/plan_userdef_for_in.md) ---- */

static char *foreach_strdup(const char *s)
{
    size_t n = strlen(s) + 1;
    char *p = (char *)malloc_safe(n);
    memcpy(p, s, n);
    return p;
}

static AstNode *foreach_mk_ident(const char *name, int line, int col)
{
    AstNode *n = ast_new(AST_IDENT, line, col);
    n->as.ident.name = foreach_strdup(name);
    return n;
}

/* recv.method()  — recv is consumed (owned by the returned node). */
static AstNode *foreach_mk_call0(AstNode *recv, const char *method, int line, int col)
{
    AstNode *fld = ast_new(AST_FIELD, line, col);
    fld->as.field_access.object = recv;
    fld->as.field_access.field = foreach_strdup(method);
    AstNode *call = ast_new(AST_CALL, line, col);
    call->as.call.callee = fld;
    call->as.call.args = NULL;
    call->as.call.arg_count = 0;
    call->as.call.type_args = NULL;
    call->as.call.type_arg_count = 0;
    return call;
}

/* `name = init`  — type-inferred local (var_type NULL). init is consumed. */
static AstNode *foreach_mk_let(const char *name, AstNode *init, int line, int col)
{
    AstNode *vd = ast_new(AST_VAR_DECL, line, col);
    vd->as.var_decl.var_type = NULL;
    vd->as.var_decl.name = foreach_strdup(name);
    vd->as.var_decl.init = init;
    return vd;
}

static AstNode *foreach_mk_block(AstNode **stmts, int n, int line, int col)
{
    AstNode *b = ast_new(AST_BLOCK, line, col);
    b->as.block.stmts = (AstNode **)malloc_safe((size_t)(n > 0 ? n : 1) * sizeof(AstNode *));
    for (int i = 0; i < n; i++) b->as.block.stmts[i] = stmts[i];
    b->as.block.stmt_count = n;
    return b;
}

/* Build the iterator-protocol equivalent of `for var in <iter>` and store it on
   node->as.for_stmt.desugared.  has_iter: the iter type exposes iter()->I (call
   it); otherwise the value is itself an iterator (drive next() directly).
   src_is_ident: the source is a bare variable (borrow in place) — otherwise the
   source is materialized into an owned __src local that outlives the loop. */
static int g_foreach_uid = 0;

static AstNode *build_foreach_desugar(AstNode *node, bool has_iter, bool src_is_ident)
{
    int line = node->line, col = node->column;
    const char *var = node->as.for_stmt.var;
    int uid = g_foreach_uid++;
    char itname[40], srcname[40];
    snprintf(itname, sizeof itname, "__it$%d", uid);
    snprintf(srcname, sizeof srcname, "__src$%d", uid);

    AstNode *outer[3];
    int oc = 0;

    AstNode *recv;
    if (src_is_ident)
    {
        recv = ast_clone_deep(node->as.for_stmt.iter);   /* IDENT(v) — borrow */
    }
    else
    {
        /* materialize the temporary so its buffer outlives the loop */
        outer[oc++] = foreach_mk_let(srcname,
                                     ast_clone_deep(node->as.for_stmt.iter), line, col);
        recv = foreach_mk_ident(srcname, line, col);
    }

    AstNode *it_init = has_iter ? foreach_mk_call0(recv, "iter", line, col) : recv;
    outer[oc++] = foreach_mk_let(itname, it_init, line, col);

    /* match __it.next() { Some(var) => {BODY}  None => { break } } */
    AstNode *next_call = foreach_mk_call0(foreach_mk_ident(itname, line, col), "next", line, col);

    AstNode *some_pat = ast_new(AST_CALL, line, col);
    some_pat->as.call.callee = foreach_mk_ident("Some", line, col);
    some_pat->as.call.args = (AstNode **)malloc_safe(sizeof(AstNode *));
    some_pat->as.call.args[0] = foreach_mk_ident(var, line, col);
    some_pat->as.call.arg_count = 1;
    some_pat->as.call.type_args = NULL;
    some_pat->as.call.type_arg_count = 0;

    AstNode *some_body = ast_clone_deep(node->as.for_stmt.body);

    AstNode *brk = ast_new(AST_BREAK, line, col);
    AstNode *none_body = foreach_mk_block(&brk, 1, line, col);
    AstNode *none_pat = foreach_mk_ident("None", line, col);

    AstNode *mtch = ast_new(AST_MATCH, line, col);
    mtch->as.match.subject = next_call;
    mtch->as.match.arms = (MatchArm *)malloc_safe(2 * sizeof(MatchArm));
    mtch->as.match.arms[0].pattern = some_pat;
    mtch->as.match.arms[0].body = some_body;
    mtch->as.match.arms[1].pattern = none_pat;
    mtch->as.match.arms[1].body = none_body;
    mtch->as.match.arm_count = 2;

    AstNode *match_stmt = ast_new(AST_EXPR_STMT, line, col);
    match_stmt->as.expr_stmt.expr = mtch;

    AstNode *while_body = foreach_mk_block(&match_stmt, 1, line, col);
    AstNode *cond = ast_new(AST_BOOL_LIT, line, col);
    cond->as.bool_lit.value = true;
    AstNode *whl = ast_new(AST_WHILE, line, col);
    whl->as.while_stmt.cond = cond;
    whl->as.while_stmt.body = while_body;

    outer[oc++] = whl;
    return foreach_mk_block(outer, oc, line, col);
}

/* ---- Statement checking ---- */

static void check_stmt(Checker *c, AstNode *node)
{
    if (node == NULL)
        return;

    switch (node->kind)
    {
    case AST_VAR_DECL:
    {
        /* Type-inferred local (var_type == NULL).  Only synthesized by the
           for-in desugarer (the parser always requires an explicit type); infer
           the type from the initializer and bind. */
        if (node->as.var_decl.var_type == NULL)
        {
            Type *it = node->as.var_decl.init
                           ? check_expr(c, node->as.var_decl.init)
                           : NULL;
            if (it == NULL)
                break;
            node->resolved_type = it;
            scope_define(c->current_scope, node->as.var_decl.name, it);
            break;
        }

        Type *declared = resolve_type_node(c, node->as.var_decl.var_type,
                                           node->line, node->column);
        if (declared == NULL)
            break;

        /* M-DEF: implicit empty/default init — `T v` ≡ `T v = {}` for any type
           where `= {}` is already a legal initializer (user containers like
           Vec/Map and struct zero-init via the empty-brace branch below).
           Synthesize an empty brace
           literal so the existing `= {}` paths run unchanged. POD/string/enum
           keep their current no-init behavior (their `{}` is not a legal init). */
        if (node->as.var_decl.init == NULL &&
            declared->kind == TYPE_STRUCT)
        {
            AstNode *empty = ast_new(AST_MAP_LIT, node->line, node->column);
            empty->as.map_lit.keys = NULL;
            empty->as.map_lit.vals = NULL;
            empty->as.map_lit.pair_count = 0;
            node->as.var_decl.init = empty;
        }

        if (node->as.var_decl.init)
        {
            if (declared && declared->kind == TYPE_STRUCT &&
                node->as.var_decl.init->kind == AST_ARRAY_LIT &&
                find_method(c, impl_key_of_type(declared), "__from_list") != NULL)
            {
                checker_tag_user_from_list_literal(c, declared,
                    node->as.var_decl.init, "list-literal");
            }
            /* M-LIT: `Map(K,V) m = { k: v, ... }` (non-empty) → user key-value
               literal via the __from_pairs protocol. */
            else if (declared && declared->kind == TYPE_STRUCT &&
                node->as.var_decl.init->kind == AST_MAP_LIT &&
                node->as.var_decl.init->as.map_lit.pair_count > 0 &&
                find_method(c, impl_key_of_type(declared), "__from_pairs") != NULL)
            {
                checker_tag_user_from_pairs_literal(c, declared,
                    node->as.var_decl.init, "map-literal");
            }
            else if (declared && declared->kind == TYPE_STRUCT &&
                node->as.var_decl.init->kind == AST_MAP_LIT &&
                node->as.var_decl.init->as.map_lit.pair_count == 0)
            {
                /* Inferred aggregate init: `Type v = {}` zero-initializes a struct
                   (C++-style), inferring the struct type from the declared LHS.
                   Reinterpret the empty brace literal (parsed as an empty map) as a
                   zero-init struct literal of `declared`. Unspecified fields are
                   zero (AST_NEW_EXPR codegen ConstNull's the whole struct first).
                   Lets `RawVec(string) v = {}` replace `new_rawvec(string)()`,
                   matching the builtin `vec(T) v = []`. */
                AstNode *ml = node->as.var_decl.init;
                /* free the (empty) map-lit arrays before repurposing the node */
                free(ml->as.map_lit.keys);
                free(ml->as.map_lit.vals);
                ml->kind = AST_NEW_EXPR;
                size_t snl = strlen(declared->as.strukt.name);
                char *sdup = (char *)malloc_safe(snl + 1);
                memcpy(sdup, declared->as.strukt.name, snl + 1);
                ml->as.new_expr.struct_name = sdup;
                ml->as.new_expr.module = NULL;
                ml->as.new_expr.field_inits = NULL;
                ml->as.new_expr.field_init_count = 0;
                ml->as.new_expr.on_stack = true;
                ml->as.new_expr.type_args = NULL;
                ml->as.new_expr.type_arg_count = 0;
                ml->resolved_type = declared;
            }
            else
            {
                /* Plumb expected_type so variant-ctor disambiguation can pick the
                   right enum (e.g. Some(42) when both Option(int) & Option(string)
                   are instantiated). */
                Type *saved_expected = c->expected_type;
                c->expected_type = declared;
                Type *init_type = check_expr(c, node->as.var_decl.init);
                c->expected_type = saved_expected;
                /* Migration bridge (B-step): a builtin-string value initializing a
                   `Str` is deep-copied into an owned Str. The literal case already
                   coerced (zero-copy) above; this catches string variables / string-
                   returning calls. */
                if (init_type != NULL && init_type->kind == TYPE_STRING &&
                    type_is_str_struct(declared) &&
                    !node->as.var_decl.init->coerce_str_lit_to_str)
                {
                    node->as.var_decl.init->coerce_string_to_str = true;
                }
                else if (init_type != NULL && !type_assignable(declared, init_type))
                {
                    checker_error(c, node->line, node->column,
                                  "cannot initialize '%s' (type '%s') with value of type '%s'",
                                  node->as.var_decl.name, type_name(declared), type_name(init_type));
                }
            }

            /* Move tracking (Phase 3 — struct): structs with has_drop follow move
               semantics just like strings. Source IDENT is marked below via
               checker_try_mark_moved; codegen still deep-clones, but the checker
               now rejects any subsequent use of the source. */
        }

        if (scope_resolve_local(c->current_scope, node->as.var_decl.name))
        {
            checker_error(c, node->line, node->column,
                          "variable '%s' already defined in this scope",
                          node->as.var_decl.name);
        }
        else
        {
            Symbol *new_sym = scope_define(c->current_scope, node->as.var_decl.name, declared);
            if (new_sym && declared->kind == TYPE_STRING)
            {
                /* Track whether this string is statically allocated (cap==0 at runtime).
                   Static: initialized from a string literal or from a known-static identifier.
                   Dynamic: all other initializers (method calls, concatenation, f-strings, etc.) */
                new_sym->is_static_string = string_expr_is_static(c, node->as.var_decl.init);
            }
            /* Phase 5.5: copying out of a writable borrow leaks ownership the
               caller still holds. Reject before move-tracking runs. */
            checker_reject_mut_borrow_copy_source(c, node->as.var_decl.init,
                                                 "initialize new variable from writable borrow");
            /* Phase 5.8: same for struct borrows. */
            checker_reject_struct_borrow_copy_source(c, node->as.var_decl.init,
                                                 "initialize new variable from struct borrow");
            /* F.2: Block parameters are shallow-copy borrows; cannot be moved out. */
            checker_reject_block_param_move(c, node->as.var_decl.init,
                                            "move Block into new variable");
            /* Phase G: copying a Block out of a struct field / vec element / map
               value is now allowed — codegen deep-clones the env (see
               cg_emit_block_env_clone), so the new variable owns an independent
               env with no shared-env double-free. (Former F.3/F.4A rejections.) */
            /* Move tracking: if the initializer is a dynamic string IDENT, the source is moved.
               Static strings, borrow params, and non-string types are left untouched
               (checker_try_mark_moved skips them). Reading a borrow into a new local
               yields a shallow copy with cap==0 at codegen — safe, no move. */
            checker_try_mark_moved(c, node->as.var_decl.init);
        }
        node->resolved_type = declared;
        break;
    }

    case AST_ASSIGN:
    {
        /* IndexMut protocol: `v[i] = x` where v is a struct opting in via
           `__index_set(&!self, int, E)` desugars to `v.__index_set(i, x)`. Must
           run BEFORE check_expr(target) (which would read-rewrite v[i] to
           __index). Reuses tobj/idxn/valn; rewrites the assign node into a call. */
        if (node->as.assign.target->kind == AST_INDEX)
        {
            AstNode *tobj = node->as.assign.target->as.index_expr.object;
            Type *to = check_expr(c, tobj);
            if (to && to->kind == TYPE_STRUCT &&
                find_method(c, impl_key_of_type(to), "__index_set") != NULL)
            {
                AstNode *idxn = node->as.assign.target->as.index_expr.index;
                AstNode *valn = node->as.assign.value;
                /* (the small AST_INDEX shell is intentionally leaked, not freed,
                   to avoid any aliasing with the union we overwrite below) */
                AstNode *call = make_index_protocol_call(node->line, node->column,
                                                         tobj, idxn, valn,
                                                         "__index_set");
                node->kind = AST_EXPR_STMT;
                node->as.expr_stmt.expr = call;
                check_expr(c, call);
                break;
            }
        }
        Type *target = check_expr(c, node->as.assign.target);
        /* Plumb expected_type = target type so a bare variant ctor on the RHS
           (`x = None`, `self.buf[i] = Some(v)`) disambiguates when several enum
           instantiations share the variant name (e.g. Option(int)+Option(string)). */
        Type *saved_exp_assign = c->expected_type;
        if (target) c->expected_type = target;
        Type *value = check_expr(c, node->as.assign.value);
        c->expected_type = saved_exp_assign;
        if (target == NULL || value == NULL)
            break;

        /* Phase 5: reassignment to a borrow parameter is forbidden — it holds
           no ownership, so overwriting would either leak (if new value is owned)
           or produce confusing "mutation visible to caller?" semantics. */
        if (node->as.assign.target->kind == AST_IDENT)
        {
            Symbol *tsym = scope_resolve(c->current_scope,
                                         node->as.assign.target->as.ident.name);
            if (tsym && tsym->is_borrow)
            {
                checker_move_error(c, node->line, node->column,
                                   "cannot assign to borrowed variable '%s' (read-only reference)",
                                   node->as.assign.target->as.ident.name);
                break;
            }
        }
        /* Phase 5.8: field assign `s.field = x` is forbidden when the base
           is a read-only struct borrow. Writable borrows pass through. */
        if (node->as.assign.target->kind == AST_FIELD)
        {
            AstNode *base = node->as.assign.target->as.field_access.object;
            if (base && base->kind == AST_IDENT)
            {
                Symbol *bsym = scope_resolve(c->current_scope, base->as.ident.name);
                if (bsym && bsym->is_borrow && bsym->type &&
                    bsym->type->kind == TYPE_STRUCT)
                {
                    checker_move_error(c, node->line, node->column,
                                       "cannot assign to '%s.%s': '%s' is a read-only borrow",
                                       base->as.ident.name,
                                       node->as.assign.target->as.field_access.field,
                                       base->as.ident.name);
                    break;
                }
            }
        }
        /* For compound assignments (+=, -=, etc.), check operand types */
        if (node->as.assign.op != TOKEN_ASSIGN)
        {
            /* string += string|char|int is allowed (in-place append) */
            bool str_append = (node->as.assign.op == TOKEN_PLUS_ASSIGN &&
                               target && target->kind == TYPE_STRING &&
                               value && (value->kind == TYPE_STRING ||
                                         value->kind == TYPE_CHAR ||
                                         type_is_integer(value)));
            if (!type_is_numeric(target) && !str_append)
            {
                checker_error(c, node->line, node->column,
                              "compound assignment requires numeric type, got '%s'",
                              type_name(target));
                break;
            }
            if (str_append) {
                /* `s += ...` turns a static string into an owned (dynamic) one at
                   runtime. Update is_static_string so subsequent move analysis
                   (e.g. vs.push(s)) correctly tracks ownership transfer. */
                if (node->as.assign.target->kind == AST_IDENT) {
                    Symbol *dst = scope_resolve(c->current_scope,
                                                node->as.assign.target->as.ident.name);
                    if (dst)
                        dst->is_static_string = false;
                }
                break; /* type-checked, no further assignability check needed */
            }
        }

        if (!type_assignable(target, value))
        {
            checker_error(c, node->line, node->column,
                          "cannot assign '%s' to '%s'",
                          type_name(value), type_name(target));
        }

        /* Move tracking for simple assignment (=) only.
           Compound assignments (+=, etc.) do not transfer ownership. */
        if (node->as.assign.op == TOKEN_ASSIGN)
        {
            /* Phase 5.5: reject RHS being a writable borrow (same rationale as
               var_decl — content cannot leave the borrow). */
            checker_reject_mut_borrow_copy_source(c, node->as.assign.value,
                                                  "assign writable borrow contents to another variable");
            /* Phase 5.8: same for struct borrows. */
            checker_reject_struct_borrow_copy_source(c, node->as.assign.value,
                                                 "assign struct borrow contents to another variable");
            /* F.2: Block parameters are shallow-copy borrows; cannot be moved out. */
            checker_reject_block_param_move(c, node->as.assign.value,
                                            "assign Block parameter to another variable");
            /* Phase G: assigning a Block out of a struct field / vec element / map
               value is now allowed — codegen deep-clones the env. (Former
               F.3/F.4A rejections.) */
            /* If RHS is a dynamic string/Block/movable IDENT, mark it as moved */
            checker_try_mark_moved(c, node->as.assign.value);

            /* Update is_static_string on the target variable to reflect its new value */
            if (node->as.assign.target->kind == AST_IDENT &&
                target != NULL && target->kind == TYPE_STRING)
            {
                Symbol *dst = scope_resolve(c->current_scope,
                                            node->as.assign.target->as.ident.name);
                if (dst)
                    dst->is_static_string = string_expr_is_static(c, node->as.assign.value);
            }
        }

        /* Struct assignment (Phase 3): structs with has_drop are treated as
           movable. Source IDENT is marked moved above via checker_try_mark_moved
           when the target is an AST_IDENT (full-variable reassignment).
           Field-level assignment (p.name = ...) has target->kind == AST_FIELD,
           which does NOT mark the surrounding struct 'p' as moved — only the
           rhs identifier (if any) is moved. */
        break;
    }

    case AST_RETURN:
    {
        if (c->current_fn_return == NULL)
        {
            /* Inference mode (map() return type): capture type instead of erroring. */
            if (c->closure_infer_return_slot && node->as.return_stmt.value)
            {
                bool saved_in_return = c->in_return_expr;
                c->in_return_expr = true;
                Type *val = check_expr(c, node->as.return_stmt.value);
                c->in_return_expr = saved_in_return;
                if (*c->closure_infer_return_slot == NULL)
                    *c->closure_infer_return_slot = val;
                break;
            }
            checker_error(c, node->line, node->column, "return outside of function");
            break;
        }
        if (node->as.return_stmt.value)
        {
            /* Mark as being in return expression - prevents move semantics on the returned var */
            bool saved_in_return = c->in_return_expr;
            c->in_return_expr = true;

            /* Plumb expected_type so bare variant ctors (e.g. `Err("msg")`) in
               `return` can disambiguate against the function's declared return
               type when several Result/Option instantiations are in scope. */
            Type *saved_expected = c->expected_type;
            c->expected_type = c->current_fn_return;
            Type *val = check_expr(c, node->as.return_stmt.value);
            c->expected_type = saved_expected;
            /* Migration bridge (B-2b): a builtin-string value returned from a
               function declared `-> Str` is deep-copied into an owned Str by
               codegen (the source string is dropped normally, not transferred).
               Literals already coerced zero-copy via expected_type above. */
            if (val != NULL && val->kind == TYPE_STRING &&
                type_is_str_struct(c->current_fn_return) &&
                !node->as.return_stmt.value->coerce_str_lit_to_str)
            {
                node->as.return_stmt.value->coerce_string_to_str = true;
            }
            else if (val != NULL && !type_assignable(c->current_fn_return, val))
            {
                checker_error(c, node->line, node->column,
                              "return type mismatch: expected '%s', got '%s'",
                              type_name(c->current_fn_return), type_name(val));
            }

            /* Mark returned identifier as is_returning (skip destructor) */
            if (node->as.return_stmt.value->kind == AST_IDENT)
            {
                Symbol *sym = scope_resolve(c->current_scope,
                                            node->as.return_stmt.value->as.ident.name);
                if (sym != NULL)
                {
                    sym->is_returning = true;
                }
            }

            c->in_return_expr = saved_in_return;
        }
        else
        {
            if (c->current_fn_return->kind != TYPE_VOID)
            {
                checker_error(c, node->line, node->column,
                              "return without value in function returning '%s'",
                              type_name(c->current_fn_return));
            }
        }
        break;
    }

    case AST_IF:
    {
        Type *cond = check_expr(c, node->as.if_stmt.cond);
        if (cond != NULL && cond->kind != TYPE_BOOL)
        {
            checker_error(c, node->as.if_stmt.cond->line, node->as.if_stmt.cond->column,
                          "if condition must be bool, got '%s'", type_name(cond));
        }

        /* Phase B: snapshot move state before each branch so we can merge at the join. */
        MoveSnapshot before_if;
        move_snap_capture(c, &before_if);

        check_stmt(c, node->as.if_stmt.then_block);

        if (node->as.if_stmt.else_block)
        {
            MoveSnapshot after_then;
            move_snap_capture(c, &after_then);

            /* Reset to pre-if state and check the else branch */
            move_snap_restore(&before_if);
            check_stmt(c, node->as.if_stmt.else_block);

            MoveSnapshot after_else;
            move_snap_capture(c, &after_else);

            /* Merge: MOVED∧MOVED → MOVED; otherwise any move contributes → MAYBE_MOVED */
            move_snap_merge_into_symbols(&after_then, &after_else);

            move_snap_free(&after_then);
            move_snap_free(&after_else);
        }
        else
        {
            /* No else branch: any move in the then-branch is only possible,
               so elevate LIVE→MOVED transitions to MAYBE_MOVED. */
            move_elevate_moves_to_maybe(&before_if);
        }
        move_snap_free(&before_if);
        break;
    }

    case AST_WHILE:
    {
        Type *cond = check_expr(c, node->as.while_stmt.cond);
        if (cond != NULL && cond->kind != TYPE_BOOL)
        {
            checker_error(c, node->as.while_stmt.cond->line, node->as.while_stmt.cond->column,
                          "while condition must be bool, got '%s'", type_name(cond));
        }

        /* Phase B: 2-pass analysis for loops.
           Pass 1 silently discovers all moves in the body; pass 2 pre-seeds those
           variables as MAYBE_MOVED and re-checks to report real errors. The loop
           may execute 0 times, so post-loop state is at best MAYBE_MOVED. */
        MoveSnapshot before_loop;
        move_snap_capture(c, &before_loop);

        bool saved_silent = c->silent_move_errors;
        c->silent_move_errors = true;
        check_stmt(c, node->as.while_stmt.body);
        c->silent_move_errors = saved_silent;

        MoveSnapshot after_pass1;
        move_snap_capture(c, &after_pass1);

        /* Restore to pre-loop, then pre-seed MAYBE_MOVED for anything moved in pass 1 */
        move_snap_restore(&before_loop);
        move_preseed_maybe_from_pass1(&before_loop, &after_pass1);

        /* Pass 2: real error reporting */
        check_stmt(c, node->as.while_stmt.body);

        /* Post-loop: anything that became MOVED in pass 2 is really MAYBE_MOVED
           since the loop may not execute at all. */
        move_elevate_moves_to_maybe(&before_loop);

        move_snap_free(&before_loop);
        move_snap_free(&after_pass1);
        break;
    }

    case AST_FOR:
    {
        Type *iter = check_expr(c, node->as.for_stmt.iter);

        /* Iterator-protocol path: a struct that exposes iter()->I or is itself an
           iterator (has next()) is desugared into the equivalent while/match loop
           and that subtree is checked instead (docs/plan_userdef_for_in.md). */
        if (iter != NULL && iter->kind == TYPE_STRUCT)
        {
            const char *key = impl_key_of_type(iter);
            bool has_iter = key && find_method(c, key, "iter") != NULL;
            bool has_next = key && find_method(c, key, "next") != NULL;
            if (has_iter || has_next)
            {
                bool src_is_ident = (node->as.for_stmt.iter->kind == AST_IDENT);
                AstNode *d = build_foreach_desugar(node, has_iter, src_is_ident);
                node->as.for_stmt.desugared = d;
                check_stmt(c, d);
                break;
            }
            /* fall through to the generic "not iterable" error below */
        }

        push_scope(c);
        if (iter != NULL)
        {
            if (node->as.for_stmt.iter->kind == AST_RANGE)
            {
                /* Range iteration: loop variable is int */
                scope_define(c->current_scope, node->as.for_stmt.var, type_int());
            }
            else if (iter->kind == TYPE_ARRAY)
            {
                /* Array iteration: loop variable is element type */
                scope_define(c->current_scope, node->as.for_stmt.var, iter->as.array.elem);
            }
            else if (type_is_integer(iter))
            {
                /* Single integer: iterate 0..n */
                scope_define(c->current_scope, node->as.for_stmt.var, type_int());
            }
            else
            {
                checker_error(c, node->as.for_stmt.iter->line,
                              node->as.for_stmt.iter->column,
                              "cannot iterate over '%s'; expected range (a..b), array, "
                              "integer, or a type with an iter()->Iterator(T) / next()->Option(T) method",
                              type_name(iter));
            }
        }
        /* Phase B: 2-pass analysis for the foreach body */
        {
            MoveSnapshot before_loop;
            move_snap_capture(c, &before_loop);

            bool saved_silent = c->silent_move_errors;
            c->silent_move_errors = true;
            check_stmt(c, node->as.for_stmt.body);
            c->silent_move_errors = saved_silent;

            MoveSnapshot after_pass1;
            move_snap_capture(c, &after_pass1);

            move_snap_restore(&before_loop);
            move_preseed_maybe_from_pass1(&before_loop, &after_pass1);

            check_stmt(c, node->as.for_stmt.body);

            move_elevate_moves_to_maybe(&before_loop);

            move_snap_free(&before_loop);
            move_snap_free(&after_pass1);
        }
        pop_scope(c);
        break;
    }

    case AST_FOR_C:
    {
        /* C-style for: for (init; cond; update) { body }
           All three clauses are optional. */
        push_scope(c);
        if (node->as.for_c_stmt.init)
        {
            check_stmt(c, node->as.for_c_stmt.init);
        }
        if (node->as.for_c_stmt.cond)
        {
            Type *cond = check_expr(c, node->as.for_c_stmt.cond);
            if (cond != NULL && cond->kind != TYPE_BOOL)
            {
                checker_error(c, node->as.for_c_stmt.cond->line,
                              node->as.for_c_stmt.cond->column,
                              "for condition must be bool, got '%s'", type_name(cond));
            }
        }

        /* Phase B: 2-pass analysis for the body + update (both repeat). */
        {
            MoveSnapshot before_loop;
            move_snap_capture(c, &before_loop);

            bool saved_silent = c->silent_move_errors;
            c->silent_move_errors = true;
            check_stmt(c, node->as.for_c_stmt.body);
            if (node->as.for_c_stmt.update)
                check_stmt(c, node->as.for_c_stmt.update);
            c->silent_move_errors = saved_silent;

            MoveSnapshot after_pass1;
            move_snap_capture(c, &after_pass1);

            move_snap_restore(&before_loop);
            move_preseed_maybe_from_pass1(&before_loop, &after_pass1);

            check_stmt(c, node->as.for_c_stmt.body);
            if (node->as.for_c_stmt.update)
                check_stmt(c, node->as.for_c_stmt.update);

            move_elevate_moves_to_maybe(&before_loop);

            move_snap_free(&before_loop);
            move_snap_free(&after_pass1);
        }
        pop_scope(c);
        break;
    }

    case AST_BLOCK:
    {
        push_scope(c);
        for (int i = 0; i < node->as.block.stmt_count; i++)
        {
            check_stmt(c, node->as.block.stmts[i]);
        }
        pop_scope(c);
        break;
    }

    case AST_EXPR_STMT:
        check_expr(c, node->as.expr_stmt.expr);
        break;

    case AST_BREAK:
    case AST_CONTINUE:
        break;

    default:
        /* Declarations or expressions — dispatch */
        check_decl(c, node);
        break;
    }
}

/* ---- Declaration checking (top-level pass) ---- */

/* G2: register a generic function template */
static void register_fn_template(Checker *c, AstNode *node) {
    if (c->fn_template_count >= c->fn_template_cap) {
        c->fn_template_cap = c->fn_template_cap < 4 ? 4 : c->fn_template_cap * 2;
        c->fn_templates = realloc_safe(c->fn_templates,
            (size_t)c->fn_template_cap * sizeof(c->fn_templates[0]));
    }
    int idx = c->fn_template_count++;
    c->fn_templates[idx].name = node->as.fn_decl.name;
    c->fn_templates[idx].type_params = node->as.fn_decl.type_params;
    c->fn_templates[idx].type_param_count = node->as.fn_decl.type_param_count;
    c->fn_templates[idx].decl_node = node;
}

/* G2: find a generic function template by name */
static int find_fn_template(Checker *c, const char *name) {
    for (int i = 0; i < c->fn_template_count; i++) {
        if (strcmp(c->fn_templates[i].name, name) == 0)
            return i;
    }
    return -1;
}

/* Function parameter defaults (档1): attach the fn_decl's literal defaults to
   the function type, require defaulted params be trailing, and type-check each
   default against its param type. `params` are the resolved param types. */
static void attach_param_defaults(Checker *c, AstNode *node, Type *fn_type, Type **params)
{
    int n = node->as.fn_decl.param_count;
    if (!node->as.fn_decl.param_defaults || n <= 0)
        return;
    fn_type->as.function.param_defaults = (void **)malloc_safe((size_t)n * sizeof(void *));
    bool seen_default = false;
    for (int i = 0; i < n; i++)
    {
        AstNode *pd = node->as.fn_decl.param_defaults[i];
        fn_type->as.function.param_defaults[i] = pd;
        if (pd != NULL)
            seen_default = true;
        else if (seen_default)
            checker_error(c, node->line, node->column,
                "parameter '%s' (no default) must come before parameters with defaults",
                node->as.fn_decl.param_names[i]);
        if (pd != NULL && params && params[i])
        {
            Type *fpt = params[i];
            Type *saved_pexp = c->expected_type;
            if (fpt->kind == TYPE_STRUCT || fpt->kind == TYPE_BLOCK)
                c->expected_type = fpt;
            Type *dt = check_expr(c, pd);
            c->expected_type = saved_pexp;
            if (dt && !type_equals(dt, fpt))
                checker_error(c, pd->line, pd->column,
                    "default for parameter '%s': expected '%s', got '%s'",
                    node->as.fn_decl.param_names[i], type_name(fpt), type_name(dt));
        }
    }
}

static void check_fn_decl(Checker *c, AstNode *node)
{
    /* G2: skip generic function templates — registered in forward_pass,
       instantiated on demand at call sites. */
    if (node->as.fn_decl.type_param_count > 0)
        return;

    /* Phase A1: top-level functions (no impl_struct_name) cannot declare a
       self parameter; only methods inside `impl Struct { ... }` may. */
    if (node->as.fn_decl.self_borrow_kind != 0 &&
        node->as.fn_decl.impl_struct_name == NULL)
    {
        checker_error(c, node->line, node->column,
                      "&%sself is only valid as the first parameter of a method "
                      "inside an `impl` block",
                      node->as.fn_decl.self_borrow_kind == 2 ? "!" : "");
    }
    int n = node->as.fn_decl.param_count;
    Type **params = NULL;
    if (n > 0)
    {
        params = (Type **)malloc_safe((size_t)n * sizeof(Type *));
        for (int i = 0; i < n; i++)
        {
            params[i] = resolve_type_node(c, node->as.fn_decl.param_types[i],
                                          node->line, node->column);
            /* array(T,N) must be passed by pointer */
            if (params[i])
            {
                if (params[i]->kind == TYPE_ARRAY)
                {
                    checker_error(c, node->line, node->column,
                                  "parameter '%s': array must be passed by pointer",
                                  node->as.fn_decl.param_names[i]);
                }
            }
        }
    }
    Type *ret = resolve_type_node(c, node->as.fn_decl.return_type,
                                  node->line, node->column);
    Type *fn_type = type_function(params, n, ret, false);
    /* param defaults already attached in forward_pass (the type calls resolve to). */

    /* Define function in current scope */
    if (!scope_define(c->current_scope, node->as.fn_decl.name, fn_type))
    {
        checker_error(c, node->line, node->column,
                      "function '%s' already defined", node->as.fn_decl.name);
    }

    /* Check body */
    push_scope(c);
    for (int i = 0; i < n; i++)
    {
        if (params[i])
        {
            /* For &T borrow parameters, the symbol's effective type is the pointee.
               The TYPE_REFERENCE wrapper stays only in the function's signature
               (params[i] of the TYPE_FUNCTION) so call-site matching remains exact. */
            Type *sym_type = params[i];
            bool is_borrow = false;
            bool is_mut_borrow = false;
            if (sym_type->kind == TYPE_REFERENCE)
            {
                if (sym_type->is_mut) is_mut_borrow = true;
                else                  is_borrow     = true;
                sym_type = sym_type->as.pointer_to;
            }
            Symbol *param_sym = scope_define(c->current_scope,
                                             node->as.fn_decl.param_names[i], sym_type);
            if (param_sym)
            {
                param_sym->is_borrow = is_borrow;
                param_sym->is_mut_borrow = is_mut_borrow;
                /* String parameters are deep copies of the caller's value — always dynamic.
                   Borrow / mut-borrow params: is_static_string stays false. */
                if (sym_type->kind == TYPE_STRING)
                    param_sym->is_static_string = false;
                /* F.2: Block params are shallow copies (caller and callee share env_ptr).
                   Mark as borrow so checker_reject_block_param_move catches F1 h = g. */
                if (sym_type->kind == TYPE_BLOCK)
                    param_sym->is_borrow = true;
            }
        }
    }
    Type *saved_ret = c->current_fn_return;
    c->current_fn_return = ret;
    check_stmt(c, node->as.fn_decl.body);
    c->current_fn_return = saved_ret;
    pop_scope(c);

    node->resolved_type = fn_type;
}

static void check_struct_decl(Checker *c, AstNode *node)
{
    const char *name = node->as.struct_decl.name;
    int tpc = node->as.struct_decl.type_param_count;

    /* G1: Generic struct → register as template, skip field checking */
    if (tpc > 0)
    {
        register_struct_template(c, name,
                                 node->as.struct_decl.type_params, tpc,
                                 node);
        return;
    }

    int n = node->as.struct_decl.field_count;

    /* Check for duplicate struct */
    if (find_struct_type(c, name))
    {
        checker_error(c, node->line, node->column,
                      "struct '%s' already defined", name);
        return;
    }

    Type *st = type_struct(name, n);
    /* B-2: set LLVM-level type name for module-defined structs */
    st->as.strukt.llvm_name = checker_module_type_llvmname(c, name);
    for (int i = 0; i < n; i++)
    {
        Type *ft = resolve_type_node(c, node->as.struct_decl.field_types[i],
                                     node->line, node->column);
        /* Copy field name */
        const char *fn = node->as.struct_decl.field_names[i];
        size_t len = strlen(fn);
        char *fn_copy = (char *)malloc_safe(len + 1);
        memcpy(fn_copy, fn, len + 1);
        st->as.strukt.fields[i].name = fn_copy;
        st->as.strukt.fields[i].type = ft;
        st->as.strukt.fields[i].default_expr =
            node->as.struct_decl.field_defaults ? node->as.struct_decl.field_defaults[i] : NULL;

        /* Check for duplicate fields */
        for (int j = 0; j < i; j++)
        {
            if (strcmp(st->as.strukt.fields[j].name, fn) == 0)
            {
                checker_error(c, node->line, node->column,
                              "duplicate field '%s' in struct '%s'", fn, name);
            }
        }
    }

    /* Auto-set has_drop if a field owns heap: string /
       has_drop-struct / has_drop-enum / Block. */
    bool needs_drop = false;
    for (int i = 0; i < n && !needs_drop; i++)
    {
        Type *ft = st->as.strukt.fields[i].type;
        if (ft->kind == TYPE_STRING)
        {
            needs_drop = true;
        }
        else if (ft->kind == TYPE_STRUCT && ft->as.strukt.has_drop)
        {
            needs_drop = true;
        }
        else if (ft->kind == TYPE_ENUM && ft->as.enom.has_drop)
        {
            needs_drop = true;
        }
        else if (ft->kind == TYPE_BLOCK)
        {
            needs_drop = true;
        }
    }
    if (needs_drop)
    {
        st->as.strukt.has_drop = true;
        /* Register compiler-generated __drop method in impl_registry so it's callable */
        int impl_idx = find_or_create_impl(c, impl_key_of_type(st));  /* B-4.1 */
        Type *drop_ret = type_void();
        /* Allocate params on heap (not stack) to avoid dangling pointer */
        Type **drop_params = (Type **)malloc_safe(sizeof(Type *));
        drop_params[0] = type_pointer(st); /* *Struct self */
        Type *drop_type = type_function(drop_params, 1, drop_ret, false);
        register_method(c, impl_idx, "__drop", drop_type, false, 0,
                        node->line, node->column);
        /* Also define in global scope for free function call */
        scope_define(c->current_scope, "__drop", drop_type);
    }

    register_struct_type(c, name, st);
    node->resolved_type = st;
}

/* Helper: returns true if the given type owns heap memory and therefore
   contributes to a containing aggregate's has_drop flag. */
static bool type_owns_heap_for_enum(const Type *t)
{
    if (t == NULL) return false;
    switch (t->kind)
    {
    case TYPE_STRING: return true;
    case TYPE_STRUCT: return t->as.strukt.has_drop;
    case TYPE_ENUM:   return t->as.enom.has_drop;
    default:          return false;
    }
}

static void check_enum_decl(Checker *c, AstNode *node)
{
    const char *name = node->as.enum_decl.name;
    int n = node->as.enum_decl.variant_count;

    /* Reject duplicate enum / struct name */
    if (find_enum_type(c, name) || find_struct_type(c, name))
    {
        checker_error(c, node->line, node->column,
                      "type '%s' already defined", name);
        return;
    }

    Type *et = type_enum(name, n);
    /* B-2: set LLVM-level type name for module-defined enums */
    et->as.enom.llvm_name = checker_module_type_llvmname(c, name);
    bool has_drop = false;

    /* Pre-register the enum type so that resolve_type_node can find it
       when resolving indirect self-references like vec(EnumName) or
       map(string, EnumName).  has_drop is updated after the loop. */
    register_enum_type(c, name, et);

    /* Pass A: copy ALL variant names first (with duplicate check).  This must
       happen before any payload type is resolved: resolving a generic-struct
       payload (e.g. Vec(int)) recursively type-checks that struct's methods,
       which can call find_variant() and iterate THIS still-being-built enum's
       variants.  If a later variant's name were still NULL at that point,
       find_variant's strcmp would dereference NULL → crash. */
    for (int i = 0; i < n; i++)
    {
        const char *vn = node->as.enum_decl.variants[i].name;
        for (int j = 0; j < i; j++)
        {
            if (strcmp(node->as.enum_decl.variants[j].name, vn) == 0)
            {
                checker_error(c, node->line, node->column,
                              "duplicate variant '%s' in enum '%s'", vn, name);
            }
        }
        size_t vlen = strlen(vn);
        char *vn_copy = (char *)malloc_safe(vlen + 1);
        memcpy(vn_copy, vn, vlen + 1);
        et->as.enom.variants[i].name = vn_copy;
    }

    /* Pass B: resolve payload types for each variant. */
    for (int i = 0; i < n; i++)
    {
        int pc = node->as.enum_decl.variants[i].payload_count;
        et->as.enom.variants[i].payload_count = pc;
        if (pc > 0)
        {
            et->as.enom.variants[i].payload_types =
                (Type **)malloc_safe((size_t)pc * sizeof(Type *));
            for (int j = 0; j < pc; j++)
            {
                TypeNode *ptn = node->as.enum_decl.variants[i].payload_types[j];
                Type *pt = NULL;
                /* Self-recursive payload (Tree { Node(int, Tree, Tree) }):
                   detect by name match and bind to the type being defined.
                   The resulting type is treated as a boxed self-pointer at
                   codegen time. */
                if (ptn && ptn->kind == TYPE_NODE_NAMED &&
                    ptn->as.named.arg_count == 0 &&
                    strcmp(ptn->as.named.name, name) == 0)
                {
                    pt = et;
                    has_drop = true;  /* self-recursive payload requires drop */
                }
                else
                {
                    pt = resolve_type_node(c, ptn, node->line, node->column);
                }
                et->as.enom.variants[i].payload_types[j] = pt;
                if (type_owns_heap_for_enum(pt))
                    has_drop = true;
            }
        }
    }

    et->as.enom.has_drop = has_drop;
    /* register_enum_type was already called before the loop (pre-registration
       for indirect self-reference support); has_drop is now final. */
    node->resolved_type = et;
}

/* Check if a method call matches "self.field.__drop()" pattern.
   Used to warn when user-defined __drop() doesn't call member __drop(). */
static bool has_member_drop_call(AstNode *node, Type *struct_type)
{
    if (node == NULL || struct_type == NULL)
        return false;
    if (struct_type->kind != TYPE_STRUCT)
        return false;

    if (node->kind == AST_CALL &&
        node->as.call.callee->kind == AST_FIELD)
    {
        AstNode *callee = node->as.call.callee;
        /* callee is "self.field.__drop" or similar */
        if (callee->as.field_access.object->kind == AST_FIELD)
        {
            AstNode *obj = callee->as.field_access.object;
            /* Check if this is "self.field.__drop()" */
            if (obj->as.field_access.object->kind == AST_IDENT &&
                strcmp(obj->as.field_access.object->as.ident.name, "self") == 0)
            {
                const char *field_name = obj->as.field_access.field;
                for (int i = 0; i < struct_type->as.strukt.field_count; i++)
                {
                    if (strcmp(struct_type->as.strukt.fields[i].name, field_name) == 0)
                    {
                        Type *field_type = struct_type->as.strukt.fields[i].type;
                        /* Accept if field has has_drop (user-defined or compiler-generated) */
                        if (field_type && field_type->kind == TYPE_STRUCT &&
                            field_type->as.strukt.has_drop)
                        {
                            if (strcmp(callee->as.field_access.field, "__drop") == 0)
                            {
                                return true;
                            }
                        }
                    }
                }
            }
        }
    }

    /* Recursively check nested expressions */
    switch (node->kind)
    {
    case AST_BLOCK:
        for (int i = 0; i < node->as.block.stmt_count; i++)
        {
            if (has_member_drop_call(node->as.block.stmts[i], struct_type))
            {
                return true;
            }
        }
        break;
    case AST_IF:
        if (has_member_drop_call(node->as.if_stmt.then_block, struct_type))
            return true;
        if (node->as.if_stmt.else_block &&
            has_member_drop_call(node->as.if_stmt.else_block, struct_type))
            return true;
        break;
    case AST_WHILE:
        if (has_member_drop_call(node->as.while_stmt.body, struct_type))
            return true;
        break;
    case AST_FOR:
        if (has_member_drop_call(node->as.for_stmt.body, struct_type))
            return true;
        break;
    case AST_EXPR_STMT:
        if (has_member_drop_call(node->as.expr_stmt.expr, struct_type))
            return true;
        break;
    case AST_RETURN:
        if (has_member_drop_call(node->as.return_stmt.value, struct_type))
            return true;
        break;
    default:
        break;
    }
    return false;
}

static void check_impl_decl(Checker *c, AstNode *node)
{
    const char *name = node->as.impl_decl.name;

    /* G1.5: generic impl — bind to struct template, defer method checking
       to instantiation time (Step 8/9). */
    if (node->as.impl_decl.type_param_count > 0)
    {
        int tidx = find_struct_template_idx(c, name);
        if (tidx < 0) {
            checker_error(c, node->line, node->column,
                          "impl(T) for undefined generic struct '%s'", name);
            return;
        }
        c->struct_templates[tidx].impl_node = node;
        return;
    }

    Type *st = find_struct_type(c, name);
    Type *et = NULL;
    bool is_enum_impl = false;
    bool is_builtin_impl = false;  /* Phase 2.5: impl on a builtin type (string) */
    Type *builtin_self = NULL;
    if (st == NULL)
    {
        et = find_enum_type(c, name);
        if (et == NULL)
        {
            /* Phase 2.5: allow `impl string` (and future builtin types).
               resolve_builtin_type returns a fresh Type for known names. */
            builtin_self = resolve_builtin_type_by_name(name);
            if (builtin_self == NULL)
            {
                checker_error(c, node->line, node->column,
                              "impl for undefined type '%s'", name);
                return;
            }
            if (builtin_self->kind != TYPE_STRING)
            {
                checker_error(c, node->line, node->column,
                              "impl on builtin type '%s' is not yet supported "
                              "(only 'string' for now)", name);
                return;
            }
            is_builtin_impl = true;
        }
        else
        {
            is_enum_impl = true;
        }
    }

    /* Set Self context so resolve_type_node can resolve 'Self'
       (struct and enum are mutually exclusive). */
    Type *saved_impl_st = c->current_impl_struct_type;
    Type *saved_impl_et = c->current_impl_enum_type;
    c->current_impl_struct_type = (is_enum_impl || is_builtin_impl) ? NULL : st;
    c->current_impl_enum_type   = is_enum_impl ? et : NULL;
    Type *self_type = is_builtin_impl ? builtin_self : (st ? st : et);

    /* B-4.1: key the impl_registry by the type's unique name (llvm_name for module
       types) so same-named impls across modules don't collide. */
    int impl_idx = find_or_create_impl(c, impl_key_of_type(self_type));

    for (int i = 0; i < node->as.impl_decl.method_count; i++)
    {
        AstNode *method = node->as.impl_decl.methods[i];
        if (method->kind != AST_FN_DECL)
            continue;

        bool is_static = method->as.fn_decl.is_static;
        int user_n = method->as.fn_decl.param_count;

        /* Resolve user-declared param types */
        Type **user_params = NULL;
        if (user_n > 0)
        {
            user_params = (Type **)malloc_safe((size_t)user_n * sizeof(Type *));
            for (int j = 0; j < user_n; j++)
            {
                user_params[j] = resolve_type_node(c, method->as.fn_decl.param_types[j],
                                                   method->line, method->column);
                /* array(T,N) must be passed by pointer */
                if (user_params[j])
                {
                    if (user_params[j]->kind == TYPE_ARRAY)
                    {
                        checker_error(c, method->line, method->column,
                                      "parameter '%s': array must be passed by pointer",
                                      method->as.fn_decl.param_names[j]);
                    }
                }
            }
        }
        Type *ret = resolve_type_node(c, method->as.fn_decl.return_type,
                                      method->line, method->column);

        /* For instance methods: internal function type has an extra first param (*Struct).
           The user doesn't write 'self' — the compiler injects it. */
        int total_n;
        Type **all_params;
        if (!is_static)
        {
            total_n = user_n + 1;
            all_params = (Type **)malloc_safe((size_t)total_n * sizeof(Type *));
            all_params[0] = type_pointer(self_type); /* implicit *Self */
            for (int j = 0; j < user_n; j++)
            {
                all_params[j + 1] = user_params[j];
            }
            free(user_params);
        }
        else
        {
            total_n = user_n;
            all_params = user_params;
        }

        Type *method_type = type_function(all_params, total_n, ret, false);

        if (!register_method(c, impl_idx, method->as.fn_decl.name, method_type, is_static,
                        method->as.fn_decl.self_borrow_kind,
                        method->line, method->column))
            continue;

        /* Also define in global scope so it can be called as a free function */
        scope_define(c->current_scope, method->as.fn_decl.name, method_type);

        /* Check body */
        push_scope(c);
        if (!is_static)
        {
            int sbk = method->as.fn_decl.self_borrow_kind;
            /* Phase B: drop struct &self / &!self now allowed. */
            if (sbk == 0)
            {
                /* Legacy: self is *Self pointer (mut-style). */
                scope_define(c->current_scope, "self", type_pointer(self_type));
            }
            else
            {
                /* &self / &!self: self is Self with borrow flags. */
                Symbol *self_sym = scope_define(c->current_scope, "self", self_type);
                if (self_sym)
                {
                    if (sbk == 1) self_sym->is_borrow = true;
                    else if (sbk == 2) self_sym->is_mut_borrow = true;
                }
            }
        }
        else if (method->as.fn_decl.self_borrow_kind != 0)
        {
            /* `static fn m(&self/&!self ...)` is contradictory. */
            checker_error(c, method->line, method->column,
                          "static method '%s' cannot declare a self parameter",
                          method->as.fn_decl.name);
        }
        for (int j = 0; j < user_n; j++)
        {
            Type *pt = is_static ? all_params[j] : all_params[j + 1];
            if (pt)
            {
                /* Phase 5: unwrap &T / &!T → T; remember borrow kind on body symbol. */
                bool is_borrow = false;
                bool is_mut_borrow = false;
                if (pt->kind == TYPE_REFERENCE)
                {
                    if (pt->is_mut) is_mut_borrow = true;
                    else            is_borrow     = true;
                    pt = pt->as.pointer_to;
                }
                Symbol *param_sym = scope_define(c->current_scope,
                                                 method->as.fn_decl.param_names[j], pt);
                if (param_sym)
                {
                    param_sym->is_borrow = is_borrow;
                    param_sym->is_mut_borrow = is_mut_borrow;
                    /* String method parameters receive deep copies — always dynamic */
                    if (pt->kind == TYPE_STRING)
                        param_sym->is_static_string = false;
                    /* F.2: Block params are shallow-copy borrows of caller's env */
                    if (pt->kind == TYPE_BLOCK)
                        param_sym->is_borrow = true;
                }
            }
        }
        Type *saved_ret = c->current_fn_return;
        bool saved_in_drop = c->in_user_defined_drop;
        c->current_fn_return = ret;
        /* If this is a user-defined __drop method, set the flag */
        if (!is_static && strcmp(method->as.fn_decl.name, "__drop") == 0)
        {
            c->in_user_defined_drop = true;
        }
        check_stmt(c, method->as.fn_decl.body);
        c->current_fn_return = saved_ret;
        c->in_user_defined_drop = saved_in_drop;
        pop_scope(c);

        method->resolved_type = method_type;

        /* Check for __drop destructor method */
        if (!is_static &&
            strcmp(method->as.fn_decl.name, "__drop") == 0)
        {
            if (is_enum_impl)
            {
                checker_error(c, method->line, method->column,
                              "enum '%s' cannot have a user-defined __drop method",
                              name);
            }
            else if (is_builtin_impl)
            {
                checker_error(c, method->line, method->column,
                              "builtin type '%s' cannot have a user-defined "
                              "__drop method", name);
            }
            else
            {
                /* __drop must be an instance method with no user parameters and void return */
                if (user_n != 0)
                {
                    checker_error(c, method->line, method->column,
                                  "__drop() must have no parameters (self is implicit)");
                }
                if (ret->kind != TYPE_VOID)
                {
                    checker_error(c, method->line, method->column,
                                  "__drop() must return void");
                }
                /* Note: For user-defined __drop with nested struct fields, we don't require
                   explicit self.field.__drop() calls because:
                   1. If nested struct has compiler-generated __drop, it's not in impl_registry
                   2. Compiler will auto-handle nested struct cleanup after user's __drop runs
                */
                /* Mark struct as having a destructor */
                st->as.strukt.has_drop = true;
                st->as.strukt.has_user_drop = true;
            }
        }
    }

    /* Restore Self context */
    c->current_impl_struct_type = saved_impl_st;
    c->current_impl_enum_type   = saved_impl_et;
}

static void check_extern_fn(Checker *c, AstNode *node)
{
    int n = node->as.extern_fn.param_count;
    Type **params = NULL;
    if (n > 0)
    {
        params = (Type **)malloc_safe((size_t)n * sizeof(Type *));
        for (int i = 0; i < n; i++)
        {
            params[i] = resolve_type_node(c, node->as.extern_fn.param_types[i],
                                          node->line, node->column);
        }
    }
    Type *ret = resolve_type_node(c, node->as.extern_fn.return_type,
                                  node->line, node->column);
    Type *fn_type = type_function(params, n, ret, node->as.extern_fn.is_vararg);

    if (!scope_define(c->current_scope, node->as.extern_fn.name, fn_type))
    {
        checker_error(c, node->line, node->column,
                      "extern function '%s' already defined", node->as.extern_fn.name);
    }
    node->resolved_type = fn_type;
}

/* Returns true iff type t is valid as an extern struct field (C-ABI compatible). */
static bool type_is_c_compatible(const Type *t)
{
    if (t == NULL) return false;
    switch (t->kind)
    {
    case TYPE_INT: case TYPE_I8:  case TYPE_I16: case TYPE_I32: case TYPE_I64:
    case TYPE_U8:  case TYPE_U16: case TYPE_U32: case TYPE_U64:
    case TYPE_F32: case TYPE_F64: case TYPE_BOOL:
    case TYPE_OBJECT:  /* void* */
        return true;
    case TYPE_POINTER: /* *T — any pointer is C-compatible */
        return true;
    case TYPE_STRUCT:
        return t->as.strukt.is_extern_c; /* only extern struct, not LS struct */
    default:
        return false; /* string/vec/map/enum/etc. */
    }
}

static void check_extern_struct_decl(Checker *c, AstNode *node)
{
    const char *name = node->as.extern_struct_decl.name;
    int n = node->as.extern_struct_decl.field_count;

    if (find_struct_type(c, name))
    {
        checker_error(c, node->line, node->column,
                      "extern struct '%s' already defined", name);
        return;
    }

    Type *st = type_struct(name, n);
    st->as.strukt.is_extern_c = true;
    /* extern structs have no LS drop — C manages the memory */
    st->as.strukt.has_drop = false;

    for (int i = 0; i < n; i++)
    {
        Type *ft = resolve_type_node(c, node->as.extern_struct_decl.field_types[i],
                                     node->line, node->column);
        if (ft == NULL) continue;

        if (!type_is_c_compatible(ft))
        {
            checker_error(c, node->line, node->column,
                          "extern struct '%s' field '%s' has non-C-compatible type '%s';"
                          " only primitive / pointer / extern struct types are allowed",
                          name, node->as.extern_struct_decl.field_names[i],
                          type_name(ft));
        }

        const char *fn = node->as.extern_struct_decl.field_names[i];
        size_t len = strlen(fn);
        char *fn_copy = (char *)malloc_safe(len + 1);
        memcpy(fn_copy, fn, len + 1);
        st->as.strukt.fields[i].name = fn_copy;
        st->as.strukt.fields[i].type = ft;

        for (int j = 0; j < i; j++)
        {
            if (strcmp(st->as.strukt.fields[j].name, fn) == 0)
            {
                checker_error(c, node->line, node->column,
                              "duplicate field '%s' in extern struct '%s'", fn, name);
            }
        }
    }

    register_struct_type(c, name, st);
    node->resolved_type = st;
}

static void check_extern_block(Checker *c, AstNode *node)
{
    for (int i = 0; i < node->as.extern_block.decl_count; i++)
    {
        AstNode *d = node->as.extern_block.decls[i];
        if (d->kind == AST_EXTERN_STRUCT_DECL)
            check_extern_struct_decl(c, d);
        else if (d->kind == AST_EXTERN_FN)
            check_extern_fn(c, d);
    }
}

static void check_load_lib(Checker *c, AstNode *node)
{
    if (!scope_define(c->current_scope, node->as.load_lib.var_name, type_lib()))
    {
        checker_error(c, node->line, node->column,
                      "library '%s' already defined", node->as.load_lib.var_name);
    }
    node->resolved_type = type_lib();
}

/* ---- Trait registry helpers ---- */

static int find_trait(Checker *c, const char *name)
{
    for (int i = 0; i < c->trait_count; i++)
    {
        if (strcmp(c->trait_registry[i].name, name) == 0)
            return i;
    }
    return -1;
}

/* ---- Operator overloading: built-in operator traits ---- */

/* The 7 soft-reserved built-in operator traits. */
static bool is_builtin_operator_trait(const char *name)
{
    return strcmp(name, "Add") == 0 || strcmp(name, "Sub") == 0 ||
           strcmp(name, "Mul") == 0 || strcmp(name, "Div") == 0 ||
           strcmp(name, "Rem") == 0 || strcmp(name, "Eq")  == 0 ||
           strcmp(name, "Ord") == 0;
}

/* Given an internal operator method name ($op_*), return the built-in trait that
   declares it, or NULL if mname is not an operator method. */
static const char *operator_trait_for_method(const char *mname)
{
    if (mname == NULL || mname[0] != '$') return NULL;
    if (strcmp(mname, "$op_add") == 0) return "Add";
    if (strcmp(mname, "$op_sub") == 0) return "Sub";
    if (strcmp(mname, "$op_mul") == 0) return "Mul";
    if (strcmp(mname, "$op_div") == 0) return "Div";
    if (strcmp(mname, "$op_rem") == 0) return "Rem";
    if (strcmp(mname, "$op_eq") == 0 || strcmp(mname, "$op_ne") == 0) return "Eq";
    if (strcmp(mname, "$op_lt") == 0 || strcmp(mname, "$op_gt") == 0 ||
        strcmp(mname, "$op_le") == 0 || strcmp(mname, "$op_ge") == 0) return "Ord";
    return NULL;
}

/* Map an internal operator method name back to its source symbol (for diagnostics). */
static const char *operator_symbol_for_method(const char *mname)
{
    if (strcmp(mname, "$op_add") == 0) return "+";
    if (strcmp(mname, "$op_sub") == 0) return "-";
    if (strcmp(mname, "$op_mul") == 0) return "*";
    if (strcmp(mname, "$op_div") == 0) return "/";
    if (strcmp(mname, "$op_rem") == 0) return "%";
    if (strcmp(mname, "$op_eq") == 0)  return "==";
    if (strcmp(mname, "$op_ne") == 0)  return "!=";
    if (strcmp(mname, "$op_lt") == 0)  return "<";
    if (strcmp(mname, "$op_gt") == 0)  return ">";
    if (strcmp(mname, "$op_le") == 0)  return "<=";
    if (strcmp(mname, "$op_ge") == 0)  return ">=";
    return mname;
}

/* Comparison operators that may be omitted from an Eq/Ord impl (derived from ==/<). */
static bool is_optional_operator_method(const char *mname)
{
    return strcmp(mname, "$op_ne") == 0 || strcmp(mname, "$op_gt") == 0 ||
           strcmp(mname, "$op_le") == 0 || strcmp(mname, "$op_ge") == 0;
}

/* Register one built-in operator trait. Every method is `fn OP(&self, &Self rhs)`;
   ret_is_bool selects comparison (-> bool) vs arithmetic (-> Self). */
static void add_builtin_op_trait(Checker *c, const char *name,
                                 const char *const *methods, int method_count,
                                 bool ret_is_bool)
{
    if (c->trait_count >= c->trait_cap)
    {
        c->trait_cap = GROW_CAPACITY(c->trait_cap);
        c->trait_registry = realloc_safe(c->trait_registry,
                                         (size_t)c->trait_cap * sizeof(c->trait_registry[0]));
    }
    int idx = c->trait_count++;
    {
        size_t len = strlen(name) + 1;
        char *dup = (char *)malloc_safe(len);
        memcpy(dup, name, len);
        c->trait_registry[idx].name = dup;
    }
    c->trait_registry[idx].method_count = method_count;
    c->trait_registry[idx].methods = (void *)
        malloc_safe((size_t)method_count * sizeof(c->trait_registry[idx].methods[0]));
    for (int i = 0; i < method_count; i++)
    {
        size_t nlen = strlen(methods[i]) + 1;
        char *ndup = (char *)malloc_safe(nlen);
        memcpy(ndup, methods[i], nlen);
        Type **params = (Type **)malloc_safe(sizeof(Type *));
        params[0] = type_reference(&g_self_placeholder_type);            /* &Self rhs */
        Type *ret = ret_is_bool ? type_bool() : &g_self_placeholder_type; /* bool | Self */
        c->trait_registry[idx].methods[i].name = ndup;
        c->trait_registry[idx].methods[i].type = type_function(params, 1, ret, false);
        c->trait_registry[idx].methods[i].self_borrow_kind = 1; /* &self */
    }
}

/* Pre-register the 7 built-in operator traits into the trait registry, before any
   user declarations. A user `trait Add {}` then collides via the duplicate check. */
static void register_builtin_operator_traits(Checker *c)
{
    static const char *const m_add[] = {"$op_add"};
    static const char *const m_sub[] = {"$op_sub"};
    static const char *const m_mul[] = {"$op_mul"};
    static const char *const m_div[] = {"$op_div"};
    static const char *const m_rem[] = {"$op_rem"};
    static const char *const m_eq[]  = {"$op_eq", "$op_ne"};
    static const char *const m_ord[] = {"$op_lt", "$op_gt", "$op_le", "$op_ge"};
    add_builtin_op_trait(c, "Add", m_add, 1, false);
    add_builtin_op_trait(c, "Sub", m_sub, 1, false);
    add_builtin_op_trait(c, "Mul", m_mul, 1, false);
    add_builtin_op_trait(c, "Div", m_div, 1, false);
    add_builtin_op_trait(c, "Rem", m_rem, 1, false);
    add_builtin_op_trait(c, "Eq",  m_eq,  2, true);
    add_builtin_op_trait(c, "Ord", m_ord, 4, true);
}

/* Build `obj.method(arg)` as a fresh AST_CALL. obj/arg are deep-cloned so the
   synthesized tree owns its subtrees (freed by ordinary ast_free recursion). */
static AstNode *op_make_call(AstNode *obj_src, const char *method,
                             AstNode *arg_src, int line, int col)
{
    AstNode *fld = ast_new(AST_FIELD, line, col);
    fld->as.field_access.object = ast_clone_deep(obj_src);
    size_t mlen = strlen(method) + 1;
    char *mdup = (char *)malloc_safe(mlen);
    memcpy(mdup, method, mlen);
    fld->as.field_access.field = mdup;

    AstNode *call = ast_new(AST_CALL, line, col);
    call->as.call.callee = fld;
    call->as.call.args = (AstNode **)malloc_safe(sizeof(AstNode *));
    call->as.call.args[0] = ast_clone_deep(arg_src);
    call->as.call.arg_count = 1;
    call->as.call.type_args = NULL;
    call->as.call.type_arg_count = 0;
    return call;
}

/* Build `!(inner)`. Takes ownership of inner. */
static AstNode *op_make_not(AstNode *inner, int line, int col)
{
    AstNode *u = ast_new(AST_UNARY, line, col);
    u->as.unary.op = TOKEN_BANG;
    u->as.unary.operand = inner;
    return u;
}

static bool try_operator_overload(Checker *c, AstNode *node, Type *left, Type *right,
                                  Type **out_result)
{
    (void)right;
    *out_result = NULL;

    /* Only struct/enum VALUE operands overload. Pointer operands (e.g. `*Point p`,
       `p == nil`) keep builtin pointer-identity comparison — do NOT deref. Borrow
       params (`&self`, `&Vec2 rhs`) are already presented as value types by the
       checker, so no deref is needed for them either. */
    Type *lt = left;
    if (lt == NULL || (lt->kind != TYPE_STRUCT && lt->kind != TYPE_ENUM))
        return false;  /* not an overload site — let the builtin path handle it */

    /* Map operator → (trait, primary method, derivation kind).
       derive: 0 none, 1 !=(neg-eq), 2 >(swap-lt), 3 <=(neg-swap-lt), 4 >=(neg-lt). */
    const char *trait = NULL, *prim = NULL;
    int derive = 0;
    switch (node->as.binary.op)
    {
    case TOKEN_PLUS:    trait = "Add"; prim = "$op_add"; break;
    case TOKEN_MINUS:   trait = "Sub"; prim = "$op_sub"; break;
    case TOKEN_STAR:    trait = "Mul"; prim = "$op_mul"; break;
    case TOKEN_SLASH:   trait = "Div"; prim = "$op_div"; break;
    case TOKEN_PERCENT: trait = "Rem"; prim = "$op_rem"; break;
    case TOKEN_EQ:      trait = "Eq";  prim = "$op_eq";  break;
    case TOKEN_NEQ:     trait = "Eq";  prim = "$op_ne";  derive = 1; break;
    case TOKEN_LT:      trait = "Ord"; prim = "$op_lt";  break;
    case TOKEN_GT:      trait = "Ord"; prim = "$op_gt";  derive = 2; break;
    case TOKEN_LEQ:     trait = "Ord"; prim = "$op_le";  derive = 3; break;
    case TOKEN_GEQ:     trait = "Ord"; prim = "$op_ge";  derive = 4; break;
    default:
        /* Bitwise/shift/logical on struct/enum are not overloadable; let the
           builtin path emit its "requires integer/numeric" error. */
        return false;
    }

    if (!checker_type_satisfies_trait(c, lt, trait))
    {
        checker_error(c, node->line, node->column,
                      "type '%s' does not implement trait '%s' (required for operator '%s')",
                      type_name(lt), trait, operator_symbol_for_method(prim));
        return true;  /* recognized overload site, reported */
    }

    const char *key = impl_key_of_type(lt);
    if (key == NULL) key = type_name(lt);

    AstNode *lhs = node->as.binary.left;
    AstNode *rhs = node->as.binary.right;
    int line = node->line, col = node->column;

    /* Prefer an explicit method when present (covers required ops and any
       explicit override of a derivable comparison). */
    bool have_prim = (find_method(c, key, prim) != NULL);
    AstNode *lowered = NULL;
    if (derive == 0 || have_prim)
    {
        lowered = op_make_call(lhs, prim, rhs, line, col);
    }
    else
    {
        switch (derive)
        {
        case 1: /* a != b  ->  !(a $op_eq b) */
            lowered = op_make_not(op_make_call(lhs, "$op_eq", rhs, line, col), line, col);
            break;
        case 2: /* a > b   ->  b $op_lt a */
            lowered = op_make_call(rhs, "$op_lt", lhs, line, col);
            break;
        case 3: /* a <= b  ->  !(b $op_lt a) */
            lowered = op_make_not(op_make_call(rhs, "$op_lt", lhs, line, col), line, col);
            break;
        case 4: /* a >= b  ->  !(a $op_lt b) */
            lowered = op_make_not(op_make_call(lhs, "$op_lt", rhs, line, col), line, col);
            break;
        }
    }

    /* Type-check the synthesized expression (resolves dispatch, borrows, sret). */
    Type *t = check_expr(c, lowered);
    if (t == NULL)
    {
        ast_free(lowered);  /* inner check already reported a precise error */
        return true;
    }
    node->as.binary.lowered = lowered;
    *out_result = t;
    return true;
}

/* Check whether a concrete type satisfies a trait constraint by looking up trait_impls. */
static bool checker_type_satisfies_trait(Checker *c, Type *type, const char *trait_name)
{
    if (type == NULL) return false;
    if (strcmp(trait_name, "Eq") == 0)
    {
        if (type->kind == TYPE_STRING || type->kind == TYPE_BOOL ||
            type_is_numeric(type) || type_is_pointer_like(type))
            return true;
    }
    else if (strcmp(trait_name, "Ord") == 0)
    {
        if (type->kind == TYPE_STRING || type_is_numeric(type))
            return true;
    }
    const char *tname = type_name(type);
    for (int i = 0; i < c->trait_impl_count; i++)
    {
        if (strcmp(c->trait_impls[i].trait_name, trait_name) == 0 &&
            strcmp(c->trait_impls[i].struct_name, tname) == 0)
        {
            return true;
        }
    }
    return false;
}

/* Check a trait declaration: resolve method signatures and register in trait_registry. */
static void check_trait_decl(Checker *c, AstNode *node)
{
    const char *name = node->as.trait_decl.name;

    /* Check for duplicate trait name */
    if (find_trait(c, name) >= 0)
    {
        if (is_builtin_operator_trait(name))
            checker_error(c, node->line, node->column,
                          "'%s' is a built-in operator trait and cannot be redefined", name);
        else
            checker_error(c, node->line, node->column,
                          "trait '%s' already defined", name);
        return;
    }

    /* Grow registry if needed */
    if (c->trait_count >= c->trait_cap)
    {
        c->trait_cap = GROW_CAPACITY(c->trait_cap);
        c->trait_registry = realloc_safe(c->trait_registry,
                                          (size_t)c->trait_cap * sizeof(c->trait_registry[0]));
    }

    int idx = c->trait_count++;
    {
        size_t len = strlen(name) + 1;
        char *dup = (char *)malloc_safe(len);
        memcpy(dup, name, len);
        c->trait_registry[idx].name = dup;
    }
    c->trait_registry[idx].method_count = 0;
    c->trait_registry[idx].methods = NULL;

    int sig_count = node->as.trait_decl.method_sig_count;
    if (sig_count == 0) return;

    /* Set Self placeholder so resolve_type_node("Self") works in trait sigs */
    Type *saved_impl_st = c->current_impl_struct_type;
    c->current_impl_struct_type = &g_self_placeholder_type;

    c->trait_registry[idx].methods = (void *)
        malloc_safe((size_t)sig_count * sizeof(c->trait_registry[idx].methods[0]));

    for (int i = 0; i < sig_count; i++)
    {
        AstNode *sig = node->as.trait_decl.method_sigs[i];
        if (sig == NULL || sig->kind != AST_FN_DECL) continue;

        /* Resolve parameter types */
        int n = sig->as.fn_decl.param_count;
        Type **params = NULL;
        if (n > 0)
        {
            params = (Type **)malloc_safe((size_t)n * sizeof(Type *));
            for (int j = 0; j < n; j++)
            {
                params[j] = resolve_type_node(c, sig->as.fn_decl.param_types[j],
                                               sig->line, sig->column);
            }
        }
        Type *ret = resolve_type_node(c, sig->as.fn_decl.return_type,
                                       sig->line, sig->column);
        Type *fn_type = type_function(params, n, ret, false);

        int mi = c->trait_registry[idx].method_count++;
        {
            size_t nlen = strlen(sig->as.fn_decl.name) + 1;
            char *ndup = (char *)malloc_safe(nlen);
            memcpy(ndup, sig->as.fn_decl.name, nlen);
            c->trait_registry[idx].methods[mi].name = ndup;
        }
        c->trait_registry[idx].methods[mi].type = fn_type;
        c->trait_registry[idx].methods[mi].self_borrow_kind = sig->as.fn_decl.self_borrow_kind;
    }

    /* Restore Self context */
    c->current_impl_struct_type = saved_impl_st;
}

/* Check an `impl Trait for Struct { ... }` block:
   verify trait exists, struct exists, method signatures match the trait,
   then register methods into impl_registry and record the trait-impl pair. */
static void check_impl_trait_decl(Checker *c, AstNode *node)
{
    const char *trait_name  = node->as.impl_trait_decl.trait_name;
    const char *struct_name = node->as.impl_trait_decl.struct_name;

    /* 1. Find the trait */
    int tidx = find_trait(c, trait_name);
    if (tidx < 0)
    {
        checker_error(c, node->line, node->column,
                      "unknown trait '%s'", trait_name);
        return;
    }

    /* 2. Find the target type (struct or builtin) */
    Type *st = find_struct_type(c, struct_name);
    if (st == NULL)
    {
        /* Step 11: fallback to builtin type names (int, f64, string, ...) */
        st = resolve_builtin_type_by_name(struct_name);
    }
    if (st == NULL)
    {
        checker_error(c, node->line, node->column,
                      "impl trait for undefined type '%s'", struct_name);
        return;
    }

    /* Step 10: Self context — so resolve_type_node("Self") → st */
    Type *saved_impl_st = c->current_impl_struct_type;
    c->current_impl_struct_type = st;

    int user_method_count = node->as.impl_trait_decl.method_count;
    int trait_method_count = c->trait_registry[tidx].method_count;

    /* 3. Check for duplicate impl */
    for (int i = 0; i < c->trait_impl_count; i++)
    {
        if (strcmp(c->trait_impls[i].trait_name, trait_name) == 0 &&
            strcmp(c->trait_impls[i].struct_name, struct_name) == 0)
        {
            checker_error(c, node->line, node->column,
                          "trait '%s' already implemented for struct '%s'",
                          trait_name, struct_name);
            return;
        }
    }

    /* 4. Build a matched[] array to track which trait methods are covered */
    bool *matched = (bool *)malloc_safe((size_t)trait_method_count * sizeof(bool));
    memset(matched, 0, (size_t)trait_method_count * sizeof(bool));

    /* B-4.1: key by the type's unique name (module-prefixed) when available;
       builtins (impl trait for int) keep their bare name. */
    const char *impl_trait_key = impl_key_of_type(st);
    if (impl_trait_key == NULL) impl_trait_key = struct_name;
    int impl_idx = find_or_create_impl(c, impl_trait_key);

    /* 5. For each user method, resolve types and match against trait signature */
    for (int i = 0; i < user_method_count; i++)
    {
        AstNode *method = node->as.impl_trait_decl.methods[i];
        if (method == NULL || method->kind != AST_FN_DECL) continue;

        const char *mname = method->as.fn_decl.name;
        bool is_static = method->as.fn_decl.is_static;
        int user_sbk = method->as.fn_decl.self_borrow_kind;

        /* Operator overloading: a $op_* method (parsed from `fn +`, `fn ==`, ...)
           is only valid when this trait is the matching built-in operator trait.
           Catches `impl Add for T { fn == }` and `impl UserTrait for T { fn + }`. */
        if (mname[0] == '$')
        {
            const char *want = operator_trait_for_method(mname);
            if (want == NULL || strcmp(want, trait_name) != 0)
            {
                checker_error(c, method->line, method->column,
                              "operator method '%s' is only valid when implementing built-in trait '%s'",
                              operator_symbol_for_method(mname), want ? want : "<operator>");
                continue;
            }
        }

        /* Static methods are not allowed in trait impls */
        if (is_static)
        {
            checker_error(c, method->line, method->column,
                          "static method '%s' not allowed in trait impl", mname);
            continue;
        }

        /* Find matching trait method by name */
        int trait_mi = -1;
        for (int j = 0; j < trait_method_count; j++)
        {
            if (strcmp(c->trait_registry[tidx].methods[j].name, mname) == 0)
            {
                trait_mi = j;
                break;
            }
        }
        if (trait_mi < 0)
        {
            checker_error(c, method->line, method->column,
                          "method '%s' is not declared in trait '%s'",
                          mname, trait_name);
            continue;
        }
        if (matched[trait_mi])
        {
            checker_error(c, method->line, method->column,
                          "duplicate implementation of method '%s'", mname);
            continue;
        }
        matched[trait_mi] = true;

        /* Check self_borrow_kind matches */
        int trait_sbk = c->trait_registry[tidx].methods[trait_mi].self_borrow_kind;
        if (user_sbk != trait_sbk)
        {
            const char *expected_str = trait_sbk == 1 ? "&self" : (trait_sbk == 2 ? "&!self" : "no self");
            const char *got_str = user_sbk == 1 ? "&self" : (user_sbk == 2 ? "&!self" : "no self");
            checker_error(c, method->line, method->column,
                          "method '%s' self parameter mismatch: trait '%s' requires %s, got %s",
                          mname, trait_name, expected_str, got_str);
        }

        /* Resolve user parameter types */
        int user_n = method->as.fn_decl.param_count;
        Type **user_params = NULL;
        if (user_n > 0)
        {
            user_params = (Type **)malloc_safe((size_t)user_n * sizeof(Type *));
            for (int j = 0; j < user_n; j++)
            {
                user_params[j] = resolve_type_node(c, method->as.fn_decl.param_types[j],
                                                     method->line, method->column);
            }
        }
        Type *ret = resolve_type_node(c, method->as.fn_decl.return_type,
                                        method->line, method->column);

        /* Compare parameter count and types against trait signature.
           The trait signature does NOT include the implicit *Self param —
           it stores only user-visible params (same as what parser gives). */
        Type *trait_fn = c->trait_registry[tidx].methods[trait_mi].type;
        int trait_n = trait_fn->as.function.param_count;
        if (user_n != trait_n)
        {
            checker_error(c, method->line, method->column,
                          "method '%s' parameter count mismatch: trait '%s' requires %d, got %d",
                          mname, trait_name, trait_n, user_n);
        }
        else
        {
            for (int j = 0; j < user_n; j++)
            {
                if (user_params[j] && trait_fn->as.function.params[j] &&
                    !type_equals_with_self(trait_fn->as.function.params[j], user_params[j], st))
                {
                    checker_error(c, method->line, method->column,
                                  "method '%s' parameter %d type mismatch in trait '%s'",
                                  mname, j + 1, trait_name);
                }
            }
        }

        /* Compare return type (Self placeholder → st) */
        Type *trait_ret = trait_fn->as.function.return_type;
        if (ret && trait_ret && !type_equals_with_self(trait_ret, ret, st))
        {
            checker_error(c, method->line, method->column,
                          "method '%s' return type mismatch in trait '%s'",
                          mname, trait_name);
        }

        /* Build the internal function type with *Self prepended (instance method) */
        int total_n = user_n + 1;
        Type **all_params = (Type **)malloc_safe((size_t)total_n * sizeof(Type *));
        all_params[0] = type_pointer(st); /* implicit *Self */
        for (int j = 0; j < user_n; j++)
            all_params[j + 1] = user_params[j];
        free(user_params);

        Type *method_type = type_function(all_params, total_n, ret, false);

        /* Register in impl_registry (same as check_impl_decl) */
        if (!register_method(c, impl_idx, mname, method_type, false, user_sbk,
                           method->line, method->column))
            continue;

        /* Register in impl_registry (same as check_impl_decl) */
        scope_define(c->current_scope, mname, method_type);

        /* Check body (same pattern as check_impl_decl) */
        push_scope(c);
        {
            int sbk = method->as.fn_decl.self_borrow_kind;
            if (sbk == 0)
            {
                scope_define(c->current_scope, "self", type_pointer(st));
            }
            else
            {
                Symbol *self_sym = scope_define(c->current_scope, "self", st);
                if (self_sym)
                {
                    if (sbk == 1) self_sym->is_borrow = true;
                    else if (sbk == 2) self_sym->is_mut_borrow = true;
                }
            }
        }
        for (int j = 0; j < user_n; j++)
        {
            Type *pt = all_params[j + 1];
            if (pt)
            {
                bool is_borrow = false;
                bool is_mut_borrow = false;
                if (pt->kind == TYPE_REFERENCE)
                {
                    if (pt->is_mut) is_mut_borrow = true;
                    else            is_borrow     = true;
                    pt = pt->as.pointer_to;
                }
                Symbol *param_sym = scope_define(c->current_scope,
                                                   method->as.fn_decl.param_names[j], pt);
                if (param_sym)
                {
                    param_sym->is_borrow = is_borrow;
                    param_sym->is_mut_borrow = is_mut_borrow;
                    if (pt->kind == TYPE_STRING)
                        param_sym->is_static_string = false;
                    if (pt->kind == TYPE_BLOCK)
                        param_sym->is_borrow = true;
                }
            }
        }
        Type *saved_ret = c->current_fn_return;
        c->current_fn_return = ret;
        check_stmt(c, method->as.fn_decl.body);
        c->current_fn_return = saved_ret;
        pop_scope(c);

        method->resolved_type = method_type;
    }

    /* 6. Check for missing methods */
    for (int j = 0; j < trait_method_count; j++)
    {
        if (!matched[j])
        {
            const char *tmname = c->trait_registry[tidx].methods[j].name;
            /* Eq/Ord derivable comparison operators (!=, >, <=, >=) are optional —
               they are synthesized from == / < when not explicitly provided. */
            if (is_builtin_operator_trait(trait_name) && is_optional_operator_method(tmname))
                continue;
            const char *disp = is_builtin_operator_trait(trait_name)
                                   ? operator_symbol_for_method(tmname) : tmname;
            checker_error(c, node->line, node->column,
                          "struct '%s' does not implement trait '%s': missing method '%s'",
                          struct_name, trait_name, disp);
        }
    }
    free(matched);

    /* 7. Register trait impl */
    if (c->trait_impl_count >= c->trait_impl_cap)
    {
        c->trait_impl_cap = GROW_CAPACITY(c->trait_impl_cap);
        c->trait_impls = realloc_safe(c->trait_impls,
                                        (size_t)c->trait_impl_cap * sizeof(c->trait_impls[0]));
    }
    int ti = c->trait_impl_count++;
    c->trait_impls[ti].trait_name = trait_name;   /* points into AST (not owned) */
    c->trait_impls[ti].struct_name = struct_name; /* points into AST (not owned) */

    /* Restore Self context */
    c->current_impl_struct_type = saved_impl_st;
}

/* M-H: register a single imported `trait`/`impl Trait for T` decl into importer
   `c`. For traits → trait_registry (deduped by find_trait). For trait-impls →
   methods into impl_registry (so `x.foo()` dispatches) + the (trait,type) pair
   into trait_impls (so `where K: Foo` is satisfied). Builtin targets (int/string)
   key on the bare name; user structs/enums on their module-unique llvm_name.
   `mod_type` supplies the source module's export table for user-type keying. */
static void register_one_imported_trait_decl(Checker *c, AstNode *d, Type *mod_type)
{
    if (d->kind == AST_TRAIT_DECL)
    {
        if (find_trait(c, d->as.trait_decl.name) < 0)
            check_trait_decl(c, d);
        return;
    }
    if (d->kind != AST_IMPL_TRAIT_DECL)
        return;

    const char *tr_name = d->as.impl_trait_decl.trait_name;
    const char *ty_name = d->as.impl_trait_decl.struct_name;

    /* Idempotency (B-MAP-M5-003): if this (trait, type) pair was already
       registered — e.g. a diamond import where both `import std.map` and another
       module that transitively imports it propagate `impl Hash for int` — skip
       the whole thing. Crucially this guards register_method() below, which is
       NOT idempotent and would otherwise error "conflicting method 'hash'". The
       methods were already registered when the pair was first recorded. */
    for (int ii = 0; ii < c->trait_impl_count; ii++)
    {
        if (strcmp(c->trait_impls[ii].trait_name, tr_name) == 0 &&
            strcmp(c->trait_impls[ii].struct_name, ty_name) == 0)
            return;
    }

    const char *impl_key = ty_name;
    Type *impl_st = mod_type ? type_module_find_export(mod_type, ty_name) : NULL;
    if (impl_st == NULL) impl_st = find_struct_type(c, ty_name);
    if (impl_st == NULL) impl_st = resolve_builtin_type_by_name(ty_name);
    if (impl_st)
    {
        const char *k = impl_key_of_type(impl_st);
        if (k) impl_key = k;
    }
    int it_impl_idx = find_or_create_impl(c, impl_key);
    for (int mi = 0; mi < d->as.impl_trait_decl.method_count; mi++)
    {
        AstNode *method = d->as.impl_trait_decl.methods[mi];
        if (method == NULL || method->kind != AST_FN_DECL)
            continue;
        if (method->resolved_type == NULL)
            continue;
        register_method(c, it_impl_idx, method->as.fn_decl.name,
                        method->resolved_type,
                        method->as.fn_decl.is_static,
                        method->as.fn_decl.self_borrow_kind,
                        method->line, method->column);
    }
    /* Record the trait-impl pair (so the dedup above fires on re-import). */
    if (c->trait_impl_count >= c->trait_impl_cap)
    {
        c->trait_impl_cap = GROW_CAPACITY(c->trait_impl_cap);
        c->trait_impls = realloc_safe(c->trait_impls,
            (size_t)c->trait_impl_cap * sizeof(c->trait_impls[0]));
    }
    int ti2 = c->trait_impl_count++;
    c->trait_impls[ti2].trait_name = tr_name;
    c->trait_impls[ti2].struct_name = ty_name;
}

/* M-H: recursively register the traits/trait-impls of an imported module AND its
   own transitive imports. Needed because user-defined trait bounds (e.g. Hash)
   used inside a stdlib generic must be satisfiable at the call site even when the
   user imported only the outer module (user → std.map → std.hash). Builtin
   operator traits (Eq/Ord) bypass this. `visited`/`vcount` guard diamonds/cycles. */
static void propagate_imported_traits(Checker *c, const char *import_path,
                                      const char **visited, int *vcount)
{
    if (import_path == NULL || c->registry == NULL) return;
    for (int i = 0; i < *vcount; i++)
        if (visited[i] && strcmp(visited[i], import_path) == 0) return;
    if (*vcount < 64) visited[(*vcount)++] = import_path;

    /* Builtin modules expose no user-defined traits. */
    if (builtin_module_exists(import_path) &&
        !module_user_file_exists(import_path, c->source_path))
        return;

    ModuleInfo *mod = module_load(c->registry, import_path, c->source_path);
    if (mod == NULL || mod->ast == NULL) return;
    AstNode *mod_ast = mod->ast;

    /* Export table for user-type impl-trait keying (builtins bypass it). */
    Type *mod_type = type_module_new(import_path);
    for (int j = 0; j < mod_ast->as.program.decl_count; j++)
    {
        AstNode *d = mod_ast->as.program.decls[j];
        if (d->kind == AST_STRUCT_DECL && d->resolved_type)
            type_module_add_export(mod_type, d->as.struct_decl.name, d->resolved_type);
        else if (d->kind == AST_ENUM_DECL && d->resolved_type)
            type_module_add_export(mod_type, d->as.enum_decl.name, d->resolved_type);
    }

    for (int j = 0; j < mod_ast->as.program.decl_count; j++)
    {
        AstNode *d = mod_ast->as.program.decls[j];
        if (d->kind == AST_TRAIT_DECL || d->kind == AST_IMPL_TRAIT_DECL)
            register_one_imported_trait_decl(c, d, mod_type);
        else if (d->kind == AST_IMPORT_DECL)
            propagate_imported_traits(c, d->as.import_decl.path, visited, vcount);
    }
}

static void check_decl(Checker *c, AstNode *node)
{
    if (node == NULL)
        return;

    switch (node->kind)
    {
    case AST_FN_DECL:
        check_fn_decl(c, node);
        break;
    case AST_STRUCT_DECL:
        check_struct_decl(c, node);
        break;
    case AST_ENUM_DECL:
        check_enum_decl(c, node);
        break;
    case AST_IMPL_DECL:
        check_impl_decl(c, node);
        break;
    case AST_EXTERN_FN:
        check_extern_fn(c, node);
        break;
    case AST_EXTERN_STRUCT_DECL:
        check_extern_struct_decl(c, node);
        break;
    case AST_EXTERN_BLOCK:
        check_extern_block(c, node);
        break;
    case AST_LOAD_LIB:
        check_load_lib(c, node);
        break;
    case AST_MODULE_DECL:
        /* Module declaration: noted but no type checking needed */
        break;
    case AST_IMPORT_DECL:
        /* Handled in forward_pass */
        break;
    case AST_TYPE_ALIAS_DECL:
        /* Handled in forward_pass */
        break;
    case AST_TRAIT_DECL:
        /* Handled in forward_pass */
        break;
    case AST_IMPL_TRAIT_DECL:
        check_impl_trait_decl(c, node);
        break;
    case AST_FFI_CALL:
        /* FFI dynamic call: check lib expr, skip type checking of args */
        check_expr(c, node->as.ffi_call.lib_expr);
        for (int i = 0; i < node->as.ffi_call.arg_count; i++)
        {
            check_expr(c, node->as.ffi_call.args[i]);
        }
        break;
    default:
        /* Statements or expressions appearing at top level */
        check_stmt(c, node);
        break;
    }
}

/* ---- Two-pass checking ---- */

/* Pass 1: Register all struct types and function signatures (forward declarations) */
static void forward_pass(Checker *c, AstNode *program)
{
    for (int i = 0; i < program->as.program.decl_count; i++)
    {
        AstNode *decl = program->as.program.decls[i];
        switch (decl->kind)
        {
        case AST_STRUCT_DECL:
            check_struct_decl(c, decl);
            break;
        case AST_ENUM_DECL:
            check_enum_decl(c, decl);
            break;
        case AST_TYPE_ALIAS_DECL:
        {
            /* Resolve target type and register under the alias name. Source-
               order rule: struct/enum names referenced by the alias must
               appear earlier in the file (we don't do a separate pre-pass). */
            if (find_type_alias(c, decl->as.type_alias_decl.name) ||
                find_struct_type(c, decl->as.type_alias_decl.name) ||
                find_enum_type(c, decl->as.type_alias_decl.name))
            {
                checker_error(c, decl->line, decl->column,
                              "type name '%s' already defined",
                              decl->as.type_alias_decl.name);
                break;
            }
            Type *target = resolve_type_node(c, decl->as.type_alias_decl.target,
                                             decl->line, decl->column);
            if (target == NULL) break;
            register_type_alias(c, decl->as.type_alias_decl.name, target);
            decl->resolved_type = target;
            break;
        }
        case AST_TRAIT_DECL:
            check_trait_decl(c, decl);
            break;
        case AST_FN_DECL:
        {
            /* G2: skip generic function templates — register as template only */
            if (decl->as.fn_decl.type_param_count > 0) {
                register_fn_template(c, decl);
                break;
            }
            /* Register function signature only (don't check body yet) */
            int n = decl->as.fn_decl.param_count;
            Type **params = NULL;
            if (n > 0)
            {
                params = (Type **)malloc_safe((size_t)n * sizeof(Type *));
                for (int j = 0; j < n; j++)
                {
                    params[j] = resolve_type_node(c, decl->as.fn_decl.param_types[j],
                                                  decl->line, decl->column);
                }
            }
            Type *ret = resolve_type_node(c, decl->as.fn_decl.return_type,
                                          decl->line, decl->column);
            Type *fn_type = type_function(params, n, ret, false);
            attach_param_defaults(c, decl, fn_type, params);
            scope_define(c->current_scope, decl->as.fn_decl.name, fn_type);
            decl->resolved_type = fn_type;
            break;
        }
        case AST_EXTERN_FN:
            check_extern_fn(c, decl);
            break;
        case AST_EXTERN_STRUCT_DECL:
            check_extern_struct_decl(c, decl);
            break;
        case AST_EXTERN_BLOCK:
            check_extern_block(c, decl);
            break;
        case AST_LOAD_LIB:
            check_load_lib(c, decl);
            break;
        case AST_IMPORT_DECL:
        {
            if (c->registry == NULL)
                break;
            const char *import_path = decl->as.import_decl.path;

            /* Circular import detection */
            if (module_is_importing(c->registry, import_path))
            {
                checker_error(c, decl->line, decl->column,
                              "circular import detected: '%s'", import_path);
                break;
            }

            /* Built-in stdlib fallback (user-priority shadowing): if the
               name matches a compiler built-in module AND no user .ls
               file is present at the resolved path, skip the file loader
               and synthesise a TYPE_MODULE directly from the built-in
               symbol table. The module Type is bound into the current
               scope so subsequent name-resolution treats `math.X` like
               any other module-qualified access. */
            if (builtin_module_exists(import_path) &&
                !module_user_file_exists(import_path, c->source_path))
            {
                Type *mod_type = builtin_module_make_type(c, import_path);
                if (mod_type)
                {
                    const char *bn = decl->as.import_decl.alias
                                     ? decl->as.import_decl.alias
                                     : import_path;
                    scope_define(c->current_scope, bn, mod_type);
                }
                break;
            }

            /* Load module (parse if not already loaded) */
            ModuleInfo *mod = module_load(c->registry, import_path, c->source_path);
            if (mod == NULL)
            {
                checker_error(c, decl->line, decl->column,
                              "cannot find module '%s'", import_path);
                break;
            }

            /* Type-check the module if not already checked */
            if (!mod->checked)
            {
                module_push_import(c->registry, import_path);
                /* A1 (module generics): collect the module's generic
                   instantiations (identity(int), Pair(int,string).get, ...) and
                   merge them into THIS checker's pending queue so they bubble up
                   to the root and reach codegen. Previously passed NULL here, so
                   module-defined generics were silently discarded → call sites
                   failed with "undefined function 'identity(int)'".
                   A2: stash the module name so the recursive checker prefixes its
                   generic instantiation symbols with it (cross-module collision). */
                const char *saved_ccm = c->registry->current_check_module;
                c->registry->current_check_module = import_path;
                CheckerGenericMethods sub_gm = {0};
                bool ok = checker_check(mod->ast, mod->file_path,
                                        c->registry, &sub_gm);
                c->registry->current_check_module = saved_ccm;
                module_pop_import(c->registry);
                if (!ok)
                {
                    if (sub_gm.methods)
                    {
                        for (int gi = 0; gi < sub_gm.count; gi++)
                        {
                            if (sub_gm.methods[gi].cloned_fn)
                                ast_free(sub_gm.methods[gi].cloned_fn);
                            free(sub_gm.methods[gi].mangled_name);
                        }
                        free(sub_gm.methods);
                    }
                    checker_error(c, decl->line, decl->column,
                                  "errors in imported module '%s'", import_path);
                    break;
                }
                for (int gi = 0; gi < sub_gm.count; gi++)
                {
                    if (c->pending_gm_count >= c->pending_gm_cap) {
                        c->pending_gm_cap = c->pending_gm_cap < 8 ? 8 : c->pending_gm_cap * 2;
                        c->pending_generic_methods = realloc_safe(c->pending_generic_methods,
                            (size_t)c->pending_gm_cap * sizeof(c->pending_generic_methods[0]));
                    }
                    int gm_idx = c->pending_gm_count++;
                    c->pending_generic_methods[gm_idx].cloned_fn    = sub_gm.methods[gi].cloned_fn;
                    c->pending_generic_methods[gm_idx].mangled_name = sub_gm.methods[gi].mangled_name;
                    c->pending_generic_methods[gm_idx].struct_type  = sub_gm.methods[gi].struct_type;
                }
                free(sub_gm.methods);
                mod->checked = true;
            }

            /* Collect exported symbols from the module */
            Type *mod_type = type_module_new(import_path);
            AstNode *mod_ast = mod->ast;
            for (int j = 0; j < mod_ast->as.program.decl_count; j++)
            {
                AstNode *d = mod_ast->as.program.decls[j];
                if (d->kind == AST_FN_DECL && d->resolved_type)
                {
                    type_module_add_export(mod_type,
                                           d->as.fn_decl.name, d->resolved_type);
                }
                /* Step 0 (cross-module generics): a generic free function
                   (type_param_count > 0) has no resolved_type — it is a template,
                   not a concrete fn. Register it into THIS importer's fn-template
                   registry (idempotent) so call sites here can instantiate it,
                   e.g. new_stack(int)(). The instantiation machinery (checker
                   ~L4369 + codegen pending-gm) is origin-agnostic and takes over. */
                else if (d->kind == AST_FN_DECL &&
                         d->as.fn_decl.type_param_count > 0)
                {
                    if (find_fn_template(c, d->as.fn_decl.name) < 0)
                        register_fn_template(c, d);
                }
                else if (d->kind == AST_STRUCT_DECL && d->resolved_type)
                {
                    type_module_add_export(mod_type,
                                           d->as.struct_decl.name, d->resolved_type);
                    /* B-4: same struct name from a different module. Previously
                       B-1 errored here. Now: mark the bare name AMBIGUOUS (bare
                       use → error; qualified `mod.Struct` resolves precisely via
                       the module export table). Same pointer = same module imported
                       transitively → OK, leave the single bare registration. */
                    {
                        const char *sname = d->as.struct_decl.name;
                        Type *existing = find_struct_type(c, sname);
                        if (existing && existing != d->resolved_type)
                        {
                            checker_mark_ambiguous_type(c, sname);
                        }
                        else if (!existing)
                        {
                            /* Phase E.4: register the imported struct type into the
                               importer's struct registry so user code can name it
                               directly (e.g. `File f` after `import io`). */
                            checker_register_struct(c, sname, d->resolved_type);
                        }
                    }
                }
                /* Step 0 (cross-module generics): a generic struct template
                   (type_param_count > 0) has no resolved_type. Register it into
                   THIS importer's struct-template registry (idempotent) and attach
                   the module's matching generic impl, so `Stack(int)` at a call
                   site here flows through checker_instantiate_struct exactly like a
                   same-file generic. NOTE: two modules defining the same generic
                   name collide on the bare name — v1 keeps the first registration
                   (single-definition use is unambiguous). */
                else if (d->kind == AST_STRUCT_DECL &&
                         d->as.struct_decl.type_param_count > 0)
                {
                    const char *gname = d->as.struct_decl.name;
                    int tidx = register_imported_struct_template(
                        c, gname,
                        d->as.struct_decl.type_params,
                        d->as.struct_decl.type_param_count, d, import_path);
                    /* Attach the module's generic impl(T) block, if present and not
                       already attached (idempotent on transitive re-import). */
                    if (tidx >= 0 && c->struct_templates[tidx].impl_node == NULL)
                    {
                        for (int k = 0; k < mod_ast->as.program.decl_count; k++)
                        {
                            AstNode *id = mod_ast->as.program.decls[k];
                            if (id->kind == AST_IMPL_DECL &&
                                id->as.impl_decl.type_param_count > 0 &&
                                id->as.impl_decl.name &&
                                strcmp(id->as.impl_decl.name, gname) == 0)
                            {
                                c->struct_templates[tidx].impl_node = id;
                                break;
                            }
                        }
                    }
                }
                else if (d->kind == AST_ENUM_DECL && d->resolved_type)
                {
                    type_module_add_export(mod_type,
                                           d->as.enum_decl.name, d->resolved_type);
                    /* B-4: same enum name from a different module → mark ambiguous
                       (bare use errors; `mod.Enum` resolves via export table). */
                    {
                        const char *ename = d->as.enum_decl.name;
                        Type *existing_e = find_enum_type(c, ename);
                        if (existing_e && existing_e != d->resolved_type)
                        {
                            checker_mark_ambiguous_type(c, ename);
                        }
                        else if (!existing_e)
                        {
                            /* Phase E.4: register the imported enum into the importer's
                               enum registry so bare variant names resolve without
                               qualification — matching the behaviour the built-in io
                               module had. */
                            checker_register_enum(c, ename, d->resolved_type);
                        }
                    }
                }
                else if (d->kind == AST_VAR_DECL && d->resolved_type)
                {
                    type_module_add_export(mod_type,
                                           d->as.var_decl.name, d->resolved_type);
                }
                else if (d->kind == AST_EXTERN_FN && d->resolved_type)
                {
                    type_module_add_export(mod_type,
                                           d->as.extern_fn.name, d->resolved_type);
                }
                else if (d->kind == AST_EXTERN_BLOCK)
                {
                    for (int eb = 0; eb < d->as.extern_block.decl_count; eb++)
                    {
                        AstNode *ebd = d->as.extern_block.decls[eb];
                        if (ebd->kind == AST_EXTERN_FN && ebd->resolved_type)
                            type_module_add_export(mod_type,
                                                   ebd->as.extern_fn.name,
                                                   ebd->resolved_type);
                    }
                }
                else if (d->kind == AST_IMPL_DECL &&
                         d->as.impl_decl.type_param_count == 0)
                {
                    /* Register impl methods from the imported module into the
                       importer's impl_registry so that instance/static method
                       calls on imported types (e.g. JsonValue.null_val()) work.
                       method->resolved_type was set by check_impl_decl (L7106)
                       and is kept alive because types are intentionally leaked. */
                    const char *impl_name = d->as.impl_decl.name;
                    /* B-4.1: key by THIS module's struct/enum unique name (its
                       llvm_name) so two imported modules' same-named `impl Widget`
                       register under distinct keys. Use the module export table to
                       get THIS module's type (find_struct_type would return the
                       ambiguous first-registered one). */
                    const char *impl_key = impl_name;
                    Type *impl_st = type_module_find_export(mod_type, impl_name);
                    if (impl_st)
                    {
                        const char *k = impl_key_of_type(impl_st);
                        if (k) impl_key = k;
                    }
                    int impl_idx = find_or_create_impl(c, impl_key);
                    for (int mi = 0; mi < d->as.impl_decl.method_count; mi++)
                    {
                        AstNode *method = d->as.impl_decl.methods[mi];
                        if (method == NULL || method->kind != AST_FN_DECL)
                            continue;
                        if (method->resolved_type == NULL)
                            continue;
                        bool m_static = method->as.fn_decl.is_static;
                        int  m_sbk    = method->as.fn_decl.self_borrow_kind;
                        const char *mname = method->as.fn_decl.name;
                        register_method(c, impl_idx, mname,
                                        method->resolved_type,
                                        m_static, m_sbk,
                                        method->line, method->column);
                        /* Also expose as a free function so direct calls work */
                        scope_define(c->current_scope, mname,
                                     method->resolved_type);
                    }
                }
                /* An imported `trait Foo`/`impl Trait for Type` (incl. builtin
                   targets like `impl Hash for int`): register it so `where T:Foo`
                   bounds and `x.foo()` dispatch work in THIS importer. */
                else if (d->kind == AST_TRAIT_DECL ||
                         d->kind == AST_IMPL_TRAIT_DECL)
                {
                    register_one_imported_trait_decl(c, d, mod_type);
                }
                /* A transitive import inside the imported module (user → std.map →
                   std.hash): propagate that sub-module's traits/impls too, so a
                   `where K: Hash` bound inside std.map's monomorphized methods is
                   satisfiable here even though the user never imported std.hash. */
                else if (d->kind == AST_IMPORT_DECL)
                {
                    const char *visited[64];
                    int vcount = 0;
                    propagate_imported_traits(c, d->as.import_decl.path,
                                              visited, &vcount);
                }
            }

            /* If `import foo as bar` was used, bind under the alias; otherwise
               bind under the full dotted path (e.g. "std.time"). */
            const char *bind_name = decl->as.import_decl.alias
                                    ? decl->as.import_decl.alias
                                    : import_path;
            scope_define(c->current_scope, bind_name, mod_type);
            break;
        }
        default:
            break;
        }
    }
}

/* Pass 2: Check all function bodies and remaining declarations */
static void check_pass(Checker *c, AstNode *program)
{
    for (int i = 0; i < program->as.program.decl_count; i++)
    {
        AstNode *decl = program->as.program.decls[i];
        switch (decl->kind)
        {
        case AST_STRUCT_DECL:
        case AST_ENUM_DECL:
        case AST_TYPE_ALIAS_DECL:
            /* Already handled in forward pass */
            break;
        case AST_FN_DECL:
        {
            /* Check function body (signature already registered) */
            Type *fn_type = decl->resolved_type;
            if (fn_type == NULL || fn_type->kind != TYPE_FUNCTION)
                break;

            /* Phase A1: top-level fns cannot declare &self / &!self. */
            if (decl->as.fn_decl.self_borrow_kind != 0 &&
                decl->as.fn_decl.impl_struct_name == NULL)
            {
                checker_error(c, decl->line, decl->column,
                              "&%sself is only valid as the first parameter of a method "
                              "inside an `impl` block",
                              decl->as.fn_decl.self_borrow_kind == 2 ? "!" : "");
                break;
            }

            push_scope(c);
            for (int j = 0; j < decl->as.fn_decl.param_count; j++)
            {
                Type *pt = fn_type->as.function.params[j];
                /* Phase 5: unwrap &T / &!T → T for the body-local symbol, remember kind. */
                bool is_borrow = false;
                bool is_mut_borrow = false;
                if (pt && pt->kind == TYPE_REFERENCE)
                {
                    if (pt->is_mut) is_mut_borrow = true;
                    else            is_borrow     = true;
                    pt = pt->as.pointer_to;
                }
                Symbol *param_sym = scope_define(c->current_scope,
                                                 decl->as.fn_decl.param_names[j], pt);
                if (param_sym)
                {
                    param_sym->is_borrow = is_borrow;
                    param_sym->is_mut_borrow = is_mut_borrow;
                    /* String parameters receive deep copies from caller — always dynamic */
                    if (pt && pt->kind == TYPE_STRING)
                        param_sym->is_static_string = false;
                    /* F.2: Block params are shallow-copy borrows of caller's env */
                    if (pt && pt->kind == TYPE_BLOCK)
                        param_sym->is_borrow = true;
                }
            }
            Type *saved_ret = c->current_fn_return;
            c->current_fn_return = fn_type->as.function.return_type;
            check_stmt(c, decl->as.fn_decl.body);
            c->current_fn_return = saved_ret;
            pop_scope(c);
            break;
        }
        case AST_IMPL_DECL:
            check_impl_decl(c, decl);
            break;
        case AST_IMPL_TRAIT_DECL:
            check_impl_trait_decl(c, decl);
            break;
        case AST_EXTERN_FN:
        case AST_EXTERN_STRUCT_DECL:
        case AST_EXTERN_BLOCK:
        case AST_LOAD_LIB:
            /* Already handled in forward pass */
            break;
        default:
            check_decl(c, decl);
            break;
        }
    }
}

/* ---- Register built-in functions ---- */

static void register_builtins(Checker *c)
{
    /* Builtin enums (Option / Result) — see register_builtin_enums for details. */
    register_builtin_enums(c);

    /* print(...) -> void — accepts any printable type */
    {
        Type *ft = type_function(NULL, 0, type_void(), true);
        scope_define(c->current_scope, "print", ft);
    }
    /* A-FLIP (docs/plan_runtime_primitives.md): malloc/realloc/free/abort are no
       longer global builtins. They live in std.c (extern fn malloc/realloc/free
       + `fn abort`) and are reached either by canonical path std.c.malloc (which
       the checker/codegen recognise by spelling — works inside generic bodies) or
       via an import alias (c.malloc). A bare `malloc(...)` is now "undefined". */
    /* sizeof(Type) is handled as a compile-time AST_SIZEOF node (see parser
       infix_call + check_expr), not a runtime function — nothing to register. */
    /* sqrt(f64) -> f64 */
    {
        Type **params = (Type **)malloc_safe(sizeof(Type *));
        params[0] = type_f64();
        Type *ft = type_function(params, 1, type_f64(), false);
        scope_define(c->current_scope, "sqrt", ft);
    }
    /* abort: see the A-FLIP note above — now std.c.abort() / std_c__abort. */
}

/* B-MAP-M5-004: has_drop fixpoint. Generic struct/enum instantiations cache
   has_drop at instantiation time, which can be a stale `false` for a type that
   only BECOMES has_drop through a recursive container payload — e.g. JsonValue
   owns heap via Vec(JsonValue)/Map(string,JsonValue), and Option(JsonValue)
   (from Map.get) is instantiated before that propagates, so it caches
   has_drop=false → no __drop emitted → leak. After all decls + impl
   instantiations are processed, re-propagate has_drop across every registered
   struct/enum type until stable. Monotonic (only ever sets true, never clears —
   so user-__drop structs with POD fields keep their has_drop), so it converges. */
static void checker_propagate_has_drop_fixpoint(Checker *c)
{
    bool changed = true;
    int guard = 0;
    while (changed && guard++ < 4096)
    {
        changed = false;
        for (int i = 0; i < c->struct_type_count; i++)
        {
            Type *st = c->struct_types[i].type;
            if (st == NULL || st->kind != TYPE_STRUCT || st->as.strukt.has_drop)
                continue;
            for (int f = 0; f < st->as.strukt.field_count; f++)
            {
                if (type_owns_heap_for_enum(st->as.strukt.fields[f].type))
                {
                    st->as.strukt.has_drop = true;
                    changed = true;
                    break;
                }
            }
        }
        for (int i = 0; i < c->enum_type_count; i++)
        {
            Type *et = c->enum_types[i].type;
            if (et == NULL || et->kind != TYPE_ENUM || et->as.enom.has_drop)
                continue;
            for (int v = 0; v < et->as.enom.variant_count && !et->as.enom.has_drop; v++)
            {
                for (int p = 0; p < et->as.enom.variants[v].payload_count; p++)
                {
                    Type *pt = et->as.enom.variants[v].payload_types[p];
                    if (pt == et || type_owns_heap_for_enum(pt))
                    {
                        et->as.enom.has_drop = true;
                        changed = true;
                        break;
                    }
                }
            }
        }
    }
}

/* ---- Public entry point ---- */

bool checker_check(AstNode *program, const char *source_path,
                   struct ModuleRegistry *registry,
                   CheckerGenericMethods *out_gm)
{
    if (program == NULL || program->kind != AST_PROGRAM)
        return false;

    Checker c;
    memset(&c, 0, sizeof(Checker));
    c.source_path = source_path;
    c.registry = registry;
    /* A2: NULL for the root program; set to the module name when this is a
       recursive module check (the import handler stashes it on the registry). */
    c.module_name = registry ? registry->current_check_module : NULL;
    c.current_scope = scope_new(NULL);

    register_builtins(&c);
    register_builtin_operator_traits(&c);
    forward_pass(&c, program);
    check_pass(&c, program);
    /* B-MAP-M5-004: settle has_drop across recursive-via-container types so that
       e.g. Option(JsonValue) is correctly has_drop and gets a __drop emitted. */
    checker_propagate_has_drop_fixpoint(&c);

    /* G1.5: transfer pending generic methods to caller if requested */
    if (out_gm && !c.had_error && c.pending_gm_count > 0) {
        out_gm->count = c.pending_gm_count;
        out_gm->methods = malloc_safe(
            (size_t)c.pending_gm_count * sizeof(out_gm->methods[0]));
        for (int i = 0; i < c.pending_gm_count; i++) {
            out_gm->methods[i].cloned_fn    = c.pending_generic_methods[i].cloned_fn;
            out_gm->methods[i].mangled_name = c.pending_generic_methods[i].mangled_name;
            out_gm->methods[i].struct_type  = c.pending_generic_methods[i].struct_type;
        }
        /* Ownership transferred — just free the container array */
        free(c.pending_generic_methods);
    } else {
        /* Not transferred — free everything as safety net */
        for (int i = 0; i < c.pending_gm_count; i++) {
            if (c.pending_generic_methods[i].cloned_fn)
                ast_free(c.pending_generic_methods[i].cloned_fn);
            free(c.pending_generic_methods[i].mangled_name);
        }
        free(c.pending_generic_methods);
        if (out_gm) { out_gm->methods = NULL; out_gm->count = 0; }
    }

    /* Cleanup */
    for (int i = 0; i < c.lazy_gm_count; i++) {
        free(c.lazy_generic_methods[i].mangled_name);
        free(c.lazy_generic_methods[i].type_args);
    }
    free(c.lazy_generic_methods);
    scope_free(c.current_scope);
    /* Note: struct types and function types are intentionally leaked for now
       since AST nodes reference them via resolved_type. They will be freed
       when the full compilation pipeline is in place. */
    free(c.struct_types);
    free(c.enum_types);
    free(c.type_aliases);
    for (int i = 0; i < c.enum_template_count; i++)
    {
        for (int v = 0; v < c.enum_templates[i].variant_count; v++)
            free(c.enum_templates[i].variants[v].payload);
        free(c.enum_templates[i].variants);
    }
    free(c.enum_templates);
    /* G1: struct_templates — entries point into AST, nothing to deep-free */
    free(c.struct_templates);
    /* G2: fn_templates — entries point into AST, nothing to deep-free */
    free(c.fn_templates);
    for (int i = 0; i < c.impl_count; i++)
    {
        free(c.impl_registry[i].methods);
    }
    free(c.impl_registry);
    /* Trait registry cleanup */
    for (int i = 0; i < c.trait_count; i++)
    {
        free((void *)c.trait_registry[i].name);
        for (int j = 0; j < c.trait_registry[i].method_count; j++)
            free((void *)c.trait_registry[i].methods[j].name);
        free(c.trait_registry[i].methods);
    }
    free(c.trait_registry);
    /* Trait impls — pointers into AST, no deep-free needed */
    free(c.trait_impls);

    return !c.had_error;
}

/* ---- Public API for built-in stdlib modules (e.g. `io`) ---- */

void checker_register_struct(Checker *c, const char *name, Type *type) {
    register_struct_type(c, name, type);
}

void checker_register_enum(Checker *c, const char *name, Type *type) {
    register_enum_type(c, name, type);
}

Type *checker_find_enum(Checker *c, const char *name) {
    return find_enum_type(c, name);
}

Type *checker_instantiate_result(Checker *c, Type *t, Type *e) {
    int idx = find_template_idx(c, "Result");
    if (idx < 0) return NULL;
    Type *args[2] = { t, e };
    return instantiate_template(c, idx, args, 2, 0, 0);
}

Type *checker_instantiate_option(Checker *c, Type *t) {
    int idx = find_template_idx(c, "Option");
    if (idx < 0) return NULL;
    return instantiate_template(c, idx, &t, 1, 0, 0);
}
