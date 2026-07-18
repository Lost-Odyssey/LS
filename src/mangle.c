/* mangle.c — see mangle.h for the contract. */
#include "mangle.h"
#include "common.h" /* malloc_safe/realloc_safe */
#include "types.h"  /* Type, type_name — needed by the Type-aware functions below */
#include <string.h>

char *mangle_module_symbol(const char *module_name, const char *name)
{
    if (module_name == NULL || module_name[0] == '\0')
    {
        size_t n = strlen(name) + 1;
        char *out = (char *)malloc_safe(n);
        memcpy(out, name, n);
        return out;
    }

    size_t mod_len = strlen(module_name);
    size_t name_len = strlen(name);
    /* "<mod>" + "__" + "<name>" + NUL, mod chars are 1:1 replaced ('.' -> '_'). */
    char *out = (char *)malloc_safe(mod_len + 2 + name_len + 1);
    size_t pos = 0;
    for (const char *p = module_name; *p; p++)
        out[pos++] = (*p == '.') ? '_' : *p;
    out[pos++] = '_';
    out[pos++] = '_';
    memcpy(out + pos, name, name_len + 1); /* includes NUL */
    return out;
}

char *mangle_method_symbol(const char *type_name, const char *iface_or_null,
                           const char *method_name)
{
    size_t tn = strlen(type_name);
    size_t in = iface_or_null ? strlen(iface_or_null) : 0;
    size_t mn = strlen(method_name);
    /* "<type>" + "." + ["<iface>" + "."] + "<method>" + NUL */
    size_t total = tn + 1 + (iface_or_null ? in + 1 : 0) + mn + 1;
    char *out = (char *)malloc_safe(total);
    size_t pos = 0;
    memcpy(out + pos, type_name, tn); pos += tn;
    out[pos++] = '.';
    if (iface_or_null)
    {
        memcpy(out + pos, iface_or_null, in); pos += in;
        out[pos++] = '.';
    }
    memcpy(out + pos, method_name, mn + 1); /* includes NUL */
    return out;
}

/* ---- growable generic instance-name builder (Task 2.2) ---- */

void mangle_buf_init(MangleBuf *b)
{
    b->p = NULL;
    b->len = 0;
    b->cap = 0;
}

void mangle_buf_free(MangleBuf *b)
{
    free(b->p);
    b->p = NULL;
    b->len = 0;
    b->cap = 0;
}

/* Ensures b->cap - b->len >= extra + 1 (room for extra more bytes plus the
   NUL), growing geometrically (double, with a floor) when it doesn't. */
static void mangle_buf_reserve(MangleBuf *b, size_t extra)
{
    size_t need = b->len + extra + 1;
    if (need <= b->cap) return;
    size_t new_cap = b->cap < 64 ? 64 : b->cap;
    while (new_cap < need) new_cap *= 2;
    b->p = (char *)realloc_safe(b->p, new_cap);
    b->cap = new_cap;
}

void mangle_buf_append(MangleBuf *b, const char *s)
{
    size_t slen = strlen(s);
    mangle_buf_reserve(b, slen);
    memcpy(b->p + b->len, s, slen + 1); /* includes NUL */
    b->len += slen;
}

char *mangle_buf_take(MangleBuf *b)
{
    char *out;
    if (b->p == NULL) {
        out = (char *)malloc_safe(1);
        out[0] = '\0';
    } else {
        out = b->p;
    }
    b->p = NULL;
    b->len = 0;
    b->cap = 0;
    return out;
}

const char *mangle_type_arg_name(const Type *at)
{
    if (at && at->kind == TYPE_STRUCT && at->as.strukt.llvm_name)
        return at->as.strukt.llvm_name;
    if (at && at->kind == TYPE_ENUM && at->as.enom.llvm_name)
        return at->as.enom.llvm_name;
    return type_name(at);
}

void mangle_append_type_arg(MangleBuf *b, const Type *at)
{
    mangle_buf_append(b, mangle_type_arg_name(at));
}
