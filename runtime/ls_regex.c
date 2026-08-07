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

/* Boyer-Moore-Horspool / Sunday substring search, shared with std.core.str.
   Defined in runtime/builtins.c; both files link into ls and ls_os_backend. */
extern int __ls_str_find(const char *hay, int hlen,
                         const char *needle, int nlen, int start);

/* ===== Constants ===== */

#define MAX_GROUPS    17    /* group 0 = full match, 1..16 = captures */
#define MAX_NAMED     16
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
    int          flags;         /* LS_RE_* bits */
    int          n_groups;      /* number of capture groups (excl. group 0) */
    int          prog_len;
    int          prog_cap;      /* allocated length of prog[] */
    ReInstr     *prog;          /* heap, prog_cap entries */
    ReCharClass  classes[MAX_CLASSES];
    int          n_classes;
    NamedGroup   named[MAX_NAMED];
    int          n_named;

    /* Results of the last exec on THIS handle. Used to be a single
       file-level global (g_last_saved), which raced between threads and
       forced every caller to read the captures before running any other
       pattern. */
    int          saved[MAX_GROUPS * 2];

    /* ---- Prefilter (built once at compile time, read by __ls_regex_exec) ----
       lit/lit_len: a literal string every match must start with. If
       lit_is_whole is set the pattern is nothing BUT this literal, so a match
       can be answered by __ls_str_find alone without entering the VM.
       first_bytes: 128-bit set of bytes a match may start with; only valid
       when has_first_bytes is set. Both are disabled under LS_RE_IGNORECASE,
       where a byte-exact search would be wrong. */
    char          lit[64];
    int           lit_len;
    int           lit_is_whole;
    unsigned char first_bytes[CLASS_BITMAP_BYTES];
    int           has_first_bytes;

    /* ---- Anchor detection (built once at compile time, read by
       __ls_regex_exec) ----
       0 = unanchored: every start position in [start, text_len] must be
           tried, same as before this field existed.
       1 = absolute: every match must begin at text position 0 (\A, or ^
           without LS_RE_MULTILINE). __ls_regex_exec then tries at most
           ONE candidate position instead of walking the whole string.
       2 = line-start: every match must begin at position 0 or immediately
           after a '\n' (^ WITH LS_RE_MULTILINE). __ls_regex_exec then
           only tries line-start positions.
       See re_detect_anchor for the conservative rule that sets this. */
    int           anchor_mode;
} ReHandle;

/* Compile failures have no handle to hang an error message on, so this one
   stays process-global; it is advisory only (see ls_regex.h). */
static char     g_last_error[256];

/* ===== Thread ===== */

typedef struct {
    int pc;
    int saved[MAX_GROUPS * 2];
} ReThread;

/* Number of saved[] slots this pattern actually uses: two per capture group
   plus two for group 0.  Copying only these instead of the fixed MAX_GROUPS*2
   is what keeps thread propagation cheap for the common 0-5 group case. */
static int re_slot_count(const ReHandle *re) {
    int n = (re->n_groups + 1) * 2;
    if (n > MAX_GROUPS * 2) n = MAX_GROUPS * 2;
    return n;
}

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
    if (re->prog_len >= re->prog_cap) {
        int ncap = re->prog_cap ? re->prog_cap * 2 : 16;
        if (ncap > MAX_INSTRS) ncap = MAX_INSTRS;
        ReInstr *np = (ReInstr *)realloc(re->prog, (size_t)ncap * sizeof(ReInstr));
        if (!np) return -1;
        re->prog     = np;
        re->prog_cap = ncap;
    }
    re->prog[re->prog_len].op         = op;
    re->prog[re->prog_len].operand_a  = a;
    re->prog[re->prog_len].operand_b  = b;
    return re->prog_len++;
}

/* Ensure prog[] has room for `extra` more instructions beyond prog_len.
   emit() grows the buffer itself, but several places write instructions
   directly -- apply_quantifier's memmove, the alternation rewrites, and
   apply_count's body copies -- and those bypass emit entirely. That was
   harmless while prog was a fixed MAX_INSTRS array, but prog is heap-grown
   now, so prog_len can sit exactly at prog_cap and a raw write runs off the
   end of the allocation. Returns 0 on success, -1 if it cannot grow. */
static int re_prog_reserve(ReHandle *re, int extra) {
    int need = re->prog_len + extra;
    if (need > MAX_INSTRS) return -1;
    if (need <= re->prog_cap) return 0;
    int ncap = re->prog_cap ? re->prog_cap : 16;
    while (ncap < need) ncap *= 2;
    if (ncap > MAX_INSTRS) ncap = MAX_INSTRS;
    ReInstr *np = (ReInstr *)realloc(re->prog, (size_t)ncap * sizeof(ReInstr));
    if (!np) return -1;
    re->prog     = np;
    re->prog_cap = ncap;
    return 0;
}

/* apply_quantifier and the two alternation rewrites in parse_expr_inner all
   make room for a new SPLIT ahead of an already-emitted body by memmove'ing
   that body forward `shift` slots (shift is always 1 at every current call
   site, but the helper is written generally). Several opcodes carry
   ABSOLUTE program counters in their operands -- OP_JUMP.operand_a,
   OP_SPLIT.operand_a/b (the enum comment calls them "offsets" but every
   writer in this file stores absolute pcs like start+1 and end), and
   OP_LOOKAHEAD.operand_a -- and a plain memmove does not adjust any of
   them.

   The naive fix ("bump every operand >= old_pos by `shift`, uniformly")
   is WRONG. An operand whose value is exactly old_pos is genuinely
   ambiguous, and the two readings need opposite treatment:

     - An instruction that sits BEFORE old_pos (i.e. was already emitted
       when this shift happens, such as an enclosing alternation's SPLIT
       whose branch-B target is exactly old_pos) means "the entry point of
       the construct that starts here". After the insertion, that entry
       point is still old_pos -- the newly inserted instruction (the new
       SPLIT this shift is making room for) IS the construct's new front
       door. This operand must NOT be bumped when it equals old_pos
       exactly (only bumped if it is strictly greater, i.e. it names
       something deeper inside the region that physically moved).
       Concretely: "x|(a*)*" -- the outer alternation's branch-B target
       (old_pos) must keep pointing at old_pos, which after the shift
       holds the new outer '*' SPLIT, the correct entry to "(a*)*".
       Bumping it would skip that SPLIT and jump straight past it.

     - An instruction that is ITSELF part of the region that just
       physically moved (now living at index >= old_pos+shift) was
       created while compiling the body, so any of its absolute operands
       were computed relative to the OLD, pre-shift layout and range over
       [old_pos, end) -- including possibly old_pos itself. E.g. a
       non-capturing "(?:a*)*": the inner '*'s own back-edge JUMP points
       to the inner SPLIT, which (with no SAVE to offset it) sits at
       exactly old_pos before the outer shift. That reference MUST bump
       when it equals old_pos, or the loop-back ends up targeting the
       newly-inserted OUTER split instead of the inner one it meant.

   So the rule keys off WHERE THE OPERAND LIVES, not just its value:
   strict '>' for instructions before old_pos, non-strict '>=' for
   instructions in the moved region. The vacated slot(s)
   [old_pos, old_pos+shift) hold a stale leftover duplicate of the body's
   old first instruction(s) at fixup time and are skipped outright -- the
   caller overwrites them immediately after with freshly computed values.

   Walk the WHOLE program (0..limit), not just the moved region: an
   earlier, unmoved instruction can legitimately hold a forward reference
   into the region that just moved, and that reference needs the same
   correction (see the "x|(a*)*" example above).
   Callers must invoke this AFTER the memmove (and after bumping prog_len)
   but BEFORE writing the new instruction's own operands into the vacated
   slot -- those operands are computed fresh for the post-shift layout and
   must not be adjusted again. */
static void re_fixup_pcs_after_shift(ReHandle *re, int old_pos, int shift, int limit) {
    int moved_from = old_pos + shift; /* first index holding relocated body content */
    for (int i = 0; i < limit; i++) {
        if (i >= old_pos && i < moved_from) continue; /* vacated slot(s): ignore */
        int in_moved_region = (i >= moved_from);
        ReInstr *ins = &re->prog[i];
        switch (ins->op) {
            case OP_JUMP:
            case OP_LOOKAHEAD: {
                int hit = in_moved_region ? (ins->operand_a >= old_pos)
                                           : (ins->operand_a >  old_pos);
                if (hit) ins->operand_a += shift;
                break;
            }
            case OP_SPLIT: {
                int hit_a = in_moved_region ? (ins->operand_a >= old_pos)
                                             : (ins->operand_a >  old_pos);
                int hit_b = in_moved_region ? (ins->operand_b >= old_pos)
                                             : (ins->operand_b >  old_pos);
                if (hit_a) ins->operand_a += shift;
                if (hit_b) ins->operand_b += shift;
                break;
            }
            default:
                break;
        }
    }
}

/* apply_count's {n,m} expansion (below) duplicates an already-emitted body
   to new locations via memcpy, not memmove -- unlike apply_quantifier and
   the alternation rewrites above, which shift a body IN PLACE inside the
   same array (handled by re_fixup_pcs_after_shift). A copy leaves a second,
   independent instance of the body's instructions coexisting with the
   original, and every ABSOLUTE pc operand in the copy (OP_JUMP,
   OP_SPLIT.operand_a/b, OP_LOOKAHEAD.operand_a) still points into the
   ORIGINAL body's address range -- so, uncorrected, a copy with any
   internal control flow (a nested quantifier or alternation) branches back
   into the first copy instead of staying inside itself.

   A copied body is always a self-contained compiled sub-program (one
   piece's whole emitted output, or that same output after being wrapped by
   apply_quantifier and re-fixed-up in place): every internal
   SPLIT/JUMP/LOOKAHEAD operand it carries was computed relative to itself
   and lies in the CLOSED interval [src_start, src_end]. The interval must
   be closed (inclusive of src_end, i.e. "one past my own last
   instruction") because a piece with nothing emitted after it -- e.g. a
   non-capturing group whose content is itself an alternation -- ends with
   a JUMP operand equal to exactly that: "fall through to whatever follows
   me" (see parse_expr_inner's `final_end`, which is the alternation's own
   prog_len at the moment nothing else has been emitted yet). There is no
   backreference-into-an-enclosing-construct in this engine, so no operand
   inside a body should ever point outside that interval; it is therefore
   both safe and correct to translate every in-range operand by the same
   delta and leave everything else alone. Call this AFTER the memcpy (and
   after bumping prog_len), on the instructions now living at dst_start. */
static void re_fixup_pcs_after_copy(ReHandle *re, int src_start, int src_end, int dst_start) {
    int delta = dst_start - src_start;
    int len   = src_end - src_start;
    for (int i = 0; i < len; i++) {
        ReInstr *ins = &re->prog[dst_start + i];
        switch (ins->op) {
            case OP_JUMP:
            case OP_LOOKAHEAD:
                if (ins->operand_a >= src_start && ins->operand_a <= src_end)
                    ins->operand_a += delta;
                break;
            case OP_SPLIT:
                if (ins->operand_a >= src_start && ins->operand_a <= src_end)
                    ins->operand_a += delta;
                if (ins->operand_b >= src_start && ins->operand_b <= src_end)
                    ins->operand_b += delta;
                break;
            default:
                break;
        }
    }
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
        if (re_prog_reserve(re, 1) < 0) return -1;
        /* Move body forward by 1 slot */
        memmove(&re->prog[start+1], &re->prog[start], (size_t)(end - start) * sizeof(ReInstr));
        re->prog_len++;
        end++;
        re_fixup_pcs_after_shift(re, start, 1, re->prog_len);
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
        if (re_prog_reserve(re, 1) < 0) return -1;
        memmove(&re->prog[start+1], &re->prog[start], (size_t)(end - start) * sizeof(ReInstr));
        re->prog_len++;
        end++;
        re_fixup_pcs_after_shift(re, start, 1, re->prog_len);
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

    /* A zero lower bound needs its own path. The caller has already emitted one
       copy of the body and that copy is mandatory as written; the loop below
       only ever appends n_min-1 MORE copies, so with n_min == 0 nothing ever
       makes that first copy optional and {0,m} silently behaves as {1,m+1}.
       Make it optional up front, then append m-1 further optional copies --
       the wrapped copy accounts for one of the m allowed repetitions.
       Note apply_quantifier inserts its SPLIT ahead of the body and shifts the
       body forward one slot, so later copies must be taken from body_start+1. */
    if (n_min == 0) {
        if (n_max == 0) {           /* {0,0}: the body must not match at all */
            re->prog_len = body_start;
            return re->prog_len;
        }
        if (n_max == -1)            /* {0,} is exactly * */
            return apply_quantifier(c, body_start, '*', lazy);
        if (apply_quantifier(c, body_start, '?', lazy) < 0) return -1;
        body_start += 1;
        for (int i = 1; i < n_max; i++) {
            if (re->prog_len + body_len + 2 >= MAX_INSTRS - 4) {
                comp_error(c, "{n,m}: pattern too large"); return -1;
            }
            if (re_prog_reserve(re, body_len + 2) < 0) {
                comp_error(c, "{n,m}: pattern too large"); return -1;
            }
            int opt_start = re->prog_len;
            memcpy(&re->prog[opt_start], &re->prog[body_start],
                   (size_t)body_len * sizeof(ReInstr));
            re->prog_len += body_len;
            re_fixup_pcs_after_copy(re, body_start, body_start + body_len, opt_start);
            if (apply_quantifier(c, opt_start, '?', lazy) < 0) return -1;
        }
        return re->prog_len;
    }

    /* Emit n_min-1 additional mandatory copies */
    for (int i = 1; i < n_min; i++) {
        if (re->prog_len + body_len >= MAX_INSTRS - 4) {
            comp_error(c, "{n,m}: pattern too large"); return -1;
        }
        if (re_prog_reserve(re, body_len) < 0) {
            comp_error(c, "{n,m}: pattern too large"); return -1;
        }
        int dst_start = re->prog_len;
        memcpy(&re->prog[dst_start], &re->prog[body_start],
               (size_t)body_len * sizeof(ReInstr));
        re->prog_len += body_len;
        re_fixup_pcs_after_copy(re, body_start, body_start + body_len, dst_start);
    }

    if (n_max == -1) {
        /* {n,} → emit one more copy wrapped with * */
        if (re_prog_reserve(re, body_len + 2) < 0) {
            comp_error(c, "{n,m}: pattern too large"); return -1;
        }
        int opt_start = re->prog_len;
        memcpy(&re->prog[opt_start], &re->prog[body_start],
               (size_t)body_len * sizeof(ReInstr));
        re->prog_len += body_len;
        re_fixup_pcs_after_copy(re, body_start, body_start + body_len, opt_start);
        return apply_quantifier(c, opt_start, '*', lazy);
    }

    /* {n,m}: emit (n_max - n_min) optional copies each wrapped with ? */
    for (int i = n_min; i < n_max; i++) {
        if (re->prog_len + body_len + 2 >= MAX_INSTRS - 4) {
            comp_error(c, "{n,m}: pattern too large"); return -1;
        }
        if (re_prog_reserve(re, body_len + 2) < 0) {
            comp_error(c, "{n,m}: pattern too large"); return -1;
        }
        int opt_start = re->prog_len;
        memcpy(&re->prog[opt_start], &re->prog[body_start],
               (size_t)body_len * sizeof(ReInstr));
        re->prog_len += body_len;
        re_fixup_pcs_after_copy(re, body_start, body_start + body_len, opt_start);
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
        if (re_prog_reserve(re, 1) < 0) { comp_error(c, "pattern too long"); return -1; }
        memmove(&re->prog[alt_start + 1], &re->prog[alt_start],
                (size_t)a_len * sizeof(ReInstr));
        re->prog_len++;
        re_fixup_pcs_after_shift(re, alt_start, 1, re->prog_len);
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
                if (re_prog_reserve(re, 1) < 0) { comp_error(c, "pattern too long"); return -1; }
                memmove(&re->prog[alt_start+1], &re->prog[alt_start],
                        (size_t)blen * sizeof(ReInstr));
                re->prog_len++;
                re_fixup_pcs_after_shift(re, alt_start, 1, re->prog_len);
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
        /* No further fix-up is needed here. Every placeholder SPLIT this
           function inserts (the one before the loop, and any inserted at
           the bottom of a loop iteration when another '|' follows) is
           patched at the top of the NEXT loop iteration, which is
           guaranteed to run because the insertion only happens when the
           lookahead already saw a pending '|'. So by the time the loop
           exits, `alt_start` merely names the start of the LAST branch --
           there is no dangling SPLIT left to patch there.
           A previous version of this code assumed otherwise and blindly
           patched `re->prog[alt_start]` whenever it happened to be an
           OP_SPLIT. That fired whenever the last branch's own first
           instruction was itself a SPLIT belonging to an unrelated inner
           construct (e.g. "a|(?:b|c)d", where B's first instruction is the
           inner b|c alternation's SPLIT, or "a|b*", where B's first
           instruction is the '*' quantifier's SPLIT) and silently
           corrupted that inner SPLIT's targets. See bug (d) in the regex
           differential-oracle writeup. */
    }

    return c->re->prog_len;
}

/* Walk the program from pc 0 and derive what every match must start with.
   Only the leading run of OP_CHAR instructions is used: the moment anything
   else appears (a split, a class, an anchor, a save that is not group 0's
   opening one) the literal ends. Conservative by construction -- when in
   doubt we record nothing and __ls_regex_exec falls back to a full scan. */
static void re_build_prefilter(ReHandle *re) {
    re->lit_len         = 0;
    re->lit_is_whole    = 0;
    re->has_first_bytes = 0;

    /* A byte-exact prefilter is wrong when folding case. */
    if (re->flags & LS_RE_IGNORECASE) return;

    int pc = 0;
    /* Skip the group-0 opening SAVE emitted by __ls_regex_compile. */
    if (pc < re->prog_len && re->prog[pc].op == OP_SAVE && re->prog[pc].operand_a == 0)
        pc++;

    while (pc < re->prog_len &&
           re->prog[pc].op == OP_CHAR &&
           re->lit_len < (int)sizeof(re->lit)) {
        re->lit[re->lit_len++] = (char)re->prog[pc].operand_a;
        pc++;
    }

    if (re->lit_len > 0) {
        /* Nothing but the literal, then group-0 close + MATCH? Then the whole
           pattern is that literal and the VM is not needed at all. */
        if (pc + 1 < re->prog_len &&
            re->prog[pc].op == OP_SAVE && re->prog[pc].operand_a == 1 &&
            re->prog[pc + 1].op == OP_MATCH) {
            re->lit_is_whole = 1;
        }
        return;
    }

    /* No literal prefix. Fall back to a first-byte set if the first consuming
       instruction is a class we can copy verbatim. */
    if (pc < re->prog_len) {
        const ReInstr *in = &re->prog[pc];
        if (in->op == OP_CLASS && in->operand_a >= 0 && in->operand_a < re->n_classes) {
            memcpy(re->first_bytes, re->classes[in->operand_a].bits, CLASS_BITMAP_BYTES);
            re->has_first_bytes = 1;
        }
    }
}

/* Detect whether every match this pattern can ever produce must begin at
   text position 0 (anchor_mode=1) or at a line start under LS_RE_MULTILINE
   (anchor_mode=2). __ls_regex_exec uses this to skip candidate start
   positions that are provably doomed instead of entering the VM at each one
   just to have it fail the anchor check on its very first instruction.

   Soundness: __ls_regex_compile always emits the group-0 opening OP_SAVE as
   the unconditional first instruction (prog[0]), and the VM's single seed
   thread walks pc 0, 1, 2, ... in a straight line -- there is no branch
   before prog[1] can execute. So prog[1] is, unconditionally, the first
   real instruction of every possible execution path. If prog[1] is itself
   an anchor, EVERY thread that ever exists must have passed it, because
   there is no alternate route into the program that skips pc 1.

   This is why the check is exactly "is prog[1] an anchor", not "does an
   anchor appear anywhere reachable from pc 0". An anchor behind a SPLIT --
   e.g. "(^a|b)" (group open at prog[1], SPLIT at prog[2]) or "^a|b" (the
   top-level alternation's SPLIT sits at prog[1], with ANCHOR_BOL only on
   the A branch at prog[2]) -- does NOT dominate every path: "b" alone can
   still match without ever touching the anchor. In both cases prog[1] is a
   SAVE or a SPLIT, not the anchor opcode, so the detector correctly stays
   at anchor_mode=0 and __ls_regex_exec falls back to trying every
   position. A detector that walked past SPLITs looking for an anchor on
   "some" branch would be unsound (a silent wrong answer on exactly the
   inputs the caller warned about), which is why this one does not. */
static void re_detect_anchor(ReHandle *re) {
    re->anchor_mode = 0;
    if (re->prog_len < 2) return;
    if (re->prog[0].op != OP_SAVE || re->prog[0].operand_a != 0) return; /* defensive */

    ReOpCode op = re->prog[1].op;
    if (op == OP_ANCHOR_BOS) {
        re->anchor_mode = 1; /* \A: absolute, independent of MULTILINE */
    } else if (op == OP_ANCHOR_BOL) {
        /* Non-multiline ^ behaves exactly like \A (pos==0 only). Multiline
           ^ additionally allows any position right after a '\n'. */
        re->anchor_mode = (re->flags & LS_RE_MULTILINE) ? 2 : 1;
    }
}

/* ===== Compile API ===== */

void *__ls_regex_compile(const char *pattern, int flags) {
    ReHandle *re = (ReHandle *)calloc(1, sizeof(ReHandle));
    if (!re) {
        snprintf(g_last_error, sizeof(g_last_error), "out of memory");
        return NULL;
    }
    re->flags = flags;

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
        free(re->prog);
        free(re);
        return NULL;
    }
    if (c.pos < c.pat_len) {
        snprintf(g_last_error, sizeof(g_last_error), "unexpected ')' at pos %d", c.pos);
        free(re->prog);
        free(re);
        return NULL;
    }

    /* Emit group 0 close SAVE + MATCH */
    emit(re, OP_SAVE, 1, 0);
    emit(re, OP_MATCH, 0, 0);

    re->n_groups = c.group_counter;
    re_build_prefilter(re);
    re_detect_anchor(re);
    return re;
}

void __ls_regex_free(void *h) {
    ReHandle *re = (ReHandle *)h;
    if (!re) return;
    free(re->prog);
    free(re);
}

/* ===== Thread-local pattern cache =====
   The free functions in std.text.regex take the pattern as a string on every
   call. Compiling a handle also means it cannot outlive the call, which is
   what forces the two-lifetime split (owned vs. cached) this cache removes;
   with the engine now ~165x faster on the literal shape (see git history),
   compilation is the dominant remaining cost for short patterns, so this is
   also a real per-call win, not just a Task 7 prerequisite.

   Thread-local rather than shared+locked: a shared cache would put a lock on
   the hot path of every regex call, and with heap-allocated handles the cost
   of one cache per thread is a few hundred bytes per entry. */

#define RE_CACHE_SLOTS 32
#define RE_CACHE_PATLEN 128

typedef struct {
    char      pat[RE_CACHE_PATLEN];
    int       flags;
    ReHandle *re;
    unsigned  stamp;      /* LRU: higher is more recently used */
} ReCacheSlot;

#ifdef _MSC_VER
#  define RE_THREAD_LOCAL __declspec(thread)
#else
#  define RE_THREAD_LOCAL _Thread_local
#endif

static RE_THREAD_LOCAL ReCacheSlot tls_cache[RE_CACHE_SLOTS];
static RE_THREAD_LOCAL unsigned    tls_clock;

void *__ls_regex_cached(const char *pattern, int flags) {
    size_t plen = strlen(pattern);
    /* Patterns longer than the inline key are not cached -- correctness first;
       they just compile every time, exactly like before this cache existed. */
    if (plen >= RE_CACHE_PATLEN) return NULL;

    int lru = 0;
    for (int i = 0; i < RE_CACHE_SLOTS; i++) {
        ReCacheSlot *s = &tls_cache[i];
        if (s->re && s->flags == flags && strcmp(s->pat, pattern) == 0) {
            s->stamp = ++tls_clock;
            return s->re;
        }
        if (s->stamp < tls_cache[lru].stamp) lru = i;
    }

    ReHandle *re = (ReHandle *)__ls_regex_compile(pattern, flags);
    if (!re) return NULL;

    ReCacheSlot *victim = &tls_cache[lru];
    if (victim->re) __ls_regex_free(victim->re);
    memcpy(victim->pat, pattern, plen + 1);
    victim->flags = flags;
    victim->re    = re;
    victim->stamp = ++tls_clock;
    return re;
}

const char *__ls_regex_last_error(void) { return g_last_error; }

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

/* Forward declarations — both defined after add_thread.
 *
 * vm_exec_range now takes its two ThreadList working buffers as parameters
 * instead of declaring them as locals (see the comment on its definition for
 * why). add_thread only ever calls vm_exec_lookahead, never vm_exec_range
 * directly: vm_exec_lookahead is a thin wrapper that owns a fresh buffer
 * pair in its *own* stack frame. That keeps the buffers out of add_thread's
 * frame entirely — add_thread is the hottest function in the engine (called
 * once per thread per position, recursively for every OP_SPLIT branch), and
 * OP_LOOKAHEAD is a rare opcode, so the ~143 KB pair must not become part of
 * every add_thread call's frame just because one switch case might need it. */
static int vm_exec_range(const ReHandle *re, const char *text, int text_len,
                         int start, int pc_start, int pc_end, int *match_saved,
                         ThreadList *buf_a, ThreadList *buf_b);
static int vm_exec_lookahead(const ReHandle *re, const char *text, int text_len,
                             int start, int pc_start, int pc_end, int *match_saved);

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
            int ok = vm_exec_lookahead(re, text, text_len, pos, t.pc + 1, sub_end_pc - 1, tmp_saved);
            if ((ok != 0) != (is_neg != 0)) {
                t.pc = sub_end_pc;
                continue;
            }
            return found_any;
        }

        case OP_MATCH:
            memcpy(match_saved, t.saved, (size_t)re_slot_count(re) * sizeof(int));
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
   pc_end=-1 means run to end of program.

   buf_a/buf_b: caller-owned ThreadList working buffers (~71.7 KB each). They
   used to be locals here, but this function is called once per candidate
   start position by __ls_regex_exec's outer loop (~50 times for a 49-byte
   text against an anchored pattern that never advances past position 0) --
   declaring ~143 KB of locals on every one of those calls means MSVC emits a
   __chkstk stack-probe loop touching ~36 pages on every single call, not
   just the ones that do real matching work. Taking the buffers as parameters
   lets the caller hoist them out of the per-start-position loop and pay that
   probe cost once per __ls_regex_exec call instead of once per start
   position. Callers must each own a buffer pair that is not shared with any
   other *concurrently in-progress* vm_exec_range call -- see
   vm_exec_lookahead below for why the recursive lookahead path needs its
   own pair rather than reusing the outer match's. */
/* Reverse ReThread entries in the inclusive range [lo, hi] in place. Used by
   vm_exec_range to restore priority order in a thread list -- see the
   comment above its per-position loop. No-op if lo >= hi. */
static void reverse_thread_range(ReThread *threads, int lo, int hi) {
    while (lo < hi) {
        ReThread tmp = threads[lo];
        threads[lo] = threads[hi];
        threads[hi] = tmp;
        lo++;
        hi--;
    }
}

static int vm_exec_range(const ReHandle *re, const char *text, int text_len,
                         int start, int pc_start, int pc_end,
                         int *match_saved,
                         ThreadList *buf_a, ThreadList *buf_b)
{
    (void)pc_end; /* we rely on OP_MATCH to terminate */

    /* Two buffers plus a pointer swap.  The previous code did `cur = nxt`
       once per character position, which copies the whole ThreadList struct
       (MAX_THREADS * sizeof(ReThread) ~= 71.7 KB) regardless of how many
       threads are actually live -- measured at 15-38x of total exec time. */
    ThreadList *cur = buf_a, *nxt = buf_b;
    Visited vis;
    const int nslot = re_slot_count(re);
    int found_saved[MAX_GROUPS * 2];
    for (int k = 0; k < nslot; k++) found_saved[k] = -1;
    int found = 0;

    tl_init(cur);
    tl_init(nxt);

    /* Seed initial thread */
    {
        ReThread t0;
        t0.pc = pc_start;
        for (int k = 0; k < nslot; k++) t0.saved[k] = -1;
        visited_clear(&vis);
        add_thread(re, cur, &vis, t0, text, text_len, start, found_saved);
        if (found_saved[0] >= 0 && found_saved[1] >= 0) {
            found = 1;
        }
    }

    for (int pos = start; pos <= text_len; pos++) {
        if (cur->count == 0) break;

        tl_init(nxt);
        visited_clear(&vis);

        /* Threads in cur are in priority order, but *ascending*: add_thread's
           OP_SPLIT handling recurses into the lower-priority operand_b branch
           first and only appends the higher-priority operand_a branch's
           consuming instructions afterwards (see the OP_MATCH case -- it
           relies on "last write wins" to let a later, higher-priority branch
           overwrite an earlier, lower-priority one within a single closure).
           So cur->threads[count-1] is the highest-priority thread and
           cur->threads[0] is the lowest.

           We must walk this list highest-priority first, for two reasons:
             1. `vis` (the per-position visited set, shared by every ti in
                this loop) deduplicates threads that converge on the same
                pc: whichever thread gets there first "wins" that pc for
                this position, so the winner must be the highest-priority
                one.
             2. Once some ti completes a full match, every thread with
                *lower* priority than ti can never produce a better match at
                this position (leftmost-first semantics: a higher-priority
                path that fully matches is always preferred over a lower-
                priority path, regardless of match length) -- so we must cut
                them, both from overwriting the recorded match and from
                extending into nxt.

           Walking cur high-to-low and calling add_thread(nxt, ...) in that
           order gets both of those right for *this* position. But it also
           means each ti's own (internally ascending) contribution lands in
           nxt in descending ti order -- e.g. [ti=3's entries][ti=2's
           entries][ti=1's entries] -- which would corrupt the very
           ascending-priority invariant the *next* position's iteration
           relies on (nxt becomes cur next iteration). So we track the
           [start,len) each processed ti contributed to nxt, and afterwards
           restore ascending order with a standard block-order reversal:
           reverse the whole array, then reverse each block back to its own
           internal order at its new position. That turns descending block
           order (with ascending content) into ascending block order (with
           ascending content restored) in O(nxt->count) -- no extra
           MAX_THREADS-sized buffer needed. */
        int blk_start[MAX_THREADS];
        int blk_len[MAX_THREADS];
        int n_blocks = 0;

        for (int ti = cur->count - 1; ti >= 0; ti--) {
            ReThread t = cur->threads[ti];
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
                memcpy(ms, found_saved, (size_t)nslot * sizeof(int));
                int nxt_before = nxt->count;
                int got = add_thread(re, nxt, &vis, nt, text, text_len, pos + 1, ms);
                int contributed = nxt->count - nxt_before;
                if (contributed > 0 && n_blocks < MAX_THREADS) {
                    blk_start[n_blocks] = nxt_before;
                    blk_len[n_blocks] = contributed;
                    n_blocks++;
                }
                if (got) {
                    memcpy(found_saved, ms, (size_t)nslot * sizeof(int));
                    found = 1;
                    /* This is the highest-priority thread (in this
                       high-to-low walk) that reached a match at this
                       position -- cut every remaining, strictly-lower-
                       priority thread in cur per the comment above. */
                    break;
                }
            }
        }

        /* Restore ascending priority order in nxt: a full-array reversal
           followed by reversing each (now relocated) block back to its own
           internal order. See the comment above the loop. */
        if (n_blocks > 1) {
            reverse_thread_range(nxt->threads, 0, nxt->count - 1);
            int total = nxt->count;
            for (int b = 0; b < n_blocks; b++) {
                int new_lo = total - (blk_start[b] + blk_len[b]);
                int new_hi = total - blk_start[b] - 1;
                reverse_thread_range(nxt->threads, new_lo, new_hi);
            }
        }

        /* Swap cur/nxt -- pointer swap, not a 71.7 KB struct copy. */
        { ThreadList *tmp = cur; cur = nxt; nxt = tmp; }
        /* Re-run epsilon transitions for the freshly advanced threads */
        /* (already handled inside add_thread — threads in nxt start at consuming op pc+1) */
    }

    if (found) {
        memcpy(match_saved, found_saved, (size_t)nslot * sizeof(int));
        return 1;
    }
    return 0;
}

/* Lookahead entry point for add_thread's OP_LOOKAHEAD case. Declares its own
   fresh ThreadList buffer pair, in *this* function's stack frame rather than
   add_thread's, and runs the sub-expression in a nested vm_exec_range call.
   A fresh pair is required (not the outer match's buffers): the outer
   vm_exec_range call that led here is still mid-flight -- its own cur/nxt
   still hold live threads for the enclosing match -- and the sub-expression
   being tested by the lookahead may have a different capture-group count,
   so it cannot safely reuse the caller's slots either way. Lookahead is a
   rare opcode, so paying a second ~143 KB frame (and its own __chkstk probe)
   only on this rarely-taken path, instead of on every add_thread call, is
   the right trade. */
static int vm_exec_lookahead(const ReHandle *re, const char *text, int text_len,
                             int start, int pc_start, int pc_end, int *match_saved)
{
    ThreadList tl_a, tl_b;
    return vm_exec_range(re, text, text_len, start, pc_start, pc_end, match_saved, &tl_a, &tl_b);
}

/* ===== Public exec API ===== */

int __ls_regex_exec(void *h, const char *text, int text_len, int start) {
    ReHandle *re = (ReHandle *)h;
    if (!re) return 0;

    /* Buffers hoisted out of vm_exec_range and shared across every start
       position tried below. Each is ~71.7 KB, so declaring the pair pays one
       __chkstk stack-probe here per __ls_regex_exec call, instead of once
       per start position in the loop (an anchored pattern that only ever
       matches at position 0, if at all, otherwise tries and fails ~text_len
       more times -- ~50 for this benchmark's ~49-byte corpus strings). Safe
       to share across iterations of this loop because they run strictly
       sequentially: each vm_exec_range call fully finishes (and either
       returns a match we act on immediately, or is done with the buffers)
       before the next one starts. */
    ThreadList tl_a, tl_b;

    /* Try matching starting at each position */
    const int nslot = re_slot_count(re);

    /* Tier 1: the pattern is nothing but a literal -- answer with the shared
       BMH/Sunday search and skip the VM entirely. */
    if (re->lit_is_whole && !(re->flags & LS_RE_IGNORECASE)) {
        int at = __ls_str_find(text, text_len, re->lit, re->lit_len, start);
        if (at < 0) {
            /* No match: clear the handle's captures so a later group() query
               cannot hand back offsets from an earlier, successful exec on
               this handle. */
            for (int k = 0; k < MAX_GROUPS * 2; k++) re->saved[k] = -1;
            return 0;
        }
        for (int k = 0; k < MAX_GROUPS * 2; k++) re->saved[k] = -1;
        re->saved[0] = at;
        re->saved[1] = at + re->lit_len;
        return re->n_groups + 1;
    }

    /* Tier 0: the pattern is anchored (see re_detect_anchor) -- skip every
       candidate start position that is provably doomed instead of paying
       for a VM call per position that fails on its very first
       instruction. anchor_mode is mutually exclusive with lit_is_whole
       (re_build_prefilter's literal scan starts right after the same
       leading OP_SAVE and stops the instant it sees a non-OP_CHAR
       instruction, so an anchor at prog[1] guarantees lit_len==0), so this
       never shadows the Tier-1 path above. */
    if (re->anchor_mode == 1) {
        /* Absolute: only text position 0 can ever satisfy the anchor. If
           the caller's start offset is already past 0, no position in
           [start, text_len] is 0, so the match is impossible -- the exact
           same answer the unconditional loop below would have computed
           after up to text_len+1 doomed vm_exec_range calls. */
        if (start > 0) {
            for (int k = 0; k < MAX_GROUPS * 2; k++) re->saved[k] = -1;
            return 0;
        }
        int ms[MAX_GROUPS * 2];
        for (int k = 0; k < nslot; k++) ms[k] = -1;
        int ok = vm_exec_range(re, text, text_len, 0, 0, re->prog_len - 1, ms, &tl_a, &tl_b);
        if (ok) {
            memcpy(re->saved, ms, (size_t)nslot * sizeof(int));
            return re->n_groups + 1;
        }
        for (int k = 0; k < MAX_GROUPS * 2; k++) re->saved[k] = -1;
        return 0;
    }

    if (re->anchor_mode == 2) {
        /* Line-start under MULTILINE: only position 0 or a position
           immediately after a '\n' can ever satisfy the anchor. Walk only
           those positions instead of every byte offset. */
        int s = (start < 0) ? 0 : start;
        if (s != 0) {
            while (s < text_len && text[s - 1] != '\n') s++;
        }
        while (s <= text_len) {
            int ms[MAX_GROUPS * 2];
            for (int k = 0; k < nslot; k++) ms[k] = -1;
            int ok = vm_exec_range(re, text, text_len, s, 0, re->prog_len - 1, ms, &tl_a, &tl_b);
            if (ok) {
                memcpy(re->saved, ms, (size_t)nslot * sizeof(int));
                return re->n_groups + 1;
            }
            if (s >= text_len) break;
            s++;
            while (s < text_len && text[s - 1] != '\n') s++;
        }
        for (int k = 0; k < MAX_GROUPS * 2; k++) re->saved[k] = -1;
        return 0;
    }

    for (int s = start; s <= text_len; s++) {
        /* Tier 2: every match starts with a known literal -- jump there. */
        if (re->lit_len > 0) {
            int at = __ls_str_find(text, text_len, re->lit, re->lit_len, s);
            if (at < 0) {
                /* No match: clear the handle's captures (see the tier-1
                   comment above). */
                for (int k = 0; k < MAX_GROUPS * 2; k++) re->saved[k] = -1;
                return 0;
            }
            s = at;
        }
        /* Tier 3: we know the set of bytes a match may start with. */
        else if (re->has_first_bytes) {
            while (s < text_len) {
                unsigned char ch = (unsigned char)text[s];
                if (ch < 128 &&
                    ((re->first_bytes[ch >> 3] >> (ch & 7)) & 1)) break;
                s++;
            }
            if (s >= text_len && text_len > 0) {
                /* Still try the empty position at end-of-text: a pattern like
                   [a-z]* can match empty there. */
                s = text_len;
            }
        }

        int ms[MAX_GROUPS * 2];
        for (int k = 0; k < MAX_GROUPS * 2; k++) ms[k] = -1;
        int ok = vm_exec_range(re, text, text_len, s, 0, re->prog_len - 1, ms, &tl_a, &tl_b);
        if (ok) {
            /* Copy only the slots this pattern uses; the rest of re->saved
               is left from an earlier exec but is never read, because
               __ls_regex_cap_start/_len are only valid for groups < n_groups+1. */
            memcpy(re->saved, ms, (size_t)nslot * sizeof(int));
            return re->n_groups + 1; /* groups + group-0 */
        }
    }
    /* No match: clear the handle's captures so a later group() query cannot
       hand back offsets from an earlier, successful exec on this handle. */
    for (int k = 0; k < MAX_GROUPS * 2; k++) re->saved[k] = -1;
    return 0;
}

int __ls_regex_cap_start(void *h, int group) {
    ReHandle *re = (ReHandle *)h;
    if (!re || group < 0 || group >= MAX_GROUPS) return -1;
    return re->saved[group * 2];
}

int __ls_regex_cap_len(void *h, int group) {
    ReHandle *re = (ReHandle *)h;
    if (!re || group < 0 || group >= MAX_GROUPS) return -1;
    int s = re->saved[group * 2];
    int e = re->saved[group * 2 + 1];
    if (s < 0 || e < 0) return -1;
    return e - s;
}

int __ls_regex_group_count(void *h) {
    ReHandle *re = (ReHandle *)h;
    return re ? re->n_groups : 0;
}

int __ls_regex_named_count(void *h) {
    ReHandle *re = (ReHandle *)h;
    return re ? re->n_named : 0;
}

const char *__ls_regex_named_name(void *h, int i) {
    ReHandle *re = (ReHandle *)h;
    if (!re || i < 0 || i >= re->n_named) return "";
    return re->named[i].name;
}

int __ls_regex_named_index(void *h, int i) {
    ReHandle *re = (ReHandle *)h;
    if (!re || i < 0 || i >= re->n_named) return -1;
    return re->named[i].group_id;
}
