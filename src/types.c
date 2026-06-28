/* types.c — Semantic type representation, comparison, and utilities */
#include "types.h"
#include <stdio.h>
#include <string.h>

/* ---- Primitive singletons ---- */

static Type PRIM_INT    = { TYPE_INT,    false, {{0}} };
static Type PRIM_I8     = { TYPE_I8,     false, {{0}} };
static Type PRIM_I16    = { TYPE_I16,    false, {{0}} };
static Type PRIM_I32    = { TYPE_I32,    false, {{0}} };
static Type PRIM_I64    = { TYPE_I64,    false, {{0}} };
static Type PRIM_U8     = { TYPE_U8,     false, {{0}} };
static Type PRIM_U16    = { TYPE_U16,    false, {{0}} };
static Type PRIM_U32    = { TYPE_U32,    false, {{0}} };
static Type PRIM_U64    = { TYPE_U64,    false, {{0}} };
static Type PRIM_F32    = { TYPE_F32,    false, {{0}} };
static Type PRIM_F64    = { TYPE_F64,    false, {{0}} };
static Type PRIM_F16    = { TYPE_F16,    false, {{0}} };
static Type PRIM_BF16   = { TYPE_BF16,   false, {{0}} };
static Type PRIM_BOOL   = { TYPE_BOOL,   false, {{0}} };
static Type PRIM_CHAR   = { TYPE_CHAR,   false, {{0}} };
static Type PRIM_VOID   = { TYPE_VOID,   false, {{0}} };
static Type PRIM_NIL    = { TYPE_NIL,    false, {{0}} };
static Type PRIM_LIB    = { TYPE_LIB,    false, {{0}} };
static Type PRIM_OBJECT = { TYPE_OBJECT, false, {{0}} };

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
Type *type_f16(void)    { return &PRIM_F16; }
Type *type_bf16(void)   { return &PRIM_BF16; }
Type *type_bool(void)   { return &PRIM_BOOL; }
Type *type_char(void)   { return &PRIM_CHAR; }
Type *type_void(void)   { return &PRIM_VOID; }
Type *type_nil(void)    { return &PRIM_NIL; }
Type *type_lib(void)    { return &PRIM_LIB; }
Type *type_object(void) { return &PRIM_OBJECT; }

/* ---- Is singleton? ---- */

static bool is_singleton(const Type *t) {
    return t == &PRIM_INT  || t == &PRIM_I8  || t == &PRIM_I16 ||
           t == &PRIM_I32  || t == &PRIM_I64 || t == &PRIM_U8  ||
           t == &PRIM_U16  || t == &PRIM_U32 || t == &PRIM_U64 ||
           t == &PRIM_F32  || t == &PRIM_F64 || t == &PRIM_F16 ||
           t == &PRIM_BF16 || t == &PRIM_BOOL ||
           t == &PRIM_CHAR || t == &PRIM_VOID ||
           t == &PRIM_NIL || t == &PRIM_LIB || t == &PRIM_OBJECT;
}

/* ---- Composite constructors ---- */

Type *type_pointer(Type *pointee) {
    Type *t = (Type *)malloc_safe(sizeof(Type));
    memset(t, 0, sizeof(Type));
    t->kind = TYPE_POINTER;
    t->as.pointer_to = pointee;
    return t;
}

Type *type_reference(Type *pointee) {
    Type *t = (Type *)malloc_safe(sizeof(Type));
    memset(t, 0, sizeof(Type));
    t->kind = TYPE_REFERENCE;
    t->is_mut = false;
    t->as.pointer_to = pointee;
    return t;
}

Type *type_mut_reference(Type *pointee) {
    Type *t = (Type *)malloc_safe(sizeof(Type));
    memset(t, 0, sizeof(Type));
    t->kind = TYPE_REFERENCE;
    t->is_mut = true;
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

Type *type_simd(Type *elem, int lanes) {
    Type *t = (Type *)malloc_safe(sizeof(Type));
    memset(t, 0, sizeof(Type));
    t->kind = TYPE_SIMD;
    t->as.simd.elem = elem;
    t->as.simd.lanes = lanes;
    return t;
}

/* &[T] (is_mut=false) / &![T] (is_mut=true): a borrowed {ptr,len} slice over a
   contiguous range. Element type stored in as.array.elem (size unused). */
Type *type_slice(Type *elem, bool is_mut) {
    Type *t = (Type *)malloc_safe(sizeof(Type));
    memset(t, 0, sizeof(Type));
    t->kind = TYPE_SLICE;
    t->is_mut = is_mut;
    t->as.array.elem = elem;
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

Type *type_block(Type **params, int param_count, Type *return_type) {
    Type *t = (Type *)malloc_safe(sizeof(Type));
    memset(t, 0, sizeof(Type));
    t->kind = TYPE_BLOCK;
    t->as.function.params = params;
    t->as.function.param_count = param_count;
    t->as.function.return_type = return_type;
    t->as.function.is_vararg = false;
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

Type *type_enum(const char *name, int variant_count) {
    Type *t = (Type *)malloc_safe(sizeof(Type));
    memset(t, 0, sizeof(Type));
    t->kind = TYPE_ENUM;
    /* Duplicate name so callers can free their source string independently. */
    if (name) {
        size_t len = strlen(name);
        char *copy = (char *)malloc_safe(len + 1);
        memcpy(copy, name, len + 1);
        t->as.enom.name = copy;
    }
    t->as.enom.variant_count = variant_count;
    if (variant_count > 0) {
        size_t sz = (size_t)variant_count * sizeof(t->as.enom.variants[0]);
        t->as.enom.variants = malloc_safe(sz);
        memset(t->as.enom.variants, 0, sz);
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

/* B-4: look up an exported symbol's type by name. Returns NULL if absent. */
Type *type_module_find_export(Type *mod, const char *name) {
    if (mod == NULL || mod->kind != TYPE_MODULE || name == NULL) return NULL;
    for (int i = 0; i < mod->as.module.export_count; i++) {
        if (mod->as.module.exports[i].name &&
            strcmp(mod->as.module.exports[i].name, name) == 0)
            return mod->as.module.exports[i].type;
    }
    return NULL;
}

/* ---- Deep clone ---- */

Type *type_clone(const Type *t) {
    if (t == NULL) return NULL;
    if (is_singleton(t)) return (Type *)t;

    Type *c = (Type *)malloc_safe(sizeof(Type));
    memset(c, 0, sizeof(Type));
    c->kind = t->kind;
    c->is_mut = t->is_mut;

    switch (t->kind) {
    case TYPE_POINTER:
    case TYPE_REFERENCE:
        c->as.pointer_to = type_clone(t->as.pointer_to);
        break;
    case TYPE_ARRAY:
    case TYPE_SLICE:
        c->as.array.elem = type_clone(t->as.array.elem);
        c->as.array.size = t->as.array.size;
        break;
    case TYPE_SIMD:
        c->as.simd.elem = type_clone(t->as.simd.elem);
        c->as.simd.lanes = t->as.simd.lanes;
        break;
    case TYPE_FUNCTION:
    case TYPE_BLOCK: {
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
        /* AstNode* owned by the AST; shallow-copy the pointer array. */
        if (t->as.function.param_defaults && n > 0) {
            c->as.function.param_defaults = (void **)malloc_safe((size_t)n * sizeof(void *));
            for (int i = 0; i < n; i++)
                c->as.function.param_defaults[i] = t->as.function.param_defaults[i];
        }
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
                /* AstNode* owned by the AST; shallow-copy the reference. */
                c->as.strukt.fields[i].default_expr = t->as.strukt.fields[i].default_expr;
                c->as.strukt.fields[i].is_private = t->as.strukt.fields[i].is_private;
            }
        }
        break;
    }
    case TYPE_ENUM: {
        int n = t->as.enom.variant_count;
        char *name_copy = NULL;
        if (t->as.enom.name) {
            size_t len = strlen(t->as.enom.name);
            name_copy = (char *)malloc_safe(len + 1);
            memcpy(name_copy, t->as.enom.name, len + 1);
        }
        c->as.enom.name = name_copy;
        c->as.enom.variant_count = n;
        c->as.enom.has_drop = t->as.enom.has_drop;
        if (n > 0) {
            size_t sz = (size_t)n * sizeof(c->as.enom.variants[0]);
            c->as.enom.variants = malloc_safe(sz);
            memset(c->as.enom.variants, 0, sz);
            for (int i = 0; i < n; i++) {
                const char *vn = t->as.enom.variants[i].name;
                char *vn_copy = NULL;
                if (vn) {
                    size_t vlen = strlen(vn);
                    vn_copy = (char *)malloc_safe(vlen + 1);
                    memcpy(vn_copy, vn, vlen + 1);
                }
                c->as.enom.variants[i].name = vn_copy;
                int pc = t->as.enom.variants[i].payload_count;
                c->as.enom.variants[i].payload_count = pc;
                if (pc > 0) {
                    c->as.enom.variants[i].payload_types =
                        (Type **)malloc_safe((size_t)pc * sizeof(Type *));
                    for (int j = 0; j < pc; j++) {
                        c->as.enom.variants[i].payload_types[j] =
                            type_clone(t->as.enom.variants[i].payload_types[j]);
                    }
                }
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
    case TYPE_REFERENCE:
        type_free(t->as.pointer_to);
        break;
    case TYPE_ARRAY:
    case TYPE_SLICE:
        type_free(t->as.array.elem);
        break;
    case TYPE_SIMD:
        type_free(t->as.simd.elem);
        break;
    case TYPE_FUNCTION:
    case TYPE_BLOCK:
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
    case TYPE_ENUM:
        for (int i = 0; i < t->as.enom.variant_count; i++) {
            free((void *)t->as.enom.variants[i].name);
            for (int j = 0; j < t->as.enom.variants[i].payload_count; j++) {
                type_free(t->as.enom.variants[i].payload_types[j]);
            }
            free(t->as.enom.variants[i].payload_types);
        }
        free(t->as.enom.variants);
        free((void *)t->as.enom.name);
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
    case TYPE_REFERENCE:
        /* &T and &!T are distinct types */
        if (a->is_mut != b->is_mut) return false;
        return type_equals(a->as.pointer_to, b->as.pointer_to);
    case TYPE_ARRAY:
        return a->as.array.size == b->as.array.size &&
               type_equals(a->as.array.elem, b->as.array.elem);
    case TYPE_SIMD:
        return a->as.simd.lanes == b->as.simd.lanes &&
               type_equals(a->as.simd.elem, b->as.simd.elem);
    case TYPE_SLICE:
        /* &[T] and &![T] are distinct (mut differs). */
        return a->is_mut == b->is_mut &&
               type_equals(a->as.array.elem, b->as.array.elem);
    case TYPE_FUNCTION:
    case TYPE_BLOCK:
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
    case TYPE_ENUM:
        /* Enum equality is by mangled name (e.g. "Option(int)" vs "Option(string)"
           are distinct nominal types post-monomorphization) */
        if (a->as.enom.name && b->as.enom.name)
            return strcmp(a->as.enom.name, b->as.enom.name) == 0;
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
    case TYPE_CHAR:
        return true;
    default:
        return false;
    }
}

bool type_is_float(const Type *t) {
    if (t == NULL) return false;
    return t->kind == TYPE_F32 || t->kind == TYPE_F64 ||
           t->kind == TYPE_F16 || t->kind == TYPE_BF16;
}

/* Float rank for widening: f16/bf16 < f32 < f64 (f16 and bf16 are both 16-bit
   but incompatible — neither widens to the other). */
static int type_float_rank(const Type *t) {
    if (t->kind == TYPE_F64) return 3;
    if (t->kind == TYPE_F32) return 2;
    return 1;  /* f16 / bf16 */
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

int type_int_bits(const Type *t) {
    if (t == NULL) return 0;
    switch (t->kind) {
    case TYPE_I8:  case TYPE_U8:  case TYPE_CHAR: return 8;
    case TYPE_I16: case TYPE_U16: return 16;
    case TYPE_INT: case TYPE_I32: case TYPE_U32: return 32;
    case TYPE_I64: case TYPE_U64: return 64;
    default: return 0;
    }
}

int type_float_mantissa_bits(const Type *t) {
    if (t == NULL) return 0;
    if (t->kind == TYPE_F32) return 24;  /* IEEE 754 binary32 */
    if (t->kind == TYPE_F64) return 53;  /* IEEE 754 binary64 */
    if (t->kind == TYPE_F16) return 11;  /* IEEE 754 binary16 (10 stored + implicit) */
    if (t->kind == TYPE_BF16) return 8;  /* bfloat16 (7 stored + implicit) */
    return 0;
}

bool type_widens_to(const Type *src, const Type *dst) {
    if (src == NULL || dst == NULL) return false;
    if (type_equals(src, dst)) return false;  /* trivial: not a widening */

    /* Float → float: widen up the rank (f16/bf16 → f32 → f64). f16 and bf16
       share rank 1, so neither widens to the other (incompatible 16-bit formats). */
    if (type_is_float(src) && type_is_float(dst)) {
        return type_float_rank(dst) > type_float_rank(src);
    }

    /* Int → float: dst's mantissa must hold every value of src. */
    if (type_is_integer(src) && type_is_float(dst)) {
        int sb = type_int_bits(src);
        int mb = type_float_mantissa_bits(dst);
        /* Signed: needs sb bits including sign; mantissa stores absolute
           value so sb-1 magnitude bits must fit within mb. We use sb here
           (worst case INT_MIN has sb-1 magnitude); conservative & safe. */
        return sb > 0 && mb > 0 && sb <= mb;
    }

    /* Int → int. */
    if (type_is_integer(src) && type_is_integer(dst)) {
        int sb = type_int_bits(src);
        int db = type_int_bits(dst);
        bool ss = type_is_signed(src);
        bool su = type_is_unsigned(src);
        bool ds = type_is_signed(dst);
        bool du = type_is_unsigned(dst);

        if (ss && ds) return db >= sb;        /* signed → signed wider */
        if (su && du) return db >= sb;        /* unsigned → unsigned wider */
        if (su && ds) return db > sb;         /* unsigned → signed STRICTLY wider */
        if (ss && du) return false;           /* signed → unsigned: never */
    }

    return false;
}

Type *type_numeric_common(Type *a, Type *b) {
    if (a == NULL || b == NULL) return NULL;
    if (!type_is_numeric(a) || !type_is_numeric(b)) return NULL;
    if (type_equals(a, b)) return a;
    if (type_widens_to(a, b)) return b;
    if (type_widens_to(b, a)) return a;
    return NULL;
}

bool type_is_unsigned(const Type *t) {
    if (t == NULL) return false;
    switch (t->kind) {
    case TYPE_U8: case TYPE_U16: case TYPE_U32: case TYPE_U64:
    case TYPE_CHAR:   /* char is treated as unsigned 8-bit (u8) */
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
    #define TYPE_NAME_BUFS 8
    static char bufs[TYPE_NAME_BUFS][256];
    static int buf_idx = 0;

    if (t == NULL) return "void";

    /* Composing types (pointer/reference/array/slice/fn/Block/module) are built
       into a STACK-LOCAL buffer, NOT directly into a rotating pool slot. A pool
       slot is claimed only at return. This is essential: a recursive type_name
       call rotates the global pool, and for e.g. `Block(&int, &int) -> int` the
       nested calls wrap the pool right back onto the half-built outer slot,
       corrupting it (the old bug: error messages showed expected==got because
       both names came from the same clobbered slot). Building on the stack makes
       construction immune to pool rotation; the pool only has to keep multiple
       FINISHED names alive within one caller expression ("expected %s got %s"). */
    char tmp[256];

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
    case TYPE_F16:    return "f16";
    case TYPE_BF16:   return "bf16";
    case TYPE_BOOL:   return "bool";
    case TYPE_CHAR:   return "char";
    case TYPE_VOID:   return "void";
    case TYPE_NIL:    return "nil";
    case TYPE_LIB:    return "lib";
    case TYPE_OBJECT: return "object";
    case TYPE_STRUCT:
        if (t->as.strukt.name) return t->as.strukt.name;
        return "<struct>";
    case TYPE_ENUM:
        if (t->as.enom.name) return t->as.enom.name;
        return "<enum>";
    case TYPE_POINTER:
        snprintf(tmp, 256, "*%s", type_name(t->as.pointer_to));
        break;
    case TYPE_REFERENCE:
        snprintf(tmp, 256, "&%s%s", t->is_mut ? "!" : "", type_name(t->as.pointer_to));
        break;
    case TYPE_ARRAY:
        snprintf(tmp, 256, "array(%s, %d)", type_name(t->as.array.elem), t->as.array.size);
        break;
    case TYPE_SIMD:
        snprintf(tmp, 256, "Simd(%s, %d)", type_name(t->as.simd.elem), t->as.simd.lanes);
        break;
    case TYPE_SLICE:
        snprintf(tmp, 256, "&%sarray(%s)", t->is_mut ? "!" : "", type_name(t->as.array.elem));
        break;
    case TYPE_FUNCTION:
    case TYPE_BLOCK: {
        int pos = snprintf(tmp, 256, "%s(", t->kind == TYPE_BLOCK ? "Block" : "def");
        for (int i = 0; i < t->as.function.param_count && pos < (int)256 - 10; i++) {
            if (i > 0) pos += snprintf(tmp + pos, 256 - (size_t)pos, ", ");
            pos += snprintf(tmp + pos, 256 - (size_t)pos, "%s",
                            type_name(t->as.function.params[i]));
        }
        if (t->as.function.is_vararg) {
            pos += snprintf(tmp + pos, 256 - (size_t)pos, ", ...");
        }
        snprintf(tmp + pos, 256 - (size_t)pos, ") -> %s",
                 type_name(t->as.function.return_type));
        break;
    }
    case TYPE_MODULE:
        if (t->as.module.name) {
            snprintf(tmp, 256, "module(%s)", t->as.module.name);
            break;
        }
        return "module";
    default:
        return "<unknown>";
    }

    /* Claim one pool slot for the finished name (done recursing — no nested call
       can clobber it now). The pool of N lets up to N finished names coexist in
       a single caller expression. */
    char *out = bufs[buf_idx++ % TYPE_NAME_BUFS];
    memcpy(out, tmp, 256);
    return out;
}
