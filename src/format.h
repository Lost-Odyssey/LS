/* format.h — LS source formatter (`ls fmt`).
 *
 * Reformats LS source to a canonical style: 4-space indentation by brace
 * depth, normalized intra-line spacing, collapsed blank-line runs, preserved
 * comments. Line breaks chosen by the user are preserved (P1 does not reflow
 * long lines). Parse-equivalence is the hard invariant: formatting never
 * changes token meaning.
 */
#ifndef LS_FORMAT_H
#define LS_FORMAT_H

/* Format `source`. Returns a newly malloc'd NUL-terminated string the caller
 * must free, or NULL if the source could not be scanned (caller should then
 * leave the file untouched). */
char *ls_format_source(const char *source);

#endif /* LS_FORMAT_H */
