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

/* Return true when `buf` is a complete logical input (balanced brackets, no
 * trailing continuation operator, no unterminated string). Used to decide
 * whether the editor should keep reading continuation lines. */
bool repl_input_is_complete(const char *buf);

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
