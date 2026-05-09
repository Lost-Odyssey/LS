/* checker.c — Type checker: walks AST, validates types, fills resolved_type */
#include "checker.h"
#include "module.h"
#include "builtins_math.h"
#include <stdio.h>
#include <string.h>

/* ---- Stdlib gate ----
   Internal builtins (named with `__` prefix by convention) are only callable
   from files physically located under a `stdlib/` directory — typically
   <LS_HOME>/stdlib/. Detected by looking for a "/stdlib/" or "\stdlib\"
   segment in the source path. Imperfect (a user could name their own
   directory "stdlib"), but good enough to keep these footguns out of normal
   user code while staying allocator-policy-free. */
static bool path_is_under_stdlib(const char *path)
{
    if (path == NULL) return false;
    for (const char *p = path; *p; p++) {
        if ((p[0] == '/' || p[0] == '\\') &&
            p[1] == 's' && p[2] == 't' && p[3] == 'd' &&
            p[4] == 'l' && p[5] == 'i' && p[6] == 'b' &&
            (p[7] == '/' || p[7] == '\\'))
        {
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
    for (int i = 0; i < c->type_alias_count; i++)
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
static bool type_owns_heap_for_enum(const Type *t);

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
            if (strcmp(et->as.enom.variants[v].name, vname) == 0)
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
            if (strcmp(et->as.enom.variants[v].name, vname) == 0)
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
        Type *got = check_expr(c, args[i]);
        Type *want = enum_type->as.enom.variants[variant_idx].payload_types[i];
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

static void register_method(Checker *c, int impl_idx, const char *name,
                            Type *type, bool is_static, int self_borrow_kind)
{
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
        /* Phase 5/5.5/5.6/5.7/5.8: supported borrow pointees are
           string / vec / map / struct(POD).
           Other kinds TBD. */
        bool ok_kind = (pointee->kind == TYPE_STRING ||
                        pointee->kind == TYPE_VECTOR ||
                        pointee->kind == TYPE_MAP ||
                        pointee->kind == TYPE_STRUCT);
        if (!ok_kind)
        {
            checker_error(c, line, col,
                          "&%s%s is not supported yet; only &string / &!string / "
                          "&vec(T) / &!vec(T) / &map(K,V) / &!map(K,V) / "
                          "&struct / &!struct are implemented",
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
    case TYPE_NODE_VECTOR:
        return type_vector(resolve_type_node(c, tn->as.vec.elem, line, col));
    case TYPE_NODE_MAP:
    {
        Type *kt = resolve_type_node(c, tn->as.map.key, line, col);
        Type *vt = resolve_type_node(c, tn->as.map.val, line, col);
        if (kt == NULL || vt == NULL)
            return NULL;
        if (!type_is_integer(kt) && kt->kind != TYPE_STRING)
        {
            checker_error(c, line, col,
                          "map key type must be int or string, got '%s'", type_name(kt));
            return NULL;
        }
        return type_map(kt, vt);
    }
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
        /* Plain named type: try alias, then struct, then enum (for recursive
           enum payloads like Tree where the variant payload references the
           enum being defined, or Color used as a non-instantiated enum). */
        if (tn->as.named.arg_count == 0)
        {
            Type *al = find_type_alias(c, tn->as.named.name);
            if (al) return al;
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

        Type *et = find_enum_type(c, buf);
        if (et) return et;

        /* Try template instantiation (Option/Result, etc.). */
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
    case TYPE_VECTOR: return true;
    case TYPE_MAP:    return true;
    case TYPE_STRUCT: return t->as.strukt.has_drop;
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

/* Phase 5.7: reject mutating map methods on read-only borrow (&map(K,V)). */
static bool map_method_is_mutating(const char *m)
{
    return strcmp(m, "set") == 0 || strcmp(m, "remove") == 0 ||
           strcmp(m, "clear") == 0;
}

static bool checker_reject_map_mut_on_readonly_borrow(Checker *c, AstNode *call_node,
                                                     const char *method)
{
    if (!map_method_is_mutating(method)) return false;
    AstNode *recv = call_node->as.call.callee->as.field_access.object;
    if (!recv || recv->kind != AST_IDENT) return false;
    Symbol *sym = scope_resolve(c->current_scope, recv->as.ident.name);
    if (!sym || !sym->is_borrow) return false;
    checker_move_error(c, recv->line, recv->column,
                       "cannot call map.%s() on '%s': it is a read-only borrow",
                       method, recv->as.ident.name);
    return true;
}

/* Phase 5.7: reject copy-out of a map borrow (both &map and &!map).
   Same rationale as vec — copying the bucket array pointer would cause
   double-free on scope exit. */
static bool checker_reject_map_borrow_copy_source(Checker *c, AstNode *src, const char *what)
{
    if (!src || src->kind != AST_IDENT) return false;
    Symbol *sym = scope_resolve(c->current_scope, src->as.ident.name);
    if (!sym) return false;
    if (!sym->type || sym->type->kind != TYPE_MAP) return false;
    if (!sym->is_borrow && !sym->is_mut_borrow) return false;
    checker_move_error(c, src->line, src->column,
                       "cannot %s: '%s' is a %sborrow of map — data cannot be copied out",
                       what, src->as.ident.name,
                       sym->is_mut_borrow ? "writable " : "read-only ");
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

/* Phase 5.6: reject copy-out of a vec borrow (both &vec and &!vec).
   Unlike string (whose by-value ABI lets `string t = s` produce a harmless
   cap==0 alias), vec copy-out would create two independent vecs sharing the
   same data buffer — free-on-scope would be a double-free. */
static bool checker_reject_vec_borrow_copy_source(Checker *c, AstNode *src, const char *what)
{
    if (!src || src->kind != AST_IDENT) return false;
    Symbol *sym = scope_resolve(c->current_scope, src->as.ident.name);
    if (!sym) return false;
    if (!sym->type || sym->type->kind != TYPE_VECTOR) return false;
    if (!sym->is_borrow && !sym->is_mut_borrow) return false;
    checker_move_error(c, src->line, src->column,
                       "cannot %s: '%s' is a %sborrow of vec — data cannot be copied out",
                       what, src->as.ident.name,
                       sym->is_mut_borrow ? "writable " : "read-only ");
    return true;
}

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
    if (strcmp(method, "at") == 0)
    {
        if (argc != 1)
        {
            checker_error(c, call_node->line, call_node->column,
                          "string.at() takes 1 argument, got %d", argc);
            return NULL;
        }
        Type *arg = check_expr(c, call_node->as.call.args[0]);
        if (arg && !type_is_integer(arg))
        {
            checker_error(c, call_node->as.call.args[0]->line,
                          call_node->as.call.args[0]->column,
                          "string.at() index must be integer, got '%s'", type_name(arg));
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
    if (strcmp(method, "split") == 0)
    {
        if (argc != 1)
        {
            checker_error(c, call_node->line, call_node->column,
                          "string.split() takes 1 argument, got %d", argc);
            return NULL;
        }
        Type *arg = check_expr(c, call_node->as.call.args[0]);
        if (arg && arg->kind != TYPE_STRING)
        {
            checker_error(c, call_node->as.call.args[0]->line,
                          call_node->as.call.args[0]->column,
                          "string.split() separator must be string, got '%s'",
                          type_name(arg));
            return NULL;
        }
        return type_vector(type_string());
    }

    /* sep.join(vec(string)) -> string */
    if (strcmp(method, "join") == 0)
    {
        if (argc != 1)
        {
            checker_error(c, call_node->line, call_node->column,
                          "string.join() takes 1 argument, got %d", argc);
            return NULL;
        }
        Type *arg = check_expr(c, call_node->as.call.args[0]);
        if (arg && (arg->kind != TYPE_VECTOR || arg->as.vec.elem->kind != TYPE_STRING))
        {
            checker_error(c, call_node->as.call.args[0]->line,
                          call_node->as.call.args[0]->column,
                          "string.join() argument must be vec(string), got '%s'",
                          type_name(arg));
            return NULL;
        }
        return type_string();
    }

    checker_error(c, call_node->line, call_node->column,
                  "string has no method '%s'", method);
    return NULL;
}

/* Phase 5.6: mutating vec methods are forbidden on a read-only borrow (&vec(T)).
   Writable borrows (&!vec(T)) are fine — mutations propagate through the pointer. */
static bool vec_method_is_mutating(const char *m)
{
    return strcmp(m, "push") == 0 || strcmp(m, "pop") == 0 ||
           strcmp(m, "clear") == 0 || strcmp(m, "reserve") == 0 ||
           strcmp(m, "remove") == 0 || strcmp(m, "truncate") == 0 ||
           strcmp(m, "swap") == 0 || strcmp(m, "reverse") == 0 ||
           strcmp(m, "extend") == 0 || strcmp(m, "insert") == 0 ||
           strcmp(m, "resize") == 0 || strcmp(m, "sort") == 0 ||
           strcmp(m, "sort_by") == 0 || strcmp(m, "shrink_to_fit") == 0 ||
           strcmp(m, "set") == 0;
}

static bool checker_reject_vec_mut_on_readonly_borrow(Checker *c, AstNode *call_node,
                                                     const char *method)
{
    if (!vec_method_is_mutating(method)) return false;
    AstNode *recv = call_node->as.call.callee->as.field_access.object;
    if (!recv || recv->kind != AST_IDENT) return false;
    Symbol *sym = scope_resolve(c->current_scope, recv->as.ident.name);
    if (!sym || !sym->is_borrow) return false;
    checker_move_error(c, recv->line, recv->column,
                       "cannot call vec.%s() on '%s': it is a read-only borrow",
                       method, recv->as.ident.name);
    return true;
}

/* Type-check method calls on vec(T) objects */
static Type *check_vector_method(Checker *c, AstNode *call_node, Type *vec_type)
{
    const char *method = call_node->as.call.callee->as.field_access.field;
    int argc = call_node->as.call.arg_count;
    Type *elem = vec_type->as.vec.elem;

    /* Phase 5.6: gate mutating methods on read-only borrow receivers. */
    if (checker_reject_vec_mut_on_readonly_borrow(c, call_node, method))
        return NULL;

    /* v.push(x) -> void  — append one element */
    if (strcmp(method, "push") == 0)
    {
        if (argc != 1)
        {
            checker_error(c, call_node->line, call_node->column,
                          "vec.push() takes 1 argument, got %d", argc);
            return NULL;
        }
        Type *arg = check_expr(c, call_node->as.call.args[0]);
        if (arg && !type_equals(arg, elem))
        {
            checker_error(c, call_node->as.call.args[0]->line,
                          call_node->as.call.args[0]->column,
                          "vec.push() expects '%s', got '%s'",
                          type_name(elem), type_name(arg));
            return NULL;
        }
        /* Move tracking: dynamic string (and future movable) arguments are moved into the vec.
           Static strings (is_static_string==true) are shared freely — not moved. */
        checker_reject_borrow_move(c, call_node->as.call.args[0], "move into vec");
        checker_try_mark_moved(c, call_node->as.call.args[0]);
        return type_void();
    }

    /* v.pop() -> void  — remove last element (drop if needed) */
    if (strcmp(method, "pop") == 0)
    {
        if (argc != 0)
        {
            checker_error(c, call_node->line, call_node->column,
                          "vec.pop() takes no arguments, got %d", argc);
            return NULL;
        }
        return type_void();
    }

    /* v.clear() -> void  — drop all elements, set len=0 */
    if (strcmp(method, "clear") == 0)
    {
        if (argc != 0)
        {
            checker_error(c, call_node->line, call_node->column,
                          "vec.clear() takes no arguments, got %d", argc);
            return NULL;
        }
        return type_void();
    }

    /* v.reserve(n) -> void  — ensure capacity >= n */
    if (strcmp(method, "reserve") == 0)
    {
        if (argc != 1)
        {
            checker_error(c, call_node->line, call_node->column,
                          "vec.reserve() takes 1 argument, got %d", argc);
            return NULL;
        }
        Type *arg = check_expr(c, call_node->as.call.args[0]);
        if (arg && !type_is_integer(arg))
        {
            checker_error(c, call_node->as.call.args[0]->line,
                          call_node->as.call.args[0]->column,
                          "vec.reserve() expects integer, got '%s'", type_name(arg));
            return NULL;
        }
        return type_void();
    }

    /* v.is_empty() -> bool  — true when len == 0 */
    if (strcmp(method, "is_empty") == 0)
    {
        if (argc != 0)
        {
            checker_error(c, call_node->line, call_node->column,
                          "vec.is_empty() takes no arguments, got %d", argc);
            return NULL;
        }
        return type_bool();
    }

    /* Phase E.3.3: v.as_ptr() -> object  — raw data pointer for FFI buffers.
       Aliases the vec's heap; valid until the vec is grown/freed. */
    if (strcmp(method, "as_ptr") == 0)
    {
        if (argc != 0)
        {
            checker_error(c, call_node->line, call_node->column,
                          "vec.as_ptr() takes no arguments, got %d", argc);
            return NULL;
        }
        return type_object();
    }

    /* v.first() -> T  — deep clone of first element; zero/empty default on empty vec */
    if (strcmp(method, "first") == 0)
    {
        if (argc != 0)
        {
            checker_error(c, call_node->line, call_node->column,
                          "vec.first() takes no arguments, got %d", argc);
            return NULL;
        }
        return elem;
    }

    /* v.get(i) -> T  — deep clone of element at index i (alias of v[i]).
       Out-of-bounds yields zero/empty default value. */
    if (strcmp(method, "get") == 0)
    {
        if (argc != 1)
        {
            checker_error(c, call_node->line, call_node->column,
                          "vec.get() takes 1 argument, got %d", argc);
            return NULL;
        }
        Type *arg = check_expr(c, call_node->as.call.args[0]);
        if (arg && !type_is_integer(arg))
        {
            checker_error(c, call_node->as.call.args[0]->line,
                          call_node->as.call.args[0]->column,
                          "vec.get() index must be integer, got '%s'",
                          type_name(arg));
            return NULL;
        }
        return elem;
    }

    /* v.last() -> T  — deep clone of last element; zero/empty default on empty vec */
    if (strcmp(method, "last") == 0)
    {
        if (argc != 0)
        {
            checker_error(c, call_node->line, call_node->column,
                          "vec.last() takes no arguments, got %d", argc);
            return NULL;
        }
        return elem;
    }

    /* v.truncate(n) -> void  — drop elements [n, len), set len = n */
    if (strcmp(method, "truncate") == 0)
    {
        if (argc != 1)
        {
            checker_error(c, call_node->line, call_node->column,
                          "vec.truncate() takes 1 argument, got %d", argc);
            return NULL;
        }
        Type *arg = check_expr(c, call_node->as.call.args[0]);
        if (arg && !type_is_integer(arg))
        {
            checker_error(c, call_node->as.call.args[0]->line,
                          call_node->as.call.args[0]->column,
                          "vec.truncate() expects integer, got '%s'", type_name(arg));
            return NULL;
        }
        return type_void();
    }

    /* v.remove(i) -> void  — drop element at i, shift tail left */
    if (strcmp(method, "remove") == 0)
    {
        if (argc != 1)
        {
            checker_error(c, call_node->line, call_node->column,
                          "vec.remove() takes 1 argument, got %d", argc);
            return NULL;
        }
        Type *arg = check_expr(c, call_node->as.call.args[0]);
        if (arg && !type_is_integer(arg))
        {
            checker_error(c, call_node->as.call.args[0]->line,
                          call_node->as.call.args[0]->column,
                          "vec.remove() expects integer index, got '%s'", type_name(arg));
            return NULL;
        }
        return type_void();
    }

    /* v.swap(i, j) -> void  — swap elements at indices i and j (raw byte swap) */
    if (strcmp(method, "swap") == 0)
    {
        if (argc != 2)
        {
            checker_error(c, call_node->line, call_node->column,
                          "vec.swap() takes 2 arguments, got %d", argc);
            return NULL;
        }
        Type *ai = check_expr(c, call_node->as.call.args[0]);
        Type *aj = check_expr(c, call_node->as.call.args[1]);
        if (ai && !type_is_integer(ai))
        {
            checker_error(c, call_node->as.call.args[0]->line,
                          call_node->as.call.args[0]->column,
                          "vec.swap() expects integer index, got '%s'", type_name(ai));
            return NULL;
        }
        if (aj && !type_is_integer(aj))
        {
            checker_error(c, call_node->as.call.args[1]->line,
                          call_node->as.call.args[1]->column,
                          "vec.swap() expects integer index, got '%s'", type_name(aj));
            return NULL;
        }
        return type_void();
    }

    /* v.reverse() -> void  — reverse elements in-place (raw byte swap) */
    if (strcmp(method, "reverse") == 0)
    {
        if (argc != 0)
        {
            checker_error(c, call_node->line, call_node->column,
                          "vec.reverse() takes no arguments, got %d", argc);
            return NULL;
        }
        return type_void();
    }

    /* v.extend(src) -> void — deep-clone all elements of src and append */
    if (strcmp(method, "extend") == 0)
    {
        if (argc != 1)
        {
            checker_error(c, call_node->line, call_node->column,
                          "vec.extend() takes 1 argument, got %d", argc);
            return NULL;
        }
        Type *arg = check_expr(c, call_node->as.call.args[0]);
        if (arg == NULL)
            return NULL;
        if (arg->kind != TYPE_VECTOR)
        {
            checker_error(c, call_node->as.call.args[0]->line,
                          call_node->as.call.args[0]->column,
                          "vec.extend() expects vec(%s), got '%s'",
                          type_name(elem), type_name(arg));
            return NULL;
        }
        if (!type_equals(arg->as.vec.elem, elem))
        {
            checker_error(c, call_node->as.call.args[0]->line,
                          call_node->as.call.args[0]->column,
                          "vec.extend() element type mismatch: expected vec(%s), got vec(%s)",
                          type_name(elem), type_name(arg->as.vec.elem));
            return NULL;
        }
        return type_void();
    }

    /* v.insert(i, x) -> void — insert x at index i, shift tail right */
    if (strcmp(method, "insert") == 0)
    {
        if (argc != 2)
        {
            checker_error(c, call_node->line, call_node->column,
                          "vec.insert() takes 2 arguments, got %d", argc);
            return NULL;
        }
        Type *ai = check_expr(c, call_node->as.call.args[0]);
        if (ai && !type_is_integer(ai))
        {
            checker_error(c, call_node->as.call.args[0]->line,
                          call_node->as.call.args[0]->column,
                          "vec.insert() index must be integer, got '%s'", type_name(ai));
            return NULL;
        }
        Type *ax = check_expr(c, call_node->as.call.args[1]);
        if (ax && !type_equals(ax, elem))
        {
            checker_error(c, call_node->as.call.args[1]->line,
                          call_node->as.call.args[1]->column,
                          "vec.insert() expects '%s', got '%s'",
                          type_name(elem), type_name(ax));
            return NULL;
        }
        /* Move tracking: same semantics as push — dynamic string element is moved */
        checker_reject_borrow_move(c, call_node->as.call.args[1], "move into vec");
        checker_try_mark_moved(c, call_node->as.call.args[1]);
        return type_void();
    }

    /* v.contains(x) -> bool  — linear search for x; supported for numeric, bool, string */
    if (strcmp(method, "contains") == 0)
    {
        if (argc != 1)
        {
            checker_error(c, call_node->line, call_node->column,
                          "vec.contains() takes 1 argument, got %d", argc);
            return NULL;
        }
        if (elem->kind == TYPE_STRUCT)
        {
            checker_error(c, call_node->line, call_node->column,
                          "vec.contains() is not supported for struct elements "
                          "(implement __eq and use index_of instead)");
            return NULL;
        }
        Type *arg = check_expr(c, call_node->as.call.args[0]);
        if (arg && !type_equals(arg, elem))
        {
            checker_error(c, call_node->as.call.args[0]->line,
                          call_node->as.call.args[0]->column,
                          "vec.contains() expects '%s', got '%s'",
                          type_name(elem), type_name(arg));
            return NULL;
        }
        return type_bool();
    }

    /* v.index_of(x) -> int  — first index of x, -1 if not found */
    if (strcmp(method, "index_of") == 0)
    {
        if (argc != 1)
        {
            checker_error(c, call_node->line, call_node->column,
                          "vec.index_of() takes 1 argument, got %d", argc);
            return NULL;
        }
        if (elem->kind == TYPE_STRUCT)
        {
            checker_error(c, call_node->line, call_node->column,
                          "vec.index_of() is not supported for struct elements");
            return NULL;
        }
        Type *arg = check_expr(c, call_node->as.call.args[0]);
        if (arg && !type_equals(arg, elem))
        {
            checker_error(c, call_node->as.call.args[0]->line,
                          call_node->as.call.args[0]->column,
                          "vec.index_of() expects '%s', got '%s'",
                          type_name(elem), type_name(arg));
            return NULL;
        }
        return type_int();
    }

    /* v.resize(n) -> void  — grow (zero/empty fill) or shrink (drop excess) to n */
    if (strcmp(method, "resize") == 0)
    {
        if (argc != 1)
        {
            checker_error(c, call_node->line, call_node->column,
                          "vec.resize() takes 1 argument, got %d", argc);
            return NULL;
        }
        Type *arg = check_expr(c, call_node->as.call.args[0]);
        if (arg && !type_is_integer(arg))
        {
            checker_error(c, call_node->as.call.args[0]->line,
                          call_node->as.call.args[0]->column,
                          "vec.resize() expects integer, got '%s'", type_name(arg));
            return NULL;
        }
        return type_void();
    }

    /* v.copy() -> vec(T)  — deep clone entire vec into a new independent vec */
    if (strcmp(method, "copy") == 0)
    {
        if (argc != 0)
        {
            checker_error(c, call_node->line, call_node->column,
                          "vec.copy() takes no arguments, got %d", argc);
            return NULL;
        }
        return vec_type; /* return same vec(T) type */
    }

    /* v.sort() -> void  — in-place ascending sort (numeric/string elements only) */
    if (strcmp(method, "sort") == 0)
    {
        if (argc != 0)
        {
            checker_error(c, call_node->line, call_node->column,
                          "vec.sort() takes no arguments, got %d", argc);
            return NULL;
        }
        if (elem->kind == TYPE_STRUCT)
        {
            checker_error(c, call_node->line, call_node->column,
                          "vec.sort() is not supported for struct elements "
                          "(use vec.sort_by(cmp) with a custom comparator)");
            return NULL;
        }
        return type_void();
    }

    /* v.sort_by(fn(T,T)->int) -> void  — in-place sort with user comparator */
    if (strcmp(method, "sort_by") == 0)
    {
        if (argc != 1)
        {
            checker_error(c, call_node->line, call_node->column,
                          "vec.sort_by() takes 1 argument, got %d", argc);
            return NULL;
        }
        Type *arg = check_expr(c, call_node->as.call.args[0]);
        if (arg == NULL)
            return NULL;
        if (arg->kind != TYPE_FUNCTION)
        {
            checker_error(c, call_node->as.call.args[0]->line,
                          call_node->as.call.args[0]->column,
                          "vec.sort_by() expects a comparator function, got '%s'",
                          type_name(arg));
            return NULL;
        }
        return type_void();
    }

    /* v.slice(start, end) -> vec(T)  — deep-clone [start, end) sub-range */
    if (strcmp(method, "slice") == 0)
    {
        if (argc != 2)
        {
            checker_error(c, call_node->line, call_node->column,
                          "vec.slice() takes 2 arguments (start, end), got %d", argc);
            return NULL;
        }
        Type *as = check_expr(c, call_node->as.call.args[0]);
        if (as && !type_is_integer(as))
        {
            checker_error(c, call_node->as.call.args[0]->line,
                          call_node->as.call.args[0]->column,
                          "vec.slice() start must be integer, got '%s'", type_name(as));
            return NULL;
        }
        Type *ae = check_expr(c, call_node->as.call.args[1]);
        if (ae && !type_is_integer(ae))
        {
            checker_error(c, call_node->as.call.args[1]->line,
                          call_node->as.call.args[1]->column,
                          "vec.slice() end must be integer, got '%s'", type_name(ae));
            return NULL;
        }
        return vec_type; /* returns same vec(T) */
    }

    /* v.shrink_to_fit() -> void  — release excess capacity */
    if (strcmp(method, "shrink_to_fit") == 0)
    {
        if (argc != 0)
        {
            checker_error(c, call_node->line, call_node->column,
                          "vec.shrink_to_fit() takes no arguments, got %d", argc);
            return NULL;
        }
        return type_void();
    }

    checker_error(c, call_node->line, call_node->column,
                  "vec has no method '%s' (available: push, pop, clear, reserve, "
                  "is_empty, get, first, last, truncate, remove, swap, reverse, "
                  "extend, insert, contains, index_of, resize, copy, "
                  "sort, sort_by, slice, shrink_to_fit)",
                  method);
    return NULL;
}

/* Type-check method calls on map(K,V) objects */
static Type *check_map_method(Checker *c, AstNode *call_node, Type *map_type)
{
    const char *method = call_node->as.call.callee->as.field_access.field;
    int argc = call_node->as.call.arg_count;
    Type *K = map_type->as.map.key;
    Type *V = map_type->as.map.val;

    /* Phase 5.7: gate mutating methods on read-only borrow receivers. */
    if (checker_reject_map_mut_on_readonly_borrow(c, call_node, method))
        return NULL;

    /* m.set(key, val) -> void */
    if (strcmp(method, "set") == 0)
    {
        if (argc != 2)
        {
            checker_error(c, call_node->line, call_node->column,
                          "map.set() takes 2 arguments, got %d", argc);
            return NULL;
        }
        Type *k = check_expr(c, call_node->as.call.args[0]);
        if (k && !type_equals(k, K))
            checker_error(c, call_node->as.call.args[0]->line,
                          call_node->as.call.args[0]->column,
                          "map.set() key expects '%s', got '%s'", type_name(K), type_name(k));
        Type *v = check_expr(c, call_node->as.call.args[1]);
        if (v && !type_equals(v, V))
            checker_error(c, call_node->as.call.args[1]->line,
                          call_node->as.call.args[1]->column,
                          "map.set() value expects '%s', got '%s'", type_name(V), type_name(v));
        /* Move tracking: dynamic string key and value are moved into the map.
           Static strings (is_static_string==true) are shared freely — not moved. */
        checker_reject_borrow_move(c, call_node->as.call.args[0], "move into map");
        checker_reject_borrow_move(c, call_node->as.call.args[1], "move into map");
        checker_try_mark_moved(c, call_node->as.call.args[0]); /* key */
        checker_try_mark_moved(c, call_node->as.call.args[1]); /* value */
        return type_void();
    }

    /* m.get(key) -> V  (returns zero value if key absent) */
    if (strcmp(method, "get") == 0)
    {
        if (argc != 1)
        {
            checker_error(c, call_node->line, call_node->column,
                          "map.get() takes 1 argument, got %d", argc);
            return NULL;
        }
        Type *k = check_expr(c, call_node->as.call.args[0]);
        if (k && !type_equals(k, K))
            checker_error(c, call_node->as.call.args[0]->line,
                          call_node->as.call.args[0]->column,
                          "map.get() key expects '%s', got '%s'", type_name(K), type_name(k));
        return V;
    }

    /* m.contains_key(key) -> bool */
    if (strcmp(method, "contains_key") == 0)
    {
        if (argc != 1)
        {
            checker_error(c, call_node->line, call_node->column,
                          "map.contains_key() takes 1 argument, got %d", argc);
            return NULL;
        }
        Type *k = check_expr(c, call_node->as.call.args[0]);
        if (k && !type_equals(k, K))
            checker_error(c, call_node->as.call.args[0]->line,
                          call_node->as.call.args[0]->column,
                          "map.contains_key() key expects '%s', got '%s'",
                          type_name(K), type_name(k));
        return type_bool();
    }

    /* m.remove(key) -> void */
    if (strcmp(method, "remove") == 0)
    {
        if (argc != 1)
        {
            checker_error(c, call_node->line, call_node->column,
                          "map.remove() takes 1 argument, got %d", argc);
            return NULL;
        }
        Type *k = check_expr(c, call_node->as.call.args[0]);
        if (k && !type_equals(k, K))
            checker_error(c, call_node->as.call.args[0]->line,
                          call_node->as.call.args[0]->column,
                          "map.remove() key expects '%s', got '%s'", type_name(K), type_name(k));
        return type_void();
    }

    /* m.clear() -> void */
    if (strcmp(method, "clear") == 0)
    {
        if (argc != 0)
        {
            checker_error(c, call_node->line, call_node->column,
                          "map.clear() takes no arguments, got %d", argc);
            return NULL;
        }
        return type_void();
    }

    /* m.is_empty() -> bool */
    if (strcmp(method, "is_empty") == 0)
    {
        if (argc != 0)
        {
            checker_error(c, call_node->line, call_node->column,
                          "map.is_empty() takes no arguments, got %d", argc);
            return NULL;
        }
        return type_bool();
    }

    /* m.keys() -> vec(K) */
    if (strcmp(method, "keys") == 0)
    {
        if (argc != 0)
        {
            checker_error(c, call_node->line, call_node->column,
                          "map.keys() takes no arguments, got %d", argc);
            return NULL;
        }
        return type_vector(K);
    }

    /* m.values() -> vec(V) */
    if (strcmp(method, "values") == 0)
    {
        if (argc != 0)
        {
            checker_error(c, call_node->line, call_node->column,
                          "map.values() takes no arguments, got %d", argc);
            return NULL;
        }
        return type_vector(V);
    }

    checker_error(c, call_node->line, call_node->column,
                  "map has no method '%s' (available: set, get, contains_key, remove, "
                  "clear, is_empty, keys, values)",
                  method);
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
           strcmp(name, "__move") == 0;
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

    /* Output list (deep-owned names). */
    struct {
        char *name;
        Type *type;
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

/* Phase C.5: by-move capture types — env owns the value, outer is marked
   moved. Currently only string; vec/map/struct(drop) follow the same
   template once their codegen lands. */
static bool capture_type_is_by_move(const Type *t) {
    return t != NULL && t->kind == TYPE_STRING;
}

static bool capture_type_supported(const Type *t) {
    return capture_type_is_pod(t) || capture_type_is_by_move(t);
}

static void cap_record(CaptureScan *s, AstNode *site, const char *name, Type *t) {
    if (cap_already(s, name)) return;
    if (!capture_type_supported(t)) {
        checker_error(s->c, site->line, site->column,
                      "capturing variable '%s' of type '%s' in a closure is "
                      "not yet implemented (Phase C.5 supports POD captures "
                      "and string by-move; vec/map/struct(drop) capture is "
                      "Phase C.7). Move the value via parameter or use a "
                      "supported intermediate.",
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
    case AST_RANGE:
        capture_walk(s, node->as.range.start);
        capture_walk(s, node->as.range.end);
        return;
    case AST_TRY:
        capture_walk(s, node->as.try_expr.expr);
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

static Type *check_expr(Checker *c, AstNode *node)
{
    if (node == NULL)
        return NULL;

    Type *result = NULL;

    switch (node->kind)
    {
    case AST_INT_LIT:
        result = node->as.int_lit.is_char ? type_char() : type_int();
        break;

    case AST_FLOAT_LIT:
        result = type_f64();
        break;

    case AST_STRING_LIT:
        result = type_string();
        break;

    case AST_FORMAT_STRING:
    {
        /* Type-check each interpolated expression */
        for (int i = 0; i < node->as.format_string.expr_count; i++)
        {
            Type *et = check_expr(c, node->as.format_string.exprs[i]);
            if (et == NULL)
                continue;
            /* Ensure the expression is a printable type */
            if (!type_is_numeric(et) && et->kind != TYPE_BOOL && et->kind != TYPE_STRING && et->kind != TYPE_POINTER && et->kind != TYPE_OBJECT)
            {
                checker_error(c, node->as.format_string.exprs[i]->line,
                              node->as.format_string.exprs[i]->column,
                              "cannot interpolate type '%s' in format string",
                              type_name(et));
            }
        }
        result = type_string();
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
        Type *K = check_expr(c, node->as.map_lit.keys[0]);
        Type *V = check_expr(c, node->as.map_lit.vals[0]);
        if (!K || !V)
        {
            result = NULL;
            break;
        }
        for (int i = 1; i < count; i++)
        {
            Type *kt = check_expr(c, node->as.map_lit.keys[i]);
            Type *vt = check_expr(c, node->as.map_lit.vals[i]);
            if (kt && !type_equals(K, kt))
                checker_error(c, node->as.map_lit.keys[i]->line,
                              node->as.map_lit.keys[i]->column,
                              "map literal key type mismatch: expected '%s', got '%s'",
                              type_name(K), type_name(kt));
            if (vt && !type_equals(V, vt))
                checker_error(c, node->as.map_lit.vals[i]->line,
                              node->as.map_lit.vals[i]->column,
                              "map literal value type mismatch: expected '%s', got '%s'",
                              type_name(V), type_name(vt));
        }
        result = type_map(K, V);
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
            (sym->type->kind != TYPE_STRING && sym->type->kind != TYPE_VECTOR &&
             sym->type->kind != TYPE_MAP && sym->type->kind != TYPE_STRUCT))
        {
            checker_error(c, node->line, node->column,
                          "&!: only &!string, &!vec(T), &!map(K,V) and &!struct are supported, got &!%s",
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
                              "comparison requires numeric types, got '%s' and '%s'",
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
                    int si = method_is_static(c, obj_node->as.ident.name, method_name);
                    if (si >= 0)
                    {
                        method_struct = obj_node->as.ident.name;
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

            /* If not a struct-type static call, resolve the object expression */
            if (!is_static_call)
            {
                Type *obj_type = check_expr(c, obj_node);

                /* Intercept string builtin method calls: s.method(args...) */
                if (obj_type && obj_type->kind == TYPE_STRING)
                {
                    result = check_string_method(c, node, obj_type);
                    break;
                }

                /* Intercept vec builtin method calls: v.method(args...) or (*vec).method() */
                {
                    Type *vec_t = obj_type;
                    if (vec_t && vec_t->kind == TYPE_POINTER && vec_t->as.pointer_to &&
                        vec_t->as.pointer_to->kind == TYPE_VECTOR)
                        vec_t = vec_t->as.pointer_to; /* auto-deref *vec(T) */
                    if (vec_t && vec_t->kind == TYPE_VECTOR)
                    {
                        result = check_vector_method(c, node, vec_t);
                        break;
                    }
                }

                /* Intercept map builtin method calls: m.method(args...) */
                if (obj_type && obj_type->kind == TYPE_MAP)
                {
                    result = check_map_method(c, node, obj_type);
                    break;
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
                        int si = method_is_static(c, deref->as.strukt.name, method_name);
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
                                        deref->as.strukt.name, method_name);
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
                            method_struct = deref->as.strukt.name;
                        }
                        else if (si == 1)
                        {
                            /* Static method called via instance — allowed, ignore obj */
                            is_static_call = true;
                            method_struct = deref->as.strukt.name;
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
                              "struct '%s' has no method '%s'", method_struct, method_name);
                result = NULL;
                break;
            }
            /* Set resolved_type on the callee node so codegen can find it */
            node->as.call.callee->resolved_type = callee_type;

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
            if (actual != user_expected)
            {
                checker_error(c, node->line, node->column,
                              "wrong number of arguments: expected %d, got %d", user_expected, actual);
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
        for (int i = user_expected; i < actual; i++)
        {
            check_expr(c, node->as.call.args[i]);
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
        else if (obj->kind == TYPE_VECTOR)
        {
            if (!type_is_integer(idx))
            {
                checker_error(c, node->line, node->column,
                              "vec index must be integer, got '%s'", type_name(idx));
                result = NULL;
            }
            else
            {
                result = obj->as.vec.elem;
            }
        }
        else if (obj->kind == TYPE_MAP)
        {
            if (!type_equals(idx, obj->as.map.key))
            {
                checker_error(c, node->line, node->column,
                              "map key type mismatch: expected '%s', got '%s'",
                              type_name(obj->as.map.key), type_name(idx));
                result = NULL;
            }
            else
            {
                result = obj->as.map.val;
            }
        }
        else
        {
            checker_error(c, node->line, node->column,
                          "cannot index non-array/non-vec type '%s'", type_name(obj));
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

        /* vec .length / .capacity — also accept *vec(T) (auto-deref) */
        {
            Type *vt = obj;
            if (vt && vt->kind == TYPE_POINTER && vt->as.pointer_to &&
                vt->as.pointer_to->kind == TYPE_VECTOR)
                vt = vt->as.pointer_to;
            if (vt && vt->kind == TYPE_VECTOR)
            {
                if (strcmp(field_name, "length") == 0 ||
                    strcmp(field_name, "capacity") == 0)
                {
                    result = type_int();
                }
                else
                {
                    checker_error(c, node->line, node->column,
                                  "vec has no field '%s' (available: length, capacity)",
                                  field_name);
                    result = NULL;
                }
                break;
            }
        }

        /* map .length */
        if (obj->kind == TYPE_MAP)
        {
            if (strcmp(field_name, "length") == 0)
            {
                result = type_int();
            }
            else
            {
                checker_error(c, node->line, node->column,
                              "map has no field '%s' (use .length)", field_name);
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

        /* Auto-dereference: *Struct → Struct for field/method access (like C++ -> ) */
        if (obj->kind == TYPE_POINTER && obj->as.pointer_to &&
            obj->as.pointer_to->kind == TYPE_STRUCT)
        {
            obj = obj->as.pointer_to;
        }

        if (obj->kind != TYPE_STRUCT)
        {
            checker_error(c, node->line, node->column,
                          "field access on non-struct type '%s'", type_name(obj));
            result = NULL;
            break;
        }

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
            result = find_method(c, obj->as.strukt.name, field_name);
        }

        if (result == NULL)
        {
            checker_error(c, node->line, node->column,
                          "struct '%s' has no field or method '%s'",
                          obj->as.strukt.name ? obj->as.strukt.name : "<anon>",
                          field_name);
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
        }

        /* Check body in new scope */
        push_scope(c);
        for (int i = 0; i < n; i++)
        {
            if (params[i])
            {
                scope_define(c->current_scope, node->as.closure.param_names[i], params[i]);
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
                    scope_define(c->current_scope, bname, bt);
                    bnode->resolved_type = bt;
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

        /* Non-enum subjects: existing literal/ident/wildcard pattern handling */
        Type *arm_type = NULL;
        for (int i = 0; i < node->as.match.arm_count; i++)
        {
            MatchArm *arm = &node->as.match.arms[i];

            /* Check pattern type matches subject */
            if (arm->pattern->kind != AST_IDENT ||
                strcmp(arm->pattern->as.ident.name, "_") != 0)
            {
                Type *pat_type = check_expr(c, arm->pattern);
                if (pat_type && !type_equals(pat_type, subject))
                {
                    checker_error(c, arm->pattern->line, arm->pattern->column,
                                  "match pattern type '%s' doesn't match subject type '%s'",
                                  type_name(pat_type), type_name(subject));
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
        /* Look up the struct type */
        Type *st = find_struct_type(c, node->as.new_expr.struct_name);
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
            /* Type-check the value */
            Type *vt = check_expr(c, node->as.new_expr.field_inits[i].value);
            if (vt && !type_equals(vt, st->as.strukt.fields[field_idx].type))
            {
                checker_error(c, node->as.new_expr.field_inits[i].value->line,
                              node->as.new_expr.field_inits[i].value->column,
                              "field '%s': expected '%s', got '%s'",
                              fname,
                              type_name(st->as.strukt.fields[field_idx].type),
                              type_name(vt));
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
        node->resolved_type = result;
    return result;
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
        Type *declared = resolve_type_node(c, node->as.var_decl.var_type,
                                           node->line, node->column);
        if (declared == NULL)
            break;

        if (node->as.var_decl.init)
        {
            /* Special case: map(K,V) m = { key -> val, ... }
               Check each pair against the declared K,V types directly and set resolved_type
               so that check_expr(AST_MAP_LIT) can early-return without re-checking. */
            /* Special case: vec(T) v = [e1, e2, ...] (or empty []).
               Type-check each element against the declared element type and tag
               the literal's resolved_type as the vec type so check_expr(AST_ARRAY_LIT)
               returns vec rather than array. Codegen has a parallel branch that
               builds the vec via repeated push. */
            if (declared && declared->kind == TYPE_VECTOR &&
                node->as.var_decl.init->kind == AST_ARRAY_LIT)
            {
                AstNode *al = node->as.var_decl.init;
                Type *E = declared->as.vec.elem;
                for (int i = 0; i < al->as.array_lit.count; i++)
                {
                    Type *et = check_expr(c, al->as.array_lit.elements[i]);
                    if (et && !type_assignable(E, et))
                        checker_error(c, al->as.array_lit.elements[i]->line,
                                      al->as.array_lit.elements[i]->column,
                                      "vec literal element type mismatch: expected '%s', got '%s'",
                                      type_name(E), type_name(et));
                }
                al->resolved_type = declared; /* signal check_expr / codegen */
            }
            else if (declared && declared->kind == TYPE_MAP &&
                node->as.var_decl.init->kind == AST_MAP_LIT)
            {
                AstNode *ml = node->as.var_decl.init;
                Type *K = declared->as.map.key;
                Type *V = declared->as.map.val;
                for (int i = 0; i < ml->as.map_lit.pair_count; i++)
                {
                    Type *kt = check_expr(c, ml->as.map_lit.keys[i]);
                    Type *vt = check_expr(c, ml->as.map_lit.vals[i]);
                    if (kt && !type_assignable(K, kt))
                        checker_error(c, ml->as.map_lit.keys[i]->line,
                                      ml->as.map_lit.keys[i]->column,
                                      "map literal key type mismatch: expected '%s', got '%s'",
                                      type_name(K), type_name(kt));
                    if (vt && !type_assignable(V, vt))
                        checker_error(c, ml->as.map_lit.vals[i]->line,
                                      ml->as.map_lit.vals[i]->column,
                                      "map literal value type mismatch: expected '%s', got '%s'",
                                      type_name(V), type_name(vt));
                }
                ml->resolved_type = declared; /* signal check_expr to skip re-check */
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
                if (init_type != NULL && !type_assignable(declared, init_type))
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
            /* Phase 5.6: copy-out from a vec borrow (either & or &!) is rejected. */
            checker_reject_vec_borrow_copy_source(c, node->as.var_decl.init,
                                                 "initialize new variable from vec borrow");
            /* Phase 5.7: same for map borrows. */
            checker_reject_map_borrow_copy_source(c, node->as.var_decl.init,
                                                 "initialize new variable from map borrow");
            /* Phase 5.8: same for struct borrows. */
            checker_reject_struct_borrow_copy_source(c, node->as.var_decl.init,
                                                 "initialize new variable from struct borrow");
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
        Type *target = check_expr(c, node->as.assign.target);
        Type *value = check_expr(c, node->as.assign.value);
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
        /* Phase 5.6/5.7: subscript assign `v[i] = x` / `m[k] = v` is forbidden
           when the base is a read-only borrow (&vec(T) / &map(K,V)). Writable
           borrows pass through. */
        if (node->as.assign.target->kind == AST_INDEX)
        {
            AstNode *base = node->as.assign.target->as.index_expr.object;
            if (base && base->kind == AST_IDENT)
            {
                Symbol *bsym = scope_resolve(c->current_scope, base->as.ident.name);
                if (bsym && bsym->is_borrow && bsym->type &&
                    (bsym->type->kind == TYPE_VECTOR ||
                     bsym->type->kind == TYPE_MAP))
                {
                    checker_move_error(c, node->line, node->column,
                                       "cannot assign to '%s[..]': '%s' is a read-only borrow",
                                       base->as.ident.name, base->as.ident.name);
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
            /* Phase 5.6: copy-out from a vec borrow (either & or &!) on the RHS. */
            checker_reject_vec_borrow_copy_source(c, node->as.assign.value,
                                                 "assign vec borrow contents to another variable");
            /* Phase 5.7: same for map borrows. */
            checker_reject_map_borrow_copy_source(c, node->as.assign.value,
                                                 "assign map borrow contents to another variable");
            /* Phase 5.8: same for struct borrows. */
            checker_reject_struct_borrow_copy_source(c, node->as.assign.value,
                                                 "assign struct borrow contents to another variable");
            /* If RHS is a dynamic string IDENT, mark it as moved */
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
            if (val != NULL && !type_assignable(c->current_fn_return, val))
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
            else if (iter->kind == TYPE_VECTOR)
            {
                /* Vec iteration: loop variable is element type */
                scope_define(c->current_scope, node->as.for_stmt.var, iter->as.vec.elem);
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
                              "cannot iterate over '%s'; expected range (a..b), array, vec, or integer",
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

static void check_fn_decl(Checker *c, AstNode *node)
{
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
            /* vec(T) and array(T,N) must be passed by pointer */
            if (params[i])
            {
                if (params[i]->kind == TYPE_VECTOR)
                {
                    checker_error(c, node->line, node->column,
                                  "parameter '%s': vec must be passed by pointer (*vec(%s))",
                                  node->as.fn_decl.param_names[i],
                                  type_name(params[i]->as.vec.elem));
                }
                else if (params[i]->kind == TYPE_ARRAY)
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
    int n = node->as.struct_decl.field_count;

    /* Check for duplicate struct */
    if (find_struct_type(c, name))
    {
        checker_error(c, node->line, node->column,
                      "struct '%s' already defined", name);
        return;
    }

    Type *st = type_struct(name, n);
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

    /* Auto-set has_drop if struct contains string fields (for auto-destruction) */
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
    }
    if (needs_drop)
    {
        st->as.strukt.has_drop = true;
        /* Register compiler-generated __drop method in impl_registry so it's callable */
        int impl_idx = find_or_create_impl(c, name);
        Type *drop_ret = type_void();
        /* Allocate params on heap (not stack) to avoid dangling pointer */
        Type **drop_params = (Type **)malloc_safe(sizeof(Type *));
        drop_params[0] = type_pointer(st); /* *Struct self */
        Type *drop_type = type_function(drop_params, 1, drop_ret, false);
        register_method(c, impl_idx, "__drop", drop_type, false, 0);
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
    case TYPE_VECTOR: return true;
    case TYPE_MAP:    return true;
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
    bool has_drop = false;

    for (int i = 0; i < n; i++)
    {
        /* Duplicate variant name check */
        const char *vn = node->as.enum_decl.variants[i].name;
        for (int j = 0; j < i; j++)
        {
            if (strcmp(node->as.enum_decl.variants[j].name, vn) == 0)
            {
                checker_error(c, node->line, node->column,
                              "duplicate variant '%s' in enum '%s'", vn, name);
            }
        }

        /* Copy variant name */
        size_t vlen = strlen(vn);
        char *vn_copy = (char *)malloc_safe(vlen + 1);
        memcpy(vn_copy, vn, vlen + 1);
        et->as.enom.variants[i].name = vn_copy;

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
    register_enum_type(c, name, et);
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
    Type *st = find_struct_type(c, name);
    if (st == NULL)
    {
        checker_error(c, node->line, node->column,
                      "impl for undefined struct '%s'", name);
        return;
    }

    int impl_idx = find_or_create_impl(c, name);

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
                /* vec(T) and array(T,N) must be passed by pointer */
                if (user_params[j])
                {
                    if (user_params[j]->kind == TYPE_VECTOR)
                    {
                        checker_error(c, method->line, method->column,
                                      "parameter '%s': vec must be passed by pointer",
                                      method->as.fn_decl.param_names[j]);
                    }
                    else if (user_params[j]->kind == TYPE_ARRAY)
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
            all_params[0] = type_pointer(st); /* implicit *Self */
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

        register_method(c, impl_idx, method->as.fn_decl.name, method_type, is_static,
                        method->as.fn_decl.self_borrow_kind);

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
                /* Legacy: self is *Struct pointer (mut-style). */
                scope_define(c->current_scope, "self", type_pointer(st));
            }
            else
            {
                /* &self / &!self: self is Struct with borrow flags. */
                Symbol *self_sym = scope_define(c->current_scope, "self", st);
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
        }
    }
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
        case AST_FN_DECL:
        {
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
                    scope_define(c->current_scope, import_path, mod_type);
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
                bool ok = checker_check(mod->ast, mod->file_path, c->registry);
                module_pop_import(c->registry);
                if (!ok)
                {
                    checker_error(c, decl->line, decl->column,
                                  "errors in imported module '%s'", import_path);
                    break;
                }
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
                else if (d->kind == AST_STRUCT_DECL && d->resolved_type)
                {
                    type_module_add_export(mod_type,
                                           d->as.struct_decl.name, d->resolved_type);
                    /* Phase E.4: register the imported struct type into the
                       importer's struct registry so user code can name it
                       directly (e.g. `File f` after `import io`). */
                    checker_register_struct(c, d->as.struct_decl.name,
                                            d->resolved_type);
                }
                else if (d->kind == AST_ENUM_DECL && d->resolved_type)
                {
                    type_module_add_export(mod_type,
                                           d->as.enum_decl.name, d->resolved_type);
                    /* Phase E.4: register the imported enum into the importer's
                       enum registry so bare variant names (e.g. `ReadBinary`,
                       `Start`) resolve at call sites without qualification —
                       matching the behaviour the built-in io module had. */
                    checker_register_enum(c, d->as.enum_decl.name,
                                          d->resolved_type);
                }
                else if (d->kind == AST_VAR_DECL && d->resolved_type)
                {
                    type_module_add_export(mod_type,
                                           d->as.var_decl.name, d->resolved_type);
                }
            }

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
    /* malloc(i64) -> *u8  (size_t is 64-bit on x64) */
    {
        Type **params = (Type **)malloc_safe(sizeof(Type *));
        params[0] = type_i64();
        Type *ft = type_function(params, 1, type_pointer(type_u8()), false);
        scope_define(c->current_scope, "malloc", ft);
    }
    /* free(*u8) -> void */
    {
        Type **params = (Type **)malloc_safe(sizeof(Type *));
        params[0] = type_pointer(type_u8());
        Type *ft = type_function(params, 1, type_void(), false);
        scope_define(c->current_scope, "free", ft);
    }
    /* sizeof(type) -> int — treated as special; for now just register */
    {
        Type **params = (Type **)malloc_safe(sizeof(Type *));
        params[0] = type_int(); /* placeholder */
        Type *ft = type_function(params, 1, type_int(), false);
        scope_define(c->current_scope, "sizeof", ft);
    }
    /* sqrt(f64) -> f64 */
    {
        Type **params = (Type **)malloc_safe(sizeof(Type *));
        params[0] = type_f64();
        Type *ft = type_function(params, 1, type_f64(), false);
        scope_define(c->current_scope, "sqrt", ft);
    }
}

/* ---- Public entry point ---- */

bool checker_check(AstNode *program, const char *source_path,
                   struct ModuleRegistry *registry)
{
    if (program == NULL || program->kind != AST_PROGRAM)
        return false;

    Checker c;
    memset(&c, 0, sizeof(Checker));
    c.source_path = source_path;
    c.registry = registry;
    c.current_scope = scope_new(NULL);

    register_builtins(&c);
    forward_pass(&c, program);
    check_pass(&c, program);

    /* Cleanup */
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
    for (int i = 0; i < c.impl_count; i++)
    {
        free(c.impl_registry[i].methods);
    }
    free(c.impl_registry);

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
