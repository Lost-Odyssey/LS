/* mangle.c — see mangle.h for the contract. */
#include "mangle.h"
#include "common.h" /* malloc_safe */
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
