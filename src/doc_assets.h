/* doc_assets.h — built-in default CSS + HTML skeleton for `ls doc`.
 * Defined in doc_assets.c; kept out of main.c so the large string literals
 * don't clutter the CLI. Override at runtime with `--css` / `--template`. */
#ifndef LS_DOC_ASSETS_H
#define LS_DOC_ASSETS_H

/* Default stylesheet — mirrors docs/stdlib.html (sidebar, sig badges, dark
   mode, responsive). Used when `--css` is not given. */
extern const char *const ls_doc_default_css;

/* Default HTML skeleton with {{TITLE}} {{STYLE}} {{NAV}} {{OVERVIEW}}
   {{CONTENT}} placeholders. Used when `--template` is not given. */
extern const char *const ls_doc_default_template;

#endif /* LS_DOC_ASSETS_H */
