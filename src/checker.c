/* checker.c — Type checker: walks AST, validates types, fills resolved_type */
#include "checker.h"
#include "module.h"
#include <stdio.h>
#include <string.h>

/* ---- Error reporting ---- */

static void checker_error(Checker *c, int line, int col, const char *fmt, ...) {
    if (c->error_count >= CHECKER_MAX_ERRORS) return;
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

static void checker_warning(Checker *c, int line, int col, const char *fmt, ...) {
    fprintf(stderr, "[warning] %s:%d:%d: ",
            c->source_path ? c->source_path : "<unknown>", line, col);
    va_list args;
    va_start(args, fmt);
    vfprintf(stderr, fmt, args);
    va_end(args);
    fprintf(stderr, "\n");
}

/* ---- Struct type registry ---- */

static void register_struct_type(Checker *c, const char *name, Type *type) {
    if (c->struct_type_count >= c->struct_type_cap) {
        c->struct_type_cap = GROW_CAPACITY(c->struct_type_cap);
        c->struct_types = realloc_safe(c->struct_types,
            (size_t)c->struct_type_cap * sizeof(c->struct_types[0]));
    }
    c->struct_types[c->struct_type_count].name = name;
    c->struct_types[c->struct_type_count].type = type;
    c->struct_type_count++;
}

static Type *find_struct_type(Checker *c, const char *name) {
    for (int i = 0; i < c->struct_type_count; i++) {
        if (strcmp(c->struct_types[i].name, name) == 0) {
            return c->struct_types[i].type;
        }
    }
    return NULL;
}

/* ---- Impl/method registry ---- */

static int find_or_create_impl(Checker *c, const char *struct_name) {
    for (int i = 0; i < c->impl_count; i++) {
        if (strcmp(c->impl_registry[i].struct_name, struct_name) == 0) return i;
    }
    if (c->impl_count >= c->impl_cap) {
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
                            Type *type, bool is_static) {
    int mc = c->impl_registry[impl_idx].method_count;
    int cap = c->impl_registry[impl_idx].method_cap;
    if (mc >= cap) {
        cap = GROW_CAPACITY(cap);
        c->impl_registry[impl_idx].method_cap = cap;
        c->impl_registry[impl_idx].methods = realloc_safe(
            c->impl_registry[impl_idx].methods,
            (size_t)cap * sizeof(c->impl_registry[impl_idx].methods[0]));
    }
    c->impl_registry[impl_idx].methods[mc].name = name;
    c->impl_registry[impl_idx].methods[mc].type = type;
    c->impl_registry[impl_idx].methods[mc].is_static = is_static;
    c->impl_registry[impl_idx].method_count++;
}

static Type *find_method(Checker *c, const char *struct_name, const char *method_name) {
    for (int i = 0; i < c->impl_count; i++) {
        if (strcmp(c->impl_registry[i].struct_name, struct_name) != 0) continue;
        for (int j = 0; j < c->impl_registry[i].method_count; j++) {
            if (strcmp(c->impl_registry[i].methods[j].name, method_name) == 0) {
                return c->impl_registry[i].methods[j].type;
            }
        }
    }
    return NULL;
}

/* Check if a registered method is static. Returns -1 if not found, 0 if instance, 1 if static */
static int method_is_static(Checker *c, const char *struct_name, const char *method_name) {
    for (int i = 0; i < c->impl_count; i++) {
        if (strcmp(c->impl_registry[i].struct_name, struct_name) != 0) continue;
        for (int j = 0; j < c->impl_registry[i].method_count; j++) {
            if (strcmp(c->impl_registry[i].methods[j].name, method_name) == 0) {
                return c->impl_registry[i].methods[j].is_static ? 1 : 0;
            }
        }
    }
    return -1;
}

/* ---- Resolve TypeNode -> Type ---- */

static Type *resolve_type_node(Checker *c, TypeNode *tn, int line, int col) {
    if (tn == NULL) return type_void();

    switch (tn->kind) {
    case TYPE_NODE_PRIMITIVE:
        switch (tn->as.primitive) {
        case TOKEN_TYPE_INT:    return type_int();
        case TOKEN_TYPE_I8:     return type_i8();
        case TOKEN_TYPE_I16:    return type_i16();
        case TOKEN_TYPE_I32:    return type_i32();
        case TOKEN_TYPE_I64:    return type_i64();
        case TOKEN_TYPE_U8:     return type_u8();
        case TOKEN_TYPE_U16:    return type_u16();
        case TOKEN_TYPE_U32:    return type_u32();
        case TOKEN_TYPE_U64:    return type_u64();
        case TOKEN_TYPE_F32:    return type_f32();
        case TOKEN_TYPE_F64:    return type_f64();
        case TOKEN_TYPE_BOOL:   return type_bool();
        case TOKEN_TYPE_STRING: return type_string();
        case TOKEN_TYPE_VOID:   return type_void();
        case TOKEN_TYPE_LIB:    return type_lib();
        case TOKEN_TYPE_OBJECT: return type_object();
        default:
            checker_error(c, line, col, "unknown primitive type");
            return NULL;
        }
    case TYPE_NODE_POINTER:
        return type_pointer(resolve_type_node(c, tn->as.pointee, line, col));
    case TYPE_NODE_ARRAY:
        return type_array(resolve_type_node(c, tn->as.array.elem, line, col),
                          tn->as.array.size);
    case TYPE_NODE_VECTOR:
        return type_vector(resolve_type_node(c, tn->as.vec.elem, line, col));
    case TYPE_NODE_FN: {
        int n = tn->as.fn.param_count;
        Type **params = NULL;
        if (n > 0) {
            params = (Type **)malloc_safe((size_t)n * sizeof(Type *));
            for (int i = 0; i < n; i++) {
                params[i] = resolve_type_node(c, tn->as.fn.params[i], line, col);
            }
        }
        Type *ret = resolve_type_node(c, tn->as.fn.ret, line, col);
        return type_function(params, n, ret, false);
    }
    case TYPE_NODE_NAMED: {
        Type *st = find_struct_type(c, tn->as.named.name);
        if (st == NULL) {
            checker_error(c, line, col, "unknown type '%s'", tn->as.named.name);
            return NULL;
        }
        return st;
    }
    }
    return NULL;
}

/* ---- Type compatibility ---- */

/* Check if src type can be assigned to dst type (includes implicit conversions).
   Returns true if compatible. This is stricter than type_equals: it also allows
   *T -> object (implicit), nil -> object, nil -> *T. */
static bool type_assignable(const Type *dst, const Type *src) {
    if (type_equals(dst, src)) return true;
    if (dst == NULL || src == NULL) return false;

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

    /* object -> object (trivially via type_equals above, but explicit) */
    return false;
}

/* ---- Scope helpers ---- */

static void push_scope(Checker *c) {
    c->current_scope = scope_new(c->current_scope);
}

static void pop_scope(Checker *c) {
    Scope *old = c->current_scope;
    c->current_scope = old->parent;
    scope_free(old);
}

/* ---- Forward declarations ---- */

static Type *check_expr(Checker *c, AstNode *node);
static void check_stmt(Checker *c, AstNode *node);
static void check_decl(Checker *c, AstNode *node);

/* ---- Helper functions ---- */

static bool struct_has_string_fields(Type *t) {
    if (t == NULL || t->kind != TYPE_STRUCT) return false;
    for (int i = 0; i < t->as.strukt.field_count; i++) {
        Type *ft = t->as.strukt.fields[i].type;
        if (ft->kind == TYPE_STRING) return true;
        if (ft->kind == TYPE_STRUCT && struct_has_string_fields(ft)) return true;
    }
    return false;
}

/* ---- String builtin method type checking ---- */

/* Type-check a string method call: s.method(args...).
   Returns the result type, or NULL on error. */
static Type *check_string_method(Checker *c, AstNode *call_node, Type *obj_type) {
    (void)obj_type;
    const char *method = call_node->as.call.callee->as.field_access.field;
    int argc = call_node->as.call.arg_count;

    /* s.empty() -> bool */
    if (strcmp(method, "empty") == 0) {
        if (argc != 0) {
            checker_error(c, call_node->line, call_node->column,
                          "string.empty() takes no arguments, got %d", argc);
            return NULL;
        }
        return type_bool();
    }

    /* s.at(int i) -> int */
    if (strcmp(method, "at") == 0) {
        if (argc != 1) {
            checker_error(c, call_node->line, call_node->column,
                          "string.at() takes 1 argument, got %d", argc);
            return NULL;
        }
        Type *arg = check_expr(c, call_node->as.call.args[0]);
        if (arg && !type_is_integer(arg)) {
            checker_error(c, call_node->as.call.args[0]->line,
                          call_node->as.call.args[0]->column,
                          "string.at() index must be integer, got '%s'", type_name(arg));
            return NULL;
        }
        return type_int();
    }

    /* s.find(string sub) -> int */
    if (strcmp(method, "find") == 0) {
        if (argc != 1) {
            checker_error(c, call_node->line, call_node->column,
                          "string.find() takes 1 argument, got %d", argc);
            return NULL;
        }
        Type *arg = check_expr(c, call_node->as.call.args[0]);
        if (arg && arg->kind != TYPE_STRING) {
            checker_error(c, call_node->as.call.args[0]->line,
                          call_node->as.call.args[0]->column,
                          "string.find() argument must be string, got '%s'", type_name(arg));
            return NULL;
        }
        return type_int();
    }

    /* s.contains(string sub) -> bool */
    if (strcmp(method, "contains") == 0) {
        if (argc != 1) {
            checker_error(c, call_node->line, call_node->column,
                          "string.contains() takes 1 argument, got %d", argc);
            return NULL;
        }
        Type *arg = check_expr(c, call_node->as.call.args[0]);
        if (arg && arg->kind != TYPE_STRING) {
            checker_error(c, call_node->as.call.args[0]->line,
                          call_node->as.call.args[0]->column,
                          "string.contains() argument must be string, got '%s'", type_name(arg));
            return NULL;
        }
        return type_bool();
    }

    /* s.starts_with(string prefix) -> bool */
    if (strcmp(method, "starts_with") == 0) {
        if (argc != 1) {
            checker_error(c, call_node->line, call_node->column,
                          "string.starts_with() takes 1 argument, got %d", argc);
            return NULL;
        }
        Type *arg = check_expr(c, call_node->as.call.args[0]);
        if (arg && arg->kind != TYPE_STRING) {
            checker_error(c, call_node->as.call.args[0]->line,
                          call_node->as.call.args[0]->column,
                          "string.starts_with() argument must be string, got '%s'",
                          type_name(arg));
            return NULL;
        }
        return type_bool();
    }

    /* s.ends_with(string suffix) -> bool */
    if (strcmp(method, "ends_with") == 0) {
        if (argc != 1) {
            checker_error(c, call_node->line, call_node->column,
                          "string.ends_with() takes 1 argument, got %d", argc);
            return NULL;
        }
        Type *arg = check_expr(c, call_node->as.call.args[0]);
        if (arg && arg->kind != TYPE_STRING) {
            checker_error(c, call_node->as.call.args[0]->line,
                          call_node->as.call.args[0]->column,
                          "string.ends_with() argument must be string, got '%s'",
                          type_name(arg));
            return NULL;
        }
        return type_bool();
    }

    /* s.compare(string other) -> int */
    if (strcmp(method, "compare") == 0) {
        if (argc != 1) {
            checker_error(c, call_node->line, call_node->column,
                          "string.compare() takes 1 argument, got %d", argc);
            return NULL;
        }
        Type *arg = check_expr(c, call_node->as.call.args[0]);
        if (arg && arg->kind != TYPE_STRING) {
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
    if (strcmp(method, "upper") == 0) {
        if (argc != 0) {
            checker_error(c, call_node->line, call_node->column,
                          "string.upper() takes no arguments, got %d", argc);
            return NULL;
        }
        return type_string();
    }

    /* s.lower() -> string */
    if (strcmp(method, "lower") == 0) {
        if (argc != 0) {
            checker_error(c, call_node->line, call_node->column,
                          "string.lower() takes no arguments, got %d", argc);
            return NULL;
        }
        return type_string();
    }

    /* s.substr(int start, int len) -> string */
    if (strcmp(method, "substr") == 0) {
        if (argc != 2) {
            checker_error(c, call_node->line, call_node->column,
                          "string.substr() takes 2 arguments, got %d", argc);
            return NULL;
        }
        Type *arg0 = check_expr(c, call_node->as.call.args[0]);
        if (arg0 && !type_is_integer(arg0)) {
            checker_error(c, call_node->as.call.args[0]->line,
                          call_node->as.call.args[0]->column,
                          "string.substr() start must be integer, got '%s'",
                          type_name(arg0));
            return NULL;
        }
        Type *arg1 = check_expr(c, call_node->as.call.args[1]);
        if (arg1 && !type_is_integer(arg1)) {
            checker_error(c, call_node->as.call.args[1]->line,
                          call_node->as.call.args[1]->column,
                          "string.substr() length must be integer, got '%s'",
                          type_name(arg1));
            return NULL;
        }
        return type_string();
    }

    /* s.trim() -> string */
    if (strcmp(method, "trim") == 0) {
        if (argc != 0) {
            checker_error(c, call_node->line, call_node->column,
                          "string.trim() takes no arguments, got %d", argc);
            return NULL;
        }
        return type_string();
    }

    /* s.copy() -> string */
    if (strcmp(method, "copy") == 0) {
        if (argc != 0) {
            checker_error(c, call_node->line, call_node->column,
                          "string.copy() takes no arguments, got %d", argc);
            return NULL;
        }
        return type_string();
    }

    /* s.replace(string old, string new) -> string */
    if (strcmp(method, "replace") == 0) {
        if (argc != 2) {
            checker_error(c, call_node->line, call_node->column,
                          "string.replace() takes 2 arguments, got %d", argc);
            return NULL;
        }
        Type *arg0 = check_expr(c, call_node->as.call.args[0]);
        if (arg0 && arg0->kind != TYPE_STRING) {
            checker_error(c, call_node->as.call.args[0]->line,
                          call_node->as.call.args[0]->column,
                          "string.replace() first argument must be string, got '%s'",
                          type_name(arg0));
            return NULL;
        }
        Type *arg1 = check_expr(c, call_node->as.call.args[1]);
        if (arg1 && arg1->kind != TYPE_STRING) {
            checker_error(c, call_node->as.call.args[1]->line,
                          call_node->as.call.args[1]->column,
                          "string.replace() second argument must be string, got '%s'",
                          type_name(arg1));
            return NULL;
        }
        return type_string();
    }

    checker_error(c, call_node->line, call_node->column,
                  "string has no method '%s'", method);
    return NULL;
}

/* Type-check method calls on vec(T) objects */
static Type *check_vector_method(Checker *c, AstNode *call_node, Type *vec_type) {
    const char *method = call_node->as.call.callee->as.field_access.field;
    int argc = call_node->as.call.arg_count;
    Type *elem = vec_type->as.vec.elem;

    /* v.push(x) -> void  — append one element */
    if (strcmp(method, "push") == 0) {
        if (argc != 1) {
            checker_error(c, call_node->line, call_node->column,
                          "vec.push() takes 1 argument, got %d", argc);
            return NULL;
        }
        Type *arg = check_expr(c, call_node->as.call.args[0]);
        if (arg && !type_equals(arg, elem)) {
            checker_error(c, call_node->as.call.args[0]->line,
                          call_node->as.call.args[0]->column,
                          "vec.push() expects '%s', got '%s'",
                          type_name(elem), type_name(arg));
            return NULL;
        }
        return type_void();
    }

    /* v.pop() -> void  — remove last element (drop if needed) */
    if (strcmp(method, "pop") == 0) {
        if (argc != 0) {
            checker_error(c, call_node->line, call_node->column,
                          "vec.pop() takes no arguments, got %d", argc);
            return NULL;
        }
        return type_void();
    }

    /* v.clear() -> void  — drop all elements, set len=0 */
    if (strcmp(method, "clear") == 0) {
        if (argc != 0) {
            checker_error(c, call_node->line, call_node->column,
                          "vec.clear() takes no arguments, got %d", argc);
            return NULL;
        }
        return type_void();
    }

    /* v.reserve(n) -> void  — ensure capacity >= n */
    if (strcmp(method, "reserve") == 0) {
        if (argc != 1) {
            checker_error(c, call_node->line, call_node->column,
                          "vec.reserve() takes 1 argument, got %d", argc);
            return NULL;
        }
        Type *arg = check_expr(c, call_node->as.call.args[0]);
        if (arg && !type_is_integer(arg)) {
            checker_error(c, call_node->as.call.args[0]->line,
                          call_node->as.call.args[0]->column,
                          "vec.reserve() expects integer, got '%s'", type_name(arg));
            return NULL;
        }
        return type_void();
    }

    checker_error(c, call_node->line, call_node->column,
                  "vec has no method '%s' (available: push, pop, clear, reserve)", method);
    return NULL;
}

/* Check builtin function calls that don't belong to a type */
static Type *check_builtin_call(Checker *c, const char *name, AstNode *call_node) {
    int argc = call_node->as.call.arg_count;
    AstNode **args = call_node->as.call.args;

    /* to_string(int/f64/etc) -> string */
    if (strcmp(name, "to_string") == 0) {
        if (argc != 1) {
            checker_error(c, call_node->line, call_node->column,
                          "to_string() takes 1 argument, got %d", argc);
            return NULL;
        }
        Type *arg_type = check_expr(c, args[0]);
        if (arg_type == NULL) return NULL;
        if (!type_is_numeric(arg_type) && arg_type->kind != TYPE_BOOL) {
            checker_error(c, args[0]->line, args[0]->column,
                          "to_string() requires numeric or bool type, got '%s'",
                          type_name(arg_type));
            return NULL;
        }
        return type_string();
    }

    /* from_int(string) -> int: parse string as integer */
    if (strcmp(name, "from_int") == 0) {
        if (argc != 1) {
            checker_error(c, call_node->line, call_node->column,
                          "from_int() takes 1 argument, got %d", argc);
            return NULL;
        }
        Type *arg_type = check_expr(c, args[0]);
        if (arg_type == NULL) return NULL;
        if (arg_type->kind != TYPE_STRING) {
            checker_error(c, args[0]->line, args[0]->column,
                          "from_int() requires string type, got '%s'",
                          type_name(arg_type));
            return NULL;
        }
        return type_int();
    }

    /* from_float(string) -> f64: parse string as float */
    if (strcmp(name, "from_float") == 0) {
        if (argc != 1) {
            checker_error(c, call_node->line, call_node->column,
                          "from_float() takes 1 argument, got %d", argc);
            return NULL;
        }
        Type *arg_type = check_expr(c, args[0]);
        if (arg_type == NULL) return NULL;
        if (arg_type->kind != TYPE_STRING) {
            checker_error(c, args[0]->line, args[0]->column,
                          "from_float() requires string type, got '%s'",
                          type_name(arg_type));
            return NULL;
        }
        return type_f64();
    }

    return NULL;
}

/* Check if a name is a builtin function (so we don't report "undefined variable") */
static bool is_builtin_function(const char *name) {
    return strcmp(name, "to_string") == 0 ||
           strcmp(name, "from_int") == 0 ||
           strcmp(name, "from_float") == 0;
}

/* ---- Expression checking ---- */

static Type *check_expr(Checker *c, AstNode *node) {
    if (node == NULL) return NULL;

    Type *result = NULL;

    switch (node->kind) {
    case AST_INT_LIT:
        result = type_int();
        break;

    case AST_FLOAT_LIT:
        result = type_f64();
        break;

    case AST_STRING_LIT:
        result = type_string();
        break;

    case AST_FORMAT_STRING: {
        /* Type-check each interpolated expression */
        for (int i = 0; i < node->as.format_string.expr_count; i++) {
            Type *et = check_expr(c, node->as.format_string.exprs[i]);
            if (et == NULL) continue;
            /* Ensure the expression is a printable type */
            if (!type_is_numeric(et) && et->kind != TYPE_BOOL
                && et->kind != TYPE_STRING && et->kind != TYPE_POINTER
                && et->kind != TYPE_OBJECT) {
                checker_error(c, node->as.format_string.exprs[i]->line,
                              node->as.format_string.exprs[i]->column,
                              "cannot interpolate type '%s' in format string",
                              type_name(et));
            }
        }
        result = type_string();
        break;
    }

    case AST_ARRAY_LIT: {
        /* Infer element type from first element, check all others match */
        int count = node->as.array_lit.count;
        if (count == 0) {
            checker_error(c, node->line, node->column,
                          "empty array literal (cannot infer element type)");
            result = NULL;
            break;
        }
        Type *elem_type = check_expr(c, node->as.array_lit.elements[0]);
        if (elem_type == NULL) { result = NULL; break; }
        for (int i = 1; i < count; i++) {
            Type *et = check_expr(c, node->as.array_lit.elements[i]);
            if (et == NULL) continue;
            if (!type_equals(elem_type, et)) {
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

    case AST_IDENT: {
        /* If the identifier is a builtin function, don't report "undefined variable" */
        if (is_builtin_function(node->as.ident.name)) {
            result = NULL;  /* Signal to caller to check for builtin */
            break;
        }
        Symbol *sym = scope_resolve(c->current_scope, node->as.ident.name);
        if (sym == NULL) {
            checker_error(c, node->line, node->column,
                          "undefined variable '%s'", node->as.ident.name);
            result = NULL;
        } else {
            /* Check for use of moved value */
            if (sym->is_moved) {
                checker_error(c, node->line, node->column,
                              "use of moved value '%s'", node->as.ident.name);
            }
            result = sym->type;
        }
        break;
    }

    case AST_UNARY: {
        Type *operand = check_expr(c, node->as.unary.operand);
        if (operand == NULL) { result = NULL; break; }

        switch (node->as.unary.op) {
        case TOKEN_MINUS:
            if (!type_is_numeric(operand)) {
                checker_error(c, node->line, node->column,
                              "unary '-' requires numeric type, got '%s'", type_name(operand));
                result = NULL;
            } else {
                result = operand;
            }
            break;
        case TOKEN_BANG:
            if (operand->kind != TYPE_BOOL) {
                checker_error(c, node->line, node->column,
                              "unary '!' requires bool, got '%s'", type_name(operand));
                result = NULL;
            } else {
                result = type_bool();
            }
            break;
        case TOKEN_TILDE:
            if (!type_is_integer(operand)) {
                checker_error(c, node->line, node->column,
                              "unary '~' requires integer type, got '%s'", type_name(operand));
                result = NULL;
            } else {
                result = operand;
            }
            break;
        case TOKEN_AMP:
            /* &x -> *T */
            result = type_pointer(operand);
            break;
        case TOKEN_STAR:
            /* *ptr -> dereference */
            if (operand->kind != TYPE_POINTER) {
                checker_error(c, node->line, node->column,
                              "cannot dereference non-pointer type '%s'", type_name(operand));
                result = NULL;
            } else {
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

    case AST_BINARY: {
        Type *left = check_expr(c, node->as.binary.left);
        Type *right = check_expr(c, node->as.binary.right);
        if (left == NULL || right == NULL) { result = NULL; break; }

        switch (node->as.binary.op) {
        /* Arithmetic: +, -, *, /, % */
        case TOKEN_PLUS:
            /* Allow string + string for concatenation */
            if (left->kind == TYPE_STRING && right->kind == TYPE_STRING) {
                result = type_string();
                break;
            }
            /* fall through to numeric check */
        case TOKEN_MINUS:
        case TOKEN_STAR:
        case TOKEN_SLASH:
            if (!type_is_numeric(left) || !type_is_numeric(right)) {
                checker_error(c, node->line, node->column,
                              "arithmetic operator requires numeric types, got '%s' and '%s'",
                              type_name(left), type_name(right));
                result = NULL;
            } else if (!type_equals(left, right)) {
                checker_error(c, node->line, node->column,
                              "type mismatch in arithmetic: '%s' vs '%s'",
                              type_name(left), type_name(right));
                result = NULL;
            } else {
                result = left;
            }
            break;

        case TOKEN_PERCENT:
            if (!type_is_integer(left) || !type_is_integer(right)) {
                checker_error(c, node->line, node->column,
                              "'%%' requires integer types, got '%s' and '%s'",
                              type_name(left), type_name(right));
                result = NULL;
            } else if (!type_equals(left, right)) {
                checker_error(c, node->line, node->column,
                              "type mismatch in '%%': '%s' vs '%s'",
                              type_name(left), type_name(right));
                result = NULL;
            } else {
                result = left;
            }
            break;

        /* Bitwise: &, |, ^, <<, >> */
        case TOKEN_AMP: case TOKEN_PIPE: case TOKEN_CARET:
            if (!type_is_integer(left) || !type_is_integer(right)) {
                checker_error(c, node->line, node->column,
                              "bitwise operator requires integer types, got '%s' and '%s'",
                              type_name(left), type_name(right));
                result = NULL;
            } else if (!type_equals(left, right)) {
                checker_error(c, node->line, node->column,
                              "type mismatch in bitwise op: '%s' vs '%s'",
                              type_name(left), type_name(right));
                result = NULL;
            } else {
                result = left;
            }
            break;

        case TOKEN_LSHIFT: case TOKEN_RSHIFT:
            if (!type_is_integer(left) || !type_is_integer(right)) {
                checker_error(c, node->line, node->column,
                              "shift operator requires integer types, got '%s' and '%s'",
                              type_name(left), type_name(right));
                result = NULL;
            } else {
                result = left;
            }
            break;

        /* Comparison: ==, !=, <, >, <=, >= */
        case TOKEN_EQ: case TOKEN_NEQ:
            if (type_equals(left, right)) {
                result = type_bool();
            } else if (type_is_pointer_like(left) && type_is_pointer_like(right)) {
                /* Allow: *T == nil, object == nil, *T == object, etc. */
                result = type_bool();
            } else {
                checker_error(c, node->line, node->column,
                              "cannot compare '%s' and '%s' for equality",
                              type_name(left), type_name(right));
                result = NULL;
            }
            break;

        case TOKEN_LT: case TOKEN_GT: case TOKEN_LEQ: case TOKEN_GEQ:
            if (!type_is_numeric(left) || !type_is_numeric(right)) {
                checker_error(c, node->line, node->column,
                              "comparison requires numeric types, got '%s' and '%s'",
                              type_name(left), type_name(right));
                result = NULL;
            } else if (!type_equals(left, right)) {
                checker_error(c, node->line, node->column,
                              "type mismatch in comparison: '%s' vs '%s'",
                              type_name(left), type_name(right));
                result = NULL;
            } else {
                result = type_bool();
            }
            break;

        /* Logical: &&, || */
        case TOKEN_AND: case TOKEN_OR:
            if (left->kind != TYPE_BOOL || right->kind != TYPE_BOOL) {
                checker_error(c, node->line, node->column,
                              "logical operator requires bool, got '%s' and '%s'",
                              type_name(left), type_name(right));
                result = NULL;
            } else {
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

    case AST_CALL: {
        /* Detect struct method calls: obj.method(args) or StructName.method(args) */
        bool is_method_call = false;    /* instance method call: auto-pass self */
        bool is_static_call = false;    /* static method call via type or instance */
        const char *method_struct = NULL;

        if (node->as.call.callee->kind == AST_FIELD) {
            AstNode *obj_node = node->as.call.callee->as.field_access.object;
            const char *method_name = node->as.call.callee->as.field_access.field;

            /* Check if obj is a struct type name (static call: Point.origin()) */
            if (obj_node->kind == AST_IDENT) {
                Type *st = find_struct_type(c, obj_node->as.ident.name);
                if (st && st->kind == TYPE_STRUCT) {
                    int si = method_is_static(c, obj_node->as.ident.name, method_name);
                    if (si >= 0) {
                        method_struct = obj_node->as.ident.name;
                        is_static_call = true;
                        if (si == 0) {
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
            if (!is_static_call) {
                Type *obj_type = check_expr(c, obj_node);

                /* Intercept string builtin method calls: s.method(args...) */
                if (obj_type && obj_type->kind == TYPE_STRING) {
                    result = check_string_method(c, node, obj_type);
                    break;
                }

                /* Intercept vec builtin method calls: v.method(args...) */
                if (obj_type && obj_type->kind == TYPE_VECTOR) {
                    result = check_vector_method(c, node, obj_type);
                    break;
                }

                /* Check if obj is an instance of a struct */
                if (obj_type) {
                    Type *deref = obj_type;
                    if (deref->kind == TYPE_POINTER && deref->as.pointer_to &&
                        deref->as.pointer_to->kind == TYPE_STRUCT) {
                        deref = deref->as.pointer_to;
                    }
                    if (deref->kind == TYPE_STRUCT && deref->as.strukt.name) {
                        int si = method_is_static(c, deref->as.strukt.name, method_name);
                        if (si == 0) {
                            /* Instance method — auto self */
                            is_method_call = true;
                            method_struct = deref->as.strukt.name;
                        } else if (si == 1) {
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
        if (is_method_call || is_static_call) {
            const char *method_name = node->as.call.callee->as.field_access.field;
            callee_type = find_method(c, method_struct, method_name);
            if (callee_type == NULL) {
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
                is_method_call) {
                /* Check if the target struct has compiler-generated __drop (no user impl) */
                Type *target_struct = find_struct_type(c, method_struct);
                if (target_struct && target_struct->kind == TYPE_STRUCT &&
                    target_struct->as.strukt.has_drop) {
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
        } else {
            callee_type = check_expr(c, node->as.call.callee);
            
            /* Check builtin functions before checking function type */
            if (callee_type == NULL && node->as.call.callee->kind == AST_IDENT) {
                result = check_builtin_call(c, node->as.call.callee->as.ident.name, node);
                if (result != NULL) break;
            }
        }
        if (callee_type == NULL) { result = NULL; break; }

        if (callee_type->kind != TYPE_FUNCTION) {
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
        if (callee_type->as.function.is_vararg && user_expected == 0 && actual == 0
            && node->as.call.callee->kind == AST_IDENT
            && strcmp(node->as.call.callee->as.ident.name, "print") == 0) {
            checker_error(c, node->line, node->column,
                          "print() requires at least 1 argument");
            result = NULL;
            break;
        }

        if (callee_type->as.function.is_vararg) {
            if (actual < user_expected) {
                checker_error(c, node->line, node->column,
                              "too few arguments: expected at least %d, got %d", user_expected, actual);
                result = NULL;
                break;
            }
        } else {
            if (actual != user_expected) {
                checker_error(c, node->line, node->column,
                              "wrong number of arguments: expected %d, got %d", user_expected, actual);
                result = NULL;
                break;
            }
        }

        /* Check argument types for non-vararg params (skip self param for instance methods) */
        bool args_ok = true;

        /* First pass: check for duplicate moves in struct arguments */
        for (int i = 0; i < user_expected && i < actual; i++) {
            Type *param_type = callee_type->as.function.params[i + param_offset];
            if (param_type->kind == TYPE_STRUCT &&
                node->as.call.args[i]->kind == AST_IDENT) {
                const char *arg_name = node->as.call.args[i]->as.ident.name;
                for (int j = 0; j < i; j++) {
                    Type *prev_param_type = callee_type->as.function.params[j + param_offset];
                    if (prev_param_type->kind == TYPE_STRUCT &&
                        node->as.call.args[j]->kind == AST_IDENT &&
                        strcmp(node->as.call.args[j]->as.ident.name, arg_name) == 0) {
                        checker_error(c, node->as.call.args[i]->line,
                                      node->as.call.args[i]->column,
                                      "cannot move '%s' more than once in same call",
                                      arg_name);
                        break;
                    }
                }
            }
        }

        for (int i = 0; i < user_expected && i < actual; i++) {
            Type *arg_type = check_expr(c, node->as.call.args[i]);
            if (arg_type == NULL) { args_ok = false; continue; }
            Type *param_type = callee_type->as.function.params[i + param_offset];
            if (!type_assignable(param_type, arg_type)) {
                checker_error(c, node->as.call.args[i]->line, node->as.call.args[i]->column,
                              "argument %d: expected '%s', got '%s'",
                              i + 1,
                              type_name(param_type),
                              type_name(arg_type));
                args_ok = false;
            }

            /* Move semantics: struct value parameters trigger move.
               - param is TYPE_STRUCT (value, not pointer)
               - arg must be an identifier (variable name) or a temporary literal
               - Skip if arg was already marked as moved (error already reported above) */
            if (param_type->kind == TYPE_STRUCT &&
                node->as.call.args[i]->kind == AST_IDENT) {
                const char *arg_name = node->as.call.args[i]->as.ident.name;
                Symbol *sym = scope_resolve(c->current_scope, arg_name);
                if (sym != NULL && !sym->is_moved) {
                    sym->is_moved = true;
                }
            } else if (param_type->kind == TYPE_STRUCT &&
                       node->as.call.args[i]->kind == AST_FIELD) {
                /* Cannot move a struct field (e.g., o.foo) */
                checker_error(c, node->as.call.args[i]->line,
                              node->as.call.args[i]->column,
                              "cannot move struct field, use the struct variable instead");
            }
            /* Anonymous literals (AST_NEW_EXPR like Foo{1}) are allowed - they have no owner */
        }
        /* Check vararg args (just resolve types, no checking) */
        for (int i = user_expected; i < actual; i++) {
            check_expr(c, node->as.call.args[i]);
        }

        result = args_ok ? callee_type->as.function.return_type : NULL;
        break;
    }

    case AST_INDEX: {
        Type *obj = check_expr(c, node->as.index_expr.object);
        Type *idx = check_expr(c, node->as.index_expr.index);
        if (obj == NULL || idx == NULL) { result = NULL; break; }

        if (obj->kind == TYPE_ARRAY) {
            if (!type_is_integer(idx)) {
                checker_error(c, node->line, node->column,
                              "array index must be integer, got '%s'", type_name(idx));
                result = NULL;
            } else {
                result = obj->as.array.elem;
            }
        } else if (obj->kind == TYPE_VECTOR) {
            if (!type_is_integer(idx)) {
                checker_error(c, node->line, node->column,
                              "vec index must be integer, got '%s'", type_name(idx));
                result = NULL;
            } else {
                result = obj->as.vec.elem;
            }
        } else {
            checker_error(c, node->line, node->column,
                          "cannot index non-array/non-vec type '%s'", type_name(obj));
            result = NULL;
        }
        break;
    }

    case AST_FIELD: {
        Type *obj = check_expr(c, node->as.field_access.object);
        if (obj == NULL) { result = NULL; break; }

        const char *field_name = node->as.field_access.field;

        /* Module-qualified access (e.g., math.add) */
        if (obj->kind == TYPE_MODULE) {
            for (int i = 0; i < obj->as.module.export_count; i++) {
                if (strcmp(obj->as.module.exports[i].name, field_name) == 0) {
                    result = obj->as.module.exports[i].type;
                    break;
                }
            }
            if (result == NULL) {
                checker_error(c, node->line, node->column,
                              "module '%s' has no export '%s'",
                              obj->as.module.name ? obj->as.module.name : "<unknown>",
                              field_name);
            }
            break;
        }

        /* Array .length — compile-time constant */
        if (obj->kind == TYPE_ARRAY) {
            if (strcmp(field_name, "length") == 0) {
                result = type_int();
            } else {
                checker_error(c, node->line, node->column,
                              "array has no field '%s' (only 'length')", field_name);
                result = NULL;
            }
            break;
        }

        /* vec .length / .capacity — runtime values */
        if (obj->kind == TYPE_VECTOR) {
            if (strcmp(field_name, "length") == 0 ||
                strcmp(field_name, "capacity") == 0) {
                result = type_int();
            } else {
                checker_error(c, node->line, node->column,
                              "vec has no field '%s' (available: length, capacity)",
                              field_name);
                result = NULL;
            }
            break;
        }

        /* String .length — O(1) from LsString struct */
        if (obj->kind == TYPE_STRING) {
            if (strcmp(field_name, "length") == 0) {
                result = type_int();
            } else {
                checker_error(c, node->line, node->column,
                              "string has no field '%s'", field_name);
                result = NULL;
            }
            break;
        }

        /* Auto-dereference: *Struct → Struct for field/method access (like C++ -> ) */
        if (obj->kind == TYPE_POINTER && obj->as.pointer_to &&
            obj->as.pointer_to->kind == TYPE_STRUCT) {
            obj = obj->as.pointer_to;
        }

        if (obj->kind != TYPE_STRUCT) {
            checker_error(c, node->line, node->column,
                          "field access on non-struct type '%s'", type_name(obj));
            result = NULL;
            break;
        }

        /* Search struct fields */
        for (int i = 0; i < obj->as.strukt.field_count; i++) {
            if (strcmp(obj->as.strukt.fields[i].name, field_name) == 0) {
                result = obj->as.strukt.fields[i].type;
                break;
            }
        }

        /* Search methods if not found as field */
        if (result == NULL && obj->as.strukt.name) {
            result = find_method(c, obj->as.strukt.name, field_name);
        }

        if (result == NULL) {
            checker_error(c, node->line, node->column,
                          "struct '%s' has no field or method '%s'",
                          obj->as.strukt.name ? obj->as.strukt.name : "<anon>",
                          field_name);
        }
        break;
    }

    case AST_CLOSURE: {
        int n = node->as.closure.param_count;
        Type **params = NULL;
        if (n > 0) {
            params = (Type **)malloc_safe((size_t)n * sizeof(Type *));
            for (int i = 0; i < n; i++) {
                params[i] = resolve_type_node(c, node->as.closure.param_types[i],
                                              node->line, node->column);
            }
        }
        Type *ret = resolve_type_node(c, node->as.closure.return_type,
                                      node->line, node->column);

        /* Check body in new scope */
        push_scope(c);
        for (int i = 0; i < n; i++) {
            if (params[i]) {
                scope_define(c->current_scope, node->as.closure.param_names[i], params[i]);
            }
        }

        Type *saved_ret = c->current_fn_return;
        c->current_fn_return = ret;
        check_stmt(c, node->as.closure.body);
        c->current_fn_return = saved_ret;
        pop_scope(c);

        result = type_function(params, n, ret, false);
        break;
    }

    case AST_MATCH: {
        Type *subject = check_expr(c, node->as.match.subject);
        if (subject == NULL) { result = NULL; break; }

        Type *arm_type = NULL;
        for (int i = 0; i < node->as.match.arm_count; i++) {
            MatchArm *arm = &node->as.match.arms[i];

            /* Check pattern type matches subject */
            if (arm->pattern->kind != AST_IDENT ||
                strcmp(arm->pattern->as.ident.name, "_") != 0) {
                Type *pat_type = check_expr(c, arm->pattern);
                if (pat_type && !type_equals(pat_type, subject)) {
                    checker_error(c, arm->pattern->line, arm->pattern->column,
                                  "match pattern type '%s' doesn't match subject type '%s'",
                                  type_name(pat_type), type_name(subject));
                }
            }

            /* Check body */
            Type *body_type = check_expr(c, arm->body);
            if (body_type == NULL) continue;

            if (arm_type == NULL) {
                arm_type = body_type;
            } else if (!type_equals(arm_type, body_type)) {
                checker_error(c, arm->body->line, arm->body->column,
                              "match arm type mismatch: expected '%s', got '%s'",
                              type_name(arm_type), type_name(body_type));
            }
        }
        result = arm_type;
        break;
    }

    case AST_CAST: {
        Type *expr = check_expr(c, node->as.cast.expr);
        Type *target = resolve_type_node(c, node->as.cast.target_type,
                                         node->line, node->column);
        if (expr == NULL || target == NULL) { result = NULL; break; }

        /* Allow numeric<->numeric casts, pointer casts, and object casts */
        if (type_is_numeric(expr) && type_is_numeric(target)) {
            result = target;
        } else if (expr->kind == TYPE_POINTER && target->kind == TYPE_POINTER) {
            result = target;
        } else if (type_is_integer(expr) && target->kind == TYPE_POINTER) {
            result = target;
        } else if (expr->kind == TYPE_POINTER && type_is_integer(target)) {
            result = target;
        /* object <-> pointer: explicit cast */
        } else if (expr->kind == TYPE_OBJECT && target->kind == TYPE_POINTER) {
            result = target;
        } else if (expr->kind == TYPE_POINTER && target->kind == TYPE_OBJECT) {
            result = target;
        /* object <-> integer: explicit cast (like void* <-> intptr_t) */
        } else if (expr->kind == TYPE_OBJECT && type_is_integer(target)) {
            result = target;
        } else if (type_is_integer(expr) && target->kind == TYPE_OBJECT) {
            result = target;
        } else {
            checker_error(c, node->line, node->column,
                          "invalid cast from '%s' to '%s'",
                          type_name(expr), type_name(target));
            result = NULL;
        }
        break;
    }

    case AST_RANGE: {
        Type *start = check_expr(c, node->as.range.start);
        Type *end = check_expr(c, node->as.range.end);
        if (start == NULL || end == NULL) { result = NULL; break; }
        if (!type_is_integer(start)) {
            checker_error(c, node->as.range.start->line, node->as.range.start->column,
                          "range start must be integer, got '%s'", type_name(start));
            result = NULL; break;
        }
        if (!type_is_integer(end)) {
            checker_error(c, node->as.range.end->line, node->as.range.end->column,
                          "range end must be integer, got '%s'", type_name(end));
            result = NULL; break;
        }
        /* Range expression's resolved_type is int (the element type) */
        result = type_int();
        break;
    }

    case AST_NEW_EXPR: {
        /* Look up the struct type */
        Type *st = find_struct_type(c, node->as.new_expr.struct_name);
        if (!st) {
            checker_error(c, node->line, node->column,
                          "unknown struct type '%s'", node->as.new_expr.struct_name);
            result = NULL; break;
        }
        /* Type-check each field initializer */
        int ninits = node->as.new_expr.field_init_count;
        for (int i = 0; i < ninits; i++) {
            const char *fname = node->as.new_expr.field_inits[i].name;
            /* Check for duplicates */
            for (int j = 0; j < i; j++) {
                if (strcmp(node->as.new_expr.field_inits[j].name, fname) == 0) {
                    checker_error(c, node->line, node->column,
                                  "duplicate field initializer '%s'", fname);
                    goto new_expr_done;
                }
            }
            /* Find field in struct */
            int field_idx = -1;
            for (int j = 0; j < st->as.strukt.field_count; j++) {
                if (strcmp(st->as.strukt.fields[j].name, fname) == 0) {
                    field_idx = j;
                    break;
                }
            }
            if (field_idx < 0) {
                checker_error(c, node->line, node->column,
                              "struct '%s' has no field '%s'",
                              node->as.new_expr.struct_name, fname);
                goto new_expr_done;
            }
            /* Type-check the value */
            Type *vt = check_expr(c, node->as.new_expr.field_inits[i].value);
            if (vt && !type_equals(vt, st->as.strukt.fields[field_idx].type)) {
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
    case AST_BLOCK: {
        push_scope(c);
        Type *last = type_void();
        for (int i = 0; i < node->as.block.stmt_count; i++) {
            AstNode *s = node->as.block.stmts[i];
            if (i == node->as.block.stmt_count - 1 && s->kind == AST_EXPR_STMT) {
                last = check_expr(c, s->as.expr_stmt.expr);
            } else {
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

    if (node) node->resolved_type = result;
    return result;
}

/* ---- Statement checking ---- */

static void check_stmt(Checker *c, AstNode *node) {
    if (node == NULL) return;

    switch (node->kind) {
    case AST_VAR_DECL: {
        Type *declared = resolve_type_node(c, node->as.var_decl.var_type,
                                           node->line, node->column);
        if (declared == NULL) break;

        if (node->as.var_decl.init) {
            Type *init_type = check_expr(c, node->as.var_decl.init);
            if (init_type != NULL && !type_assignable(declared, init_type)) {
                checker_error(c, node->line, node->column,
                              "cannot initialize '%s' (type '%s') with value of type '%s'",
                              node->as.var_decl.name, type_name(declared), type_name(init_type));
            }

            /* Move semantics: if initializing a struct from another variable, mark source as moved.
               WARNING: Copying a struct with __drop() causes double-free risk! */
            if (!c->in_return_expr &&
                declared->kind == TYPE_STRUCT &&
                node->as.var_decl.init->kind == AST_IDENT) {
                Symbol *src_sym = scope_resolve(c->current_scope,
                                             node->as.var_decl.init->as.ident.name);
                if (src_sym != NULL) {
                    if (declared->as.strukt.has_drop) {
                        checker_error(c, node->line, node->column,
                                      "copy initialization of struct '%s' with __drop may cause double-free; "
                                      "consider using a pointer (*%s) instead",
                                      declared->as.strukt.name, declared->as.strukt.name);
                    }
                    src_sym->is_moved = true;
                }
            }
        }

        if (scope_resolve_local(c->current_scope, node->as.var_decl.name)) {
            checker_error(c, node->line, node->column,
                          "variable '%s' already defined in this scope",
                          node->as.var_decl.name);
        } else {
            scope_define(c->current_scope, node->as.var_decl.name, declared);
        }
        node->resolved_type = declared;
        break;
    }

    case AST_ASSIGN: {
        Type *target = check_expr(c, node->as.assign.target);
        Type *value = check_expr(c, node->as.assign.value);
        if (target == NULL || value == NULL) break;

        /* For compound assignments (+=, -=, etc.), check operand types */
        if (node->as.assign.op != TOKEN_ASSIGN) {
            if (!type_is_numeric(target)) {
                checker_error(c, node->line, node->column,
                              "compound assignment requires numeric type, got '%s'",
                              type_name(target));
                break;
            }
        }

        if (!type_assignable(target, value)) {
            checker_error(c, node->line, node->column,
                          "cannot assign '%s' to '%s'",
                           type_name(value), type_name(target));
        }

        /* Move semantics: mark source variable as moved for struct assignment.
           Skip if this assignment is inside a return expression.
           
           WARNING: Copying a struct with __drop() causes double-free risk!
           Both copies will call __drop() when they go out of scope. */
        if (node->as.assign.op == TOKEN_ASSIGN &&
            value->kind == TYPE_STRUCT &&
            !c->in_return_expr &&
            node->as.assign.value->kind == AST_IDENT) {
            Symbol *src_sym = scope_resolve(c->current_scope,
                                           node->as.assign.value->as.ident.name);
            if (src_sym != NULL) {
                /* Check if struct has __drop destructor - copy causes double-free */
                if (value->as.strukt.has_drop) {
                    checker_error(c, node->line, node->column,
                                  "copy assignment of struct '%s' with __drop may cause double-free; "
                                  "consider using a pointer (*%s) instead",
                                  value->as.strukt.name, value->as.strukt.name);
                }
                /* Also check if struct contains string fields - implicit copy forbidden */
                if (struct_has_string_fields(value)) {
                    checker_error(c, node->line, node->column,
                                  "struct '%s' contains string fields, cannot be implicitly copied; "
                                  "use explicit clone or move semantics",
                                  value->as.strukt.name);
                }
                src_sym->is_moved = true;
            }
        }
        break;
    }

    case AST_RETURN: {
        if (c->current_fn_return == NULL) {
            checker_error(c, node->line, node->column, "return outside of function");
            break;
        }
        if (node->as.return_stmt.value) {
            /* Mark as being in return expression - prevents move semantics on the returned var */
            bool saved_in_return = c->in_return_expr;
            c->in_return_expr = true;

            Type *val = check_expr(c, node->as.return_stmt.value);
            if (val != NULL && !type_assignable(c->current_fn_return, val)) {
                checker_error(c, node->line, node->column,
                              "return type mismatch: expected '%s', got '%s'",
                              type_name(c->current_fn_return), type_name(val));
            }

            /* Mark returned identifier as is_returning (skip destructor) */
            if (node->as.return_stmt.value->kind == AST_IDENT) {
                Symbol *sym = scope_resolve(c->current_scope,
                                          node->as.return_stmt.value->as.ident.name);
                if (sym != NULL) {
                    sym->is_returning = true;
                }
            }

            c->in_return_expr = saved_in_return;
        } else {
            if (c->current_fn_return->kind != TYPE_VOID) {
                checker_error(c, node->line, node->column,
                              "return without value in function returning '%s'",
                              type_name(c->current_fn_return));
            }
        }
        break;
    }

    case AST_IF: {
        Type *cond = check_expr(c, node->as.if_stmt.cond);
        if (cond != NULL && cond->kind != TYPE_BOOL) {
            checker_error(c, node->as.if_stmt.cond->line, node->as.if_stmt.cond->column,
                          "if condition must be bool, got '%s'", type_name(cond));
        }
        check_stmt(c, node->as.if_stmt.then_block);
        if (node->as.if_stmt.else_block) {
            check_stmt(c, node->as.if_stmt.else_block);
        }
        break;
    }

    case AST_WHILE: {
        Type *cond = check_expr(c, node->as.while_stmt.cond);
        if (cond != NULL && cond->kind != TYPE_BOOL) {
            checker_error(c, node->as.while_stmt.cond->line, node->as.while_stmt.cond->column,
                          "while condition must be bool, got '%s'", type_name(cond));
        }
        check_stmt(c, node->as.while_stmt.body);
        break;
    }

    case AST_FOR: {
        Type *iter = check_expr(c, node->as.for_stmt.iter);
        push_scope(c);
        if (iter != NULL) {
            if (node->as.for_stmt.iter->kind == AST_RANGE) {
                /* Range iteration: loop variable is int */
                scope_define(c->current_scope, node->as.for_stmt.var, type_int());
            } else if (iter->kind == TYPE_ARRAY) {
                /* Array iteration: loop variable is element type */
                scope_define(c->current_scope, node->as.for_stmt.var, iter->as.array.elem);
            } else if (iter->kind == TYPE_VECTOR) {
                /* Vec iteration: loop variable is element type */
                scope_define(c->current_scope, node->as.for_stmt.var, iter->as.vec.elem);
            } else if (type_is_integer(iter)) {
                /* Single integer: iterate 0..n */
                scope_define(c->current_scope, node->as.for_stmt.var, type_int());
            } else {
                checker_error(c, node->as.for_stmt.iter->line,
                              node->as.for_stmt.iter->column,
                              "cannot iterate over '%s'; expected range (a..b), array, vec, or integer",
                              type_name(iter));
            }
        }
        check_stmt(c, node->as.for_stmt.body);
        pop_scope(c);
        break;
    }

    case AST_FOR_C: {
        /* C-style for: for (init; cond; update) { body }
           All three clauses are optional. */
        push_scope(c);
        if (node->as.for_c_stmt.init) {
            check_stmt(c, node->as.for_c_stmt.init);
        }
        if (node->as.for_c_stmt.cond) {
            Type *cond = check_expr(c, node->as.for_c_stmt.cond);
            if (cond != NULL && cond->kind != TYPE_BOOL) {
                checker_error(c, node->as.for_c_stmt.cond->line,
                              node->as.for_c_stmt.cond->column,
                              "for condition must be bool, got '%s'", type_name(cond));
            }
        }
        if (node->as.for_c_stmt.update) {
            check_stmt(c, node->as.for_c_stmt.update);
        }
        check_stmt(c, node->as.for_c_stmt.body);
        pop_scope(c);
        break;
    }

    case AST_BLOCK: {
        push_scope(c);
        for (int i = 0; i < node->as.block.stmt_count; i++) {
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

static void check_fn_decl(Checker *c, AstNode *node) {
    int n = node->as.fn_decl.param_count;
    Type **params = NULL;
    if (n > 0) {
        params = (Type **)malloc_safe((size_t)n * sizeof(Type *));
        for (int i = 0; i < n; i++) {
            params[i] = resolve_type_node(c, node->as.fn_decl.param_types[i],
                                          node->line, node->column);
        }
    }
    Type *ret = resolve_type_node(c, node->as.fn_decl.return_type,
                                  node->line, node->column);
    Type *fn_type = type_function(params, n, ret, false);

    /* Define function in current scope */
    if (!scope_define(c->current_scope, node->as.fn_decl.name, fn_type)) {
        checker_error(c, node->line, node->column,
                      "function '%s' already defined", node->as.fn_decl.name);
    }

    /* Check body */
    push_scope(c);
    for (int i = 0; i < n; i++) {
        if (params[i]) {
            scope_define(c->current_scope, node->as.fn_decl.param_names[i], params[i]);
        }
    }
    Type *saved_ret = c->current_fn_return;
    c->current_fn_return = ret;
    check_stmt(c, node->as.fn_decl.body);
    c->current_fn_return = saved_ret;
    pop_scope(c);

    node->resolved_type = fn_type;
}

static void check_struct_decl(Checker *c, AstNode *node) {
    const char *name = node->as.struct_decl.name;
    int n = node->as.struct_decl.field_count;

    /* Check for duplicate struct */
    if (find_struct_type(c, name)) {
        checker_error(c, node->line, node->column,
                      "struct '%s' already defined", name);
        return;
    }

    Type *st = type_struct(name, n);
    for (int i = 0; i < n; i++) {
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
        for (int j = 0; j < i; j++) {
            if (strcmp(st->as.strukt.fields[j].name, fn) == 0) {
                checker_error(c, node->line, node->column,
                              "duplicate field '%s' in struct '%s'", fn, name);
            }
        }
    }

    /* Auto-set has_drop if struct contains string fields (for auto-destruction) */
    bool needs_drop = false;
    for (int i = 0; i < n && !needs_drop; i++) {
        Type *ft = st->as.strukt.fields[i].type;
        if (ft->kind == TYPE_STRING) {
            needs_drop = true;
        } else if (ft->kind == TYPE_STRUCT && ft->as.strukt.has_drop) {
            needs_drop = true;
        }
    }
    if (needs_drop) {
        st->as.strukt.has_drop = true;
        /* Register compiler-generated __drop method in impl_registry so it's callable */
        int impl_idx = find_or_create_impl(c, name);
        Type *drop_ret = type_void();
        /* Allocate params on heap (not stack) to avoid dangling pointer */
        Type **drop_params = (Type **)malloc_safe(sizeof(Type *));
        drop_params[0] = type_pointer(st);  /* *Struct self */
        Type *drop_type = type_function(drop_params, 1, drop_ret, false);
        register_method(c, impl_idx, "__drop", drop_type, false);
        /* Also define in global scope for free function call */
        scope_define(c->current_scope, "__drop", drop_type);
    }

    register_struct_type(c, name, st);
    node->resolved_type = st;
}

/* Check if a method call matches "self.field.__drop()" pattern.
   Used to warn when user-defined __drop() doesn't call member __drop(). */
static bool has_member_drop_call(AstNode *node, Type *struct_type) {
    if (node == NULL || struct_type == NULL) return false;
    if (struct_type->kind != TYPE_STRUCT) return false;

    if (node->kind == AST_CALL &&
        node->as.call.callee->kind == AST_FIELD) {
        AstNode *callee = node->as.call.callee;
        /* callee is "self.field.__drop" or similar */
        if (callee->as.field_access.object->kind == AST_FIELD) {
            AstNode *obj = callee->as.field_access.object;
            /* Check if this is "self.field.__drop()" */
            if (obj->as.field_access.object->kind == AST_IDENT &&
                strcmp(obj->as.field_access.object->as.ident.name, "self") == 0) {
                const char *field_name = obj->as.field_access.field;
                for (int i = 0; i < struct_type->as.strukt.field_count; i++) {
                    if (strcmp(struct_type->as.strukt.fields[i].name, field_name) == 0) {
                        Type *field_type = struct_type->as.strukt.fields[i].type;
                        /* Accept if field has has_drop (user-defined or compiler-generated) */
                        if (field_type && field_type->kind == TYPE_STRUCT &&
                            field_type->as.strukt.has_drop) {
                            if (strcmp(callee->as.field_access.field, "__drop") == 0) {
                                return true;
                            }
                        }
                    }
                }
            }
        }
    }

    /* Recursively check nested expressions */
    switch (node->kind) {
    case AST_BLOCK:
        for (int i = 0; i < node->as.block.stmt_count; i++) {
            if (has_member_drop_call(node->as.block.stmts[i], struct_type)) {
                return true;
            }
        }
        break;
    case AST_IF:
        if (has_member_drop_call(node->as.if_stmt.then_block, struct_type)) return true;
        if (node->as.if_stmt.else_block &&
            has_member_drop_call(node->as.if_stmt.else_block, struct_type)) return true;
        break;
    case AST_WHILE:
        if (has_member_drop_call(node->as.while_stmt.body, struct_type)) return true;
        break;
    case AST_FOR:
        if (has_member_drop_call(node->as.for_stmt.body, struct_type)) return true;
        break;
    case AST_EXPR_STMT:
        if (has_member_drop_call(node->as.expr_stmt.expr, struct_type)) return true;
        break;
    case AST_RETURN:
        if (has_member_drop_call(node->as.return_stmt.value, struct_type)) return true;
        break;
    default:
        break;
    }
    return false;
}

static void check_impl_decl(Checker *c, AstNode *node) {
    const char *name = node->as.impl_decl.name;
    Type *st = find_struct_type(c, name);
    if (st == NULL) {
        checker_error(c, node->line, node->column,
                      "impl for undefined struct '%s'", name);
        return;
    }

    int impl_idx = find_or_create_impl(c, name);

    for (int i = 0; i < node->as.impl_decl.method_count; i++) {
        AstNode *method = node->as.impl_decl.methods[i];
        if (method->kind != AST_FN_DECL) continue;

        bool is_static = method->as.fn_decl.is_static;
        int user_n = method->as.fn_decl.param_count;

        /* Resolve user-declared param types */
        Type **user_params = NULL;
        if (user_n > 0) {
            user_params = (Type **)malloc_safe((size_t)user_n * sizeof(Type *));
            for (int j = 0; j < user_n; j++) {
                user_params[j] = resolve_type_node(c, method->as.fn_decl.param_types[j],
                                                   method->line, method->column);
            }
        }
        Type *ret = resolve_type_node(c, method->as.fn_decl.return_type,
                                      method->line, method->column);

        /* For instance methods: internal function type has an extra first param (*Struct).
           The user doesn't write 'self' — the compiler injects it. */
        int total_n;
        Type **all_params;
        if (!is_static) {
            total_n = user_n + 1;
            all_params = (Type **)malloc_safe((size_t)total_n * sizeof(Type *));
            all_params[0] = type_pointer(st);  /* implicit *Self */
            for (int j = 0; j < user_n; j++) {
                all_params[j + 1] = user_params[j];
            }
            free(user_params);
        } else {
            total_n = user_n;
            all_params = user_params;
        }

        Type *method_type = type_function(all_params, total_n, ret, false);

        register_method(c, impl_idx, method->as.fn_decl.name, method_type, is_static);

        /* Also define in global scope so it can be called as a free function */
        scope_define(c->current_scope, method->as.fn_decl.name, method_type);

        /* Check body */
        push_scope(c);
        if (!is_static) {
            /* Inject implicit 'self' as *Struct pointer in the method scope */
            scope_define(c->current_scope, "self", type_pointer(st));
        }
        for (int j = 0; j < user_n; j++) {
            Type *pt = is_static ? all_params[j] : all_params[j + 1];
            if (pt) {
                scope_define(c->current_scope, method->as.fn_decl.param_names[j], pt);
            }
        }
        Type *saved_ret = c->current_fn_return;
        bool saved_in_drop = c->in_user_defined_drop;
        c->current_fn_return = ret;
        /* If this is a user-defined __drop method, set the flag */
        if (!is_static && strcmp(method->as.fn_decl.name, "__drop") == 0) {
            c->in_user_defined_drop = true;
        }
        check_stmt(c, method->as.fn_decl.body);
        c->current_fn_return = saved_ret;
        c->in_user_defined_drop = saved_in_drop;
        pop_scope(c);

        method->resolved_type = method_type;

        /* Check for __drop destructor method */
        if (!is_static &&
            strcmp(method->as.fn_decl.name, "__drop") == 0) {
            /* __drop must be an instance method with no user parameters and void return */
            if (user_n != 0) {
                checker_error(c, method->line, method->column,
                              "__drop() must have no parameters (self is implicit)");
            }
            if (ret->kind != TYPE_VOID) {
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

static void check_extern_fn(Checker *c, AstNode *node) {
    int n = node->as.extern_fn.param_count;
    Type **params = NULL;
    if (n > 0) {
        params = (Type **)malloc_safe((size_t)n * sizeof(Type *));
        for (int i = 0; i < n; i++) {
            params[i] = resolve_type_node(c, node->as.extern_fn.param_types[i],
                                          node->line, node->column);
        }
    }
    Type *ret = resolve_type_node(c, node->as.extern_fn.return_type,
                                  node->line, node->column);
    Type *fn_type = type_function(params, n, ret, node->as.extern_fn.is_vararg);

    if (!scope_define(c->current_scope, node->as.extern_fn.name, fn_type)) {
        checker_error(c, node->line, node->column,
                      "extern function '%s' already defined", node->as.extern_fn.name);
    }
    node->resolved_type = fn_type;
}

static void check_load_lib(Checker *c, AstNode *node) {
    if (!scope_define(c->current_scope, node->as.load_lib.var_name, type_lib())) {
        checker_error(c, node->line, node->column,
                      "library '%s' already defined", node->as.load_lib.var_name);
    }
    node->resolved_type = type_lib();
}

static void check_decl(Checker *c, AstNode *node) {
    if (node == NULL) return;

    switch (node->kind) {
    case AST_FN_DECL:
        check_fn_decl(c, node);
        break;
    case AST_STRUCT_DECL:
        check_struct_decl(c, node);
        break;
    case AST_IMPL_DECL:
        check_impl_decl(c, node);
        break;
    case AST_EXTERN_FN:
        check_extern_fn(c, node);
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
    case AST_FFI_CALL:
        /* FFI dynamic call: check lib expr, skip type checking of args */
        check_expr(c, node->as.ffi_call.lib_expr);
        for (int i = 0; i < node->as.ffi_call.arg_count; i++) {
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
static void forward_pass(Checker *c, AstNode *program) {
    for (int i = 0; i < program->as.program.decl_count; i++) {
        AstNode *decl = program->as.program.decls[i];
        switch (decl->kind) {
        case AST_STRUCT_DECL:
            check_struct_decl(c, decl);
            break;
        case AST_FN_DECL: {
            /* Register function signature only (don't check body yet) */
            int n = decl->as.fn_decl.param_count;
            Type **params = NULL;
            if (n > 0) {
                params = (Type **)malloc_safe((size_t)n * sizeof(Type *));
                for (int j = 0; j < n; j++) {
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
        case AST_LOAD_LIB:
            check_load_lib(c, decl);
            break;
        case AST_IMPORT_DECL: {
            if (c->registry == NULL) break;
            const char *import_path = decl->as.import_decl.path;

            /* Circular import detection */
            if (module_is_importing(c->registry, import_path)) {
                checker_error(c, decl->line, decl->column,
                              "circular import detected: '%s'", import_path);
                break;
            }

            /* Load module (parse if not already loaded) */
            ModuleInfo *mod = module_load(c->registry, import_path, c->source_path);
            if (mod == NULL) {
                checker_error(c, decl->line, decl->column,
                              "cannot find module '%s'", import_path);
                break;
            }

            /* Type-check the module if not already checked */
            if (!mod->checked) {
                module_push_import(c->registry, import_path);
                bool ok = checker_check(mod->ast, mod->file_path, c->registry);
                module_pop_import(c->registry);
                if (!ok) {
                    checker_error(c, decl->line, decl->column,
                                  "errors in imported module '%s'", import_path);
                    break;
                }
                mod->checked = true;
            }

            /* Collect exported symbols from the module */
            Type *mod_type = type_module_new(import_path);
            AstNode *mod_ast = mod->ast;
            for (int j = 0; j < mod_ast->as.program.decl_count; j++) {
                AstNode *d = mod_ast->as.program.decls[j];
                if (d->kind == AST_FN_DECL && d->resolved_type) {
                    type_module_add_export(mod_type,
                        d->as.fn_decl.name, d->resolved_type);
                } else if (d->kind == AST_STRUCT_DECL && d->resolved_type) {
                    type_module_add_export(mod_type,
                        d->as.struct_decl.name, d->resolved_type);
                } else if (d->kind == AST_VAR_DECL && d->resolved_type) {
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
static void check_pass(Checker *c, AstNode *program) {
    for (int i = 0; i < program->as.program.decl_count; i++) {
        AstNode *decl = program->as.program.decls[i];
        switch (decl->kind) {
        case AST_STRUCT_DECL:
            /* Already handled in forward pass */
            break;
        case AST_FN_DECL: {
            /* Check function body (signature already registered) */
            Type *fn_type = decl->resolved_type;
            if (fn_type == NULL || fn_type->kind != TYPE_FUNCTION) break;

            push_scope(c);
            for (int j = 0; j < decl->as.fn_decl.param_count; j++) {
                scope_define(c->current_scope, decl->as.fn_decl.param_names[j],
                             fn_type->as.function.params[j]);
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

static void register_builtins(Checker *c) {
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
                   struct ModuleRegistry *registry) {
    if (program == NULL || program->kind != AST_PROGRAM) return false;

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
    for (int i = 0; i < c.impl_count; i++) {
        free(c.impl_registry[i].methods);
    }
    free(c.impl_registry);

    return !c.had_error;
}
