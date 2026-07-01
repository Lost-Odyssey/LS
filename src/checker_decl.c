/* checker_decl.c
   声明检查：fn/struct/enum/impl/trait/extern 声明 + 模板/trait 基建 + check_decl 派发

   Bodies mechanically relocated from checker.c (docs/plan_checker_split.md).
   No logic changes. All prototypes live in checker_internal.h. */
#include "checker.h"
#include "checker_internal.h"
#include "module.h"
#include "builtins_math.h"
#include "builtins_perf.h"
#include <stdio.h>
#include <string.h>
/* File-local helpers (single-TU; re-static'd at checker split end). */
static void check_fn_decl(Checker *c, AstNode *node);
static int find_trait(Checker *c, const char *name);
static bool has_member_drop_call(AstNode *node, Type *struct_type);
static bool type_is_c_compatible(const Type *t);

/* G2: register a generic function template */
void register_fn_template(Checker *c, AstNode *node) {
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
int find_fn_template(Checker *c, const char *name) {
    for (int i = 0; i < c->fn_template_count; i++) {
        if (strcmp(c->fn_templates[i].name, name) == 0)
            return i;
    }
    return -1;
}

/* Function parameter defaults (档1): attach the fn_decl's literal defaults to
   the function type, require defaulted params be trailing, and type-check each
   default against its param type. `params` are the resolved param types. */
void attach_param_defaults(Checker *c, AstNode *node, Type *fn_type, Type **params)
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
                      "inside an `methods` block",
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
    checker_reject_borrow_return(c, ret, node, node->line, node->column);  /* Phase 0/2 */
    Type *fn_type = type_function(params, n, ret, false);
    /* param defaults already attached in forward_pass (the type calls resolve to). */

    /* Define function in current scope */
    if (!scope_define(c->current_scope, node->as.fn_decl.name, fn_type))
    {
        checker_error(c, node->line, node->column,
                      "function '%s' already defined", node->as.fn_decl.name);
    }

    /* Check body */
    chk_push_scope(c);
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
    chk_pop_scope(c);

    node->resolved_type = fn_type;
}

void check_struct_decl(Checker *c, AstNode *node)
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
        /* Phase 0 (borrow extension, docs/plan_borrow_extension.md §3): a borrow
           field (&T / &!T) was silently accepted with zero safety checks — a
           latent dangling-pointer landmine (the Ref can outlive its referent).
           Reject until a real region analysis (Phase 3) lands; until then use
           a value-offset view (StrSlice{off,len}) over an owned buffer. */
        if (ft != NULL && ft->kind == TYPE_REFERENCE)
        {
            checker_error(c, node->line, node->column,
                          "struct fields cannot be borrows yet: field '%s' of "
                          "struct '%s' has borrow type &%s%s (use a value-offset "
                          "view instead)",
                          node->as.struct_decl.field_names[i], name,
                          ft->is_mut ? "!" : "",
                          ft->as.pointer_to ? type_name(ft->as.pointer_to) : "T");
        }
        /* A slice field would equally dangle (it carries a borrowed *T). */
        if (ft != NULL && ft->kind == TYPE_SLICE)
        {
            checker_error(c, node->line, node->column,
                          "struct fields cannot be slices: field '%s' of struct "
                          "'%s' has slice type '%s' (a borrowed view cannot be "
                          "stored; copy into a Vec(%s) to own it)",
                          node->as.struct_decl.field_names[i], name,
                          type_name(ft), type_name(ft->as.array.elem));
        }
        /* Copy field name */
        const char *fn = node->as.struct_decl.field_names[i];
        size_t len = strlen(fn);
        char *fn_copy = (char *)malloc_safe(len + 1);
        memcpy(fn_copy, fn, len + 1);
        st->as.strukt.fields[i].name = fn_copy;
        st->as.strukt.fields[i].type = ft;
        st->as.strukt.fields[i].default_expr =
            node->as.struct_decl.field_defaults ? node->as.struct_decl.field_defaults[i] : NULL;
        st->as.strukt.fields[i].is_private =
            node->as.struct_decl.field_private ? node->as.struct_decl.field_private[i] : false;

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
        /* A-3 (docs/bugs_deferred_p5_4.md §3): an unresolved field type (e.g. a
           forward reference to a not-yet-defined struct) leaves ft == NULL and
           resolve_type_node already reported the error. Skip rather than deref
           NULL (which segfaulted instead of exiting gracefully). */
        if (ft == NULL)
            continue;
        if (ft->kind == TYPE_STRUCT && ft->as.strukt.has_drop)
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
                        NULL, NULL, node->line, node->column);
        /* Also define in global scope for free function call */
        scope_define(c->current_scope, "__drop", drop_type);
    }

    register_struct_type(c, name, st);
    node->resolved_type = st;
}

/* Helper: returns true if the given type owns heap memory and therefore
   contributes to a containing aggregate's has_drop flag. */
bool type_owns_heap_for_enum(const Type *t)
{
    if (t == NULL) return false;
    switch (t->kind)
    {
    case TYPE_STRUCT: return t->as.strukt.has_drop;
    case TYPE_ENUM:   return t->as.enom.has_drop;
    default:          return false;
    }
}

void check_enum_decl(Checker *c, AstNode *node)
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
                /* Phase 0 (borrow extension): a borrow payload is the same
                   dangling landmine as a borrow struct field — the &T can
                   outlive its referent. Reject until region analysis lands. */
                if (pt != NULL && pt->kind == TYPE_REFERENCE)
                {
                    checker_error(c, node->line, node->column,
                        "enum payloads cannot be borrows yet: variant '%s' of "
                        "'%s' has borrow payload &%s%s (use a value-offset view "
                        "instead)",
                        et->as.enom.variants[i].name, name,
                        pt->is_mut ? "!" : "",
                        pt->as.pointer_to ? type_name(pt->as.pointer_to) : "T");
                }
                if (pt != NULL && pt->kind == TYPE_SLICE)
                {
                    checker_error(c, node->line, node->column,
                        "enum payloads cannot be slices: variant '%s' of '%s' "
                        "has slice payload '%s' (a borrowed view cannot be stored)",
                        et->as.enom.variants[i].name, name, type_name(pt));
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

void check_impl_decl(Checker *c, AstNode *node)
{
    const char *name = node->as.impl_decl.name;

    /* G1.5: generic impl — bind to struct template, defer method checking
       to instantiation time (Step 8/9). */
    if (node->as.impl_decl.type_param_count > 0)
    {
        int tidx = find_struct_template_idx(c, name);
        if (tidx < 0) {
            checker_error(c, node->line, node->column,
                          "methods for undefined generic struct '%s'", name);
            return;
        }
        c->struct_templates[tidx].impl_node = node;
        return;
    }

    Type *st = find_struct_type(c, name);
    Type *et = NULL;
    bool is_enum_impl = false;
    if (st == NULL)
    {
        et = find_enum_type(c, name);
        if (et == NULL)
        {
            if (resolve_builtin_type_by_name(name) != NULL)
            {
                /* P5-4: `impl <builtin>` died with `impl string` (Phase 2.5). */
                checker_error(c, node->line, node->column,
                              "methods on builtin type '%s' is not supported", name);
                return;
            }
            checker_error(c, node->line, node->column,
                          "methods for undefined type '%s'", name);
            return;
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
    c->current_impl_struct_type = is_enum_impl ? NULL : st;
    c->current_impl_enum_type   = is_enum_impl ? et : NULL;
    Type *self_type = st ? st : et;

    /* B-4.1: key the impl_registry by the type's unique name (llvm_name for module
       types) so same-named impls across modules don't collide. */
    int impl_idx = find_or_create_impl(c, impl_key_of_type(self_type));

    for (int i = 0; i < node->as.impl_decl.method_count; i++)
    {
        AstNode *method = node->as.impl_decl.methods[i];
        if (method->kind != AST_FN_DECL)
            continue;

        /* Retire the bare reserved protocol method names: users must implement
           the FromList / FromPairs interface, not hand-write `def __from_list`.
           origin_iface != NULL means this node was folded in from an interface
           impl (which renamed from_list→__from_list) — those are allowed. */
        {
            const char *mn = method->as.fn_decl.name;
            if (mn != NULL && method->as.fn_decl.origin_iface == NULL &&
                (strcmp(mn, "__from_list") == 0 || strcmp(mn, "__from_pairs") == 0))
            {
                const char *iface = strcmp(mn, "__from_list") == 0 ? "FromList" : "FromPairs";
                const char *um    = strcmp(mn, "__from_list") == 0 ? "from_list" : "from_pairs";
                checker_error(c, method->line, method->column,
                    "'%s' is reserved; implement '%s' via `methods T: %s` instead",
                    mn, um, iface);
            }
        }

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
        checker_reject_borrow_return(c, ret, method, method->line, method->column);  /* Phase 0/2 */

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
                        NULL, method,  /* inherent method */
                        method->line, method->column))
            continue;

        /* Also define in global scope so it can be called as a free function */
        scope_define(c->current_scope, method->as.fn_decl.name, method_type);

        /* Check body */
        chk_push_scope(c);
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
        chk_pop_scope(c);

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

        /* Record a user-defined __clone (raw-pointer-owning structs supply
           their own deep copy). Codegen needs this to forward-declare the
           symbol when the defining module's body is emitted after a consumer
           module — without it, emit_struct_clone_val silently falls back to
           the field-wise auto-clone, which shallow-copies raw *T buffers and
           double-frees (e.g. Str clone in another module's function). */
        if (!is_static && !is_enum_impl &&
            strcmp(method->as.fn_decl.name, "__clone") == 0)
        {
            st->as.strukt.has_user_clone = true;
        }
    }

    /* Restore Self context */
    c->current_impl_struct_type = saved_impl_st;
    c->current_impl_enum_type   = saved_impl_et;
}

void check_extern_fn(Checker *c, AstNode *node)
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

void check_extern_struct_decl(Checker *c, AstNode *node)
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

void check_extern_block(Checker *c, AstNode *node)
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

void check_load_lib(Checker *c, AstNode *node)
{
    if (!scope_define(c->current_scope, node->as.load_lib.var_name, type_lib()))
    {
        checker_error(c, node->line, node->column,
                      "library '%s' already defined", node->as.load_lib.var_name);
    }
    node->resolved_type = type_lib();
}

static int find_trait(Checker *c, const char *name)
{
    for (int i = 0; i < c->trait_count; i++)
    {
        if (strcmp(c->trait_registry[i].name, name) == 0)
            return i;
    }
    return -1;
}

/* Check whether a concrete type satisfies a trait constraint by looking up trait_impls. */
bool checker_type_satisfies_trait(Checker *c, Type *type, const char *trait_name)
{
    if (type == NULL) return false;
    if (strcmp(trait_name, "Equal") == 0)
    {
        if (type->kind == TYPE_BOOL ||
            type_is_numeric(type) || type_is_pointer_like(type))
            return true;
    }
    else if (strcmp(trait_name, "Order") == 0)
    {
        if (type_is_numeric(type))
            return true;
    }
    else if (strcmp(trait_name, "Pod") == 0)
    {
        /* Pod = plain-old-data: no malloc-owning fields and no user __drop, i.e.
           type_owns_heap_for_enum() is false (POD scalars, raw pointers, object,
           and has_drop==false structs/enums all qualify). std.arena requires its
           element type to be Pod so a bulk reset()/__drop can never leak an owned
           buffer (an arena object can't carry a Str/Vec/Map field). See
           docs/plan_arena_allocator.md §2/§4.3. A built-in bound, not a real
           trait — no `impl Pod` needed. */
        return !type_owns_heap_for_enum(type);
    }
    else if (strcmp(trait_name, "Destroy") == 0)
    {
        /* Destroy = the type has a destructor: a user `~`/__drop, or has_drop
           fields needing RAII cleanup. The inverse of Pod. A built-in bound, not a
           real trait — `methods X: Destroy { def ~ }` folds to an inherent __drop
           at parse time, so this only backs `where T: Destroy` constraints. */
        return type_owns_heap_for_enum(type);
    }
    const char *tname = type_name(type);
    /* A generic trait impl (`impl(T) Add for Complex(T)`) is recorded under the
       generic BASE name ("Complex"); a concrete instance's type_name is the
       mangled "Complex(f64)". Match either. */
    const char *tbase = (type->kind == TYPE_STRUCT) ? type->as.strukt.generic_base : NULL;
    for (int i = 0; i < c->trait_impl_count; i++)
    {
        if (strcmp(c->trait_impls[i].trait_name, trait_name) != 0)
            continue;
        if (strcmp(c->trait_impls[i].struct_name, tname) == 0 ||
            (tbase != NULL && strcmp(c->trait_impls[i].struct_name, tbase) == 0))
        {
            return true;
        }
    }
    return false;
}

/* Check a trait declaration: resolve method signatures and register in trait_registry. */
void check_trait_decl(Checker *c, AstNode *node)
{
    const char *name = node->as.trait_decl.name;

    /* Check for duplicate trait name */
    if (find_trait(c, name) >= 0)
    {
        if (is_builtin_operator_trait(name))
            checker_error(c, node->line, node->column,
                          "'%s' is a built-in operator interface and cannot be redefined", name);
        else
            checker_error(c, node->line, node->column,
                          "interface '%s' already defined", name);
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
        c->trait_registry[idx].methods[mi].is_static = sig->as.fn_decl.is_static;
        c->trait_registry[idx].methods[mi].self_borrow_kind = sig->as.fn_decl.self_borrow_kind;
    }

    /* Restore Self context */
    c->current_impl_struct_type = saved_impl_st;
}

/* Check an `impl Trait for Struct { ... }` block:
   verify trait exists, struct exists, method signatures match the trait,
   then register methods into impl_registry and record the trait-impl pair. */
void check_impl_trait_decl(Checker *c, AstNode *node)
{
    const char *trait_name  = node->as.impl_trait_decl.trait_name;
    const char *struct_name = node->as.impl_trait_decl.struct_name;

    /* Clone interface: the user writes `def clone(&self) -> Self`, but the deep-copy
       machinery (has_user_clone / emit_clone_value) scans for the internal name
       `__clone`. Rename the method node up front so both the generic fold path and
       the non-generic validation/registration see `__clone`. (Destroy's `~` already
       produces `__drop` in the parser, so it needs no rename.) */
    if (strcmp(trait_name, "Clone") == 0)
    {
        for (int i = 0; i < node->as.impl_trait_decl.method_count; i++)
        {
            AstNode *m = node->as.impl_trait_decl.methods[i];
            if (m != NULL && m->kind == AST_FN_DECL && m->as.fn_decl.name != NULL &&
                strcmp(m->as.fn_decl.name, "clone") == 0)
            {
                free(m->as.fn_decl.name);
                size_t ln = strlen("__clone") + 1;
                char *nm = (char *)malloc_safe(ln);
                memcpy(nm, "__clone", ln);
                m->as.fn_decl.name = nm;
            }
        }
    }

    /* Protocol interface facades: rename the user-facing method to the internal
       reserved name the literal-init detection scans for (mirrors clone→__clone).
       from_list→__from_list, from_pairs→__from_pairs. (The __index family is an
       indexing-operator protocol — __index/__index_set/__indexN — kept as-is, not
       a marker facade.) */
    {
        const char *user_m = NULL, *internal_m = NULL;
        if (strcmp(trait_name, "FromList") == 0)  { user_m = "from_list";  internal_m = "__from_list"; }
        else if (strcmp(trait_name, "FromPairs") == 0) { user_m = "from_pairs"; internal_m = "__from_pairs"; }
        if (user_m != NULL)
        {
            for (int i = 0; i < node->as.impl_trait_decl.method_count; i++)
            {
                AstNode *m = node->as.impl_trait_decl.methods[i];
                if (m != NULL && m->kind == AST_FN_DECL && m->as.fn_decl.name != NULL &&
                    strcmp(m->as.fn_decl.name, user_m) == 0)
                {
                    free(m->as.fn_decl.name);
                    size_t ln = strlen(internal_m) + 1;
                    char *nm = (char *)malloc_safe(ln);
                    memcpy(nm, internal_m, ln);
                    m->as.fn_decl.name = nm;
                }
            }
        }
    }

    /* Generic trait impl: `impl(T) Add for Complex(T)`. The struct is generic, so
       its methods are monomorphized per concrete instance. Rather than build a
       parallel instantiation path, FOLD this impl's method nodes into the struct's
       generic inherent impl_node (stamped on the struct template); the existing
       instantiate_impl_method_types then emits them for every Complex(U). Record
       the (trait, base) pair so operator dispatch / satisfies_trait recognize any
       Complex(U). Methods already carry $op_* (operator) / static (zero) shapes. */
    if (node->as.impl_trait_decl.type_param_count > 0)
    {
        /* Idempotent: the impl_node AST is shared across module checkers, so guard
           against re-folding (re-recording the pair would also dup-append). */
        for (int ii = 0; ii < c->trait_impl_count; ii++)
            if (strcmp(c->trait_impls[ii].trait_name, trait_name) == 0 &&
                strcmp(c->trait_impls[ii].struct_name, struct_name) == 0)
                return;

        if (find_trait(c, trait_name) < 0 && !is_builtin_operator_trait(trait_name))
        {
            checker_error(c, node->line, node->column, "unknown interface '%s'", trait_name);
            return;
        }
        int gtidx = find_struct_template_idx(c, struct_name);
        if (gtidx < 0)
        {
            checker_error(c, node->line, node->column,
                          "generic interface methods for undefined generic struct '%s'", struct_name);
            return;
        }
        AstNode *impl_node = c->struct_templates[gtidx].impl_node;
        if (impl_node == NULL)
        {
            checker_error(c, node->line, node->column,
                          "generic interface methods '%s for %s' requires an inherent "
                          "'methods %s(T)' block before it", trait_name, struct_name, struct_name);
            return;
        }
        int old_n = impl_node->as.impl_decl.method_count;
        int add_n = node->as.impl_trait_decl.method_count;
        impl_node->as.impl_decl.methods = realloc_safe(
            impl_node->as.impl_decl.methods,
            (size_t)(old_n + add_n) * sizeof(AstNode *));
        for (int i = 0; i < add_n; i++)
        {
            AstNode *fm = node->as.impl_trait_decl.methods[i];
            impl_node->as.impl_decl.methods[old_n + i] = fm;
            /* L-002 v2: remember which interface provided this folded method so the
               monomorphization loop can register it with the right origin (the
               impl_trait_decl node's identity is lost after folding). trait_name
               points into the persisting impl_trait_decl node — same program AST
               lifetime as the method node. Inherent methods keep origin_iface NULL. */
            if (fm != NULL && fm->kind == AST_FN_DECL)
                fm->as.fn_decl.origin_iface = trait_name;
        }
        impl_node->as.impl_decl.method_count = old_n + add_n;
        /* Transfer ownership of the method NODES to the inherent impl_node: free
           only the (now-empty) trait-decl array and detach it, so ast_free does not
           double-free the nodes (they are referenced from both arrays otherwise). */
        free(node->as.impl_trait_decl.methods);
        node->as.impl_trait_decl.methods = NULL;
        node->as.impl_trait_decl.method_count = 0;

        if (c->trait_impl_count >= c->trait_impl_cap)
        {
            c->trait_impl_cap = GROW_CAPACITY(c->trait_impl_cap);
            c->trait_impls = realloc_safe(c->trait_impls,
                (size_t)c->trait_impl_cap * sizeof(c->trait_impls[0]));
        }
        int gti = c->trait_impl_count++;
        c->trait_impls[gti].trait_name = trait_name;   /* points into AST (base) */
        c->trait_impls[gti].struct_name = struct_name; /* generic base name */
        return;
    }

    /* 1. Find the trait */
    int tidx = find_trait(c, trait_name);
    if (tidx < 0)
    {
        checker_error(c, node->line, node->column,
                      "unknown interface '%s'", trait_name);
        return;
    }

    /* 2. Find the target type (struct, enum, or builtin) */
    Type *st = find_struct_type(c, struct_name);
    if (st == NULL)
    {
        /* Enums can implement interfaces too (operator overloads, Hash, ...). */
        st = find_enum_type(c, struct_name);
    }
    if (st == NULL)
    {
        /* Step 11: fallback to builtin type names (int, f64, string, ...) */
        st = resolve_builtin_type_by_name(struct_name);
    }
    if (st == NULL)
    {
        checker_error(c, node->line, node->column,
                      "methods interface for undefined type '%s'", struct_name);
        return;
    }

    /* Lifecycle traits flag the type's drop/clone status — the non-generic mirror of
       the per-instance flagging in the generic path (checker.c ~1837). Str/Mutex/
       Region have all-POD fields, so without this their `~`/`clone` never registers
       has_drop and their values leak. (Generic types get flagged via the fold +
       monomorphization detection instead.) */
    if (st->kind == TYPE_STRUCT)
    {
        if (strcmp(trait_name, "Destroy") == 0)
        {
            st->as.strukt.has_drop = true;
            st->as.strukt.has_user_drop = true;
        }
        else if (strcmp(trait_name, "Clone") == 0)
        {
            st->as.strukt.has_user_clone = true;
        }
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
                          "interface '%s' already implemented for struct '%s'",
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
                              "operator method '%s' is only valid when implementing built-in interface '%s'",
                              operator_symbol_for_method(mname), want ? want : "<operator>");
                continue;
            }
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
                          "method '%s' is not declared in interface '%s'",
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

        /* Static-ness must match the trait declaration */
        bool trait_static = c->trait_registry[tidx].methods[trait_mi].is_static;
        if (is_static != trait_static)
        {
            checker_error(c, method->line, method->column,
                          "method '%s' static mismatch: interface '%s' declares it %s",
                          mname, trait_name, trait_static ? "static" : "non-static");
        }

        /* Check self_borrow_kind matches (instance methods only) */
        if (!is_static)
        {
            int trait_sbk = c->trait_registry[tidx].methods[trait_mi].self_borrow_kind;
            if (user_sbk != trait_sbk)
            {
                const char *expected_str = trait_sbk == 1 ? "&self" : (trait_sbk == 2 ? "&!self" : "no self");
                const char *got_str = user_sbk == 1 ? "&self" : (user_sbk == 2 ? "&!self" : "no self");
                checker_error(c, method->line, method->column,
                              "method '%s' self parameter mismatch: interface '%s' requires %s, got %s",
                              mname, trait_name, expected_str, got_str);
            }
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
        checker_reject_borrow_return(c, ret, method, method->line, method->column);  /* Phase 0/2 */

        /* Compare parameter count and types against trait signature.
           The trait signature does NOT include the implicit *Self param —
           it stores only user-visible params (same as what parser gives). */
        Type *trait_fn = c->trait_registry[tidx].methods[trait_mi].type;
        int trait_n = trait_fn->as.function.param_count;
        if (user_n != trait_n)
        {
            checker_error(c, method->line, method->column,
                          "method '%s' parameter count mismatch: interface '%s' requires %d, got %d",
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
                                  "method '%s' parameter %d type mismatch in interface '%s'",
                                  mname, j + 1, trait_name);
                }
            }
        }

        /* Compare return type (Self placeholder → st) */
        Type *trait_ret = trait_fn->as.function.return_type;
        if (ret && trait_ret && !type_equals_with_self(trait_ret, ret, st))
        {
            checker_error(c, method->line, method->column,
                          "method '%s' return type mismatch in interface '%s'",
                          mname, trait_name);
        }

        /* Build the internal function type. Instance methods get *Self prepended
           as the implicit self param; static methods take only the user params. */
        Type *method_type;
        if (is_static)
        {
            /* user_params is handed to (and owned by) the function type. */
            method_type = type_function(user_params, user_n, ret, false);
        }
        else
        {
            int total_n = user_n + 1;
            Type **all_params = (Type **)malloc_safe((size_t)total_n * sizeof(Type *));
            all_params[0] = type_pointer(st); /* implicit *Self */
            for (int j = 0; j < user_n; j++)
                all_params[j + 1] = user_params[j];
            free(user_params);
            method_type = type_function(all_params, total_n, ret, false);
        }

        /* Register in impl_registry (same as check_impl_decl) */
        if (!register_method(c, impl_idx, mname, method_type, is_static, user_sbk,
                           trait_name, method,  /* interface-provided method */
                           method->line, method->column))
            continue;

        /* Register in impl_registry (same as check_impl_decl) */
        scope_define(c->current_scope, mname, method_type);

        /* Check body (same pattern as check_impl_decl) */
        chk_push_scope(c);
        if (!is_static)
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
            Type *pt = method_type->as.function.params[is_static ? j : j + 1];
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
                    if (pt->kind == TYPE_BLOCK)
                        param_sym->is_borrow = true;
                }
            }
        }
        Type *saved_ret = c->current_fn_return;
        c->current_fn_return = ret;
        check_stmt(c, method->as.fn_decl.body);
        c->current_fn_return = saved_ret;
        chk_pop_scope(c);

        method->resolved_type = method_type;
    }

    /* 6. Check for missing methods */
    for (int j = 0; j < trait_method_count; j++)
    {
        if (!matched[j])
        {
            const char *tmname = c->trait_registry[tidx].methods[j].name;
            /* Equal/Order derivable comparison operators (!=, >, <=, >=) are optional —
               they are synthesized from == / < when not explicitly provided. */
            if (is_builtin_operator_trait(trait_name) && is_optional_operator_method(tmname))
                continue;
            const char *disp = is_builtin_operator_trait(trait_name)
                                   ? operator_symbol_for_method(tmname) : tmname;
            checker_error(c, node->line, node->column,
                          "struct '%s' does not implement interface '%s': missing method '%s'",
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
void register_one_imported_trait_decl(Checker *c, AstNode *d, Type *mod_type)
{
    if (d->kind == AST_TRAIT_DECL)
    {
        if (find_trait(c, d->as.trait_decl.name) < 0)
        {
            /* This trait was already validated in its defining module (a module
               only reaches here after checking cleanly). Re-registering it in the
               importer's scope re-resolves its method signatures, which can refer
               to a same-module type the importer hasn't bound yet (e.g.
               `interface Reflect { reflect() -> TypeInfo }` propagated transitively
               before the importer's own `import std.core.reflect` runs). Suppress
               such spurious resolution errors — a genuinely broken trait would have
               failed in its own module's check and aborted the import earlier. */
            bool saved_silent = c->silent_type_errors;
            c->silent_type_errors = true;
            check_trait_decl(c, d);
            c->silent_type_errors = saved_silent;
        }
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
                        tr_name, method,  /* imported interface-provided method */
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
   operator traits (Equal/Order) bypass this. `visited`/`vcount` guard diamonds/cycles. */
void propagate_imported_traits(Checker *c, const char *import_path,
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

void check_decl(Checker *c, AstNode *node)
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
