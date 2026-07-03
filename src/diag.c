/* diag.c — Diagnostic sink: text renderer (default). */
#include "diag.h"
#include <stdio.h>
#include <string.h>

/* ---- Text renderer (default sink) ---- */

static const char *diag_prefix(DiagKind kind)
{
    switch (kind) {
    case DIAG_TYPE_ERROR:  return "[type error]";
    case DIAG_MOVE_ERROR:  return "[move error]";
    case DIAG_WARNING:     return "[warning]";
    case DIAG_PARSE_ERROR:
    case DIAG_SCAN_ERROR:  return "[error]";
    }
    return "[error]";
}

static void text_sink_emit(DiagSink *self, const Diagnostic *d)
{
    (void)self;
    fprintf(stderr, "%s %s:%d:%d: %s\n",
            diag_prefix(d->kind),
            d->file ? d->file : "<unknown>",
            d->line, d->col, d->message);
}

static DiagSink g_text_sink = { text_sink_emit, NULL };
static DiagSink *g_current_sink = &g_text_sink;

/* ---- Sink plumbing ---- */

DiagSink *diag_current_sink(void)
{
    return g_current_sink;
}

void diag_set_sink(DiagSink *sink)
{
    g_current_sink = sink ? sink : &g_text_sink;
}

void diag_emit(DiagSink *sink, const Diagnostic *d)
{
    if (sink == NULL) sink = g_current_sink;
    sink->emit(sink, d);
}

void diag_vemitf(DiagKind kind, const char *file, int line, int col, int len,
                 const char *help, const char *fmt, va_list args)
{
    Diagnostic d;
    d.kind = kind;
    d.file = file;
    d.line = line;
    d.col = col;
    d.len = len > 0 ? len : 1;
    vsnprintf(d.message, sizeof(d.message), fmt, args);
    if (help && help[0])
        snprintf(d.help, sizeof(d.help), "%s", help);
    else
        d.help[0] = '\0';
    diag_emit(g_current_sink, &d);
}

void diag_emitf(DiagKind kind, const char *file, int line, int col, int len,
                const char *help, const char *fmt, ...)
{
    va_list args;
    va_start(args, fmt);
    diag_vemitf(kind, file, line, col, len, help, fmt, args);
    va_end(args);
}
