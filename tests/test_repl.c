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
    ASSERT(repl_classify("import std.sys.time as T") == REPL_IMPORT, "import as → IMPORT");
    ASSERT(repl_classify("def f() {}") == REPL_DECL, "def → DECL");
    ASSERT(repl_classify("struct P { int x }") == REPL_DECL, "struct → DECL");
    ASSERT(repl_classify("enum E { A }") == REPL_DECL, "enum → DECL");
    ASSERT(repl_classify("interface T { def d(&self) }") == REPL_DECL, "interface → DECL");
    ASSERT(repl_classify("methods P {}") == REPL_DECL, "methods → DECL");
    ASSERT(repl_classify("type F = int") == REPL_DECL, "type → DECL");
    ASSERT(repl_classify("int x = 1") == REPL_VAR, "int x → VAR");
    ASSERT(repl_classify("Str s = \"hi\"") == REPL_VAR, "Str s → VAR");
    ASSERT(repl_classify("Point p = mk()") == REPL_VAR, "user type → VAR");
    /* generic / container-typed var decls: `IDENT(...) IDENT` → VAR */
    ASSERT(repl_classify("Vec(int) v = []") == REPL_VAR, "Vec(int) v → VAR");
    ASSERT(repl_classify("Result(JsonValue, Str) r = p()") == REPL_VAR,
           "Result(...) r → VAR");
    ASSERT(repl_classify("Map(int, Vec(int)) m = {}") == REPL_VAR,
           "nested generic Map → VAR");
    /* a call/ctor expression has no trailing identifier after `)` → EXPR */
    ASSERT(repl_classify("@print(x)") == REPL_EXPR, "call → EXPR");
    ASSERT(repl_classify("foo(1, 2)") == REPL_EXPR, "multi-arg call → EXPR");
    ASSERT(repl_classify("Some(42)") == REPL_EXPR, "enum ctor call → EXPR");
    ASSERT(repl_classify("x = 5") == REPL_EXPR, "assign existing → EXPR");
    ASSERT(repl_classify("1 + 2") == REPL_EXPR, "arith → EXPR");
    printf("  PASS: test_classify\n");
}

/* ---- POD-scalar var-decl detection (REPL global persistence) ---- */

static void test_pod_scalar(void) {
    /* scalar-typed decls → name returned */
    char *n;
    n = repl_pod_scalar_var_name("int i = 1");      ASSERT(n && strcmp(n,"i")==0, "int i"); free(n);
    n = repl_pod_scalar_var_name("f64 x = 2.5");    ASSERT(n && strcmp(n,"x")==0, "f64 x"); free(n);
    n = repl_pod_scalar_var_name("bool flag=true"); ASSERT(n && strcmp(n,"flag")==0, "bool flag"); free(n);
    n = repl_pod_scalar_var_name("i64 big = 9");    ASSERT(n && strcmp(n,"big")==0, "i64 big"); free(n);
    n = repl_pod_scalar_var_name("char c = 'a'");   ASSERT(n && strcmp(n,"c")==0, "char c"); free(n);
    /* non-POD / non-decl → NULL (wrapper-replay path) */
    ASSERT(repl_pod_scalar_var_name("Str s = \"x\"") == NULL, "Str → NULL");
    ASSERT(repl_pod_scalar_var_name("Vec(int) v = []") == NULL, "Vec → NULL");
    ASSERT(repl_pod_scalar_var_name("Point p = mk()") == NULL, "user type → NULL");
    ASSERT(repl_pod_scalar_var_name("i += 1") == NULL, "assignment → NULL");
    ASSERT(repl_pod_scalar_var_name("@print(i)") == NULL, "call → NULL");
    ASSERT(repl_pod_scalar_var_name("int") == NULL, "bare type, no name → NULL");
    printf("  PASS: test_pod_scalar\n");
}

/* ---- persisted var-decl detection (Phase 2: POD + container/struct/user) ---- */

static void test_persisted(void) {
    char *n;
    /* POD scalars still detected */
    n = repl_persisted_var_name("int i = 1");        ASSERT(n && strcmp(n,"i")==0, "int i"); free(n);
    n = repl_persisted_var_name("char c = 'a'");     ASSERT(n && strcmp(n,"c")==0, "char c"); free(n);
    /* containers / structs / user types now ALSO detected (Phase 2) */
    n = repl_persisted_var_name("Str s = \"x\"");    ASSERT(n && strcmp(n,"s")==0, "Str s"); free(n);
    n = repl_persisted_var_name("Vec(int) v = []");  ASSERT(n && strcmp(n,"v")==0, "Vec v"); free(n);
    n = repl_persisted_var_name("Map(int, Vec(int)) m = {}");
                                                     ASSERT(n && strcmp(n,"m")==0, "Map m"); free(n);
    n = repl_persisted_var_name("Point p = mk()");   ASSERT(n && strcmp(n,"p")==0, "Point p"); free(n);
    n = repl_persisted_var_name("Vec(int) w");       ASSERT(n && strcmp(n,"w")==0, "bare Vec w"); free(n);
    /* non-decls → NULL (wrapper-replay / expr path) */
    ASSERT(repl_persisted_var_name("i += 1") == NULL, "assignment → NULL");
    ASSERT(repl_persisted_var_name("@print(i)") == NULL, "call → NULL");
    ASSERT(repl_persisted_var_name("v.push(4)") == NULL, "method call → NULL");
    ASSERT(repl_persisted_var_name("Some(42)") == NULL, "enum ctor → NULL");
    ASSERT(repl_persisted_var_name("int") == NULL, "bare type, no name → NULL");
    printf("  PASS: test_persisted\n");
}

/* ---- completeness ---- */

static void test_complete(void) {
    /* complete */
    ASSERT(repl_input_is_complete("1 + 2"), "arith complete");
    ASSERT(repl_input_is_complete("@print(x)"), "balanced call complete");
    ASSERT(repl_input_is_complete("def f() { return 1 }"), "full def complete");
    ASSERT(repl_input_is_complete("[1, 2, 3]"), "balanced array complete");
    ASSERT(repl_input_is_complete("\"a string\""), "closed string complete");
    /* incomplete */
    ASSERT(!repl_input_is_complete("def f() {"), "open brace incomplete");
    ASSERT(!repl_input_is_complete("@print("), "open paren incomplete");
    ASSERT(!repl_input_is_complete("[1,"), "open bracket incomplete");
    ASSERT(!repl_input_is_complete("1 +"), "trailing + incomplete");
    ASSERT(!repl_input_is_complete("x ="), "trailing = incomplete");
    ASSERT(!repl_input_is_complete("\"abc"), "unterminated string incomplete");
    ASSERT(!repl_input_is_complete("foo("), "open call incomplete");
    /* a // comment does not start a bracket; line is complete */
    ASSERT(repl_input_is_complete("1 + 2 // adds"), "trailing comment complete");
    printf("  PASS: test_complete\n");
}

static void test_continuation_prompt(void) {
    char buf[80];
    /* open brace → prompt hints the unclosed { */
    repl_continuation_prompt("def f() {", buf, sizeof(buf));
    ASSERT(strcmp(buf, "..{> ") == 0, "open brace → ..{>");
    /* nested open delimiters, innermost on the right */
    repl_continuation_prompt("def f() { g(", buf, sizeof(buf));
    ASSERT(strcmp(buf, "..{(> ") == 0, "brace+paren → ..{(>");
    /* a closed inner pair leaves only the outer brace */
    repl_continuation_prompt("def f() { x[0]", buf, sizeof(buf));
    ASSERT(strcmp(buf, "..{> ") == 0, "closed inner leaves ..{>");
    /* unterminated string / char / block comment */
    repl_continuation_prompt("\"abc", buf, sizeof(buf));
    ASSERT(strcmp(buf, "..\"> ") == 0, "open string → ..\">");
    repl_continuation_prompt("'a", buf, sizeof(buf));
    ASSERT(strcmp(buf, "..'> ") == 0, "open char → ..'>");
    repl_continuation_prompt("/* note", buf, sizeof(buf));
    ASSERT(strcmp(buf, "..*/> ") == 0, "open block comment → ..*/>");
    /* trailing continuation operator: balanced, default prompt */
    repl_continuation_prompt("1 +", buf, sizeof(buf));
    ASSERT(strcmp(buf, "...> ") == 0, "trailing op → ...>");
    printf("  PASS: test_continuation_prompt\n");
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
    const char *src = "def add(int a) -> int { return a + 42 }";
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
    test_pod_scalar();
    test_persisted();
    test_complete();
    test_continuation_prompt();
    test_highlight();
    printf("\n=== Results: %d/%d passed ===\n", tests_passed, tests_run);
    return (tests_passed == tests_run) ? 0 : 1;
}
