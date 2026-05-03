/* builtins_io.c — Type & lookup side of the built-in `io` module.
 *
 * Constructs the OpenMode enum, the File struct, and the function table
 * for the `io` module. Registers File/OpenMode into the checker's struct
 * and enum registries so user code can name them (e.g. `File f`,
 * `OpenMode.Read`).
 *
 * Codegen-side emission lives in builtins_io_cg.c.
 */
#include "builtins_io.h"
#include "checker.h"
#include "common.h"
#include <string.h>
#include <stdlib.h>

static char *dup_str(const char *s) {
    size_t n = strlen(s);
    char *p = (char *)malloc_safe(n + 1);
    memcpy(p, s, n + 1);
    return p;
}

/* OpenMode variant names — order MUST match the IO_OPEN_* enum in the header. */
static const char *OPEN_MODE_NAMES[IO_OPEN_MODE_COUNT] = {
    "Read", "Write", "Append",
    "ReadBinary", "WriteBinary", "AppendBinary",
};

/* SeekFrom variant names — order matches C SEEK_SET/CUR/END (0/1/2). */
static const char *SEEK_FROM_NAMES[IO_SEEK_FROM_COUNT] = {
    "Start", "Current", "End",
};

/* Build a no-payload enum (each variant has 0 payload slots) and register it. */
static Type *make_simple_enum(Checker *c, const char *name,
                              const char *const *variant_names, int n) {
    Type *cached = checker_find_enum(c, name);
    if (cached) return cached;

    Type *et = type_enum(name, n);
    for (int i = 0; i < n; i++) {
        et->as.enom.variants[i].name = dup_str(variant_names[i]);
        et->as.enom.variants[i].payload_count = 0;
        et->as.enom.variants[i].payload_types = NULL;
    }
    et->as.enom.has_drop = false;
    checker_register_enum(c, et->as.enom.name, et);
    return et;
}

/* Build the File struct: { object handle, bool is_binary }. */
static Type *make_file_struct(Checker *c) {
    /* If user already declared a struct named File this would clash —
       but in v1 we just register ours. The checker rejects duplicate
       struct names elsewhere; conflicts with `import io` are out of scope. */
    Type *st = type_struct(dup_str("File"), 2);
    st->as.strukt.fields[0].name = dup_str("handle");
    st->as.strukt.fields[0].type = type_object();
    st->as.strukt.fields[1].name = dup_str("is_binary");
    st->as.strukt.fields[1].type = type_bool();
    st->as.strukt.has_drop = false;
    st->as.strukt.has_user_drop = false;
    st->as.strukt.drop_fn = NULL;
    checker_register_struct(c, st->as.strukt.name, st);
    return st;
}

Type *builtin_io_make_type(Checker *c) {
    if (c == NULL) return NULL;

    Type *mod = type_module_new("io");
    mod->as.module.is_builtin = true;

    Type *open_mode = make_simple_enum(c, "OpenMode", OPEN_MODE_NAMES, IO_OPEN_MODE_COUNT);
    Type *seek_from = make_simple_enum(c, "SeekFrom", SEEK_FROM_NAMES, IO_SEEK_FROM_COUNT);
    Type *file_t    = make_file_struct(c);
    Type *str_t     = type_string();
    Type *int_t     = type_int();
    Type *i64_t     = type_i64();
    Type *bool_t    = type_bool();

    Type *res_str_str  = checker_instantiate_result(c, str_t, str_t);
    Type *res_int_str  = checker_instantiate_result(c, int_t, str_t);
    Type *res_i64_str  = checker_instantiate_result(c, i64_t, str_t);
    Type *res_file_str = checker_instantiate_result(c, file_t, str_t);

    /* Export the type names so user code can refer to them directly. */
    type_module_add_export(mod, "OpenMode", open_mode);
    type_module_add_export(mod, "SeekFrom", seek_from);
    type_module_add_export(mod, "File",     file_t);

    /* ---- Function signatures ----
       We pass param-type arrays via type_function which deep-copies them
       type_function does NOT copy the params array; it stores the pointer.
       So we must malloc each params array (mirrors builtins_math.c pattern). */

    #define MK_FN(name, ret_type, ...) do { \
        Type *src[] = { __VA_ARGS__ }; \
        int n = (int)(sizeof(src) / sizeof(src[0])); \
        Type **p = (Type **)malloc_safe((size_t)n * sizeof(Type *)); \
        for (int i = 0; i < n; i++) p[i] = src[i]; \
        type_module_add_export(mod, name, type_function(p, n, (ret_type), false)); \
    } while (0)

    MK_FN("read_file",   res_str_str,  str_t);
    MK_FN("write_file",  res_int_str,  str_t, str_t);
    MK_FN("append_file", res_int_str,  str_t, str_t);
    MK_FN("exists",      bool_t,       str_t);
    MK_FN("remove",      res_int_str,  str_t);
    MK_FN("open",        res_file_str, str_t, open_mode);
    MK_FN("close",       int_t,        file_t);
    MK_FN("read_all",    res_str_str,  file_t);
    MK_FN("write",       res_int_str,  file_t, str_t);
    /* v2 positioning (binary-mode files only) */
    MK_FN("seek",        res_i64_str,  file_t, i64_t, seek_from);
    MK_FN("tell",        res_i64_str,  file_t);
    MK_FN("size",        res_i64_str,  file_t);
    MK_FN("rewind",      res_int_str,  file_t);

    #undef MK_FN
    return mod;
}
