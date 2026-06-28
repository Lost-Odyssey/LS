/* repl_edit.h — Self-built REPL line editor, syntax highlighter, and input
 *               classification/completeness helpers.
 *
 * Zero external dependencies (only the project's own scanner). The line editor
 * runs a raw-mode, char-by-char input loop with live syntax highlighting on a
 * real terminal; when stdin is not a TTY it transparently falls back to plain
 * line-buffered fgets so piped input (`echo ... | ls repl`) and CI keep working.
 */
#ifndef LS_REPL_EDIT_H
#define LS_REPL_EDIT_H

#include <stdbool.h>
#include <stddef.h>

/* Classification of a complete logical REPL input. */
typedef enum {
    REPL_DECL,      /* fn / struct / enum / impl / trait / extern / type — top-level */
    REPL_IMPORT,    /* import X [as Y] — top-level, accumulated before decls */
    REPL_VAR,       /* int x = ... — accumulated into the wrapper body */
    REPL_EXPR,      /* expression / statement — wrapped in __repl_N() */
} ReplLineKind;

/* Classify a complete input buffer using the scanner's first significant token. */
ReplLineKind repl_classify(const char *buf);

/* If `buf` is a scalar-typed variable declaration (`int x = ...`, `f64 y`, ...),
 * return a malloc'd copy of the variable name (caller frees); otherwise NULL.
 * POD scalars (int, sized ints, floats, bool, char) have no M-DEF default-init
 * and are not has_drop, so the REPL can persist them as real globals. Pointer,
 * container, and user-typed declarations return NULL (wrapper-replay path). */
char *repl_pod_scalar_var_name(const char *buf);

/* If `buf` is any variable declaration the REPL can persist as a real top-level
 * global (POD scalars plus containers / structs / user types), return a malloc'd
 * copy of the variable name (caller frees); otherwise NULL. Borrows and slices
 * classify as REPL_EXPR and never match. Used by the REPL Phase 2 routing. */
char *repl_persisted_var_name(const char *buf);

/* Return true when `buf` is a complete logical input (balanced brackets, no
 * trailing continuation operator, no unterminated string). Used to decide
 * whether the editor should keep reading continuation lines. */
bool repl_input_is_complete(const char *buf);

/* Build into `out` the continuation prompt to show while `buf` is incomplete,
 * hinting what is still unclosed (e.g. "..{> " for an open brace, "..\"> " for
 * an unterminated string). Gives the user feedback instead of a featureless
 * "...>". */
void repl_continuation_prompt(const char *buf, char *out, size_t cap);

/* Render `line` into `out` with ANSI color escapes per token span. Stripping the
 * ANSI codes from `out` yields `line` verbatim. Always emits (TTY-independent) so
 * it is unit-testable; callers gate on color-enabled themselves. */
void repl_highlight_render(const char *line, char *out, size_t out_cap);

/* Opaque line editor (owns in-process history). */
typedef struct ReplEditor ReplEditor;

ReplEditor *repl_editor_new(void);
void repl_editor_free(ReplEditor *e);

/* Read one logical input (possibly spanning multiple physical lines, driven by
 * `is_complete`). Returns a malloc'd string the caller must free, or NULL on
 * EOF / Ctrl-D (signalling the REPL should quit). `prompt` is shown for the first
 * line, `cont_prompt` for continuation lines. */
char *repl_editor_read(ReplEditor *e, const char *prompt, const char *cont_prompt,
                       bool (*is_complete)(const char *buf));

/* Whether ANSI color should be used (TTY on stdout AND NO_COLOR unset). */
bool repl_color_enabled(void);

#endif /* LS_REPL_EDIT_H */
