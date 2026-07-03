/* diag.h — Centralized diagnostics: one Diagnostic struct, one sink.
   C2 (docs/plan_diagnostics_v2.md): every compiler diagnostic (checker,
   parser, scanner) is assembled into a Diagnostic and routed through the
   current DiagSink. Two sink implementations exist: the stderr text
   renderer (default) and the JSON collector (`lls check --json`). */
#ifndef LS_DIAG_H
#define LS_DIAG_H

#include <stdarg.h>

/* Kind decides both the legacy text prefix and the JSON "kind" field:
     DIAG_TYPE_ERROR  -> "[type error]" / "type"
     DIAG_MOVE_ERROR  -> "[move error]" / "move"
     DIAG_WARNING     -> "[warning]"    / "type" (severity: warning)
     DIAG_PARSE_ERROR -> "[error]"      / "parse"
     DIAG_SCAN_ERROR  -> "[error]"      / "scan" */
typedef enum {
    DIAG_TYPE_ERROR,
    DIAG_MOVE_ERROR,
    DIAG_WARNING,
    DIAG_PARSE_ERROR,
    DIAG_SCAN_ERROR
} DiagKind;

typedef struct {
    DiagKind kind;
    const char *file;      /* borrowed; NULL renders as "<unknown>" */
    int line, col;         /* 1-based */
    int len;               /* squiggle length; 1 when unknown */
    char message[512];
    char help[256];        /* "" = no suggestion */
} Diagnostic;

typedef struct DiagSink DiagSink;
struct DiagSink {
    void (*emit)(DiagSink *self, const Diagnostic *d);
    void *user;
};

/* Emit through an explicit sink (NULL = current sink). */
void diag_emit(DiagSink *sink, const Diagnostic *d);

/* Process-wide current sink. Defaults to the stderr text renderer.
   diag_set_sink(NULL) restores the default. */
DiagSink *diag_current_sink(void);
void diag_set_sink(DiagSink *sink);

/* Convenience: format the message and emit through the current sink.
   help may be NULL ("" = no suggestion). */
void diag_emitf(DiagKind kind, const char *file, int line, int col, int len,
                const char *help, const char *fmt, ...);
void diag_vemitf(DiagKind kind, const char *file, int line, int col, int len,
                 const char *help, const char *fmt, va_list args);

#endif /* LS_DIAG_H */
