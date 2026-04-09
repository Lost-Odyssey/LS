/* types.h — Semantic type representation and type utilities */
#ifndef LS_TYPES_H
#define LS_TYPES_H

#include "common.h"

typedef struct Type Type;

typedef enum {
    TYPE_INT, TYPE_I8, TYPE_I16, TYPE_I32, TYPE_I64,
    TYPE_U8, TYPE_U16, TYPE_U32, TYPE_U64,
    TYPE_F32, TYPE_F64,
    TYPE_BOOL, TYPE_STRING, TYPE_VOID, TYPE_NIL,
    TYPE_OBJECT,        /* object — type-erased pointer (void*) */
    TYPE_POINTER,       /* *T */
    TYPE_ARRAY,         /* array(T, N) — fixed-size */
    TYPE_VECTOR,        /* vec(T)      — dynamic array */
    TYPE_FUNCTION,      /* fn(A, B) -> R */
    TYPE_STRUCT,        /* struct { ... } */
    TYPE_MODULE,        /* module reference */
    TYPE_LIB,           /* FFI library handle */
} TypeKind;

struct Type {
    TypeKind kind;
    union {
        Type *pointer_to;                               /* TYPE_POINTER */
        struct { Type *elem; int size; } array;           /* TYPE_ARRAY — size > 0 for fixed */
        struct { Type *elem; } vec;                       /* TYPE_VECTOR */
        struct {                                        /* TYPE_FUNCTION */
            Type **params;
            int param_count;
            Type *return_type;
            bool is_vararg;
        } function;
        struct {                                        /* TYPE_STRUCT */
            const char *name;
            struct { const char *name; Type *type; } *fields;
            int field_count;
            bool has_drop;           /* true if struct has a __drop destructor */
            bool has_user_drop;      /* true if __drop was user-defined (not auto-generated) */
            void *drop_fn;           /* LLVMValueRef: complete __drop function (codegen use) */
        } strukt;
        struct {                                        /* TYPE_MODULE */
            const char *name;
            struct { const char *name; Type *type; } *exports;
            int export_count;
        } module;
    } as;
};

/* Create primitive types (returns interned singletons, do NOT free) */
Type *type_int(void);
Type *type_i8(void);
Type *type_i16(void);
Type *type_i32(void);
Type *type_i64(void);
Type *type_u8(void);
Type *type_u16(void);
Type *type_u32(void);
Type *type_u64(void);
Type *type_f32(void);
Type *type_f64(void);
Type *type_bool(void);
Type *type_string(void);
Type *type_void(void);
Type *type_nil(void);
Type *type_lib(void);
Type *type_object(void);

/* Check if a type is pointer-like (pointer, object, nil — can convert to object) */
bool type_is_pointer_like(const Type *t);

/* Create composite types (caller owns the returned Type) */
Type *type_pointer(Type *pointee);
Type *type_array(Type *elem, int size);
Type *type_vector(Type *elem);  /* vec(T) — dynamic array */
Type *type_function(Type **params, int param_count, Type *return_type, bool is_vararg);
Type *type_struct(const char *name, int field_count);

/* Create a module type with the given name (caller owns the Type) */
Type *type_module_new(const char *name);

/* Add an exported symbol to a module type */
void type_module_add_export(Type *mod, const char *name, Type *type);

/* Deep-copy a type */
Type *type_clone(const Type *t);

/* Free a non-singleton type (safe to call on singletons — does nothing) */
void type_free(Type *t);

/* Check structural equality of two types */
bool type_equals(const Type *a, const Type *b);

/* Check if a type is numeric (int, i8..i64, u8..u64, f32, f64) */
bool type_is_numeric(const Type *t);

/* Check if a type is integer (int, i8..i64, u8..u64) */
bool type_is_integer(const Type *t);

/* Check if a type is floating-point (f32, f64) */
bool type_is_float(const Type *t);

/* Check if a type is a signed integer (int, i8..i64) */
bool type_is_signed(const Type *t);

/* Check if a type is an unsigned integer (u8..u64) */
bool type_is_unsigned(const Type *t);

/* Return a human-readable string for the type (static buffer, do NOT free) */
const char *type_name(const Type *t);

#endif /* LS_TYPES_H */
