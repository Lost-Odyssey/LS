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

/* Compile pattern; returns handle 0..31, or -1 on error */
int  __ls_regex_compile(const char *pattern, int flags);

/* Release handle */
void __ls_regex_free(int handle);

/* Error message from last failed compile */
const char *__ls_regex_last_error(void);

/* Execute on text[start..text_len).
   Returns number of groups (incl. group 0 = full match), 0 = no match. */
int  __ls_regex_exec(int handle, const char *text, int text_len, int start);

/* Query results of last successful exec */
int  __ls_regex_cap_start(int group);   /* byte offset, -1 = did not participate */
int  __ls_regex_cap_len(int group);     /* byte length */

/* Number of capture groups in compiled pattern (excluding group 0) */
int         __ls_regex_group_count(int handle);

/* Named capture queries */
int         __ls_regex_named_count(int handle);
const char *__ls_regex_named_name(int handle, int i);
int         __ls_regex_named_index(int handle, int i);

#ifdef __cplusplus
}
#endif
#endif /* LS_REGEX_H */
