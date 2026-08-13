/* checker.c — Type checker: walks AST, validates types, fills resolved_type */
#include "checker.h"
#include "checker_internal.h"
#include "builtins_math.h"
#include "mangle.h"
#include "diag.h"
#include "module.h"
#include "builtins_perf.h"
#include <stdio.h>
#include <string.h>
#include <stdarg.h>
#include <stdlib.h>
#include <math.h>

/* File-local helpers (single-TU; re-static'd at checker split end). */
static void check_pass(Checker *c, AstNode *program);
void checker_mark_ambiguous_type(Checker *c, const char *name);
static void checker_propagate_has_drop_fixpoint(Checker *c);
void checker_warning(Checker *c, int line, int col, const char *fmt, ...);
static bool is_self_placeholder(const Type *t);
static bool path_is_under_stdlib(const char *path);
static void register_builtins(Checker *c);

/* ---- Stdlib gate ----
   Internal builtins (named with `__` prefix by convention) are only callable
   from files physically located under a `std/` or `stdlib/` directory — i.e.
   <LS_HOME>/std/ or <LS_HOME>/stdlib/. Detected by looking for a "/std/" or
   "/stdlib/" segment in the source path. Imperfect (a user could name their
   own directory "std"), but good enough to keep these footguns out of normal
   user code while staying allocator-policy-free. */
static bool path_is_under_stdlib(const char *path)
{
    if (path == NULL) return false;
    for (const char *p = path; *p; p++) {
        if ((p[0] == '/' || p[0] == '\\') &&
            p[1] == 's' && p[2] == 't' && p[3] == 'd')
        {
            /* /stdlib/ */
            if (p[4] == 'l' && p[5] == 'i' && p[6] == 'b' &&
                (p[7] == '/' || p[7] == '\\'))
                return true;
            /* /std/ (pure-LS stdlib modules like std/time.ls, std/proc.ls) */
            if (p[4] == '/' || p[4] == '\\')
                return true;
        }
    }
    return false;
}

/* ---- Error reporting ---- */

/* True if (file, line, col, msg) was already emitted; records it otherwise.
   See Checker::diag_seen for why the filtering lives here and not in the
   two-pass loop analysis that produces the duplicates. On allocation failure the
   entry is simply not recorded — that risks a duplicate line, never a lost
   diagnostic. */
static bool checker_diag_is_dup(Checker *c, int line, int col, const char *msg)
{
    const char *file = c->source_path ? c->source_path : "";
    for (int i = 0; i < c->diag_seen_count; i++)
    {
        CheckerDiagSeen *s = &c->diag_seen[i];
        if (s->line == line && s->col == col &&
            strcmp(s->msg, msg) == 0 && strcmp(s->file, file) == 0)
            return true;
    }
    if (c->diag_seen_count >= CHECKER_MAX_ERRORS)
        return false;
    char *fcopy = strdup(file);
    char *mcopy = strdup(msg);
    if (fcopy == NULL || mcopy == NULL) { free(fcopy); free(mcopy); return false; }
    CheckerDiagSeen *slot = &c->diag_seen[c->diag_seen_count++];
    slot->file = fcopy;
    slot->line = line;
    slot->col  = col;
    slot->msg  = mcopy;
    return false;
}

void checker_error(Checker *c, int line, int col, const char *fmt, ...)
{
    /* Suppressed during transitive trait re-registration: the trait was already
       validated in its home module, so a resolution failure here is a spurious
       cross-scope artifact, not a user error. (Mirrors silent_move_errors.) */
    if (c->silent_type_errors)
        return;
    if (c->error_count >= CHECKER_MAX_ERRORS)
        return;

    /* Same size as Diagnostic::message, so the text compared here is exactly
       the text the user sees -- a longer buffer would compare bytes that get
       truncated on the way out. */
    char buf[512];
    va_list args;
    va_start(args, fmt);
    vsnprintf(buf, sizeof buf, fmt, args);
    va_end(args);

    /* Checked before had_error/error_count so a re-reported error neither
       double-counts against CHECKER_MAX_ERRORS nor prints twice. */
    if (checker_diag_is_dup(c, line, col, buf))
        return;

    c->had_error = true;
    c->error_count++;

    diag_emitf(DIAG_TYPE_ERROR, c->source_path, line, col, 1, NULL, "%s", buf);
}

void checker_warning(Checker *c, int line, int col, const char *fmt, ...)
{
    va_list args;
    va_start(args, fmt);
    diag_vemitf(DIAG_WARNING, c->source_path, line, col, 1, NULL, fmt, args);
    va_end(args);
}

/* checker_error variant carrying a squiggle length and a help suggestion
   (C2-2 did-you-mean). help may be NULL. */
void checker_error_help(Checker *c, int line, int col, int len,
                               const char *help, const char *fmt, ...)
{
    if (c->silent_type_errors)
        return;
    if (c->error_count >= CHECKER_MAX_ERRORS)
        return;

    /* Same size as Diagnostic::message, so the text compared here is exactly
       the text the user sees -- a longer buffer would compare bytes that get
       truncated on the way out. */
    char buf[512];
    va_list args;
    va_start(args, fmt);
    vsnprintf(buf, sizeof buf, fmt, args);
    va_end(args);

    /* Same de-duplication as checker_error; `help` is not part of the identity
       because it is derived from the same position and message. */
    if (checker_diag_is_dup(c, line, col, buf))
        return;

    c->had_error = true;
    c->error_count++;

    diag_emitf(DIAG_TYPE_ERROR, c->source_path, line, col, len, help, "%s", buf);
}

/* ---- did-you-mean candidate iterators (C2-2) ----
   Pull-based iterators fed to diag_suggest. Each covers one high-frequency
   error's candidate namespace (plan_diagnostics_v2.md §3.3). */

/* Undefined variable/function: every symbol on the scope chain. */

const char *diag_scope_iter_next(void *ctx)
{
    DiagScopeIter *it = (DiagScopeIter *)ctx;
    while (it->sc) {
        if (it->i < it->sc->count)
            return it->sc->symbols[it->i++].name;
        it->sc = it->sc->parent;
        it->i = 0;
    }
    return NULL;
}

/* Unknown type: struct registry, enum registry, type aliases, generic struct
   templates, and the builtin type keywords. */
static const char *k_diag_builtin_types[] = {
    "int", "i8", "i16", "i32", "i64", "u8", "u16", "u32", "u64",
    "f16", "bf16", "f32", "f64", "bool", "char", "object", "void",
    "array", "Block", "Simd",
};


const char *diag_type_iter_next(void *ctx)
{
    DiagTypeIter *it = (DiagTypeIter *)ctx;
    Checker *c = it->c;
    for (;;) {
        switch (it->stage) {
        case 0:
            if (it->i < c->struct_type_count)
                return c->struct_types[it->i++].name;
            break;
        case 1:
            if (it->i < c->enum_type_count)
                return c->enum_types[it->i++].name;
            break;
        case 2:
            if (it->i < c->type_alias_count)
                return c->type_aliases[it->i++].name;
            break;
        case 3:
            if (it->i < c->struct_template_count)
                return c->struct_templates[it->i++].base_name;
            break;
        case 4:
            if (it->i < (int)(sizeof(k_diag_builtin_types) /
                              sizeof(k_diag_builtin_types[0])))
                return k_diag_builtin_types[it->i++];
            break;
        default:
            return NULL;
        }
        it->stage++;
        it->i = 0;
    }
}

/* Unknown method (or struct field): the receiver's field names followed by
   its impl-registry method table. Internal methods (__drop/__clone, $op_*)
   are not suggestions. strukt may be NULL (method-call path: methods only). */

const char *diag_method_iter_next(void *ctx)
{
    DiagMethodIter *it = (DiagMethodIter *)ctx;
    Checker *c = it->c;
    if (it->strukt && it->strukt->kind == TYPE_STRUCT &&
        it->fi < it->strukt->as.strukt.field_count)
        return it->strukt->as.strukt.fields[it->fi++].name;
    if (!it->impl_found) {
        if (it->impl_key == NULL)
            return NULL;
        it->ii = find_impl_idx(c, it->impl_key);
        if (it->ii < 0)
            return NULL;
        it->impl_found = true;
    }
    while (it->mi < c->impl_registry[it->ii].method_count) {
        const char *m = c->impl_registry[it->ii].methods[it->mi++].name;
        if (m[0] == '$' || (m[0] == '_' && m[1] == '_'))
            continue; /* __drop/__clone/$op_* are not user-callable */
        return m;
    }
    return NULL;
}

/* Build "did you mean 'X'?" into buf; returns buf or NULL when no unique
   near-miss candidate exists. */
const char *diag_help_suggestion(char *buf, size_t bufsz,
                                        const char *bad,
                                        DiagCandidateFn next, void *ctx)
{
    const char *sugg = diag_suggest(bad, next, ctx);
    if (sugg == NULL)
        return NULL;
    snprintf(buf, bufsz, "did you mean '%s'?", sugg);
    return buf;
}

/* Move-semantics error — separate from type errors so the user can distinguish them */
void checker_move_error(Checker *c, int line, int col, const char *fmt, ...)
{
    /* Phase B: during the discovery pass of a loop we silently collect move state
       without reporting errors. The reporting pass re-runs with the flag off. */
    if (c->silent_move_errors)
        return;
    if (c->error_count >= CHECKER_MAX_ERRORS)
        return;
    c->had_error = true;
    c->error_count++;
    va_list args;
    va_start(args, fmt);
    diag_vemitf(DIAG_MOVE_ERROR, c->source_path, line, col, 1, NULL, fmt, args);
    va_end(args);
}

/* ---- Struct type registry ---- */

/* ---- Self placeholder for trait signatures ----
   Used as current_impl_struct_type during check_trait_decl so that
   resolve_type_node("Self") returns this sentinel instead of NULL.
   check_impl_trait_decl then substitutes it with the real struct type. */
/* Shared across checker TUs (extern in checker_internal.h) — used by the
   operator-overload lowering in checker_lower.c as well as trait checking. */
Type g_self_placeholder_type = {
    .kind = TYPE_STRUCT,
    .as = { .strukt = { .name = "Self", .fields = NULL, .field_count = 0, .has_drop = false } }
};

static bool is_self_placeholder(const Type *t) {
    return t == &g_self_placeholder_type;
}

/* type_equals variant that treats g_self_placeholder_type as equal to `concrete`.
   Handles TYPE_REFERENCE wrapping (e.g. &Self == &Vec2). */
/* F6b: the name a concrete type arg contributes to a generic instance key
   ("Vec(...)", "Option(...)") — moved to mangle_type_arg_name (src/mangle.c,
   Task 2.2) so the struct/enum instantiation paths (checker.c) and this
   textual Self substitution share one implementation instead of a
   checker-local static copy; see mangle.h's header comment for the full
   rationale (module llvm_name preferred, collision history, and why it must
   stay in lockstep with impl_key_of_type without being merged into it). */

bool type_equals_with_self(const Type *trait_t, const Type *impl_t, const Type *concrete)
{
    if (trait_t == NULL || impl_t == NULL) return trait_t == impl_t;
    if (is_self_placeholder(trait_t)) return type_equals(concrete, impl_t);
    if (trait_t->kind == TYPE_REFERENCE && impl_t->kind == TYPE_REFERENCE) {
        if (trait_t->is_mut != impl_t->is_mut) return false;
        return type_equals_with_self(trait_t->as.pointer_to, impl_t->as.pointer_to, concrete);
    }
    if (trait_t->kind == TYPE_POINTER && impl_t->kind == TYPE_POINTER) {
        return type_equals_with_self(trait_t->as.pointer_to, impl_t->as.pointer_to, concrete);
    }
    /* `Self` nested inside a generic struct/enum return type — e.g. a trait method
       `-> Result(Self, Str)` validated against an impl `-> Result(int, Str)`. Generic
       enum/struct instances are identified by their mangled name (enums carry no
       structured arg list), so substitute the concrete type's name for a whole-word
       `Self` in the trait name and compare. (Top-level `-> Self` is handled above.) */
    if (concrete != NULL &&
        ((trait_t->kind == TYPE_ENUM   && impl_t->kind == TYPE_ENUM) ||
         (trait_t->kind == TYPE_STRUCT && impl_t->kind == TYPE_STRUCT))) {
        char tnbuf[512], inbuf[512];
        snprintf(tnbuf, sizeof tnbuf, "%s", type_name(trait_t));
        snprintf(inbuf, sizeof inbuf, "%s", type_name(impl_t));
        if (strcmp(tnbuf, inbuf) == 0) return true;
        if (strstr(tnbuf, "Self") != NULL) {
            /* Render the concrete type the same way instance keys are built
               (module llvm_name preferred) — the impl side's instance name
               embeds it, so a bare-name substitution would falsely mismatch
               for module-defined Self types. */
            char cnbuf[256];
            snprintf(cnbuf, sizeof cnbuf, "%s", mangle_type_arg_name(concrete));
            char outbuf[512]; int op = 0; bool fits = true;
            int cl = (int)strlen(cnbuf);
            #define LS_IDENT_CH(ch) (((ch) >= 'A' && (ch) <= 'Z') || \
                ((ch) >= 'a' && (ch) <= 'z') || ((ch) >= '0' && (ch) <= '9') || (ch) == '_')
            for (const char *q = tnbuf; *q != '\0' && op < 500; ) {
                char prev = (q == tnbuf) ? '\0' : q[-1];
                if (strncmp(q, "Self", 4) == 0 &&
                    !LS_IDENT_CH(prev) && !LS_IDENT_CH(q[4])) {
                    if (op + cl >= 500) { fits = false; break; }
                    memcpy(outbuf + op, cnbuf, (size_t)cl); op += cl; q += 4;
                } else {
                    outbuf[op++] = *q++;
                }
            }
            #undef LS_IDENT_CH
            outbuf[op] = '\0';
            if (fits && strcmp(outbuf, inbuf) == 0) return true;
        }
    }
    return type_equals(trait_t, impl_t);
}

/* B-2: Compute the LLVM-level type name for a struct/enum defined in a module.
   Returns a malloc'd "<mod>__Name" string when c->module_name is non-NULL, else NULL.
   The caller is responsible for nothing — the returned pointer is stored in
   Type.strukt.llvm_name / Type.enom.llvm_name and intentionally leaked with the Type. */
char *checker_module_type_llvmname(Checker *c, const char *bare_name)
{
    if (c->module_name == NULL || c->module_name[0] == '\0')
        return NULL;
    /* mangle_module_symbol's NULL/empty-module case returns a strdup of
       bare_name, not NULL — that path is intentionally not taken here
       (guarded above) because callers store this in Type.strukt/enom.llvm_name
       and rely on NULL meaning "no module prefix, use the bare name". */
    return mangle_module_symbol(c->module_name, bare_name);
}

/* B-4: mark a bare type name as ambiguous (exported by 2+ imported modules). */
void checker_mark_ambiguous_type(Checker *c, const char *name)
{
    for (int i = 0; i < c->ambiguous_type_count; i++)
        if (strcmp(c->ambiguous_types[i], name) == 0) return; /* already marked */
    if (c->ambiguous_type_count >= c->ambiguous_type_cap)
    {
        c->ambiguous_type_cap = c->ambiguous_type_cap < 8 ? 8 : c->ambiguous_type_cap * 2;
        c->ambiguous_types = realloc_safe(c->ambiguous_types,
            (size_t)c->ambiguous_type_cap * sizeof(c->ambiguous_types[0]));
    }
    c->ambiguous_types[c->ambiguous_type_count++] = name;
}

/* B-4: true if a bare type name is ambiguous across imported modules. */
bool checker_type_is_ambiguous(Checker *c, const char *name)
{
    for (int i = 0; i < c->ambiguous_type_count; i++)
        if (strcmp(c->ambiguous_types[i], name) == 0) return true;
    return false;
}

/* --- C1: type-registry hash index (open addressing, linear probe) --------- */
/* FxHash over a NUL-terminated key. Mirrors runtime __ls_fxhash_bytes so the
   mix is well-understood; the checker never shares buckets with the runtime so
   only the distribution matters here, not bit-identical values. */
static unsigned long long type_name_hash(const char *s)
{
    unsigned long long h = 0;
    const unsigned long long SEED = 0x517cc1b727220a95ULL;
    for (; *s; s++) {
        unsigned long long x = h ^ (unsigned long long)(unsigned char)*s;
        h = ((x << 5) | (x >> 59)) * SEED;
    }
    return h;
}

/* LS_NO_TYPETAB=1 → skip the hash index, fall back to the linear array scan
   (kept for one release as a safety valve; see plan §5 rollback row). */
bool type_tab_disabled(void)
{
    static int cached = -1;
    if (cached < 0) cached = getenv("LS_NO_TYPETAB") ? 1 : 0;
    return cached != 0;
}

/* Rehash into a table twice the size (or 16 when empty). Never called with
   LS_NO_TYPETAB set — insert short-circuits before reaching here. */
static void type_tab_grow(TypeTabEntry **tab, int *cap)
{
    int oldcap = *cap;
    TypeTabEntry *old = *tab;
    int newcap = oldcap < 16 ? 16 : oldcap * 2;
    TypeTabEntry *nt = malloc_safe((size_t)newcap * sizeof(TypeTabEntry));
    memset(nt, 0, (size_t)newcap * sizeof(TypeTabEntry));
    unsigned long long mask = (unsigned long long)newcap - 1;
    for (int i = 0; i < oldcap; i++) {
        if (old[i].name == NULL) continue;
        unsigned long long j = type_name_hash(old[i].name) & mask;
        while (nt[j].name != NULL) j = (j + 1) & mask;
        nt[j] = old[i];
    }
    free(old);
    *tab = nt;
    *cap = newcap;
}

/* Insert name->type. Keeps the FIRST entry on a duplicate name (matching the
   old linear find, which returned the earliest registration). No deletion ever
   happens on these tables, so open addressing needs no tombstones. */
void type_tab_insert(TypeTabEntry **tab, int *cap, int *count,
                            const char *name, Type *type)
{
    if (type_tab_disabled()) return;
    if (*cap == 0 || (*count + 1) * 10 >= *cap * 7)   /* grow past 70% load */
        type_tab_grow(tab, cap);
    unsigned long long mask = (unsigned long long)*cap - 1;
    unsigned long long i = type_name_hash(name) & mask;
    for (;;) {
        if ((*tab)[i].name == NULL) {
            (*tab)[i].name = name;
            (*tab)[i].type = type;
            (*count)++;
            return;
        }
        if (strcmp((*tab)[i].name, name) == 0) return;  /* keep first */
        i = (i + 1) & mask;
    }
}

Type *type_tab_find(TypeTabEntry *tab, int cap, const char *name)
{
    if (cap == 0) return NULL;
    unsigned long long mask = (unsigned long long)cap - 1;
    unsigned long long i = type_name_hash(name) & mask;
    for (;;) {
        if (tab[i].name == NULL) return NULL;
        if (strcmp(tab[i].name, name) == 0) return tab[i].type;
        i = (i + 1) & mask;
    }
}

/* --- S1: impl_registry hash index (struct_name -> impl_idx) --------------- */
/* Mirrors the type-tab scheme above. Only the outer "locate the impl slot for
   this receiver" step is hashed; the per-impl method scan (and all origin_iface
   disambiguation) stays linear and byte-identical to the legacy path. */

/* LS_NO_IMPLTAB=1 → skip the hash index, fall back to the linear array scan
   (safety valve, mirrors LS_NO_TYPETAB). */
static bool impl_tab_disabled(void)
{
    static int cached = -1;
    if (cached < 0) cached = getenv("LS_NO_IMPLTAB") ? 1 : 0;
    return cached != 0;
}

static void impl_tab_grow(ImplTabEntry **tab, int *cap)
{
    int oldcap = *cap;
    ImplTabEntry *old = *tab;
    int newcap = oldcap < 16 ? 16 : oldcap * 2;
    ImplTabEntry *nt = malloc_safe((size_t)newcap * sizeof(ImplTabEntry));
    memset(nt, 0, (size_t)newcap * sizeof(ImplTabEntry));
    unsigned long long mask = (unsigned long long)newcap - 1;
    for (int i = 0; i < oldcap; i++) {
        if (old[i].name == NULL) continue;
        unsigned long long j = type_name_hash(old[i].name) & mask;
        while (nt[j].name != NULL) j = (j + 1) & mask;
        nt[j] = old[i];
    }
    free(old);
    *tab = nt;
    *cap = newcap;
}

/* Insert struct_name->idx. Keeps the FIRST entry on a duplicate name (matching
   find_or_create_impl, which reuses the earliest slot for a repeated name). */
void impl_tab_insert(Checker *c, const char *name, int idx)
{
    if (impl_tab_disabled()) return;
    if (c->impl_tab_cap == 0 || (c->impl_tab_count + 1) * 10 >= c->impl_tab_cap * 7)
        impl_tab_grow(&c->impl_tab, &c->impl_tab_cap);
    unsigned long long mask = (unsigned long long)c->impl_tab_cap - 1;
    unsigned long long i = type_name_hash(name) & mask;
    for (;;) {
        if (c->impl_tab[i].name == NULL) {
            c->impl_tab[i].name = name;
            c->impl_tab[i].idx = idx;
            c->impl_tab_count++;
            return;
        }
        if (strcmp(c->impl_tab[i].name, name) == 0) return;  /* keep first */
        i = (i + 1) & mask;
    }
}

/* Linear scan for the impl slot keyed by struct_name; -1 if absent. This is the
   legacy behaviour, reused by the LS_NO_IMPLTAB fallback. */
static int impl_idx_linear(Checker *c, const char *struct_name)
{
    for (int i = 0; i < c->impl_count; i++)
        if (strcmp(c->impl_registry[i].struct_name, struct_name) == 0)
            return i;
    return -1;
}

/* Locate the impl_registry index for a receiver's struct_name, or -1. Hashed by
   default; linear under LS_NO_IMPLTAB. */
int find_impl_idx(Checker *c, const char *struct_name)
{
    if (impl_tab_disabled())
        return impl_idx_linear(c, struct_name);

    int found = -1;
    if (c->impl_tab_cap != 0) {
        unsigned long long mask = (unsigned long long)c->impl_tab_cap - 1;
        unsigned long long i = type_name_hash(struct_name) & mask;
        for (;;) {
            if (c->impl_tab[i].name == NULL) break;
            if (strcmp(c->impl_tab[i].name, struct_name) == 0) {
                found = c->impl_tab[i].idx;
                break;
            }
            i = (i + 1) & mask;
        }
    }

    return found;
}

void register_struct_type(Checker *c, const char *name, Type *type)
{
    if (c->struct_type_count >= c->struct_type_cap)
    {
        c->struct_type_cap = GROW_CAPACITY(c->struct_type_cap);
        c->struct_types = realloc_safe(c->struct_types,
                                       (size_t)c->struct_type_cap * sizeof(c->struct_types[0]));
    }
    c->struct_types[c->struct_type_count].name = name;
    c->struct_types[c->struct_type_count].type = type;
    c->struct_type_count++;
    type_tab_insert(&c->struct_tab, &c->struct_tab_cap, &c->struct_tab_count, name, type);
}

Type *find_struct_type(Checker *c, const char *name)
{
    if (!type_tab_disabled())
        return type_tab_find(c->struct_tab, c->struct_tab_cap, name);
    for (int i = 0; i < c->struct_type_count; i++)
    {
        if (strcmp(c->struct_types[i].name, name) == 0)
        {
            return c->struct_types[i].type;
        }
    }
    return NULL;
}

/* Step 11: Resolve a builtin type name ("int", "i64", "f64", "bool", "char")
   to its Type*.  Returns NULL for non-builtin names. */
Type *resolve_builtin_type_by_name(const char *name)
{
    if (strcmp(name, "int") == 0)    return type_int();
    if (strcmp(name, "i64") == 0)    return type_i64();
    if (strcmp(name, "f64") == 0)    return type_f64();
    if (strcmp(name, "bool") == 0)   return type_bool();
    if (strcmp(name, "char") == 0)   return type_char();
    /* Sized integer / f32 scalars: valid trait-impl targets too (e.g. so
       @derive(Show/Serialize) on a generic `Box(T)` works for Box(i16)/Box(u32) —
       std.core.{show,value} impl those interfaces for every scalar). */
    if (strcmp(name, "i8") == 0)     return type_i8();
    if (strcmp(name, "i16") == 0)    return type_i16();
    if (strcmp(name, "i32") == 0)    return type_i32();
    if (strcmp(name, "u8") == 0)     return type_u8();
    if (strcmp(name, "u16") == 0)    return type_u16();
    if (strcmp(name, "u32") == 0)    return type_u32();
    if (strcmp(name, "u64") == 0)    return type_u64();
    if (strcmp(name, "f32") == 0)    return type_f32();
    return NULL;
}

/* True iff `t` is the pure-LS `Str` struct (std/str.ls). Recognized by name,
   like Vec/Map — the string-to-stdlib lowerings (docs/plan_string_to_stdlib.md)
   key off this. Str is non-generic, so a plain name match suffices. */
bool type_is_str_struct(const Type *t)
{
    return t != NULL && t->kind == TYPE_STRUCT &&
           t->as.strukt.name != NULL && strcmp(t->as.strukt.name, "Str") == 0;
}

/* If `t` is `Str` or a read-only borrow `&Str`, return the underlying Str struct
   type; else NULL. A string literal coerces to a (static) Str in either slot —
   a direct `Str` position or an auto-borrowed `&Str` parameter (the resulting Str
   value is then auto-borrowed via `&Str ← Str`). */
Type *str_target_of_expected(const Type *t)
{
    if (type_is_str_struct(t)) return (Type *)t;
    if (t != NULL && t->kind == TYPE_REFERENCE && !t->is_mut &&
        type_is_str_struct(t->as.pointer_to))
        return t->as.pointer_to;
    return NULL;
}

/* P5-4 S-2: every string literal / f-string IS a `Str` — the builtin string
   type is gone. Resolve the Str struct type: normally visible via import
   std.str (the root program gets a prelude-injected import). Modules inside
   std.str's own dependency cone (std.vec, std.c's chain, std.map) cannot
   import std.str back; their literals resolve through the shared registry
   instead — std.str's forward_pass registers `struct Str` (resolved_type set)
   before its imports are recursively checked, so the type is always there. */
Type *checker_str_type(Checker *c)
{
    Type *t = find_struct_type(c, "Str");
    if (t != NULL) return t;
    struct ModuleRegistry *reg = c->registry;
    if (reg == NULL) return NULL;
    for (int m = 0; m < reg->count; m++) {
        AstNode *mast = reg->modules[m].ast;
        if (mast == NULL || mast->kind != AST_PROGRAM) continue;
        for (int d = 0; d < mast->as.program.decl_count; d++) {
            AstNode *decl = mast->as.program.decls[d];
            if (decl != NULL && decl->kind == AST_STRUCT_DECL &&
                decl->resolved_type != NULL &&
                decl->as.struct_decl.name != NULL &&
                strcmp(decl->as.struct_decl.name, "Str") == 0)
                return decl->resolved_type;
        }
    }
    return NULL;
}

/* Step 11: Get the impl_registry key name for a type.
   For structs returns the struct name; for builtins returns "int", "f64" etc. */
const char *type_impl_name(Type *t)
{
    if (t == NULL) return NULL;
    if (t->kind == TYPE_STRUCT && t->as.strukt.name) return t->as.strukt.name;
    switch (t->kind) {
    case TYPE_INT:    return "int";
    case TYPE_I64:    return "i64";
    case TYPE_F64:    return "f64";
    case TYPE_BOOL:   return "bool";
    case TYPE_CHAR:   return "char";
    case TYPE_I8:     return "i8";
    case TYPE_I16:    return "i16";
    case TYPE_I32:    return "i32";
    case TYPE_U8:     return "u8";
    case TYPE_U16:    return "u16";
    case TYPE_U32:    return "u32";
    case TYPE_U64:    return "u64";
    case TYPE_F32:    return "f32";
    default:          return NULL;
    }
}


/* ---- Type compatibility ---- */

/* Check if src type can be assigned to dst type (includes implicit conversions).
   Returns true if compatible. This is stricter than type_equals: it also allows
   *T -> object (implicit), nil -> object, nil -> *T. */
bool type_assignable(const Type *dst, const Type *src)
{
    if (type_equals(dst, src))
        return true;
    if (dst == NULL || src == NULL)
        return false;

    /* A named function can be coerced to a Block value with the same signature.
       The ABI adaptation is handled by codegen via a thunk that ignores env. */
    if (dst->kind == TYPE_BLOCK && src->kind == TYPE_FUNCTION)
    {
        if (dst->as.function.param_count != src->as.function.param_count)
            return false;
        if (dst->as.function.is_vararg != src->as.function.is_vararg)
            return false;
        if (!type_equals(dst->as.function.return_type, src->as.function.return_type))
            return false;
        for (int i = 0; i < dst->as.function.param_count; i++)
            if (!type_equals(dst->as.function.params[i], src->as.function.params[i]))
                return false;
        return true;
    }

    /* Implicit numeric widening (Zig-style): only when dst can represent
       every value of src. Narrowing, signed↔unsigned same-width, float→int,
       and i64↔f64 (mantissa overflow) all remain compile errors. */
    if (type_widens_to(src, dst))
        return true;

    /* nil -> *T or nil -> object */
    if (src->kind == TYPE_NIL && (dst->kind == TYPE_POINTER || dst->kind == TYPE_OBJECT))
        return true;

    /* *T -> object (implicit upcast) */
    if (dst->kind == TYPE_OBJECT && src->kind == TYPE_POINTER)
        return true;

    /* *T -> *u8 (any pointer to byte pointer, e.g. for free()) */
    if (dst->kind == TYPE_POINTER && src->kind == TYPE_POINTER &&
        dst->as.pointer_to && dst->as.pointer_to->kind == TYPE_U8)
        return true;

    /* &T ← T  (auto-borrow — only for READ-only references; &!T must be explicit) */
    if (dst->kind == TYPE_REFERENCE && !dst->is_mut &&
        type_equals(dst->as.pointer_to, src))
        return true;

    /* &T ← &T / &!T ← &!T — already covered by type_equals above (is_mut matches) */

    /* &T ← &!T  (mutable-to-readonly downgrade: callee sees it as read-only).
       The reverse &!T ← &T is FORBIDDEN — you can't upgrade a read-only
       borrow into a writable one. */
    if (dst->kind == TYPE_REFERENCE && !dst->is_mut &&
        src->kind == TYPE_REFERENCE &&
        type_equals(dst->as.pointer_to, src->as.pointer_to))
        return true;

    /* T ← &T / T ← &!T  (auto-reborrow: reading through a borrow yields same T).
       Safe because string ABI is pass-by-value regardless. */
    if (src->kind == TYPE_REFERENCE &&
        type_equals(src->as.pointer_to, dst))
        return true;

    /* object -> object (trivially via type_equals above, but explicit) */
    return false;
}

/* ---- Scope helpers ---- */

void chk_push_scope(Checker *c)
{
    c->current_scope = scope_new(c->current_scope);
}

void chk_pop_scope(Checker *c)
{
    Scope *old = c->current_scope;
    c->current_scope = old->parent;
    scope_free(old);
}

/* ---- Forward declarations ---- */


/* ---- Helper functions ---- */

/* ---- Move semantics helpers (Phase A: linear, no control flow) ---- */










/* Phase G note: the former F.3/F.4A rejections (copy a Block out of a struct
   field / Vec element / Map value) have been removed — codegen now deep-clones
   the closure env at the copy-out site (cg_emit_block_env_clone, defined in
   codegen_stmt.c), so the destination owns an independent env with no
   shared-env double-free. */


/* ---- Move semantics helpers (Phase B: control-flow aware) ---- */

/* A snapshot records the (is_moved, is_maybe_moved) pair for every movable symbol
   reachable from a scope chain at the moment of capture. Used by if/else merging
   and 2-pass loop analysis. */
/* MoveSnapEntry / MoveSnapshot moved to checker_internal.h (shared across TUs). */










/* ---- for-in iterator-protocol desugaring (docs/plan_userdef_for_in.md) ---- */






/* Build the iterator-protocol equivalent of `for var in <iter>` and store it on
   node->as.for_stmt.desugared.  has_iter: the iter type exposes iter()->I (call
   it); otherwise the value is itself an iterator (drive next() directly).
   src_is_ident: the source is a bare variable (borrow in place) — otherwise the
   source is materialized into an owned __src local that outlives the loop. */
/* g_foreach_uid + for-in desugaring moved to checker_lower.c. */


/* Compile-time constant evaluator (comptime_expand_block / CtEval / CtScalar /
   CtFlow / ct_exec_block / ct_eval_scalar / ct_env_free / ct_aenv_find /
   cts_to_f) moved to checker_comptime.c (declared in checker_internal.h). */


/* ---- Declaration checking (top-level pass) ---- */


















/* ---- Trait registry helpers ---- */


/* ---- Operator overloading: built-in operator traits ---- */
















/* ---- Two-pass checking ---- */

/* Pass 1: Register all struct types and function signatures (forward declarations) */
void forward_pass(Checker *c, AstNode *program)
{
    for (int i = 0; i < program->as.program.decl_count; i++)
    {
        AstNode *decl = program->as.program.decls[i];
        switch (decl->kind)
        {
        case AST_STRUCT_DECL:
            check_struct_decl(c, decl);
            break;
        case AST_ENUM_DECL:
            check_enum_decl(c, decl);
            break;
        case AST_TYPE_ALIAS_DECL:
        {
            /* Resolve target type and register under the alias name. Source-
               order rule: struct/enum names referenced by the alias must
               appear earlier in the file (we don't do a separate pre-pass). */
            if (find_type_alias(c, decl->as.type_alias_decl.name) ||
                find_struct_type(c, decl->as.type_alias_decl.name) ||
                find_enum_type(c, decl->as.type_alias_decl.name))
            {
                checker_error(c, decl->line, decl->column,
                              "type name '%s' already defined",
                              decl->as.type_alias_decl.name);
                break;
            }
            Type *target = resolve_type_node(c, decl->as.type_alias_decl.target,
                                             decl->line, decl->column);
            if (target == NULL) break;
            register_type_alias(c, decl->as.type_alias_decl.name, target);
            decl->resolved_type = target;
            break;
        }
        case AST_TRAIT_DECL:
            check_trait_decl(c, decl);
            break;
        case AST_FN_DECL:
        {
            /* G2: skip generic function templates — register as template only */
            if (decl->as.fn_decl.type_param_count > 0) {
                register_fn_template(c, decl);
                break;
            }
            /* Register function signature only (don't check body yet) */
            int n = decl->as.fn_decl.param_count;
            Type **params = NULL;
            if (n > 0)
            {
                params = (Type **)malloc_safe((size_t)n * sizeof(Type *));
                for (int j = 0; j < n; j++)
                {
                    params[j] = resolve_type_node(c, decl->as.fn_decl.param_types[j],
                                                  decl->line, decl->column);
                    /* Top-level fns register here and the check pass inlines
                       AST_FN_DECL (bypassing check_fn_decl), so this is the
                       live rejection site for free functions. */
                    reject_array_by_value_param(c, params[j],
                                                decl->as.fn_decl.param_names[j],
                                                decl->line, decl->column);
                }
            }
            Type *ret = resolve_type_node(c, decl->as.fn_decl.return_type,
                                          decl->line, decl->column);
            checker_reject_borrow_return(c, ret, decl, decl->line, decl->column);  /* Phase 0/2 */
            Type *fn_type = type_function(params, n, ret, false);
            attach_param_defaults(c, decl, fn_type, params);
            scope_define(c->current_scope, decl->as.fn_decl.name, fn_type);
            decl->resolved_type = fn_type;
            break;
        }
        case AST_EXTERN_FN:
            check_extern_fn(c, decl);
            break;
        case AST_EXTERN_STRUCT_DECL:
            check_extern_struct_decl(c, decl);
            break;
        case AST_EXTERN_BLOCK:
            check_extern_block(c, decl);
            break;
        case AST_LOAD_LIB:
            check_load_lib(c, decl);
            break;
        case AST_IMPORT_DECL:
        {
            if (c->registry == NULL)
                break;
            const char *import_path = decl->as.import_decl.path;

            /* Circular import detection */
            if (module_is_importing(c->registry, import_path))
            {
                checker_error(c, decl->line, decl->column,
                              "circular import detected: '%s'", import_path);
                break;
            }

            /* Built-in stdlib fallback (user-priority shadowing): if the
               name matches a compiler built-in module AND no user .ls
               file is present at the resolved path, skip the file loader
               and synthesise a TYPE_MODULE directly from the built-in
               symbol table. The module Type is bound into the current
               scope so subsequent name-resolution treats `math.X` like
               any other module-qualified access. */
            if (getenv("LS_DEBUG_MODULES"))
                fprintf(stderr, "[mod] import '%s' (from %s): builtin=%d userfile=%d\n",
                        import_path, c->source_path ? c->source_path : "?",
                        builtin_module_exists(import_path),
                        module_user_file_exists(import_path, c->source_path));
            if (builtin_module_exists(import_path) &&
                !module_user_file_exists(import_path, c->source_path))
            {
                Type *mod_type = builtin_module_make_type_merged(c, import_path);
                if (mod_type)
                {
                    const char *bn = decl->as.import_decl.alias
                                     ? decl->as.import_decl.alias
                                     : import_path;
                    scope_define(c->current_scope, bn, mod_type);
                }
                break;
            }

            /* Load module (parse if not already loaded) */
            ModuleInfo *mod = module_load(c->registry, import_path, c->source_path);
            if (mod == NULL)
            {
                /* The built-in math module moved to the canonical std.core.math
                   path (merged with lib/std/core/math.ls). Help bare `import math`
                   that doesn't resolve to a user file find the new home. */
                if (strcmp(import_path, "math") == 0)
                    checker_error(c, decl->line, decl->column,
                                  "cannot find module 'math'; the built-in math "
                                  "module is now 'std.core.math' (import std.core.math as math)");
                else
                    checker_error(c, decl->line, decl->column,
                                  "cannot find module '%s'", import_path);
                break;
            }

            /* Type-check the module if not already checked */
            if (!mod->checked)
            {
                module_push_import(c->registry, import_path);
                /* A1 (module generics): collect the module's generic
                   instantiations (identity(int), Pair(int,string).get, ...) and
                   merge them into THIS checker's pending queue so they bubble up
                   to the root and reach codegen. Previously passed NULL here, so
                   module-defined generics were silently discarded → call sites
                   failed with "undefined function 'identity(int)'".
                   A2: stash the module name so the recursive checker prefixes its
                   generic instantiation symbols with it (cross-module collision). */
                const char *saved_ccm = c->registry->current_check_module;
                c->registry->current_check_module = import_path;
                CheckerGenericMethods sub_gm = {0};
                bool ok = checker_check(mod->ast, mod->file_path,
                                        c->registry, &sub_gm);
                c->registry->current_check_module = saved_ccm;
                module_pop_import(c->registry);
                /* DANGLING-POINTER FIX: the recursive check above loads the
                   module's own imports, growing reg->modules; GROW_ARRAY's
                   realloc can MOVE the array, invalidating `mod` (a pointer
                   INTO it). Writing mod->checked / reading mod->ast through
                   the stale pointer is a use-after-free whose symptom depends
                   on heap reuse: sometimes fine, sometimes "unknown type
                   'Str'" / "module has no export" cascades, sometimes a
                   silent segfault — all NONDETERMINISTIC per run (hit when
                   the std.str chain pushed the registry past a capacity
                   boundary). Re-resolve by name after the recursion. */
                mod = module_find(c->registry, import_path);
                if (mod == NULL)
                {
                    checker_error(c, decl->line, decl->column,
                                  "module '%s' lost during recursive check",
                                  import_path);
                    break;
                }
                if (getenv("LS_DEBUG_MODULES"))
                    fprintf(stderr, "[mod] checked '%s': ok=%d\n", import_path, (int)ok);
                if (!ok)
                {
                    if (sub_gm.methods)
                    {
                        for (int gi = 0; gi < sub_gm.count; gi++)
                        {
                            if (sub_gm.methods[gi].cloned_fn)
                                ast_free(sub_gm.methods[gi].cloned_fn);
                            free(sub_gm.methods[gi].mangled_name);
                        }
                        free(sub_gm.methods);
                    }
                    checker_error(c, decl->line, decl->column,
                                  "errors in imported module '%s'", import_path);
                    break;
                }
                for (int gi = 0; gi < sub_gm.count; gi++)
                {
                    if (c->pending_gm_count >= c->pending_gm_cap) {
                        c->pending_gm_cap = c->pending_gm_cap < 8 ? 8 : c->pending_gm_cap * 2;
                        c->pending_generic_methods = realloc_safe(c->pending_generic_methods,
                            (size_t)c->pending_gm_cap * sizeof(c->pending_generic_methods[0]));
                    }
                    int gm_idx = c->pending_gm_count++;
                    c->pending_generic_methods[gm_idx].cloned_fn    = sub_gm.methods[gi].cloned_fn;
                    c->pending_generic_methods[gm_idx].mangled_name = sub_gm.methods[gi].mangled_name;
                    c->pending_generic_methods[gm_idx].struct_type  = sub_gm.methods[gi].struct_type;
                }
                free(sub_gm.methods);
                mod->checked = true;
            }

            /* Collect exported symbols from the module.
               B-5: key the module TYPE by mod->name (the canonical first-loaded
               spelling), NOT the local import_path. When two spellings resolve to
               the same file (module_load dedups them, e.g. `import std.sys.io` and
               `import sys.io`), both must mangle call-site symbols to the single
               emitted copy. For the common single-spelling case mod->name ==
               import_path, so this is an identity change. The scope binding below
               still keys on the local spelling/alias, so name lookup is unaffected. */
            Type *mod_type = type_module_new(mod->name);
            AstNode *mod_ast = mod->ast;
            for (int j = 0; j < mod_ast->as.program.decl_count; j++)
            {
                AstNode *d = mod_ast->as.program.decls[j];
                if (d->kind == AST_FN_DECL && d->resolved_type)
                {
                    type_module_add_export(mod_type,
                                           d->as.fn_decl.name, d->resolved_type);
                }
                /* Step 0 (cross-module generics): a generic free function
                   (type_param_count > 0) has no resolved_type — it is a template,
                   not a concrete fn. Register it into THIS importer's fn-template
                   registry (idempotent) so call sites here can instantiate it,
                   e.g. new_stack(int)(). The instantiation machinery (checker
                   ~L4369 + codegen pending-gm) is origin-agnostic and takes over. */
                else if (d->kind == AST_FN_DECL &&
                         d->as.fn_decl.type_param_count > 0)
                {
                    if (find_fn_template(c, d->as.fn_decl.name) < 0)
                        register_fn_template(c, d);
                }
                else if (d->kind == AST_STRUCT_DECL && d->resolved_type)
                {
                    type_module_add_export(mod_type,
                                           d->as.struct_decl.name, d->resolved_type);
                    /* B-4: same struct name from a different module. Previously
                       B-1 errored here. Now: mark the bare name AMBIGUOUS (bare
                       use → error; qualified `mod.Struct` resolves precisely via
                       the module export table). Same pointer = same module imported
                       transitively → OK, leave the single bare registration. */
                    {
                        const char *sname = d->as.struct_decl.name;
                        Type *existing = find_struct_type(c, sname);
                        if (existing && existing != d->resolved_type)
                        {
                            checker_mark_ambiguous_type(c, sname);
                        }
                        else if (!existing)
                        {
                            /* Phase E.4: register the imported struct type into the
                               importer's struct registry so user code can name it
                               directly (e.g. `File f` after `import io`). */
                            checker_register_struct(c, sname, d->resolved_type);
                        }
                    }
                }
                /* Step 0 (cross-module generics): a generic struct template
                   (type_param_count > 0) has no resolved_type. Register it into
                   THIS importer's struct-template registry (idempotent) and attach
                   the module's matching generic impl, so `Stack(int)` at a call
                   site here flows through checker_instantiate_struct exactly like a
                   same-file generic. NOTE: two modules defining the same generic
                   name collide on the bare name — v1 keeps the first registration
                   (single-definition use is unambiguous). */
                else if (d->kind == AST_STRUCT_DECL &&
                         d->as.struct_decl.type_param_count > 0)
                {
                    const char *gname = d->as.struct_decl.name;
                    int tidx = register_imported_struct_template(
                        c, gname,
                        d->as.struct_decl.type_params,
                        d->as.struct_decl.type_param_count, d, import_path);
                    /* Attach the module's generic impl(T) block, if present and not
                       already attached (idempotent on transitive re-import). */
                    if (tidx >= 0 && c->struct_templates[tidx].impl_node == NULL)
                    {
                        for (int k = 0; k < mod_ast->as.program.decl_count; k++)
                        {
                            AstNode *id = mod_ast->as.program.decls[k];
                            if (id->kind == AST_IMPL_DECL &&
                                id->as.impl_decl.type_param_count > 0 &&
                                id->as.impl_decl.name &&
                                strcmp(id->as.impl_decl.name, gname) == 0)
                            {
                                c->struct_templates[tidx].impl_node = id;
                                break;
                            }
                        }
                    }
                }
                else if (d->kind == AST_ENUM_DECL && d->resolved_type)
                {
                    type_module_add_export(mod_type,
                                           d->as.enum_decl.name, d->resolved_type);
                    /* B-4: same enum name from a different module → mark ambiguous
                       (bare use errors; `mod.Enum` resolves via export table). */
                    {
                        const char *ename = d->as.enum_decl.name;
                        Type *existing_e = find_enum_type(c, ename);
                        if (existing_e && existing_e != d->resolved_type)
                        {
                            checker_mark_ambiguous_type(c, ename);
                        }
                        else if (!existing_e)
                        {
                            /* Phase E.4: register the imported enum into the importer's
                               enum registry so bare variant names resolve without
                               qualification — matching the behaviour the built-in io
                               module had. */
                            checker_register_enum(c, ename, d->resolved_type);
                        }
                    }
                }
                else if (d->kind == AST_VAR_DECL && d->resolved_type)
                {
                    type_module_add_export(mod_type,
                                           d->as.var_decl.name, d->resolved_type);
                }
                else if (d->kind == AST_EXTERN_FN && d->resolved_type)
                {
                    type_module_add_export(mod_type,
                                           d->as.extern_fn.name, d->resolved_type);
                }
                else if (d->kind == AST_EXTERN_BLOCK)
                {
                    for (int eb = 0; eb < d->as.extern_block.decl_count; eb++)
                    {
                        AstNode *ebd = d->as.extern_block.decls[eb];
                        if (ebd->kind == AST_EXTERN_FN && ebd->resolved_type)
                            type_module_add_export(mod_type,
                                                   ebd->as.extern_fn.name,
                                                   ebd->resolved_type);
                    }
                }
                else if (d->kind == AST_IMPL_DECL &&
                         d->as.impl_decl.type_param_count == 0)
                {
                    /* Register impl methods from the imported module into the
                       importer's impl_registry so that instance/static method
                       calls on imported types (e.g. JsonValue.null_val()) work.
                       method->resolved_type was set by check_impl_decl (L7106)
                       and is kept alive because types are intentionally leaked. */
                    const char *impl_name = d->as.impl_decl.name;
                    /* B-4.1: key by THIS module's struct/enum unique name (its
                       llvm_name) so two imported modules' same-named `impl Widget`
                       register under distinct keys. Use the module export table to
                       get THIS module's type (find_struct_type would return the
                       ambiguous first-registered one). */
                    const char *impl_key = impl_name;
                    Type *impl_st = type_module_find_export(mod_type, impl_name);
                    /* L-022 phase 3: the `methods Type` block's type may be
                       IMPORTED into this module rather than declared here (e.g.
                       std.core.str_search's `methods Str` where Str lives in
                       std.core.str_core). Then the module export table misses,
                       and the bare-name fallback would key the methods under
                       "Str" while the call site dispatches on Str's real
                       llvm_name (std_core_str_core__Str) -> lost method. Recover
                       the owning-module llvm_name via the global type registry
                       so registration and dispatch agree. */
                    if (impl_st == NULL)
                        impl_st = find_struct_type(c, impl_name);
                    if (impl_st == NULL)
                        impl_st = find_enum_type(c, impl_name);
                    if (impl_st)
                    {
                        const char *k = impl_key_of_type(impl_st);
                        if (k) impl_key = k;
                    }
                    int impl_idx = find_or_create_impl(c, impl_key);
                    for (int mi = 0; mi < d->as.impl_decl.method_count; mi++)
                    {
                        AstNode *method = d->as.impl_decl.methods[mi];
                        if (method == NULL || method->kind != AST_FN_DECL)
                            continue;
                        if (method->resolved_type == NULL)
                            continue;
                        bool m_static = method->as.fn_decl.is_static;
                        int  m_sbk    = method->as.fn_decl.self_borrow_kind;
                        const char *mname = method->as.fn_decl.name;
                        /* L-022 half 2 idempotency: propagate_inherited_methods
                           may already have registered this exact inherent method
                           via a transitive/facade path. Registering it again
                           would trip register_method's same-origin duplicate
                           error, so skip if it already exists. For every
                           pre-L-022 program nothing was pre-registered here, so
                           this pre-check never fires and behaviour/IR is
                           unchanged. */
                        if (find_method(c, impl_key, mname) != NULL)
                            continue;
                        register_method(c, impl_idx, mname,
                                        method->resolved_type,
                                        m_static, m_sbk,
                                        NULL, method,  /* imported inherent impl */
                                        method->line, method->column);
                        /* Also expose as a free function so direct calls work */
                        scope_define(c->current_scope, mname,
                                     method->resolved_type);
                    }
                }
                /* An imported `interface Foo`/`methods Type: Foo` (incl. builtin
                   targets like `methods int: Hash`): register it so `where T:Foo`
                   bounds and `x.foo()` dispatch work in THIS importer. */
                else if (d->kind == AST_TRAIT_DECL ||
                         d->kind == AST_IMPL_TRAIT_DECL)
                {
                    register_one_imported_trait_decl(c, d, mod_type);
                }
                /* A transitive import inside the imported module (user → std.map →
                   std.hash): propagate that sub-module's traits/impls too, so a
                   `where K: Hash` bound inside std.map's monomorphized methods is
                   satisfiable here even though the user never imported std.hash. */
                else if (d->kind == AST_IMPORT_DECL)
                {
                    const char *visited[64];
                    int vcount = 0;
                    propagate_imported_traits(c, d->as.import_decl.path,
                                              visited, &vcount);
                    /* L-022 half 2: also propagate transitively-reachable
                       inherent methods + their concrete types, so a consumer
                       importing only a facade module sees the types/methods
                       defined in that facade's dependency cone. */
                    const char *mvisited[64];
                    int mvcount = 0;
                    propagate_inherited_methods(c, d->as.import_decl.path,
                                                mvisited, &mvcount);
                }
            }

            /* If `import foo as bar` was used, bind under the alias; otherwise
               bind under the full dotted path (e.g. "std.time"). */
            const char *bind_name = decl->as.import_decl.alias
                                    ? decl->as.import_decl.alias
                                    : import_path;
            scope_define(c->current_scope, bind_name, mod_type);
            /* Phase 1 (docs/plan_module_fn_resolution.md): also bind the full
               canonical dotted path (e.g. "std.time") so `std.time.fn()` resolves
               without an alias, uniformly whether or not an alias was used. The
               dotted name never collides with a real IDENT (no source identifier
               contains '.'). Skip when bind_name already IS the path (no alias). */
            if (decl->as.import_decl.alias &&
                scope_resolve_local(c->current_scope, import_path) == NULL)
                scope_define(c->current_scope, import_path, mod_type);
            break;
        }
        default:
            break;
        }
    }
}

/* Pass 2: Check all function bodies and remaining declarations */
static void check_pass(Checker *c, AstNode *program)
{
    for (int i = 0; i < program->as.program.decl_count; i++)
    {
        AstNode *decl = program->as.program.decls[i];
        switch (decl->kind)
        {
        case AST_STRUCT_DECL:
        case AST_ENUM_DECL:
        case AST_TYPE_ALIAS_DECL:
            /* Already handled in forward pass */
            break;
        case AST_FN_DECL:
        {
            /* Check function body (signature already registered) */
            Type *fn_type = decl->resolved_type;
            if (fn_type == NULL || fn_type->kind != TYPE_FUNCTION)
                break;

            /* Phase A1: top-level fns cannot declare &self / &!self. */
            if (decl->as.fn_decl.self_borrow_kind != 0 &&
                decl->as.fn_decl.impl_struct_name == NULL)
            {
                checker_error(c, decl->line, decl->column,
                              "&%sself is only valid as the first parameter of a method "
                              "inside an `methods` block",
                              decl->as.fn_decl.self_borrow_kind == 2 ? "!" : "");
                break;
            }

            chk_push_scope(c);
            for (int j = 0; j < decl->as.fn_decl.param_count; j++)
            {
                Type *pt = fn_type->as.function.params[j];
                /* Phase 5: unwrap &T / &!T → T for the body-local symbol, remember kind. */
                bool is_borrow = false;
                bool is_mut_borrow = false;
                if (pt && pt->kind == TYPE_REFERENCE)
                {
                    if (pt->is_mut) is_mut_borrow = true;
                    else            is_borrow     = true;
                    pt = pt->as.pointer_to;
                }
                Symbol *param_sym = scope_define(c->current_scope,
                                                 decl->as.fn_decl.param_names[j], pt);
                if (param_sym)
                {
                    param_sym->is_borrow = is_borrow;
                    param_sym->is_mut_borrow = is_mut_borrow;
                    /* F.2: Block params are shallow-copy borrows of caller's env */
                    if (pt && pt->kind == TYPE_BLOCK)
                        param_sym->is_borrow = true;
                }
            }
            Type *saved_ret = c->current_fn_return;
            c->current_fn_return = fn_type->as.function.return_type;
            check_stmt(c, decl->as.fn_decl.body);
            checker_elide_last_use(c, decl); /* A1 clone-elision */
            c->current_fn_return = saved_ret;
            chk_pop_scope(c);
            break;
        }
        case AST_IMPL_DECL:
            check_impl_decl(c, decl);
            break;
        case AST_IMPL_TRAIT_DECL:
            check_impl_trait_decl(c, decl);
            break;
        case AST_EXTERN_FN:
        case AST_EXTERN_STRUCT_DECL:
        case AST_EXTERN_BLOCK:
        case AST_LOAD_LIB:
            /* Already handled in forward pass */
            break;
        default:
            check_decl(c, decl);
            break;
        }
    }
}

/* ---- Register built-in functions ---- */

static void register_builtins(Checker *c)
{
    /* Builtin enums (Option / Result) — see register_builtin_enums for details. */
    register_builtin_enums(c);

    /* @print(...) -> void — the print intrinsic (only spelling; scanner produces
       IDENT "@print"). Bare print is retired: it resolves to nothing -> a clear
       "undefined 'print'" error. Accepts any printable type. */
    {
        Type *ft = type_function(NULL, 0, type_void(), true);
        scope_define(c->current_scope, "@print", ft);
    }
    /* A-FLIP (docs/plan_runtime_primitives.md): malloc/realloc/free/abort are no
       longer global builtins. They live in std.c (extern fn malloc/realloc/free
       + `def abort`) and are reached either by canonical path std.c.malloc (which
       the checker/codegen recognise by spelling — works inside generic bodies) or
       via an import alias (c.malloc). A bare `malloc(...)` is now "undefined". */
    /* sizeof(Type) is handled as a compile-time AST_SIZEOF node (see parser
       infix_call + check_expr), not a runtime function — nothing to register. */
    /* sqrt(f64) -> f64 */
    {
        Type **params = (Type **)malloc_safe(sizeof(Type *));
        params[0] = type_f64();
        Type *ft = type_function(params, 1, type_f64(), false);
        scope_define(c->current_scope, "sqrt", ft);
    }
    /* abort: see the A-FLIP note above — now std.c.abort() / std_c__abort. */

    /* Structured concurrency primitives (std.task's generic `Task(T)`):
         __task_spawn(Block()->T, *T box) -> object   run the closure, result -> box
         __task_join(object)              -> void       wait for the worker
       These are GENERIC in the result type T, so they cannot be a fixed
       scope_define — they are intercepted by name in check_builtin_call (like
       __take/__drop_at). T is read by codegen from arg0's Block return type.
       Internal plumbing — users go through std.task's constructor / join(). The
       closure is MOVE-captured, so each task is isolated (no shared mutable
       state) and sound without a lifetime system. */
}

/* B-MAP-M5-004: has_drop fixpoint. Generic struct/enum instantiations cache
   has_drop at instantiation time, which can be a stale `false` for a type that
   only BECOMES has_drop through a recursive container payload — e.g. JsonValue
   owns heap via Vec(JsonValue)/Map(string,JsonValue), and Option(JsonValue)
   (from Map.get) is instantiated before that propagates, so it caches
   has_drop=false → no __drop emitted → leak. After all decls + impl
   instantiations are processed, re-propagate has_drop across every registered
   struct/enum type until stable. Monotonic (only ever sets true, never clears —
   so user-__drop structs with POD fields keep their has_drop), so it converges. */
/* ---- C1 §3.5: has_drop fixpoint --------------------------------------------
   A struct/enum owns heap (has_drop) iff a DIRECT struct/enum field or payload
   does — type_owns_heap_for_enum() looks exactly one level deep (Vec/Map/Str
   ARE has_drop structs, so a `Vec(T)` field is already a direct struct field).
   Propagation therefore flows strictly along "type T is a direct field of
   container U" edges. The worklist flips each type at most once and
   re-examines only the containers that name a just-flipped type
   (O(types+edges), vs. the O(rounds×types×fields) full-registry rescan of the
   retired reference driver — see git history for the legacy implementation
   and its LS_HASDROP_VERIFY parity oracle, both migration-time scaffolding,
   plan §3.5 + §5 risk row). */

/* Read has_drop for a struct/enum (false for anything else / NULL). */
static bool hasdrop_is_set(const Type *t)
{
    if (t == NULL) return false;
    if (t->kind == TYPE_STRUCT) return t->as.strukt.has_drop;
    if (t->kind == TYPE_ENUM)   return t->as.enom.has_drop;
    return false;
}

/* Recompute has_drop for one type from its fields/payloads, setting the flag if
   a direct heap-owning member is found. Returns whether has_drop is set on exit
   (already-set types short-circuit true). */
static bool hasdrop_eval(Type *t)
{
    if (t == NULL) return false;
    if (t->kind == TYPE_STRUCT) {
        if (t->as.strukt.has_drop) return true;
        for (int f = 0; f < t->as.strukt.field_count; f++)
            if (type_owns_heap_for_enum(t->as.strukt.fields[f].type)) {
                t->as.strukt.has_drop = true;
                return true;
            }
        return false;
    }
    if (t->kind == TYPE_ENUM) {
        if (t->as.enom.has_drop) return true;
        for (int v = 0; v < t->as.enom.variant_count; v++)
            for (int p = 0; p < t->as.enom.variants[v].payload_count; p++) {
                Type *pt = t->as.enom.variants[v].payload_types[p];
                if (pt == t || type_owns_heap_for_enum(pt)) {  /* self-ref = box heap */
                    t->as.enom.has_drop = true;
                    return true;
                }
            }
        return false;
    }
    return false;
}

/* Worklist: seed every type once, then re-examine a container only when one of
   its direct field types just flipped to has_drop. */
static void checker_propagate_has_drop_worklist(Checker *c)
{
    int ns = c->struct_type_count, ne = c->enum_type_count;
    int n = ns + ne;
    if (n == 0) return;

    Type **types = malloc_safe((size_t)n * sizeof(Type *));
    for (int i = 0; i < ns; i++) types[i]      = c->struct_types[i].type;
    for (int i = 0; i < ne; i++) types[ns + i] = c->enum_types[i].type;

    /* pointer -> index, open addressing (keep first on a duplicate pointer). */
    int hcap = 16;
    while (hcap < n * 2) hcap <<= 1;
    int *hslot = malloc_safe((size_t)hcap * sizeof(int));
    for (int i = 0; i < hcap; i++) hslot[i] = -1;
    unsigned long long hmask = (unsigned long long)hcap - 1;
    for (int i = 0; i < n; i++) {
        if (types[i] == NULL) continue;
        unsigned long long h =
            ((unsigned long long)(uintptr_t)types[i] * 0x9E3779B97F4A7C15ULL) & hmask;
        while (hslot[h] != -1 && types[hslot[h]] != types[i]) h = (h + 1) & hmask;
        if (hslot[h] == -1) hslot[h] = i;
    }
    #define HD_IDX_OF(p, out) do {                                              \
        (out) = -1;                                                            \
        if (p) {                                                               \
            unsigned long long _h =                                            \
                ((unsigned long long)(uintptr_t)(p) * 0x9E3779B97F4A7C15ULL) & hmask; \
            while (hslot[_h] != -1) {                                          \
                if (types[hslot[_h]] == (p)) { (out) = hslot[_h]; break; }     \
                _h = (_h + 1) & hmask;                                         \
            }                                                                  \
        }                                                                      \
    } while (0)

    /* Collect edges (dep_index -> container_index) for every direct struct/enum
       member, then pack into CSR keyed by dep_index. Deps not in the registry
       (idx == -1) can never flip, so they need no edge. */
    int ecap = n + 8, ecount = 0;
    int *edge_t = malloc_safe((size_t)ecap * sizeof(int));
    int *edge_u = malloc_safe((size_t)ecap * sizeof(int));
    for (int u = 0; u < n; u++) {
        Type *U = types[u];
        if (U == NULL) continue;
        if (U->kind == TYPE_STRUCT) {
            for (int f = 0; f < U->as.strukt.field_count; f++) {
                Type *ft = U->as.strukt.fields[f].type;
                if (!ft || (ft->kind != TYPE_STRUCT && ft->kind != TYPE_ENUM)) continue;
                int ti; HD_IDX_OF(ft, ti);
                if (ti < 0) continue;
                if (ecount == ecap) {
                    ecap *= 2;
                    edge_t = realloc_safe(edge_t, (size_t)ecap * sizeof(int));
                    edge_u = realloc_safe(edge_u, (size_t)ecap * sizeof(int));
                }
                edge_t[ecount] = ti; edge_u[ecount] = u; ecount++;
            }
        } else if (U->kind == TYPE_ENUM) {
            for (int v = 0; v < U->as.enom.variant_count; v++)
                for (int p = 0; p < U->as.enom.variants[v].payload_count; p++) {
                    Type *pt = U->as.enom.variants[v].payload_types[p];
                    if (!pt || pt == U ||
                        (pt->kind != TYPE_STRUCT && pt->kind != TYPE_ENUM)) continue;
                    int ti; HD_IDX_OF(pt, ti);
                    if (ti < 0) continue;
                    if (ecount == ecap) {
                        ecap *= 2;
                        edge_t = realloc_safe(edge_t, (size_t)ecap * sizeof(int));
                        edge_u = realloc_safe(edge_u, (size_t)ecap * sizeof(int));
                    }
                    edge_t[ecount] = ti; edge_u[ecount] = u; ecount++;
                }
        }
    }

    int *roff = malloc_safe((size_t)(n + 1) * sizeof(int));
    memset(roff, 0, (size_t)(n + 1) * sizeof(int));
    for (int e = 0; e < ecount; e++) roff[edge_t[e] + 1]++;
    for (int i = 0; i < n; i++) roff[i + 1] += roff[i];
    int *rtargets = ecount ? malloc_safe((size_t)ecount * sizeof(int)) : NULL;
    int *rfill = malloc_safe((size_t)n * sizeof(int));
    memset(rfill, 0, (size_t)n * sizeof(int));
    for (int e = 0; e < ecount; e++) {
        int t = edge_t[e];
        rtargets[roff[t] + rfill[t]++] = edge_u[e];
    }

    /* Ring worklist (<= n live at once thanks to the in-queue flag). */
    int qcap = n + 1;
    int *queue = malloc_safe((size_t)qcap * sizeof(int));
    bool *inq = malloc_safe((size_t)n * sizeof(bool));
    memset(inq, 0, (size_t)n * sizeof(bool));
    int head = 0, tail = 0;
    for (int i = 0; i < n; i++) { queue[tail] = i; tail = (tail + 1) % qcap; inq[i] = true; }

    while (head != tail) {
        int u = queue[head]; head = (head + 1) % qcap; inq[u] = false;
        Type *U = types[u];
        if (hasdrop_is_set(U)) continue;   /* already propagated when it flipped */
        if (hasdrop_eval(U)) {             /* just flipped false -> true */
            for (int e = roff[u]; e < roff[u + 1]; e++) {
                int w = rtargets[e];
                if (!inq[w] && !hasdrop_is_set(types[w])) {
                    queue[tail] = w; tail = (tail + 1) % qcap; inq[w] = true;
                }
            }
        }
    }

    #undef HD_IDX_OF
    free(types); free(hslot);
    free(edge_t); free(edge_u);
    free(roff); free(rtargets); free(rfill);
    free(queue); free(inq);
}

static void checker_propagate_has_drop_fixpoint(Checker *c)
{
    checker_propagate_has_drop_worklist(c);
}

/* ---- Public entry point ---- */

/* Tear down a Checker after its passes have run. Shared by checker_check and
   checker_inspect. out_gm (may be NULL): if set, no error occurred, and there
   are pending generic methods, ownership transfers to the caller; otherwise the
   pending methods are freed here. Behaviour is identical to the cleanup that
   previously lived inline in checker_check. */
static void checker_teardown(Checker *c, CheckerGenericMethods *out_gm)
{
    /* G1.5: transfer pending generic methods to caller if requested */
    if (out_gm && !c->had_error && c->pending_gm_count > 0) {
        out_gm->count = c->pending_gm_count;
        out_gm->methods = malloc_safe(
            (size_t)c->pending_gm_count * sizeof(out_gm->methods[0]));
        for (int i = 0; i < c->pending_gm_count; i++) {
            out_gm->methods[i].cloned_fn    = c->pending_generic_methods[i].cloned_fn;
            out_gm->methods[i].mangled_name = c->pending_generic_methods[i].mangled_name;
            out_gm->methods[i].struct_type  = c->pending_generic_methods[i].struct_type;
        }
        /* Ownership transferred — just free the container array */
        free(c->pending_generic_methods);
    } else {
        /* Not transferred — free everything as safety net */
        for (int i = 0; i < c->pending_gm_count; i++) {
            if (c->pending_generic_methods[i].cloned_fn)
                ast_free(c->pending_generic_methods[i].cloned_fn);
            free(c->pending_generic_methods[i].mangled_name);
        }
        free(c->pending_generic_methods);
        if (out_gm) { out_gm->methods = NULL; out_gm->count = 0; }
    }

    for (int i = 0; i < c->diag_seen_count; i++) {
        free(c->diag_seen[i].file);
        free(c->diag_seen[i].msg);
    }

    /* Cleanup */
    for (int i = 0; i < c->lazy_gm_count; i++) {
        free(c->lazy_generic_methods[i].mangled_name);
        free(c->lazy_generic_methods[i].type_args);
    }
    free(c->lazy_generic_methods);
    scope_free(c->current_scope);
    /* Note: struct types and function types are intentionally leaked for now
       since AST nodes reference them via resolved_type. They will be freed
       when the full compilation pipeline is in place. */
    free(c->struct_types);
    free(c->struct_tab);
    free(c->enum_types);
    free(c->enum_tab);
    free(c->type_aliases);
    for (int i = 0; i < c->enum_template_count; i++)
    {
        for (int v = 0; v < c->enum_templates[i].variant_count; v++)
            free(c->enum_templates[i].variants[v].payload);
        free(c->enum_templates[i].variants);
    }
    free(c->enum_templates);
    /* G1: struct_templates — entries point into AST, nothing to deep-free */
    free(c->struct_templates);
    /* G2: fn_templates — entries point into AST, nothing to deep-free */
    free(c->fn_templates);
    for (int i = 0; i < c->impl_count; i++)
    {
        free(c->impl_registry[i].methods);
    }
    free(c->impl_registry);
    free(c->impl_tab);
    /* Trait registry cleanup */
    for (int i = 0; i < c->trait_count; i++)
    {
        free((void *)c->trait_registry[i].name);
        for (int j = 0; j < c->trait_registry[i].method_count; j++)
            free((void *)c->trait_registry[i].methods[j].name);
        free(c->trait_registry[i].methods);
    }
    free(c->trait_registry);
    /* Trait impls — pointers into AST, no deep-free needed */
    free(c->trait_impls);
}

/* ---- `ls inspect`: static reflection of a type's fields + methods ----
   Stage 1.5 of docs/plan_static_reflection.md. Walks the checker registries
   (which already hold field metadata in Type and method metadata in
   impl_registry) and prints a human-readable summary. Pure checker-side: no
   codegen, no new language semantics. */

/* Render a method's self receiver for display; NULL for static methods. */
static const char *inspect_self_str(bool is_static, int self_borrow_kind)
{
    if (is_static) return NULL;
    switch (self_borrow_kind) {
        case 2:  return "&!self";  /* writable borrow */
        case 1:  return "&self";   /* read-only borrow */
        default: return "self";    /* 0 = legacy implicit */
    }
}

/* Append safely to a fixed buffer, keeping pos in [0, cap). */
static int inspect_appendf(char *buf, int pos, int cap, const char *fmt, ...)
{
    if (pos < 0 || pos >= cap) return cap - 1;
    va_list ap;
    va_start(ap, fmt);
    int n = vsnprintf(buf + pos, (size_t)(cap - pos), fmt, ap);
    va_end(ap);
    if (n < 0) return pos;
    pos += n;
    if (pos >= cap) pos = cap - 1;
    return pos;
}

/* Map an internal method name to its user-facing display form, shared by
   `ls inspect` and @derive(Reflect) so both speak the same surface terminology:
   the destructor is `~` (never `__drop`), the deep-copy hook is `clone`, and
   operator overloads show as their symbol. Returns mname unchanged otherwise. */
const char *method_display_name(const char *mname)
{
    if (strcmp(mname, "__drop") == 0)  return "~";
    if (strcmp(mname, "__clone") == 0) return "clone";
    if (strncmp(mname, "$op_", 4) == 0) {
        const char *o = mname + 4;
        if (strcmp(o, "eq")  == 0) return "==";
        if (strcmp(o, "ne")  == 0) return "!=";
        if (strcmp(o, "lt")  == 0) return "<";
        if (strcmp(o, "le")  == 0) return "<=";
        if (strcmp(o, "gt")  == 0) return ">";
        if (strcmp(o, "ge")  == 0) return ">=";
        if (strcmp(o, "add") == 0) return "+";
        if (strcmp(o, "sub") == 0) return "-";
        if (strcmp(o, "mul") == 0) return "*";
        if (strcmp(o, "div") == 0) return "/";
        if (strcmp(o, "rem") == 0) return "%";
    }
    return mname;
}

static void inspect_print_methods(Checker *c, const char *key)
{
    int idx = find_impl_idx(c, key);
    if (idx < 0 || c->impl_registry[idx].method_count == 0) {
        printf("  methods: (none)\n");
        return;
    }
    printf("  methods:\n");
    for (int j = 0; j < c->impl_registry[idx].method_count; j++) {
        const char *mname = c->impl_registry[idx].methods[j].name;
        Type *mt          = c->impl_registry[idx].methods[j].type;
        bool is_static    = c->impl_registry[idx].methods[j].is_static;
        int  sbk          = c->impl_registry[idx].methods[j].self_borrow_kind;

        /* Pretty-print the lifecycle hooks per user terminology (shared mapping). */
        const char *disp = method_display_name(mname);
        const char *tag  = NULL;
        if (strcmp(mname, "__drop") == 0)       tag = "Destroy";
        else if (strcmp(mname, "__clone") == 0) tag = "Clone";

        char line[512];
        int cap = (int)sizeof(line);
        int pos = inspect_appendf(line, 0, cap, "    def %s(", disp);
        const char *self_s = inspect_self_str(is_static, sbk);
        bool first = true;
        if (self_s) { pos = inspect_appendf(line, pos, cap, "%s", self_s); first = false; }
        Type *ret = NULL;
        if (mt && mt->kind == TYPE_FUNCTION) {
            int start = is_static ? 0 : 1;   /* params[0] is the implicit self ptr */
            for (int p = start; p < mt->as.function.param_count; p++) {
                if (!first) pos = inspect_appendf(line, pos, cap, ", ");
                pos = inspect_appendf(line, pos, cap, "%s",
                                      type_name(mt->as.function.params[p]));
                first = false;
            }
            ret = mt->as.function.return_type;
        }
        pos = inspect_appendf(line, pos, cap, ")");
        if (ret && ret->kind != TYPE_VOID)
            pos = inspect_appendf(line, pos, cap, " -> %s", type_name(ret));
        if (is_static) pos = inspect_appendf(line, pos, cap, "   [static]");
        if (tag)       pos = inspect_appendf(line, pos, cap, "   [%s]", tag);
        printf("%s\n", line);
    }
}

/* Print fields and/or methods for the named type. Returns 0 if found, 1 if not.
   want_fields/want_methods gate the two sections (REPL :fields / :methods). */
static int dump_type_info(Checker *c, const char *query,
                          bool want_fields, bool want_methods)
{
    Type *st = find_struct_type(c, query);
    if (st && st->kind == TYPE_STRUCT) {
        printf("struct %s\n", st->as.strukt.name ? st->as.strukt.name : query);
        if (want_fields) {
            if (st->as.strukt.field_count == 0) {
                printf("  fields: (none)\n");
            } else {
                printf("  fields:\n");
                for (int i = 0; i < st->as.strukt.field_count; i++) {
                    printf("    %s : %s%s\n",
                           st->as.strukt.fields[i].name,
                           type_name(st->as.strukt.fields[i].type),
                           st->as.strukt.fields[i].is_private ? "   [private]" : "");
                }
            }
        }
        if (want_methods) {
            const char *skey = impl_key_of_type(st);
            inspect_print_methods(c, skey ? skey : query);
        }
        return 0;
    }
    Type *en = find_enum_type(c, query);
    if (en && en->kind == TYPE_ENUM) {
        printf("enum %s\n", en->as.enom.name ? en->as.enom.name : query);
        if (want_fields) {
            printf("  variants:\n");
            for (int i = 0; i < en->as.enom.variant_count; i++) {
                char line[512];
                int cap = (int)sizeof(line);
                int pos = inspect_appendf(line, 0, cap, "    %s",
                                          en->as.enom.variants[i].name);
                int pc = en->as.enom.variants[i].payload_count;
                if (pc > 0) {
                    pos = inspect_appendf(line, pos, cap, "(");
                    for (int p = 0; p < pc; p++) {
                        if (p > 0) pos = inspect_appendf(line, pos, cap, ", ");
                        pos = inspect_appendf(line, pos, cap, "%s",
                                type_name(en->as.enom.variants[i].payload_types[p]));
                    }
                    pos = inspect_appendf(line, pos, cap, ")");
                }
                printf("%s\n", line);
            }
        }
        if (want_methods) {
            const char *ekey = impl_key_of_type(en);
            inspect_print_methods(c, ekey ? ekey : query);
        }
        return 0;
    }
    fprintf(stderr,
        "inspect: type '%s' not found. It must be a concrete struct or enum "
        "visible in the file (defined or imported). For a generic template, "
        "inspect a concrete instantiation.\n", query);
    return 1;
}

int checker_inspect_ex(AstNode *program, const char *source_path,
                       struct ModuleRegistry *registry, const char *type_query,
                       bool want_fields, bool want_methods)
{
    if (program == NULL || program->kind != AST_PROGRAM) return 1;

    Checker c;
    memset(&c, 0, sizeof(Checker));
    c.source_path = source_path;
    c.registry = registry;
    c.module_name = registry ? registry->current_check_module : NULL;
    c.elide_pass_enabled = checker_elide_env_enabled();
    c.current_scope = scope_new(NULL);

    register_builtins(&c);
    register_builtin_operator_traits(&c);
    expand_derives(&c, program);
    forward_pass(&c, program);
    check_pass(&c, program);
    checker_propagate_has_drop_fixpoint(&c);

    int rc;
    if (c.had_error) {
        fprintf(stderr, "inspect: file has type errors; cannot inspect '%s'.\n",
                type_query);
        rc = 1;
    } else {
        rc = dump_type_info(&c, type_query, want_fields, want_methods);
    }
    checker_teardown(&c, NULL);
    return rc;
}

int checker_inspect(AstNode *program, const char *source_path,
                    struct ModuleRegistry *registry, const char *type_query)
{
    return checker_inspect_ex(program, source_path, registry, type_query,
                              true, true);
}

bool checker_check(AstNode *program, const char *source_path,
                   struct ModuleRegistry *registry,
                   CheckerGenericMethods *out_gm)
{
    if (program == NULL || program->kind != AST_PROGRAM)
        return false;

    Checker c;
    memset(&c, 0, sizeof(Checker));
    c.source_path = source_path;
    c.registry = registry;
    /* A2: NULL for the root program; set to the module name when this is a
       recursive module check (the import handler stashes it on the registry). */
    c.module_name = registry ? registry->current_check_module : NULL;
    c.elide_pass_enabled = checker_elide_env_enabled();
    c.current_scope = scope_new(NULL);

    register_builtins(&c);
    register_builtin_operator_traits(&c);
    expand_derives(&c, program);
    forward_pass(&c, program);
    check_pass(&c, program);
    /* B-MAP-M5-004: settle has_drop across recursive-via-container types so that
       e.g. Option(JsonValue) is correctly has_drop and gets a __drop emitted. */
    checker_propagate_has_drop_fixpoint(&c);

    checker_teardown(&c, out_gm);

    return !c.had_error;
}

/* ---- Public API for built-in stdlib modules (e.g. `io`) ---- */

void checker_register_struct(Checker *c, const char *name, Type *type) {
    register_struct_type(c, name, type);
}

void checker_register_enum(Checker *c, const char *name, Type *type) {
    register_enum_type(c, name, type);
}

Type *checker_find_enum(Checker *c, const char *name) {
    return find_enum_type(c, name);
}

Type *checker_instantiate_result(Checker *c, Type *t, Type *e) {
    int idx = find_template_idx(c, "Result");
    if (idx < 0) return NULL;
    Type *args[2] = { t, e };
    return instantiate_template(c, idx, args, 2, 0, 0);
}

Type *checker_instantiate_option(Checker *c, Type *t) {
    int idx = find_template_idx(c, "Option");
    if (idx < 0) return NULL;
    return instantiate_template(c, idx, &t, 1, 0, 0);
}
