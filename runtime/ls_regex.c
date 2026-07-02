/* runtime/ls_regex.c — LS built-in regex engine: NFA compiler + Pike VM
 *
 * Design: Pike VM with per-thread saved[] arrays for O(n*m) regex with
 * capture groups.  No backtracking, no ReDoS.
 *
 * Supported: . ^ $ \A \Z * + ? {n,m} lazy-variants | [...] \d\w\s\b
 *            (...) (?:...) (?<name>...) (?=...) (?!...) (?i)(?m)(?s)
 * Not supported: backreferences \1, lookbehind (?<=...), Unicode \p{...}
 */

#include "ls_regex.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

/* ===== Constants ===== */

#define MAX_GROUPS    17    /* group 0 = full match, 1..16 = captures */
#define MAX_NAMED     16
#define MAX_HANDLES   32
#define MAX_INSTRS    2048
#define MAX_THREADS   512
#define NAME_MAX_LEN  64

/* ===== Opcodes ===== */

typedef enum {
    OP_CHAR,        /* match literal char; operand_a = char code */
    OP_ANY,         /* . */
    OP_CLASS,       /* [abc]; operand_a = class_id */
    OP_NCLASS,      /* [^abc]; operand_a = class_id */
    OP_DIGIT,       /* \d */
    OP_NDIGIT,      /* \D */
    OP_WORD,        /* \w */
    OP_NWORD,       /* \W */
    OP_SPACE,       /* \s */
    OP_NSPACE,      /* \S */
    OP_ANCHOR_BOL,  /* ^ */
    OP_ANCHOR_EOL,  /* $ */
    OP_ANCHOR_BOS,  /* \A */
    OP_ANCHOR_EOS,  /* \Z */
    OP_WORDBND,     /* \b */
    OP_NWORDBND,    /* \B */
    OP_SAVE,        /* operand_a = slot index (group_id*2 + open/close) */
    OP_SPLIT,       /* fork; operand_a = offset A (greedy first),
                             operand_b = offset B */
    OP_JUMP,        /* operand_a = absolute target pc */
    OP_LOOKAHEAD,   /* operand_a = pc after lookahead content (sub_end+1),
                       operand_b = is_negative */
    OP_MATCH,       /* success */
} ReOpCode;

/* ===== Instruction ===== */

typedef struct {
    ReOpCode op;
    int      operand_a;
    int      operand_b;
} ReInstr;

/* ===== Character class (bitmap for ASCII 0-127) ===== */

#define CLASS_BITMAP_BYTES 16   /* 128 bits */

typedef struct {
    unsigned char bits[CLASS_BITMAP_BYTES];
} ReCharClass;

#define MAX_CLASSES 64

/* ===== Named group ===== */

typedef struct {
    char name[NAME_MAX_LEN];
    int  group_id;   /* 1-based */
} NamedGroup;

/* ===== Pattern handle ===== */

typedef struct {
    int          used;
    int          flags;         /* LS_RE_* bits */
    int          n_groups;      /* number of capture groups (excl. group 0) */
    ReInstr      prog[MAX_INSTRS];
    int          prog_len;
    ReCharClass  classes[MAX_CLASSES];
    int          n_classes;
    NamedGroup   named[MAX_NAMED];
    int          n_named;
} ReHandle;

static ReHandle g_pool[MAX_HANDLES];
static int      g_last_saved[MAX_GROUPS * 2];
static char     g_last_error[256];

/* ===== Thread ===== */

typedef struct {
    int pc;
    int saved[MAX_GROUPS * 2];
} ReThread;

/* ===== Utility ===== */

static void class_set(ReCharClass *cls, unsigned char c) {
    cls->bits[c >> 3] |= (unsigned char)(1u << (c & 7));
}
static int class_test(const ReCharClass *cls, unsigned char c) {
    return (cls->bits[c >> 3] >> (c & 7)) & 1;
}

static int is_word_char(unsigned char c) {
    return isalnum(c) || c == '_';
}
static int is_space_char(unsigned char c) {
    return c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '\f' || c == '\v';
}

/* ===== Compiler state ===== */

typedef struct {
    const char *pat;
    int         pos;
    int         pat_len;
    ReHandle   *re;
    int         group_counter;
    char        error[256];
    int         had_error;
    int         depth;          /* current group-nesting depth (recursion guard) */
} Compiler;

/* Cap on regex group nesting. Deeply-nested groups ("(((((...") would otherwise
   recurse until the native stack overflows and the process crashes. Real
   patterns never nest anywhere near this; found by stdfuzz (crash at ~2000). */
#define RE_MAX_DEPTH 256

static void comp_error(Compiler *c, const char *msg) {
    if (!c->had_error) {
        snprintf(c->error, sizeof(c->error), "%s", msg);
        c->had_error = 1;
    }
}

static int  emit(ReHandle *re, ReOpCode op, int a, int b) {
    if (re->prog_len >= MAX_INSTRS - 1) return -1;
    re->prog[re->prog_len].op         = op;
    re->prog[re->prog_len].operand_a  = a;
    re->prog[re->prog_len].operand_b  = b;
    return re->prog_len++;
}

/* Forward declarations for recursive descent */
static int parse_expr(Compiler *c);
static int parse_expr_inner(Compiler *c);
static int parse_concat(Compiler *c);
static int parse_piece(Compiler *c);
static int parse_atom(Compiler *c);

/* ---- Character class parsing ---- */

static int parse_class_body(Compiler *c) {
    ReHandle *re = c->re;
    if (re->n_classes >= MAX_CLASSES) { comp_error(c, "too many char classes"); return -1; }
    int cid = re->n_classes++;
    ReCharClass *cls = &re->classes[cid];
    memset(cls, 0, sizeof(*cls));

    int negate = 0;
    if (c->pos < c->pat_len && c->pat[c->pos] == '^') { negate = 1; c->pos++; }

    /* Allow ] as first char */
    int first = 1;
    int closed = 0;
    while (c->pos < c->pat_len) {
        unsigned char ch = (unsigned char)c->pat[c->pos];
        if (ch == ']' && !first) { closed = 1; break; }
        first = 0;
        c->pos++;

        if (ch == '\\' && c->pos < c->pat_len) {
            unsigned char esc = (unsigned char)c->pat[c->pos++];
            switch (esc) {
                case 'd': for (int i='0';i<='9';i++) class_set(cls,(unsigned char)i); break;
                case 'D': for (int i=0;i<128;i++) if (!isdigit(i)) class_set(cls,(unsigned char)i); break;
                case 'w': for (int i=0;i<128;i++) if (is_word_char((unsigned char)i)) class_set(cls,(unsigned char)i); break;
                case 'W': for (int i=0;i<128;i++) if (!is_word_char((unsigned char)i)) class_set(cls,(unsigned char)i); break;
                case 's': for (int i=0;i<128;i++) if (is_space_char((unsigned char)i)) class_set(cls,(unsigned char)i); break;
                case 'S': for (int i=0;i<128;i++) if (!is_space_char((unsigned char)i)) class_set(cls,(unsigned char)i); break;
                case 'n': class_set(cls,'\n'); break;
                case 'r': class_set(cls,'\r'); break;
                case 't': class_set(cls,'\t'); break;
                default:  class_set(cls,esc); break;
            }
            continue;
        }

        /* Check for range a-z */
        if (c->pos + 1 < c->pat_len && c->pat[c->pos] == '-' && c->pat[c->pos+1] != ']') {
            c->pos++;  /* consume '-' */
            unsigned char hi = (unsigned char)c->pat[c->pos++];
            if (hi == '\\' && c->pos < c->pat_len) hi = (unsigned char)c->pat[c->pos++];
            if (hi < ch) { comp_error(c, "invalid character range"); return -1; }
            for (unsigned char k = ch; k <= hi; k++) class_set(cls, k);
        } else {
            class_set(cls, ch);
        }
    }
    if (!closed) { comp_error(c, "unclosed character class '['"); return -1; }
    c->pos++;  /* consume ']' */

    if (negate) {
        /* Flip all bits for ASCII range */
        for (int i = 0; i < CLASS_BITMAP_BYTES; i++)
            cls->bits[i] = (unsigned char)(~cls->bits[i]);
        /* But keep non-ASCII (>127) always off by clearing the upper half */
        /* We only care about bits 0-127 */
    }

    return cid;
}

/* ---- Quantifier application ----
   Wraps the sub-program [start..end) with a quantifier.
   Returns new end pc (or -1 on error). */

static int apply_quantifier(Compiler *c, int start, char qc, int lazy) {
    ReHandle *re = c->re;
    int end = re->prog_len;

    if (qc == '?') {
        /* SPLIT(start, end+1) ... [body] ... */
        /* Insert SPLIT before body: shift body instructions */
        if (end - start > MAX_INSTRS - 3) return -1;
        /* Move body forward by 1 slot */
        memmove(&re->prog[start+1], &re->prog[start], (size_t)(end - start) * sizeof(ReInstr));
        re->prog_len++;
        end++;
        re->prog[start].op = OP_SPLIT;
        if (lazy) {
            re->prog[start].operand_a = end;    /* skip first = lazy */
            re->prog[start].operand_b = start+1;
        } else {
            re->prog[start].operand_a = start+1; /* try body first = greedy */
            re->prog[start].operand_b = end;
        }
        return end;
    }

    if (qc == '*') {
        /* SPLIT(start+1, end+1)  [body]  JUMP(start)  */
        memmove(&re->prog[start+1], &re->prog[start], (size_t)(end - start) * sizeof(ReInstr));
        re->prog_len++;
        end++;
        /* append JUMP back to SPLIT */
        int jmp = emit(re, OP_JUMP, start, 0);
        if (jmp < 0) return -1;
        end = re->prog_len;
        re->prog[start].op = OP_SPLIT;
        if (lazy) {
            re->prog[start].operand_a = jmp+1;  /* skip loop first */
            re->prog[start].operand_b = start+1;
        } else {
            re->prog[start].operand_a = start+1; /* try body first */
            re->prog[start].operand_b = jmp+1;
        }
        return end;
    }

    if (qc == '+') {
        /* [body]  SPLIT(start, end+1) */
        int sp = emit(re, OP_SPLIT, 0, 0);
        if (sp < 0) return -1;
        end = re->prog_len;
        if (lazy) {
            re->prog[sp].operand_a = end;    /* exit first = lazy */
            re->prog[sp].operand_b = start;
        } else {
            re->prog[sp].operand_a = start;  /* loop first = greedy */
            re->prog[sp].operand_b = end;
        }
        return end;
    }

    return end;
}

/* ---- {n,m} expansion ----
   Appends repeated copies and optional SPLIT chains.
   body_start..body_end is the first copy already emitted. */

static int apply_count(Compiler *c, int body_start, int body_end,
                        int n_min, int n_max, int lazy)
{
    ReHandle *re = c->re;
    int body_len = body_end - body_start;

    /* Emit n_min-1 additional mandatory copies */
    for (int i = 1; i < n_min; i++) {
        if (re->prog_len + body_len >= MAX_INSTRS - 4) {
            comp_error(c, "{n,m}: pattern too large"); return -1;
        }
        memcpy(&re->prog[re->prog_len], &re->prog[body_start],
               (size_t)body_len * sizeof(ReInstr));
        re->prog_len += body_len;
    }

    if (n_max == -1) {
        /* {n,} → emit one more copy wrapped with * */
        int opt_start = re->prog_len;
        memcpy(&re->prog[opt_start], &re->prog[body_start],
               (size_t)body_len * sizeof(ReInstr));
        re->prog_len += body_len;
        return apply_quantifier(c, opt_start, '*', lazy);
    }

    /* {n,m}: emit (n_max - n_min) optional copies each wrapped with ? */
    for (int i = n_min; i < n_max; i++) {
        if (re->prog_len + body_len + 2 >= MAX_INSTRS - 4) {
            comp_error(c, "{n,m}: pattern too large"); return -1;
        }
        int opt_start = re->prog_len;
        memcpy(&re->prog[opt_start], &re->prog[body_start],
               (size_t)body_len * sizeof(ReInstr));
        re->prog_len += body_len;
        int r = apply_quantifier(c, opt_start, '?', lazy);
        if (r < 0) return -1;
    }
    return re->prog_len;
}

/* ---- Atom parsing ---- */

static int parse_escape(Compiler *c) {
    ReHandle *re = c->re;
    if (c->pos >= c->pat_len) { comp_error(c, "trailing backslash"); return -1; }
    unsigned char esc = (unsigned char)c->pat[c->pos++];
    switch (esc) {
        case 'd': return emit(re, OP_DIGIT,  0, 0);
        case 'D': return emit(re, OP_NDIGIT, 0, 0);
        case 'w': return emit(re, OP_WORD,   0, 0);
        case 'W': return emit(re, OP_NWORD,  0, 0);
        case 's': return emit(re, OP_SPACE,  0, 0);
        case 'S': return emit(re, OP_NSPACE, 0, 0);
        case 'b': return emit(re, OP_WORDBND,  0, 0);
        case 'B': return emit(re, OP_NWORDBND, 0, 0);
        case 'A': return emit(re, OP_ANCHOR_BOS, 0, 0);
        case 'Z': return emit(re, OP_ANCHOR_EOS, 0, 0);
        case 'n': return emit(re, OP_CHAR, '\n', 0);
        case 'r': return emit(re, OP_CHAR, '\r', 0);
        case 't': return emit(re, OP_CHAR, '\t', 0);
        case 'f': return emit(re, OP_CHAR, '\f', 0);
        case 'v': return emit(re, OP_CHAR, '\v', 0);
        default:  return emit(re, OP_CHAR, esc, 0);
    }
}

static int parse_atom(Compiler *c) {
    ReHandle *re = c->re;
    if (c->pos >= c->pat_len) return re->prog_len;

    unsigned char ch = (unsigned char)c->pat[c->pos];

    if (ch == '(') {
        c->pos++;
        int group_id   = 0;
        int non_capture = 0;

        /* Check for special group prefixes */
        if (c->pos < c->pat_len && c->pat[c->pos] == '?') {
            c->pos++;
            if (c->pos >= c->pat_len) { comp_error(c, "incomplete group flag"); return -1; }
            unsigned char nxt = (unsigned char)c->pat[c->pos];

            if (nxt == ':') {
                /* non-capturing */
                c->pos++;
                non_capture = 1;

            } else if (nxt == '<') {
                /* named capture (?<name>...) */
                c->pos++;
                /* read name until > */
                char name[NAME_MAX_LEN];
                int  nlen = 0;
                while (c->pos < c->pat_len && c->pat[c->pos] != '>') {
                    if (nlen < NAME_MAX_LEN - 1) name[nlen++] = c->pat[c->pos];
                    c->pos++;
                }
                if (c->pos >= c->pat_len) { comp_error(c, "unclosed named group"); return -1; }
                c->pos++; /* consume '>' */
                name[nlen] = '\0';
                group_id = ++c->group_counter;
                if (re->n_named < MAX_NAMED) {
                    NamedGroup *ng = &re->named[re->n_named++];
                    strncpy(ng->name, name, NAME_MAX_LEN - 1);
                    ng->name[NAME_MAX_LEN - 1] = '\0';
                    ng->group_id = group_id;
                }

            } else if (nxt == '=') {
                /* positive lookahead (?=...) */
                c->pos++;
                int la_start = re->prog_len;
                /* Emit placeholder LOOKAHEAD, fill in end later */
                int la_instr = emit(re, OP_LOOKAHEAD, 0, 0);
                if (la_instr < 0) return -1;
                int r = parse_expr(c);
                if (r < 0 || c->had_error) return -1;
                if (c->pos >= c->pat_len || c->pat[c->pos] != ')') {
                    comp_error(c, "unclosed lookahead '(?='"); return -1;
                }
                c->pos++;
                int sub_end = re->prog_len; /* first instr after lookahead body */
                emit(re, OP_MATCH, 0, 0);  /* terminator for sub-VM */
                re->prog[la_instr].operand_a = sub_end + 1; /* pc to resume if passes */
                re->prog[la_instr].operand_b = 0;           /* positive */
                (void)la_start;
                return re->prog_len;

            } else if (nxt == '!') {
                /* negative lookahead (?!...) */
                c->pos++;
                int la_instr = emit(re, OP_LOOKAHEAD, 0, 0);
                if (la_instr < 0) return -1;
                int r = parse_expr(c);
                if (r < 0 || c->had_error) return -1;
                if (c->pos >= c->pat_len || c->pat[c->pos] != ')') {
                    comp_error(c, "unclosed lookahead '(?!'"); return -1;
                }
                c->pos++;
                int sub_end = re->prog_len;
                emit(re, OP_MATCH, 0, 0);
                re->prog[la_instr].operand_a = sub_end + 1;
                re->prog[la_instr].operand_b = 1;  /* negative */
                return re->prog_len;

            } else if (nxt == 'i' || nxt == 'm' || nxt == 's') {
                /* inline flags (?i) (?m) (?s) */
                while (c->pos < c->pat_len && c->pat[c->pos] != ')') {
                    unsigned char fl = (unsigned char)c->pat[c->pos++];
                    if (fl == 'i') re->flags |= LS_RE_IGNORECASE;
                    else if (fl == 'm') re->flags |= LS_RE_MULTILINE;
                    else if (fl == 's') re->flags |= LS_RE_DOTALL;
                }
                if (c->pos < c->pat_len) c->pos++; /* consume ')' */
                return re->prog_len; /* zero-width */

            } else {
                comp_error(c, "unknown group flag"); return -1;
            }
        } else {
            /* regular capturing group */
            group_id = ++c->group_counter;
        }

        int save_open = -1;
        if (!non_capture && group_id > 0) {
            save_open = emit(re, OP_SAVE, group_id * 2, 0);
            if (save_open < 0) return -1;
        }
        int r = parse_expr(c);
        if (r < 0 || c->had_error) return -1;
        if (c->pos >= c->pat_len || c->pat[c->pos] != ')') {
            comp_error(c, "unclosed group '('"); return -1;
        }
        c->pos++;
        if (!non_capture && group_id > 0) {
            emit(re, OP_SAVE, group_id * 2 + 1, 0);
        }
        if (group_id > re->n_groups) re->n_groups = group_id;
        return re->prog_len;
    }

    if (ch == '[') {
        c->pos++;
        int cid = parse_class_body(c);
        if (cid < 0 || c->had_error) return -1;
        /* Check if negated (we track this by whether OP_CLASS or OP_NCLASS) */
        /* Since parse_class_body handles negation via bit-flip, always OP_CLASS */
        return emit(re, OP_CLASS, cid, 0);
    }

    if (ch == '.') {
        c->pos++;
        return emit(re, OP_ANY, 0, 0);
    }

    if (ch == '^') {
        c->pos++;
        return emit(re, OP_ANCHOR_BOL, 0, 0);
    }

    if (ch == '$') {
        c->pos++;
        return emit(re, OP_ANCHOR_EOL, 0, 0);
    }

    if (ch == '\\') {
        c->pos++;
        return parse_escape(c);
    }

    /* literal character */
    c->pos++;
    int code = (int)ch;
    return emit(re, OP_CHAR, code, 0);
}

/* ---- Piece = atom + optional quantifier ---- */

static int parse_piece(Compiler *c) {
    int start = c->re->prog_len;
    int r = parse_atom(c);
    if (r < 0 || c->had_error) return -1;
    if (c->re->prog_len == start) return start; /* zero-width atom (flags etc.) */

    if (c->pos >= c->pat_len) return c->re->prog_len;

    unsigned char qc = (unsigned char)c->pat[c->pos];
    int body_end = c->re->prog_len;

    if (qc == '*' || qc == '+' || qc == '?') {
        c->pos++;
        int lazy = 0;
        if (c->pos < c->pat_len && c->pat[c->pos] == '?') { c->pos++; lazy = 1; }
        return apply_quantifier(c, start, (char)qc, lazy);
    }

    if (qc == '{') {
        c->pos++;
        /* parse {n} or {n,} or {n,m} */
        int n = 0, m = 0, has_comma = 0;
        while (c->pos < c->pat_len && isdigit((unsigned char)c->pat[c->pos]))
            n = n * 10 + (c->pat[c->pos++] - '0');
        if (c->pos < c->pat_len && c->pat[c->pos] == ',') {
            has_comma = 1; c->pos++;
            if (c->pos < c->pat_len && c->pat[c->pos] != '}') {
                while (c->pos < c->pat_len && isdigit((unsigned char)c->pat[c->pos]))
                    m = m * 10 + (c->pat[c->pos++] - '0');
            } else { m = -1; } /* {n,} = unlimited */
        }
        if (c->pos >= c->pat_len || c->pat[c->pos] != '}') {
            comp_error(c, "invalid {n,m}"); return -1;
        }
        c->pos++;
        int lazy = 0;
        if (c->pos < c->pat_len && c->pat[c->pos] == '?') { c->pos++; lazy = 1; }
        if (!has_comma) m = n; /* {n} */
        if (m != -1 && m > 255) { comp_error(c, "{n,m}: m exceeds limit 255"); return -1; }
        if (m != -1 && m < n)   { comp_error(c, "{n,m}: m < n"); return -1; }
        if (n == 0 && m == 0) {
            /* {0} = erase the body we just emitted */
            c->re->prog_len = start;
            return start;
        }
        return apply_count(c, start, body_end, n, m, lazy);
    }

    return body_end;
}

/* ---- Concat = piece* ---- */

static int parse_concat(Compiler *c) {
    while (c->pos < c->pat_len) {
        unsigned char ch = (unsigned char)c->pat[c->pos];
        if (ch == ')' || ch == '|') break;
        int r = parse_piece(c);
        if (r < 0 || c->had_error) return -1;
    }
    return c->re->prog_len;
}

/* ---- Expr = concat (| concat)* ---- */

/* Depth-guarding wrapper: every recursion into a group goes through parse_expr,
   so bounding it here (inc on enter, dec on exit) caps nesting without crashing
   on adversarial input like "(((((...". */
static int parse_expr(Compiler *c) {
    if (c->depth >= RE_MAX_DEPTH) {
        comp_error(c, "regex nested too deeply");
        return -1;
    }
    c->depth++;
    int r = parse_expr_inner(c);
    c->depth--;
    return r;
}

static int parse_expr_inner(Compiler *c) {
    ReHandle *re = c->re;
    int alt_start = re->prog_len;
    int r = parse_concat(c);
    if (r < 0 || c->had_error) return -1;

    if (c->pos < c->pat_len && c->pat[c->pos] == '|') {
        /* Collect all branches, then wire up SPLITs and JUMPs */
        /* We use a simpler approach: for A|B emit:
             SPLIT(A_start, B_start)
             [A body]
             JUMP(end)
             [B body]  ← B_start
           For multiple alternatives chain them. */

        /* Move A body forward by 1 to make room for SPLIT */
        int a_len = re->prog_len - alt_start;
        if (re->prog_len + 2 >= MAX_INSTRS) { comp_error(c, "pattern too long"); return -1; }
        memmove(&re->prog[alt_start + 1], &re->prog[alt_start],
                (size_t)a_len * sizeof(ReInstr));
        re->prog_len++;
        /* placeholder SPLIT at alt_start */
        re->prog[alt_start].op = OP_SPLIT;

        int jump_patches[64];
        int n_jumps = 0;

        /* keep going while we see '|' */
        while (c->pos < c->pat_len && c->pat[c->pos] == '|') {
            c->pos++; /* consume '|' */

            /* emit JUMP from end of previous branch (before computing branch_start) */
            if (n_jumps >= 64) { comp_error(c, "too many alternatives"); return -1; }
            jump_patches[n_jumps++] = emit(re, OP_JUMP, 0, 0); /* patch later */

            /* B branch starts after the JUMP */
            int branch_start = re->prog_len;
            /* patch the last SPLIT to point A=body, B=this branch start */
            re->prog[alt_start].operand_a = alt_start + 1; /* A side */
            re->prog[alt_start].operand_b = branch_start;  /* B side */

            /* But we need to handle multi-way alt properly.
               For 3+ alternatives we chain SPLITs:
               SPLIT(A, next_split)
               [A]  JUMP(end)
               SPLIT(B, next_split2)
               [B]  JUMP(end)
               [C]

               Simpler: use a linked list of SPLIT->next_alt_split.
               We'll re-point alt_start each iteration. */
            alt_start = re->prog_len;
            if (re->prog_len + 1 < MAX_INSTRS) {
                /* Insert another SPLIT placeholder if another '|' might come */
                /* We'll determine after parsing the branch */
            }

            int r2 = parse_concat(c);
            if (r2 < 0 || c->had_error) return -1;

            if (c->pos < c->pat_len && c->pat[c->pos] == '|') {
                /* More branches — insert SPLIT before this branch */
                int blen = re->prog_len - alt_start;
                if (re->prog_len + 1 >= MAX_INSTRS) { comp_error(c, "pattern too long"); return -1; }
                memmove(&re->prog[alt_start+1], &re->prog[alt_start],
                        (size_t)blen * sizeof(ReInstr));
                re->prog_len++;
                re->prog[alt_start].op = OP_SPLIT;
                /* will be patched on next iteration */
                /* the jump before this branch needs to jump past the SPLIT we just inserted,
                   but it was already emitted... this is getting complex.

                   Actually let's just emit a final JUMP from end of branch,
                   and the SPLIT we inserted will be patched next iteration. */
            }
        }

        /* Patch all JUMPs to point to current end */
        int final_end = re->prog_len;
        for (int i = 0; i < n_jumps; i++) {
            re->prog[jump_patches[i]].operand_a = final_end;
        }
        /* Also fix the last SPLIT (no more '|' branches to fill in) */
        /* The last branch was parsed directly, so alt_start's SPLIT points to it already */
        /* But if we inserted a SPLIT for multi-way, its B needs to point to branch_start */
        /* At this point, alt_start is the last SPLIT we inserted.
           Its B should point to the branch after it (alt_start+1). */
        if (re->prog[alt_start].op == OP_SPLIT) {
            int bs = alt_start + 1;
            /* Find the last JUMP we emitted before alt_start to get branch start */
            /* Actually alt_start+1 is the start of the last branch */
            re->prog[alt_start].operand_a = bs;
            /* operand_b not needed for last SPLIT (no next branch) — point past end */
            re->prog[alt_start].operand_b = final_end;
        }
    }

    return c->re->prog_len;
}

/* ===== Compile API ===== */

int __ls_regex_compile(const char *pattern, int flags) {
    /* Find free slot */
    int h = -1;
    for (int i = 0; i < MAX_HANDLES; i++) {
        if (!g_pool[i].used) { h = i; break; }
    }
    if (h < 0) { snprintf(g_last_error, sizeof(g_last_error), "regex handle pool exhausted"); return -1; }

    ReHandle *re = &g_pool[h];
    memset(re, 0, sizeof(*re));
    re->used   = 1;
    re->flags  = flags;

    Compiler c;
    memset(&c, 0, sizeof(c));
    c.pat     = pattern;
    c.pat_len = (int)strlen(pattern);
    c.re      = re;

    /* Emit group 0 open SAVE */
    emit(re, OP_SAVE, 0, 0);

    int r = parse_expr(&c);

    if (c.had_error || r < 0) {
        snprintf(g_last_error, sizeof(g_last_error), "%s", c.error);
        re->used = 0;
        return -1;
    }
    if (c.pos < c.pat_len) {
        snprintf(g_last_error, sizeof(g_last_error), "unexpected ')' at pos %d", c.pos);
        re->used = 0;
        return -1;
    }

    /* Emit group 0 close SAVE + MATCH */
    emit(re, OP_SAVE, 1, 0);
    emit(re, OP_MATCH, 0, 0);

    re->n_groups = c.group_counter;
    return h;
}

void __ls_regex_free(int handle) {
    if (handle >= 0 && handle < MAX_HANDLES)
        g_pool[handle].used = 0;
}

const char *__ls_regex_last_error(void) { return g_last_error; }

/* Forward declaration — vm_exec_range is defined after add_thread */
static int vm_exec_range(const ReHandle *re, const char *text, int text_len,
                         int start, int pc_start, int pc_end, int *match_saved);

/* ===== Pike VM ===== */

/* visited[] tracks which pc values have been added to current list
   in this position step, to avoid duplicate threads. */

#define VM_VISITED_BYTES  ((MAX_INSTRS + 7) / 8)

typedef struct {
    unsigned char bits[VM_VISITED_BYTES];
} Visited;

static void visited_clear(Visited *v) { memset(v->bits, 0, sizeof(v->bits)); }
static int  visited_test(Visited *v, int pc) {
    return (v->bits[pc >> 3] >> (pc & 7)) & 1;
}
static void visited_set(Visited *v, int pc) {
    v->bits[pc >> 3] |= (unsigned char)(1u << (pc & 7));
}

/* Thread list */
typedef struct {
    ReThread threads[MAX_THREADS];
    int      count;
} ThreadList;

static void tl_init(ThreadList *tl) { tl->count = 0; }

/* Add thread following epsilon transitions (OP_SPLIT, OP_JUMP, OP_SAVE,
   anchors, lookahead).  Returns 1 if OP_MATCH was hit (fill match_saved). */
static int add_thread(const ReHandle *re, ThreadList *next, Visited *vis,
                      ReThread t,
                      const char *text, int text_len, int pos,
                      int *match_saved)
{
    /* Epsilon-closure loop.
     * found_any: 1 if any sub-branch hit OP_MATCH (propagated from SPLIT B).
     * All `return 0` become `return found_any` so callers see the match. */
    int found_any = 0;

    while (1) {
        if (t.pc < 0 || t.pc >= re->prog_len) return found_any;
        if (visited_test(vis, t.pc)) return found_any;
        visited_set(vis, t.pc);

        const ReInstr *in = &re->prog[t.pc];

        switch (in->op) {

        case OP_SAVE:
            t.saved[in->operand_a] = pos;
            t.pc++;
            continue;

        case OP_SPLIT: {
            /* Fork: process B branch recursively, continue with A.
             * Propagate B's match result into found_any so callers know. */
            ReThread t2 = t;
            t2.pc = in->operand_b;
            found_any |= add_thread(re, next, vis, t2, text, text_len, pos, match_saved);
            t.pc = in->operand_a;
            continue;
        }

        case OP_JUMP:
            t.pc = in->operand_a;
            continue;

        case OP_ANCHOR_BOL:
            if (re->flags & LS_RE_MULTILINE) {
                if (pos == 0 || (pos > 0 && text[pos-1] == '\n')) { t.pc++; continue; }
            } else {
                if (pos == 0) { t.pc++; continue; }
            }
            return found_any;

        case OP_ANCHOR_EOL:
            if (re->flags & LS_RE_MULTILINE) {
                if (pos == text_len || (pos < text_len && text[pos] == '\n')) { t.pc++; continue; }
            } else {
                if (pos == text_len) { t.pc++; continue; }
            }
            return found_any;

        case OP_ANCHOR_BOS:
            if (pos == 0) { t.pc++; continue; }
            return found_any;

        case OP_ANCHOR_EOS:
            if (pos == text_len) { t.pc++; continue; }
            return found_any;

        case OP_WORDBND: {
            int prev_w = (pos > 0) && is_word_char((unsigned char)text[pos-1]);
            int cur_w  = (pos < text_len) && is_word_char((unsigned char)text[pos]);
            if (prev_w != cur_w) { t.pc++; continue; }
            return found_any;
        }

        case OP_NWORDBND: {
            int prev_w = (pos > 0) && is_word_char((unsigned char)text[pos-1]);
            int cur_w  = (pos < text_len) && is_word_char((unsigned char)text[pos]);
            if (prev_w == cur_w) { t.pc++; continue; }
            return found_any;
        }

        case OP_LOOKAHEAD: {
            int sub_end_pc = in->operand_a;
            int is_neg     = in->operand_b;
            int tmp_saved[MAX_GROUPS * 2];
            for (int k = 0; k < MAX_GROUPS * 2; k++) tmp_saved[k] = -1;
            int ok = vm_exec_range(re, text, text_len, pos, t.pc + 1, sub_end_pc - 1, tmp_saved);
            if ((ok != 0) != (is_neg != 0)) {
                t.pc = sub_end_pc;
                continue;
            }
            return found_any;
        }

        case OP_MATCH:
            memcpy(match_saved, t.saved, MAX_GROUPS * 2 * sizeof(int));
            return 1;

        default:
            /* Consuming instruction: enqueue for next position */
            if (next->count < MAX_THREADS) {
                next->threads[next->count++] = t;
            }
            return found_any;
        }
    }
}

/* Run VM on text[start..text_len), using program[pc_start..pc_end] (pc_end unused — OP_MATCH terminates).
   Fills match_saved[] (group-open/close byte offsets).
   Returns number of groups+1 (=n_groups+1) on match, 0 on no match.
   pc_end=-1 means run to end of program. */
static int vm_exec_range(const ReHandle *re, const char *text, int text_len,
                         int start, int pc_start, int pc_end,
                         int *match_saved)
{
    (void)pc_end; /* we rely on OP_MATCH to terminate */

    ThreadList cur, nxt;
    Visited vis;
    int found_saved[MAX_GROUPS * 2];
    for (int k = 0; k < MAX_GROUPS * 2; k++) found_saved[k] = -1;
    int found = 0;

    tl_init(&cur);
    tl_init(&nxt);

    /* Seed initial thread */
    {
        ReThread t0;
        t0.pc = pc_start;
        for (int k = 0; k < MAX_GROUPS * 2; k++) t0.saved[k] = -1;
        visited_clear(&vis);
        add_thread(re, &cur, &vis, t0, text, text_len, start, found_saved);
        if (found_saved[0] >= 0 && found_saved[1] >= 0) {
            found = 1;
        }
    }

    for (int pos = start; pos <= text_len; pos++) {
        if (cur.count == 0) break;

        tl_init(&nxt);
        visited_clear(&vis);

        for (int ti = 0; ti < cur.count; ti++) {
            ReThread t = cur.threads[ti];
            const ReInstr *in = &re->prog[t.pc];

            if (pos == text_len) {
                /* Only zero-width ops can match at end of string */
                /* They are handled in add_thread epsilon closure above */
                /* Consuming ops cannot match — skip */
                continue;
            }

            unsigned char ch = (unsigned char)text[pos];
            int match_char = 0;

            switch (in->op) {
            case OP_CHAR:
                if (re->flags & LS_RE_IGNORECASE)
                    match_char = (tolower(ch) == tolower((unsigned char)in->operand_a));
                else
                    match_char = (ch == (unsigned char)in->operand_a);
                break;
            case OP_ANY:
                match_char = (ch != '\n') || (re->flags & LS_RE_DOTALL);
                break;
            case OP_CLASS: {
                const ReCharClass *cls = &re->classes[in->operand_a];
                unsigned char lc = (re->flags & LS_RE_IGNORECASE) ? (unsigned char)tolower(ch) : ch;
                match_char = class_test(cls, lc);
                break;
            }
            case OP_NCLASS: {
                const ReCharClass *cls = &re->classes[in->operand_a];
                unsigned char lc = (re->flags & LS_RE_IGNORECASE) ? (unsigned char)tolower(ch) : ch;
                match_char = !class_test(cls, lc);
                break;
            }
            case OP_DIGIT:  match_char = isdigit(ch) != 0; break;
            case OP_NDIGIT: match_char = !isdigit(ch); break;
            case OP_WORD:   match_char = is_word_char(ch) != 0; break;
            case OP_NWORD:  match_char = !is_word_char(ch); break;
            case OP_SPACE:  match_char = is_space_char(ch) != 0; break;
            case OP_NSPACE: match_char = !is_space_char(ch); break;
            default: break;
            }

            if (match_char) {
                ReThread nt = t;
                nt.pc = t.pc + 1;
                int ms[MAX_GROUPS * 2];
                memcpy(ms, found_saved, MAX_GROUPS * 2 * sizeof(int));
                int got = add_thread(re, &nxt, &vis, nt, text, text_len, pos + 1, ms);
                if (got) {
                    memcpy(found_saved, ms, MAX_GROUPS * 2 * sizeof(int));
                    found = 1;
                }
            }
        }

        /* Swap cur/nxt */
        cur = nxt;
        /* Re-run epsilon transitions for the freshly advanced threads */
        /* (already handled inside add_thread — threads in nxt start at consuming op pc+1) */
    }

    if (found) {
        memcpy(match_saved, found_saved, MAX_GROUPS * 2 * sizeof(int));
        return 1;
    }
    return 0;
}

/* ===== Public exec API ===== */

int __ls_regex_exec(int handle, const char *text, int text_len, int start) {
    if (handle < 0 || handle >= MAX_HANDLES || !g_pool[handle].used) return 0;
    ReHandle *re = &g_pool[handle];

    /* Try matching starting at each position */
    for (int s = start; s <= text_len; s++) {
        int ms[MAX_GROUPS * 2];
        for (int k = 0; k < MAX_GROUPS * 2; k++) ms[k] = -1;
        int ok = vm_exec_range(re, text, text_len, s, 0, re->prog_len - 1, ms);
        if (ok) {
            memcpy(g_last_saved, ms, MAX_GROUPS * 2 * sizeof(int));
            return re->n_groups + 1; /* groups + group-0 */
        }
    }
    return 0;
}

int __ls_regex_cap_start(int group) {
    if (group < 0 || group >= MAX_GROUPS) return -1;
    return g_last_saved[group * 2];
}

int __ls_regex_cap_len(int group) {
    if (group < 0 || group >= MAX_GROUPS) return -1;
    int s = g_last_saved[group * 2];
    int e = g_last_saved[group * 2 + 1];
    if (s < 0 || e < 0) return -1;
    return e - s;
}

int __ls_regex_group_count(int handle) {
    if (handle < 0 || handle >= MAX_HANDLES || !g_pool[handle].used) return 0;
    return g_pool[handle].n_groups;
}

int __ls_regex_named_count(int handle) {
    if (handle < 0 || handle >= MAX_HANDLES || !g_pool[handle].used) return 0;
    return g_pool[handle].n_named;
}

const char *__ls_regex_named_name(int handle, int i) {
    if (handle < 0 || handle >= MAX_HANDLES || !g_pool[handle].used) return "";
    if (i < 0 || i >= g_pool[handle].n_named) return "";
    return g_pool[handle].named[i].name;
}

int __ls_regex_named_index(int handle, int i) {
    if (handle < 0 || handle >= MAX_HANDLES || !g_pool[handle].used) return -1;
    if (i < 0 || i >= g_pool[handle].n_named) return -1;
    return g_pool[handle].named[i].group_id;
}
