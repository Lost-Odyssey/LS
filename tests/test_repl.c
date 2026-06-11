/* test_repl.c — REPL helper unit tests: classification, completeness, highlight.
 * Pure logic over the scanner; no LLVM / JIT involved. */
#include "repl_edit.h"
#include "scanner.h"
#include "common.h"

#include <stdio.h>
#include <string.h>

static int tests_run = 0;
static int tests_passed = 0;

#define ASSERT(cond, msg) do { \
    tests_run++; \
    if (!(cond)) { \
        fprintf(stderr, "FAIL: %s:%d: %s\n", __FILE__, __LINE__, msg); \
        return; \
    } \
    tests_passed++; \
} while(0)

/* ---- classification ---- */

static void test_classify(void) {
    ASSERT(repl_classify("import math") == REPL_IMPORT, "import → IMPORT");
    ASSERT(repl_classify("import std.time as T") == REPL_IMPORT, "import as → IMPORT");
    ASSERT(repl_classify("fn f() {}") == REPL_DECL, "fn → DECL");
    ASSERT(repl_classify("struct P { int x }") == REPL_DECL, "struct → DECL");
    ASSERT(repl_classify("enum E { A }") == REPL_DECL, "enum → DECL");
    ASSERT(repl_classify("trait T { fn d(&self) }") == REPL_DECL, "trait → DECL");
    ASSERT(repl_classify("impl P {}") == REPL_DECL, "impl → DECL");
    ASSERT(repl_classify("type F = int") == REPL_DECL, "type → DECL");
    ASSERT(repl_classify("int x = 1") == REPL_VAR, "int x → VAR");
    ASSERT(repl_classify("Str s = \"hi\"") == REPL_VAR, "Str s → VAR");
    ASSERT(repl_classify("vec(int) v = []") == REPL_EXPR, "builtin vec syntax → EXPR");
    ASSERT(repl_classify("Point p = mk()") == REPL_VAR, "user type → VAR");
    ASSERT(repl_classify("print(x)") == REPL_EXPR, "call → EXPR");
    ASSERT(repl_classify("x = 5") == REPL_EXPR, "assign existing → EXPR");
    ASSERT(repl_classify("1 + 2") == REPL_EXPR, "arith → EXPR");
    printf("  PASS: test_classify\n");
}

/* ---- completeness ---- */

static void test_complete(void) {
    /* complete */
    ASSERT(repl_input_is_complete("1 + 2"), "arith complete");
    ASSERT(repl_input_is_complete("print(x)"), "balanced call complete");
    ASSERT(repl_input_is_complete("fn f() { return 1 }"), "full fn complete");
    ASSERT(repl_input_is_complete("[1, 2, 3]"), "balanced array complete");
    ASSERT(repl_input_is_complete("\"a string\""), "closed string complete");
    /* incomplete */
    ASSERT(!repl_input_is_complete("fn f() {"), "open brace incomplete");
    ASSERT(!repl_input_is_complete("print("), "open paren incomplete");
    ASSERT(!repl_input_is_complete("[1,"), "open bracket incomplete");
    ASSERT(!repl_input_is_complete("1 +"), "trailing + incomplete");
    ASSERT(!repl_input_is_complete("x ="), "trailing = incomplete");
    ASSERT(!repl_input_is_complete("\"abc"), "unterminated string incomplete");
    ASSERT(!repl_input_is_complete("foo("), "open call incomplete");
    /* a // comment does not start a bracket; line is complete */
    ASSERT(repl_input_is_complete("1 + 2 // adds"), "trailing comment complete");
    printf("  PASS: test_complete\n");
}

/* ---- highlight ---- */

/* Strip ANSI CSI escapes (ESC '[' ... final-byte) into `out`. */
static void strip_ansi(const char *in, char *out) {
    size_t o = 0;
    for (const char *p = in; *p; ) {
        if (p[0] == '\x1b' && p[1] == '[') {
            p += 2;
            while (*p && !((*p >= 'A' && *p <= 'Z') || (*p >= 'a' && *p <= 'z'))) p++;
            if (*p) p++;   /* skip final byte */
        } else {
            out[o++] = *p++;
        }
    }
    out[o] = '\0';
}

static void test_highlight(void) {
    const char *src = "fn add(int a) -> int { return a + 42 }";
    char out[4096];
    repl_highlight_render(src, out, sizeof(out));

    /* contains some color codes */
    ASSERT(strstr(out, "\x1b[") != NULL, "highlight emits ANSI");
    ASSERT(strstr(out, "\x1b[35m") != NULL, "keyword magenta present");  /* fn / return */
    ASSERT(strstr(out, "\x1b[36m") != NULL, "type cyan present");        /* int */
    ASSERT(strstr(out, "\x1b[33m") != NULL, "number yellow present");    /* 42 */

    /* stripping ANSI reproduces the original text exactly */
    char stripped[4096];
    strip_ansi(out, stripped);
    ASSERT(strcmp(stripped, src) == 0, "strip(highlight) == original");

    /* string literal coloring + round-trip */
    const char *s2 = "Str s = \"hello\"";
    repl_highlight_render(s2, out, sizeof(out));
    ASSERT(strstr(out, "\x1b[32m") != NULL, "string green present");
    strip_ansi(out, stripped);
    ASSERT(strcmp(stripped, s2) == 0, "strip(highlight) string == original");

    /* comment coloring + round-trip */
    const char *s3 = "x + 1 // note";
    repl_highlight_render(s3, out, sizeof(out));
    ASSERT(strstr(out, "\x1b[90m") != NULL, "comment gray present");
    strip_ansi(out, stripped);
    ASSERT(strcmp(stripped, s3) == 0, "strip(highlight) comment == original");

    printf("  PASS: test_highlight\n");
}

int main(void) {
    printf("Running REPL helper tests...\n");
    test_classify();
    test_complete();
    test_highlight();
    printf("\n=== Results: %d/%d passed ===\n", tests_passed, tests_run);
    return (tests_passed == tests_run) ? 0 : 1;
}
