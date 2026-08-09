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

/* Lazy DFA (D1): defensive cap on how large a DFA-eligible pattern's
   program may be. See the ReHandle.dfa_eligible comment and the big block
   above re_dfa_resolve for why the DFA-state count is ALREADY bounded by
   prog_len (a "state" is one pc, not a general NFA pc-set, because
   eligibility requires re->onepass==1) -- so this is not a combinatorial
   backstop the way a general subset-construction DFA would need, it is
   purely a memory cap on the dfa_trans table (RE_DFA_MAX_PROG * 256 *
   sizeof(short) bytes; 160 -> 80 KB worst case per compiled pattern, times
   RE_CACHE_SLOTS=32 thread-local cache slots -> 2.5 MB worst case per
   thread). Patterns above this (rare -- the benchmark's largest one-pass
   program is well under 100 instructions) simply fall back to the
   already-fast one-pass engine; __ls_regex_exec_dfa's fallback to
   __ls_regex_exec makes this always safe. */
#define RE_DFA_MAX_PROG 160

/* Sentinel values stored in ReHandle.dfa_trans / returned by re_dfa_resolve
   and re_dfa_step. Real states are pc values, always >= 0 and < prog_len
   (<= RE_DFA_MAX_PROG), so all three sentinels are negative and disjoint
   from any real state. */
#define RE_DFA_UNFILLED (-2)  /* cache cell not yet computed */
#define RE_DFA_DEAD     (-1)  /* no continuation: this path fails */
#define RE_DFA_ACCEPT   (-3)  /* OP_MATCH reached; group 0 ends HERE, byte not consumed */

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

    /* ---- One-pass classification (built once at compile time by
       re_is_onepass, read via __ls_regex_is_onepass) ----
       1 = the pattern can never have more than one live thread at any input
           position, so it is safe to execute by walking a single pc and
           writing capture offsets directly instead of running the Pike VM's
           thread list. 0 = general; must run the Pike VM.
       When onepass is 1, __ls_regex_exec routes to vm_exec_onepass instead
       of the Pike VM. See re_is_onepass for the analysis and its soundness
       argument. */
    int           onepass;

    /* ---- One-pass per-SPLIT dispatch tables (built once at compile time
       by re_build_onepass_tables, read by vm_exec_onepass) ----
       Heap array, re->prog_len entries, indexed by pc; only entries at an
       actual OP_SPLIT pc are ever populated or read (the rest sit
       zero-initialized and unused -- prog_len is typically small, so this
       is cheap and simpler than a sparse map). NULL unless onepass==1.
       Each entry holds the SAME FirstSet(operand_a)/FirstSet(operand_b)
       re_is_onepass already computes to classify the pattern -- see that
       function's rule 1+2+3 commentary for what these mean and why they
       are safe to use as a runtime dispatch decision. Recomputed by a
       second walk (re_build_onepass_tables) rather than having
       re_is_onepass itself stash them, so Task 1's already-verified
       classifier is not touched by this. */
    void         *onepass_splits;  /* ReSplitInfo*, opaque here to keep the
                                       type defined next to its only user */

    /* ---- Lazy DFA (match-only fast path, D1) ----
       See the big comment block above re_dfa_resolve for the design. A
       pattern is DFA-eligible (dfa_eligible=1) only when it is ALSO
       one-pass (re->onepass==1): that restriction is what makes a DFA
       "state" collapse to a single pc instead of a general NFA pc-set, so
       there is no subset-construction blowup to bound here -- see
       re_dfa_check_eligible for the exact rule and why it is sound.

       dfa_trans is a flat prog_len*256 table of cached (state,byte)
       transitions, lazily filled cell-by-cell as bytes are actually seen
       (re_dfa_step): each cell starts at RE_DFA_UNFILLED and is computed
       (and cached) at most once. NULL unless dfa_eligible==1. */
    int           dfa_eligible;
    short        *dfa_trans;

    /* ---- DFA-only exec bookkeeping (lazy capture fallback) ----
       The DFA answers "does it match" and "where does group 0 begin/end"
       -- it does not populate sub-group offsets. When the most recent exec
       on this handle ran via __ls_regex_exec_dfa's fast path AND matched,
       dfa_only_valid is set to 1 and the exact (text, text_len, start) that
       call was given is stashed here; re->saved[0..1] already holds the
       real group-0 span, but re->saved[2..] are left at -1. A later
       __ls_regex_cap_start/_len call for a group > 0 detects this and
       transparently re-runs the (cheaper-than-general, since dfa_eligible
       implies onepass) one-pass engine at the SAME start position before
       answering -- see re_dfa_fill_captures. This keeps
       __ls_regex_exec_dfa a SAFE drop-in for __ls_regex_exec at any call
       site: a caller who never reads a sub-group pays nothing extra, and
       one who does (Regex.group(i>0), capture_all(), ...) still gets the
       right answer, paying one extra one-pass re-exec exactly once on
       first such read. It is not, however, a CHEAP drop-in for a call
       site that reads several sub-groups on every call -- that pays the
       DFA scan AND the fallback re-exec where it used to pay only the
       re-exec, which is why regex.lls only routes call sites that
       provably never (find/find_all/replace/replace_all/split, which only
       ever touch group 0 themselves) or optionally (Regex.is_match(), a
       narrower-contract sibling of matches?()) read sub-groups through
       this function -- see is_match()'s doc comment in regex.lls for the
       full trade-off, discovered by benchmarking S2_fields (9 groups read
       on every call) after initially routing matches?()/find_from()
       through this path too and measuring a regression. */
    int           dfa_only_valid;
    const char   *dfa_only_text;
    int           dfa_only_text_len;
    int           dfa_only_start;

    /* ---- "which text did the last exec run against" fingerprint ----
       Captures are offsets, not bytes, so every accessor that turns them back
       into a substring has to be handed the text again -- and nothing ties
       that argument to the text the match actually ran against. Recording the
       (pointer, length) the last public exec was given lets those accessors
       refuse to answer for a different text instead of slicing it at foreign
       offsets and returning a plausible-looking wrong substring (which is
       what `group("99xyz", 1)` did after matching "abc12": Some("99x"), rc=0,
       no diagnostic).

       Set at the ENTRY of __ls_regex_exec / __ls_regex_exec_dfa, before any
       dispatch, so no early-return path can leave it stale. Deliberately NOT
       touched by re_dfa_fill_captures's internal re-exec: that replays the
       same text and must not look like a new call.

       A DETECTOR, NOT A PROOF. It cannot distinguish a freed text whose
       allocation was reused at the same address and length, and it does not
       try -- that needs lifetimes, which this language does not have. Two
       benign false "matches" are also possible and harmless: distinct Str
       values that share one .rodata literal, and a text mutated between exec
       and the read without changing pointer or length (whose offsets were
       already invalid, so refusing is right but not guaranteed). */
    const char   *last_text;
    int           last_text_len;
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

/* NOTE: a variable-length-memcpy version of thread propagation (copying
   only re_slot_count(re) ints of saved[] instead of the fixed MAX_GROUPS*2
   via plain struct assignment) was tried here and measured WORSE on cap/
   capmat (~7-16% slower, reproduced across two runs) -- MSVC compiles
   `memcpy` with a runtime-variable length as a real call, while the plain
   fixed-size `ReThread b = a;` struct assignment it would have replaced is
   small enough (136 bytes) that the compiler inlines it as a straight-line
   sequence of loads/stores. The call overhead outweighed the smaller
   payload for every pattern size tried. Reverted; see the task-vmopt
   report for the measurement. Left as a documented dead end so it is not
   retried without re-measuring. */

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

/* Value of one hex digit. Caller must have checked isxdigit(). */
static int hex_val(unsigned char ch) {
    if (ch >= '0' && ch <= '9') return ch - '0';
    if (ch >= 'a' && ch <= 'f') return ch - 'a' + 10;
    return ch - 'A' + 10;
}

static void comp_error(Compiler *c, const char *msg) {
    if (!c->had_error) {
        snprintf(c->error, sizeof(c->error), "%s", msg);
        c->had_error = 1;
    }
}

/* Allocate the next capture-group id, or -1 (with a diagnostic already set)
   when the pattern would exceed what saved[] can hold.

   Group N occupies slots N*2 and N*2+1 of `int saved[MAX_GROUPS * 2]`, so the
   last usable id is MAX_GROUPS - 1: group MAX_GROUPS writes one int past the
   end. Nothing used to enforce that anywhere on the path -- not this
   allocation, not `re->n_groups`, not the emitted OP_SAVE slot index, and not
   the two stores that finally commit it -- so a 17-group pattern silently
   corrupted the adjacent ReThread's pc field and 18+ killed the process
   (rc=127, all buffered output lost).

   The check belongs HERE, at the single allocation point, for two reasons:
   the stores that would overflow (`t.saved[in->operand_a]` in the thread-list
   VM, `match_saved[in->operand_a]` in the one-pass VM) are the hottest writes
   in the engine and must not grow a per-execution bounds check; and clamping
   the id instead of rejecting the pattern would quietly return offsets
   belonging to a different group, which is worse than an error. Pinned by
   tests/samples/regex_group_limit.lls. */
static int re_alloc_group(Compiler *c) {
    if (c->group_counter + 1 >= MAX_GROUPS) {
        comp_error(c, "too many capture groups (max 16); "
                      "use (?:...) for grouping that does not capture");
        return -1;
    }
    return ++c->group_counter;
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

        /* \xHH — one byte, two hex digits, either case. Must be handled
           explicitly: the `default` arm below would otherwise turn it into the
           literal characters "xHH", silently, so `^\x41$` matched the text
           "x41" instead of "A". */
        case 'x': {
            if (c->pos + 1 >= c->pat_len ||
                !isxdigit((unsigned char)c->pat[c->pos]) ||
                !isxdigit((unsigned char)c->pat[c->pos + 1])) {
                comp_error(c, "\\xHH needs exactly two hex digits");
                return -1;
            }
            int hi = hex_val((unsigned char)c->pat[c->pos]);
            int lo = hex_val((unsigned char)c->pat[c->pos + 1]);
            c->pos += 2;
            return emit(re, OP_CHAR, (hi << 4) | lo, 0);
        }

        /* \1 .. \9 — backreference syntax in every other engine. This one is a
           Pike-VM: a thread carries only a pc and the capture slots, and the
           linear-time guarantee comes precisely from never backtracking, so a
           backreference is not implementable here rather than merely missing.
           Rejecting is the only honest answer: the `default` arm below used to
           make `(ab)\1` mean "ab" followed by a literal '1', so it matched
           "ab1" and not "abab", with rc=0 and no diagnostic. */
        case '1': case '2': case '3': case '4': case '5':
        case '6': case '7': case '8': case '9':
            comp_error(c, "backreference (\\1-\\9) is not supported: this engine "
                          "never backtracks; capture the group and compare the "
                          "text yourself");
            return -1;

        /* Anything else is the literal character. This is what makes `\.`,
           `\+`, `\(`, `\\` and friends work, and it matches every other
           engine's treatment of escaped punctuation. It also means an
           unrecognised letter escape (`\q`) is just that letter -- pinned in
           tests/samples/regex_escapes.lls so narrowing this to a whitelist
           would be a deliberate change rather than an accident. */
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
                group_id = re_alloc_group(c);
                if (group_id < 0) return -1;
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
            group_id = re_alloc_group(c);
            if (group_id < 0) return -1;
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
    /* Skip the group-0 opening SAVE emitted by __ls_regex_compile.

       NOTE: a variant of this was tried that skipped the WHOLE leading run
       of OP_SAVE instructions (group-0 open plus any capturing group opens
       that begin the pattern, e.g. the benchmark's `([A-Za-z]+) ...`,
       sound by the same dominance argument as re_detect_anchor). It is
       correct and does let has_first_bytes fire for group-leading
       patterns that previously got no prefilter at all, but measured as a
       null result on the gated benchmark (cap ~49.1-49.9ms both with and
       without it, i.e. noise-level -- reproduced across three runs): most
       of the positions it lets the outer loop skip via a cheap byte check
       were already failing just as cheaply inside vm_exec_range (the
       class test fails on the first character, no thread survives, the
       position loop exits immediately), so the byte check is a lateral
       move, not a real skip. Reverted to keep the diff to the two levers
       with measured effect; see the task-vmopt report. */
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

/* ===== One-pass classification =====

   Detects, once per compiled pattern, whether execution can ever have more
   than one live thread at any input position. If it cannot, a later
   execution path (not this one -- this commit only computes and exposes the
   property) can walk a single pc and write capture offsets directly instead
   of running the Pike VM's thread list.

   THIS IS PURELY ANALYSIS. It does not change what __ls_regex_exec does.
   That separation is deliberate: an imprecise analysis here would, once
   something depends on it, turn directly into silently corrupted capture
   offsets -- the failure mode this project pays most dearly for (see
   CLAUDE.md's running list of rc=0 wrong-answer bugs). A false negative
   (calling a safe pattern "general") only costs the speed of a path this
   pattern would have qualified for; a false positive is unacceptable. Every
   rule below is written to fail toward "general" whenever it is not certain.

   ---- The byte-set representation ----

   A "byte set" here is 256 bits (32 bytes), one per possible input byte
   value 0-255 -- not the compiler's 128-bit ReCharClass (which only covers
   ASCII and is what OP_CLASS/OP_NCLASS operands point at). The wider set
   matters because OP_NDIGIT, OP_NWORD and OP_NSPACE are satisfied by any
   byte outside their positive class, including bytes 128-255, which the
   128-bit ReCharClass has no room to represent. re_consuming_first_bytes
   below computes each opcode's byte set by literally re-running the same
   byte-by-byte predicate vm_exec_range uses in its OP_CHAR/OP_CLASS/...
   switch (case-folding, DOTALL, class lookup and all) over all 256 byte
   values, rather than hand-deriving a table that could drift from the VM's
   actual matching semantics. */

#define RE_BYTESET_BYTES 32   /* 256 bits */

static void re_byteset_zero(unsigned char *bs) {
    memset(bs, 0, RE_BYTESET_BYTES);
}
static void re_byteset_set(unsigned char *bs, unsigned char b) {
    bs[b >> 3] |= (unsigned char)(1u << (b & 7));
}
static int re_byteset_intersects(const unsigned char *a, const unsigned char *b) {
    for (int i = 0; i < RE_BYTESET_BYTES; i++) if (a[i] & b[i]) return 1;
    return 0;
}
static int re_byteset_any(const unsigned char *a) {
    for (int i = 0; i < RE_BYTESET_BYTES; i++) if (a[i]) return 1;
    return 0;
}

/* Byte set of a single CONSUMING instruction (everything except SAVE,
   SPLIT, JUMP, the anchors, OP_LOOKAHEAD and OP_MATCH). Mirrors
   vm_exec_range's per-position switch exactly, byte by byte, instead of
   re-deriving the same classification a second, independently-maintained
   way -- see the file comment above. OP_CLASS/OP_NCLASS only ever test
   bytes 0-127 against the 128-bit ReCharClass (class_test indexes
   cls->bits[c>>3], which is out of bounds past byte 127), matching the
   comment on parse_class_body ("keep non-ASCII always off"): bytes >= 128
   never set a bit here for either polarity, which is the conservative
   answer for OP_NCLASS too (an unclaimed byte is simply absent from the
   set, never wrongly claimed). */
static void re_consuming_first_bytes(const ReHandle *re, const ReInstr *in, unsigned char *out) {
    re_byteset_zero(out);
    for (int ci = 0; ci < 256; ci++) {
        unsigned char ch = (unsigned char)ci;
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
        case OP_CLASS:
            if (ch < 128 && in->operand_a >= 0 && in->operand_a < re->n_classes) {
                const ReCharClass *cls = &re->classes[in->operand_a];
                unsigned char lc = (re->flags & LS_RE_IGNORECASE) ? (unsigned char)tolower(ch) : ch;
                match_char = class_test(cls, lc);
            }
            break;
        case OP_NCLASS:
            if (ch < 128 && in->operand_a >= 0 && in->operand_a < re->n_classes) {
                const ReCharClass *cls = &re->classes[in->operand_a];
                unsigned char lc = (re->flags & LS_RE_IGNORECASE) ? (unsigned char)tolower(ch) : ch;
                match_char = !class_test(cls, lc);
            }
            break;
        case OP_DIGIT:  match_char = isdigit(ch) != 0; break;
        case OP_NDIGIT: match_char = !isdigit(ch); break;
        case OP_WORD:   match_char = is_word_char(ch) != 0; break;
        case OP_NWORD:  match_char = !is_word_char(ch); break;
        case OP_SPACE:  match_char = is_space_char(ch) != 0; break;
        case OP_NSPACE: match_char = !is_space_char(ch); break;
        default: break; /* not a consuming opcode; caller never asks */
        }
        if (match_char) re_byteset_set(out, ch);
    }
}

/* Result of an epsilon-closure walk from one program counter: the union of
   every consuming instruction's byte set reachable via epsilon transitions
   only, plus whether OP_MATCH is reachable via an all-epsilon path (i.e.
   the closure can accept having consumed zero further bytes from here). */
typedef struct {
    unsigned char bytes[RE_BYTESET_BYTES];
    int           can_match_empty;
} ReFirstSet;

/* Per-OP_SPLIT dispatch entry for the one-pass executor (vm_exec_onepass,
   defined further down). Holds exactly the two FirstSets re_is_onepass
   computes to classify a SPLIT (rule 1+2+3), so the executor can decide
   which branch to take from the current input byte in O(1) instead of
   re-walking the epsilon closure at run time. See re_build_onepass_tables
   and vm_exec_onepass for how this is populated and consumed. Declared
   here, next to ReFirstSet, rather than down by its use so ReHandle (above)
   can hold a pointer to an array of these without needing this whole
   analysis section forward-declared -- ReHandle's field is `void *` for
   that reason and is cast back to `ReSplitInfo *` at every use. */
typedef struct {
    ReFirstSet a;
    ReFirstSet b;
} ReSplitInfo;

/* Epsilon-closure walk computing FirstSet(pc): follow SAVE, JUMP and BOTH
   branches of SPLIT (all zero-width) and the four positional anchors
   (^ $ \A \Z, also zero-width) until a consuming instruction is reached
   (its byte set is OR'd in and this path stops) or OP_MATCH is reached
   (can_match_empty is set and this path stops).

   `seen` is a caller-owned array of re->prog_len flags, zeroed by the
   caller before the top-level call. It both breaks the cycles that JUMP
   loops (`*`/`+`) introduce -- the plan's watch item -- and makes this
   traversal a sound, complete reachability computation despite them: once
   a pc has been visited, everything reachable FROM it has already been
   folded into `out`, so a revisit correctly contributes nothing further
   (standard DFS-with-visited graph reachability; it does not undercount).

   Each call site gives each branch of a SPLIT it is inspecting its OWN
   fresh `seen` array (see re_is_onepass below) -- the two branches must be
   allowed to independently revisit whatever pcs they each reach, including
   pcs the OTHER branch also reaches, since rule 2 below needs each
   branch's byte set computed in full on its own.

   Treats OP_WORDBND/OP_NWORDBND and OP_LOOKAHEAD as unreachable in
   practice: re_is_onepass rejects any pattern containing either, globally,
   before ever calling this, for reasons that don't fit a byte set (see the
   comment on that rejection). If one is ever reached here regardless (it
   should not be, but "bail on anything unsure" applies), it is treated
   like any other unrecognized opcode: contributes nothing and is flagged
   via `unsupported` so the caller can force the whole pattern to "general"
   rather than silently under-approximate. */
static void re_first_set(const ReHandle *re, int pc, unsigned char *seen,
                          ReFirstSet *out, int *unsupported)
{
    if (pc < 0 || pc >= re->prog_len) { *unsupported = 1; return; }
    if (seen[pc]) return;
    seen[pc] = 1;

    const ReInstr *in = &re->prog[pc];
    switch (in->op) {
    case OP_SAVE:
        re_first_set(re, pc + 1, seen, out, unsupported);
        return;
    case OP_JUMP:
        re_first_set(re, in->operand_a, seen, out, unsupported);
        return;
    case OP_SPLIT:
        re_first_set(re, in->operand_a, seen, out, unsupported);
        re_first_set(re, in->operand_b, seen, out, unsupported);
        return;
    case OP_ANCHOR_BOL:
    case OP_ANCHOR_EOL:
    case OP_ANCHOR_BOS:
    case OP_ANCHOR_EOS:
        /* Zero-width positional test. Its pass/fail depends only on the
           input position, not on which byte (if any) sits at that
           position, so -- unlike OP_WORDBND below -- stepping over it
           optimistically (as if it always passes) cannot make two
           genuinely disjoint branches look like they share a byte: the
           byte reachable beyond the anchor is a property of the pattern
           text at that point, independent of whether the anchor itself
           holds. Worst case this over-approximates reachability (treats a
           position the anchor would actually reject as reachable too),
           which only pushes an intersection test toward "general", never
           the other way. */
        re_first_set(re, pc + 1, seen, out, unsupported);
        return;
    case OP_MATCH:
        out->can_match_empty = 1;
        return;
    case OP_WORDBND:
    case OP_NWORDBND:
    case OP_LOOKAHEAD:
        /* Should be unreachable -- re_is_onepass rejects these globally
           before calling here. Defensive only; see the function comment. */
        *unsupported = 1;
        return;
    default: {
        /* Consuming instruction: fold in its byte set and stop -- this is
           where the epsilon closure along this path ends. */
        unsigned char bs[RE_BYTESET_BYTES];
        re_consuming_first_bytes(re, in, bs);
        for (int i = 0; i < RE_BYTESET_BYTES; i++) out->bytes[i] |= bs[i];
        return;
    }
    }
}

/* The property to compute (see the module comment above): a pattern is
   one-pass if the set of live threads can never exceed one at any input
   position. Walked as a per-OP_SPLIT check, since OP_SPLIT is the only
   instruction that ever forks execution -- a program with none is trivially
   one-pass (e.g. "([0-9]{4})-([0-9]{2})-([0-9]{2})": {n} with n==m needs no
   SPLIT at all, just repeated body copies).

   Rule 4 (OP_LOOKAHEAD anywhere -> not one-pass): lookahead runs a whole
   nested sub-VM (vm_exec_lookahead) that does its own, separate epsilon
   closure and can itself contain arbitrary alternation; folding its
   contents into this analysis is out of scope, so any pattern containing
   one is rejected outright, unconditionally.

   Also rejects OP_WORDBND/OP_NWORDBND (\b \B) anywhere, unconditionally, by
   the same global-reject mechanism, for a reason specific to this analysis
   (not called out as its own numbered rule in the design because it only
   surfaced while implementing rule 1's "follow anchors" step): unlike ^ $
   \A \Z, whether \b holds depends on the very byte a first-byte-set
   computation is trying to classify (is *this* byte a word character,
   relative to the previous one) -- it is not a pure position test. Folding
   it in as a plain epsilon pass-through, the way the other anchors are
   handled, would silently mix "the anchor holds" and "the anchor doesn't"
   byte sets together under one name, which is exactly the kind of
   imprecision this analysis cannot safely absorb. Rejecting the whole
   pattern is the conservative answer; \b is rare enough in the extraction
   patterns this feature targets that losing the fast path on it costs
   little.

   Rule 1+2: for each OP_SPLIT, compute FirstSet(operand_a) and
   FirstSet(operand_b) independently (each gets its own fresh `seen`, so
   each branch's closure is computed to completion on its own -- see
   re_first_set's comment on why that is required, not just convenient).
   If the two byte sets intersect, some byte could be the very next one
   consumed via EITHER branch -- two genuinely different continuations
   would both be live -- so the pattern is not one-pass.

   Rule 3 (self-ambiguous branch, not a cross-branch comparison): a branch
   is rejected on its own if ITS OWN closure can both consume a byte (a
   non-empty byte set) AND reach OP_MATCH with zero further bytes consumed.
   This is deliberately narrower than "the other branch has a non-empty
   byte set" -- see op1-report.md for why a naive cross-branch
   version of this check would reject the exact flat trailing-quantifier
   shapes (`[^ ]+` at the end of a pattern, `([^\"]*)"`, ...) this whole
   feature exists to speed up, while a plain greedy-priority "prefer to
   keep consuming while the byte matches, only fall through to the
   immediate-match branch once it does not" is fully deterministic and
   correct for that flat shape -- no second live thread is ever actually
   needed. What a same-branch check like this one catches instead is an
   EMPTY LOOP BODY nested inside another loop, e.g. `(a*)+`: entering the
   outer `+`'s loop branch, the inner `a*` can immediately match zero
   times, which loops the outer `+` straight back to itself having
   consumed nothing -- so *that one branch alone* is ambiguous between
   "consume an 'a' now" and "match right here, zero more bytes", which is
   a real second live thread at the very same position, not resolvable by
   the greedy-priority-with-fallback argument above (that argument only
   works when the "keep consuming" option and the "stop now" option are
   the two DIFFERENT branches of the split, not two possibilities inside
   the very branch that is supposed to unambiguously mean "keep
   consuming"). */
static int re_is_onepass(const ReHandle *re) {
    if (!re || re->prog_len <= 0) return 0;

    for (int i = 0; i < re->prog_len; i++) {
        ReOpCode op = re->prog[i].op;
        if (op == OP_LOOKAHEAD || op == OP_WORDBND || op == OP_NWORDBND) return 0;
    }

    /* seenA/seenB are sized to MAX_INSTRS (like Visited.gen elsewhere in
       this file) so they are safe to index with any in-range pc regardless
       of this particular pattern's prog_len; only the first prog_len
       entries are ever touched. This runs once per compile, not on any
       execution hot path, so the repeated zeroing here is not a concern
       the way it was for Visited (see that struct's comment). */
    unsigned char seenA[MAX_INSTRS];
    unsigned char seenB[MAX_INSTRS];

    for (int pc = 0; pc < re->prog_len; pc++) {
        if (re->prog[pc].op != OP_SPLIT) continue;
        const ReInstr *sp = &re->prog[pc];

        ReFirstSet ra, rb;
        memset(&ra, 0, sizeof(ra));
        memset(&rb, 0, sizeof(rb));
        int unsupported = 0;

        memset(seenA, 0, (size_t)re->prog_len);
        re_first_set(re, sp->operand_a, seenA, &ra, &unsupported);
        memset(seenB, 0, (size_t)re->prog_len);
        re_first_set(re, sp->operand_b, seenB, &rb, &unsupported);

        if (unsupported) return 0;

        if (re_byteset_intersects(ra.bytes, rb.bytes)) return 0;              /* rule 2 */
        if (re_byteset_any(ra.bytes) && ra.can_match_empty) return 0;         /* rule 3 */
        if (re_byteset_any(rb.bytes) && rb.can_match_empty) return 0;         /* rule 3 */
    }

    return 1;
}

/* Populate re->onepass_splits: for every OP_SPLIT pc, the same
   FirstSet(operand_a)/FirstSet(operand_b) re_is_onepass computed above to
   classify the pattern, this time stashed so vm_exec_onepass can look them
   up by pc instead of recomputing an epsilon closure on every step of every
   match attempt. Deliberately a SEPARATE walk (calling the same re_first_set
   helper a second time) rather than having re_is_onepass itself save its
   results -- that keeps Task 1's already-tested classifier untouched by
   this addition; the extra walk is compile-time-only and cheap (the TLS
   pattern cache means most patterns pay it once, not once per exec).

   Only called when re_is_onepass has already returned 1. If anything here
   disagrees with that verdict (should be unreachable -- defensive only, see
   the `unsupported` comment on re_first_set) or allocation fails, this
   fails CLOSED: it clears re->onepass back to 0 so __ls_regex_exec falls
   back to the Pike VM rather than run vm_exec_onepass without valid
   dispatch tables. */
static void re_build_onepass_tables(ReHandle *re) {
    ReSplitInfo *tab = (ReSplitInfo *)calloc((size_t)re->prog_len, sizeof(ReSplitInfo));
    if (!tab) { re->onepass = 0; return; }

    unsigned char seen[MAX_INSTRS];

    for (int pc = 0; pc < re->prog_len; pc++) {
        if (re->prog[pc].op != OP_SPLIT) continue;
        const ReInstr *sp = &re->prog[pc];
        ReSplitInfo *info = &tab[pc];
        int unsupported = 0;

        memset(seen, 0, (size_t)re->prog_len);
        re_first_set(re, sp->operand_a, seen, &info->a, &unsupported);
        memset(seen, 0, (size_t)re->prog_len);
        re_first_set(re, sp->operand_b, seen, &info->b, &unsupported);

        if (unsupported) {
            free(tab);
            re->onepass = 0;
            return;
        }
    }

    re->onepass_splits = tab;
}

/* ===== Lazy DFA eligibility (D1) =====

   Whether __ls_regex_exec_dfa may route this pattern through the byte-table
   walk (re_exec_vm_dfa) instead of the one-pass NFA walk (vm_exec_onepass).
   Called once per compile, only when re->onepass is already 1.

   Requires re->onepass==1 (see the ReHandle.dfa_eligible comment): the
   textbook lazy DFA has a state be a SET of NFA program counters (the
   epsilon closure reached so far), and computing/caching transitions for
   arbitrary sets is exactly where a pathological pattern can blow up
   combinatorially. Restricting to one-pass patterns collapses every
   reachable set to size <= 1 (that is what "one-pass" means: at most one
   live thread at any position), so a DFA state here is literally a single
   pc -- no subset explosion is possible by construction, and no state-count
   budget is needed beyond RE_DFA_MAX_PROG's plain memory cap.

   Two further restrictions, both about a single opcode kind (OP_ANCHOR_BOL/
   OP_ANCHOR_BOS), because the byte-indexed transition cache
   (re->dfa_trans, keyed only by (pc, byte)) has no room to also key on
   "is this position 0" the way vm_exec_onepass's live pos variable can:

   1. LS_RE_MULTILINE is rejected outright. Under multiline, ^ can pass at
      ANY position immediately after a '\n', not just position 0 -- that
      is a per-position fact the cached table cannot see (a cached
      (pc,byte) transition, once filled, is reused at every later position
      that lands on the same pc, regardless of what came before it).
      re_dfa_resolve's ANCHOR_EOL handling below shows the case where this
      IS safely knowable from the byte alone (multiline $ checking whether
      the byte about to be read is '\n'); BOL has no such byte-local
      substitute because it looks at the PRECEDING byte, not the current
      one.

   2. OP_ANCHOR_BOL/OP_ANCHOR_BOS anywhere except exactly pc==1 (the
      position re_detect_anchor already proves dominates every execution
      path, and which re_exec_vm_dfa handles OUTSIDE the per-byte cache by
      only ever trying start position 0 when anchor_mode==1 -- see that
      function). A mid-pattern BOL/BOS (e.g. "(^a|b)") tests "is the CURRENT
      position 0", which is a fact about how far the outer scan has
      advanced, not about the pc or the byte at that pc -- again not
      representable in a (pc,byte)-keyed cache. This restriction is what
      lets re_dfa_resolve treat a BOL/BOS it reaches (necessarily at pc==1,
      necessarily while resolving the unique start state, necessarily at
      position 0 -- see that function's own comment) as an unconditional
      PASS: without it, a mid-pattern BOL/BOS could reach this same case at
      a position that is NOT 0, and unconditionally passing it there would
      be a silent WRONG ANSWER (accepting positions vm_exec_onepass would
      have rejected) -- this eligibility scan is what prevents that. */
static int re_dfa_check_eligible(const ReHandle *re) {
    if (!re->onepass) return 0;
    if (re->flags & LS_RE_MULTILINE) return 0;
    if (re->prog_len <= 0 || re->prog_len > RE_DFA_MAX_PROG) return 0;

    for (int pc = 0; pc < re->prog_len; pc++) {
        ReOpCode op = re->prog[pc].op;
        if ((op == OP_ANCHOR_BOL || op == OP_ANCHOR_BOS) && pc != 1) return 0;
    }
    return 1;
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
    re->onepass = re_is_onepass(re);
    if (re->onepass) re_build_onepass_tables(re);

    re->dfa_eligible = re_dfa_check_eligible(re);
    if (re->dfa_eligible) {
        /* Flat prog_len*256 cache, lazily filled cell-by-cell by
           re_dfa_step -- see the ReHandle.dfa_trans comment. Every cell
           starts at RE_DFA_UNFILLED; a calloc'd-then-memset short array is
           simplest and this runs once per distinct compiled pattern (the
           thread-local pattern cache means most patterns pay this once,
           not once per exec). Fails CLOSED like re_build_onepass_tables
           above: if the allocation fails, dfa_eligible drops back to 0 so
           __ls_regex_exec_dfa falls back to the (already fully verified)
           one-pass/general engines instead of running the DFA walk with no
           table to cache into. */
        size_t ncells = (size_t)re->prog_len * 256;
        short *tab = (short *)malloc(ncells * sizeof(short));
        if (!tab) {
            re->dfa_eligible = 0;
        } else {
            for (size_t i = 0; i < ncells; i++) tab[i] = RE_DFA_UNFILLED;
            re->dfa_trans = tab;
        }
    }
    return re;
}

void __ls_regex_free(void *h) {
    ReHandle *re = (ReHandle *)h;
    if (!re) return;
    free(re->onepass_splits);
    free(re->dfa_trans);
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
   in this position step, to avoid duplicate threads.

   Represented as a generation counter per pc rather than a bitmap: pc `p`
   counts as "visited in the current generation" exactly when
   gen[p] == current. A "clear" is then a single increment of `current`
   instead of an O(MAX_INSTRS/8) memset -- visited_clear is called once per
   text position stepped through (plus once to seed the initial thread),
   so on the cap/capmat shapes (many start positions x many positions
   stepped per start) that memset dominated: it always zeroed the full
   256-byte bitmap sized for MAX_INSTRS, regardless of how few instructions
   the compiled pattern actually used.

   Ownership mirrors ThreadList's buf_a/buf_b (see the comment above
   vm_exec_range): Visited is hoisted out of vm_exec_range into a parameter
   the caller owns, instead of being a >=4KB per-call local. vm_exec_range
   runs once per candidate start position -- up to text_len+1 times for an
   unanchored pattern -- and a local that size would reintroduce exactly
   the repeated __chkstk stack-probe cost that comment documents fixing for
   ThreadList. Hoisting also means `current` stays monotonic across every
   candidate start tried within one __ls_regex_exec call: since it only
   ever increases (short of the wraparound handled in visited_clear), a
   stale gen[pc] from an earlier candidate start can never equal the new
   `current`, so the array never needs re-zeroing except once, when the
   Visited is first constructed (visited_init).

   gen[] is declared at the MAX_INSTRS worst-case size (a fixed, one-time
   stack reservation -- irrelevant to per-call cost), but every zeroing
   pass (visited_init, and visited_clear's wraparound fallback) only
   touches the first `prog_len` entries, not all MAX_INSTRS. This matters:
   the OLD bitmap this replaces was ALSO fixed at MAX_INSTRS/8 = 256 bytes
   regardless of how few instructions a given pattern actually compiled to,
   so a first cut of this change that zeroed the full gen[MAX_INSTRS] once
   per __ls_regex_exec call measurably REGRESSED small patterns like
   "^GET " (prog_len ~8): one 8192-byte memset beat out five 256-byte
   memsets it replaced. Sizing the zeroing pass to the pattern's actual
   prog_len (a handful of ints for "^GET ", tens for the 5-group
   benchmark pattern) is what makes this a strict improvement over the old
   bitmap at every pattern size instead of only the larger ones. */

typedef struct {
    unsigned gen[MAX_INSTRS];
    unsigned current;
} Visited;

static void visited_init(Visited *v, int prog_len) {
    memset(v->gen, 0, (size_t)prog_len * sizeof(v->gen[0]));
    v->current = 0;
}
static void visited_clear(Visited *v, int prog_len) {
    v->current++;
    if (v->current == 0) {
        /* Wrapped past UINT_MAX increments. Never observed in practice --
           this call is made once per text position stepped through per
           candidate start position within a single __ls_regex_exec call,
           so it would take billions of text-position steps in one call to
           reach -- but a wrapped counter without this guard would make an
           old generation's gen[pc]==0 collide with a freshly-wrapped
           current==0, resurrecting stale "visited" marks. A full reset
           restores the invariant; skip generation 0 afterwards so it stays
           reserved for "never visited" (matching a freshly-init'd array). */
        memset(v->gen, 0, (size_t)prog_len * sizeof(v->gen[0]));
        v->current = 1;
    }
}
static int  visited_test(Visited *v, int pc) { return v->gen[pc] == v->current; }
static void visited_set(Visited *v, int pc) { v->gen[pc] = v->current; }

/* Thread list */
typedef struct {
    ReThread threads[MAX_THREADS];
    int      count;
} ThreadList;

static void tl_init(ThreadList *tl) { tl->count = 0; }

/* Reverse ReThread entries in the inclusive range [lo, hi] in place. Used
   both by add_thread's OP_SPLIT case (to restore ascending priority order
   after exploring the higher-priority branch first -- see the comment
   there) and by vm_exec_range's per-position loop (to restore priority
   order across multiple top-level threads -- see the comment above that
   loop). No-op if lo >= hi. Defined ahead of add_thread because add_thread
   now needs it too. */
static void reverse_thread_range(ReThread *threads, int lo, int hi) {
    while (lo < hi) {
        ReThread tmp = threads[lo];
        threads[lo] = threads[hi];
        threads[hi] = tmp;
        lo++;
        hi--;
    }
}

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
                         ThreadList *buf_a, ThreadList *buf_b, Visited *vis);
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
            /* Explore the higher-priority A branch FIRST (both operands are
               "offset A (greedy/preferred first), offset B" per the OP_SPLIT
               comment in the ReOp enum above). This matters for two
               distinct reasons, both about which branch gets "first claim"
               when A and B can reach the *same* downstream pc (an ambiguous
               / self-overlapping split -- e.g. nested quantifiers where an
               inner subexpression can match empty, or two alternatives that
               both accept the empty string):

               1. `vis` dedup is shared across both branches at this
                  position. Whichever branch's traversal reaches a shared pc
                  FIRST claims it (visited_set); the other is silently cut
                  short there (visited_test). For leftmost-first semantics,
                  the higher-priority branch must get first claim, so it
                  must run first -- not the other way around.
               2. If A's own subtree reaches OP_MATCH, B is *strictly*
                  lower priority than an already-found match and must
                  contribute nothing at all: not to `next` (a surviving
                  low-priority thread there could complete a match at a
                  later position and wrongly overwrite match_saved, since
                  nothing else in this engine re-checks priority across
                  positions) and not another, competing write to
                  match_saved. So B is skipped entirely, not just
                  out-prioritized, whenever A already matched.

               (This inverts the previous B-then-A order, which gave B first
               claim on shared pcs and let B's leftover `next` entries
               outlive an A-side match found later in the same closure --
               the root cause of the nested-repetition / ambiguous-
               alternation capture bugs this comment's commit fixes.)

               `next` order: every caller of `next` (this function's own
               parent split, if nested, and vm_exec_range's per-position
               loop) requires ASCENDING priority order -- lowest-priority
               entry at the lowest index, so "cur->threads[count-1] is the
               highest-priority thread" holds. Running A first appends A's
               entries before B's, i.e. DESCENDING order for this split's
               own contribution; swap the two resulting blocks back into
               ascending order the same way vm_exec_range's per-position
               loop restores it across multiple top-level threads: full-
               range reverse, then reverse each block back to its own
               internal order. */
            int base = next->count;

            ReThread ta = t;
            ta.pc = in->operand_a;
            int found_a = add_thread(re, next, vis, ta, text, text_len, pos, match_saved);
            int len_a = next->count - base;

            int found_b = 0;
            int len_b = 0;
            if (!found_a) {
                ReThread tb = t;
                tb.pc = in->operand_b;
                int b_before = next->count;
                found_b = add_thread(re, next, vis, tb, text, text_len, pos, match_saved);
                len_b = next->count - b_before;
            }

            if (len_a > 0 && len_b > 0) {
                reverse_thread_range(next->threads, base, base + len_a + len_b - 1);
                reverse_thread_range(next->threads, base, base + len_b - 1);
                reverse_thread_range(next->threads, base + len_b, base + len_a + len_b - 1);
            }

            return found_any || found_a || found_b;
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
   own pair rather than reusing the outer match's. reverse_thread_range is
   now defined above (ahead of add_thread), which also uses it. */

static int vm_exec_range(const ReHandle *re, const char *text, int text_len,
                         int start, int pc_start, int pc_end,
                         int *match_saved,
                         ThreadList *buf_a, ThreadList *buf_b, Visited *vis)
{
    (void)pc_end; /* we rely on OP_MATCH to terminate */

    /* Two buffers plus a pointer swap.  The previous code did `cur = nxt`
       once per character position, which copies the whole ThreadList struct
       (MAX_THREADS * sizeof(ReThread) ~= 71.7 KB) regardless of how many
       threads are actually live -- measured at 15-38x of total exec time. */
    ThreadList *cur = buf_a, *nxt = buf_b;
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
        visited_clear(vis, re->prog_len);
        add_thread(re, cur, vis, t0, text, text_len, start, found_saved);
        if (found_saved[0] >= 0 && found_saved[1] >= 0) {
            found = 1;
        }
    }

    for (int pos = start; pos <= text_len; pos++) {
        if (cur->count == 0) break;

        tl_init(nxt);
        visited_clear(vis, re->prog_len);

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
                int got = add_thread(re, nxt, vis, nt, text, text_len, pos + 1, ms);
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
   the right trade. Same reasoning applies to Visited now that it is a
   hoisted parameter rather than a vm_exec_range local: this nested call
   needs its own, independent from the outer match's (still mid-flight,
   possibly a different pc range), so it gets a fresh one here too. */
static int vm_exec_lookahead(const ReHandle *re, const char *text, int text_len,
                             int start, int pc_start, int pc_end, int *match_saved)
{
    ThreadList tl_a, tl_b;
    Visited vis;
    visited_init(&vis, re->prog_len);
    return vm_exec_range(re, text, text_len, start, pc_start, pc_end, match_saved, &tl_a, &tl_b, &vis);
}

/* ===== One-pass executor =====

   Walks a SINGLE pc and writes capture offsets into a SINGLE saved[] array
   -- no thread list, no per-position epsilon closure fan-out, no ThreadList/
   Visited buffers. Only ever called when re->onepass is 1, i.e. Task 1's
   analysis has already proven at most one thread can be alive at any input
   position for this pattern.

   THE HAZARD (see the plan and the file header on re_is_onepass): in the
   Pike VM every thread carries its own saved[], so a thread that dies takes
   its SAVE writes with it -- speculation is safe because it is discarded on
   failure. Here there is exactly one saved[] and every SAVE write lands
   directly in match_saved, non-speculatively. This is only sound because
   the one-pass property guarantees there is never a competing path: at
   every OP_SPLIT, the decision below (which branch to take) is made by the
   SAME disjointness/self-ambiguity facts (re->onepass_splits, built by
   re_build_onepass_tables from the identical re_first_set computation
   re_is_onepass used to classify the pattern) that make the classifier
   correct in the first place. If a SPLIT's two branches were not actually
   disjoint, this function would silently pick a branch, consume/SAVE along
   it, and never discover the other branch might also have matched --
   exactly the corrupted-capture failure mode the classifier exists to
   prevent. This function does not re-derive that guarantee; it TRUSTS it.
   Any imprecision belongs in re_is_onepass/re_build_onepass_tables, not
   here.

   A failure at any point (a byte does not match a consuming instruction, an
   anchor fails, or a SPLIT reaches a position where neither branch's first
   byte matches and neither branch can match empty) means this ENTIRE start
   position fails -- there is no other thread to fall back to, so return 0
   immediately. The caller's start-position loop (mirroring the Pike VM
   path's prefilter/anchor tiers) tries the next candidate start. */
static int vm_exec_onepass(const ReHandle *re, const char *text, int text_len,
                            int start, int *match_saved)
{
    const int nslot = re_slot_count(re);
    for (int k = 0; k < nslot; k++) match_saved[k] = -1;

    const ReSplitInfo *splits = (const ReSplitInfo *)re->onepass_splits;
    int pc  = 0;
    int pos = start;

    for (;;) {
        const ReInstr *in = &re->prog[pc];

        switch (in->op) {

        case OP_SAVE:
            match_saved[in->operand_a] = pos;
            pc++;
            continue;

        case OP_JUMP:
            pc = in->operand_a;
            continue;

        case OP_SPLIT: {
            const ReSplitInfo *info = &splits[pc];
            int chosen = -1;

            if (pos < text_len) {
                unsigned char ch = (unsigned char)text[pos];
                int in_a = (info->a.bytes[ch >> 3] >> (ch & 7)) & 1;
                int in_b = (info->b.bytes[ch >> 3] >> (ch & 7)) & 1;
                /* re_is_onepass's rule 2 guarantees these are never both
                   true -- the classifier would have rejected this pattern
                   otherwise. */
                if (in_a)      chosen = in->operand_a;
                else if (in_b) chosen = in->operand_b;
            }
            if (chosen < 0) {
                /* Either at end of text (no byte to test) or the current
                   byte matched neither branch's first-byte set: fall
                   through via whichever branch can match having consumed
                   zero further bytes here. Rule 3 guarantees at most one of
                   the two can_match_empty (a branch with a nonempty byte
                   set can never also be flagged can_match_empty by that
                   rule), so this is not a second live-thread choice -- it
                   is reading off the single answer the classifier already
                   proved unique. */
                if (info->a.can_match_empty)      chosen = in->operand_a;
                else if (info->b.can_match_empty)  chosen = in->operand_b;
                else return 0;  /* dead end: this start position fails */
            }
            pc = chosen;
            continue;
        }

        case OP_ANCHOR_BOL:
            if (re->flags & LS_RE_MULTILINE) {
                if (pos == 0 || (pos > 0 && text[pos-1] == '\n')) { pc++; continue; }
            } else {
                if (pos == 0) { pc++; continue; }
            }
            return 0;

        case OP_ANCHOR_EOL:
            if (re->flags & LS_RE_MULTILINE) {
                if (pos == text_len || (pos < text_len && text[pos] == '\n')) { pc++; continue; }
            } else {
                if (pos == text_len) { pc++; continue; }
            }
            return 0;

        case OP_ANCHOR_BOS:
            if (pos == 0) { pc++; continue; }
            return 0;

        case OP_ANCHOR_EOS:
            if (pos == text_len) { pc++; continue; }
            return 0;

        case OP_MATCH:
            return 1;

        case OP_WORDBND:
        case OP_NWORDBND:
        case OP_LOOKAHEAD:
            /* Unreachable in a well-formed one-pass program: re_is_onepass
               rejects any pattern containing these globally before
               re->onepass is ever set to 1, so vm_exec_onepass is never
               invoked on a program that can reach this case. Defensive
               only -- fail rather than guess at a result. */
            return 0;

        default: {
            /* Consuming instruction -- mirrors vm_exec_range's per-position
               switch exactly (same opcodes, same case-folding/DOTALL/class
               rules), just applied to one byte at the current pc/pos
               instead of to every live thread. */
            if (pos >= text_len) return 0;
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
            default: return 0; /* unreachable: every opcode is handled above or here */
            }
            if (!match_char) return 0;
            pos++;
            pc++;
            continue;
        }
        }
    }
}

/* ===== Public exec API =====

   __ls_regex_exec is a thin dispatcher: it owns only the Tier-1 whole-
   literal fast path (needs neither engine) and then hands off to one of two
   engines with completely separate stack frames -- re_exec_vm_general (the
   Pike VM, unchanged from before this task) or re_exec_vm_onepass (new).
   Keeping them as separate functions, not a runtime branch inside one
   function, matters for more than readability: re_exec_vm_general declares
   ThreadList tl_a/tl_b and a Visited buffer (~150 KB combined -- see their
   own comments for why they are hoisted this high already), and a local
   declaration's frame cost is paid at function entry regardless of which
   branch of an if/else actually touches it. A one-pass pattern that took
   that hit on every __ls_regex_exec call would be paying the exact
   __chkstk stack-probe cost this file has already gone to some trouble to
   avoid, for buffers it never uses. */

static long long re_onepass_debug_execs;   /* see __ls_regex_debug_onepass_execs */
static long long re_general_debug_execs;   /* see __ls_regex_debug_general_execs */

/* Tier 0/2/3 candidate-position selection mirrors re_exec_vm_general exactly
   (see that function's own comments for why each tier is sound) -- the only
   difference is which engine runs at each candidate position. */
static int re_exec_vm_onepass(ReHandle *re, const char *text, int text_len, int start) {
    const int nslot = re_slot_count(re);

    if (re->anchor_mode == 1) {
        if (start > 0) {
            for (int k = 0; k < MAX_GROUPS * 2; k++) re->saved[k] = -1;
            return 0;
        }
        int ms[MAX_GROUPS * 2];
        re_onepass_debug_execs++;
        int ok = vm_exec_onepass(re, text, text_len, 0, ms);
        if (ok) {
            memcpy(re->saved, ms, (size_t)nslot * sizeof(int));
            return re->n_groups + 1;
        }
        for (int k = 0; k < MAX_GROUPS * 2; k++) re->saved[k] = -1;
        return 0;
    }

    if (re->anchor_mode == 2) {
        int s = (start < 0) ? 0 : start;
        if (s != 0) {
            while (s < text_len && text[s - 1] != '\n') s++;
        }
        while (s <= text_len) {
            int ms[MAX_GROUPS * 2];
            re_onepass_debug_execs++;
            int ok = vm_exec_onepass(re, text, text_len, s, ms);
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
        if (re->lit_len > 0) {
            int at = __ls_str_find(text, text_len, re->lit, re->lit_len, s);
            if (at < 0) {
                for (int k = 0; k < MAX_GROUPS * 2; k++) re->saved[k] = -1;
                return 0;
            }
            s = at;
        } else if (re->has_first_bytes) {
            while (s < text_len) {
                unsigned char ch = (unsigned char)text[s];
                if (ch < 128 &&
                    ((re->first_bytes[ch >> 3] >> (ch & 7)) & 1)) break;
                s++;
            }
            if (s >= text_len && text_len > 0) {
                s = text_len;
            }
        }

        int ms[MAX_GROUPS * 2];
        re_onepass_debug_execs++;
        int ok = vm_exec_onepass(re, text, text_len, s, ms);
        if (ok) {
            memcpy(re->saved, ms, (size_t)nslot * sizeof(int));
            return re->n_groups + 1;
        }
    }
    for (int k = 0; k < MAX_GROUPS * 2; k++) re->saved[k] = -1;
    return 0;
}

static int re_exec_vm_general(ReHandle *re, const char *text, int text_len, int start) {
    /* Buffers hoisted out of vm_exec_range and shared across every start
       position tried below. Each is ~71.7 KB, so declaring the pair pays one
       __chkstk stack-probe here per call, instead of once per start
       position in the loop (an anchored pattern that only ever matches at
       position 0, if at all, otherwise tries and fails ~text_len more times
       -- ~50 for this benchmark's ~49-byte corpus strings). Safe to share
       across iterations of this loop because they run strictly
       sequentially: each vm_exec_range call fully finishes (and either
       returns a match we act on immediately, or is done with the buffers)
       before the next one starts. */
    ThreadList tl_a, tl_b;

    const int nslot = re_slot_count(re);

    /* Visited is hoisted the same way tl_a/tl_b are (see above) and shared
       across every vm_exec_range call below within this call -- see the
       comment on the Visited typedef for why that is sound (its generation
       counter never needs re-zeroing except here, at construction). */
    Visited vis;
    visited_init(&vis, re->prog_len);

    /* Tier 0: the pattern is anchored (see re_detect_anchor) -- skip every
       candidate start position that is provably doomed instead of paying
       for a VM call per position that fails on its very first
       instruction. anchor_mode is mutually exclusive with lit_is_whole
       (re_build_prefilter's literal scan starts right after the same
       leading OP_SAVE and stops the instant it sees a non-OP_CHAR
       instruction, so an anchor at prog[1] guarantees lit_len==0), so this
       never shadows the Tier-1 path in __ls_regex_exec. */
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
        re_general_debug_execs++;
        int ok = vm_exec_range(re, text, text_len, 0, 0, re->prog_len - 1, ms, &tl_a, &tl_b, &vis);
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
            re_general_debug_execs++;
            int ok = vm_exec_range(re, text, text_len, s, 0, re->prog_len - 1, ms, &tl_a, &tl_b, &vis);
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
        re_general_debug_execs++;
        int ok = vm_exec_range(re, text, text_len, s, 0, re->prog_len - 1, ms, &tl_a, &tl_b, &vis);
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

/* ===== Lazy DFA executor (D1) =====

   Only ever entered when re->dfa_eligible is 1, i.e. re_dfa_check_eligible
   has already proven this pattern is one-pass, non-multiline, and has no
   mid-pattern BOL/BOS -- see that function's comment for exactly why each
   restriction is required for what follows to be sound.

   THE CORE IDEA: for a one-pass pattern, vm_exec_onepass already proves
   that at any given (pc, input byte) pair, the ENTIRE epsilon-closure
   resolution that follows -- skip every SAVE, follow every JUMP, and at
   every OP_SPLIT pick a branch using the exact same disjoint first-byte
   sets re->onepass_splits already computes -- is a DETERMINISTIC function
   of (pc, byte) alone. It does not depend on how we got to pc, and it does
   not depend on any OTHER live thread (there is only ever one). So instead
   of re-walking that resolution on every single byte of every single exec
   (which is exactly the "51% of dispatched steps are SPLIT+JUMP, consuming
   no byte" cost the A2 attribution measured), it can be computed ONCE per
   distinct (pc, byte) pair and cached: re->dfa_trans[pc*256 + byte].

   A "DFA state" here is therefore just a pc -- specifically, the pc of the
   NEXT consuming instruction (or 0, at the very start) -- not the general
   textbook "set of NFA program counters" a subset-construction DFA needs
   for an arbitrary NFA. That generality is what re_dfa_check_eligible's
   onepass requirement trades away: it is exactly what guarantees the set
   can never have more than one element, so representing it as a bare pc
   loses nothing for the patterns this path is allowed to run on. See the
   ReHandle.dfa_eligible / re_dfa_check_eligible comments for the full
   argument, including why this also means no state-count budget beyond
   RE_DFA_MAX_PROG's plain memory cap is needed (no combinatorial
   explosion is possible by construction). */

/* Byte test for a single consuming instruction -- deliberately mirrors
   vm_exec_onepass's own per-opcode switch (same case-folding/DOTALL/class
   rules) byte for byte, rather than sharing code with it, so a change to
   one cannot silently desync from the other without both being touched
   (the two are verified to agree via the differential oracle, not via
   sharing a code path -- see the D1 report's injection experiment for what
   happens when they disagree).

   NOTE (pre-existing, not introduced or fixed here): OP_CLASS/OP_NCLASS
   below has no ch<128 guard, matching vm_exec_range/vm_exec_onepass's own
   OP_CLASS/OP_NCLASS cases exactly (only re_consuming_first_bytes, used
   purely for the one-pass/DFA-eligibility ANALYSIS, guards it). Adding a
   guard here that the two engines being mirrored do not have would make
   this function DISAGREE with them on ch>=128 against an OP_CLASS pattern,
   which is a correctness bug in the other direction from the one this
   comment is warning about -- so it stays unguarded, bug-compatible on
   purpose, matching the class it mirrors. */
static int re_dfa_instr_matches(const ReHandle *re, int pc, unsigned char ch) {
    const ReInstr *in = &re->prog[pc];
    switch (in->op) {
    case OP_CHAR:
        if (re->flags & LS_RE_IGNORECASE)
            return tolower(ch) == tolower((unsigned char)in->operand_a);
        return ch == (unsigned char)in->operand_a;
    case OP_ANY:
        return (ch != '\n') || (re->flags & LS_RE_DOTALL);
    case OP_CLASS: {
        const ReCharClass *cls = &re->classes[in->operand_a];
        unsigned char lc = (re->flags & LS_RE_IGNORECASE) ? (unsigned char)tolower(ch) : ch;
        return class_test(cls, lc);
    }
    case OP_NCLASS: {
        const ReCharClass *cls = &re->classes[in->operand_a];
        unsigned char lc = (re->flags & LS_RE_IGNORECASE) ? (unsigned char)tolower(ch) : ch;
        return !class_test(cls, lc);
    }
    case OP_DIGIT:  return isdigit(ch) != 0;
    case OP_NDIGIT: return !isdigit(ch);
    case OP_WORD:   return is_word_char(ch) != 0;
    case OP_NWORD:  return !is_word_char(ch);
    case OP_SPACE:  return is_space_char(ch) != 0;
    case OP_NSPACE: return !is_space_char(ch);
    default: return 0; /* unreachable: caller only ever passes a consuming pc */
    }
}

/* Resolve the epsilon closure starting at pc, mirroring vm_exec_onepass's
   own loop (SAVE skip, JUMP follow, SPLIT branch-by-first-byte-set,
   anchors) but STOPPING at the next consuming instruction instead of also
   testing/consuming a byte -- the caller (re_dfa_step) does that part,
   since it is the piece that needs to be cached per (pc,byte).

   have_byte/peek: peek is the byte about to be tested (text[pos], the SAME
   byte re_dfa_step is resolving a transition for) -- have_byte is 0 only
   at true end-of-text (pos==text_len), matching vm_exec_onepass's own
   `pos < text_len` guard before it reads text[pos] to decide a SPLIT.

   Returns RE_DFA_DEAD (no continuation -- this path fails), RE_DFA_ACCEPT
   (OP_MATCH reached via pure epsilon: group 0 ends exactly at the CURRENT
   position, peek is not consumed), or a consuming instruction's pc (>= 0,
   to be tested against peek by the caller). */
static int re_dfa_resolve(const ReHandle *re, int pc, int have_byte, unsigned char peek) {
    const ReSplitInfo *splits = (const ReSplitInfo *)re->onepass_splits;
    for (;;) {
        const ReInstr *in = &re->prog[pc];
        switch (in->op) {
        case OP_SAVE:
            pc++;
            continue;
        case OP_JUMP:
            pc = in->operand_a;
            continue;
        case OP_SPLIT: {
            const ReSplitInfo *info = &splits[pc];
            int chosen = -1;
            if (have_byte) {
                int in_a = (info->a.bytes[peek >> 3] >> (peek & 7)) & 1;
                int in_b = (info->b.bytes[peek >> 3] >> (peek & 7)) & 1;
                /* re_is_onepass's rule 2 guarantees these are never both
                   true (see vm_exec_onepass's identical comment). */
                if (in_a)      chosen = in->operand_a;
                else if (in_b) chosen = in->operand_b;
            }
            if (chosen < 0) {
                if (info->a.can_match_empty)      chosen = in->operand_a;
                else if (info->b.can_match_empty)  chosen = in->operand_b;
                else return RE_DFA_DEAD;
            }
            pc = chosen;
            continue;
        }
        case OP_ANCHOR_EOL:
            /* Non-multiline (re_dfa_check_eligible rejects MULTILINE
               outright): $ passes only at true end-of-text. Whether we are
               at end-of-text is exactly !have_byte -- known from the SAME
               byte-availability fact re_dfa_step already has, no separate
               position tracking needed. */
            if (!have_byte) { pc++; continue; }
            return RE_DFA_DEAD;
        case OP_ANCHOR_EOS:
            if (!have_byte) { pc++; continue; }
            return RE_DFA_DEAD;
        case OP_ANCHOR_BOL:
        case OP_ANCHOR_BOS:
            /* re_dfa_check_eligible guarantees a BOL/BOS anywhere in the
               program appears ONLY at pc==1 (any other pc gets the whole
               pattern rejected as DFA-ineligible). pc==1 is the
               instruction immediately after pc==0's unconditional group-0
               SAVE, and nothing in a one-pass program's JUMP/SPLIT graph
               ever targets pc==1 from anywhere else (it sits before any
               user-pattern content a loop body could wrap back around to
               -- a pattern that DID loop back over its leading anchor,
               e.g. "(^a)*", puts the anchor at some pc > 1 inside the
               loop body, which the eligibility scan already rejects). So
               the ONLY DFA state whose resolution ever reaches pc==1 is
               state 0, the unique initial state of every walk -- and
               re_exec_vm_dfa only ever starts a walk at state 0 with
               pos==0 for such a pattern (anchor_mode==1's branch, forced
               unconditionally by re_detect_anchor whenever prog[1] is
               ANCHOR_BOL/ANCHOR_BOS -- see that function). Reaching this
               case therefore means "current position is 0" is ALREADY
               established by the caller, not something this function
               needs to re-derive from a peek byte the way ANCHOR_EOL/EOS
               above do from have_byte -- so it always passes. */
            pc++;
            continue;
        case OP_MATCH:
            return RE_DFA_ACCEPT;
        case OP_WORDBND:
        case OP_NWORDBND:
        case OP_LOOKAHEAD:
            /* Unreachable: re_is_onepass rejects any pattern containing
               these globally, and dfa_eligible requires onepass==1.
               Defensive only. */
            return RE_DFA_DEAD;
        default:
            return pc; /* consuming instruction: stop here, caller tests it */
        }
    }
}

/* One cached (state,byte) transition, computing and filling the cell on
   first use. state is a pc (see the file comment above). Returns
   RE_DFA_DEAD, RE_DFA_ACCEPT, or a new state pc (>= 0). */
static int re_dfa_step(ReHandle *re, int state, const char *text, int text_len, int pos) {
    if (pos >= text_len) {
        /* End of text: no byte to cache against (and nothing to cache --
           this is evaluated at most once per exec, never once per byte, so
           there is no repeated-work cost to amortize here the way there is
           for the have_byte==1 case below). */
        int r = re_dfa_resolve(re, state, 0, 0);
        return (r == RE_DFA_ACCEPT) ? RE_DFA_ACCEPT : RE_DFA_DEAD;
    }

    unsigned char byte = (unsigned char)text[pos];
    short *cell = &re->dfa_trans[(size_t)state * 256 + byte];
    if (*cell != RE_DFA_UNFILLED) return *cell;

    int cpc = re_dfa_resolve(re, state, 1, byte);
    int result;
    if (cpc == RE_DFA_DEAD)          result = RE_DFA_DEAD;
    else if (cpc == RE_DFA_ACCEPT)   result = RE_DFA_ACCEPT;
    else if (!re_dfa_instr_matches(re, cpc, byte)) result = RE_DFA_DEAD;
    else                              result = cpc + 1;

    *cell = (short)result;
    return result;
}

/* Walk the DFA from (start, pc=0) to either a match (group 0 = [start,pos))
   or failure. pc=0 is always OP_SAVE(0) (group-0 open, emitted
   unconditionally as the very first instruction by __ls_regex_compile), so
   the very first re_dfa_step's resolve step just skips over it like any
   other state -- no special-casing needed for the initial call.

   Terminates in at most (text_len - start + 1) steps: DEAD/ACCEPT return
   immediately, and the only way to loop again is the `state = r` branch,
   which is only reached after re_dfa_resolve found a REAL consuming
   instruction whose byte test passed -- i.e. pos strictly increases on
   every iteration that does not terminate. Same O(n) bound as
   vm_exec_onepass, by the same argument. */
static int vm_exec_dfa(ReHandle *re, const char *text, int text_len, int start,
                       int *out_start, int *out_end) {
    int state = 0;
    int pos = start;
    for (;;) {
        int r = re_dfa_step(re, state, text, text_len, pos);
        if (r == RE_DFA_DEAD) return 0;
        if (r == RE_DFA_ACCEPT) { *out_start = start; *out_end = pos; return 1; }
        state = r;
        pos++;
    }
}

static long long re_dfa_debug_execs;  /* see __ls_regex_debug_dfa_execs */

/* Tier 0/2/3 candidate-position selection mirrors re_exec_vm_onepass
   exactly (see that function's comments) -- anchor_mode==2 (multiline
   line-start) never occurs here because re_dfa_check_eligible already
   rejects LS_RE_MULTILINE outright. */
static int re_exec_vm_dfa(ReHandle *re, const char *text, int text_len, int start) {
    if (re->anchor_mode == 1) {
        if (start > 0) {
            for (int k = 0; k < MAX_GROUPS * 2; k++) re->saved[k] = -1;
            return 0;
        }
        int s0, e0;
        re_dfa_debug_execs++;
        if (vm_exec_dfa(re, text, text_len, 0, &s0, &e0)) {
            for (int k = 2; k < MAX_GROUPS * 2; k++) re->saved[k] = -1;
            re->saved[0] = s0;
            re->saved[1] = e0;
            return re->n_groups + 1;
        }
        for (int k = 0; k < MAX_GROUPS * 2; k++) re->saved[k] = -1;
        return 0;
    }

    for (int s = start; s <= text_len; s++) {
        if (re->lit_len > 0) {
            int at = __ls_str_find(text, text_len, re->lit, re->lit_len, s);
            if (at < 0) {
                for (int k = 0; k < MAX_GROUPS * 2; k++) re->saved[k] = -1;
                return 0;
            }
            s = at;
        } else if (re->has_first_bytes) {
            while (s < text_len) {
                unsigned char ch = (unsigned char)text[s];
                if (ch < 128 &&
                    ((re->first_bytes[ch >> 3] >> (ch & 7)) & 1)) break;
                s++;
            }
            if (s >= text_len && text_len > 0) {
                s = text_len;
            }
        }

        int s0, e0;
        re_dfa_debug_execs++;
        if (vm_exec_dfa(re, text, text_len, s, &s0, &e0)) {
            for (int k = 2; k < MAX_GROUPS * 2; k++) re->saved[k] = -1;
            re->saved[0] = s0;
            re->saved[1] = e0;
            return re->n_groups + 1;
        }
    }
    for (int k = 0; k < MAX_GROUPS * 2; k++) re->saved[k] = -1;
    return 0;
}

/* On-demand fallback for a group>0 read after an exec that ran through the
   DFA-only fast path (__ls_regex_exec_dfa, below). See the
   ReHandle.dfa_only_valid comment for the full contract: this re-runs the
   one-pass engine (always sound here -- dfa_eligible implies onepass==1,
   so vm_exec_onepass is the correct, already-fully-verified engine, never
   the general Pike VM) at the EXACT (text, text_len, start) the DFA-only
   exec was given, deterministically recovering the same match and every
   sub-group offset the DFA never computed. A no-op whenever dfa_only_valid
   is already 0 -- either this handle's last exec populated full captures
   directly, or a previous call to this function already did the fallback
   (it clears the flag immediately, before doing the re-exec, precisely so
   it runs at most once per exec no matter how many group()/cap_start
   calls follow). */
static void re_dfa_fill_captures(ReHandle *re) {
    if (!re->dfa_only_valid) return;
    re->dfa_only_valid = 0;
    (void)re_exec_vm_onepass(re, re->dfa_only_text, re->dfa_only_text_len, re->dfa_only_start);
}

/* Match-only-safe drop-in for __ls_regex_exec: identical return value and
   re->saved[0]/re->saved[1] (group-0 span) contract on every call, for
   every pattern -- including ones the DFA is not eligible for, via the
   fallback below -- but skips sub-group capture work when the fast path is
   taken, recovering it transparently and lazily (re_dfa_fill_captures,
   triggered from __ls_regex_cap_start/_len) if and only if a caller later
   asks for group() with index > 0.

   SAFE for every call site (correctness never depends on which ones use
   it), but only CHEAP for call sites that read group 0 or nothing at all
   -- see the ReHandle.dfa_only_valid comment for why a call site that
   reads several sub-groups on every call should keep using
   __ls_regex_exec directly instead. regex.lls's routing choices, in that
   light: find/find_all/replace/replace_all/split (only ever read group 0
   internally) and matches()/full_match() (read nothing) route through
   this unconditionally; Regex.is_match() is an opt-in narrower-contract
   sibling of matches?() for callers who know they mostly want the yes/no
   (or group 0) answer; Regex.matches?()/find_from() and capture()/
   capture_all()/capture_named()/capture_all_spans()/capture_all_slices()
   (which read several groups on every successful match) deliberately keep
   calling __ls_regex_exec instead. */
int __ls_regex_exec_dfa(void *h, const char *text, int text_len, int start) {
    ReHandle *re = (ReHandle *)h;
    if (!re) return 0;

    /* Record which text this exec ran against, before any dispatch below can
       return early -- see the last_text comment on ReHandle. */
    re->last_text     = text;
    re->last_text_len = text_len;

    if (re->lit_is_whole && !(re->flags & LS_RE_IGNORECASE)) {
        /* Tier 1 is engine-agnostic (no SPLIT, no onepass/DFA/general
           distinction applies to it at all) -- delegate rather than
           duplicate. __ls_regex_exec clears dfa_only_valid itself. */
        return __ls_regex_exec(h, text, text_len, start);
    }

    if (!re->dfa_eligible) {
        re->dfa_only_valid = 0;
        return __ls_regex_exec(h, text, text_len, start);
    }

    int n = re_exec_vm_dfa(re, text, text_len, start);
    if (n > 0) {
        re->dfa_only_valid    = 1;
        re->dfa_only_text     = text;
        re->dfa_only_text_len = text_len;
        re->dfa_only_start    = start;
    } else {
        re->dfa_only_valid = 0;
    }
    return n;
}

int __ls_regex_exec(void *h, const char *text, int text_len, int start) {
    ReHandle *re = (ReHandle *)h;
    if (!re) return 0;

    /* Record which text this exec ran against, before any dispatch below can
       return early -- see the last_text comment on ReHandle. */
    re->last_text     = text;
    re->last_text_len = text_len;

    /* Defensive: this handle's captures are about to be fully repopulated
       by one of the two engines below (or the tier-1 literal path just
       past it), so any stale "last exec was DFA-only" bookkeeping from a
       PREVIOUS call must not survive -- otherwise a later
       __ls_regex_cap_start(group>0) could trigger a needless (though not
       wrong -- re_dfa_fill_captures is idempotent and re-derives the same
       answer either way) re-exec. No current call site mixes
       __ls_regex_exec_dfa and __ls_regex_exec on the same handle (see the
       D1 report), but this keeps that combination safe by construction
       rather than by convention. */
    re->dfa_only_valid = 0;

    /* Tier 1: the pattern is nothing but a literal -- answer with the shared
       BMH/Sunday search and skip BOTH engines entirely. Checked here, once,
       ahead of the onepass/general split: a whole-literal pattern is
       trivially one-pass too (no SPLIT at all), but there is no reason to
       route it through vm_exec_onepass's dispatch loop when this direct
       answer is cheaper still. */
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

    if (re->onepass) return re_exec_vm_onepass(re, text, text_len, start);
    return re_exec_vm_general(re, text, text_len, start);
}

/* 1 if (text, text_len) is the exact text the last exec on this handle ran
   against, 0 otherwise (including a handle that has never been exec'd). The
   byte-producing accessors in regex.lls gate on this so a wrong text yields
   None instead of a plausible-looking wrong substring; see the last_text
   comment on ReHandle for what this can and cannot detect. Pointer identity
   plus length, not a content compare: this must stay O(1), and a content
   compare would additionally accept an unrelated text that merely looks the
   same, which is not the question being asked. */
int __ls_regex_text_is(void *h, const char *text, int text_len) {
    ReHandle *re = (ReHandle *)h;
    if (!re || re->last_text == NULL) return 0;
    return (re->last_text == text && re->last_text_len == text_len) ? 1 : 0;
}

int __ls_regex_cap_start(void *h, int group) {
    ReHandle *re = (ReHandle *)h;
    if (!re || group < 0 || group >= MAX_GROUPS) return -1;
    /* Group 0 is always real (the DFA-only path computes its exact span);
       only group>0 can be missing, and only after a DFA-only exec -- see
       re_dfa_fill_captures. */
    if (group > 0) re_dfa_fill_captures(re);
    return re->saved[group * 2];
}

int __ls_regex_cap_len(void *h, int group) {
    ReHandle *re = (ReHandle *)h;
    if (!re || group < 0 || group >= MAX_GROUPS) return -1;
    if (group > 0) re_dfa_fill_captures(re);
    int s = re->saved[group * 2];
    int e = re->saved[group * 2 + 1];
    if (s < 0 || e < 0) return -1;
    return e - s;
}

int __ls_regex_group_count(void *h) {
    ReHandle *re = (ReHandle *)h;
    return re ? re->n_groups : 0;
}

/* Diagnostic query surface for re_is_onepass (see the comment there).
   __ls_regex_exec routes to vm_exec_onepass exactly when this returns
   nonzero (see re_exec_vm_onepass/re_exec_vm_general in __ls_regex_exec's
   dispatcher). */
int __ls_regex_is_onepass(void *h) {
    ReHandle *re = (ReHandle *)h;
    return re ? re->onepass : 0;
}

/* Process-wide (not thread-local, unlike the pattern cache -- these are
   diagnostic counters, not correctness-sensitive, and the probes that read
   them run single-threaded) counters of how many times each engine's
   per-start-position exec function actually ran, incremented in
   re_exec_vm_onepass/re_exec_vm_general. Exists purely to let a probe PROVE
   the one-pass path is taken for a given pattern, rather than trusting that
   is_onepass()==true implies vm_exec_onepass ran -- see the Task 2 report
   (.superpowers/sdd/op2-report.md) for how this was used to confirm the
   benchmark shapes route through the new engine. */
long long __ls_regex_debug_onepass_execs(void) { return re_onepass_debug_execs; }
long long __ls_regex_debug_general_execs(void) { return re_general_debug_execs; }
/* Same counter contract as the two above (incremented once per candidate
   start position tried, i.e. once per vm_exec_dfa call), for the D1 lazy
   DFA path -- see re_exec_vm_dfa. */
long long __ls_regex_debug_dfa_execs(void) { return re_dfa_debug_execs; }

/* Diagnostic query surface for re_dfa_check_eligible (see the comment
   there). __ls_regex_exec_dfa routes to the DFA walk exactly when this
   returns nonzero (whole-literal patterns aside -- those always use the
   engine-agnostic tier-1 path regardless of this flag). */
int __ls_regex_is_dfa_eligible(void *h) {
    ReHandle *re = (ReHandle *)h;
    return re ? re->dfa_eligible : 0;
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
