/* repl_edit.c — Self-built REPL line editor + syntax highlighter.
 *
 * No third-party dependencies: the highlighter reuses the project scanner and
 * the editor is a hand-written raw-mode input loop (Win32 console / POSIX
 * termios). When stdin is not a TTY it falls back to plain fgets so piped input
 * and CI behave deterministically.
 */
#include "repl_edit.h"
#include "repl_term.h"
#include "common.h"
#include "scanner.h"
#include "token.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ---- ANSI colors ---- */
#define C_RESET   "\x1b[0m"
#define C_KEYWORD "\x1b[35m"   /* magenta */
#define C_TYPE    "\x1b[36m"   /* cyan    */
#define C_STRING  "\x1b[32m"   /* green   */
#define C_NUMBER  "\x1b[33m"   /* yellow  */
#define C_COMMENT "\x1b[90m"   /* gray    */

/* ===================================================================== */
/* Classification                                                        */
/* ===================================================================== */

static bool is_scalar_type_token(TokenType t) {
    switch (t) {
        case TOKEN_TYPE_INT: case TOKEN_TYPE_I8: case TOKEN_TYPE_I16:
        case TOKEN_TYPE_I32: case TOKEN_TYPE_I64: case TOKEN_TYPE_U8:
        case TOKEN_TYPE_U16: case TOKEN_TYPE_U32: case TOKEN_TYPE_U64:
        case TOKEN_TYPE_F32: case TOKEN_TYPE_F64: case TOKEN_TYPE_BOOL:
        case TOKEN_TYPE_CHAR: case TOKEN_TYPE_VOID:
        case TOKEN_TYPE_OBJECT: case TOKEN_TYPE_LIB:
            return true;
        default:
            return false;
    }
}

ReplLineKind repl_classify(const char *buf) {
    Scanner s;
    scanner_init(&s, buf);
    Token t1 = scanner_next(&s);

    switch (t1.type) {
        case TOKEN_IMPORT:
            return REPL_IMPORT;
        case TOKEN_FN: case TOKEN_STRUCT: case TOKEN_ENUM:
        case TOKEN_IMPL: case TOKEN_TRAIT: case TOKEN_EXTERN:
        case TOKEN_TYPE_ALIAS:
            return REPL_DECL;
        /* container type constructors only appear leading a declaration */
        case TOKEN_ARRAY:
            return REPL_VAR;
        default:
            break;
    }

    /* scalar type keyword followed by an identifier → `int x = ...` */
    if (is_scalar_type_token(t1.type)) {
        Token t2 = scanner_next(&s);
        return (t2.type == TOKEN_IDENTIFIER) ? REPL_VAR : REPL_EXPR;
    }

    /* pointer type: `*int x = ...` */
    if (t1.type == TOKEN_STAR) {
        Token t2 = scanner_next(&s);
        if (is_scalar_type_token(t2.type)) {
            Token t3 = scanner_next(&s);
            if (t3.type == TOKEN_IDENTIFIER) return REPL_VAR;
        }
        return REPL_EXPR;
    }

    /* user type: `Point p = ...` (identifier followed by identifier) */
    if (t1.type == TOKEN_IDENTIFIER) {
        Token t2 = scanner_next(&s);
        if (t2.type == TOKEN_IDENTIFIER) return REPL_VAR;
        /* generic / container type: `Result(JsonValue, Str) r`,
           `Vec(int) v`, `Map(K, Vec(V)) m` — scan the balanced type-argument
           parens, then a trailing identifier marks a declaration. A call
           expression like `foo(1, 2)` has no identifier after `)`, so it stays
           REPL_EXPR. */
        if (t2.type == TOKEN_LPAREN) {
            int depth = 1;
            while (depth > 0) {
                Token tk = scanner_next(&s);
                if (tk.type == TOKEN_LPAREN)      depth++;
                else if (tk.type == TOKEN_RPAREN) depth--;
                else if (tk.type == TOKEN_EOF ||
                         tk.type == TOKEN_ERROR)  return REPL_EXPR;
            }
            Token after = scanner_next(&s);
            if (after.type == TOKEN_IDENTIFIER) return REPL_VAR;
        }
    }

    return REPL_EXPR;
}

char *repl_pod_scalar_var_name(const char *buf) {
    Scanner s;
    scanner_init(&s, buf);
    Token t1 = scanner_next(&s);
    if (!is_scalar_type_token(t1.type)) return NULL;   /* must lead with a scalar type */
    Token t2 = scanner_next(&s);
    if (t2.type != TOKEN_IDENTIFIER) return NULL;       /* `int x`, not a cast/expr */
    char *name = (char *)malloc((size_t)t2.length + 1);
    if (name == NULL) return NULL;
    memcpy(name, t2.start, (size_t)t2.length);
    name[t2.length] = '\0';
    return name;
}

char *repl_persisted_var_name(const char *buf) {
    /* Any variable declaration the REPL can persist as a real top-level global —
       POD scalars (Phase 1) plus containers / structs / user types (Phase 2).
       Gate on repl_classify==REPL_VAR (so calls/assignments/borrows, which are
       REPL_EXPR, never match), then extract the declared name: in a var decl
       `<type> <name> [= init]` the name is the last IDENTIFIER token before the
       first `=` (or before EOL when there is no initializer). Scanning to the
       last identifier handles every type form uniformly — `int x`, `Point p`,
       `Vec(int) v`, `Map(K, Vec(V)) m` — without per-form parsing. */
    if (repl_classify(buf) != REPL_VAR) return NULL;
    Scanner s;
    scanner_init(&s, buf);
    Token last_ident = {0}; last_ident.type = TOKEN_EOF;
    for (;;) {
        Token t = scanner_next(&s);
        if (t.type == TOKEN_EOF || t.type == TOKEN_ERROR ||
            t.type == TOKEN_ASSIGN)
            break;
        if (t.type == TOKEN_IDENTIFIER) last_ident = t;
    }
    if (last_ident.type != TOKEN_IDENTIFIER) return NULL;
    char *name = (char *)malloc((size_t)last_ident.length + 1);
    if (name == NULL) return NULL;
    memcpy(name, last_ident.start, (size_t)last_ident.length);
    name[last_ident.length] = '\0';
    return name;
}

/* ===================================================================== */
/* Completeness                                                          */
/* ===================================================================== */

static bool is_continuation_token(TokenType t) {
    switch (t) {
        case TOKEN_PLUS: case TOKEN_MINUS: case TOKEN_STAR: case TOKEN_SLASH:
        case TOKEN_PERCENT: case TOKEN_AMP: case TOKEN_PIPE: case TOKEN_CARET:
        case TOKEN_LSHIFT: case TOKEN_RSHIFT: case TOKEN_AND: case TOKEN_OR:
        case TOKEN_EQ: case TOKEN_NEQ: case TOKEN_LT: case TOKEN_GT:
        case TOKEN_LEQ: case TOKEN_GEQ: case TOKEN_ASSIGN:
        case TOKEN_PLUS_ASSIGN: case TOKEN_MINUS_ASSIGN:
        case TOKEN_STAR_ASSIGN: case TOKEN_SLASH_ASSIGN:
        case TOKEN_COMMA: case TOKEN_DOT: case TOKEN_ARROW:
        case TOKEN_FAT_ARROW: case TOKEN_COLON:
            return true;
        default:
            return false;
    }
}

bool repl_input_is_complete(const char *buf) {
    /* Phase 1: robust manual scan for bracket depth and unterminated
       string/char/block-comment — independent of scanner error semantics so a
       genuinely malformed line can never trap the user inside continuation. */
    int depth = 0;
    bool in_str = false, in_chr = false, in_block = false;
    for (const char *p = buf; *p; p++) {
        char c = *p;
        if (in_block) {
            if (c == '*' && p[1] == '/') { in_block = false; p++; }
            continue;
        }
        if (in_str) {
            if (c == '\\' && p[1]) { p++; continue; }
            if (c == '"') in_str = false;
            continue;
        }
        if (in_chr) {
            if (c == '\\' && p[1]) { p++; continue; }
            if (c == '\'') in_chr = false;
            continue;
        }
        if (c == '/' && p[1] == '/') {           /* line comment */
            while (p[1] && p[1] != '\n') p++;
            continue;
        }
        if (c == '/' && p[1] == '*') { in_block = true; p++; continue; }
        if (c == '"')  { in_str = true; continue; }
        if (c == '\'') { in_chr = true; continue; }
        if (c == '(' || c == '[' || c == '{') depth++;
        else if (c == ')' || c == ']' || c == '}') { if (depth > 0) depth--; }
    }
    if (depth > 0 || in_str || in_chr || in_block) return false;

    /* Phase 2: trailing continuation operator. Brackets/strings are balanced
       here, so the scanner won't hit an unterminated-literal error. */
    Scanner s;
    scanner_init(&s, buf);
    TokenType last = TOKEN_EOF;
    bool any = false;
    for (;;) {
        Token t = scanner_next(&s);
        if (t.type == TOKEN_EOF || t.type == TOKEN_ERROR) break;
        last = t.type;
        any = true;
    }
    if (any && is_continuation_token(last)) return false;

    return true;
}

/* Build the continuation prompt shown while input is incomplete, hinting WHAT
   the REPL is still waiting to close (unclosed (/[/{ , string, char, or block
   comment) — so a stray `{` doesn't silently trap the user in a featureless
   `...>` with no clue why nothing evaluates. Mirrors repl_input_is_complete's
   scan but keeps a small delimiter stack. */
void repl_continuation_prompt(const char *buf, char *out, size_t cap) {
    char stack[64];
    int  top = 0;
    bool in_str = false, in_chr = false, in_block = false;
    for (const char *p = buf; *p; p++) {
        char c = *p;
        if (in_block) { if (c == '*' && p[1] == '/') { in_block = false; p++; } continue; }
        if (in_str)   { if (c == '\\' && p[1]) { p++; continue; } if (c == '"')  in_str = false; continue; }
        if (in_chr)   { if (c == '\\' && p[1]) { p++; continue; } if (c == '\'') in_chr = false; continue; }
        if (c == '/' && p[1] == '/') { while (p[1] && p[1] != '\n') p++; continue; }
        if (c == '/' && p[1] == '*') { in_block = true; p++; continue; }
        if (c == '"')  { in_str = true; continue; }
        if (c == '\'') { in_chr = true; continue; }
        if (c == '(' || c == '[' || c == '{') { if (top < (int)sizeof(stack)) stack[top] = c; top++; }
        else if (c == ')' || c == ']' || c == '}') { if (top > 0) top--; }
    }
    if (in_str)        { snprintf(out, cap, "..\"> "); return; }
    if (in_chr)        { snprintf(out, cap, "..'> ");  return; }
    if (in_block)      { snprintf(out, cap, "..*/> "); return; }
    if (top > 0) {
        /* show the innermost few open delimiters (rightmost = most recent) */
        int n = top; if (n > (int)sizeof(stack)) n = (int)sizeof(stack);
        int start = (n > 4) ? n - 4 : 0;
        char tmp[8]; int j = 0;
        for (int i = start; i < n && j < (int)sizeof(tmp) - 1; i++) tmp[j++] = stack[i];
        tmp[j] = '\0';
        snprintf(out, cap, "..%s> ", tmp);
        return;
    }
    snprintf(out, cap, "...> ");   /* trailing continuation operator */
}

/* ===================================================================== */
/* Highlighting                                                          */
/* ===================================================================== */

static const char *ansi_for_token(TokenType t) {
    switch (t) {
        /* keywords */
        case TOKEN_FN: case TOKEN_RETURN: case TOKEN_IF: case TOKEN_ELSE:
        case TOKEN_WHILE: case TOKEN_FOR: case TOKEN_IN: case TOKEN_MATCH:
        case TOKEN_STRUCT: case TOKEN_ENUM: case TOKEN_IMPL: case TOKEN_MODULE:
        case TOKEN_IMPORT: case TOKEN_LOAD: case TOKEN_SELF: case TOKEN_STATIC:
        case TOKEN_DO: case TOKEN_END: case TOKEN_BREAK: case TOKEN_CONTINUE:
        case TOKEN_EXTERN: case TOKEN_AS: case TOKEN_FROM: case TOKEN_PUB:
        case TOKEN_NEW: case TOKEN_TRY: case TOKEN_TYPE_ALIAS: case TOKEN_TRAIT:
        case TOKEN_TRUE: case TOKEN_FALSE: case TOKEN_NIL:
            return C_KEYWORD;
        /* type keywords */
        case TOKEN_TYPE_INT: case TOKEN_TYPE_I8: case TOKEN_TYPE_I16:
        case TOKEN_TYPE_I32: case TOKEN_TYPE_I64: case TOKEN_TYPE_U8:
        case TOKEN_TYPE_U16: case TOKEN_TYPE_U32: case TOKEN_TYPE_U64:
        case TOKEN_TYPE_F32: case TOKEN_TYPE_F64: case TOKEN_TYPE_BOOL:
        case TOKEN_TYPE_CHAR: case TOKEN_TYPE_VOID:
        case TOKEN_TYPE_LIB: case TOKEN_TYPE_OBJECT: case TOKEN_ARRAY:
        case TOKEN_BLOCK:
            return C_TYPE;
        /* literals */
        case TOKEN_STRING_LIT: case TOKEN_CHAR_LIT:
        case TOKEN_FSTRING_START: case TOKEN_FSTRING_TEXT: case TOKEN_FSTRING_END:
            return C_STRING;
        case TOKEN_INT_LIT: case TOKEN_FLOAT_LIT:
            return C_NUMBER;
        default:
            return NULL;
    }
}

/* Append helpers writing into a bounded buffer. */
#define HL_PUTS(str) do { const char *_s = (str); \
    while (*_s && o + 1 < out_cap) out[o++] = *_s++; } while (0)
#define HL_PUTN(ptr, n) do { for (int _i = 0; _i < (n) && o + 1 < out_cap; _i++) \
    out[o++] = (ptr)[_i]; } while (0)

/* Emit a raw span [a, b); if it begins (or contains) a `//` or `/*`, color the
   comment portion gray. */
static size_t emit_gap(char *out, size_t o, size_t out_cap, const char *a, const char *b) {
    const char *cmt = NULL;
    for (const char *p = a; p + 1 < b; p++) {
        if (p[0] == '/' && (p[1] == '/' || p[1] == '*')) { cmt = p; break; }
    }
    if (cmt) {
        HL_PUTN(a, (int)(cmt - a));
        HL_PUTS(C_COMMENT);
        HL_PUTN(cmt, (int)(b - cmt));
        HL_PUTS(C_RESET);
    } else {
        HL_PUTN(a, (int)(b - a));
    }
    return o;
}

void repl_highlight_render(const char *line, char *out, size_t out_cap) {
    size_t o = 0;
    Scanner s;
    scanner_init(&s, line);
    const char *cursor = line;   /* emitted up to here */

    for (;;) {
        Token t = scanner_next(&s);
        if (t.type == TOKEN_EOF || t.type == TOKEN_ERROR) break;

        if (t.start > cursor) {
            o = emit_gap(out, o, out_cap, cursor, t.start);
            cursor = t.start;
        }
        const char *color = ansi_for_token(t.type);
        if (color) HL_PUTS(color);
        HL_PUTN(t.start, t.length);
        if (color) HL_PUTS(C_RESET);
        cursor = t.start + t.length;
    }

    /* trailing remainder (whitespace / comment / unterminated literal) */
    if (*cursor) {
        o = emit_gap(out, o, out_cap, cursor, cursor + strlen(cursor));
    }
    out[o] = '\0';
}

/* ===================================================================== */
/* Color gating                                                          */
/* ===================================================================== */

bool repl_color_enabled(void) {
    if (getenv("NO_COLOR") != NULL) return false;
    return repl_term_stdout_is_tty();
}

/* ===================================================================== */
/* Line editor                                                           */
/* ===================================================================== */

struct ReplEditor {
    char **hist;
    int hist_count;
    int hist_cap;
    bool tty;       /* stdin is an interactive terminal */
    bool color;     /* stdout supports color */
};

ReplEditor *repl_editor_new(void) {
    ReplEditor *e = (ReplEditor *)malloc_safe(sizeof(ReplEditor));
    memset(e, 0, sizeof(*e));
    e->tty   = repl_term_stdin_is_tty();
    e->color = repl_color_enabled();
    /* The line editor relies on VT escapes (clear-line, cursor-column) for all
       interactive editing, so enable VT whenever we have a TTY — independent of
       whether colored output is wanted. */
    if (e->tty) repl_term_enable_vt();
    return e;
}

void repl_editor_free(ReplEditor *e) {
    if (!e) return;
    for (int i = 0; i < e->hist_count; i++) free(e->hist[i]);
    free(e->hist);
    free(e);
}

static void hist_push(ReplEditor *e, const char *s) {
    if (!s || !*s) return;
    /* Sanitize for a single-line editor: drop trailing newlines and flatten any
       internal newlines to spaces, so recalling an entry never injects a '\n'
       into the line buffer (which would desync the cursor-column math). */
    size_t len = strlen(s);
    while (len > 0 && (s[len-1] == '\n' || s[len-1] == '\r')) len--;
    if (len == 0) return;
    char *dup = (char *)malloc_safe(len + 1);
    for (size_t i = 0; i < len; i++)
        dup[i] = (s[i] == '\n' || s[i] == '\r') ? ' ' : s[i];
    dup[len] = '\0';

    if (e->hist_count > 0 && strcmp(e->hist[e->hist_count - 1], dup) == 0) {
        free(dup);
        return;
    }
    if (e->hist_count == e->hist_cap) {
        e->hist_cap = e->hist_cap ? e->hist_cap * 2 : 16;
        e->hist = (char **)realloc_safe(e->hist, (size_t)e->hist_cap * sizeof(char *));
    }
    e->hist[e->hist_count++] = dup;
}

/* ---- Fallback (non-TTY) path: plain fgets, no raw mode, no highlight ---- */
static char *read_fallback(ReplEditor *e, const char *prompt, const char *cont_prompt,
                           bool (*is_complete)(const char *buf)) {
    char *full = (char *)malloc_safe(1);
    full[0] = '\0';
    size_t full_len = 0;
    char line[4096];
    bool first = true;
    for (;;) {
        fputs(first ? prompt : cont_prompt, stdout);
        fflush(stdout);
        if (fgets(line, sizeof(line), stdin) == NULL) {
            if (full_len == 0) { free(full); return NULL; }
            return full;   /* EOF mid-input: hand back what we have */
        }
        size_t ll = strlen(line);
        full = (char *)realloc_safe(full, full_len + ll + 1);
        memcpy(full + full_len, line, ll + 1);
        full_len += ll;
        first = false;
        /* strip a trailing newline only for the completeness check copy */
        if (is_complete(full)) {
            (void)e;
            return full;
        }
    }
}

/* Editing state for one physical line. */
typedef struct {
    char  *buf;
    size_t len;
    size_t cap;
    size_t cursor;     /* index into buf */
    /* What is currently on screen, so redraw() can rewrite only the part that
       changed instead of clearing and reprinting the whole line every key. */
    char  *shown;
    size_t shown_len;
    size_t shown_cap;
    size_t prompt_cols;  /* prompt width currently drawn (0 = fresh line) */
} LineBuf;

static void lb_init(LineBuf *l) {
    l->cap = 128;
    l->buf = (char *)malloc_safe(l->cap);
    l->buf[0] = '\0';
    l->len = 0;
    l->cursor = 0;
    l->shown_cap = 128;
    l->shown = (char *)malloc_safe(l->shown_cap);
    l->shown[0] = '\0';
    l->shown_len = 0;
    l->prompt_cols = 0;
}
static void lb_free(LineBuf *l) {
    free(l->buf);
    free(l->shown);
}
static void lb_ensure(LineBuf *l, size_t extra) {
    if (l->len + extra + 1 > l->cap) {
        while (l->len + extra + 1 > l->cap) l->cap *= 2;
        l->buf = (char *)realloc_safe(l->buf, l->cap);
    }
}
static void lb_insert(LineBuf *l, char c) {
    lb_ensure(l, 1);
    memmove(l->buf + l->cursor + 1, l->buf + l->cursor, l->len - l->cursor + 1);
    l->buf[l->cursor] = c;
    l->len++;
    l->cursor++;
}
static void lb_backspace(LineBuf *l) {
    if (l->cursor == 0) return;
    memmove(l->buf + l->cursor - 1, l->buf + l->cursor, l->len - l->cursor + 1);
    l->len--;
    l->cursor--;
}
static void lb_delete(LineBuf *l) {
    if (l->cursor >= l->len) return;
    memmove(l->buf + l->cursor, l->buf + l->cursor + 1, l->len - l->cursor);
    l->len--;
}
static void lb_set(LineBuf *l, const char *s) {
    size_t n = strlen(s);
    lb_ensure(l, n);
    memcpy(l->buf, s, n + 1);
    l->len = n;
    l->cursor = n;
}

/* Differential repaint: rewrite only from the first character that differs
   from what is already on screen. Ordinary typing (append at end) emits just
   the new character — no whole-line clear — which removes the per-keystroke
   flicker of the naive "\r clear prompt text" version. The prompt is laid down
   once per physical line (tracked via prompt_cols).
   NOTE: still a single-row model (absolute-column cursor); terminal line
   wrapping is the second cut. Highlighting stays disabled for now —
   repl_highlight_render still exists (and is unit-tested) for later re-enable. */
static void redraw(LineBuf *l, const char *prompt) {
    size_t plen = strlen(prompt);

    /* Fresh physical line: lay down the prompt from column 0 once. */
    if (l->prompt_cols == 0) {
        fputs("\r\x1b[K", stdout);
        fputs(prompt, stdout);
        l->prompt_cols = plen;
        l->shown[0] = '\0';
        l->shown_len = 0;
    }

    /* Longest common prefix between what is shown and the new buffer. */
    size_t i = 0;
    while (i < l->shown_len && i < l->len && l->shown[i] == l->buf[i]) i++;

    /* Jump to the first differing column and rewrite the tail. */
    printf("\r\x1b[%zuG", plen + i + 1);
    if (i < l->len) fputs(l->buf + i, stdout);
    /* If the line shrank, clear the now-stale trailing characters. */
    if (l->shown_len > l->len) fputs("\x1b[K", stdout);

    /* Place the cursor at its logical column. */
    printf("\r\x1b[%zuG", plen + l->cursor + 1);
    fflush(stdout);

    /* Remember what is now on screen. */
    if (l->len + 1 > l->shown_cap) {
        while (l->len + 1 > l->shown_cap) l->shown_cap *= 2;
        l->shown = (char *)realloc_safe(l->shown, l->shown_cap);
    }
    memcpy(l->shown, l->buf, l->len + 1);
    l->shown_len = l->len;
}

/* ---- Interactive (TTY) path ---- */
static char *read_interactive(ReplEditor *e, const char *prompt, const char *cont_prompt,
                              bool (*is_complete)(const char *buf)) {
    char  *full = (char *)malloc_safe(1);
    full[0] = '\0';
    size_t full_len = 0;
    const char *cur_prompt = prompt;
    char  cont_buf[80];             /* dynamic continuation prompt */
    int   empty_streak = 0;         /* consecutive blank lines in continuation */
    int hist_idx = e->hist_count;   /* one past the end = "current" */

    repl_term_raw_enable();

    LineBuf l;
    lb_init(&l);
    redraw(&l, cur_prompt);

    char *result = NULL;
    bool quit = false;

    for (;;) {
        int k = repl_term_read_key();

        if (k == '\r' || k == '\n') {                 /* submit physical line */
            bool empty_line = (l.len == 0);
            fputs("\r\n", stdout);
            fflush(stdout);
            full = (char *)realloc_safe(full, full_len + l.len + 2);
            memcpy(full + full_len, l.buf, l.len);
            full_len += l.len;
            full[full_len++] = '\n';
            full[full_len] = '\0';
            if (is_complete(full)) {
                result = full;
                break;
            }
            /* Incomplete. A blank line in continuation is the escape hatch: the
               first one warns, a second consecutive one force-submits the
               incomplete buffer so the parser reports the real error (e.g.
               "expected '}'") instead of trapping the user in endless `...>`.
               A single blank line still lets you keep a blank line in a body. */
            if (empty_line) {
                if (++empty_streak >= 2) {
                    result = full;   /* force-evaluate; caller surfaces the error */
                    break;
                }
                fputs("  (blank line again to run as-is, Ctrl-C to cancel)\r\n",
                      stdout);
                fflush(stdout);
            } else {
                empty_streak = 0;
            }
            /* continuation line: free the finished line buffer, start fresh.
               The prompt hints what is still unclosed (e.g. "..{> "). */
            lb_free(&l);
            lb_init(&l);
            repl_continuation_prompt(full, cont_buf, sizeof(cont_buf));
            cur_prompt = cont_buf;
            (void)cont_prompt;       /* dynamic prompt supersedes the fixed one */
            redraw(&l, cur_prompt);
            continue;
        }
        else if (k == 3) {                              /* Ctrl-C: cancel input */
            fputs("^C\r\n", stdout);
            fflush(stdout);
            free(full);
            full = (char *)malloc_safe(1);
            full[0] = '\0';
            result = full;   /* empty → caller re-prompts */
            break;
        }
        else if (k == 4) {                              /* Ctrl-D / EOF */
            if (l.len == 0 && full_len == 0) {
                fputs("\r\n", stdout);
                fflush(stdout);
                free(full);
                result = NULL;
                quit = true;
                break;
            }
            /* otherwise ignore */
        }
        else if (k == 8 || k == 127) {                  /* Backspace */
            lb_backspace(&l);
            redraw(&l, cur_prompt);
        }
        else if (k == K_DELETE) {
            lb_delete(&l);
            redraw(&l, cur_prompt);
        }
        else if (k == K_LEFT) {
            if (l.cursor > 0) { l.cursor--; redraw(&l, cur_prompt); }
        }
        else if (k == K_RIGHT) {
            if (l.cursor < l.len) { l.cursor++; redraw(&l, cur_prompt); }
        }
        else if (k == K_HOME) {
            l.cursor = 0; redraw(&l, cur_prompt);
        }
        else if (k == K_END) {
            l.cursor = l.len; redraw(&l, cur_prompt);
        }
        else if (k == K_UP) {
            if (hist_idx > 0) {
                hist_idx--;
                lb_set(&l, e->hist[hist_idx]);
                redraw(&l, cur_prompt);
            }
        }
        else if (k == K_DOWN) {
            if (hist_idx < e->hist_count) {
                hist_idx++;
                lb_set(&l, hist_idx == e->hist_count ? "" : e->hist[hist_idx]);
                redraw(&l, cur_prompt);
            }
        }
        else if (k >= 32 && k != 127) {                 /* printable (and UTF-8) */
            lb_insert(&l, (char)k);
            redraw(&l, cur_prompt);
        }
        /* k == -2 (ignored nav) and other control chars: no-op */
    }

    lb_free(&l);
    repl_term_raw_disable();

    if (!quit && result && *result) hist_push(e, result);
    return result;
}

char *repl_editor_read(ReplEditor *e, const char *prompt, const char *cont_prompt,
                       bool (*is_complete)(const char *buf)) {
    if (!e->tty) return read_fallback(e, prompt, cont_prompt, is_complete);
    return read_interactive(e, prompt, cont_prompt, is_complete);
}
