/* types.c — Semantic type representation, comparison, and utilities */
#include "types.h"
#include <stdio.h>
#include <string.h>

/* ---- Primitive singletons ---- */

static Type PRIM_INT    = { TYPE_INT,    {{0}} };
static Type PRIM_I8     = { TYPE_I8,     {{0}} };
static Type PRIM_I16    = { TYPE_I16,    {{0}} };
static Type PRIM_I32    = { TYPE_I32,    {{0}} };
static Type PRIM_I64    = { TYPE_I64,    {{0}} };
static Type PRIM_U8     = { TYPE_U8,     {{0}} };
static Type PRIM_U16    = { TYPE_U16,    {{0}} };
static Type PRIM_U32    = { TYPE_U32,    {{0}} };
static Type PRIM_U64    = { TYPE_U64,    {{0}} };
static Type PRIM_F32    = { TYPE_F32,    {{0}} };
static Type PRIM_F64    = { TYPE_F64,    {{0}} };
static Type PRIM_BOOL   = { TYPE_BOOL,   {{0}} };
static Type PRIM_STRING = { TYPE_STRING, {{0}} };
static Type PRIM_VOID   = { TYPE_VOID,   {{0}} };
static Type PRIM_NIL    = { TYPE_NIL,    {{0}} };
static Type PRIM_LIB    = { TYPE_LIB,    {{0}} };
static Type PRIM_OBJECT = { TYPE_OBJECT, {{0}} };

Type *type_int(void)    { return &PRIM_INT; }
Type *type_i8(void)     { return &PRIM_I8; }
Type *type_i16(void)    { return &PRIM_I16; }
Type *type_i32(void)    { return &PRIM_I32; }
Type *type_i64(void)    { return &PRIM_I64; }
Type *type_u8(void)     { return &PRIM_U8; }
Type *type_u16(void)    { return &PRIM_U16; }
Type *type_u32(void)    { return &PRIM_U32; }
Type *type_u64(void)    { return &PRIM_U64; }
Type *type_f32(void)    { return &PRIM_F32; }
Type *type_f64(void)    { return &PRIM_F64; }
Type *type_bool(void)   { return &PRIM_BOOL; }
Type *type_string(void) { return &PRIM_STRING; }
Type *type_void(void)   { return &PRIM_VOID; }
Type *type_nil(void)    { return &PRIM_NIL; }
Type *type_lib(void)    { return &PRIM_LIB; }
Type *type_object(void) { return &PRIM_OBJECT; }

/* ---- Is singleton? ---- */

static bool is_singleton(const Type *t) {
    return t == &PRIM_INT  || t == &PRIM_I8  || t == &PRIM_I16 ||
           t == &PRIM_I32  || t == &PRIM_I64 || t == &PRIM_U8  ||
           t == &PRIM_U16  || t == &PRIM_U32 || t == &PRIM_U64 ||
           t == &PRIM_F32  || t == &PRIM_F64 || t == &PRIM_BOOL ||
           t == &PRIM_STRING || t == &PRIM_VOID || t == &PRIM_NIL ||
           t == &PRIM_LIB || t == &PRIM_OBJECT;
}

/* ---- Composite constructors ---- */

Type *type_pointer(Type *pointee) {
    Type *t = (Type *)malloc_safe(sizeof(Type));
    memset(t, 0, sizeof(Type));
    t->kind = TYPE_POINTER;
    t->as.pointer_to = pointee;
    return t;
}

Type *type_array(Type *elem, int size) {
    Type *t = (Type *)malloc_safe(sizeof(Type));
    memset(t, 0, sizeof(Type));
    t->kind = TYPE_ARRAY;
    t->as.array.elem = elem;
    t->as.array.size = size;
    return t;
}

Type *type_vector(Type *elem) {
    Type *t = (Type *)malloc_safe(sizeof(Type));
    memset(t, 0, sizeof(Type));
    t->kind = TYPE_VECTOR;
    t->as.vec.elem = elem;
    return t;
}

Type *type_function(Type **params, int param_count, Type *return_type, bool is_vararg) {
    Type *t = (Type *)malloc_safe(sizeof(Type));
    memset(t, 0, sizeof(Type));
    t->kind = TYPE_FUNCTION;
    t->as.function.params = params;
    t->as.function.param_count = param_count;
    t->as.function.return_type = return_type;
    t->as.function.is_vararg = is_vararg;
    return t;
}

Type *type_struct(const char *name, int field_count) {
    Type *t = (Type *)malloc_safe(sizeof(Type));
    memset(t, 0, sizeof(Type));
    t->kind = TYPE_STRUCT;
    t->as.strukt.name = name;
    t->as.strukt.field_count = field_count;
    if (field_count > 0) {
        size_t sz = (size_t)field_count * sizeof(t->as.strukt.fields[0]);
        t->as.strukt.fields = malloc_safe(sz);
        memset(t->as.strukt.fields, 0, sz);
    }
    return t;
}

/* ---- Module type ---- */

Type *type_module_new(const char *name) {
    Type *t = (Type *)malloc_safe(sizeof(Type));
    memset(t, 0, sizeof(Type));
    t->kind = TYPE_MODULE;
    t->as.module.name = name;  /* not owned — must outlive the type */
    t->as.module.exports = NULL;
    t->as.module.export_count = 0;
    return t;
}

void type_module_add_export(Type *mod, const char *name, Type *type) {
    if (mod == NULL || mod->kind != TYPE_MODULE) return;
    int n = mod->as.module.export_count;
    mod->as.module.exports = realloc_safe(mod->as.module.exports,
        (size_t)(n + 1) * sizeof(mod->as.module.exports[0]));
    mod->as.module.exports[n].name = name;
    mod->as.module.exports[n].type = type;
    mod->as.module.export_count = n + 1;
}

/* ---- Deep clone ---- */

Type *type_clone(const Type *t) {
    if (t == NULL) return NULL;
    if (is_singleton(t)) return (Type *)t;

    Type *c = (Type *)malloc_safe(sizeof(Type));
    memset(c, 0, sizeof(Type));
    c->kind = t->kind;

    switch (t->kind) {
    case TYPE_POINTER:
        c->as.pointer_to = type_clone(t->as.pointer_to);
        break;
    case TYPE_ARRAY:
        c->as.array.elem = type_clone(t->as.array.elem);
        c->as.array.size = t->as.array.size;
        break;
    case TYPE_VECTOR:
        c->as.vec.elem = type_clone(t->as.vec.elem);
        break;
    case TYPE_FUNCTION: {
        int n = t->as.function.param_count;
        Type **params = NULL;
        if (n > 0) {
            params = (Type **)malloc_safe((size_t)n * sizeof(Type *));
            for (int i = 0; i < n; i++) {
                params[i] = type_clone(t->as.function.params[i]);
            }
        }
        c->as.function.params = params;
        c->as.function.param_count = n;
        c->as.function.return_type = type_clone(t->as.function.return_type);
        c->as.function.is_vararg = t->as.function.is_vararg;
        break;
    }
    case TYPE_STRUCT: {
        int n = t->as.strukt.field_count;
        char *name_copy = NULL;
        if (t->as.strukt.name) {
            size_t len = strlen(t->as.strukt.name);
            name_copy = (char *)malloc_safe(len + 1);
            memcpy(name_copy, t->as.strukt.name, len + 1);
        }
        c->as.strukt.name = name_copy;
        c->as.strukt.field_count = n;
        if (n > 0) {
            size_t sz = (size_t)n * sizeof(c->as.strukt.fields[0]);
            c->as.strukt.fields = malloc_safe(sz);
            for (int i = 0; i < n; i++) {
                const char *fn = t->as.strukt.fields[i].name;
                char *fn_copy = NULL;
                if (fn) {
                    size_t flen = strlen(fn);
                    fn_copy = (char *)malloc_safe(flen + 1);
                    memcpy(fn_copy, fn, flen + 1);
                }
                c->as.strukt.fields[i].name = fn_copy;
                c->as.strukt.fields[i].type = type_clone(t->as.strukt.fields[i].type);
            }
        }
        break;
    }
    default:
        /* Primitive kind but not a singleton (shouldn't happen, but safe) */
        break;
    }
    return c;
}

/* ---- Free ---- */

void type_free(Type *t) {
    if (t == NULL || is_singleton(t)) return;

    switch (t->kind) {
    case TYPE_POINTER:
        type_free(t->as.pointer_to);
        break;
    case TYPE_ARRAY:
        type_free(t->as.array.elem);
        break;
    case TYPE_VECTOR:
        type_free(t->as.vec.elem);
        break;
    case TYPE_FUNCTION:
        for (int i = 0; i < t->as.function.param_count; i++) {
            type_free(t->as.function.params[i]);
        }
        free(t->as.function.params);
        type_free(t->as.function.return_type);
        break;
    case TYPE_STRUCT:
        for (int i = 0; i < t->as.strukt.field_count; i++) {
            free((void *)t->as.strukt.fields[i].name);
            type_free(t->as.strukt.fields[i].type);
        }
        free(t->as.strukt.fields);
        free((void *)t->as.strukt.name);
        break;
    case TYPE_MODULE:
        /* exports are not owned — don't free them, just the array */
        free(t->as.module.exports);
        break;
    default:
        break;
    }
    free(t);
}

/* ---- Equality ---- */

bool type_equals(const Type *a, const Type *b) {
    if (a == b) return true;
    if (a == NULL || b == NULL) return false;
    if (a->kind != b->kind) return false;

    switch (a->kind) {
    case TYPE_POINTER:
        return type_equals(a->as.pointer_to, b->as.pointer_to);
    case TYPE_ARRAY:
        return a->as.array.size == b->as.array.size &&
               type_equals(a->as.array.elem, b->as.array.elem);
    case TYPE_VECTOR:
        return type_equals(a->as.vec.elem, b->as.vec.elem);
    case TYPE_FUNCTION:
        if (a->as.function.param_count != b->as.function.param_count) return false;
        if (a->as.function.is_vararg != b->as.function.is_vararg) return false;
        if (!type_equals(a->as.function.return_type, b->as.function.return_type)) return false;
        for (int i = 0; i < a->as.function.param_count; i++) {
            if (!type_equals(a->as.function.params[i], b->as.function.params[i])) return false;
        }
        return true;
    case TYPE_STRUCT:
        /* Struct equality is by name (nominal typing) */
        if (a->as.strukt.name && b->as.strukt.name)
            return strcmp(a->as.strukt.name, b->as.strukt.name) == 0;
        return false;
    default:
        /* Primitive types: same kind = equal */
        return true;
    }
}

/* ---- Type queries ---- */

bool type_is_integer(const Type *t) {
    if (t == NULL) return false;
    switch (t->kind) {
    case TYPE_INT: case TYPE_I8: case TYPE_I16: case TYPE_I32: case TYPE_I64:
    case TYPE_U8: case TYPE_U16: case TYPE_U32: case TYPE_U64:
        return true;
    default:
        return false;
    }
}

bool type_is_float(const Type *t) {
    if (t == NULL) return false;
    return t->kind == TYPE_F32 || t->kind == TYPE_F64;
}

bool type_is_numeric(const Type *t) {
    return type_is_integer(t) || type_is_float(t);
}

bool type_is_signed(const Type *t) {
    if (t == NULL) return false;
    switch (t->kind) {
    case TYPE_INT: case TYPE_I8: case TYPE_I16: case TYPE_I32: case TYPE_I64:
        return true;
    default:
        return false;
    }
}

bool type_is_unsigned(const Type *t) {
    if (t == NULL) return false;
    switch (t->kind) {
    case TYPE_U8: case TYPE_U16: case TYPE_U32: case TYPE_U64:
        return true;
    default:
        return false;
    }
}

bool type_is_pointer_like(const Type *t) {
    if (t == NULL) return false;
    switch (t->kind) {
    case TYPE_POINTER: case TYPE_OBJECT: case TYPE_NIL:
        return true;
    default:
        return false;
    }
}

/* ---- type_name ---- */

/* Return a human-readable string for a type (uses rotating static buffers
   so that two calls can be used in the same printf/snprintf without
   the second overwriting the first) */
const char *type_name(const Type *t) {
    #define TYPE_NAME_BUFS 4
    static char bufs[TYPE_NAME_BUFS][256];
    static int buf_idx = 0;
    char *buf = bufs[buf_idx++ % TYPE_NAME_BUFS];

    if (t == NULL) return "void";

    switch (t->kind) {
    case TYPE_INT:    return "int";
    case TYPE_I8:     return "i8";
    case TYPE_I16:    return "i16";
    case TYPE_I32:    return "i32";
    case TYPE_I64:    return "i64";
    case TYPE_U8:     return "u8";
    case TYPE_U16:    return "u16";
    case TYPE_U32:    return "u32";
    case TYPE_U64:    return "u64";
    case TYPE_F32:    return "f32";
    case TYPE_F64:    return "f64";
    case TYPE_BOOL:   return "bool";
    case TYPE_STRING: return "string";
    case TYPE_VOID:   return "void";
    case TYPE_NIL:    return "nil";
    case TYPE_LIB:    return "lib";
    case TYPE_OBJECT: return "object";
    case TYPE_POINTER:
        snprintf(buf, 256, "*%s", type_name(t->as.pointer_to));
        return buf;
    case TYPE_ARRAY:
        snprintf(buf, 256, "array(%s, %d)", type_name(t->as.array.elem), t->as.array.size);
        return buf;
    case TYPE_VECTOR:
        snprintf(buf, 256, "vec(%s)", type_name(t->as.vec.elem));
        return buf;
    case TYPE_FUNCTION: {
        int pos = snprintf(buf, 256, "fn(");
        for (int i = 0; i < t->as.function.param_count && pos < (int)256 - 10; i++) {
            if (i > 0) pos += snprintf(buf + pos, 256 - (size_t)pos, ", ");
            pos += snprintf(buf + pos, 256 - (size_t)pos, "%s",
                            type_name(t->as.function.params[i]));
        }
        if (t->as.function.is_vararg) {
            pos += snprintf(buf + pos, 256 - (size_t)pos, ", ...");
        }
        snprintf(buf + pos, 256 - (size_t)pos, ") -> %s",
                 type_name(t->as.function.return_type));
        return buf;
    }
    case TYPE_STRUCT:
        if (t->as.strukt.name) return t->as.strukt.name;
        return "<struct>";
    case TYPE_MODULE:
        if (t->as.module.name) {
            snprintf(buf, 256, "module(%s)", t->as.module.name);
            return buf;
        }
        return "module";
    }
    return "<unknown>";
}
