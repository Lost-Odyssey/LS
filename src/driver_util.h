/* driver_util.h — small CLI helpers shared by the driver TUs (W4).
   Definitions live in main.c; test_driver.c / link_driver.c consume them.
   NOTE: jit.c and module.c each keep their own file-local `read_file` with
   different error behavior (silent NULL / fopen_retry) — those statics are
   deliberately not unified with the CLI-facing helper declared here. */
#ifndef LS_DRIVER_UTIL_H
#define LS_DRIVER_UTIL_H

#include <stddef.h>

/* Read an entire file into a malloc'd NUL-terminated buffer; prints an error
   to stderr and returns NULL on failure. Caller frees. */
char *read_file(const char *path);

/* Write `content` to `path` (binary, no newline translation). 0 on success. */
int write_file_str(const char *path, const char *content);

/* Resolve the full path to the running lls executable (not just its
   containing directory). Returns 0 on success and writes a NUL-terminated
   path to `out`. */
int get_executable_path(char *out, size_t out_sz);

#endif /* LS_DRIVER_UTIL_H */
