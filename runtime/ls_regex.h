/* runtime/ls_regex.h — LS built-in regex engine (Pike VM NFA) */
#ifndef LS_REGEX_H
#define LS_REGEX_H

#ifdef __cplusplus
extern "C" {
#endif

/* Compile-time flags (can be OR'd) */
#define LS_RE_IGNORECASE  0x01   /* (?i) */
#define LS_RE_MULTILINE   0x02   /* (?m) ^ $ match line boundaries */
#define LS_RE_DOTALL      0x04   /* (?s) . matches \n */

/* Compile pattern; returns an opaque handle, or NULL on error.
   The caller owns the handle and must release it with __ls_regex_free. */
void *__ls_regex_compile(const char *pattern, int flags);

/* Release a handle returned by __ls_regex_compile. NULL is a no-op. */
void __ls_regex_free(void *h);

/* Compile via a thread-local LRU cache. The returned handle is owned by the
   cache -- do NOT pass it to __ls_regex_free. Returns NULL on a bad pattern
   or when the pattern is too long to cache (in which case the caller should
   fall back to __ls_regex_compile + __ls_regex_free). */
void *__ls_regex_cached(const char *pattern, int flags);

/* Error message from the last failed compile. Compile failures have no handle
   to hang the message on, so this stays process-global; it is advisory only. */
const char *__ls_regex_last_error(void);

/* Execute on text[start..text_len).
   Returns number of groups (incl. group 0 = full match), 0 = no match.
   Results are stored in the handle, so two handles never clobber each other. */
int __ls_regex_exec(void *h, const char *text, int text_len, int start);

/* Query results of the last successful exec ON THIS HANDLE */
int __ls_regex_cap_start(void *h, int group);   /* byte offset, -1 = absent */
int __ls_regex_cap_len(void *h, int group);     /* byte length */

/* Number of capture groups in compiled pattern (excluding group 0) */
int __ls_regex_group_count(void *h);

/* Named capture queries */
int         __ls_regex_named_count(void *h);
const char *__ls_regex_named_name(void *h, int i);
int         __ls_regex_named_index(void *h, int i);

#ifdef __cplusplus
}
#endif
#endif /* LS_REGEX_H */
