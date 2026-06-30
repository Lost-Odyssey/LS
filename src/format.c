/* format.c — LS source formatter. See format.h.
 *
 * Strategy (P1): drive the real scanner to get the exact token stream, recover
 * comments from the whitespace gaps between tokens (a gap between two code
 * tokens contains only whitespace + comments by construction), collapse each
 * f-string run to one raw atom, then re-emit:
 *   - indentation = brace depth * 4 (leading '}' dedents; open paren/bracket
 *     adds one continuation level)
 *   - canonical intra-line spacing driven by a (prev,cur) rule table
 *   - blank-line runs collapsed to at most one
 * User line breaks are preserved (no long-line reflow yet).
 */
#include "scanner.h"
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>

/* ---- growable buffer ---- */
typedef struct { char *data; size_t len, cap; } Buf;

static void buf_init(Buf *b) {
    b->cap = 1024; b->len = 0;
    b->data = (char *)malloc(b->cap);
    b->data[0] = '\0';
}
static void buf_reserve(Buf *b, size_t add) {
    if (b->len + add + 1 > b->cap) {
        while (b->len + add + 1 > b->cap) b->cap *= 2;
        b->data = (char *)realloc(b->data, b->cap);
    }
}
static void buf_put(Buf *b, const char *s, size_t n) {
    buf_reserve(b, n);
    memcpy(b->data + b->len, s, n);
    b->len += n;
    b->data[b->len] = '\0';
}
static void buf_putc(Buf *b, char c) { buf_put(b, &c, 1); }
/* copy a source slice, dropping '\r' so the buffer is uniformly LF */
static void buf_put_lf(Buf *b, const char *s, size_t n) {
    for (size_t i = 0; i < n; i++) if (s[i] != '\r') buf_putc(b, s[i]);
}
static void buf_indent(Buf *b, int level) {
    for (int i = 0; i < level; i++) buf_put(b, "    ", 4);
}

/* ---- display token: code tokens with f-strings collapsed to one atom ---- */
typedef struct {
    TokenType type;
    const char *start;
    int length;
    int line;
} DTok;

/* Does this token type end a value (so a following + - * & is binary, and a
 * following ( or [ is a call/index)? */
static bool is_value_end(TokenType t) {
    switch (t) {
    case TOKEN_IDENTIFIER:
    case TOKEN_INT_LIT: case TOKEN_FLOAT_LIT: case TOKEN_STRING_LIT:
    case TOKEN_CHAR_LIT: case TOKEN_TRUE: case TOKEN_FALSE: case TOKEN_NIL:
    case TOKEN_SELF:
    case TOKEN_RPAREN: case TOKEN_RBRACKET:
    case TOKEN_TYPE_INT: case TOKEN_TYPE_I8: case TOKEN_TYPE_I16:
    case TOKEN_TYPE_I32: case TOKEN_TYPE_I64: case TOKEN_TYPE_U8:
    case TOKEN_TYPE_U16: case TOKEN_TYPE_U32: case TOKEN_TYPE_U64:
    case TOKEN_TYPE_F32: case TOKEN_TYPE_F64: case TOKEN_TYPE_BOOL:
    case TOKEN_TYPE_CHAR: case TOKEN_TYPE_VOID: case TOKEN_TYPE_OBJECT:
    case TOKEN_TYPE_LIB: case TOKEN_ARRAY: case TOKEN_BLOCK:
        return true;
    default:
        return false;
    }
}

static bool is_prefixable(TokenType t) {
    return t == TOKEN_STAR || t == TOKEN_AMP || t == TOKEN_MINUS ||
           t == TOKEN_BANG || t == TOKEN_TILDE || t == TOKEN_PLUS;
}

/* Should there be a space between prev and cur on the same line? */
static bool space_between(TokenType prev, bool prev_value_end,
                         bool prev_prefix_op, TokenType cur) {
    /* legacy `methods(T)` form is retired; this rule is now inert (methods never directly precedes `(`) */
    if (prev == TOKEN_IMPL && cur == TOKEN_LPAREN) return false;
    /* glue after openers */
    if (prev == TOKEN_LPAREN || prev == TOKEN_LBRACKET) return false;
    if (prev == TOKEN_LBRACE && cur == TOKEN_RBRACE) return false; /* {} */
    /* glue before closers / separators */
    if (cur == TOKEN_RPAREN || cur == TOKEN_RBRACKET) return false;
    if (cur == TOKEN_COMMA || cur == TOKEN_SEMICOLON) return false;
    /* member access / ranges */
    if (cur == TOKEN_DOT || prev == TOKEN_DOT) return false;
    if (cur == TOKEN_DOTDOT || prev == TOKEN_DOTDOT) return false;
    if (cur == TOKEN_ELLIPSIS || prev == TOKEN_ELLIPSIS) return false;
    /* prefix unary operator glues to its operand */
    if (prev_prefix_op) return false;
    /* call / index */
    if ((cur == TOKEN_LPAREN || cur == TOKEN_LBRACKET) && prev_value_end)
        return false;
    /* postfix force-unwrap: x! */
    if (cur == TOKEN_BANG && prev_value_end) return false;
    /* colon: no space before, space after */
    if (cur == TOKEN_COLON) return false;
    /* annotation @ glues to its name */
    if (prev == TOKEN_AT || prev == TOKEN_AT_TIME || prev == TOKEN_AT_BENCH)
        return false;
    return true;
}

/* display width in code points (UTF-8 continuation bytes don't add a column) */
static int cp_width(const char *p, const char *end) {
    int w = 0;
    for (const char *q = p; q < end; q++) if ((*q & 0xC0) != 0x80) w++;
    return w;
}

/* gofmt-style trailing-comment alignment: within a run of consecutive lines
   that each end in a trailing comment, align the comments to one column.
   `coff` holds the byte offset (into `s`) of each trailing comment's start,
   in ascending order (recorded during emission, so string/`//`-in-string
   confusion is impossible). Returns a new buffer, or NULL if nothing to do. */
static char *align_trailing(const char *s, size_t slen, const size_t *coff, int noff) {
    if (noff == 0) return NULL;
    int nlines = 1;
    for (size_t i = 0; i < slen; i++) if (s[i] == '\n') nlines++;
    size_t *lstart = (size_t *)malloc((size_t)nlines * sizeof(size_t));
    size_t *lend   = (size_t *)malloc((size_t)nlines * sizeof(size_t));
    size_t *ccol   = (size_t *)malloc((size_t)nlines * sizeof(size_t));
    int li = 0; lstart[0] = 0;
    for (size_t i = 0; i < slen; i++)
        if (s[i] == '\n') { lend[li] = i; li++; lstart[li] = i + 1; }
    lend[li] = slen;
    for (int i = 0; i < nlines; i++) ccol[i] = (size_t)-1;
    int ci = 0;
    for (int l = 0; l < nlines; l++) {
        while (ci < noff && coff[ci] < lstart[l]) ci++;
        if (ci < noff && coff[ci] > lstart[l] && coff[ci] < lend[l]) ccol[l] = coff[ci];
    }
    Buf out; buf_init(&out);
    for (int l = 0; l < nlines; ) {
        if (ccol[l] == (size_t)-1) {
            buf_put(&out, s + lstart[l], lend[l] - lstart[l]);
            if (l < nlines - 1) buf_putc(&out, '\n');
            l++;
        } else {
            int g1 = l;
            while (g1 < nlines && ccol[g1] != (size_t)-1) g1++;
            int maxw = 0;
            for (int g = l; g < g1; g++) {
                int w = cp_width(s + lstart[g], s + (ccol[g] - 1));
                if (w > maxw) maxw = w;
            }
            for (int g = l; g < g1; g++) {
                size_t code_end = ccol[g] - 1;           /* drop the single space */
                buf_put(&out, s + lstart[g], code_end - lstart[g]);
                int w = cp_width(s + lstart[g], s + code_end);
                for (int p = 0; p < (maxw - w) + 1; p++) buf_putc(&out, ' ');
                buf_put(&out, s + ccol[g], lend[g] - ccol[g]);
                if (g < nlines - 1) buf_putc(&out, '\n');
            }
            l = g1;
        }
    }
    free(lstart); free(lend); free(ccol);
    return out.data;
}

char *ls_format_source(const char *source) {
    /* preserve the source's line-ending convention (CRLF vs LF) */
    bool use_crlf = (strstr(source, "\r\n") != NULL);

    /* 1. collect raw tokens */
    Scanner sc;
    scanner_init(&sc, source);
    int rcap = 256, rn = 0;
    Token *raw = (Token *)malloc((size_t)rcap * sizeof(Token));
    for (;;) {
        if (rn >= rcap) { rcap *= 2; raw = (Token *)realloc(raw, (size_t)rcap * sizeof(Token)); }
        Token t = scanner_next(&sc);
        raw[rn++] = t;
        if (t.type == TOKEN_EOF) break;
        if (t.type == TOKEN_ERROR) { free(raw); return NULL; }
    }

    /* 2. collapse f-string runs to one atom (start..end of the whole literal) */
    int dcap = rn + 1, dn = 0;
    DTok *D = (DTok *)malloc((size_t)dcap * sizeof(DTok));
    for (int i = 0; i < rn; i++) {
        if (raw[i].type == TOKEN_FSTRING_START) {
            int depth = 0, j = i;
            for (; j < rn; j++) {
                if (raw[j].type == TOKEN_FSTRING_START) depth++;
                else if (raw[j].type == TOKEN_FSTRING_END) { depth--; if (depth == 0) break; }
            }
            int end = (j < rn) ? j : rn - 1;
            const char *e = raw[end].start + raw[end].length;
            D[dn].type = TOKEN_STRING_LIT;
            D[dn].start = raw[i].start;
            D[dn].length = (int)(e - raw[i].start);
            D[dn].line = raw[i].line;
            dn++;
            i = end;
        } else {
            D[dn].type = raw[i].type;
            D[dn].start = raw[i].start;
            D[dn].length = raw[i].length;
            D[dn].line = raw[i].line;
            dn++;
        }
    }

    /* 2b. match braces; a block is "multi-line" if '{' and '}' sit on
       different source lines. match[i] = paired index, or -1. */
    int *match = (int *)malloc((size_t)dn * sizeof(int));
    for (int i = 0; i < dn; i++) match[i] = -1;
    {
        int *stk = (int *)malloc((size_t)dn * sizeof(int) + 1);
        int sp = 0;
        for (int i = 0; i < dn; i++) {
            if (D[i].type == TOKEN_LBRACE) stk[sp++] = i;
            else if (D[i].type == TOKEN_RBRACE && sp > 0) {
                int o = stk[--sp];
                match[o] = i; match[i] = o;
            }
        }
        free(stk);
    }

    /* 3. emit */
    Buf b; buf_init(&b);
    size_t *trail = (size_t *)malloc(sizeof(size_t) * (size_t)(rn + 8)); /* trailing-comment offsets */
    int ntrail = 0;
    int brace_depth = 0, paren_depth = 0;
    bool started = false;
    bool force_open_next = false;   /* next token must start a new line (after a multi-line '{') */
    bool prev_value_end = false;
    bool prev_prefix_op = false;
    TokenType prev_type = TOKEN_EOF;
    TokenType prev_code = TOKEN_EOF; /* last emitted code token */

    for (int i = 0; i < dn; i++) {
        DTok *d = &D[i];
        bool is_eof = (d->type == TOKEN_EOF);

        /* gap before this token: [gs, ge) is whitespace + comments only */
        const char *gs = (i == 0) ? source : (D[i-1].start + D[i-1].length);
        const char *ge = d->start;
        const char *p = gs;
        int pending_nl = 0;

        while (p < ge) {
            char c = *p;
            if (c == '\n') { pending_nl++; p++; continue; }
            if (c == ' ' || c == '\t' || c == '\r') { p++; continue; }
            if (c == '/' && p + 1 < ge && p[1] == '/') {
                const char *cs = p;
                while (p < ge && *p != '\n') p++;
                int clen = (int)(p - cs);
                while (clen > 0 && (cs[clen-1] == ' ' || cs[clen-1] == '\t' || cs[clen-1] == '\r')) clen--;
                if (pending_nl == 0 && started) {
                    /* trailing comment: keep on the current line */
                    buf_putc(&b, ' ');
                    trail[ntrail++] = b.len;   /* record for trailing-comment alignment */
                    buf_put_lf(&b, cs, (size_t)clen);
                } else {
                    /* standalone comment on its own line */
                    if (started) {
                        buf_putc(&b, '\n');
                        if (pending_nl >= 2) buf_putc(&b, '\n'); /* one blank max */
                    }
                    buf_indent(&b, brace_depth + (paren_depth > 0 ? 1 : 0));
                    buf_put_lf(&b, cs, (size_t)clen);
                    started = true;
                }
                pending_nl = 0;
                prev_type = TOKEN_EOF;        /* force next real token onto a new line */
                prev_value_end = false;
                prev_prefix_op = false;
                continue;
            }
            if (c == '/' && p + 1 < ge && p[1] == '*') {
                const char *cs = p;
                p += 2;
                while (p + 1 < ge && !(*p == '*' && p[1] == '/')) p++;
                if (p + 1 < ge) p += 2; else p = ge;
                int clen = (int)(p - cs);
                if (pending_nl == 0 && started) {
                    buf_putc(&b, ' ');
                    trail[ntrail++] = b.len;   /* record for trailing-comment alignment */
                    buf_put_lf(&b, cs, (size_t)clen);
                } else {
                    if (started) {
                        buf_putc(&b, '\n');
                        if (pending_nl >= 2) buf_putc(&b, '\n');
                    }
                    buf_indent(&b, brace_depth + (paren_depth > 0 ? 1 : 0));
                    buf_put_lf(&b, cs, (size_t)clen);
                    started = true;
                }
                pending_nl = 0;
                prev_type = TOKEN_EOF;
                prev_value_end = false;
                prev_prefix_op = false;
                continue;
            }
            /* unexpected char in a gap — skip defensively */
            p++;
        }

        if (is_eof) break;

        /* a multi-line block forces its first stmt and its '}' onto own lines */
        bool forced_open = force_open_next;
        force_open_next = false;
        bool force_close = (d->type == TOKEN_RBRACE && match[i] >= 0 &&
                           D[match[i]].line != d->line);
        bool new_line = (pending_nl > 0) || !started || forced_open || force_close;

        if (new_line) {
            if (started) {
                buf_putc(&b, '\n');
                /* blank line (collapsed), suppressed around braces */
                bool suppress = (d->type == TOKEN_RBRACE) || (prev_code == TOKEN_LBRACE);
                if (pending_nl >= 2 && !suppress) buf_putc(&b, '\n');
            }
            int eff = brace_depth - (d->type == TOKEN_RBRACE ? 1 : 0);
            if (eff < 0) eff = 0;
            int cont;
            if (d->type == TOKEN_RPAREN || d->type == TOKEN_RBRACKET)
                cont = (paren_depth - 1 > 0) ? 1 : 0;
            else
                cont = (paren_depth > 0) ? 1 : 0;
            buf_indent(&b, eff + cont);
        } else if (started) {
            /* same line: decide spacing */
            if (space_between(prev_type, prev_value_end, prev_prefix_op, d->type))
                buf_putc(&b, ' ');
        }

        /* emit the token verbatim (slice of source), normalized to LF */
        buf_put_lf(&b, d->start, (size_t)d->length);
        started = true;

        /* update depth */
        if (d->type == TOKEN_LBRACE) {
            brace_depth++;
            if (match[i] >= 0 && D[match[i]].line != d->line) force_open_next = true;
        }
        else if (d->type == TOKEN_RBRACE) { brace_depth--; if (brace_depth < 0) brace_depth = 0; }
        else if (d->type == TOKEN_LPAREN || d->type == TOKEN_LBRACKET) paren_depth++;
        else if (d->type == TOKEN_RPAREN || d->type == TOKEN_RBRACKET) { paren_depth--; if (paren_depth < 0) paren_depth = 0; }

        /* update spacing state */
        bool this_prefix = false;
        if (is_prefixable(d->type)) {
            this_prefix = !prev_value_end; /* prefix if prev wasn't a value */
        }
        prev_prefix_op = this_prefix;

        if (d->type == TOKEN_BANG) {
            /* postfix unwrap keeps value-end; prefix '!' does not */
            prev_value_end = prev_value_end; /* postfix x! -> value ; prefix !x -> not */
            prev_value_end = !this_prefix ? true : false;
        } else {
            prev_value_end = is_value_end(d->type);
        }
        prev_type = d->type;
        prev_code = d->type;
    }

    /* ensure file ends with exactly one newline */
    while (b.len > 0 && (b.data[b.len-1] == '\n' || b.data[b.len-1] == ' ' ||
                         b.data[b.len-1] == '\t' || b.data[b.len-1] == '\r')) {
        b.len--;
    }
    b.data[b.len] = '\0';
    buf_putc(&b, '\n');

    free(raw);
    free(D);
    free(match);

    /* align runs of trailing comments (gofmt-style) */
    char *aligned = align_trailing(b.data, b.len, trail, ntrail);
    free(trail);
    if (aligned != NULL) { free(b.data); b.data = aligned; b.len = strlen(aligned); }

    /* re-expand to the source's line-ending convention */
    if (use_crlf) {
        Buf c; buf_init(&c);
        for (size_t i = 0; i < b.len; i++) {
            if (b.data[i] == '\n') buf_putc(&c, '\r');
            buf_putc(&c, b.data[i]);
        }
        free(b.data);
        return c.data;
    }
    return b.data;
}
