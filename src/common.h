/* common.h — Public types, macros, and error codes */
#ifndef LS_COMMON_H
#define LS_COMMON_H

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdarg.h>

/* Dynamic array growth */
#define GROW_CAPACITY(cap) ((cap) < 8 ? 8 : (cap) * 2)
#define GROW_ARRAY(type, ptr, new_count) \
    (type *)realloc_safe((ptr), sizeof(type) * (new_count))

/* LsString minimum capacity — all dynamic strings allocate at least this many bytes */
#define LS_MIN_STR_CAP 16

static inline void *realloc_safe(void *ptr, size_t size) {
    void *result = realloc(ptr, size);
    if (result == NULL && size != 0) {
        fprintf(stderr, "fatal: out of memory\n");
        exit(1);
    }
    return result;
}

static inline void *malloc_safe(size_t size) {
    void *result = malloc(size);
    if (result == NULL && size != 0) {
        fprintf(stderr, "fatal: out of memory\n");
        exit(1);
    }
    return result;
}

#endif /* LS_COMMON_H */
