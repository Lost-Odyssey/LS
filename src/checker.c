/* checker.c — Type checker: walks AST, validates types, fills resolved_type */
#include "checker.h"
#include "checker_internal.h"
#include "parser.h"
#include "module.h"
#include "builtins_math.h"
#include "builtins_perf.h"
#include <stdio.h>
#include <string.h>
#include <stdarg.h>
#include <stdlib.h>
#include <math.h>

/* File-local helpers (single-TU; re-static'd at checker split end). */
static void bind_generic_defining_module_imports(Checker *c, const char *module_path);
static Type *build_module_type_with_exports(Checker *c, const char *path);
static bool check_and_queue_generic_method(Checker *c, Type *struct_type, const char *mangled_name, AstNode *method, Type *mtype, char **tp_names, Type **type_args, int tp_count, int line, int col);
static Type *check_builtin_call(Checker *c, const char *name, AstNode *call_node);
static bool check_method_where_bounds(Checker *c, AstNode *method, const char *qualified_name, char **tp_names, Type **type_args, int tp_count);
static void check_pass(Checker *c, AstNode *program);
static Type *check_variant_ctor(Checker *c, AstNode *node, Type *enum_type, int variant_idx, AstNode **args, int arg_count);
static void checker_mark_ambiguous_type(Checker *c, const char *name);
static void checker_propagate_has_drop_fixpoint(Checker *c);
static Type *checker_str_type(Checker *c);
static bool checker_type_is_ambiguous(Checker *c, const char *name);
static void checker_warning(Checker *c, int line, int col, const char *fmt, ...);
static bool ensure_generic_method_instantiated(Checker *c, const char *mangled_struct, const char *method_name, int line, int col);
static void ensure_generic_struct_impls_local(Checker *c, Type *st);
/* Non-static: also used by the from_list/from_pairs taggers in checker_lower.c
   (declared in checker_internal.h). */
Type *find_method_ensured(Checker *c, Type *st, const char *mname);
static int find_struct_template_idx_pull(Checker *c, const char *base_name);
static Type *find_type_alias(Checker *c, const char *name);
static bool generic_method_is_eager(const char *name);
static bool is_builtin_function(const char *name);
static const char *intrinsic_retired_spelling(const char *name);
static bool is_self_placeholder(const Type *t);
static Type *lookup_impl_type_arg(char **tp_names, Type **type_args, int tp_count, const char *name);
static int method_is_static(Checker *c, const char *struct_name, const char *method_name);
static int method_self_borrow_kind(Checker *c, const char *struct_name, const char *method_name);
static bool path_is_under_stdlib(const char *path);
static void pending_generic_method_add(Checker *c, AstNode *cloned, char *owned_mangled, Type *struct_type);
static void generic_method_symbol(char *buf, size_t sz, const char *mangled_name, AstNode *method);
static void register_builtin_enums(Checker *c);
static void register_builtins(Checker *c);
static int register_imported_struct_template(Checker *c, const char *base_name, char **type_params, int type_param_count, AstNode *decl_node, const char *module_path);
static void register_lazy_generic_method(Checker *c, const char *mfn_name, AstNode *method, Type *mtype, Type *struct_type, char **tp_names, Type **type_args, int tp_count);
static void register_template(Checker *c, const char *base_name, int type_param_count, int variant_count, const char *const *variant_names, const int *variant_payload_counts, const int *variant_payload_param_idxs);
static void register_type_alias(Checker *c, const char *name, Type *type);
static Type *resolve_type_node_with_substitution( Checker *c, TypeNode *node, char **tp_names, Type **type_args, int tp_count);
static Type *str_target_of_expected(const Type *t);
static Type *try_instantiate_method_level_generic(Checker *c, const char *impl_key, const char *method_name, TypeNode **call_type_args, int call_type_arg_count, Type **pre_resolved_args, int line, int col);
static const char *type_impl_name(Type *t);
static bool type_is_str_struct(const Type *t);
static void comptime_expand_block(Checker *c, AstNode *block);

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

void checker_error(Checker *c, int line, int col, const char *fmt, ...)
{
    /* Suppressed during transitive trait re-registration: the trait was already
       validated in its home module, so a resolution failure here is a spurious
       cross-scope artifact, not a user error. (Mirrors silent_move_errors.) */
    if (c->silent_type_errors)
        return;
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
void checker_move_error(Checker *c, int line, int col, const char *fmt, ...)
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
/* Shared across checker TUs (extern in checker_internal.h) — used by the
   operator-overload lowering in checker_lower.c as well as trait checking. */
Type g_self_placeholder_type = {
    .kind = TYPE_STRUCT,
    .as = { .strukt = { .name = "Self", .fields = NULL, .field_count = 0, .has_drop = false } }
};

static bool is_self_placeholder(const Type *t) {
    return t == &g_self_placeholder_type;
}

/* type_equals variant that treats g_self_placeholder_type as equal to `concrete`.
   Handles TYPE_REFERENCE wrapping (e.g. &Self == &Vec2). */
bool type_equals_with_self(const Type *trait_t, const Type *impl_t, const Type *concrete)
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
    /* `Self` nested inside a generic struct/enum return type — e.g. a trait method
       `-> Result(Self, Str)` validated against an impl `-> Result(int, Str)`. Generic
       enum/struct instances are identified by their mangled name (enums carry no
       structured arg list), so substitute the concrete type's name for a whole-word
       `Self` in the trait name and compare. (Top-level `-> Self` is handled above.) */
    if (concrete != NULL &&
        ((trait_t->kind == TYPE_ENUM   && impl_t->kind == TYPE_ENUM) ||
         (trait_t->kind == TYPE_STRUCT && impl_t->kind == TYPE_STRUCT))) {
        char tnbuf[512], inbuf[512];
        snprintf(tnbuf, sizeof tnbuf, "%s", type_name(trait_t));
        snprintf(inbuf, sizeof inbuf, "%s", type_name(impl_t));
        if (strcmp(tnbuf, inbuf) == 0) return true;
        if (strstr(tnbuf, "Self") != NULL) {
            char cnbuf[256];
            snprintf(cnbuf, sizeof cnbuf, "%s", type_name(concrete));
            char outbuf[512]; int op = 0; bool fits = true;
            int cl = (int)strlen(cnbuf);
            #define LS_IDENT_CH(ch) (((ch) >= 'A' && (ch) <= 'Z') || \
                ((ch) >= 'a' && (ch) <= 'z') || ((ch) >= '0' && (ch) <= '9') || (ch) == '_')
            for (const char *q = tnbuf; *q != '\0' && op < 500; ) {
                char prev = (q == tnbuf) ? '\0' : q[-1];
                if (strncmp(q, "Self", 4) == 0 &&
                    !LS_IDENT_CH(prev) && !LS_IDENT_CH(q[4])) {
                    if (op + cl >= 500) { fits = false; break; }
                    memcpy(outbuf + op, cnbuf, (size_t)cl); op += cl; q += 4;
                } else {
                    outbuf[op++] = *q++;
                }
            }
            #undef LS_IDENT_CH
            outbuf[op] = '\0';
            if (fits && strcmp(outbuf, inbuf) == 0) return true;
        }
    }
    return type_equals(trait_t, impl_t);
}

/* B-2: Compute the LLVM-level type name for a struct/enum defined in a module.
   Returns a malloc'd "<mod>__Name" string when c->module_name is non-NULL, else NULL.
   The caller is responsible for nothing — the returned pointer is stored in
   Type.strukt.llvm_name / Type.enom.llvm_name and intentionally leaked with the Type. */
char *checker_module_type_llvmname(Checker *c, const char *bare_name)
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

void register_struct_type(Checker *c, const char *name, Type *type)
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

Type *find_struct_type(Checker *c, const char *name)
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

/* Step 11: Resolve a builtin type name ("int", "i64", "f64", "bool", "char")
   to its Type*.  Returns NULL for non-builtin names. */
Type *resolve_builtin_type_by_name(const char *name)
{
    if (strcmp(name, "int") == 0)    return type_int();
    if (strcmp(name, "i64") == 0)    return type_i64();
    if (strcmp(name, "f64") == 0)    return type_f64();
    if (strcmp(name, "bool") == 0)   return type_bool();
    if (strcmp(name, "char") == 0)   return type_char();
    /* Sized integer / f32 scalars: valid trait-impl targets too (e.g. so
       @derive(Show/Serialize) on a generic `Box(T)` works for Box(i16)/Box(u32) —
       std.core.{show,value} impl those interfaces for every scalar). */
    if (strcmp(name, "i8") == 0)     return type_i8();
    if (strcmp(name, "i16") == 0)    return type_i16();
    if (strcmp(name, "i32") == 0)    return type_i32();
    if (strcmp(name, "u8") == 0)     return type_u8();
    if (strcmp(name, "u16") == 0)    return type_u16();
    if (strcmp(name, "u32") == 0)    return type_u32();
    if (strcmp(name, "u64") == 0)    return type_u64();
    if (strcmp(name, "f32") == 0)    return type_f32();
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

/* P5-4 S-2: every string literal / f-string IS a `Str` — the builtin string
   type is gone. Resolve the Str struct type: normally visible via import
   std.str (the root program gets a prelude-injected import). Modules inside
   std.str's own dependency cone (std.vec, std.c's chain, std.map) cannot
   import std.str back; their literals resolve through the shared registry
   instead — std.str's forward_pass registers `struct Str` (resolved_type set)
   before its imports are recursively checked, so the type is always there. */
static Type *checker_str_type(Checker *c)
{
    Type *t = find_struct_type(c, "Str");
    if (t != NULL) return t;
    struct ModuleRegistry *reg = c->registry;
    if (reg == NULL) return NULL;
    for (int m = 0; m < reg->count; m++) {
        AstNode *mast = reg->modules[m].ast;
        if (mast == NULL || mast->kind != AST_PROGRAM) continue;
        for (int d = 0; d < mast->as.program.decl_count; d++) {
            AstNode *decl = mast->as.program.decls[d];
            if (decl != NULL && decl->kind == AST_STRUCT_DECL &&
                decl->resolved_type != NULL &&
                decl->as.struct_decl.name != NULL &&
                strcmp(decl->as.struct_decl.name, "Str") == 0)
                return decl->resolved_type;
        }
    }
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
    case TYPE_CHAR:   return "char";
    case TYPE_I8:     return "i8";
    case TYPE_I16:    return "i16";
    case TYPE_I32:    return "i32";
    case TYPE_U8:     return "u8";
    case TYPE_U16:    return "u16";
    case TYPE_U32:    return "u32";
    case TYPE_U64:    return "u64";
    case TYPE_F32:    return "f32";
    default:          return NULL;
    }
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
}

Type *find_enum_type(Checker *c, const char *name)
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
int find_template_idx(Checker *c, const char *base)
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
            /* Replace the existing __drop entry. Free the old compiler-generated
               function type and its param array + pointer type to avoid a
               compile-time leak. */
            Type *old = c->impl_registry[impl_idx].methods[j].type;
            if (old && old->kind == TYPE_FUNCTION) {
                /* The pointer param (old->as.function.params[0]) was created
                   by type_pointer(st); it outlives use here so free it.
                   Must happen BEFORE freeing params itself (use-after-free
                   otherwise — glibc's tcache overwrites the freed block with
                   freelist metadata, corrupting params[0] before this read). */
                if (old->as.function.params &&
                    old->as.function.params[0] &&
                    old->as.function.params[0]->kind == TYPE_POINTER)
                    free(old->as.function.params[0]);
                free(old->as.function.params);
                free(old);
            }
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
static int method_self_borrow_kind(Checker *c, const char *struct_name,
                                   const char *method_name)
{
    for (int i = 0; i < c->impl_count; i++)
    {
        if (strcmp(c->impl_registry[i].struct_name, struct_name) != 0)
            continue;
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
    return 0;
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
    for (int i = 0; i < c->impl_count; i++)
    {
        if (strcmp(c->impl_registry[i].struct_name, struct_name) != 0)
            continue;
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
    return NULL;
}

/* L-002: find a method on `struct_name` whose origin matches `origin`
   (NULL = inherent; else interface name). Returns its type or NULL. Used by the
   interface-qualified call `Iface.method(recv)` to select the right overload. */
Type *find_method_origin(Checker *c, const char *struct_name,
                         const char *method_name, const char *origin)
{
    for (int i = 0; i < c->impl_count; i++)
    {
        if (strcmp(c->impl_registry[i].struct_name, struct_name) != 0)
            continue;
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
    for (int i = 0; i < c->impl_count; i++)
    {
        if (strcmp(c->impl_registry[i].struct_name, struct_name) != 0)
            continue;
        for (int j = 0; j < c->impl_registry[i].method_count; j++)
        {
            if (strcmp(c->impl_registry[i].methods[j].name, method_name) != 0)
                continue;
            const char *o = c->impl_registry[i].methods[j].origin_iface;
            if (o == NULL) inh++;
            else { ifc++; if (!a) a = o; else if (!b) b = o; }
        }
        break;
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
static int method_is_static(Checker *c, const char *struct_name, const char *method_name)
{
    for (int i = 0; i < c->impl_count; i++)
    {
        if (strcmp(c->impl_registry[i].struct_name, struct_name) != 0)
            continue;
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
    return -1;
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
static Type *builtin_module_make_type_merged(Checker *c, const char *name)
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
    char mfn_name[512];
    /* L-002 v2: contended interface methods get `T.<Iface>.m` (the flag was
       pre-set on `method` by the instantiation loop). */
    generic_method_symbol(mfn_name, sizeof(mfn_name), mangled_name, method);

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

/* L-002 v2: instantiate by the FULL lazy symbol (e.g. "Box(int).Show3.tag" for a
   contended interface method, or "Box(int).tag" otherwise). The plain-name wrapper
   below builds "T.m"; the interface-qualified call path builds "T.<Iface>.m". */
static bool ensure_generic_method_instantiated_sym(Checker *c,
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

static bool ensure_generic_method_instantiated(Checker *c,
                                               const char *mangled_struct,
                                               const char *method_name,
                                               int line, int col)
{
    char mfn_name[512];
    snprintf(mfn_name, sizeof(mfn_name), "%s.%s", mangled_struct, method_name);
    return ensure_generic_method_instantiated_sym(c, mangled_struct, mfn_name,
                                                  line, col);
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
            free(mtp_type_args);
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
static Type *try_infer_method_generic_from_closure(Checker *c,
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
           node carries no explicit type_args. */
        const char *un = type_name(inferred_u);
        if (un)
            call->as.call.resolved_type_args = chk_strdup(un);
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
static void generic_method_symbol(char *buf, size_t sz, const char *mangled_name,
                                  AstNode *method)
{
    const char *mname = method->as.fn_decl.name;
    if (method->as.fn_decl.iface_method_contended &&
        method->as.fn_decl.origin_iface)
        snprintf(buf, sz, "%s.%s.%s", mangled_name,
                 method->as.fn_decl.origin_iface, mname);
    else
        snprintf(buf, sz, "%s.%s", mangled_name, mname);
}

/* G1.5: For each method in a generic impl, resolve its param/return types
   with the concrete type arguments and register the method signature. Ordinary
   method bodies are checked lazily at call sites; compiler-reserved hooks that
   codegen calls by name are still checked and queued eagerly. */
void instantiate_impl_method_types(
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

        Type *mtype = type_function(params, total, ret, false);

        register_method(c, impl_idx, mname, mtype, is_static, sbk,
                        origin, method,  /* L-002 v2: carry fold origin */
                        method->line, method->column);

        /* Build mangled function name: "Pair(int,string).get_first"
           L-002 v2: a contended interface method becomes "T.<Iface>.m". */
        char mfn_name[512];
        generic_method_symbol(mfn_name, sizeof(mfn_name), mangled_name, method);
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
                if (checker_reject_borrow_type_arg(c, ta[i], base, line, col))
                    { free(ta); return NULL; }
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
                    if (checker_reject_borrow_type_arg(c, ta[i], base, line, col))
                        { free(ta); return NULL; }
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
bool type_assignable(const Type *dst, const Type *src)
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

void chk_push_scope(Checker *c)
{
    c->current_scope = scope_new(c->current_scope);
}

void chk_pop_scope(Checker *c)
{
    Scope *old = c->current_scope;
    c->current_scope = old->parent;
    scope_free(old);
}

/* ---- Forward declarations ---- */


/* ---- Helper functions ---- */

/* ---- Move semantics helpers (Phase A: linear, no control flow) ---- */










/* Phase G note: the former F.3/F.4A rejections (copy a Block out of a struct
   field / vec element / map value) have been removed — codegen now deep-clones
   the closure env at the copy-out site (cg_emit_block_env_clone in codegen.c),
   so the destination owns an independent env with no shared-env double-free. */


/* ---- Move semantics helpers (Phase B: control-flow aware) ---- */

/* A snapshot records the (is_moved, is_maybe_moved) pair for every movable symbol
   reachable from a scope chain at the moment of capture. Used by if/else merging
   and 2-pass loop analysis. */
/* MoveSnapEntry / MoveSnapshot moved to checker_internal.h (shared across TUs). */









/* Check builtin function calls that don't belong to a type */
static Type *check_builtin_call(Checker *c, const char *name, AstNode *call_node)
{
    int argc = call_node->as.call.arg_count;
    AstNode **args = call_node->as.call.args;

    /* Phase 2: the legacy __take/__drop_at/__dup/__move spellings are retired.
       Reject them with a clear pointer to the @-sigil replacement. */
    const char *retired = intrinsic_retired_spelling(name);
    if (retired != NULL)
    {
        checker_error(c, call_node->line, call_node->column,
                      "'%s' is retired; use '%s'", name, retired);
        return NULL;
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

    /* Phase E.3.3 / P5-4 S-2: from_cstr(object) -> Str
       Copies a C-style NUL-terminated char* (received via FFI as `object`)
       into an OWNED Str. Critical glue for getenv/strerror/readdir. */
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
        Type *strt = checker_str_type(c);
        if (strt == NULL)
        {
            checker_error(c, call_node->line, call_node->column,
                          "from_cstr() requires the Str type from std.core.str "
                          "(add `import std.core.str`)");
            return NULL;
        }
        return strt;
    }

    /* __move(var) -> T  — explicit move annotation.
       Marks the argument variable as MOVED and returns its type transparently.
       Works on any movable type; also force-moves static strings (unlike implicit moves). */
    if (intrinsic_lookup(name) && intrinsic_lookup(name)->kind == INTR_VAR_MOVE)
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
    if (intrinsic_lookup(name) && intrinsic_lookup(name)->kind == INTR_PLACE_DISPOSE)
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
    if (intrinsic_lookup(name) && intrinsic_lookup(name)->kind == INTR_PLACE_TAKE)
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

    /* __dup(place) -> T — DEEP COPY of the value at a place, WITHOUT consuming it
       (the source stays live). The generic value-duplication primitive: codegen
       loads the value and runs emit_clone_value — a bit-copy for POD T, a deep
       clone (__clone) for has_drop T (Str/Vec/Map/struct/enum). The counterpart of
       __take (which moves out): use __dup when you need an independent copy of a
       value you still own — e.g. Vec.fill(x) writes N copies of x; Map.get_or_insert
       returns a copy of the default it also inserts. Returns the value type. */
    if (intrinsic_lookup(name) && intrinsic_lookup(name)->kind == INTR_PLACE_DUP)
    {
        if (argc != 1)
        {
            checker_error(c, call_node->line, call_node->column,
                          "__dup() takes exactly 1 argument, got %d", argc);
            return NULL;
        }
        Type *arg_type = check_expr(c, args[0]);
        if (arg_type == NULL) return NULL;
        if (args[0]->kind != AST_INDEX && args[0]->kind != AST_FIELD &&
            args[0]->kind != AST_IDENT &&
            !(args[0]->kind == AST_UNARY && args[0]->as.unary.op == TOKEN_STAR))
        {
            checker_error(c, args[0]->line, args[0]->column,
                          "__dup() requires a place expression (p[i], field, or *p)");
            return NULL;
        }
        return arg_type; /* an independent copy of the value's type */
    }

    /* __rawstr("literal") -> *u8 — a raw pointer to a baked .rodata string,
       WITHOUT going through Str. Needed by std.core.reflect_core (a leaf module
       below Str/Vec that cannot import str), whose RawType stores names/signatures
       as *u8+len. The arg must be a string literal; codegen emits its
       GlobalStringPtr directly (same bytes Str's .data points at). Pair it with a
       compile-time length (strlen of the literal) at the call site. */
    if (strcmp(name, "__rawstr") == 0)
    {
        if (argc != 1 || args[0]->kind != AST_STRING_LIT)
        {
            checker_error(c, call_node->line, call_node->column,
                          "__rawstr() takes exactly 1 string-literal argument");
            return NULL;
        }
        return type_pointer(type_u8());
    }

    /* __task_spawn(Block()->T, *T box) -> object — GENERIC structured-concurrency
       intrinsic (std.task). Runs the closure on a worker; codegen synthesises a
       per-T thunk that stores the by-value result into the `*T box` slot. T is
       read by codegen from arg0's Block return type; checker only validates the
       arg shapes and returns the opaque handle type. */
    if (strcmp(name, "__task_spawn") == 0)
    {
        if (argc != 2)
        {
            checker_error(c, call_node->line, call_node->column,
                          "__task_spawn() takes exactly 2 arguments, got %d", argc);
            return NULL;
        }
        Type *bt = check_expr(c, args[0]);
        if (bt == NULL) return NULL;
        if (bt->kind != TYPE_BLOCK)
        {
            checker_error(c, args[0]->line, args[0]->column,
                          "__task_spawn() requires a Block argument, got '%s'",
                          type_name(bt));
            return NULL;
        }
        Type *boxt = check_expr(c, args[1]);
        if (boxt == NULL) return NULL;
        if (boxt->kind != TYPE_POINTER)
        {
            checker_error(c, args[1]->line, args[1]->column,
                          "__task_spawn() requires a pointer box argument, got '%s'",
                          type_name(boxt));
            return NULL;
        }
        return type_object();
    }

    /* __task_join(object) -> void — wait for the worker. The result is read out
       of the Task's box by the LS join() via __take; this only joins the thread. */
    if (strcmp(name, "__task_join") == 0)
    {
        if (argc != 1)
        {
            checker_error(c, call_node->line, call_node->column,
                          "__task_join() takes exactly 1 argument, got %d", argc);
            return NULL;
        }
        Type *h = check_expr(c, args[0]);
        if (h == NULL) return NULL;
        return type_void();
    }

    /* Mutex + spin runtime intrinsics (std.sync) — opaque-handle FFI to the OS
       backend. Global intrinsics (like __task_*) so they survive generic-method
       instantiation in a consumer module without an `import std.c` alias. They
       know nothing about Mutex(T): a handle in/out. */
    if (strncmp(name, "__mutex_", 8) == 0 || strncmp(name, "__rwlock_", 9) == 0 ||
        strncmp(name, "__cond_", 7) == 0 ||
        strcmp(name, "__cpu_relax") == 0 || strcmp(name, "__cpu_yield") == 0)
    {
        int want = 1; /* handle arg */
        Type *ret = type_void();
        if (strcmp(name, "__mutex_init") == 0)         { want = 0; ret = type_object(); }
        else if (strcmp(name, "__mutex_lock") == 0)    { ret = type_int(); }
        else if (strcmp(name, "__mutex_trylock") == 0) { ret = type_int(); }
        else if (strcmp(name, "__mutex_unlock") == 0)  { ret = type_int(); }
        else if (strcmp(name, "__mutex_destroy") == 0) { ret = type_void(); }
        else if (strcmp(name, "__rwlock_init") == 0)     { want = 0; ret = type_object(); }
        else if (strcmp(name, "__rwlock_rdlock") == 0)   { ret = type_int(); }
        else if (strcmp(name, "__rwlock_wrlock") == 0)   { ret = type_int(); }
        else if (strcmp(name, "__rwlock_rdunlock") == 0) { ret = type_int(); }
        else if (strcmp(name, "__rwlock_wrunlock") == 0) { ret = type_int(); }
        else if (strcmp(name, "__rwlock_destroy") == 0)  { ret = type_void(); }
        /* condition variables (std.chan): init()->object, wait(cond,mtx) is the
           only 2-arg sync intrinsic, signal/broadcast/destroy take one handle. */
        else if (strcmp(name, "__cond_init") == 0)      { want = 0; ret = type_object(); }
        else if (strcmp(name, "__cond_wait") == 0)      { want = 2; ret = type_void(); }
        else if (strcmp(name, "__cond_signal") == 0)    { ret = type_void(); }
        else if (strcmp(name, "__cond_broadcast") == 0) { ret = type_void(); }
        else if (strcmp(name, "__cond_destroy") == 0)   { ret = type_void(); }
        else if (strcmp(name, "__cpu_relax") == 0)     { want = 0; ret = type_void(); }
        else if (strcmp(name, "__cpu_yield") == 0)     { want = 0; ret = type_void(); }
        else
        {
            checker_error(c, call_node->line, call_node->column,
                          "unknown sync intrinsic '%s'", name);
            return NULL;
        }
        if (argc != want)
        {
            checker_error(c, call_node->line, call_node->column,
                          "%s() takes %d argument(s), got %d", name, want, argc);
            return NULL;
        }
        for (int i = 0; i < want; i++)
            if (check_expr(c, args[i]) == NULL) return NULL;
        return ret;
    }

    /* Atomic intrinsics (std.atomic) — place-based, SeqCst. arg0 is an lvalue
       place (e.g. self.value); codegen takes its address and emits a single
       inline LLVM atomic instruction. T is the place's scalar type. The
       lock-free-scalar restriction on T is enforced at codegen (a clean
       cg_error pointing users at Mutex for larger types). */
    if (strncmp(name, "__atomic_", 9) == 0)
    {
        if (strcmp(name, "__atomic_fence") == 0)
        {
            if (argc != 0)
            {
                checker_error(c, call_node->line, call_node->column,
                              "__atomic_fence() takes no arguments, got %d", argc);
                return NULL;
            }
            return type_void();
        }
        if (argc < 1)
        {
            checker_error(c, call_node->line, call_node->column,
                          "%s() requires a place expression argument", name);
            return NULL;
        }
        Type *pt = check_expr(c, args[0]);
        if (pt == NULL) return NULL;
        if (args[0]->kind != AST_INDEX && args[0]->kind != AST_FIELD &&
            args[0]->kind != AST_IDENT &&
            !(args[0]->kind == AST_UNARY && args[0]->as.unary.op == TOKEN_STAR))
        {
            checker_error(c, args[0]->line, args[0]->column,
                          "%s() requires a place expression (p[i], field, or *p)", name);
            return NULL;
        }
        for (int i = 1; i < argc; i++)
            if (check_expr(c, args[i]) == NULL) return NULL;

        /* load / load_acquire / load_relaxed — all return the place type */
        if (strncmp(name, "__atomic_load", 13) == 0)
        {
            if (argc != 1) { checker_error(c, call_node->line, call_node->column,
                "%s() takes exactly 1 argument, got %d", name, argc); return NULL; }
            return pt;
        }
        /* store / store_release / store_relaxed — void */
        if (strncmp(name, "__atomic_store", 14) == 0)
        {
            if (argc != 2) { checker_error(c, call_node->line, call_node->column,
                "%s() takes exactly 2 arguments, got %d", name, argc); return NULL; }
            return type_void();
        }
        if (strcmp(name, "__atomic_add") == 0 ||
            strcmp(name, "__atomic_sub") == 0 ||
            strcmp(name, "__atomic_swap") == 0)
        {
            if (argc != 2) { checker_error(c, call_node->line, call_node->column,
                "%s() takes exactly 2 arguments, got %d", name, argc); return NULL; }
            return pt; /* the prior value */
        }
        if (strcmp(name, "__atomic_cas") == 0)
        {
            if (argc != 3) { checker_error(c, call_node->line, call_node->column,
                "__atomic_cas() takes exactly 3 arguments, got %d", argc); return NULL; }
            return type_bool(); /* success */
        }
        checker_error(c, call_node->line, call_node->column,
                      "unknown atomic intrinsic '%s'", name);
        return NULL;
    }

    /* SIMD intrinsics __simd_* — portable vector ops lowering to a single <N x T>
       IR instruction (docs/plan_simd.md §4.2). Producers (zero/splat) take their
       result Simd(T,N) from the expected type (a `Simd(T,N) x = __simd_*(...)`
       context); operand-driven ops derive it from their Simd arguments. */
    if (strncmp(name, "__simd_", 7) == 0)
    {
        for (int i = 0; i < argc; i++)
            if (check_expr(c, args[i]) == NULL) return NULL;

        /* __simd_zero() / __simd_splat(scalar) -> Simd(T,N) from expected type */
        if (strcmp(name, "__simd_zero") == 0 || strcmp(name, "__simd_splat") == 0)
        {
            int want = (strcmp(name, "__simd_splat") == 0) ? 1 : 0;
            if (argc != want) { checker_error(c, call_node->line, call_node->column,
                "%s() takes exactly %d argument(s), got %d", name, want, argc); return NULL; }
            Type *exp = c->expected_type;
            if (exp == NULL || exp->kind != TYPE_SIMD) {
                checker_error(c, call_node->line, call_node->column,
                    "%s() result type cannot be inferred here; use it where a "
                    "Simd(T, N) is expected (e.g. `Simd(f32, 16) v = %s(...)`)",
                    name, name);
                return NULL;
            }
            if (want == 1 && args[0]->resolved_type &&
                !type_is_numeric(args[0]->resolved_type)) {
                checker_error(c, args[0]->line, args[0]->column,
                    "__simd_splat() requires a numeric scalar, got '%s'",
                    type_name(args[0]->resolved_type));
                return NULL;
            }
            return type_simd(type_clone(exp->as.simd.elem), exp->as.simd.lanes);
        }

        /* __simd_lane(v, i) -> element type of v ; reductions -> element type.
           reduce_add/reduce_max/reduce_min collapse <N x T> to a scalar T. */
        if (strcmp(name, "__simd_lane") == 0 || strcmp(name, "__simd_reduce_add") == 0 ||
            strcmp(name, "__simd_reduce_max") == 0 || strcmp(name, "__simd_reduce_min") == 0)
        {
            int want = (strcmp(name, "__simd_lane") == 0) ? 2 : 1;
            if (argc != want) { checker_error(c, call_node->line, call_node->column,
                "%s() takes exactly %d argument(s), got %d", name, want, argc); return NULL; }
            Type *vt = args[0]->resolved_type;
            if (vt == NULL || vt->kind != TYPE_SIMD) {
                checker_error(c, args[0]->line, args[0]->column,
                    "%s() requires a Simd value as the first argument", name); return NULL; }
            return type_clone(vt->as.simd.elem);
        }

        /* __simd_fma(a, b, c) -> Simd(T,N) (all three operands must match) */
        if (strcmp(name, "__simd_fma") == 0)
        {
            if (argc != 3) { checker_error(c, call_node->line, call_node->column,
                "__simd_fma() takes exactly 3 arguments, got %d", argc); return NULL; }
            Type *t0 = args[0]->resolved_type;
            if (t0 == NULL || t0->kind != TYPE_SIMD) {
                checker_error(c, args[0]->line, args[0]->column,
                    "__simd_fma() requires Simd arguments"); return NULL; }
            for (int i = 1; i < 3; i++) {
                if (!type_equals(t0, args[i]->resolved_type)) {
                    checker_error(c, args[i]->line, args[i]->column,
                        "__simd_fma() argument types must all match '%s'",
                        type_name(t0)); return NULL; }
            }
            return type_clone(t0);
        }

        /* __simd_max(a, b) / __simd_min(a, b) -> Simd(T,N) (both operands match).
           Element-wise vmaxps/vminps (float) or smax/umax/... (int). */
        if (strcmp(name, "__simd_max") == 0 || strcmp(name, "__simd_min") == 0)
        {
            if (argc != 2) { checker_error(c, call_node->line, call_node->column,
                "%s() takes exactly 2 arguments, got %d", name, argc); return NULL; }
            Type *t0 = args[0]->resolved_type;
            if (t0 == NULL || t0->kind != TYPE_SIMD) {
                checker_error(c, args[0]->line, args[0]->column,
                    "%s() requires Simd arguments", name); return NULL; }
            if (!type_equals(t0, args[1]->resolved_type)) {
                checker_error(c, args[1]->line, args[1]->column,
                    "%s() argument types must match '%s'", name, type_name(t0)); return NULL; }
            return type_clone(t0);
        }

        /* __simd_load(ptr, off) -> Simd(T,N) from expected type (ptr is *T) */
        if (strcmp(name, "__simd_load") == 0)
        {
            if (argc != 2) { checker_error(c, call_node->line, call_node->column,
                "__simd_load() takes exactly 2 arguments (pointer, offset), got %d", argc); return NULL; }
            Type *pt = args[0]->resolved_type;
            if (pt == NULL || pt->kind != TYPE_POINTER) {
                checker_error(c, args[0]->line, args[0]->column,
                    "__simd_load() first argument must be a pointer *T"); return NULL; }
            if (args[1]->resolved_type == NULL || !type_is_integer(args[1]->resolved_type)) {
                checker_error(c, args[1]->line, args[1]->column,
                    "__simd_load() offset must be an integer"); return NULL; }
            Type *exp = c->expected_type;
            if (exp == NULL || exp->kind != TYPE_SIMD) {
                checker_error(c, call_node->line, call_node->column,
                    "__simd_load() result type cannot be inferred here; use it where "
                    "a Simd(T, N) is expected (e.g. `Simd(f32, 16) v = __simd_load(p, i)`)");
                return NULL;
            }
            return type_simd(type_clone(exp->as.simd.elem), exp->as.simd.lanes);
        }

        /* __simd_store(ptr, off, vec) -> void (ptr is *T) */
        if (strcmp(name, "__simd_store") == 0)
        {
            if (argc != 3) { checker_error(c, call_node->line, call_node->column,
                "__simd_store() takes exactly 3 arguments (pointer, offset, vector), got %d", argc); return NULL; }
            Type *pt = args[0]->resolved_type;
            if (pt == NULL || pt->kind != TYPE_POINTER) {
                checker_error(c, args[0]->line, args[0]->column,
                    "__simd_store() first argument must be a pointer *T"); return NULL; }
            if (args[1]->resolved_type == NULL || !type_is_integer(args[1]->resolved_type)) {
                checker_error(c, args[1]->line, args[1]->column,
                    "__simd_store() offset must be an integer"); return NULL; }
            if (args[2]->resolved_type == NULL || args[2]->resolved_type->kind != TYPE_SIMD) {
                checker_error(c, args[2]->line, args[2]->column,
                    "__simd_store() third argument must be a Simd value"); return NULL; }
            return type_void();
        }

        /* __simd_load_masked(ptr, off, n) -> Simd(T,N): load the first n lanes
           (rest zero). Fringe handling for non-multiple-of-N tails — the mask
           (icmp iota<n) is built internally, hiding the i1 vector. */
        if (strcmp(name, "__simd_load_masked") == 0)
        {
            if (argc != 3) { checker_error(c, call_node->line, call_node->column,
                "__simd_load_masked() takes exactly 3 arguments (pointer, offset, n), got %d", argc); return NULL; }
            Type *pt = args[0]->resolved_type;
            if (pt == NULL || pt->kind != TYPE_POINTER) {
                checker_error(c, args[0]->line, args[0]->column,
                    "__simd_load_masked() first argument must be a pointer *T"); return NULL; }
            if (args[1]->resolved_type == NULL || !type_is_integer(args[1]->resolved_type)) {
                checker_error(c, args[1]->line, args[1]->column,
                    "__simd_load_masked() offset must be an integer"); return NULL; }
            if (args[2]->resolved_type == NULL || !type_is_integer(args[2]->resolved_type)) {
                checker_error(c, args[2]->line, args[2]->column,
                    "__simd_load_masked() lane count must be an integer"); return NULL; }
            Type *exp = c->expected_type;
            if (exp == NULL || exp->kind != TYPE_SIMD) {
                checker_error(c, call_node->line, call_node->column,
                    "__simd_load_masked() result type cannot be inferred here; use it where "
                    "a Simd(T, N) is expected (e.g. `Simd(f32, 16) v = __simd_load_masked(p, i, n)`)");
                return NULL;
            }
            return type_simd(type_clone(exp->as.simd.elem), exp->as.simd.lanes);
        }

        /* __simd_store_masked(ptr, off, vec, n) -> void: store the first n lanes. */
        if (strcmp(name, "__simd_store_masked") == 0)
        {
            if (argc != 4) { checker_error(c, call_node->line, call_node->column,
                "__simd_store_masked() takes exactly 4 arguments (pointer, offset, vector, n), got %d", argc); return NULL; }
            Type *pt = args[0]->resolved_type;
            if (pt == NULL || pt->kind != TYPE_POINTER) {
                checker_error(c, args[0]->line, args[0]->column,
                    "__simd_store_masked() first argument must be a pointer *T"); return NULL; }
            if (args[1]->resolved_type == NULL || !type_is_integer(args[1]->resolved_type)) {
                checker_error(c, args[1]->line, args[1]->column,
                    "__simd_store_masked() offset must be an integer"); return NULL; }
            if (args[2]->resolved_type == NULL || args[2]->resolved_type->kind != TYPE_SIMD) {
                checker_error(c, args[2]->line, args[2]->column,
                    "__simd_store_masked() third argument must be a Simd value"); return NULL; }
            if (args[3]->resolved_type == NULL || !type_is_integer(args[3]->resolved_type)) {
                checker_error(c, args[3]->line, args[3]->column,
                    "__simd_store_masked() lane count must be an integer"); return NULL; }
            return type_void();
        }

        /* __simd_cast(v) -> Simd(U,N) from expected type; v is Simd(T,N) (same N).
           Element-wise numeric conversion (f16<->f32, int<->float, etc.). The
           mixed-precision bridge: load f16, cast to f32, compute, cast back. */
        if (strcmp(name, "__simd_cast") == 0)
        {
            if (argc != 1) { checker_error(c, call_node->line, call_node->column,
                "__simd_cast() takes exactly 1 argument, got %d", argc); return NULL; }
            Type *vt = args[0]->resolved_type;
            if (vt == NULL || vt->kind != TYPE_SIMD) {
                checker_error(c, args[0]->line, args[0]->column,
                    "__simd_cast() requires a Simd value"); return NULL; }
            Type *exp = c->expected_type;
            if (exp == NULL || exp->kind != TYPE_SIMD) {
                checker_error(c, call_node->line, call_node->column,
                    "__simd_cast() result type cannot be inferred here; use it where "
                    "a Simd(U, N) is expected (e.g. `Simd(f32, 16) f = __simd_cast(h)`)");
                return NULL;
            }
            if (exp->as.simd.lanes != vt->as.simd.lanes) {
                checker_error(c, call_node->line, call_node->column,
                    "__simd_cast() cannot change the lane count (%d -> %d)",
                    vt->as.simd.lanes, exp->as.simd.lanes); return NULL; }
            return type_simd(type_clone(exp->as.simd.elem), exp->as.simd.lanes);
        }

        /* __simd_floor(v) -> same Simd(float, N) (round toward -inf). */
        if (strcmp(name, "__simd_floor") == 0)
        {
            if (argc != 1) { checker_error(c, call_node->line, call_node->column,
                "__simd_floor() takes exactly 1 argument, got %d", argc); return NULL; }
            Type *vt = args[0]->resolved_type;
            if (vt == NULL || vt->kind != TYPE_SIMD || !type_is_float(vt->as.simd.elem)) {
                checker_error(c, args[0]->line, args[0]->column,
                    "__simd_floor() requires a float Simd value"); return NULL; }
            return type_simd(type_clone(vt->as.simd.elem), vt->as.simd.lanes);
        }

        /* __simd_bitcast(v) -> Simd(U, N): reinterpret bits, same lane count and
           same element bit-width (e.g. i32 <-> f32). Result type from expected. */
        if (strcmp(name, "__simd_bitcast") == 0)
        {
            if (argc != 1) { checker_error(c, call_node->line, call_node->column,
                "__simd_bitcast() takes exactly 1 argument, got %d", argc); return NULL; }
            Type *vt = args[0]->resolved_type;
            if (vt == NULL || vt->kind != TYPE_SIMD) {
                checker_error(c, args[0]->line, args[0]->column,
                    "__simd_bitcast() requires a Simd value"); return NULL; }
            Type *exp = c->expected_type;
            if (exp == NULL || exp->kind != TYPE_SIMD) {
                checker_error(c, call_node->line, call_node->column,
                    "__simd_bitcast() result type cannot be inferred here; use it where "
                    "a Simd(U, N) is expected (e.g. `Simd(f32, 16) f = __simd_bitcast(i)`)");
                return NULL; }
            if (exp->as.simd.lanes != vt->as.simd.lanes) {
                checker_error(c, call_node->line, call_node->column,
                    "__simd_bitcast() cannot change the lane count (%d -> %d)",
                    vt->as.simd.lanes, exp->as.simd.lanes); return NULL; }
            return type_simd(type_clone(exp->as.simd.elem), exp->as.simd.lanes);
        }

        checker_error(c, call_node->line, call_node->column,
                      "unknown simd intrinsic '%s'", name);
        return NULL;
    }

    return NULL;
}

/* Intrinsic registry — single source of truth for the @-sigil place/ownership
   builtins. Each entry accepts both the canonical @-name and (during migration)
   the legacy __ spelling. */
static const IntrinsicDef k_intrinsics[] = {
    { "@take",    "__take",    INTR_PLACE_TAKE,    1 },
    { "@dispose", "__drop_at", INTR_PLACE_DISPOSE, 1 },
    { "@dup",     "__dup",     INTR_PLACE_DUP,     1 },
    { "@move",    "__move",    INTR_VAR_MOVE,      1 },
};

const IntrinsicDef *intrinsic_lookup(const char *name)
{
    if (name == NULL) return NULL;
    for (size_t i = 0; i < sizeof(k_intrinsics) / sizeof(k_intrinsics[0]); i++) {
        const IntrinsicDef *d = &k_intrinsics[i];
        if (strcmp(name, d->canonical) == 0) return d;
        if (d->legacy != NULL && strcmp(name, d->legacy) == 0) return d;
    }
    return NULL;
}

/* A legacy __ spelling of a now-@ intrinsic. Returns the canonical @-name, or
   NULL. Used to give a clear "retired; use @name" diagnostic (Phase 2). */
static const char *intrinsic_retired_spelling(const char *name)
{
    if (name == NULL) return NULL;
    for (size_t i = 0; i < sizeof(k_intrinsics) / sizeof(k_intrinsics[0]); i++)
        if (k_intrinsics[i].legacy != NULL &&
            strcmp(name, k_intrinsics[i].legacy) == 0)
            return k_intrinsics[i].canonical;
    return NULL;
}

/* Check if a name is a builtin function (so we don't report "undefined variable") */
static bool is_builtin_function(const char *name)
{
    if (intrinsic_lookup(name) != NULL) return true;
    return strcmp(name, "from_cstr") == 0 ||
           strcmp(name, "errno") == 0 ||
           strcmp(name, "__rawstr") == 0 ||
           strcmp(name, "__task_spawn") == 0 ||
           strcmp(name, "__task_join") == 0 ||
           strncmp(name, "__atomic_", 9) == 0 ||
           strncmp(name, "__mutex_", 8) == 0 ||
           strncmp(name, "__rwlock_", 9) == 0 ||
           strncmp(name, "__cond_", 7) == 0 ||
           strncmp(name, "__simd_", 7) == 0 ||
           strcmp(name, "__cpu_relax") == 0 ||
           strcmp(name, "__cpu_yield") == 0;
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
/* CaptureScan moved to checker_internal.h (shared across TUs). */












/* ---- Expression checking ---- */




/* ---- C1: Option/Result combinators (docs/plan_container_access_safety.md §5.3) ----
   unwrap / expect / unwrap_or / is_some? / is_none? / is_ok? / is_err? are lowered
   by the compiler, mirroring `try` and force-unwrap `!` (which are also not library
   methods). `impl` on the builtin Option/Result enum templates is unsupported, and
   generic free functions would need explicit type args at each call site — so the
   checker intercepts `opt.METHOD(args)` and rewrites the AST_CALL in place to either
   a force-unwrap (the two panic combinators) or a 2-arm match expression (the rest),
   then re-checks. This reuses the mature match drop/move machinery and the
   force-unwrap discriminant lowering, introducing NO new ownership code. */
/* OptCombinator moved to checker_internal.h; g_optc_uid + lowering moved to checker_lower.c. */


/* ---- C2b: closure combinators (map / and_then / unwrap_or_else / map_err) ----
   These take a closure argument. To sidestep the closure-as-callee inference gap
   (a bare `|x| body` literal in callee position has no Block expected type), the
   lower INLINES the closure body directly into the lowered match arm, reusing the
   closure's parameter name as the arm binder. No closure value, env, or call is
   created — captured variables resolve in the enclosing scope as ordinary reads.
   The new result type param U is supplied explicitly (`opt.map(U)(|x| ...)`),
   matching the language's existing method-level generic convention
   (`vec.map(int)(...)`); it builds the result Option(U)/Result(...) pushed as the
   expected type so the bare ctors in the arm bodies resolve. */





/* ---- V1 bit-pattern match helpers ---- */




/* ---- Generic free-function value-arg inference + borrow params (Gap 1/2) ---- */

/* Gap 1: does param TypeNode `tn` *directly* name the type parameter `tp_name` —
   either as a bare `T`, or under one reference shell `&T` / `&!T`? Deeper
   structural positions (`Vec(T)`, `Block(T)->U`, `*T`, ...) are out of scope for
   v1 inference; explicit type args still cover those. When the param is a
   reference shell, *is_ref is set so the caller can strip an explicit `&x`
   before probing the argument's value type. */
static bool fn_param_directly_names_tp(const TypeNode *tn, const char *tp_name,
                                       bool *is_ref)
{
    if (is_ref) *is_ref = false;
    if (tn == NULL || tp_name == NULL) return false;
    if (tn->kind == TYPE_NODE_REFERENCE) {
        const TypeNode *inner = tn->as.pointee;
        if (inner && inner->kind == TYPE_NODE_NAMED &&
            inner->as.named.arg_count == 0 && inner->as.named.name &&
            strcmp(inner->as.named.name, tp_name) == 0) {
            if (is_ref) *is_ref = true;
            return true;
        }
        return false;
    }
    return tn->kind == TYPE_NODE_NAMED && tn->as.named.arg_count == 0 &&
           tn->as.named.name && strcmp(tn->as.named.name, tp_name) == 0;
}

/* Reading through a borrow yields the same value type — peel one reference shell
   so the inferred type param is the pointee value (T from a `&T` argument). */
static Type *fn_infer_peel_borrow(Type *t)
{
    return (t && t->kind == TYPE_REFERENCE) ? t->as.pointer_to : t;
}

/* Gap 2 (§13 twin for the generic free-function path): when `pt` is a read-only
   `&T` parameter and the argument is an explicit `&x` / `&obj.f` / `&v[i]`, strip
   the address-of shell so the call takes the proven auto-borrow path (identical
   to passing the lvalue bare). Without this, `&x` types as a raw `*T` and
   mismatches the `&T` formal. `&!x` (AST_MUT_BORROW) and non-place operands keep
   their existing semantics (untouched here). */
static void fn_call_strip_amp_shell(AstNode *call, int ai, const Type *pt)
{
    if (pt == NULL || pt->kind != TYPE_REFERENCE || pt->is_mut) return;
    AstNode *argn = call->as.call.args[ai];
    if (argn && argn->kind == AST_UNARY && argn->as.unary.op == TOKEN_AMP &&
        argn->as.unary.operand &&
        (argn->as.unary.operand->kind == AST_IDENT ||
         argn->as.unary.operand->kind == AST_FIELD ||
         argn->as.unary.operand->kind == AST_INDEX))
        call->as.call.args[ai] = argn->as.unary.operand; /* shell intentionally leaked */
}

/* Stage C-2 / D (docs/plan_print_sink.md): a struct/enum that impls Show renders
   via Show in print() and f-string interpolation. These two helpers detect such a
   type and rewrite an arg slot `x` -> `to_str(x)` (an owned Str) in place. Str is
   excluded (its raw-text form is the desired output); POD/array aren't aggregates.
   The rewrite only fires when the type satisfies Show, which means std.core.show
   is imported (to define/derive the impl), so the bare `to_str` resolves. */
static bool type_is_show_aggregate(Checker *c, Type *t)
{
    if (t == NULL) return false;
    if (t->kind == TYPE_STRUCT)
    {
        if (t->as.strukt.name && strcmp(t->as.strukt.name, "Str") == 0) return false;
        return checker_type_satisfies_trait(c, t, "Show");
    }
    if (t->kind == TYPE_ENUM)
        return checker_type_satisfies_trait(c, t, "Show");
    return false;
}

static void wrap_arg_in_to_str(AstNode **slot)
{
    AstNode *orig = *slot;
    AstNode *callee = ast_new(AST_IDENT, orig->line, orig->column);
    callee->as.ident.name = (char *)malloc_safe(7); /* "to_str"+NUL */
    memcpy(callee->as.ident.name, "to_str", 7);
    AstNode *call = ast_new(AST_CALL, orig->line, orig->column);
    call->as.call.callee = callee;
    call->as.call.args = (AstNode **)malloc_safe(sizeof(AstNode *));
    call->as.call.args[0] = orig;
    call->as.call.arg_count = 1;
    *slot = call;
}

Type *check_expr(Checker *c, AstNode *node)
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
        /* P5-4 S-2: a string literal IS a (static) Str — the builtin string
           type is gone. Codegen emits a static Str struct value. */
        {
            Type *strt = str_target_of_expected(c->expected_type);
            if (strt == NULL) strt = checker_str_type(c);
            if (strt == NULL)
            {
                checker_error(c, node->line, node->column,
                              "string literal requires the Str type from std.core.str "
                              "(add `import std.core.str`)");
                result = NULL;
                break;
            }
            result = strt;
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
            /* Stage D: a Show struct/enum interpolates via Show — rewrite to
               to_str(expr) (Str), then fall through to the printable check (Str
               passes). Mirrors print()'s C-2 rewrite. */
            if (type_is_show_aggregate(c, et))
            {
                wrap_arg_in_to_str(&node->as.format_string.exprs[i]);
                et = check_expr(c, node->as.format_string.exprs[i]);
                if (et == NULL)
                    continue;
            }
            /* Ensure the expression is a printable type. The pure-LS `Str` is
               printable too (interpolated via "%.*s" by codegen). */
            if (!type_is_numeric(et) && et->kind != TYPE_BOOL && et->kind != TYPE_POINTER && et->kind != TYPE_OBJECT && !type_is_str_struct(et))
            {
                checker_error(c, node->as.format_string.exprs[i]->line,
                              node->as.format_string.exprs[i]->column,
                              "cannot interpolate type '%s' in format string",
                              type_name(et));
            }
        }
        c->expected_type = fstr_expected;
        /* P5-4 S-2: an f-string IS an OWNED Str rvalue (the formatted heap
           buffer wrapped as Str, cap>0), routed through the unified has_drop
           temp/drop path. In a read-only `&Str` position the owned rvalue is
           auto-borrowed via the generic struct-arg spill. */
        {
            Type *strt = str_target_of_expected(fstr_expected);
            if (strt == NULL) strt = checker_str_type(c);
            if (strt == NULL)
            {
                checker_error(c, node->line, node->column,
                              "f-string requires the Str type from std.core.str "
                              "(add `import std.core.str`)");
                result = NULL;
                break;
            }
            result = strt;
        }
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
            /* `{}` only infers its type in a typed declaration / field / arg
               position. In an assignment (`v = {}`) there is no type to infer
               here — point at the two real options instead of the bare error. */
            checker_error(c, node->line, node->column,
                          "empty `{}` has no inferable type here; use it in a typed "
                          "declaration (e.g. `Vec(T) v = {}` / `Map(K,V) m = {}`), "
                          "or to empty an existing container call `v.clear()` "
                          "instead of `v = {}`");
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
            /* Ambient builtin module: a generic method body (e.g. std.tensor's
               exp/sigmoid/tanh using `math.exp`) is re-checked at the CONSUMER
               site, where an `import math` alias is not in scope — it would
               otherwise fail with "undefined variable 'math'". A builtin module
               name (math/perf/...) that is NOT shadowed by a local symbol and
               has no overriding user .ls file resolves on demand here, mirroring
               the ambient std.sys.c.* canonical path (match_stdc_prim). A user
               variable of the same name is found by scope_resolve above, so it
               always wins. */
            if (builtin_module_exists(node->as.ident.name) &&
                !module_user_file_exists(node->as.ident.name, c->source_path))
            {
                Type *mt = builtin_module_make_type_merged(c, node->as.ident.name);
                if (mt)
                {
                    node->resolved_type = mt;
                    result = mt;
                    break;
                }
            }
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
            /* #3 hint: a bare dotted import (`import std.text.csv`) binds only
               the full path, not the short segment. If `name` is the last
               segment of an imported module's path, the user almost certainly
               meant a namespaced call (`csv.parse`) — point them at the alias
               form instead of a bare "undefined variable". */
            const char *modpath = NULL;
            {
                size_t nl = strlen(node->as.ident.name);
                for (Scope *sc = c->current_scope; sc && !modpath; sc = sc->parent)
                {
                    for (int si = 0; si < sc->count; si++)
                    {
                        Type *st = sc->symbols[si].type;
                        const char *sn = sc->symbols[si].name;
                        if (st && st->kind == TYPE_MODULE && sn)
                        {
                            size_t snl = strlen(sn);
                            if (snl > nl + 1 && sn[snl - nl - 1] == '.' &&
                                strcmp(sn + snl - nl, node->as.ident.name) == 0)
                            {
                                modpath = sn;
                                break;
                            }
                        }
                    }
                }
            }
            if (modpath)
                checker_error(c, node->line, node->column,
                              "undefined variable '%s'; for module '%s' add an alias: "
                              "`import %s as %s` (a plain dotted import binds only the "
                              "full path)", node->as.ident.name, modpath, modpath,
                              node->as.ident.name);
            else
                checker_error(c, node->line, node->column,
                              "undefined variable '%s'", node->as.ident.name);
            result = NULL;
        }
        else if (sym->is_comptime_const)
        {
            /* docs/plan_comptime_consteval.md: fold the reference into a literal in
               place — codegen never sees the name, only the constant value (zero
               runtime storage / zero codegen). The IDENT and the target literal
               share the union, so free the ident payload before overwriting. */
            Type *ct = sym->type;
            free(node->as.ident.name);
            if (node->as.ident.type_args) {
                for (int ti = 0; ti < node->as.ident.type_arg_count; ti++)
                    type_node_free(node->as.ident.type_args[ti]);
                free(node->as.ident.type_args);
            }
            if (sym->ct_is_float) {
                node->kind = AST_FLOAT_LIT;
                node->as.float_lit.value = sym->ct_f;
            } else if (ct->kind == TYPE_BOOL) {
                node->kind = AST_BOOL_LIT;
                node->as.bool_lit.value = (sym->ct_i != 0);
            } else {
                node->kind = AST_INT_LIT;
                node->as.int_lit.value   = sym->ct_i;
                node->as.int_lit.is_char = (ct->kind == TYPE_CHAR);
            }
            node->resolved_type = ct;
            result = ct;
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
        /* &!x — explicit writable borrow. Operand is either an IDENT of an
           owned, non-moved, non-borrow struct, or a field access `base.field`
           (writable borrow of a struct field — arg-only, non-escaping). */
        AstNode *op = node->as.mut_borrow.operand;
        if (op == NULL || (op->kind != AST_IDENT && op->kind != AST_FIELD))
        {
            checker_error(c, node->line, node->column,
                          "&! requires a variable name or field access "
                          "(got a non-lvalue expression)");
            result = NULL;
            break;
        }
        if (op->kind == AST_FIELD)
        {
            /* &!base.field — writable borrow of a field. Sound contained
               subset: the field outlives the call and `&!` is arg-only
               (borrow extension guards), so it cannot escape. The field may be
               a struct (Vec/Map/Str/...) or a POD scalar (int/f64/...) — both
               are valid &!T payloads for a Block(&!T) (e.g. SpinGuard(int)).
               The access root must be mutable (not a read-only borrow). */
            Type *ft = check_expr(c, op);
            if (ft == NULL) { result = NULL; break; }
            AstNode *root = op;
            while (root->kind == AST_FIELD)
                root = root->as.field_access.object;
            if (root->kind == AST_IDENT)
            {
                Symbol *rs = scope_resolve(c->current_scope,
                                           root->as.ident.name);
                if (rs != NULL && rs->is_borrow && !rs->is_mut_borrow)
                {
                    checker_error(c, node->line, node->column,
                                  "&!: cannot take writable borrow through "
                                  "read-only borrow '%s'", root->as.ident.name);
                    result = NULL;
                    break;
                }
            }
            op->resolved_type = ft;
            result = type_mut_reference(ft);
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
        if (sym->type == NULL || sym->type->kind != TYPE_STRUCT)
        {
            checker_error(c, node->line, node->column,
                              "&!: only &!struct is supported, got &!%s",
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
        /* Memoize: a binary node is type-checked exactly once during the normal
           tree walk. The only re-entry is operator-overload lowering, which REUSES
           the already-checked operands as the lowered call's object/arg (see
           try_operator_overload). Returning the cached type here makes that re-entry
           O(1); without it, re-checking would re-lower and recurse into the left
           subtree — O(2^n) over a `a + b + c + ...` chain. Generic-body re-checks are
           unaffected (those run on freshly cloned, unresolved nodes). */
        if (node->resolved_type != NULL)
        {
            result = node->resolved_type;
            break;
        }
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

        /* SIMD elementwise arithmetic: Simd(T,N) OP Simd(T,N) -> Simd(T,N), for
           + - * /. Intercepted before the scalar op switch (type_is_numeric is
           false for Simd, so the scalar path would reject it). */
        if (left->kind == TYPE_SIMD || right->kind == TYPE_SIMD)
        {
            TokenType sop = node->as.binary.op;
            bool arith = (sop == TOKEN_PLUS || sop == TOKEN_MINUS ||
                          sop == TOKEN_STAR || sop == TOKEN_SLASH);
            if (!arith)
            {
                checker_error(c, node->line, node->column,
                    "Simd vectors support only + - * /, got '%s' and '%s'",
                    type_name(left), type_name(right));
                result = NULL;
            }
            else if (!type_equals(left, right))
            {
                checker_error(c, node->line, node->column,
                    "Simd arithmetic requires matching vector types, got '%s' and '%s'",
                    type_name(left), type_name(right));
                result = NULL;
            }
            else if (left->kind == TYPE_SIMD && left->as.simd.elem->kind == TYPE_BF16)
            {
                /* bf16 has no native vector arithmetic on x86 (it would silently
                   promote to f32). Per design, reject and steer to f32 accumulation
                   (load/convert via __simd_cast(f32, v)). */
                checker_error(c, node->line, node->column,
                    "bf16 vectors are storage/convert only — no native arithmetic; "
                    "convert to Simd(f32, N) (e.g. __simd_cast(f32, v)) to compute");
                result = NULL;
            }
            else
            {
                result = type_clone(left);
            }
            break;
        }

        switch (node->as.binary.op)
        {
        /* Arithmetic: +, -, *, /, % */
        case TOKEN_PLUS:
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
            if (!type_is_numeric(left) || !type_is_numeric(right))
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
        /* Slice builtin `s.len()` — the borrowed view's element count. Intercept
           before struct/method dispatch (slices are not structs). */
        if (node->as.call.callee && node->as.call.callee->kind == AST_FIELD &&
            node->as.call.callee->as.field_access.field &&
            strcmp(node->as.call.callee->as.field_access.field, "len") == 0)
        {
            Type *recv = check_expr(c, node->as.call.callee->as.field_access.object);
            if (recv && recv->kind == TYPE_SLICE)
            {
                if (node->as.call.arg_count != 0)
                    checker_error(c, node->line, node->column,
                                  "slice 'len' takes no arguments");
                result = type_int();
                node->resolved_type = result;
                break;
            }
        }

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
                                  "std.sys.c.%s expects %d argument(s), got %d",
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

        /* Phase 1 (docs/plan_module_fn_resolution.md): canonical module-path call
           `mod.path.fn(...)` (no alias). Collapse the prefix into a single IDENT so
           the normal module-call path resolves + emits it. Mutates callee in place;
           fall through to normal handling (which now sees an alias-shaped callee). */
        rewrite_canonical_module_call(c, node->as.call.callee);

        /* G2: generic free-function call.
             - explicit type args:  identity(int)(42)
             - inferred type args:  to_csv(p)   (Gap 1) — when the call omits the
               `(T)` list, unify each type param against the value-argument types.
           Enter whenever the callee names a known fn-template, OR type args were
           given explicitly (so a stray `foo(int)(...)` on a non-template name
           still reports "not a generic function"). */
        if (node->as.call.callee->kind == AST_IDENT &&
            (node->as.call.type_arg_count > 0 ||
             find_fn_template(c, node->as.call.callee->as.ident.name) >= 0))
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
            bool inferring = (node->as.call.type_arg_count == 0);
            if (!inferring && node->as.call.type_arg_count != tp_count) {
                checker_error(c, node->line, node->column,
                    "'%s' expects %d type argument(s), got %d",
                    fn_name, tp_count, node->as.call.type_arg_count);
                result = NULL;
                break;
            }

            AstNode *tmpl_decl0 = c->fn_templates[tmpl_idx].decl_node;
            char **tp_names0 = c->fn_templates[tmpl_idx].type_params;

            /* Resolve type arguments — explicit (resolve TypeNodes) or inferred
               (Gap 1: unify each type param against the corresponding value arg). */
            Type **type_args = (Type **)malloc_safe((size_t)tp_count * sizeof(Type *));
            bool type_args_ok = true;
            if (inferring) {
                int pc0 = tmpl_decl0->as.fn_decl.param_count;
                int argc0 = node->as.call.arg_count;
                for (int ti = 0; ti < tp_count; ti++) type_args[ti] = NULL;
                for (int ti = 0; ti < tp_count && type_args_ok; ti++) {
                    const char *tname = tp_names0[ti];
                    for (int pi = 0; pi < pc0 && pi < argc0; pi++) {
                        bool is_ref = false;
                        if (!fn_param_directly_names_tp(
                                tmpl_decl0->as.fn_decl.param_types[pi], tname, &is_ref))
                            continue;
                        /* Gap 2 pre-strip: explicit `&x` against a read-only `&T`
                           param — drop the address-of shell so the probe reads the
                           lvalue's value type (mirrors §13). */
                        if (is_ref) {
                            AstNode *argn = node->as.call.args[pi];
                            if (argn->kind == AST_UNARY &&
                                argn->as.unary.op == TOKEN_AMP &&
                                argn->as.unary.operand &&
                                (argn->as.unary.operand->kind == AST_IDENT ||
                                 argn->as.unary.operand->kind == AST_FIELD ||
                                 argn->as.unary.operand->kind == AST_INDEX))
                                node->as.call.args[pi] = argn->as.unary.operand;
                        }
                        Type *at = check_expr(c, node->as.call.args[pi]);
                        if (at) type_args[ti] = fn_infer_peel_borrow(at);
                        break;
                    }
                    if (!type_args[ti]) {
                        checker_error(c, node->line, node->column,
                            "cannot infer type parameter '%s' of generic function "
                            "'%s' from the arguments; pass it explicitly as "
                            "%s(<type>)(...)", tname, fn_name, fn_name);
                        type_args_ok = false;
                    }
                }
            } else {
                for (int ti = 0; ti < tp_count; ti++) {
                    type_args[ti] = resolve_type_node(c, node->as.call.type_args[ti],
                        node->line, node->column);
                    if (!type_args[ti]) { type_args_ok = false; break; }
                }
            }
            if (!type_args_ok) { free(type_args); result = NULL; break; }

            /* Stash the resolved (concrete) type-arg names so codegen mangles the
               call to the instantiated symbol (mirrors the method-generic
               `resolved_type_args` mechanism). Inferred calls carry no `type_args`
               at all and MUST use this. Explicit calls also need it whenever the
               type args were resolved through aliases — e.g. `make(T)(..)` inside a
               generic body, where the alias T→int is checker-transient: codegen has
               no alias context, so re-mangling from the raw TypeNode would emit the
               abstract `make(T)` instead of the instantiated `make(int)`. The
               clone is checked fresh each instantiation, so this node starts NULL. */
            if (node->as.call.resolved_type_args == NULL) {
                char taj[512];
                int tp = 0;
                for (int ti = 0; ti < tp_count && tp < (int)sizeof(taj) - 1; ti++) {
                    if (ti > 0) tp += snprintf(taj + tp, sizeof(taj) - (size_t)tp, ",");
                    tp += snprintf(taj + tp, sizeof(taj) - (size_t)tp, "%s",
                                   type_name(type_args[ti]));
                }
                node->as.call.resolved_type_args = chk_strdup(taj);
            }

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
                                    "type '%s' does not satisfy interface '%s' "
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
                    Type *pt = fn_t->as.function.params[ai];
                    /* Gap 2: auto-borrow + explicit `&x` for read-only `&T` params,
                       matching the normal call path (type_assignable covers the
                       `&T ← T` auto-borrow and widening). */
                    fn_call_strip_amp_shell(node, ai, pt);
                    checker_tag_user_from_list_literal(c, pt,
                        node->as.call.args[ai], "argument list-literal");
                    Type *saved_exp = c->expected_type;
                    c->expected_type = pt;
                    Type *at = check_expr(c, node->as.call.args[ai]);
                    c->expected_type = saved_exp;
                    if (at && pt && !type_assignable(pt, at)) {
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
            checker_reject_borrow_return(c, ret, NULL, node->line, node->column);  /* Phase 0/2: generic, defer */
            Type *fn_type = type_function(params, pc, ret, false);

            /* Register in scope so subsequent calls reuse */
            scope_define(c->current_scope, mangled, fn_type);

            /* Clone the fn body and type-check it */
            AstNode *cloned = ast_clone_deep(tmpl_decl);
            cloned->resolved_type = fn_type;
            cloned->as.fn_decl.type_param_count = 0; /* concrete now */
            cloned->as.fn_decl.type_param_bounds = NULL; /* don't double-free template bounds */

            chk_push_scope(c);
            for (int pi = 0; pi < pc; pi++) {
                /* Mirror the non-generic / method-generic body-param registration:
                   unwrap a `&T` / `&!T` param to its pointee for the body-local
                   symbol and flag the borrow. Without this the symbol carries the
                   bare reference type, so field access resolves the object IDENT to
                   `&Struct` and codegen takes the is_ref_value path (load + GEP on a
                   struct value) instead of GEP-ing the borrow pointer directly —
                   the Gap-2 codegen miscompile for generic free-function `&T`. */
                Type *sym_type = params[pi];
                bool is_borrow = false, is_mut_borrow = false;
                if (sym_type && sym_type->kind == TYPE_REFERENCE) {
                    if (sym_type->is_mut) is_mut_borrow = true;
                    else                  is_borrow = true;
                    sym_type = sym_type->as.pointer_to;
                }
                Symbol *psym = scope_define(c->current_scope,
                    cloned->as.fn_decl.param_names[pi], sym_type);
                if (psym) {
                    psym->is_borrow = is_borrow;
                    psym->is_mut_borrow = is_mut_borrow;
                    /* F.2: an explicit Block param is a shallow-copy borrow; a bare
                       type-param `T` that monomorphizes to Block is owned (moved). */
                    if (sym_type && sym_type->kind == TYPE_BLOCK) {
                        bool is_tparam = false;
                        TypeNode *ptn = cloned->as.fn_decl.param_types
                                        ? cloned->as.fn_decl.param_types[pi] : NULL;
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
            Type *saved_ret = c->current_fn_return;
            c->current_fn_return = ret;
            check_stmt(c, cloned->as.fn_decl.body);
            checker_elide_last_use(c, cloned); /* A1 clone-elision */
            c->current_fn_return = saved_ret;
            chk_pop_scope(c);

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
                Type *pt = params[ai];
                /* Gap 2: auto-borrow + explicit `&x` for read-only `&T` params,
                   matching the normal call path. */
                fn_call_strip_amp_shell(node, ai, pt);
                checker_tag_user_from_list_literal(c, pt,
                    node->as.call.args[ai], "argument list-literal");
                Type *saved_exp = c->expected_type;
                c->expected_type = pt;
                Type *at = check_expr(c, node->as.call.args[ai]);
                c->expected_type = saved_exp;
                if (at && pt && !type_assignable(pt, at)) {
                    checker_error(c, node->as.call.args[ai]->line,
                        node->as.call.args[ai]->column,
                        "argument %d: expected '%s', got '%s'",
                        ai + 1, type_name(pt), type_name(at));
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
                strcmp(obj_type->as.module.name, "std.core.math") == 0)
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

            /* L-002: interface-qualified call `Iface.method(recv, args...)`. The
               leading IDENT names a known interface, not a value/type. The receiver
               is args[0]; REWRITE into an ordinary instance call so all the existing
               receiver-resolution / borrow-gating / arg-checking below applies:
                   Iface.m(recv, a1, ...)  ==>  recv.m(a1, ...)
               and stamp node.qualified_iface so the resolution step picks the
               interface overload (and codegen mangles the symbol when contended).
               (Zero parser changes — the token shape `Ident.Ident(args)` is identical
               to a static call; we recognize the interface name here.) */
            if (obj_node->kind == AST_IDENT &&
                checker_is_known_interface(c, obj_node->as.ident.name))
            {
                if (node->as.call.arg_count < 1)
                {
                    checker_error(c, node->line, node->column,
                        "interface-qualified call '%s.%s' requires a receiver "
                        "argument, e.g. '%s.%s(recv)'",
                        obj_node->as.ident.name, method_name,
                        obj_node->as.ident.name, method_name);
                    result = NULL;
                    break;
                }
                /* Stamp the interface name (owned). The resolution step frees it
                   again if the method turns out not to be contended (plain `T.m`). */
                free(node->as.call.qualified_iface);
                node->as.call.qualified_iface = chk_strdup(obj_node->as.ident.name);
                /* recv = args[0]. Strip an explicit borrow shell `&x` / `&!x`: the
                   instance-method machinery auto-borrows the receiver (takes its
                   address for self), so a borrow wrapper would make self a
                   pointer-to-pointer. Mirrors fn_call_strip_amp_shell. */
                AstNode *recv = node->as.call.args[0];
                if (recv->kind == AST_MUT_BORROW && recv->as.mut_borrow.operand)
                {
                    AstNode *inner = recv->as.mut_borrow.operand;
                    recv->as.mut_borrow.operand = NULL;
                    ast_free(recv);
                    recv = inner;
                }
                else if (recv->kind == AST_UNARY && recv->as.unary.op == TOKEN_AMP &&
                         recv->as.unary.operand &&
                         (recv->as.unary.operand->kind == AST_IDENT ||
                          recv->as.unary.operand->kind == AST_FIELD ||
                          recv->as.unary.operand->kind == AST_INDEX))
                {
                    AstNode *inner = recv->as.unary.operand;
                    recv->as.unary.operand = NULL;
                    ast_free(recv);
                    recv = inner;
                }
                /* install recv as the field object, shift the remaining args left. */
                for (int ai = 1; ai < node->as.call.arg_count; ai++)
                    node->as.call.args[ai - 1] = node->as.call.args[ai];
                node->as.call.arg_count--;
                node->as.call.callee->as.field_access.object = recv;
                ast_free(obj_node);   /* detached interface IDENT */
                obj_node = recv;
            }

            /* ③ case B: `Box(Str).reflect()` with a USER-TYPE arg parses as a call
               `Box(Str)` (Str is an IDENT, ambiguous with a value at parse time) —
               object is AST_CALL(callee=IDENT, args=[type-name idents]). Disambiguate
               HERE with type info: if the callee names a GENERIC STRUCT TEMPLATE (you
               can't "call" a struct, so this can only be an instantiation) and every
               arg is a bare type name, REWRITE the object into an AST_IDENT carrying
               type args — then the case-A branch below instantiates + dispatches it.
               A real `make_box(cfg).render()` call-chain is untouched (make_box is not
               a generic struct template → find_struct_template_idx returns -1). */
            if (obj_node->kind == AST_CALL &&
                obj_node->as.call.callee &&
                obj_node->as.call.callee->kind == AST_IDENT &&
                obj_node->as.call.arg_count > 0 &&
                obj_node->as.call.type_arg_count == 0 &&
                find_struct_template_idx(c, obj_node->as.call.callee->as.ident.name) >= 0)
            {
                bool all_type_idents = true;
                for (int ai = 0; ai < obj_node->as.call.arg_count; ai++)
                    if (obj_node->as.call.args[ai] == NULL ||
                        obj_node->as.call.args[ai]->kind != AST_IDENT)
                        { all_type_idents = false; break; }
                if (all_type_idents)
                {
                    const char *gname = obj_node->as.call.callee->as.ident.name;
                    int ac = obj_node->as.call.arg_count;
                    AstNode *idn = ast_new(AST_IDENT, obj_node->line, obj_node->column);
                    size_t gl = strlen(gname) + 1;
                    idn->as.ident.name = (char *)malloc_safe(gl);
                    memcpy(idn->as.ident.name, gname, gl);
                    idn->as.ident.type_args =
                        (TypeNode **)malloc_safe((size_t)ac * sizeof(TypeNode *));
                    idn->as.ident.type_arg_count = ac;
                    for (int ai = 0; ai < ac; ai++)
                    {
                        const char *an = obj_node->as.call.args[ai]->as.ident.name;
                        TypeNode *atn = (TypeNode *)malloc_safe(sizeof(TypeNode));
                        memset(atn, 0, sizeof(TypeNode));
                        atn->kind = TYPE_NODE_NAMED;
                        size_t al = strlen(an) + 1;
                        atn->as.named.name = (char *)malloc_safe(al);
                        memcpy(atn->as.named.name, an, al);
                        idn->as.ident.type_args[ai] = atn;
                    }
                    ast_free(obj_node);
                    node->as.call.callee->as.field_access.object = idn;
                    obj_node = idn;
                }
            }

            /* ③: static call on a parameterized generic instance written directly,
               `Box(int).reflect()` / `Box(int).from_value(v)`. The parser produced an
               AST_IDENT carrying type args (for type-keyword args). Instantiate
               name(type_args) into the concrete struct/enum type and dispatch the
               static method on it, mirroring the `type BI = Box(int); BI.reflect()`
               alias path (which find_type_alias resolves below). Stamp resolved_type
               so codegen derives the instance's symbol. */
            if (obj_node->kind == AST_IDENT && obj_node->as.ident.type_arg_count > 0)
            {
                TypeNode tn;
                memset(&tn, 0, sizeof(tn));
                tn.kind = TYPE_NODE_NAMED;
                tn.as.named.name = (char *)obj_node->as.ident.name;
                tn.as.named.args = obj_node->as.ident.type_args;
                tn.as.named.arg_count = obj_node->as.ident.type_arg_count;
                Type *inst = resolve_type_node(c, &tn, node->line, node->column);
                if (inst && (inst->kind == TYPE_STRUCT || inst->kind == TYPE_ENUM))
                {
                    const char *inst_key = impl_key_of_type(inst);
                    if (inst_key)
                    {
                        int si = method_is_static(c, inst_key, method_name);
                        if (si < 0 && inst->kind == TYPE_STRUCT &&
                            inst->as.strukt.generic_base)
                        {
                            ensure_generic_struct_impls_local(c, inst);
                            si = method_is_static(c, inst_key, method_name);
                        }
                        if (si >= 0)
                        {
                            method_struct = inst_key;
                            is_static_call = true;
                            obj_node->resolved_type = inst; /* codegen symbol source */
                            if (si == 0)
                            {
                                checker_error(c, node->line, node->column,
                                    "cannot call instance method '%s' on type '%s'; use an instance",
                                    method_name, inst_key);
                                result = NULL;
                                break;
                            }
                        }
                    }
                }
            }

            /* Check if obj is a struct type name (static call: Point.origin()) */
            if (obj_node->kind == AST_IDENT && !is_static_call)
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

                /* Static call via a generic type parameter: `T.zero()` where T is a
                   type alias bound during monomorphization (T → Complex(f64) / int).
                   find_struct_type/find_enum_type miss on the bare param name; resolve
                   it through the type-alias table, then dispatch the static method on
                   the concrete type. Stamp obj_node->resolved_type so codegen derives
                   the right symbol (Struct.llvm_name.method / int.method). */
                if (obj_node->kind == AST_IDENT && !is_static_call)
                {
                    Type *al = find_type_alias(c, obj_node->as.ident.name);
                    if (al)
                    {
                        const char *al_key = (al->kind == TYPE_STRUCT || al->kind == TYPE_ENUM)
                                                 ? impl_key_of_type(al)
                                                 : type_impl_name(al);
                        if (al_key)
                        {
                            int si = method_is_static(c, al_key, method_name);
                            if (si < 0 && al->kind == TYPE_STRUCT && al->as.strukt.generic_base)
                            {
                                ensure_generic_struct_impls_local(c, al);
                                si = method_is_static(c, al_key, method_name);
                            }
                            if (si >= 0)
                            {
                                method_struct = al_key;
                                is_static_call = true;
                                obj_node->resolved_type = al; /* codegen symbol source */
                                if (si == 0)
                                {
                                    checker_error(c, node->line, node->column,
                                                  "cannot call instance method '%s' on type parameter; use an instance",
                                                  method_name);
                                    result = NULL;
                                    break;
                                }
                            }
                        }
                    }
                }

                /* Static call on a literal primitive type name: `int.from_value(v)`
                   / `bool.show()`. Arises from the comptime `f.type` handle lowering
                   to the field's concrete type name (and is a reasonable spelling on
                   its own). find_struct_type / find_enum_type / find_type_alias all
                   miss a bare primitive keyword-name; resolve it via the builtin-type
                   table (same key as the T-alias-to-primitive path: type_impl_name). */
                if (obj_node->kind == AST_IDENT && !is_static_call)
                {
                    Type *bt = resolve_builtin_type_by_name(obj_node->as.ident.name);
                    if (bt)
                    {
                        const char *bt_key = type_impl_name(bt);
                        if (bt_key)
                        {
                            int si = method_is_static(c, bt_key, method_name);
                            if (si >= 0)
                            {
                                method_struct = bt_key;
                                is_static_call = true;
                                obj_node->resolved_type = bt; /* codegen symbol source */
                                if (si == 0)
                                {
                                    checker_error(c, node->line, node->column,
                                                  "cannot call instance method '%s' on type '%s'; use an instance",
                                                  method_name, bt_key);
                                    result = NULL;
                                    break;
                                }
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

                /* Check if obj is an instance of a struct */
                if (obj_type)
                {
                    /* Auto-deref a pointer (*T) or reference (&T / &!T) receiver
                       to its pointee struct/enum, so a method call whose receiver
                       is a borrow-returning call result — e.g.
                       `v.get_ref(i).eq?(x)` where get_ref returns &T — dispatches
                       as an instance method (self auto-passed). Without unwrapping
                       the reference, `deref` stays TYPE_REFERENCE, neither the
                       struct/enum nor the builtin branch fires, is_method_call
                       stays false, and the call falls through to the generic
                       field-access path (line 4259) which counts self as an
                       explicit argument → "wrong number of arguments: expected N,
                       got N-1". Mirrors the AST_FIELD auto-deref (it already
                       unwraps a reference result for field/method access). */
                    Type *deref = obj_type;
                    if ((deref->kind == TYPE_POINTER ||
                         deref->kind == TYPE_REFERENCE) && deref->as.pointer_to &&
                        (deref->as.pointer_to->kind == TYPE_STRUCT ||
                         deref->as.pointer_to->kind == TYPE_ENUM))
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
                                            "declare it as 'def %s(&self ...)' or "
                                            "'def %s(&!self ...)' to allow calling on borrows",
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
                                            "declare it as 'def %s(&self ...)' or "
                                            "'def %s(&!self ...)' to allow calling on borrows",
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

            /* L-002: interface-qualified call `Iface.m(recv)` (rewritten above to
               `recv.m(...)` with qualified_iface stamped). Select the interface
               overload by origin rather than the inherent-preferring find_method,
               and keep qualified_iface set ONLY if the method is contended (so
               codegen mangles to `T.<Iface>.m`; a single-provider interface keeps
               the plain `T.m`). */
            if (node->as.call.qualified_iface && is_method_call)
            {
                const char *qi = node->as.call.qualified_iface;
                callee_type = find_method_origin(c, method_struct, method_name, qi);
                if (callee_type == NULL)
                {
                    checker_error(c, node->line, node->column,
                        "interface '%s' has no method '%s' for type '%s'",
                        qi, method_name, method_struct);
                    result = NULL;
                    break;
                }
                node->as.call.callee->resolved_type = callee_type;
                int inh = 0, ifc = 0;
                method_providers(c, method_struct, method_name, &inh, &ifc, NULL, NULL);
                if (inh + ifc < 2)
                {
                    free(node->as.call.qualified_iface);  /* not contended → plain T.m */
                    node->as.call.qualified_iface = NULL;
                }
                /* L-002 v2: for a generic instance, force-instantiate THIS overload's
                   body, keyed by the iface-aware lazy symbol (`T.<Iface>.m` when
                   contended, else `T.m`). No-op for non-generic types (no lazy entry).
                   Without this the qualified path would skip instantiation → JIT
                   "Symbols not found". */
                {
                    char qsym[512];
                    if (node->as.call.qualified_iface)
                        snprintf(qsym, sizeof(qsym), "%s.%s.%s", method_struct,
                                 node->as.call.qualified_iface, method_name);
                    else
                        snprintf(qsym, sizeof(qsym), "%s.%s", method_struct, method_name);
                    ensure_generic_method_instantiated_sym(c, method_struct, qsym,
                                                           node->line, node->column);
                }
                goto after_method_check;
            }

            /* L-002: bare instance dispatch `obj.m()` where `m` is ambiguous —
               no inherent provider and >=2 interfaces provide it. The user must
               disambiguate with a qualified call `Iface.m(recv)`. (Inherent
               priority resolves the "inherent + interface" overlap silently;
               only the "all-interface, >=2" case is irresolvable here.) */
            if (is_method_call && !is_static_call)
            {
                int inh = 0, ifc = 0; const char *ia = NULL, *ib = NULL;
                method_providers(c, method_struct, method_name, &inh, &ifc, &ia, &ib);
                if (inh == 0 && ifc >= 2)
                {
                    checker_error(c, node->line, node->column,
                        "ambiguous method '%s' on type '%s': provided by interfaces "
                        "'%s' and '%s'; disambiguate with a qualified call, "
                        "e.g. '%s.%s(recv)'",
                        method_name, method_struct, ia, ib, ia, method_name);
                    result = NULL;
                    break;
                }
            }

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
                    NULL, node->line, node->column);
                if (concrete) {
                    callee_type = concrete;
                    node->as.call.callee->resolved_type = callee_type;
                    /* Stash the resolved (concrete) method-level type-arg names so
                       codegen mangles the call as `Type.method(int)` not
                       `Type.method(T)` when called with an abstract type param
                       inside a generic body. The alias (T→int) is active here, so
                       resolve_type_node yields the concrete type; codegen has no
                       alias context and would otherwise re-mangle the raw `T`.
                       Mirrors the closure-inference and free-function paths. */
                    if (node->as.call.resolved_type_args == NULL) {
                        char taj[512];
                        int tp = 0;
                        for (int ti = 0; ti < node->as.call.type_arg_count &&
                                         tp < (int)sizeof(taj) - 1; ti++) {
                            Type *rt = resolve_type_node(c, node->as.call.type_args[ti],
                                                         node->line, node->column);
                            if (ti > 0)
                                tp += snprintf(taj + tp, sizeof(taj) - (size_t)tp, ",");
                            tp += snprintf(taj + tp, sizeof(taj) - (size_t)tp, "%s",
                                           rt ? type_name(rt) : "?");
                        }
                        node->as.call.resolved_type_args = chk_strdup(taj);
                    }
                    /* Body already checked+queued by try_instantiate; skip lazy path */
                    goto after_method_check;
                }
            } else {
                /* No explicit type args: try to infer a single method-level
                   type param from a closure arg's return type, so
                   `v.map(|x| x+1)` works like `v.map(int)(|x| x+1)`. */
                Type *concrete = try_infer_method_generic_from_closure(
                    c, method_struct, method_name, node,
                    node->line, node->column);
                if (concrete) {
                    callee_type = concrete;
                    node->as.call.callee->resolved_type = callee_type;
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

            /* A-2 (docs/bugs_deferred_p5_4.md §2): explicit `.__drop()` calls in
               source are rejected. The compiler manages destruction automatically
               (RAII at scope exit); an explicit call is always a double-free
               footgun, and for a compiler-generated member __drop the symbol may
               not even be emitted (JIT "Symbols not found"). Block it cleanly at
               the checker rather than crashing/double-freeing at runtime. */
            if (strcmp(method_name, "__drop") == 0 && is_method_call)
            {
                checker_error(c, node->line, node->column,
                              "cannot call __drop() explicitly; the compiler "
                              "destroys values automatically at scope exit "
                              "(an explicit call would double-free)");
                result = NULL;
                break;
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
        if (callee_type->as.function.is_vararg && user_expected == 0 && actual == 0 && node->as.call.callee->kind == AST_IDENT && strcmp(node->as.call.callee->as.ident.name, "@print") == 0)
        {
            checker_error(c, node->line, node->column,
                          "@print() requires at least 1 argument");
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
            /* §13: explicit `&x` / `&obj.field` argument to a read-only `&T`
               parameter — strip the address-of shell so the call takes the
               proven auto-borrow path (identical to passing the lvalue bare).
               Without this, `&x` types as a raw `*T` and mismatches the `&T`
               formal. A field operand (`&self.value`) is the read-only twin of
               the `&!self.value` field borrow (AST_MUT_BORROW) — it lends a
               read-only `&T` of the field, zero-copy. Writable borrows stay
               explicit `&!x` (AST_MUT_BORROW — untouched here). */
            {
                AstNode *argn = node->as.call.args[i];
                if (param_type && param_type->kind == TYPE_REFERENCE &&
                    !param_type->is_mut &&
                    argn->kind == AST_UNARY && argn->as.unary.op == TOKEN_AMP &&
                    (argn->as.unary.operand->kind == AST_IDENT ||
                     argn->as.unary.operand->kind == AST_FIELD ||
                     argn->as.unary.operand->kind == AST_INDEX))
                {
                    /* shell intentionally leaked, same as the index-protocol rewrite */
                    node->as.call.args[i] = argn->as.unary.operand;
                }
            }
            /* Array-literal argument to a user-container param (Vec etc. with
               __from_list): tag it so codegen emits the from_list value, just
               like the var-decl / struct-field positions. Lets `f(["a","b"])`
               work where f takes Vec(Str), not only `Vec(Str) v=[..]; f(v)`.
               Self-guarded: no-op unless param is a from_list struct and the
               arg is an array literal. */
            checker_tag_user_from_list_literal(c, param_type,
                node->as.call.args[i], "argument list-literal");
            Type *saved_exp = c->expected_type;
            c->expected_type = param_type;
            Type *arg_type = check_expr(c, node->as.call.args[i]);
            c->expected_type = saved_exp;
            if (arg_type == NULL)
            {
                args_ok = false;
                continue;
            }
            if (!type_assignable(param_type, arg_type))
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
        bool is_print_call = node->as.call.callee->kind == AST_IDENT &&
                             strcmp(node->as.call.callee->as.ident.name, "@print") == 0;
        for (int i = user_expected; i < actual; i++)
        {
            Type *at = check_expr(c, node->as.call.args[i]);
            /* C-2: print(x) for a Show struct/enum renders via Show — rewrite the
               arg to to_str(x) (Str), which print prints as raw text. */
            if (is_print_call && type_is_show_aggregate(c, at))
            {
                wrap_arg_in_to_str(&node->as.call.args[i]);
                check_expr(c, node->as.call.args[i]);
            }
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
        /* Multi-subscript t[i, j, ...] -> the arity-specific reserved protocol
           method __index{N} (a generalization of v[i] -> __index). Resolved by
           subscript count, known at parse time; each __index{N} is a fixed-arity
           method (scalar args, no container) so the offset arithmetic inlines —
           the low-overhead, container-free path (cf. Julia's per-arity getindex).
           Single-subscript (count<=1) falls through to the legacy logic below,
           byte-unchanged. */
        if (node->as.index_expr.index_count >= 2)
        {
            AstNode *objn = node->as.index_expr.object;
            Type *obj = check_expr(c, objn);
            if (obj == NULL) { result = NULL; break; }
            int nidx = node->as.index_expr.index_count;
            char mname[24];
            snprintf(mname, sizeof(mname), "__index%d", nidx);
            if (obj->kind == TYPE_STRUCT && find_method_ensured(c, obj, mname) != NULL)
            {
                AstNode *call = make_multi_index_call(node->line, node->column,
                    objn, node->as.index_expr.indices, nidx, NULL, mname);
                node->kind = AST_CALL;
                node->as.call = call->as.call;
                free(call);
                result = check_expr(c, node);
            }
            else
            {
                checker_error(c, node->line, node->column,
                    "type '%s' does not support %d-D indexing (no method '%s')",
                    type_name(obj), nidx, mname);
                result = NULL;
            }
            break;
        }

        /* Slice creation `v[a..b]` — a borrowed &[T] view over a Vec(T) range.
           Intercept the AST_RANGE index before check_expr (ranges are not
           stand-alone expressions). Spike scope: source is a Vec(T). */
        if (node->as.index_expr.index &&
            node->as.index_expr.index->kind == AST_RANGE)
        {
            AstNode *objn = node->as.index_expr.object;
            Type *obj = check_expr(c, objn);
            if (obj == NULL) { result = NULL; break; }
            Type *elem = NULL;
            if (obj->kind == TYPE_STRUCT && obj->as.strukt.generic_base &&
                strcmp(obj->as.strukt.generic_base, "Vec") == 0 &&
                obj->as.strukt.generic_arg_count >= 1)
                elem = obj->as.strukt.generic_args[0];
            else if (obj->kind == TYPE_SLICE)
                elem = obj->as.array.elem;  /* sub-slice of a slice */
            else if (obj->kind == TYPE_STRUCT && obj->as.strukt.name &&
                     strcmp(obj->as.strukt.name, "Str") == 0 &&
                     obj->as.strukt.field_count >= 1 &&
                     obj->as.strukt.fields[0].type &&
                     obj->as.strukt.fields[0].type->kind == TYPE_POINTER)
                /* Str `{*u8 data; int len; int cap}` → a byte view `&array(u8)`
                   (same SoA layout as Vec: field0=data ptr, field1=len). */
                elem = obj->as.strukt.fields[0].type->as.pointer_to;
            if (elem == NULL)
            {
                checker_error(c, node->line, node->column,
                    "slice `v[a..b]` requires a Vec(T), Str, or &array(T) source "
                    "(got '%s')", type_name(obj));
                result = NULL;
                break;
            }
            AstNode *rng = node->as.index_expr.index;
            Type *lo = rng->as.range.start ? check_expr(c, rng->as.range.start) : NULL;
            Type *hi = rng->as.range.end   ? check_expr(c, rng->as.range.end)   : NULL;
            if ((lo && !type_is_integer(lo)) || (hi && !type_is_integer(hi)))
                checker_error(c, node->line, node->column,
                              "slice bounds must be integers");
            /* Mutability is driven by the expected type (a `&!array(T)` target /
               parameter), so a fresh `v[a..b]` can be passed directly to a
               writable-slice parameter. A writable view needs a writable source. */
            bool want_mut = (c->expected_type &&
                             c->expected_type->kind == TYPE_SLICE &&
                             c->expected_type->is_mut &&
                             type_equals(c->expected_type->as.array.elem, elem));
            if (want_mut)
            {
                Symbol *root = checker_place_root_symbol(c, objn);
                if (root != NULL && root->is_borrow)
                {
                    checker_error(c, node->line, node->column,
                        "cannot take a writable slice of read-only borrow '%s'",
                        root->name);
                    want_mut = false;
                }
            }
            result = type_slice(elem, want_mut);
            break;
        }

        Type *obj = check_expr(c, node->as.index_expr.object);
        Type *idx = check_expr(c, node->as.index_expr.index);
        if (obj == NULL || idx == NULL)
        {
            result = NULL;
            break;
        }

        /* `slice[i]` — element read of a borrowed slice (bounds-checked in codegen). */
        if (obj->kind == TYPE_SLICE)
        {
            if (!type_is_integer(idx))
            {
                checker_error(c, node->line, node->column,
                              "slice index must be integer, got '%s'", type_name(idx));
                result = NULL;
            }
            else
            {
                result = obj->as.array.elem;
            }
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
                 find_method_ensured(c, obj, "__index") != NULL)
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
        /* Phase 2 (borrow extension): field access auto-dereferences a borrow
           result (&T → T), e.g. `obj.get_ref().field` where get_ref returns &T.
           (Borrow *parameters* already register their symbol with the pointee
           type, so `self.field` needs no unwrap; this covers reference-typed
           sub-expressions like a borrow-returning call result.) */
        if (obj->kind == TYPE_REFERENCE && obj->as.pointer_to)
            obj = obj->as.pointer_to;

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

        /* Auto-dereference: *Struct → Struct or *Enum → Enum for field/method access */
        if (obj->kind == TYPE_POINTER && obj->as.pointer_to &&
            (obj->as.pointer_to->kind == TYPE_STRUCT ||
             obj->as.pointer_to->kind == TYPE_ENUM))
        {
            obj = obj->as.pointer_to;
        }

        if (obj->kind == TYPE_STRUCT)
        {
            bool priv_rejected = false;
            /* Search struct fields */
            for (int i = 0; i < obj->as.strukt.field_count; i++)
            {
                if (strcmp(obj->as.strukt.fields[i].name, field_name) == 0)
                {
                    if (obj->as.strukt.fields[i].is_private)
                    {
                        /* priv field: accessible only inside the owning struct's
                           own impl methods. Identity by generic_base else name
                           (so Mutex(Vec) inside impl(T) Mutex(T) matches). */
                        Type *cur = c->current_impl_struct_type;
                        const char *want = obj->as.strukt.generic_base
                            ? obj->as.strukt.generic_base : obj->as.strukt.name;
                        const char *have = (cur && cur->kind == TYPE_STRUCT)
                            ? (cur->as.strukt.generic_base
                               ? cur->as.strukt.generic_base : cur->as.strukt.name)
                            : NULL;
                        if (have == NULL || want == NULL ||
                            strcmp(have, want) != 0)
                        {
                            checker_error(c, node->line, node->column,
                                "field '%s' of struct '%s' is private "
                                "(accessible only inside its own methods methods)",
                                field_name, want ? want : "<anon>");
                            result = NULL;
                            priv_rejected = true;
                            break;
                        }
                    }
                    result = obj->as.strukt.fields[i].type;
                    break;
                }
            }
            /* Search methods if not found as field (skip if priv-rejected — the
               error is already reported; don't cascade "no field or method"). */
            if (result == NULL && !priv_rejected && obj->as.strukt.name)
            {
                result = find_method(c, impl_key_of_type(obj), field_name);  /* B-4.1 */
            }
            if (result == NULL && !priv_rejected)
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
            checker_reject_borrow_return(c, ret, NULL, node->line, node->column);  /* Phase 0/2: closure, defer */
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
        chk_push_scope(c);
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
        chk_pop_scope(c);

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
        /* The subject's type is intrinsic to the subject expression and must not
           be coerced by the match's own expected result type — otherwise a bare
           ctor subject (e.g. lowered `Some(3).map(Str)(...)`) would be checked
           against the wrong instantiation. Clear expected_type for the subject. */
        Type *saved_subj_exp = c->expected_type;
        c->expected_type = NULL;
        Type *subject = check_expr(c, node->as.match.subject);
        c->expected_type = saved_subj_exp;
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
                chk_push_scope(c);
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
                        ((bt->kind == TYPE_STRUCT && bt->as.strukt.has_drop) ||
                         (bt->kind == TYPE_ENUM   && bt->as.enom.has_drop)))
                    {
                        bsym->is_borrow = true;
                    }
                }

                Type *body_type = check_expr(c, arm->body);
                chk_pop_scope(c);

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

        /* V1 bit-pattern arms: `bits[w:name][w:0xVAL][w:_]...` Detect whether any
           arm uses bit patterns; if so, take a dedicated path (integer subject +
           strict total-width check + per-arm binder scope). */
        bool match_has_bit = false;
        for (int i = 0; i < node->as.match.arm_count; i++)
            if (pattern_has_bit_seq(node->as.match.arms[i].pattern)) { match_has_bit = true; break; }

        if (match_has_bit)
        {
            int subj_bits = bit_pattern_type_bits(subject);
            if (subj_bits == 0)
            {
                checker_error(c, node->as.match.subject->line,
                              node->as.match.subject->column,
                              "bit-pattern match subject must be an integer type "
                              "(int / i8-i64 / u8-u64), got '%s'", type_name(subject));
                result = NULL;
                break;
            }
            Type *bit_arm_type = NULL;
            for (int i = 0; i < node->as.match.arm_count; i++)
            {
                MatchArm *arm = &node->as.match.arms[i];
                AstNode *pat = arm->pattern;
                bool is_wild = pat->kind == AST_IDENT &&
                               strcmp(pat->as.ident.name, "_") == 0;

                chk_push_scope(c);
                if (!is_wild)
                {
                    /* Collect OR-tree leaves left-to-right; binders come from the
                       first leaf only (V1: OR branches should bind the same names). */
                    AstNode *leaves[64]; int nleaves = 0;
                    AstNode *stk[64]; int sp = 0;
                    stk[sp++] = pat;
                    while (sp > 0 && nleaves < 64)
                    {
                        AstNode *cur = stk[--sp];
                        if (cur->kind == AST_MATCH_OR_PATTERN)
                        {
                            if (sp + 2 <= 64) {
                                stk[sp++] = cur->as.or_pattern.right;
                                stk[sp++] = cur->as.or_pattern.left;
                            }
                        }
                        else if (cur->kind == AST_MATCH_BIT_PATTERN_SEQ)
                            leaves[nleaves++] = cur;
                        else
                            checker_error(c, cur->line, cur->column,
                                          "cannot mix bit-pattern with non-bit patterns "
                                          "in the same match arm");
                    }
                    for (int L = 0; L < nleaves; L++)
                        check_bit_pattern_seq(c, leaves[L], subj_bits,
                                              /*define_binders=*/L == 0);
                }

                Type *body_type = check_expr(c, arm->body);
                chk_pop_scope(c);
                if (body_type == NULL) continue;
                if (bit_arm_type == NULL) bit_arm_type = body_type;
                else if (!type_equals(bit_arm_type, body_type))
                    checker_error(c, arm->body->line, arm->body->column,
                                  "match arm type mismatch: expected '%s', got '%s'",
                                  type_name(bit_arm_type), type_name(body_type));
            }
            result = bit_arm_type;
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

    case AST_TYPENAME:
    {
        /* __type_name(Type) -> Str, a compile-time type name. Resolve the operand
           type (type-param `T` is substituted via the active type-alias table
           during generic instantiation, exactly like sizeof / cast). The result is
           a static Str built in codegen from the resolved type's name. */
        Type *nt = resolve_type_node(c, node->as.typename_expr.type_node,
                                     node->line, node->column);
        if (nt == NULL)
        {
            result = NULL;
            break;
        }
        node->as.typename_expr.named_type = nt;
        result = checker_str_type(c);
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

        /* C1 `.expect(msg)`: type the message expr (P5-4 S-3: codegen no longer
           has an untyped-literal fallback — the literal must resolve to Str). */
        if (node->as.force_unwrap.message != NULL)
            check_expr(c, node->as.force_unwrap.message);

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
            /* comptime v2 — generic construction: `T{}` / `T{x:..}` where the
               "struct name" is a type parameter aliased to a concrete struct
               during instantiation (e.g. `def mk(T)() -> T { return T{} }`).
               Resolve it through the type-alias table, same as resolve_type_node
               does for a bare named type. Unlocks write-once construction
               (generic from_value / builder / transform / zero-init). */
            if (!st && node->as.new_expr.type_arg_count == 0)
            {
                Type *alias = find_type_alias(c, node->as.new_expr.struct_name);
                if (alias && alias->kind == TYPE_STRUCT)
                    st = alias;
            }
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
            /* priv field: a struct literal may set it only inside the owning
               struct's own impl (so external `Guard{value: aliased}` cannot
               bypass the guard). `{}` zero-init has no field inits → unaffected. */
            if (st->as.strukt.fields[field_idx].is_private)
            {
                Type *cur = c->current_impl_struct_type;
                const char *want = st->as.strukt.generic_base
                    ? st->as.strukt.generic_base : st->as.strukt.name;
                const char *have = (cur && cur->kind == TYPE_STRUCT)
                    ? (cur->as.strukt.generic_base
                       ? cur->as.strukt.generic_base : cur->as.strukt.name)
                    : NULL;
                if (have == NULL || want == NULL || strcmp(have, want) != 0)
                {
                    checker_error(c, node->line, node->column,
                        "field '%s' of struct '%s' is private "
                        "(cannot be set in a struct literal outside its methods)",
                        fname, want ? want : "<anon>");
                    goto new_expr_done;
                }
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
        chk_push_scope(c);
        comptime_expand_block(c, node); /* expand comptime for/if/match in arm/expr blocks too */
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
        chk_pop_scope(c);
        result = last;
        break;
    }

    case AST_COMPTIME_FIELD:
        /* Step 1: `v.(f)` is lowered to a concrete field access during the comptime
           unroll (step 2). Reject cleanly here (and break the default→check_stmt
           recursion) until then; outside a comptime for it is always an error. */
        checker_error(c, node->line, node->column,
            "comptime field access 'v.(f)' is only valid inside a 'comptime for'");
        result = type_void();
        break;

    case AST_COMPTIME_BLOCK:
        /* Step 1 (docs/plan_comptime_consteval.md): a `comptime { ... }` block is
           evaluated by the compile-time evaluator (Step 3). Reject cleanly here
           (and break the check_expr→check_stmt recursion) until then. */
        checker_error(c, node->line, node->column,
            "comptime block evaluation is not yet implemented");
        result = type_void();
        break;

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






/* Build the iterator-protocol equivalent of `for var in <iter>` and store it on
   node->as.for_stmt.desugared.  has_iter: the iter type exposes iter()->I (call
   it); otherwise the value is itself an iterator (drive next() directly).
   src_is_ident: the source is a bare variable (borrow in place) — otherwise the
   source is materialized into an owned __src local that outlives the loop. */
/* g_foreach_uid + for-in desugaring moved to checker_lower.c. */


/* ---- Stage 3b: comptime field-iteration unroll ----
   `comptime for f in fields(T) { BODY }` is expanded here, at the point a block
   is checked (so the type alias for a generic T is already active and resolves to
   the concrete struct). For each field of T, BODY is deep-cloned and the comptime
   handles are rewritten to concrete leaves:
       f.name       -> "<field name>"   (string literal)
       f.index      -> <i>              (int literal)
       f.type_name  -> "<type name>"   (string literal)
       v.(f)        -> v.<field name>   (ordinary field access)
   Each field's clone is kept as its own nested block (a fresh scope, so any
   body-local declarations do not collide across iterations). The expanded
   statements replace the comptime-for in the parent block in place, so the
   checker and codegen only ever see ordinary AST (zero new codegen — mirrors the
   @derive / for-in / combinator desugaring打法). v1: read-only iteration; the
   comptime-if predicate evaluation lands in step 3. */

static AstNode *ct_str_lit(const char *s, int line, int col) {
    AstNode *n = ast_new(AST_STRING_LIT, line, col);
    n->as.string_lit.value  = chk_strdup(s);
    n->as.string_lit.length = (int)strlen(s);
    return n;
}

static AstNode *ct_int_lit(long long v, int line, int col) {
    AstNode *n = ast_new(AST_INT_LIT, line, col);
    n->as.int_lit.value   = v;
    n->as.int_lit.is_char = false;
    return n;
}

static AstNode *ct_bool_lit(bool v, int line, int col) {
    AstNode *n = ast_new(AST_BOOL_LIT, line, col);
    n->as.bool_lit.value = v;
    return n;
}

/* comptime v2 — `f.type` as a type value: lower the handle to a bare identifier
   naming the field's concrete type, so it works in type position — chiefly as a
   static-call receiver `f.type.from_value(x)` → `Int.from_value(x)` (same as the
   `T.from_value()` type-param dispatch). Using `f.type` as a runtime value is a
   clean "undefined" error from the resulting type-name identifier. */
static AstNode *ct_ident(const char *s, int line, int col) {
    AstNode *n = ast_new(AST_IDENT, line, col);
    n->as.ident.name = chk_strdup(s);
    return n;
}

/* Per-iteration rewrite context: the comptime handle (`f` / `vr`) bound to one
   concrete field or enum variant. `payload_count` is -1 in a field (fields(T))
   context and >= 0 in a variant (variants(T) / comptime match) context, where it
   also enables `<handle>.has_payload` / `<handle>.payload_count`. */
typedef struct {
    const char *handle;     /* loop var: "f" (field) / "vr" (variant) */
    const char *name;       /* field/variant name */
    long long   index;      /* field/variant index */
    const char *type_name;  /* field type / variant first-payload type ("" if none) */
    int         payload_count; /* -1 = field; >= 0 = variant payload arity */
} CtRw;

/* Recursively rewrite *slot for one bound field/variant (cx->handle). */
static void ct_rewrite(AstNode **slot, const CtRw *cx)
{
    AstNode *n = *slot;
    if (n == NULL) return;

    /* f.name / f.index / f.type_name / f.type (and, for variants, .has_payload /
       .payload_count) → literal / type-name identifier */
    if (n->kind == AST_FIELD && n->as.field_access.object &&
        n->as.field_access.object->kind == AST_IDENT &&
        strcmp(n->as.field_access.object->as.ident.name, cx->handle) == 0)
    {
        const char *m = n->as.field_access.field;
        AstNode *repl = NULL;
        if      (strcmp(m, "name") == 0)      repl = ct_str_lit(cx->name, n->line, n->column);
        else if (strcmp(m, "index") == 0)     repl = ct_int_lit(cx->index, n->line, n->column);
        else if (strcmp(m, "type_name") == 0) repl = ct_str_lit(cx->type_name, n->line, n->column);
        else if (strcmp(m, "type") == 0)      repl = ct_ident(cx->type_name, n->line, n->column);
        else if (cx->payload_count >= 0 && strcmp(m, "has_payload") == 0)
            repl = ct_bool_lit(cx->payload_count > 0, n->line, n->column);
        else if (cx->payload_count >= 0 && strcmp(m, "payload_count") == 0)
            repl = ct_int_lit(cx->payload_count, n->line, n->column);
        if (repl) { ast_free(n); *slot = repl; return; }
        /* unknown member on the handle — leave; the checker reports it as undefined */
    }

    /* v.(f) → v.<name> (ordinary field access). Rewrite inside the object first. */
    if (n->kind == AST_COMPTIME_FIELD &&
        strcmp(n->as.comptime_field.handle, cx->handle) == 0)
    {
        ct_rewrite(&n->as.comptime_field.object, cx);
        AstNode *fld = ast_new(AST_FIELD, n->line, n->column);
        fld->as.field_access.object = n->as.comptime_field.object;
        fld->as.field_access.field  = chk_strdup(cx->name);
        n->as.comptime_field.object = NULL; /* ownership transferred */
        ast_free(n);
        *slot = fld;
        return;
    }

    switch (n->kind) {
    case AST_UNARY:      ct_rewrite(&n->as.unary.operand, cx); break;
    case AST_MUT_BORROW: ct_rewrite(&n->as.mut_borrow.operand, cx); break;
    case AST_BINARY:
        ct_rewrite(&n->as.binary.left, cx);
        ct_rewrite(&n->as.binary.right, cx);
        ct_rewrite(&n->as.binary.lowered, cx);
        break;
    case AST_CALL:
        ct_rewrite(&n->as.call.callee, cx);
        for (int i = 0; i < n->as.call.arg_count; i++)
            ct_rewrite(&n->as.call.args[i], cx);
        break;
    case AST_INDEX:
        ct_rewrite(&n->as.index_expr.object, cx);
        if (n->as.index_expr.indices) {
            for (int i = 0; i < n->as.index_expr.index_count; i++)
                ct_rewrite(&n->as.index_expr.indices[i], cx);
        } else {
            ct_rewrite(&n->as.index_expr.index, cx);
        }
        break;
    case AST_FIELD:
        ct_rewrite(&n->as.field_access.object, cx);
        break;
    case AST_COMPTIME_FIELD: /* a different handle — recurse into the object */
        ct_rewrite(&n->as.comptime_field.object, cx);
        break;
    case AST_CAST:          ct_rewrite(&n->as.cast.expr, cx); break;
    case AST_TRY:           ct_rewrite(&n->as.try_expr.expr, cx); break;
    case AST_FORCE_UNWRAP:
        ct_rewrite(&n->as.force_unwrap.expr, cx);
        ct_rewrite(&n->as.force_unwrap.message, cx);
        break;
    case AST_RANGE:
        ct_rewrite(&n->as.range.start, cx);
        ct_rewrite(&n->as.range.end, cx);
        break;
    case AST_FORMAT_STRING:
        for (int i = 0; i < n->as.format_string.expr_count; i++)
            ct_rewrite(&n->as.format_string.exprs[i], cx);
        break;
    case AST_ARRAY_LIT:
        for (int i = 0; i < n->as.array_lit.count; i++)
            ct_rewrite(&n->as.array_lit.elements[i], cx);
        break;
    case AST_MAP_LIT:
        for (int i = 0; i < n->as.map_lit.pair_count; i++) {
            ct_rewrite(&n->as.map_lit.keys[i], cx);
            ct_rewrite(&n->as.map_lit.vals[i], cx);
        }
        break;
    case AST_CLOSURE:    ct_rewrite(&n->as.closure.body, cx); break;
    case AST_MATCH:
        ct_rewrite(&n->as.match.subject, cx);
        for (int i = 0; i < n->as.match.arm_count; i++) {
            ct_rewrite(&n->as.match.arms[i].pattern, cx);
            ct_rewrite(&n->as.match.arms[i].body, cx);
        }
        break;
    case AST_MATCH_OR_PATTERN:
        ct_rewrite(&n->as.or_pattern.left, cx);
        ct_rewrite(&n->as.or_pattern.right, cx);
        break;
    case AST_NEW_EXPR:
        for (int i = 0; i < n->as.new_expr.field_init_count; i++)
            ct_rewrite(&n->as.new_expr.field_inits[i].value, cx);
        break;
    case AST_VAR_DECL:   ct_rewrite(&n->as.var_decl.init, cx); break;
    case AST_ASSIGN:
        ct_rewrite(&n->as.assign.target, cx);
        ct_rewrite(&n->as.assign.value, cx);
        break;
    case AST_RETURN:     ct_rewrite(&n->as.return_stmt.value, cx); break;
    case AST_IF:
        ct_rewrite(&n->as.if_stmt.cond, cx);
        ct_rewrite(&n->as.if_stmt.then_block, cx);
        ct_rewrite(&n->as.if_stmt.else_block, cx);
        break;
    case AST_WHILE:
        ct_rewrite(&n->as.while_stmt.cond, cx);
        ct_rewrite(&n->as.while_stmt.body, cx);
        break;
    case AST_FOR:
        ct_rewrite(&n->as.for_stmt.iter, cx);
        ct_rewrite(&n->as.for_stmt.body, cx);
        break;
    case AST_FOR_C:
        ct_rewrite(&n->as.for_c_stmt.init, cx);
        ct_rewrite(&n->as.for_c_stmt.cond, cx);
        ct_rewrite(&n->as.for_c_stmt.update, cx);
        ct_rewrite(&n->as.for_c_stmt.body, cx);
        break;
    case AST_BLOCK:
        for (int i = 0; i < n->as.block.stmt_count; i++)
            ct_rewrite(&n->as.block.stmts[i], cx);
        break;
    case AST_EXPR_STMT:  ct_rewrite(&n->as.expr_stmt.expr, cx); break;
    case AST_COMPTIME_FOR:
        /* nested comptime for: rewrite its body unless its var shadows the handle */
        if (n->as.comptime_for.var == NULL ||
            strcmp(n->as.comptime_for.var, cx->handle) != 0)
            ct_rewrite(&n->as.comptime_for.body, cx);
        break;
    case AST_COMPTIME_IF:
        ct_rewrite(&n->as.comptime_if.cond, cx);
        ct_rewrite(&n->as.comptime_if.then_block, cx);
        ct_rewrite(&n->as.comptime_if.else_block, cx);
        break;
    case AST_COMPTIME_MATCH:
        /* nested comptime match: rewrite subject; rewrite body unless its handle
           or payload binder shadows the outer handle. */
        ct_rewrite(&n->as.comptime_match.subject, cx);
        if ((n->as.comptime_match.handle == NULL ||
             strcmp(n->as.comptime_match.handle, cx->handle) != 0) &&
            (n->as.comptime_match.binder == NULL ||
             strcmp(n->as.comptime_match.binder, cx->handle) != 0))
            ct_rewrite(&n->as.comptime_match.body, cx);
        break;
    default: break; /* literals, ident, sizeof, typename, break/continue: no f.* children */
    }
}

/* ---- Compile-time constant value evaluation (docs/plan_comptime_consteval.md) ----
   A small AST interpreter over the const subset: int/f64/bool/char literals,
   arithmetic / comparison / logical / bitwise / shift ops, `as` casts, references
   to other comptime constants, and math.* (constants + functions via host libm).
   Returns false (caller reports "not a compile-time constant") on anything outside
   the subset — a runtime variable, an unsupported op, a heap type, div-by-zero. */
typedef struct { bool is_float; long long i; double f; } CtScalar;
static double cts_to_f(const CtScalar *v) { return v->is_float ? v->f : (double)v->i; }

/* Comptime evaluation context: block-local variable bindings (Step 3) plus a step
   budget that bounds total work (guards against runaway compile-time loops). Names
   point into the AST (not owned). NULL `ev` is allowed for a pure scalar expression
   (Step 2) — then identifiers resolve only to outer comptime constants. */
#define CT_BUDGET_DEFAULT 10000000L
typedef struct {
    const char **names; CtScalar *vals; int count, cap;            /* scalar locals */
    const char **anames; CtScalar **arrs; int *alens; bool *afloat; /* array locals (Step 4) */
    int acount, acap;
    const char *ret_array;  /* set by `return <arrayvar>` to signal an array result */
    long budget;
} CtEval;

static bool ct_env_get(const CtEval *ev, const char *name, CtScalar *out)
{
    if (ev == NULL) return false;
    for (int i = ev->count - 1; i >= 0; i--)  /* last binding wins (shadowing) */
        if (strcmp(ev->names[i], name) == 0) { *out = ev->vals[i]; return true; }
    return false;
}
static void ct_env_set(CtEval *ev, const char *name, const CtScalar *v)
{
    for (int i = ev->count - 1; i >= 0; i--)  /* update in place if already bound */
        if (strcmp(ev->names[i], name) == 0) { ev->vals[i] = *v; return; }
    if (ev->count >= ev->cap) {
        ev->cap = ev->cap < 8 ? 8 : ev->cap * 2;
        ev->names = realloc_safe((void *)ev->names, (size_t)ev->cap * sizeof(char *));
        ev->vals  = realloc_safe(ev->vals,          (size_t)ev->cap * sizeof(CtScalar));
    }
    ev->names[ev->count] = name;
    ev->vals[ev->count]  = *v;
    ev->count++;
}

/* Array-local bindings (Step 4): a zero-initialized scalar array bound by name. */
static int ct_aenv_find(const CtEval *ev, const char *name)
{
    if (ev == NULL) return -1;
    for (int i = ev->acount - 1; i >= 0; i--)
        if (strcmp(ev->anames[i], name) == 0) return i;
    return -1;
}
static void ct_aenv_decl(CtEval *ev, const char *name, int len, bool is_float)
{
    if (ev->acount >= ev->acap) {
        ev->acap = ev->acap < 4 ? 4 : ev->acap * 2;
        ev->anames = realloc_safe((void *)ev->anames, (size_t)ev->acap * sizeof(char *));
        ev->arrs   = realloc_safe(ev->arrs,           (size_t)ev->acap * sizeof(CtScalar *));
        ev->alens  = realloc_safe(ev->alens,          (size_t)ev->acap * sizeof(int));
        ev->afloat = realloc_safe(ev->afloat,         (size_t)ev->acap * sizeof(bool));
    }
    CtScalar *a = malloc_safe((size_t)len * sizeof(CtScalar));
    for (int k = 0; k < len; k++) { a[k].is_float = is_float; a[k].i = 0; a[k].f = 0.0; }
    ev->anames[ev->acount] = name;
    ev->arrs[ev->acount]   = a;
    ev->alens[ev->acount]  = len;
    ev->afloat[ev->acount] = is_float;
    ev->acount++;
}
static void ct_env_free(CtEval *ev)
{
    free((void *)ev->names); free(ev->vals);
    for (int i = 0; i < ev->acount; i++) free(ev->arrs[i]);
    free((void *)ev->anames); free(ev->arrs); free(ev->alens); free(ev->afloat);
}

/* `math.<name>` recognition: object must be the literal identifier `math`. */
static bool ct_field_is_math(const AstNode *e, const char **out_name)
{
    if (e == NULL || e->kind != AST_FIELD) return false;
    AstNode *obj = e->as.field_access.object;
    if (obj == NULL || obj->kind != AST_IDENT) return false;
    if (strcmp(obj->as.ident.name, "math") != 0) return false;
    *out_name = e->as.field_access.field;
    return true;
}

static bool ct_eval_scalar(Checker *c, AstNode *e, CtEval *ev, CtScalar *out);

/* Evaluate `math.fn(args...)` at compile time using host libm. Returns false if the
   callee is not a recognized math function or an argument is not const. */
static bool ct_eval_math_call(Checker *c, AstNode *call, CtEval *ev, CtScalar *out)
{
    const char *fn = NULL;
    if (!ct_field_is_math(call->as.call.callee, &fn)) return false;
    int arity = 0; MathEmitKind mk; const char *en; MathPolyKind mp; const char *ip;
    if (!builtin_math_lookup_fn(fn, &arity, &mk, &en, &mp, &ip)) return false;
    if (call->as.call.arg_count != arity || arity < 1 || arity > 2) return false;

    CtScalar a, b = {0};  /* b is only assigned when arity==2; zero-init keeps the
                             min/max path (which is always arity 2) defined even if
                             the arity table were ever wrong. Silences C4701. */
    if (!ct_eval_scalar(c, call->as.call.args[0], ev, &a)) return false;
    if (arity == 2 && !ct_eval_scalar(c, call->as.call.args[1], ev, &b)) return false;

    /* abs/min/max are int-or-float polymorphic: keep integer-ness when all-integer. */
    if (strcmp(fn, "abs") == 0) {
        if (!a.is_float) { out->is_float = false; out->i = llabs(a.i); }
        else             { out->is_float = true;  out->f = fabs(a.f); }
        return true;
    }
    if (strcmp(fn, "min") == 0 || strcmp(fn, "max") == 0) {
        bool is_min = (fn[1] == 'i');
        if (!a.is_float && !b.is_float) {
            out->is_float = false;
            out->i = is_min ? (a.i < b.i ? a.i : b.i) : (a.i > b.i ? a.i : b.i);
        } else {
            double x = cts_to_f(&a), y = cts_to_f(&b);
            out->is_float = true;
            out->f = is_min ? (x < y ? x : y) : (x > y ? x : y);
        }
        return true;
    }

    double x = cts_to_f(&a), y = (arity == 2) ? cts_to_f(&b) : 0.0, r;
    if      (strcmp(fn, "sqrt")  == 0) r = sqrt(x);
    else if (strcmp(fn, "sin")   == 0) r = sin(x);
    else if (strcmp(fn, "cos")   == 0) r = cos(x);
    else if (strcmp(fn, "tan")   == 0) r = tan(x);
    else if (strcmp(fn, "asin")  == 0) r = asin(x);
    else if (strcmp(fn, "acos")  == 0) r = acos(x);
    else if (strcmp(fn, "atan")  == 0) r = atan(x);
    else if (strcmp(fn, "atan2") == 0) r = atan2(x, y);
    else if (strcmp(fn, "exp")   == 0) r = exp(x);
    else if (strcmp(fn, "log")   == 0) r = log(x);
    else if (strcmp(fn, "log2")  == 0) r = log2(x);
    else if (strcmp(fn, "log10") == 0) r = log10(x);
    else if (strcmp(fn, "pow")   == 0) r = pow(x, y);
    else if (strcmp(fn, "floor") == 0) r = floor(x);
    else if (strcmp(fn, "ceil")  == 0) r = ceil(x);
    else return false;  /* a math fn we don't evaluate at comptime yet */
    out->is_float = true; out->f = r;
    return true;
}

static bool ct_eval_scalar(Checker *c, AstNode *e, CtEval *ev, CtScalar *out)
{
    if (e == NULL) return false;
    switch (e->kind)
    {
    case AST_INT_LIT:   out->is_float = false; out->i = e->as.int_lit.value;          return true;
    case AST_FLOAT_LIT: out->is_float = true;  out->f = e->as.float_lit.value;         return true;
    case AST_BOOL_LIT:  out->is_float = false; out->i = e->as.bool_lit.value ? 1 : 0;  return true;

    case AST_IDENT: {
        if (ct_env_get(ev, e->as.ident.name, out)) return true; /* block-local comptime var */
        Symbol *s = scope_resolve(c->current_scope, e->as.ident.name);
        if (s && s->is_comptime_const) {
            out->is_float = s->ct_is_float; out->i = s->ct_i; out->f = s->ct_f;
            return true;
        }
        return false;  /* runtime variable / undefined → not a comptime constant */
    }

    case AST_FIELD: {  /* math constant: math.PI / math.E / ... */
        const char *nm = NULL; double cv;
        if (ct_field_is_math(e, &nm) && builtin_math_lookup_const(nm, &cv)) {
            out->is_float = true; out->f = cv; return true;
        }
        return false;
    }

    case AST_INDEX: {  /* read a comptime array-local element: t[i] (Step 4) */
        if (ev == NULL || e->as.index_expr.index == NULL) return false;
        AstNode *obj = e->as.index_expr.object;
        if (obj == NULL || obj->kind != AST_IDENT) return false;
        int ai = ct_aenv_find(ev, obj->as.ident.name);
        if (ai < 0) return false;
        CtScalar idx;
        if (!ct_eval_scalar(c, e->as.index_expr.index, ev, &idx) || idx.is_float) return false;
        if (idx.i < 0 || idx.i >= ev->alens[ai]) return false;  /* out of bounds */
        *out = ev->arrs[ai][idx.i];
        return true;
    }

    case AST_CALL: return ct_eval_math_call(c, e, ev, out);

    case AST_CAST: {
        CtScalar v;
        if (!ct_eval_scalar(c, e->as.cast.expr, ev, &v)) return false;
        Type *tt = resolve_type_node(c, e->as.cast.target_type, e->line, e->column);
        if (tt == NULL) return false;
        if (type_is_float(tt)) { out->is_float = true; out->f = cts_to_f(&v); return true; }
        if (type_is_integer(tt) || tt->kind == TYPE_CHAR) {
            out->is_float = false; out->i = v.is_float ? (long long)v.f : v.i; return true;
        }
        if (tt->kind == TYPE_BOOL) {
            out->is_float = false; out->i = (cts_to_f(&v) != 0.0) ? 1 : 0; return true;
        }
        return false;
    }

    case AST_UNARY: {
        CtScalar v;
        if (!ct_eval_scalar(c, e->as.unary.operand, ev, &v)) return false;
        switch (e->as.unary.op) {
        case TOKEN_MINUS:
            if (v.is_float) { out->is_float = true; out->f = -v.f; }
            else            { out->is_float = false; out->i = -v.i; }
            return true;
        case TOKEN_BANG:
            out->is_float = false; out->i = (cts_to_f(&v) != 0.0) ? 0 : 1; return true;
        case TOKEN_TILDE:
            if (v.is_float) return false;
            out->is_float = false; out->i = ~v.i; return true;
        default: return false;
        }
    }

    case AST_BINARY: {
        CtScalar L, R;
        if (!ct_eval_scalar(c, e->as.binary.left,  ev, &L)) return false;
        if (!ct_eval_scalar(c, e->as.binary.right, ev, &R)) return false;
        TokenType op = e->as.binary.op;

        /* comparisons and logical → boolean (stored as int 0/1) */
        switch (op) {
        case TOKEN_LT:  out->is_float = false; out->i = (cts_to_f(&L) <  cts_to_f(&R)) ? 1 : 0; return true;
        case TOKEN_GT:  out->is_float = false; out->i = (cts_to_f(&L) >  cts_to_f(&R)) ? 1 : 0; return true;
        case TOKEN_LEQ: out->is_float = false; out->i = (cts_to_f(&L) <= cts_to_f(&R)) ? 1 : 0; return true;
        case TOKEN_GEQ: out->is_float = false; out->i = (cts_to_f(&L) >= cts_to_f(&R)) ? 1 : 0; return true;
        case TOKEN_EQ:  out->is_float = false; out->i = (cts_to_f(&L) == cts_to_f(&R)) ? 1 : 0; return true;
        case TOKEN_NEQ: out->is_float = false; out->i = (cts_to_f(&L) != cts_to_f(&R)) ? 1 : 0; return true;
        case TOKEN_AND: out->is_float = false; out->i = (cts_to_f(&L) != 0.0 && cts_to_f(&R) != 0.0) ? 1 : 0; return true;
        case TOKEN_OR:  out->is_float = false; out->i = (cts_to_f(&L) != 0.0 || cts_to_f(&R) != 0.0) ? 1 : 0; return true;
        default: break;
        }

        if (L.is_float || R.is_float) {
            double a = cts_to_f(&L), b = cts_to_f(&R), r;
            switch (op) {
            case TOKEN_PLUS:  r = a + b; break;
            case TOKEN_MINUS: r = a - b; break;
            case TOKEN_STAR:  r = a * b; break;
            case TOKEN_SLASH: if (b == 0.0) return false; r = a / b; break;
            default: return false;  /* %, bit/shift not valid on floats */
            }
            out->is_float = true; out->f = r; return true;
        } else {
            long long a = L.i, b = R.i, r;
            switch (op) {
            case TOKEN_PLUS:    r = a + b; break;
            case TOKEN_MINUS:   r = a - b; break;
            case TOKEN_STAR:    r = a * b; break;
            case TOKEN_SLASH:   if (b == 0) return false; r = a / b; break;
            case TOKEN_PERCENT: if (b == 0) return false; r = a % b; break;
            case TOKEN_AMP:     r = a & b; break;
            case TOKEN_PIPE:    r = a | b; break;
            case TOKEN_CARET:   r = a ^ b; break;
            case TOKEN_LSHIFT:  r = a << b; break;
            case TOKEN_RSHIFT:  r = a >> b; break;
            default: return false;
            }
            out->is_float = false; out->i = r; return true;
        }
    }

    default: return false;
    }
}

/* ---- Comptime block interpreter (Step 3) ---------------------------------------
   Executes a `comptime { ... return v }` block: scalar local decls/assignments,
   if/else, bounded `for i in 0..N`, and `return <expr>`. Mutates `ev` (block-local
   bindings) and decrements ev->budget per step. CT_RETURNED → `*ret` holds the
   value; CT_NORMAL → fell off the end (caller errors: must return); CT_FAIL → not
   compile-time-constant / unsupported stmt / div0 / budget exceeded. */
typedef enum { CT_NORMAL, CT_RETURNED, CT_FAIL } CtFlow;

static CtFlow ct_exec_block(Checker *c, AstNode *blk, CtEval *ev, CtScalar *ret);

static CtFlow ct_exec_stmt(Checker *c, AstNode *s, CtEval *ev, CtScalar *ret)
{
    if (s == NULL) return CT_NORMAL;
    if (--ev->budget <= 0) return CT_FAIL;

    switch (s->kind)
    {
    case AST_VAR_DECL: {
        Type *dt = s->as.var_decl.var_type
                       ? resolve_type_node(c, s->as.var_decl.var_type, s->line, s->column)
                       : NULL;
        if (dt && dt->kind == TYPE_ARRAY) {
            /* array local `array(T,N) t = {}` — zero-initialized; the loop fills it.
               The init expression's contents are ignored in v1 (idiom is `= {}`). */
            int n = dt->as.array.size;
            Type *el = dt->as.array.elem;
            bool es = el && (type_is_numeric(el) || el->kind == TYPE_BOOL || el->kind == TYPE_CHAR);
            if (n <= 0 || !es) return CT_FAIL;
            ct_aenv_decl(ev, s->as.var_decl.name, n, type_is_float(el));
            return CT_NORMAL;
        }
        if (s->as.var_decl.init == NULL) return CT_FAIL;
        CtScalar v;
        if (!ct_eval_scalar(c, s->as.var_decl.init, ev, &v)) return CT_FAIL;
        if (dt) {
            if (type_is_float(dt)) { v.f = cts_to_f(&v); v.is_float = true; }
            else if (v.is_float)   return CT_FAIL;  /* float init for int local */
        }
        ct_env_set(ev, s->as.var_decl.name, &v);
        return CT_NORMAL;
    }

    case AST_ASSIGN: {
        AstNode *tgt = s->as.assign.target;
        if (tgt->kind == AST_INDEX) {
            /* array element assignment: t[i] = expr (Step 4); op must be plain `=` */
            AstNode *obj = tgt->as.index_expr.object;
            if (s->as.assign.op != TOKEN_ASSIGN || tgt->as.index_expr.index == NULL ||
                obj == NULL || obj->kind != AST_IDENT)
                return CT_FAIL;
            int ai = ct_aenv_find(ev, obj->as.ident.name);
            if (ai < 0) return CT_FAIL;
            CtScalar idx, val;
            if (!ct_eval_scalar(c, tgt->as.index_expr.index, ev, &idx) || idx.is_float) return CT_FAIL;
            if (idx.i < 0 || idx.i >= ev->alens[ai]) return CT_FAIL;  /* out of bounds */
            if (!ct_eval_scalar(c, s->as.assign.value, ev, &val)) return CT_FAIL;
            if (ev->afloat[ai]) { val.f = cts_to_f(&val); val.is_float = true; }
            else if (val.is_float) return CT_FAIL;  /* float into int array */
            ev->arrs[ai][idx.i] = val;
            return CT_NORMAL;
        }
        if (tgt->kind != AST_IDENT) return CT_FAIL;
        const char *nm = tgt->as.ident.name;
        CtScalar rhs;
        if (!ct_eval_scalar(c, s->as.assign.value, ev, &rhs)) return CT_FAIL;
        TokenType op = s->as.assign.op;
        if (op != TOKEN_ASSIGN) {  /* compound: cur <op> rhs */
            CtScalar cur;
            if (!ct_env_get(ev, nm, &cur)) return CT_FAIL;
            if (cur.is_float || rhs.is_float) {
                double a = cts_to_f(&cur), b = cts_to_f(&rhs), r;
                switch (op) {
                case TOKEN_PLUS_ASSIGN:  r = a + b; break;
                case TOKEN_MINUS_ASSIGN: r = a - b; break;
                case TOKEN_STAR_ASSIGN:  r = a * b; break;
                case TOKEN_SLASH_ASSIGN: if (b == 0.0) return CT_FAIL; r = a / b; break;
                default: return CT_FAIL;
                }
                rhs.is_float = true; rhs.f = r;
            } else {
                long long a = cur.i, b = rhs.i, r;
                switch (op) {
                case TOKEN_PLUS_ASSIGN:  r = a + b; break;
                case TOKEN_MINUS_ASSIGN: r = a - b; break;
                case TOKEN_STAR_ASSIGN:  r = a * b; break;
                case TOKEN_SLASH_ASSIGN: if (b == 0) return CT_FAIL; r = a / b; break;
                default: return CT_FAIL;
                }
                rhs.is_float = false; rhs.i = r;
            }
        }
        ct_env_set(ev, nm, &rhs);
        return CT_NORMAL;
    }

    case AST_IF: {
        CtScalar cond;
        if (!ct_eval_scalar(c, s->as.if_stmt.cond, ev, &cond)) return CT_FAIL;
        if (cts_to_f(&cond) != 0.0)
            return ct_exec_block(c, s->as.if_stmt.then_block, ev, ret);
        if (s->as.if_stmt.else_block)
            return ct_exec_block(c, s->as.if_stmt.else_block, ev, ret);
        return CT_NORMAL;
    }

    case AST_FOR: {
        /* bounded `for i in lo..hi` only (range iter; no user containers) */
        AstNode *it = s->as.for_stmt.iter;
        if (it == NULL || it->kind != AST_RANGE) return CT_FAIL;
        CtScalar lo, hi;
        if (!ct_eval_scalar(c, it->as.range.start, ev, &lo)) return CT_FAIL;
        if (!ct_eval_scalar(c, it->as.range.end,   ev, &hi)) return CT_FAIL;
        if (lo.is_float || hi.is_float) return CT_FAIL;
        for (long long iv = lo.i; iv < hi.i; iv++) {
            if (--ev->budget <= 0) return CT_FAIL;
            CtScalar civ; civ.is_float = false; civ.i = iv; civ.f = 0.0;
            ct_env_set(ev, s->as.for_stmt.var, &civ);
            CtFlow f = ct_exec_block(c, s->as.for_stmt.body, ev, ret);
            if (f != CT_NORMAL) return f;  /* RETURNED / FAIL propagate out of loop */
        }
        return CT_NORMAL;
    }

    case AST_BLOCK:
        return ct_exec_block(c, s, ev, ret);

    case AST_EXPR_STMT: {
        CtScalar tmp;  /* a bare expression statement: must still be const-evaluable */
        if (!ct_eval_scalar(c, s->as.expr_stmt.expr, ev, &tmp)) return CT_FAIL;
        return CT_NORMAL;
    }

    case AST_RETURN: {
        AstNode *rv = s->as.return_stmt.value;
        if (rv == NULL) return CT_FAIL;
        if (rv->kind == AST_IDENT && ct_aenv_find(ev, rv->as.ident.name) >= 0) {
            ev->ret_array = rv->as.ident.name;  /* array result (Step 4) */
            return CT_RETURNED;
        }
        if (!ct_eval_scalar(c, rv, ev, ret)) return CT_FAIL;
        return CT_RETURNED;
    }

    default:
        return CT_FAIL;  /* while / break / continue / nested decls: unsupported in v1 */
    }
}

static CtFlow ct_exec_block(Checker *c, AstNode *blk, CtEval *ev, CtScalar *ret)
{
    if (blk == NULL) return CT_NORMAL;
    if (blk->kind != AST_BLOCK) return ct_exec_stmt(c, blk, ev, ret);  /* single stmt */
    for (int i = 0; i < blk->as.block.stmt_count; i++) {
        CtFlow f = ct_exec_stmt(c, blk->as.block.stmts[i], ev, ret);
        if (f != CT_NORMAL) return f;
    }
    return CT_NORMAL;
}

/* Evaluate a comptime-constant boolean predicate (step 3). After ct_rewrite has
   substituted f.name/f.index/f.type_name to literals, a `comptime if` condition is
   an expression over string/int/bool literals + comparisons + && || !. Sets
   *ok=false (caller reports) if it is not a compile-time constant boolean. */
static bool ct_eval_bool(AstNode *e, bool *ok)
{
    if (e == NULL) { *ok = false; return false; }
    if (e->kind == AST_BOOL_LIT) return e->as.bool_lit.value;
    if (e->kind == AST_UNARY && e->as.unary.op == TOKEN_BANG)
        return !ct_eval_bool(e->as.unary.operand, ok);
    if (e->kind == AST_BINARY) {
        TokenType op = e->as.binary.op;
        AstNode *L = e->as.binary.left, *R = e->as.binary.right;
        if (op == TOKEN_AND) { bool a = ct_eval_bool(L, ok); bool b = ct_eval_bool(R, ok); return a && b; }
        if (op == TOKEN_OR)  { bool a = ct_eval_bool(L, ok); bool b = ct_eval_bool(R, ok); return a || b; }
        if (L && R && L->kind == AST_STRING_LIT && R->kind == AST_STRING_LIT) {
            int cmp = strcmp(L->as.string_lit.value, R->as.string_lit.value);
            switch (op) {
            case TOKEN_EQ:  return cmp == 0;   case TOKEN_NEQ: return cmp != 0;
            case TOKEN_LT:  return cmp < 0;    case TOKEN_GT:  return cmp > 0;
            case TOKEN_LEQ: return cmp <= 0;   case TOKEN_GEQ: return cmp >= 0;
            default: *ok = false; return false;
            }
        }
        if (L && R && L->kind == AST_INT_LIT && R->kind == AST_INT_LIT) {
            long long a = L->as.int_lit.value, b = R->as.int_lit.value;
            switch (op) {
            case TOKEN_EQ:  return a == b;     case TOKEN_NEQ: return a != b;
            case TOKEN_LT:  return a < b;      case TOKEN_GT:  return a > b;
            case TOKEN_LEQ: return a <= b;     case TOKEN_GEQ: return a >= b;
            default: *ok = false; return false;
            }
        }
        *ok = false; return false;
    }
    *ok = false; return false;
}

/* Expand every AST_COMPTIME_FOR / AST_COMPTIME_IF statement directly in `block`,
   in place. */
static void comptime_expand_block(Checker *c, AstNode *block)
{
    if (block == NULL || block->kind != AST_BLOCK) return;
    bool any = false;
    for (int i = 0; i < block->as.block.stmt_count; i++) {
        AstNode *s = block->as.block.stmts[i];
        if (s && (s->kind == AST_COMPTIME_FOR || s->kind == AST_COMPTIME_IF ||
                  s->kind == AST_COMPTIME_MATCH)) {
            any = true; break;
        }
    }
    if (!any) return;

    AstNode **out = NULL; int nc = 0, ncap = 0;
    #define CT_PUSH(node) do { \
        if (nc >= ncap) { ncap = ncap ? ncap * 2 : 8; \
            out = realloc_safe(out, (size_t)ncap * sizeof(AstNode *)); } \
        out[nc++] = (node); } while (0)

    for (int i = 0; i < block->as.block.stmt_count; i++) {
        AstNode *s = block->as.block.stmts[i];
        if (s == NULL) continue;

        /* comptime if: evaluate the (now-constant) predicate and keep only the
           taken branch. The dropped branch is freed without ever being checked —
           so code that only type-checks for some fields does not poison others.
           Iterate through `else comptime if` chains. */
        if (s->kind == AST_COMPTIME_IF) {
            AstNode *cur = s, *result = NULL;
            while (cur && cur->kind == AST_COMPTIME_IF) {
                bool ok = true;
                bool cond = ct_eval_bool(cur->as.comptime_if.cond, &ok);
                if (!ok) {
                    checker_error(c, cur->line, cur->column,
                        "comptime if condition must be a compile-time constant "
                        "(over f.name / f.index / f.type_name)");
                    ast_free(cur);
                    cur = NULL;
                    break;
                }
                if (cond) {
                    result = cur->as.comptime_if.then_block;
                    cur->as.comptime_if.then_block = NULL;
                    ast_free(cur);
                    cur = NULL;
                } else {
                    AstNode *els = cur->as.comptime_if.else_block;
                    cur->as.comptime_if.else_block = NULL;
                    ast_free(cur);
                    cur = els;  /* block (else {}), comptime_if (chain), or NULL */
                    if (cur && cur->kind != AST_COMPTIME_IF) { result = cur; cur = NULL; }
                }
            }
            if (result) CT_PUSH(result);  /* a fresh-scope nested block */
            continue;
        }

        /* comptime match v { vr(p) => body }: expand the single generic arm into a
           real `match v { Variant(p) => body, ... }` — one arm per enum variant,
           binding the active variant's first payload to the user's binder (the rest
           to `_`), and substituting vr.name / vr.index / vr.has_payload /
           vr.payload_count / vr.type_name in the cloned body. Reuses the mature
           match check + codegen (drop/move/exhaustiveness) — zero new ownership
           code, same打法 as the for-in / combinator desugaring. */
        if (s->kind == AST_COMPTIME_MATCH) {
            Type *subj_t = check_expr(c, s->as.comptime_match.subject);
            Type *en = subj_t;
            while (en && (en->kind == TYPE_REFERENCE || en->kind == TYPE_POINTER) &&
                   en->as.pointer_to)
                en = en->as.pointer_to;
            if (en == NULL || en->kind != TYPE_ENUM) {
                checker_error(c, s->line, s->column,
                    "comptime match requires an enum subject, got '%s'",
                    subj_t ? type_name(subj_t) : "<error>");
                ast_free(s);
                continue;
            }
            const char *vhandle = s->as.comptime_match.handle;
            const char *binder  = s->as.comptime_match.binder;
            int vc = en->as.enom.variant_count;
            MatchArm *arms = (MatchArm *)malloc_safe((size_t)vc * sizeof(MatchArm));
            for (int vi = 0; vi < vc; vi++) {
                const char *vname = en->as.enom.variants[vi].name;
                int pc = en->as.enom.variants[vi].payload_count;
                /* pattern: bare `Variant` (no payload) or `Variant(p, _, ...)`. */
                AstNode *pat;
                if (pc <= 0) {
                    pat = ct_ident(vname, s->line, s->column);
                } else {
                    pat = ast_new(AST_CALL, s->line, s->column);
                    pat->as.call.callee = ct_ident(vname, s->line, s->column);
                    pat->as.call.args = (AstNode **)malloc_safe((size_t)pc * sizeof(AstNode *));
                    pat->as.call.arg_count = pc;
                    for (int b = 0; b < pc; b++)
                        pat->as.call.args[b] =
                            ct_ident((b == 0 && binder) ? binder : "_", s->line, s->column);
                }
                /* body: clone + substitute the variant handle (vr.*). */
                const char *vtn = "";
                if (pc > 0 && en->as.enom.variants[vi].payload_types &&
                    en->as.enom.variants[vi].payload_types[0])
                    vtn = type_name(en->as.enom.variants[vi].payload_types[0]);
                char *vtn_owned = chk_strdup(vtn);
                AstNode *bcopy = ast_clone_deep(s->as.comptime_match.body);
                CtRw cx = { vhandle, vname, (long long)vi, vtn_owned, pc };
                ct_rewrite(&bcopy, &cx);
                free(vtn_owned);
                arms[vi].pattern = pat;
                arms[vi].body    = bcopy;
            }
            AstNode *mt = ast_new(AST_MATCH, s->line, s->column);
            mt->as.match.subject   = s->as.comptime_match.subject;  /* transfer ownership */
            mt->as.match.arms      = arms;
            mt->as.match.arm_count = vc;
            s->as.comptime_match.subject = NULL;
            AstNode *es = ast_new(AST_EXPR_STMT, s->line, s->column);
            es->as.expr_stmt.expr = mt;
            ast_free(s);
            CT_PUSH(es);
            continue;
        }

        if (s->kind != AST_COMPTIME_FOR) { CT_PUSH(s); continue; }

        Type *st = resolve_type_node(c, s->as.comptime_for.over_type, s->line, s->column);
        if (st == NULL) { ast_free(s); continue; }  /* resolve already reported */
        const char *fvar = s->as.comptime_for.var;

        if (s->as.comptime_for.over_variants) {
            /* comptime v2 — variants(T): unroll the body once per enum variant,
               exposing vr.name (Str) / vr.index (int) / vr.type_name (payload type
               name, "" when the variant has no payload). Metadata-only (mirrors
               fields(T)); value dispatch over the active variant still needs a
               runtime `match self { ... }`. */
            if (st->kind != TYPE_ENUM) {
                checker_error(c, s->line, s->column,
                    "comptime for requires an enum type in variants(...), got '%s'",
                    type_name(st));
                ast_free(s);
                continue;
            }
            int vc = st->as.enom.variant_count;
            for (int vi = 0; vi < vc; vi++) {
                const char *vname = st->as.enom.variants[vi].name;
                int pc = st->as.enom.variants[vi].payload_count;
                /* payload type name: first payload's type, "" when no payload. */
                const char *vtn = "";
                if (pc > 0 && st->as.enom.variants[vi].payload_types &&
                    st->as.enom.variants[vi].payload_types[0])
                    vtn = type_name(st->as.enom.variants[vi].payload_types[0]);
                char *vtn_owned = chk_strdup(vtn); /* type_name() reuses a pooled buffer */
                AstNode *copy = ast_clone_deep(s->as.comptime_for.body);  /* AST_BLOCK */
                CtRw cx = { fvar, vname, (long long)vi, vtn_owned, pc };
                ct_rewrite(&copy, &cx);
                free(vtn_owned);
                CT_PUSH(copy);  /* one nested block per variant = its own scope */
            }
            ast_free(s);
            continue;
        }

        if (st->kind != TYPE_STRUCT) {
            checker_error(c, s->line, s->column,
                "comptime for requires a struct type in fields(...), got '%s'",
                type_name(st));
            ast_free(s);
            continue;
        }
        int fc = st->as.strukt.field_count;
        for (int fi = 0; fi < fc; fi++) {
            const char *fname = st->as.strukt.fields[fi].name;
            /* v1: type_name() as-is (may carry a module prefix for imported generic
               instances — refine later; local/primitive field types are clean). */
            const char *ftn = st->as.strukt.fields[fi].type
                ? type_name(st->as.strukt.fields[fi].type) : "void";
            char *ftn_owned = chk_strdup(ftn); /* type_name() reuses a pooled buffer */
            AstNode *copy = ast_clone_deep(s->as.comptime_for.body);  /* AST_BLOCK */
            CtRw cx = { fvar, fname, (long long)fi, ftn_owned, -1 };
            ct_rewrite(&copy, &cx);
            free(ftn_owned);
            CT_PUSH(copy);  /* one nested block per field = its own scope */
        }
        ast_free(s);
    }
    #undef CT_PUSH

    free(block->as.block.stmts);
    block->as.block.stmts = out;
    block->as.block.stmt_count = nc;
}

/* ---- Statement checking ---- */

void check_stmt(Checker *c, AstNode *node)
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
            /* Inferred BORROW (the for-in borrow desugar `x = v.get_ref(i)` infers
               &T): route through the named-local-borrow machinery so x is marked
               is_borrow + its source is pinned, exactly like an explicit
               `&T x = v.get_ref(i)`. Without this, x is bound as a bare reference
               type with no borrow flag → field/method access misresolves (e.g.
               `x.len()` hits the Str.len field instead of the method). */
            if (it->kind == TYPE_REFERENCE)
            {
                check_local_borrow_decl(c, node, it);
                node->resolved_type = it;
                break;
            }
            node->resolved_type = it;
            scope_define(c->current_scope, node->as.var_decl.name, it);
            break;
        }

        Type *declared = resolve_type_node(c, node->as.var_decl.var_type,
                                           node->line, node->column);
        if (declared == NULL)
            break;

        /* Phase 1 (borrow extension): a named local borrow `&T r = &x`. Handled
           in full by a dedicated path (registers the symbol with the pointee
           type + is_borrow, pins the referent); skip the owned-value machinery. */
        if (declared->kind == TYPE_REFERENCE)
        {
            check_local_borrow_decl(c, node, declared);
            node->resolved_type = declared;
            break;
        }

        /* A slice local `&array(T) s = v[a..b]` — a borrowed {ptr,len} value that
           pins its source. Handled by a dedicated path; skip owned machinery. */
        if (declared->kind == TYPE_SLICE)
        {
            check_local_slice_decl(c, node, declared);
            node->resolved_type = declared;
            break;
        }

        /* REPL Phase 2: a replayed pre-existing global (container/has_drop).
           Register the symbol so later statements resolve it, but skip M-DEF
           (which would re-synthesize `= {}` → re-construct + reset the live
           global each snippet), init handling, and move-tracking. The storage
           lives in the introducing snippet; jit.c strips this re-emitted copy
           to an external reference. Only the REPL sets this flag, so AOT/JIT
           file execution never takes this path. */
        if (node->as.var_decl.is_repl_extern)
        {
            if (!scope_resolve_local(c->current_scope, node->as.var_decl.name))
                scope_define(c->current_scope, node->as.var_decl.name, declared);
            node->resolved_type = declared;
            break;
        }

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
                /* Phase 2 (borrow extension): forbid silently copying a
                   borrow-returning call result into an OWNED variable
                   (`Inner b = o.get()`). The `T ← &T` auto-reborrow would store
                   the pointer as a value / alias a has_drop referent. Bind a
                   borrow (`&Inner r = o.get()`) or copy a specific field. */
                if (init_type != NULL && init_type->kind == TYPE_REFERENCE &&
                    declared->kind != TYPE_REFERENCE &&
                    node->as.var_decl.init->kind == AST_CALL)
                {
                    checker_error(c, node->line, node->column,
                        "cannot copy a value out of a borrow result; bind it to "
                        "a borrow (`&%s %s = ...`) to alias it instead",
                        type_name(declared), node->as.var_decl.name);
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
            scope_define(c->current_scope, node->as.var_decl.name, declared);
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
        if (node->as.assign.target->kind == AST_INDEX &&
            node->as.assign.target->as.index_expr.index_count >= 2)
        {
            /* Multi-subscript store t[i,j,..] = v -> __index_set{N}(i,j,..,v). */
            AstNode *tgt = node->as.assign.target;
            AstNode *tobj = tgt->as.index_expr.object;
            Type *to = check_expr(c, tobj);
            int nidx = tgt->as.index_expr.index_count;
            char mname[28];
            snprintf(mname, sizeof(mname), "__index_set%d", nidx);
            if (to && to->kind == TYPE_STRUCT && find_method_ensured(c, to, mname) != NULL)
            {
                AstNode *valn = node->as.assign.value;
                AstNode *call = make_multi_index_call(node->line, node->column,
                    tobj, tgt->as.index_expr.indices, nidx, valn, mname);
                node->kind = AST_EXPR_STMT;
                node->as.expr_stmt.expr = call;
                check_expr(c, call);
                break;
            }
            checker_error(c, node->line, node->column,
                "type '%s' does not support %d-D index assignment (no method '%s')",
                to ? type_name(to) : "?", nidx, mname);
            break;
        }
        if (node->as.assign.target->kind == AST_INDEX)
        {
            AstNode *tobj = node->as.assign.target->as.index_expr.object;
            Type *to = check_expr(c, tobj);
            /* `s[i] = x` on a writable slice — store into the borrowed range.
               Read-only slices reject; has_drop elements deferred (the store
               would need drop-old-element semantics). */
            if (to && to->kind == TYPE_SLICE)
            {
                if (!to->is_mut)
                {
                    checker_error(c, node->line, node->column,
                        "cannot assign through a read-only slice; bind it as "
                        "`&!array(T)` for a writable view");
                    break;
                }
                Type *et = to->as.array.elem;
                Type *idxt = check_expr(c, node->as.assign.target->as.index_expr.index);
                if (idxt && !type_is_integer(idxt))
                    checker_error(c, node->line, node->column,
                                  "slice index must be integer, got '%s'", type_name(idxt));
                Type *valt = check_expr(c, node->as.assign.value);
                if (valt && et && !type_assignable(et, valt))
                    checker_error(c, node->line, node->column,
                        "cannot store '%s' into slice of '%s'",
                        type_name(valt), type_name(et));
                node->resolved_type = type_void();
                break;
            }
            if (to && to->kind == TYPE_STRUCT &&
                find_method_ensured(c, to, "__index_set") != NULL)
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
        /* A comptime constant is immutable. Detect this BEFORE check_expr folds the
           target IDENT into a literal (which would otherwise yield a confusing
           "invalid assignment target"). */
        if (node->as.assign.target->kind == AST_IDENT) {
            Symbol *cc_sym = scope_resolve(c->current_scope,
                                           node->as.assign.target->as.ident.name);
            if (cc_sym && cc_sym->is_comptime_const) {
                checker_error(c, node->line, node->column,
                              "cannot assign to comptime constant '%s'",
                              node->as.assign.target->as.ident.name);
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
            if (!type_is_numeric(target))
            {
                checker_error(c, node->line, node->column,
                              "compound assignment requires numeric type, got '%s'",
                              type_name(target));
                break;
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
            if (c->current_fn_return->kind == TYPE_SLICE)
            {
                /* Slice return under single-input elision: the returned view must
                   be a slice of matching element type, rooted at the one borrow
                   input (`self.field[a..b]` → self). A view of a local/temporary
                   would dangle. */
                Type *want = c->current_fn_return;
                if (val != NULL && (val->kind != TYPE_SLICE ||
                    !type_equals(val->as.array.elem, want->as.array.elem)))
                {
                    checker_error(c, node->line, node->column,
                                  "return type mismatch: expected '%s', got '%s'",
                                  type_name(want), type_name(val));
                }
                Symbol *root = node->as.return_stmt.value
                    ? checker_place_root_symbol(c, node->as.return_stmt.value) : NULL;
                if (root == NULL || !(root->is_borrow || root->is_mut_borrow))
                {
                    checker_error(c, node->line, node->column,
                        "a returned slice must derive from the `&self` / borrow "
                        "parameter (e.g. `self.field[a..b]`); cannot return a view "
                        "of a local or temporary — it would dangle");
                }
            }
            else if (c->current_fn_return->kind == TYPE_REFERENCE)
            {
                /* Phase 2 (borrow extension): the function returns a borrow (&T
                   / &!T). The returned expression must be a PLACE rooted at the
                   single borrow input (`self` / a borrow parameter) whose pointee
                   matches — escape analysis: a borrow of a LOCAL or temporary
                   would dangle once the function returns. The generic
                   type_assignable path is skipped here: `&!T ← T` is not a normal
                   auto-borrow, but returning the place of a `&!self` IS sound. */
                Type *pointee = c->current_fn_return->as.pointer_to;
                Type *vp = (val && val->kind == TYPE_REFERENCE)
                               ? val->as.pointer_to : val;
                if (vp != NULL && pointee != NULL && !type_equals(vp, pointee))
                {
                    checker_error(c, node->line, node->column,
                                  "return type mismatch: expected '%s', got '%s'",
                                  type_name(c->current_fn_return), type_name(val));
                }
                /* v1 scope: borrow returns are AGGREGATE-only (struct/enum, pointer
                   ABI + field-access auto-deref). A POD-scalar borrow return
                   (`-> &int`) has no wired value-context auto-deref (`x == 7` would
                   see `&int`), so reject it clearly. Screened HERE (body check) not
                   at signature registration so an uncalled generic `get_ref(&self)
                   ->&T` on a POD instance (e.g. Vec(int)) does not poison the whole
                   instantiation — only an actual scalar instantiation errors.
                   Reading a POD element needs no borrow: return by value / `get!`.
                   (docs/plan_borrow_extension.md "下一步") */
                if (pointee != NULL && pointee->kind != TYPE_STRUCT &&
                    pointee->kind != TYPE_ENUM)
                {
                    checker_error(c, node->line, node->column,
                        "cannot return a borrow of a POD scalar (&%s%s): borrow "
                        "returns are supported for struct/enum elements only — a POD "
                        "value needs no borrow, return it by value (or use `get!`)",
                        c->current_fn_return->is_mut ? "!" : "", type_name(pointee));
                }
                Symbol *root = node->as.return_stmt.value
                    ? checker_place_root_symbol(c, node->as.return_stmt.value) : NULL;
                if (root == NULL || !(root->is_borrow || root->is_mut_borrow))
                {
                    checker_error(c, node->line, node->column,
                        "a returned borrow must derive from the `&self` / borrow "
                        "parameter (a place like `self` or `self.field`); cannot "
                        "return a borrow of a local or temporary — it would dangle");
                }
                else if (c->current_fn_return->is_mut && root->is_borrow)
                {
                    /* Returning &!T but the input is only a read-only borrow. */
                    checker_error(c, node->line, node->column,
                        "cannot return a writable borrow `&!` derived from the "
                        "read-only borrow '%s'", root->name);
                }
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
        /* Borrowing for-in: `for x in &v` — bind x as a zero-copy borrow &T of
           each element (vs the owning `for x in v` which clones on read). v must
           be a Vec-like exposing get_ref(i)->&T and len(). Desugars to an index
           loop over get_ref (see build_foreach_borrow_desugar). */
        {
            AstNode *itn = node->as.for_stmt.iter;
            if (itn->kind == AST_UNARY && itn->as.unary.op == TOKEN_AMP)
            {
                Type *ct = check_expr(c, itn->as.unary.operand);
                if (ct != NULL && ct->kind == TYPE_STRUCT)
                {
                    bool has_getref = find_method_ensured(c, ct, "get_ref") != NULL;
                    bool has_len    = find_method_ensured(c, ct, "len") != NULL;
                    if (has_getref && has_len)
                    {
                        AstNode *d = build_foreach_borrow_desugar(node);
                        node->as.for_stmt.desugared = d;
                        check_stmt(c, d);
                        break;
                    }
                }
                /* not a borrowable container — fall through; check_expr below
                   re-runs on the borrow and emits the "cannot iterate" error. */
            }
        }

        Type *iter = check_expr(c, node->as.for_stmt.iter);

        /* Iterator-protocol path: a struct that exposes iter()->I or is itself an
           iterator (has next()) is desugared into the equivalent while/match loop
           and that subtree is checked instead (docs/plan_userdef_for_in.md). */
        if (iter != NULL && iter->kind == TYPE_STRUCT)
        {
            /* find_method_ensured (not find_method) so an imported generic
               instance — e.g. Vec(FieldInfo) from a generic type's reflect() —
               has its iter()/next() registered on demand (VR-LIM-018), letting
               `for x in genericThing.method()` work, not just direct calls. */
            bool has_iter = find_method_ensured(c, iter, "iter") != NULL;
            bool has_next = find_method_ensured(c, iter, "next") != NULL;
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

        chk_push_scope(c);
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
            else if (iter->kind == TYPE_SLICE)
            {
                /* Slice iteration: loop variable is the element type. */
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
        chk_pop_scope(c);
        break;
    }

    case AST_FOR_C:
    {
        /* C-style for: for (init; cond; update) { body }
           All three clauses are optional. */
        chk_push_scope(c);
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
        chk_pop_scope(c);
        break;
    }

    case AST_BLOCK:
    {
        chk_push_scope(c);
        comptime_expand_block(c, node);  /* Stage 3b: unroll comptime for in place */
        for (int i = 0; i < node->as.block.stmt_count; i++)
        {
            check_stmt(c, node->as.block.stmts[i]);
        }
        chk_pop_scope(c);
        break;
    }

    case AST_EXPR_STMT:
        check_expr(c, node->as.expr_stmt.expr);
        break;

    case AST_BREAK:
    case AST_CONTINUE:
        break;

    case AST_COMPTIME_FOR:
        /* Step 2: comptime_expand_block unrolls these in place before the block's
           statements are checked, so reaching here means a comptime for appeared
           outside an expandable statement position. */
        checker_error(c, node->line, node->column,
            "comptime for must appear at statement position inside a block");
        break;
    case AST_COMPTIME_IF:
        /* comptime_expand_block evaluates these in place before a block's
           statements are checked; reaching here means a misplaced comptime if. */
        checker_error(c, node->line, node->column,
            "comptime if must appear at statement position inside a block");
        break;
    case AST_COMPTIME_MATCH:
        /* comptime_expand_block expands these into a real match before a block's
           statements are checked; reaching here means a misplaced comptime match. */
        checker_error(c, node->line, node->column,
            "comptime match must appear at statement position inside a block");
        break;

    case AST_COMPTIME_CONST:
    {
        /* docs/plan_comptime_consteval.md: evaluate a comptime constant at compile
           time. Scalars (Steps 2+3) fold into a literal at every use. Arrays (Step 4)
           are rewritten into a normal `array(T,N) X = [lits]` var-decl so codegen
           emits a constant-initialized global (→ .rodata at -O2). */
        Type *dt = resolve_type_node(c, node->as.comptime_const.decl_type,
                                     node->line, node->column);
        if (dt == NULL) break;

        if (dt->kind == TYPE_ARRAY) {
            Type *el = dt->as.array.elem;
            int n = dt->as.array.size;
            bool el_scalar = el && (type_is_numeric(el) || el->kind == TYPE_BOOL || el->kind == TYPE_CHAR);
            if (!el_scalar || n <= 0) {
                checker_error(c, node->line, node->column,
                    "comptime array constant '%s' must have a scalar element type and a "
                    "positive length", node->as.comptime_const.name);
                break;
            }
            AstNode *arhs = node->as.comptime_const.value;
            if (!(arhs && arhs->kind == AST_COMPTIME_BLOCK)) {
                checker_error(c, node->line, node->column,
                    "comptime array constant '%s' requires a `comptime { ... return arr }` "
                    "block", node->as.comptime_const.name);
                break;
            }
            CtEval ev; memset(&ev, 0, sizeof ev); ev.budget = CT_BUDGET_DEFAULT;
            CtScalar dummy;
            CtFlow f = ct_exec_block(c, arhs->as.comptime_block.block, &ev, &dummy);
            int ai = (f == CT_RETURNED && ev.ret_array) ? ct_aenv_find(&ev, ev.ret_array) : -1;
            if (ai < 0) {
                ct_env_free(&ev);
                checker_error(c, node->line, node->column,
                    "comptime array constant '%s': the block must build an array local "
                    "(`array(T,N) t = {}`), fill it, and `return` it",
                    node->as.comptime_const.name);
                break;
            }
            if (ev.alens[ai] != n) {
                int got = ev.alens[ai];
                ct_env_free(&ev);
                checker_error(c, node->line, node->column,
                    "comptime array constant '%s': returned array length %d does not match "
                    "declared length %d", node->as.comptime_const.name, got, n);
                break;
            }
            bool el_float = type_is_float(el);
            TypeNode *elem_tn = node->as.comptime_const.decl_type->as.array.elem;
            CtScalar *arr = ev.arrs[ai];
            AstNode *lit = ast_new(AST_ARRAY_LIT, node->line, node->column);
            lit->as.array_lit.count = n;
            lit->as.array_lit.elements = malloc_safe((size_t)n * sizeof(AstNode *));
            for (int k = 0; k < n; k++) {
                AstNode *e;
                if (el_float) {
                    e = ast_new(AST_FLOAT_LIT, node->line, node->column);
                    e->as.float_lit.value = cts_to_f(&arr[k]);
                } else {
                    e = ast_new(AST_INT_LIT, node->line, node->column);
                    e->as.int_lit.value   = arr[k].i;
                    e->as.int_lit.is_char  = (el->kind == TYPE_CHAR);
                }
                /* Wrap in `(value) as <elem-type>` so the element's static type exactly
                   matches the declared array element type. LS array literals don't
                   coerce to the declared type, and int literals default to 'int' — so
                   an i64/f32/sized-int element array would otherwise mismatch. */
                AstNode *cast = ast_new(AST_CAST, node->line, node->column);
                cast->as.cast.expr = e;
                cast->as.cast.target_type = type_node_clone(elem_tn);
                lit->as.array_lit.elements[k] = cast;
            }
            ct_env_free(&ev);
            /* rewrite this node into a normal array var-decl and re-check it */
            TypeNode *aty = node->as.comptime_const.decl_type;
            char *nm = node->as.comptime_const.name;
            ast_free(node->as.comptime_const.value);
            node->kind = AST_VAR_DECL;
            node->resolved_type = NULL;
            node->as.var_decl.var_type = aty;
            node->as.var_decl.name = nm;
            node->as.var_decl.init = lit;
            node->as.var_decl.is_repl_extern = false;
            check_stmt(c, node);
            break;
        }

        bool is_scalar = type_is_numeric(dt) || dt->kind == TYPE_BOOL || dt->kind == TYPE_CHAR;
        if (!is_scalar) {
            checker_error(c, node->line, node->column,
                "comptime constant '%s': only scalar or array(T,N) types are supported "
                "(got a struct/other type)", node->as.comptime_const.name);
            break;
        }
        AstNode *rhs = node->as.comptime_const.value;
        CtScalar v;
        if (rhs && rhs->kind == AST_COMPTIME_BLOCK) {
            CtEval ev; memset(&ev, 0, sizeof ev);
            ev.budget = CT_BUDGET_DEFAULT;
            CtFlow f = ct_exec_block(c, rhs->as.comptime_block.block, &ev, &v);
            ct_env_free(&ev);
            if (f != CT_RETURNED) {
                checker_error(c, node->line, node->column,
                    "comptime block for '%s' could not be evaluated: it must be a "
                    "compile-time-constant computation ending in `return` (allowed: "
                    "scalar locals, assignments, if/else, bounded `for i in 0..N`)",
                    node->as.comptime_const.name);
                break;
            }
        } else if (!ct_eval_scalar(c, rhs, NULL, &v)) {
            checker_error(c, node->line, node->column,
                "comptime constant '%s' is not a compile-time constant (allowed: "
                "int/float literals, other comptime constants, math.*, and "
                "+ - * / %% bitwise/shift/compare/logical operators)",
                node->as.comptime_const.name);
            break;
        }
        if (!type_is_float(dt) && v.is_float) {
            checker_error(c, node->line, node->column,
                "comptime constant '%s' evaluates to a floating-point value but is "
                "declared as an integer type", node->as.comptime_const.name);
            break;
        }
        node->as.comptime_const.is_global = (c->current_scope->depth == 0);
        node->resolved_type = dt;
        Symbol *s = scope_define(c->current_scope, node->as.comptime_const.name, dt);
        if (s == NULL) {
            checker_error(c, node->line, node->column,
                "'%s' is already defined in this scope", node->as.comptime_const.name);
            break;
        }
        s->is_comptime_const = true;
        if (type_is_float(dt)) { s->ct_is_float = true;  s->ct_f = cts_to_f(&v); s->ct_i = 0; }
        else if (dt->kind == TYPE_BOOL) { s->ct_is_float = false; s->ct_i = (v.i != 0) ? 1 : 0; }
        else { s->ct_is_float = false; s->ct_i = v.i; }
        break;
    }

    default:
        /* Declarations or expressions — dispatch */
        check_decl(c, node);
        break;
    }
}

/* ---- Declaration checking (top-level pass) ---- */


















/* ---- Trait registry helpers ---- */


/* ---- Operator overloading: built-in operator traits ---- */
















/* ---- Two-pass checking ---- */

/* Pass 1: Register all struct types and function signatures (forward declarations) */
void forward_pass(Checker *c, AstNode *program)
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
            checker_reject_borrow_return(c, ret, decl, decl->line, decl->column);  /* Phase 0/2 */
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
            if (getenv("LS_DEBUG_MODULES"))
                fprintf(stderr, "[mod] import '%s' (from %s): builtin=%d userfile=%d\n",
                        import_path, c->source_path ? c->source_path : "?",
                        builtin_module_exists(import_path),
                        module_user_file_exists(import_path, c->source_path));
            if (builtin_module_exists(import_path) &&
                !module_user_file_exists(import_path, c->source_path))
            {
                Type *mod_type = builtin_module_make_type_merged(c, import_path);
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
                /* The built-in math module moved to the canonical std.core.math
                   path (merged with lib/std/core/math.ls). Help bare `import math`
                   that doesn't resolve to a user file find the new home. */
                if (strcmp(import_path, "math") == 0)
                    checker_error(c, decl->line, decl->column,
                                  "cannot find module 'math'; the built-in math "
                                  "module is now 'std.core.math' (import std.core.math as math)");
                else
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
                /* DANGLING-POINTER FIX: the recursive check above loads the
                   module's own imports, growing reg->modules; GROW_ARRAY's
                   realloc can MOVE the array, invalidating `mod` (a pointer
                   INTO it). Writing mod->checked / reading mod->ast through
                   the stale pointer is a use-after-free whose symptom depends
                   on heap reuse: sometimes fine, sometimes "unknown type
                   'Str'" / "module has no export" cascades, sometimes a
                   silent segfault — all NONDETERMINISTIC per run (hit when
                   the std.str chain pushed the registry past a capacity
                   boundary). Re-resolve by name after the recursion. */
                mod = module_find(c->registry, import_path);
                if (mod == NULL)
                {
                    checker_error(c, decl->line, decl->column,
                                  "module '%s' lost during recursive check",
                                  import_path);
                    break;
                }
                if (getenv("LS_DEBUG_MODULES"))
                    fprintf(stderr, "[mod] checked '%s': ok=%d\n", import_path, (int)ok);
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

            /* Collect exported symbols from the module.
               B-5: key the module TYPE by mod->name (the canonical first-loaded
               spelling), NOT the local import_path. When two spellings resolve to
               the same file (module_load dedups them, e.g. `import std.sys.io` and
               `import sys.io`), both must mangle call-site symbols to the single
               emitted copy. For the common single-spelling case mod->name ==
               import_path, so this is an identity change. The scope binding below
               still keys on the local spelling/alias, so name lookup is unaffected. */
            Type *mod_type = type_module_new(mod->name);
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
                                        NULL, method,  /* imported inherent impl */
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
            /* Phase 1 (docs/plan_module_fn_resolution.md): also bind the full
               canonical dotted path (e.g. "std.time") so `std.time.fn()` resolves
               without an alias, uniformly whether or not an alias was used. The
               dotted name never collides with a real IDENT (no source identifier
               contains '.'). Skip when bind_name already IS the path (no alias). */
            if (decl->as.import_decl.alias &&
                scope_resolve_local(c->current_scope, import_path) == NULL)
                scope_define(c->current_scope, import_path, mod_type);
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
                              "inside an `methods` block",
                              decl->as.fn_decl.self_borrow_kind == 2 ? "!" : "");
                break;
            }

            chk_push_scope(c);
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
                    /* F.2: Block params are shallow-copy borrows of caller's env */
                    if (pt && pt->kind == TYPE_BLOCK)
                        param_sym->is_borrow = true;
                }
            }
            Type *saved_ret = c->current_fn_return;
            c->current_fn_return = fn_type->as.function.return_type;
            check_stmt(c, decl->as.fn_decl.body);
            checker_elide_last_use(c, decl); /* A1 clone-elision */
            c->current_fn_return = saved_ret;
            chk_pop_scope(c);
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

    /* @print(...) -> void — the print intrinsic (only spelling; scanner produces
       IDENT "@print"). Bare print is retired: it resolves to nothing -> a clear
       "undefined 'print'" error. Accepts any printable type. */
    {
        Type *ft = type_function(NULL, 0, type_void(), true);
        scope_define(c->current_scope, "@print", ft);
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

    /* Structured concurrency primitives (std.task's generic `Task(T)`):
         __task_spawn(Block()->T, *T box) -> object   run the closure, result -> box
         __task_join(object)              -> void       wait for the worker
       These are GENERIC in the result type T, so they cannot be a fixed
       scope_define — they are intercepted by name in check_builtin_call (like
       __take/__drop_at). T is read by codegen from arg0's Block return type.
       Internal plumbing — users go through std.task's constructor / join(). The
       closure is MOVE-captured, so each task is isolated (no shared mutable
       state) and sound without a lifetime system. */
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

/* Tear down a Checker after its passes have run. Shared by checker_check and
   checker_inspect. out_gm (may be NULL): if set, no error occurred, and there
   are pending generic methods, ownership transfers to the caller; otherwise the
   pending methods are freed here. Behaviour is identical to the cleanup that
   previously lived inline in checker_check. */
static void checker_teardown(Checker *c, CheckerGenericMethods *out_gm)
{
    /* G1.5: transfer pending generic methods to caller if requested */
    if (out_gm && !c->had_error && c->pending_gm_count > 0) {
        out_gm->count = c->pending_gm_count;
        out_gm->methods = malloc_safe(
            (size_t)c->pending_gm_count * sizeof(out_gm->methods[0]));
        for (int i = 0; i < c->pending_gm_count; i++) {
            out_gm->methods[i].cloned_fn    = c->pending_generic_methods[i].cloned_fn;
            out_gm->methods[i].mangled_name = c->pending_generic_methods[i].mangled_name;
            out_gm->methods[i].struct_type  = c->pending_generic_methods[i].struct_type;
        }
        /* Ownership transferred — just free the container array */
        free(c->pending_generic_methods);
    } else {
        /* Not transferred — free everything as safety net */
        for (int i = 0; i < c->pending_gm_count; i++) {
            if (c->pending_generic_methods[i].cloned_fn)
                ast_free(c->pending_generic_methods[i].cloned_fn);
            free(c->pending_generic_methods[i].mangled_name);
        }
        free(c->pending_generic_methods);
        if (out_gm) { out_gm->methods = NULL; out_gm->count = 0; }
    }

    /* Cleanup */
    for (int i = 0; i < c->lazy_gm_count; i++) {
        free(c->lazy_generic_methods[i].mangled_name);
        free(c->lazy_generic_methods[i].type_args);
    }
    free(c->lazy_generic_methods);
    scope_free(c->current_scope);
    /* Note: struct types and function types are intentionally leaked for now
       since AST nodes reference them via resolved_type. They will be freed
       when the full compilation pipeline is in place. */
    free(c->struct_types);
    free(c->enum_types);
    free(c->type_aliases);
    for (int i = 0; i < c->enum_template_count; i++)
    {
        for (int v = 0; v < c->enum_templates[i].variant_count; v++)
            free(c->enum_templates[i].variants[v].payload);
        free(c->enum_templates[i].variants);
    }
    free(c->enum_templates);
    /* G1: struct_templates — entries point into AST, nothing to deep-free */
    free(c->struct_templates);
    /* G2: fn_templates — entries point into AST, nothing to deep-free */
    free(c->fn_templates);
    for (int i = 0; i < c->impl_count; i++)
    {
        free(c->impl_registry[i].methods);
    }
    free(c->impl_registry);
    /* Trait registry cleanup */
    for (int i = 0; i < c->trait_count; i++)
    {
        free((void *)c->trait_registry[i].name);
        for (int j = 0; j < c->trait_registry[i].method_count; j++)
            free((void *)c->trait_registry[i].methods[j].name);
        free(c->trait_registry[i].methods);
    }
    free(c->trait_registry);
    /* Trait impls — pointers into AST, no deep-free needed */
    free(c->trait_impls);
}

/* ---- `ls inspect`: static reflection of a type's fields + methods ----
   Stage 1.5 of docs/plan_static_reflection.md. Walks the checker registries
   (which already hold field metadata in Type and method metadata in
   impl_registry) and prints a human-readable summary. Pure checker-side: no
   codegen, no new language semantics. */

/* Render a method's self receiver for display; NULL for static methods. */
static const char *inspect_self_str(bool is_static, int self_borrow_kind)
{
    if (is_static) return NULL;
    switch (self_borrow_kind) {
        case 2:  return "&!self";  /* writable borrow */
        case 1:  return "&self";   /* read-only borrow */
        default: return "self";    /* 0 = legacy implicit */
    }
}

/* Append safely to a fixed buffer, keeping pos in [0, cap). */
static int inspect_appendf(char *buf, int pos, int cap, const char *fmt, ...)
{
    if (pos < 0 || pos >= cap) return cap - 1;
    va_list ap;
    va_start(ap, fmt);
    int n = vsnprintf(buf + pos, (size_t)(cap - pos), fmt, ap);
    va_end(ap);
    if (n < 0) return pos;
    pos += n;
    if (pos >= cap) pos = cap - 1;
    return pos;
}

/* Map an internal method name to its user-facing display form, shared by
   `ls inspect` and @derive(Reflect) so both speak the same surface terminology:
   the destructor is `~` (never `__drop`), the deep-copy hook is `clone`, and
   operator overloads show as their symbol. Returns mname unchanged otherwise. */
static const char *method_display_name(const char *mname)
{
    if (strcmp(mname, "__drop") == 0)  return "~";
    if (strcmp(mname, "__clone") == 0) return "clone";
    if (strncmp(mname, "$op_", 4) == 0) {
        const char *o = mname + 4;
        if (strcmp(o, "eq")  == 0) return "==";
        if (strcmp(o, "ne")  == 0) return "!=";
        if (strcmp(o, "lt")  == 0) return "<";
        if (strcmp(o, "le")  == 0) return "<=";
        if (strcmp(o, "gt")  == 0) return ">";
        if (strcmp(o, "ge")  == 0) return ">=";
        if (strcmp(o, "add") == 0) return "+";
        if (strcmp(o, "sub") == 0) return "-";
        if (strcmp(o, "mul") == 0) return "*";
        if (strcmp(o, "div") == 0) return "/";
        if (strcmp(o, "rem") == 0) return "%";
    }
    return mname;
}

static void inspect_print_methods(Checker *c, const char *key)
{
    int idx = -1;
    for (int i = 0; i < c->impl_count; i++) {
        if (c->impl_registry[i].struct_name &&
            strcmp(c->impl_registry[i].struct_name, key) == 0) { idx = i; break; }
    }
    if (idx < 0 || c->impl_registry[idx].method_count == 0) {
        printf("  methods: (none)\n");
        return;
    }
    printf("  methods:\n");
    for (int j = 0; j < c->impl_registry[idx].method_count; j++) {
        const char *mname = c->impl_registry[idx].methods[j].name;
        Type *mt          = c->impl_registry[idx].methods[j].type;
        bool is_static    = c->impl_registry[idx].methods[j].is_static;
        int  sbk          = c->impl_registry[idx].methods[j].self_borrow_kind;

        /* Pretty-print the lifecycle hooks per user terminology (shared mapping). */
        const char *disp = method_display_name(mname);
        const char *tag  = NULL;
        if (strcmp(mname, "__drop") == 0)       tag = "Destroy";
        else if (strcmp(mname, "__clone") == 0) tag = "Clone";

        char line[512];
        int cap = (int)sizeof(line);
        int pos = inspect_appendf(line, 0, cap, "    def %s(", disp);
        const char *self_s = inspect_self_str(is_static, sbk);
        bool first = true;
        if (self_s) { pos = inspect_appendf(line, pos, cap, "%s", self_s); first = false; }
        Type *ret = NULL;
        if (mt && mt->kind == TYPE_FUNCTION) {
            int start = is_static ? 0 : 1;   /* params[0] is the implicit self ptr */
            for (int p = start; p < mt->as.function.param_count; p++) {
                if (!first) pos = inspect_appendf(line, pos, cap, ", ");
                pos = inspect_appendf(line, pos, cap, "%s",
                                      type_name(mt->as.function.params[p]));
                first = false;
            }
            ret = mt->as.function.return_type;
        }
        pos = inspect_appendf(line, pos, cap, ")");
        if (ret && ret->kind != TYPE_VOID)
            pos = inspect_appendf(line, pos, cap, " -> %s", type_name(ret));
        if (is_static) pos = inspect_appendf(line, pos, cap, "   [static]");
        if (tag)       pos = inspect_appendf(line, pos, cap, "   [%s]", tag);
        printf("%s\n", line);
    }
}

/* Print fields and/or methods for the named type. Returns 0 if found, 1 if not.
   want_fields/want_methods gate the two sections (REPL :fields / :methods). */
static int dump_type_info(Checker *c, const char *query,
                          bool want_fields, bool want_methods)
{
    Type *st = find_struct_type(c, query);
    if (st && st->kind == TYPE_STRUCT) {
        printf("struct %s\n", st->as.strukt.name ? st->as.strukt.name : query);
        if (want_fields) {
            if (st->as.strukt.field_count == 0) {
                printf("  fields: (none)\n");
            } else {
                printf("  fields:\n");
                for (int i = 0; i < st->as.strukt.field_count; i++) {
                    printf("    %s : %s%s\n",
                           st->as.strukt.fields[i].name,
                           type_name(st->as.strukt.fields[i].type),
                           st->as.strukt.fields[i].is_private ? "   [private]" : "");
                }
            }
        }
        if (want_methods) {
            const char *skey = impl_key_of_type(st);
            inspect_print_methods(c, skey ? skey : query);
        }
        return 0;
    }
    Type *en = find_enum_type(c, query);
    if (en && en->kind == TYPE_ENUM) {
        printf("enum %s\n", en->as.enom.name ? en->as.enom.name : query);
        if (want_fields) {
            printf("  variants:\n");
            for (int i = 0; i < en->as.enom.variant_count; i++) {
                char line[512];
                int cap = (int)sizeof(line);
                int pos = inspect_appendf(line, 0, cap, "    %s",
                                          en->as.enom.variants[i].name);
                int pc = en->as.enom.variants[i].payload_count;
                if (pc > 0) {
                    pos = inspect_appendf(line, pos, cap, "(");
                    for (int p = 0; p < pc; p++) {
                        if (p > 0) pos = inspect_appendf(line, pos, cap, ", ");
                        pos = inspect_appendf(line, pos, cap, "%s",
                                type_name(en->as.enom.variants[i].payload_types[p]));
                    }
                    pos = inspect_appendf(line, pos, cap, ")");
                }
                printf("%s\n", line);
            }
        }
        if (want_methods) {
            const char *ekey = impl_key_of_type(en);
            inspect_print_methods(c, ekey ? ekey : query);
        }
        return 0;
    }
    fprintf(stderr,
        "inspect: type '%s' not found. It must be a concrete struct or enum "
        "visible in the file (defined or imported). For a generic template, "
        "inspect a concrete instantiation.\n", query);
    return 1;
}

/* ---- Stage 1 (docs/plan_static_reflection.md): @derive(...) expansion ----
   Expand @derive(Trait, ...) on a struct/enum into synthesized
   `methods Type: Trait { ... }` impls by generating source text and re-parsing
   it (no manual AST construction), then appending the impl decls to the program.
   The normal check + codegen pipeline handles them — zero new codegen.
   v1: Equal for structs. Other traits / enum targets report a clear error. */

typedef struct { char *data; size_t len, cap; } DeriveBuf;

static void db_init(DeriveBuf *b) {
    b->cap = 256; b->len = 0;
    b->data = malloc_safe(b->cap); b->data[0] = '\0';
}
static void db_puts(DeriveBuf *b, const char *s) {
    size_t n = strlen(s);
    if (b->len + n + 1 > b->cap) {
        while (b->len + n + 1 > b->cap) b->cap *= 2;
        b->data = realloc_safe(b->data, b->cap);
    }
    memcpy(b->data + b->len, s, n + 1);
    b->len += n;
}
static void db_putint(DeriveBuf *b, int v) {
    char t[24]; snprintf(t, sizeof t, "%d", v); db_puts(b, t);
}
/* Emit a binder list "(p0pfx, p1pfx, ...)" for `n` payload slots; nothing if n==0. */
static void db_binders(DeriveBuf *b, const char *pfx, int n) {
    if (n <= 0) return;
    db_puts(b, "(");
    for (int i = 0; i < n; i++) {
        if (i) db_puts(b, ", ");
        db_puts(b, pfx); db_putint(b, i);
    }
    db_puts(b, ")");
}

/* Emit one field's Value-constructing expression for @derive(Serialize):
   Int/Float/Bool leaves for primitives, Text for Str, recurse via .to_value()
   for nested struct/enum fields. */
static void derive_emit_serialize_field(DeriveBuf *sb, const char *fname, TypeNode *ft) {
    if (ft != NULL && ft->kind == TYPE_NODE_PRIMITIVE) {
        TokenType p = ft->as.primitive;
        if (p == TOKEN_TYPE_F32 || p == TOKEN_TYPE_F64) {
            db_puts(sb, "VFloat(self."); db_puts(sb, fname); db_puts(sb, " as f64)");
        } else if (p == TOKEN_TYPE_BOOL) {
            db_puts(sb, "VBool(self."); db_puts(sb, fname); db_puts(sb, ")");
        } else {   /* int family + char */
            db_puts(sb, "VInt(self."); db_puts(sb, fname); db_puts(sb, " as i64)");
        }
    } else if (ft != NULL && ft->kind == TYPE_NODE_NAMED && ft->as.named.name &&
               strcmp(ft->as.named.name, "Str") == 0) {
        db_puts(sb, "VStr(self."); db_puts(sb, fname); db_puts(sb, ".copy())");
    } else {   /* nested struct/enum: recurse (requires its own Serialize) */
        db_puts(sb, "self."); db_puts(sb, fname); db_puts(sb, ".to_value()");
    }
}

/* Primitive TypeNode token -> LS type-name keyword (for casts in Deserialize). */
static const char *prim_type_name(TokenType p) {
    switch (p) {
        case TOKEN_TYPE_I8:  return "i8";
        case TOKEN_TYPE_I16: return "i16";
        case TOKEN_TYPE_I32: return "i32";
        case TOKEN_TYPE_I64: return "i64";
        case TOKEN_TYPE_U8:  return "u8";
        case TOKEN_TYPE_U16: return "u16";
        case TOKEN_TYPE_U32: return "u32";
        case TOKEN_TYPE_U64: return "u64";
        case TOKEN_TYPE_CHAR: return "char";
        default: return "int";
    }
}

/* Emit one field's extraction expression for @derive(Deserialize):
   as_int/as_f64/as_bool/as_str(obj_get(v, "f")) for leaves (cast to the field's
   exact type), or <Type>.from_value(obj_get(v, "f")) for nested struct fields. */
static void derive_emit_deserialize_field(DeriveBuf *sb, const char *fname, TypeNode *ft) {
    /* value.ls helpers are reached by canonical path (free functions aren't
       visible unqualified across modules, same as std.core.hash.fx_mix). */
    const char *G = "std.core.value.obj_get(v, \"";
    if (ft != NULL && ft->kind == TYPE_NODE_PRIMITIVE) {
        TokenType p = ft->as.primitive;
        if (p == TOKEN_TYPE_F32 || p == TOKEN_TYPE_F64) {
            db_puts(sb, "std.core.value.as_f64("); db_puts(sb, G); db_puts(sb, fname); db_puts(sb, "\"))");
            if (p == TOKEN_TYPE_F32) db_puts(sb, " as f32");
        } else if (p == TOKEN_TYPE_BOOL) {
            db_puts(sb, "std.core.value.as_bool("); db_puts(sb, G); db_puts(sb, fname); db_puts(sb, "\"))");
        } else {
            db_puts(sb, "std.core.value.as_int("); db_puts(sb, G); db_puts(sb, fname);
            db_puts(sb, "\")) as "); db_puts(sb, prim_type_name(p));
        }
    } else if (ft != NULL && ft->kind == TYPE_NODE_NAMED && ft->as.named.name &&
               strcmp(ft->as.named.name, "Str") == 0) {
        db_puts(sb, "std.core.value.as_str("); db_puts(sb, G); db_puts(sb, fname); db_puts(sb, "\"))");
    } else if (ft != NULL && ft->kind == TYPE_NODE_NAMED && ft->as.named.name) {
        db_puts(sb, ft->as.named.name); db_puts(sb, ".from_value(");
        db_puts(sb, G); db_puts(sb, fname); db_puts(sb, "\"))");
    } else {
        /* unsupported (Vec/Map/array/...) — v1 covers primitive/Str/nested-struct. */
        db_puts(sb, "std.core.value.as_int("); db_puts(sb, G); db_puts(sb, fname); db_puts(sb, "\"))");
    }
}

/* Emit one field's STRICT extraction for @derive(Deserialize)'s try_from_value:
   `(try std.core.value.try_as_int(obj_get(v,"f"))) as <type>` for primitive leaves,
   `try <Type>.try_from_value(obj_get(v,"f"))` for Str / nested struct / type-param
   fields — `try` propagates the first Err (missing field via VNull, or type mismatch)
   out of try_from_value. */
static void derive_emit_try_deserialize_field(DeriveBuf *sb, const char *fname, TypeNode *ft) {
    const char *G = "std.core.value.obj_get(v, \"";
    if (ft != NULL && ft->kind == TYPE_NODE_PRIMITIVE) {
        TokenType p = ft->as.primitive;
        if (p == TOKEN_TYPE_F32 || p == TOKEN_TYPE_F64) {
            db_puts(sb, "(try std.core.value.try_as_f64("); db_puts(sb, G); db_puts(sb, fname); db_puts(sb, "\")))");
            if (p == TOKEN_TYPE_F32) db_puts(sb, " as f32");
        } else if (p == TOKEN_TYPE_BOOL) {
            db_puts(sb, "(try std.core.value.try_as_bool("); db_puts(sb, G); db_puts(sb, fname); db_puts(sb, "\")))");
        } else {
            db_puts(sb, "(try std.core.value.try_as_int("); db_puts(sb, G); db_puts(sb, fname);
            db_puts(sb, "\"))) as "); db_puts(sb, prim_type_name(p));
        }
    } else if (ft != NULL && ft->kind == TYPE_NODE_NAMED && ft->as.named.name &&
               strcmp(ft->as.named.name, "Str") == 0) {
        db_puts(sb, "(try std.core.value.try_as_str("); db_puts(sb, G); db_puts(sb, fname); db_puts(sb, "\")))");
    } else if (ft != NULL && ft->kind == TYPE_NODE_NAMED && ft->as.named.name) {
        db_puts(sb, "try "); db_puts(sb, ft->as.named.name); db_puts(sb, ".try_from_value(");
        db_puts(sb, G); db_puts(sb, fname); db_puts(sb, "\"))");
    } else {
        db_puts(sb, "(try std.core.value.try_as_int("); db_puts(sb, G); db_puts(sb, fname); db_puts(sb, "\")))");
    }
}

/* Append a TypeNode's display string (for @derive(Reflect) field/param types). */
static void derive_emit_typename(DeriveBuf *sb, TypeNode *ft) {
    if (ft == NULL) { db_puts(sb, "void"); return; }
    switch (ft->kind) {
        case TYPE_NODE_PRIMITIVE: {
            TokenType p = ft->as.primitive;
            switch (p) {
                case TOKEN_TYPE_INT:  db_puts(sb, "int"); break;
                case TOKEN_TYPE_I8:   db_puts(sb, "i8"); break;
                case TOKEN_TYPE_I16:  db_puts(sb, "i16"); break;
                case TOKEN_TYPE_I32:  db_puts(sb, "i32"); break;
                case TOKEN_TYPE_I64:  db_puts(sb, "i64"); break;
                case TOKEN_TYPE_U8:   db_puts(sb, "u8"); break;
                case TOKEN_TYPE_U16:  db_puts(sb, "u16"); break;
                case TOKEN_TYPE_U32:  db_puts(sb, "u32"); break;
                case TOKEN_TYPE_U64:  db_puts(sb, "u64"); break;
                case TOKEN_TYPE_F32:  db_puts(sb, "f32"); break;
                case TOKEN_TYPE_F64:  db_puts(sb, "f64"); break;
                case TOKEN_TYPE_BOOL: db_puts(sb, "bool"); break;
                case TOKEN_TYPE_CHAR: db_puts(sb, "char"); break;
                default: db_puts(sb, "?"); break;
            }
            break;
        }
        case TYPE_NODE_NAMED:
            db_puts(sb, ft->as.named.name ? ft->as.named.name : "?");
            if (ft->as.named.arg_count > 0) {
                db_puts(sb, "(");
                for (int i = 0; i < ft->as.named.arg_count; i++) {
                    if (i) db_puts(sb, ", ");
                    derive_emit_typename(sb, ft->as.named.args[i]);
                }
                db_puts(sb, ")");
            }
            break;
        case TYPE_NODE_POINTER:
            db_puts(sb, "*"); derive_emit_typename(sb, ft->as.pointee);
            break;
        case TYPE_NODE_REFERENCE:
            db_puts(sb, ft->is_mut ? "&!" : "&");
            derive_emit_typename(sb, ft->as.pointee);
            break;
        default:
            db_puts(sb, "?");
            break;
    }
}

/* Append a method's signature string for @derive(Reflect), e.g.
   "def area(&self) -> int". Built from the AST_FN_DECL (no quotes/backslashes
   appear in type/method names, so it is safe inside a "..." source literal). */
static void derive_emit_method_sig(DeriveBuf *sb, AstNode *fn) {
    db_puts(sb, "def "); db_puts(sb, method_display_name(fn->as.fn_decl.name)); db_puts(sb, "(");
    bool first = true;
    if (!fn->as.fn_decl.is_static) {
        int sbk = fn->as.fn_decl.self_borrow_kind;
        db_puts(sb, sbk == 2 ? "&!self" : (sbk == 1 ? "&self" : "self"));
        first = false;
    }
    for (int i = 0; i < fn->as.fn_decl.param_count; i++) {
        if (!first) db_puts(sb, ", ");
        derive_emit_typename(sb, fn->as.fn_decl.param_types[i]);
        first = false;
    }
    db_puts(sb, ")");
    if (fn->as.fn_decl.return_type != NULL) {
        db_puts(sb, " -> ");
        derive_emit_typename(sb, fn->as.fn_decl.return_type);
    }
}

/* Emit the synthesized impl(s) for one struct's derives into sb. `program` is
   used by @derive(Reflect) to scan for the struct's methods. */
/* Append the type-param list "(T, U)" when the struct is generic, else nothing. */
static void db_tparams(DeriveBuf *sb, AstNode *d) {
    int n = d->as.struct_decl.type_param_count;
    if (n <= 0) return;
    db_puts(sb, "(");
    for (int i = 0; i < n; i++) {
        if (i) db_puts(sb, ", ");
        db_puts(sb, d->as.struct_decl.type_params[i]);
    }
    db_puts(sb, ")");
}
/* Append the (possibly parameterized) self type: "Box" or "Box(T, U)". */
static void db_selfty(DeriveBuf *sb, AstNode *d) {
    db_puts(sb, d->as.struct_decl.name);
    db_tparams(sb, d);
}
/* Open a synthesized impl: `methods[(T,U)] Name[(T,U)]: Trait {`.
   For a generic struct, emit the type-param list. No `where` clause — the
   `methods Type: Interface` grammar doesn't accept one; the per-field operations
   in the body (e.g. `self.f == rhs.f`) enforce the bound at monomorphization, so
   instantiating with a T that lacks the trait fails there with a clear error.
   (`bound` is retained for call-site readability but currently unused.) */
static void db_methods_open(DeriveBuf *sb, AstNode *d, const char *trait, bool bound) {
    (void)bound;
    /* New syntax: type params ride on the receiver — `methods Name(T): Trait`
       (db_selfty already emits `Name(T)`), not a leading `methods(T)` list. */
    db_puts(sb, "methods ");
    db_selfty(sb, d);
    db_puts(sb, ": ");
    db_puts(sb, trait);
    db_puts(sb, " {\n");
}

static void derive_emit_struct(Checker *c, DeriveBuf *sb, AstNode *d, AstNode *program) {
    const char *T = d->as.struct_decl.name;
    int fc = d->as.struct_decl.field_count;
    char **fname = d->as.struct_decl.field_names;

    /* A generic interface-impl folds into the struct's inherent `methods Name(T)`
       block, which must appear BEFORE it in decl order. expand_derives inserts
       these synthesized impls AFTER any user inherent block (look-ahead), so the
       fold anchor exists when the user wrote one. Only when the user wrote NO
       inherent block do we synthesize an empty one here (emitting one when the
       user has one would be a duplicate that breaks registration).
       (Non-generic impls have no such requirement.) */
    if (d->as.struct_decl.type_param_count > 0) {
        bool has_inherent = false;
        if (program != NULL && program->kind == AST_PROGRAM) {
            for (int di = 0; di < program->as.program.decl_count; di++) {
                AstNode *pd = program->as.program.decls[di];
                if (pd && pd->kind == AST_IMPL_DECL && pd->as.impl_decl.name &&
                    pd->as.impl_decl.type_param_count > 0 &&
                    strcmp(pd->as.impl_decl.name, T) == 0) { has_inherent = true; break; }
            }
        }
        if (!has_inherent) {
            db_puts(sb, "methods "); db_selfty(sb, d); db_puts(sb, " {\n}\n");
        }
    }

    for (int k = 0; k < d->as.struct_decl.derive_count; k++) {
        const char *tr = d->as.struct_decl.derives[k];
        /* All seven struct derives work on generics. Equal/Hash/Order lower to
           uniform operators / .hash(). Show/Serialize/Deserialize dispatch a
           type-parameter field T to `self.f.show()` / `self.f.to_value()` /
           `T.from_value(...)`; the monomorphized call resolves on the concrete T
           because std.core.{show,value,str} provide .show()/.to_value()/from_value()
           for every primitive + Str (the concrete-struct path still formats
           primitives inline and never calls those). The generic derive adds no
           `where T: Trait` bound — instantiating with a T that lacks the operation
           fails at monomorphization with a clear missing-method error. */
        if (strcmp(tr, "Equal") == 0) {
            db_methods_open(sb, d, "Equal", true);
            db_puts(sb, "  def ==(&self, &"); db_selfty(sb, d);
            db_puts(sb, " rhs) -> bool {\n    return ");
            if (fc == 0) {
                db_puts(sb, "true");
            } else {
                for (int i = 0; i < fc; i++) {
                    if (i) db_puts(sb, " && ");
                    db_puts(sb, "self."); db_puts(sb, fname[i]);
                    db_puts(sb, " == rhs."); db_puts(sb, fname[i]);
                }
            }
            db_puts(sb, "\n  }\n}\n");
        } else if (strcmp(tr, "Hash") == 0) {
            /* fold each field's .hash() through FxHash's fx_mix (canonical path
               so no import alias is needed; .hash() resolves via std.core.hash). */
            db_methods_open(sb, d, "Hash", true);
            db_puts(sb, "  def hash(&self) -> u64 {\n    u64 h = 0 as u64\n");
            for (int i = 0; i < fc; i++) {
                db_puts(sb, "    h = std.core.hash.fx_mix(h, self.");
                db_puts(sb, fname[i]); db_puts(sb, ".hash())\n");
            }
            db_puts(sb, "    return h\n  }\n}\n");
        } else if (strcmp(tr, "Order") == 0) {
            /* lexicographic: first differing field decides; `> <= >=` are
               auto-derived from `<` by the operator-overload machinery. */
            db_methods_open(sb, d, "Order", true);
            db_puts(sb, "  def <(&self, &"); db_selfty(sb, d);
            db_puts(sb, " rhs) -> bool {\n");
            for (int i = 0; i < fc; i++) {
                db_puts(sb, "    if self."); db_puts(sb, fname[i]);
                db_puts(sb, " != rhs."); db_puts(sb, fname[i]);
                db_puts(sb, " { return self."); db_puts(sb, fname[i]);
                db_puts(sb, " < rhs."); db_puts(sb, fname[i]); db_puts(sb, " }\n");
            }
            db_puts(sb, "    return false\n  }\n}\n");
        } else if (strcmp(tr, "Show") == 0) {
            /* def show(&self, &!Sink out) { out.write("T {"); out.write(" f: ");
               self.f.show(&!out); ...; out.write(" }") } — every field renders via
               its OWN Show impl (primitives/Str/nested all impl Show), writing into
               the shared sink with no intermediate Str. Output is byte-identical to
               the old `-> Str` form. Stage 3, docs/plan_print_sink.md. */
            db_methods_open(sb, d, "Show", true);
            db_puts(sb, "  def show(&self, &!Sink out) {\n    out.write(\"");
            db_puts(sb, T); db_puts(sb, " {\")\n");
            for (int i = 0; i < fc; i++) {
                db_puts(sb, "    out.write(\"");
                db_puts(sb, (i == 0) ? " " : ", ");
                db_puts(sb, fname[i]); db_puts(sb, ": \")\n");
                db_puts(sb, "    self."); db_puts(sb, fname[i]);
                db_puts(sb, ".show(&!out)\n");
            }
            db_puts(sb, (fc > 0) ? "    out.write(\" }\")\n"
                                 : "    out.write(\"}\")\n");
            db_puts(sb, "  }\n}\n");
        } else if (strcmp(tr, "Serialize") == 0) {
            /* def to_value(&self) -> Value { build parallel keys/vals vecs, then
               return VObj(__k, __v) } — a neutral value tree (std.core.value).
               Per-field via VInt/VFloat/VBool/VStr leaves or recursive .to_value(). */
            db_methods_open(sb, d, "Serialize", true);
            db_puts(sb, "  def to_value(&self) -> Value {\n");
            db_puts(sb, "    Vec(Str) __k = []\n    Vec(Value) __v = []\n");
            for (int i = 0; i < fc; i++) {
                db_puts(sb, "    __k.push(\""); db_puts(sb, fname[i]); db_puts(sb, "\")\n");
                db_puts(sb, "    __v.push(");
                derive_emit_serialize_field(sb, fname[i], d->as.struct_decl.field_types[i]);
                db_puts(sb, ")\n");
            }
            db_puts(sb, "    return VObj(__k, __v)\n  }\n}\n");
        } else if (strcmp(tr, "Deserialize") == 0) {
            /* static def from_value(Value v) -> T { return T { f: <extract>, ... } }
               — best-effort rebuild from the neutral tree. Nested/T fields recurse
               via <Type>.from_value (so they must derive Deserialize too). */
            db_methods_open(sb, d, "Deserialize", true);
            db_puts(sb, "  static def from_value(Value v) -> "); db_selfty(sb, d);
            db_puts(sb, " {\n    return "); db_selfty(sb, d); db_puts(sb, " {\n");
            for (int i = 0; i < fc; i++) {
                db_puts(sb, "      "); db_puts(sb, fname[i]); db_puts(sb, ": ");
                derive_emit_deserialize_field(sb, fname[i], d->as.struct_decl.field_types[i]);
                if (i + 1 < fc) db_puts(sb, ",");
                db_puts(sb, "\n");
            }
            db_puts(sb, "    }\n  }\n}\n");

            /* STRICT variant: static def try_from_value(Value v) -> Result(T, Str).
               Each field is extracted into a local with `try` (missing field / type
               mismatch -> the leaf's Err propagates out); the struct is then built
               from the validated locals. Locals (not inline `try` in the literal) so
               an early-return Err drops the already-extracted fields cleanly. */
            db_methods_open(sb, d, "TryDeserialize", true);
            db_puts(sb, "  static def try_from_value(Value v) -> Result(");
            db_selfty(sb, d); db_puts(sb, ", Str) {\n");
            for (int i = 0; i < fc; i++) {
                db_puts(sb, "    ");
                derive_emit_typename(sb, d->as.struct_decl.field_types[i]);
                db_puts(sb, " __d"); db_putint(sb, i); db_puts(sb, " = ");
                derive_emit_try_deserialize_field(sb, fname[i], d->as.struct_decl.field_types[i]);
                db_puts(sb, "\n");
            }
            db_puts(sb, "    return Ok("); db_selfty(sb, d); db_puts(sb, " {\n");
            for (int i = 0; i < fc; i++) {
                db_puts(sb, "      "); db_puts(sb, fname[i]); db_puts(sb, ": __d"); db_putint(sb, i);
                if (i + 1 < fc) db_puts(sb, ",");
                db_puts(sb, "\n");
            }
            db_puts(sb, "    })\n  }\n}\n");
        } else if (strcmp(tr, "Reflect") == 0) {
            /* static def reflect() -> TypeInfo { build FieldInfo + MethodInfo
               vectors }. Fields come from the struct decl; methods are found by
               scanning `program` for impl blocks targeting this struct (their
               AST is available here even though impl_registry isn't yet). */
            db_methods_open(sb, d, "Reflect", false);
            db_puts(sb, "  static def reflect() -> TypeInfo {\n");
            db_puts(sb, "    Vec(FieldInfo) __f = []\n");
            for (int i = 0; i < fc; i++) {
                TypeNode *ft = d->as.struct_decl.field_types[i];
                /* A bare type-parameter field (`T value`) reflects its CONCRETE
                   instantiated type via __type_name(T) — resolved per
                   monomorphization (Box(int).reflect() -> "int") — instead of the
                   literal parameter name "T". Concrete fields keep a literal name. */
                bool is_tparam = false;
                if (ft != NULL && ft->kind == TYPE_NODE_NAMED &&
                    ft->as.named.arg_count == 0 && ft->as.named.name != NULL) {
                    for (int tp = 0; tp < d->as.struct_decl.type_param_count; tp++) {
                        if (d->as.struct_decl.type_params[tp] != NULL &&
                            strcmp(ft->as.named.name,
                                   d->as.struct_decl.type_params[tp]) == 0) {
                            is_tparam = true; break;
                        }
                    }
                }
                db_puts(sb, "    __f.push(FieldInfo { name: \"");
                db_puts(sb, fname[i]); db_puts(sb, "\", type_name: ");
                if (is_tparam) {
                    db_puts(sb, "__type_name("); db_puts(sb, ft->as.named.name);
                    db_puts(sb, ")");
                } else {
                    db_puts(sb, "\""); derive_emit_typename(sb, ft); db_puts(sb, "\"");
                }
                db_puts(sb, " })\n");
            }
            db_puts(sb, "    Vec(MethodInfo) __m = []\n");
            if (program != NULL && program->kind == AST_PROGRAM) {
                for (int di = 0; di < program->as.program.decl_count; di++) {
                    AstNode *pd = program->as.program.decls[di];
                    if (pd == NULL) continue;
                    AstNode **methods = NULL; int mcount = 0;
                    if (pd->kind == AST_IMPL_DECL && pd->as.impl_decl.name &&
                        strcmp(pd->as.impl_decl.name, T) == 0) {
                        methods = pd->as.impl_decl.methods;
                        mcount = pd->as.impl_decl.method_count;
                    } else if (pd->kind == AST_IMPL_TRAIT_DECL &&
                               pd->as.impl_trait_decl.struct_name &&
                               strcmp(pd->as.impl_trait_decl.struct_name, T) == 0) {
                        methods = pd->as.impl_trait_decl.methods;
                        mcount = pd->as.impl_trait_decl.method_count;
                    }
                    for (int mi = 0; mi < mcount; mi++) {
                        AstNode *fn = methods[mi];
                        if (fn == NULL || fn->kind != AST_FN_DECL) continue;
                        db_puts(sb, "    __m.push(MethodInfo { name: \"");
                        db_puts(sb, method_display_name(fn->as.fn_decl.name));
                        db_puts(sb, "\", signature: \"");
                        derive_emit_method_sig(sb, fn);
                        db_puts(sb, "\", is_static: ");
                        db_puts(sb, fn->as.fn_decl.is_static ? "true" : "false");
                        db_puts(sb, " })\n");
                    }
                }
            }
            db_puts(sb, "    return TypeInfo { name: \""); db_puts(sb, T);
            db_puts(sb, "\", fields: __f, funcs: __m }\n  }\n}\n");
        } else if (strcmp(tr, "ReflectRaw") == 0) {
            /* The substrate counterpart of Reflect for foundational types (Str/Vec)
               that sit below the str/vec layer and so cannot import std.core.reflect.
               Emits `static def reflect_raw() -> RawType` building raw (*u8) metadata
               via std.core.reflect_core; std.core.reflect.from_raw bridges it to a
               friendly TypeInfo. Field/method scanning mirrors the Reflect branch;
               only the OUTPUT shape differs (RawType.make/set_field/set_method +
               __rawstr literals instead of Vec<FieldInfo>). RawType.make needs the
               method count up front, so methods are scanned twice (count, then emit). */
            char numbuf[32];
            int nm = 0;
            if (program != NULL && program->kind == AST_PROGRAM) {
                for (int di = 0; di < program->as.program.decl_count; di++) {
                    AstNode *pd = program->as.program.decls[di];
                    if (pd == NULL) continue;
                    AstNode **methods = NULL; int mcount = 0;
                    if (pd->kind == AST_IMPL_DECL && pd->as.impl_decl.name &&
                        strcmp(pd->as.impl_decl.name, T) == 0) {
                        methods = pd->as.impl_decl.methods; mcount = pd->as.impl_decl.method_count;
                    } else if (pd->kind == AST_IMPL_TRAIT_DECL && pd->as.impl_trait_decl.struct_name &&
                               strcmp(pd->as.impl_trait_decl.struct_name, T) == 0) {
                        methods = pd->as.impl_trait_decl.methods; mcount = pd->as.impl_trait_decl.method_count;
                    }
                    for (int mi = 0; mi < mcount; mi++)
                        if (methods[mi] && methods[mi]->kind == AST_FN_DECL) nm++;
                }
            }
            db_methods_open(sb, d, "ReflectRaw", false);
            db_puts(sb, "  static def reflect_raw() -> RawType {\n");
            db_puts(sb, "    RawType __rt = RawType.make(__rawstr(\"");
            db_puts(sb, T); db_puts(sb, "\"), ");
            snprintf(numbuf, sizeof(numbuf), "%d, %d)\n", fc, nm);
            db_puts(sb, numbuf);
            for (int i = 0; i < fc; i++) {
                db_puts(sb, "    __rt.set_field(");
                snprintf(numbuf, sizeof(numbuf), "%d", i); db_puts(sb, numbuf);
                db_puts(sb, ", __rawstr(\""); db_puts(sb, fname[i]);
                db_puts(sb, "\"), __rawstr(\"");
                derive_emit_typename(sb, d->as.struct_decl.field_types[i]);
                db_puts(sb, "\"))\n");
            }
            int midx = 0;
            if (program != NULL && program->kind == AST_PROGRAM) {
                for (int di = 0; di < program->as.program.decl_count; di++) {
                    AstNode *pd = program->as.program.decls[di];
                    if (pd == NULL) continue;
                    AstNode **methods = NULL; int mcount = 0;
                    if (pd->kind == AST_IMPL_DECL && pd->as.impl_decl.name &&
                        strcmp(pd->as.impl_decl.name, T) == 0) {
                        methods = pd->as.impl_decl.methods; mcount = pd->as.impl_decl.method_count;
                    } else if (pd->kind == AST_IMPL_TRAIT_DECL && pd->as.impl_trait_decl.struct_name &&
                               strcmp(pd->as.impl_trait_decl.struct_name, T) == 0) {
                        methods = pd->as.impl_trait_decl.methods; mcount = pd->as.impl_trait_decl.method_count;
                    }
                    for (int mi = 0; mi < mcount; mi++) {
                        AstNode *fn = methods[mi];
                        if (fn == NULL || fn->kind != AST_FN_DECL) continue;
                        db_puts(sb, "    __rt.set_method(");
                        snprintf(numbuf, sizeof(numbuf), "%d", midx++); db_puts(sb, numbuf);
                        db_puts(sb, ", __rawstr(\"");
                        db_puts(sb, method_display_name(fn->as.fn_decl.name));
                        db_puts(sb, "\"), __rawstr(\"");
                        derive_emit_method_sig(sb, fn);
                        db_puts(sb, "\"), ");
                        db_puts(sb, fn->as.fn_decl.is_static ? "true" : "false");
                        db_puts(sb, ")\n");
                    }
                }
            }
            db_puts(sb, "    return __rt\n  }\n}\n");
        } else if (strcmp(tr, "Clone") == 0) {
            checker_error(c, d->line, d->column,
                "@derive(Clone) is unnecessary: has_drop structs are deep-copied "
                "automatically at clone points, and LS has no user-callable "
                ".clone() (the Clone interface is the compiler's internal hook)");
        } else {
            checker_error(c, d->line, d->column,
                "@derive(%s) is not supported (struct derives: Equal, Hash, "
                "Order, Show, Serialize, Deserialize, Reflect)", tr);
        }
    }
}

/* Emit the synthesized impl(s) for one enum's derives into sb. Equal/Hash use
   nested match over variants; Order on enums is deferred. */
static void derive_emit_enum(Checker *c, DeriveBuf *sb, AstNode *d) {
    const char *T = d->as.enum_decl.name;
    int vc = d->as.enum_decl.variant_count;
    for (int k = 0; k < d->as.enum_decl.derive_count; k++) {
        const char *tr = d->as.enum_decl.derives[k];
        if (strcmp(tr, "Equal") == 0) {
            db_puts(sb, "methods "); db_puts(sb, T);
            db_puts(sb, ": Equal {\n  def ==(&self, &"); db_puts(sb, T);
            db_puts(sb, " rhs) -> bool {\n    match self {\n");
            for (int v = 0; v < vc; v++) {
                const char *vn = d->as.enum_decl.variants[v].name;
                int pc = d->as.enum_decl.variants[v].payload_count;
                db_puts(sb, "      "); db_puts(sb, vn);
                db_binders(sb, "x", pc);
                db_puts(sb, " => match rhs { "); db_puts(sb, vn);
                db_binders(sb, "y", pc);
                db_puts(sb, " => ");
                if (pc == 0) {
                    db_puts(sb, "true");
                } else {
                    for (int p = 0; p < pc; p++) {
                        if (p) db_puts(sb, " && ");
                        db_puts(sb, "x"); db_putint(sb, p);
                        db_puts(sb, " == y"); db_putint(sb, p);
                    }
                }
                db_puts(sb, " _ => false }\n");
            }
            db_puts(sb, "    }\n  }\n}\n");
        } else if (strcmp(tr, "Hash") == 0) {
            db_puts(sb, "methods "); db_puts(sb, T);
            db_puts(sb, ": Hash {\n  def hash(&self) -> u64 {\n    u64 h = 0 as u64\n");
            db_puts(sb, "    match self {\n");
            for (int v = 0; v < vc; v++) {
                const char *vn = d->as.enum_decl.variants[v].name;
                int pc = d->as.enum_decl.variants[v].payload_count;
                db_puts(sb, "      "); db_puts(sb, vn);
                db_binders(sb, "x", pc);
                db_puts(sb, " => { h = std.core.hash.fx_mix(h, ");
                db_putint(sb, v + 1); db_puts(sb, " as u64)");
                for (int p = 0; p < pc; p++) {
                    db_puts(sb, " h = std.core.hash.fx_mix(h, x");
                    db_putint(sb, p); db_puts(sb, ".hash())");
                }
                db_puts(sb, " }\n");
            }
            db_puts(sb, "    }\n    return h\n  }\n}\n");
        } else if (strcmp(tr, "Show") == 0) {
            /* def show(&self, &!Sink out) { match self {
                 V        => { out.write("V") }
                 V(x0,..) => { out.write("V("); x0.show(&!out); out.write(", "); ..
                               out.write(")") } } } — variant name, then each payload
               via its own Show impl into the shared sink. Byte-identical to the old
               `-> Str` form. Stage 3, docs/plan_print_sink.md. */
            db_puts(sb, "methods "); db_puts(sb, T);
            db_puts(sb, ": Show {\n  def show(&self, &!Sink out) {\n    match self {\n");
            for (int v = 0; v < vc; v++) {
                const char *vn = d->as.enum_decl.variants[v].name;
                int pc = d->as.enum_decl.variants[v].payload_count;
                db_puts(sb, "      "); db_puts(sb, vn);
                db_binders(sb, "x", pc);
                if (pc == 0) {
                    db_puts(sb, " => { out.write(\""); db_puts(sb, vn);
                    db_puts(sb, "\") }\n");
                } else {
                    db_puts(sb, " => { out.write(\""); db_puts(sb, vn);
                    db_puts(sb, "(\")\n");
                    for (int p = 0; p < pc; p++) {
                        if (p) db_puts(sb, "        out.write(\", \")\n");
                        db_puts(sb, "        x"); db_putint(sb, p);
                        db_puts(sb, ".show(&!out)\n");
                    }
                    db_puts(sb, "        out.write(\")\")\n      }\n");
                }
            }
            db_puts(sb, "    }\n  }\n}\n");
        } else if (strcmp(tr, "Order") == 0) {
            /* def <(&self, &E rhs) -> bool — lexicographic: compare variant
               declaration order first (via an index match on each side), and only
               when equal compare payloads field-by-field. `> <= >=` derive from
               `<` / `==`. */
            db_puts(sb, "methods "); db_puts(sb, T);
            db_puts(sb, ": Order {\n  def <(&self, &"); db_puts(sb, T);
            db_puts(sb, " rhs) -> bool {\n");
            db_puts(sb, "    int __si = match self {\n");
            for (int v = 0; v < vc; v++) {
                const char *vn = d->as.enum_decl.variants[v].name;
                int pc = d->as.enum_decl.variants[v].payload_count;
                db_puts(sb, "      "); db_puts(sb, vn); db_binders(sb, "x", pc);
                db_puts(sb, " => "); db_putint(sb, v); db_puts(sb, "\n");
            }
            db_puts(sb, "    }\n    int __ri = match rhs {\n");
            for (int v = 0; v < vc; v++) {
                const char *vn = d->as.enum_decl.variants[v].name;
                int pc = d->as.enum_decl.variants[v].payload_count;
                db_puts(sb, "      "); db_puts(sb, vn); db_binders(sb, "y", pc);
                db_puts(sb, " => "); db_putint(sb, v); db_puts(sb, "\n");
            }
            db_puts(sb, "    }\n    if __si != __ri { return __si < __ri }\n");
            db_puts(sb, "    match self {\n");
            for (int v = 0; v < vc; v++) {
                const char *vn = d->as.enum_decl.variants[v].name;
                int pc = d->as.enum_decl.variants[v].payload_count;
                db_puts(sb, "      "); db_puts(sb, vn); db_binders(sb, "x", pc);
                db_puts(sb, " => match rhs { "); db_puts(sb, vn);
                db_binders(sb, "y", pc); db_puts(sb, " => {\n");
                for (int p = 0; p < pc; p++) {
                    db_puts(sb, "        if x"); db_putint(sb, p);
                    db_puts(sb, " != y"); db_putint(sb, p);
                    db_puts(sb, " { return x"); db_putint(sb, p);
                    db_puts(sb, " < y"); db_putint(sb, p); db_puts(sb, " }\n");
                }
                db_puts(sb, "        return false\n      } _ => { return false } }\n");
            }
            db_puts(sb, "    }\n    return false\n  }\n}\n");
        } else {
            checker_error(c, d->line, d->column,
                "@derive(%s) on enums is not supported yet (enum derives Equal, "
                "Hash, Order, Show)", tr);
        }
    }
}

/* Parse a one-line `import ...` source and append its decl(s) to out[]. */
static void push_import_src(AstNode ***out, int *nc, int *ncap, int oldn,
                            const char *src) {
    AstNode *imp = parse(src, "<derive>");
    if (imp == NULL || imp->kind != AST_PROGRAM) return;
    for (int j = 0; j < imp->as.program.decl_count; j++) {
        if (*nc >= *ncap) { *ncap = *ncap ? *ncap * 2 : (oldn + 8);
            *out = realloc_safe(*out, (size_t)(*ncap) * sizeof(AstNode *)); }
        (*out)[(*nc)++] = imp->as.program.decls[j];
    }
    imp->as.program.decl_count = 0;
    ast_free(imp);
}

static void expand_derives(Checker *c, AstNode *program) {
    if (program == NULL || program->kind != AST_PROGRAM) return;
    bool any = false, need_hash = false, need_show = false, need_value = false;
    bool need_reflect = false;
    bool has_hash_import = false, has_show_import = false;
    bool has_value_import = false, has_str_import = false, has_reflect_import = false;
    bool has_sink_import = false;   /* @derive(Show) synthesizes show(&!Sink) */
    int oldn = program->as.program.decl_count;
    for (int i = 0; i < oldn; i++) {
        AstNode *d = program->as.program.decls[i];
        if (d == NULL) continue;
        if (d->kind == AST_STRUCT_DECL && d->as.struct_decl.derive_count > 0) {
            any = true;
            for (int k = 0; k < d->as.struct_decl.derive_count; k++) {
                if (strcmp(d->as.struct_decl.derives[k], "Hash") == 0) need_hash = true;
                if (strcmp(d->as.struct_decl.derives[k], "Show") == 0) need_show = true;
                if (strcmp(d->as.struct_decl.derives[k], "Serialize") == 0) need_value = true;
                if (strcmp(d->as.struct_decl.derives[k], "Deserialize") == 0) need_value = true;
                if (strcmp(d->as.struct_decl.derives[k], "Reflect") == 0) need_reflect = true;
            }
        } else if (d->kind == AST_ENUM_DECL && d->as.enum_decl.derive_count > 0) {
            any = true;
            for (int k = 0; k < d->as.enum_decl.derive_count; k++) {
                if (strcmp(d->as.enum_decl.derives[k], "Hash") == 0) need_hash = true;
                if (strcmp(d->as.enum_decl.derives[k], "Show") == 0) need_show = true;
            }
        } else if (d->kind == AST_IMPORT_DECL && d->as.import_decl.path) {
            if (strcmp(d->as.import_decl.path, "std.core.hash") == 0)    has_hash_import = true;
            if (strcmp(d->as.import_decl.path, "std.core.show") == 0)    has_show_import = true;
            if (strcmp(d->as.import_decl.path, "std.core.value") == 0)   has_value_import = true;
            if (strcmp(d->as.import_decl.path, "std.core.reflect") == 0) has_reflect_import = true;
            if (strcmp(d->as.import_decl.path, "std.core.str") == 0)     has_str_import = true;
            if (strcmp(d->as.import_decl.path, "std.core.sink") == 0)    has_sink_import = true;
        }
    }
    if (!any) return;

    /* Rebuild the decl list, inserting each struct's synthesized impl(s)
       immediately after that struct — so trait-impl satisfaction is recorded
       (in check_pass, which is decl-order) before any later use. This mirrors
       the conventional hand-written layout: struct, then impl, then code. */
    AstNode **out = NULL; int nc = 0, ncap = 0;

    /* @derive(Hash) needs std.core.hash (no str dependency → front-inject is fine).
       @derive(Show) needs std.core.show, which imports std.core.str → it MUST come
       AFTER the prelude `import std.core.str` (injecting it before re-imports str
       and clobbers Str for later modules). Inject show right after the str import
       in the copy loop below; only front-inject as a fallback when there is no
       str prelude (str-default off), where there is nothing to clobber. */
    if (need_hash && !has_hash_import)
        push_import_src(&out, &nc, &ncap, oldn, "import std.core.hash\n");

    /* show.ls and value.ls both import std.core.str, so their injected imports
       MUST land after the prelude str import (front would re-import str first and
       clobber Str for later modules). Collect them; inject after str below. */
    const char *after_str[4]; int after_str_n = 0;
    /* @derive(Show) synthesizes `def show(&self, &!Sink out)` — the Sink type
       must be in scope. sink.ls imports str, so it also lands after the str
       prelude. */
    if (need_show && !has_sink_import)       after_str[after_str_n++] = "import std.core.sink\n";
    if (need_show && !has_show_import)       after_str[after_str_n++] = "import std.core.show\n";
    if (need_value && !has_value_import)     after_str[after_str_n++] = "import std.core.value\n";
    if (need_reflect && !has_reflect_import) after_str[after_str_n++] = "import std.core.reflect\n";
    bool after_str_injected = false;
    if (after_str_n > 0 && !has_str_import) {   /* no str prelude → front is safe */
        for (int k = 0; k < after_str_n; k++)
            push_import_src(&out, &nc, &ncap, oldn, after_str[k]);
        after_str_injected = true;
    }

    for (int i = 0; i < oldn; i++) {
        AstNode *d = program->as.program.decls[i];
        if (nc >= ncap) { ncap = ncap ? ncap * 2 : (oldn + 8);
            out = realloc_safe(out, (size_t)ncap * sizeof(AstNode *)); }
        out[nc++] = d;
        if (after_str_n > 0 && !after_str_injected && d &&
            d->kind == AST_IMPORT_DECL && d->as.import_decl.path &&
            strcmp(d->as.import_decl.path, "std.core.str") == 0) {
            for (int k = 0; k < after_str_n; k++)
                push_import_src(&out, &nc, &ncap, oldn, after_str[k]);
            after_str_injected = true;
        }
        bool is_struct_der = d && d->kind == AST_STRUCT_DECL &&
                             d->as.struct_decl.derive_count > 0;
        bool is_enum_der   = d && d->kind == AST_ENUM_DECL &&
                             d->as.enum_decl.derive_count > 0;
        if (is_struct_der || is_enum_der) {
            /* Generic structs: derived interface-impls must follow the struct's
               inherent methods Name(T) block (the fold anchor). Push any impl blocks
               for this struct first (advancing i), so the synth lands after them. */
            if (is_struct_der && d->as.struct_decl.type_param_count > 0) {
                const char *sname = d->as.struct_decl.name;
                while (i + 1 < oldn) {
                    AstNode *nx = program->as.program.decls[i + 1];
                    bool is_impl_for = nx &&
                        ((nx->kind == AST_IMPL_DECL && nx->as.impl_decl.name &&
                          strcmp(nx->as.impl_decl.name, sname) == 0) ||
                         (nx->kind == AST_IMPL_TRAIT_DECL && nx->as.impl_trait_decl.struct_name &&
                          strcmp(nx->as.impl_trait_decl.struct_name, sname) == 0));
                    if (!is_impl_for) break;
                    if (nc >= ncap) { ncap *= 2;
                        out = realloc_safe(out, (size_t)ncap * sizeof(AstNode *)); }
                    out[nc++] = nx;
                    i++;
                }
            }
            DeriveBuf sb; db_init(&sb);
            if (is_struct_der) derive_emit_struct(c, &sb, d, program);
            else               derive_emit_enum(c, &sb, d);
            if (sb.len > 0) {
                AstNode *synth = parse(sb.data, "<derive>");
                if (synth != NULL && synth->kind == AST_PROGRAM) {
                    for (int j = 0; j < synth->as.program.decl_count; j++) {
                        if (nc >= ncap) { ncap *= 2;
                            out = realloc_safe(out, (size_t)ncap * sizeof(AstNode *)); }
                        out[nc++] = synth->as.program.decls[j];
                    }
                    synth->as.program.decl_count = 0;  /* detach transferred nodes */
                    ast_free(synth);
                }
            }
            free(sb.data);
            if (is_struct_der) d->as.struct_decl.derive_count = 0;  /* prevent re-expansion */
            else               d->as.enum_decl.derive_count = 0;
        }
    }
    free(program->as.program.decls);
    program->as.program.decls = out;
    program->as.program.decl_count = nc;
}

int checker_inspect_ex(AstNode *program, const char *source_path,
                       struct ModuleRegistry *registry, const char *type_query,
                       bool want_fields, bool want_methods)
{
    if (program == NULL || program->kind != AST_PROGRAM) return 1;

    Checker c;
    memset(&c, 0, sizeof(Checker));
    c.source_path = source_path;
    c.registry = registry;
    c.module_name = registry ? registry->current_check_module : NULL;
    c.elide_pass_enabled = checker_elide_env_enabled();
    c.current_scope = scope_new(NULL);

    register_builtins(&c);
    register_builtin_operator_traits(&c);
    expand_derives(&c, program);
    forward_pass(&c, program);
    check_pass(&c, program);
    checker_propagate_has_drop_fixpoint(&c);

    int rc;
    if (c.had_error) {
        fprintf(stderr, "inspect: file has type errors; cannot inspect '%s'.\n",
                type_query);
        rc = 1;
    } else {
        rc = dump_type_info(&c, type_query, want_fields, want_methods);
    }
    checker_teardown(&c, NULL);
    return rc;
}

int checker_inspect(AstNode *program, const char *source_path,
                    struct ModuleRegistry *registry, const char *type_query)
{
    return checker_inspect_ex(program, source_path, registry, type_query,
                              true, true);
}

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
    c.elide_pass_enabled = checker_elide_env_enabled();
    c.current_scope = scope_new(NULL);

    register_builtins(&c);
    register_builtin_operator_traits(&c);
    expand_derives(&c, program);
    forward_pass(&c, program);
    check_pass(&c, program);
    /* B-MAP-M5-004: settle has_drop across recursive-via-container types so that
       e.g. Option(JsonValue) is correctly has_drop and gets a __drop emitted. */
    checker_propagate_has_drop_fixpoint(&c);

    checker_teardown(&c, out_gm);

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
