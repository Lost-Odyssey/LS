/* test_memory.c — End-to-end memory management tests (JIT mode)
 *
 * P5-4 S-1: the builtin-string memory e2e suite was removed with the builtin
 * string type itself. Equivalent Str-side coverage lives in the registered
 * samples test_mem_m3/m4/m4_5/overhaul + str_p2/str_p3 (memcheck 0/0/0).
 *
 * Strategy:
 *   - Correctness: main() returns a value encoding pass/fail
 *   - Drop counting: global int counter incremented by __drop proves destructor
 *     is called the right number of times
 *   - All tests run in JIT to exercise the full stack (parse→check→codegen→JIT)
 */

#include "jit.h"
#include "parser.h"
#include "checker.h"
#include "codegen.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ---- Assert helpers ---- */

#define ASSERT(cond, msg) do { \
    if (!(cond)) { \
        fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, msg); \
        exit(1); \
    } \
} while(0)

#define ASSERT_EQ(a, b) ASSERT((a) == (b), #a " != " #b)

static int tests_passed = 0;

/* ---- JIT helper: compile source, run main(), return its int result ---- */

static int jit_eval(const char *source) {
    AstNode *ast = parse(source, "<mem-test>");
    ASSERT(ast != NULL, "parse failed");
    ASSERT(checker_check(ast, "<mem-test>", NULL, NULL), "type check failed");

    JitEngine engine;
    ASSERT_EQ(jit_init(&engine), 0);

    LLVMContextRef ctx = LLVMOrcThreadSafeContextGetContext(engine.ts_context);
    CodegenContext cg;
    memset(&cg, 0, sizeof(CodegenContext));
    cg.context  = ctx;
    cg.module   = LLVMModuleCreateWithNameInContext("mem_test", ctx);
    cg.builder  = LLVMCreateBuilderInContext(ctx);
    cg.extern_builtins = true;

    const char *dl = LLVMOrcLLJITGetDataLayoutStr(engine.jit);
    LLVMSetDataLayout(cg.module, dl);
    const char *triple = LLVMOrcLLJITGetTripleString(engine.jit);
    LLVMSetTarget(cg.module, triple);

    ASSERT_EQ(codegen_compile(&cg, ast, NULL), 0);

    LLVMModuleRef module = cg.module;
    LLVMDisposeBuilder(cg.builder);
    free(cg.struct_types);

    ASSERT_EQ(jit_add_module(&engine, module), 0);

    uint64_t addr = jit_lookup(&engine, "main");
    ASSERT(addr != 0, "main not found in JIT");

    typedef int (*MainFn)(void);
    int result = ((MainFn)(uintptr_t)addr)();

    jit_destroy(&engine);
    ast_free(ast);
    return result;
}












/* ===========================================================
   VEC #1 — Basic push / index / length / scope cleanup
   =========================================================== */

static void test_vec_basic(void) {
    int r = jit_eval(
        "fn main() -> int {\n"
        "    vec(int) v\n"
        "    v.push(10)\n"
        "    v.push(20)\n"
        "    v.push(30)\n"
        "    if (v.length != 3) { return -1 }\n"
        "    if (v[0] != 10) { return -2 }\n"
        "    if (v[1] != 20) { return -3 }\n"
        "    if (v[2] != 30) { return -4 }\n"
        "    return 1\n"
        "}\n"
    );
    ASSERT_EQ(r, 1);
    printf("  PASS: test_vec_basic\n");
    tests_passed++;
}

/* ===========================================================
   VEC #2 — String element push / scope cleanup (no leak/crash)
   =========================================================== */

static void test_vec_string_elements(void) {
    int r = jit_eval(
        "fn main() -> int {\n"
        "    vec(string) v\n"
        "    v.push(\"hello\")\n"
        "    v.push(\"world\")\n"
        "    v.push(\"hello\".upper())\n"  /* dynamic string */
        "    if (v.length != 3) { return -1 }\n"
        "    if (v[2] == \"HELLO\") { return 1 }\n"
        "    return 0\n"
        "}\n"   /* scope exit: free dynamic elem at [2], statics at [0][1] */
    );
    ASSERT_EQ(r, 1);
    printf("  PASS: test_vec_string_elements\n");
    tests_passed++;
}

/* ===========================================================
   VEC #3 — clear() resets length; buffer is reusable after clear
   =========================================================== */

static void test_vec_clear(void) {
    int r = jit_eval(
        "fn main() -> int {\n"
        "    vec(int) v\n"
        "    v.push(1)\n"
        "    v.push(2)\n"
        "    v.push(3)\n"
        "    v.clear()\n"
        "    if (v.length != 0) { return -1 }\n"
        "    v.push(99)\n"
        "    if (v[0] != 99) { return -2 }\n"
        "    if (v.length != 1) { return -3 }\n"
        "    return 1\n"
        "}\n"
    );
    ASSERT_EQ(r, 1);
    printf("  PASS: test_vec_clear\n");
    tests_passed++;
}

/* ===========================================================
   VEC #4 — reserve() pre-allocates capacity without changing length
   =========================================================== */

static void test_vec_reserve(void) {
    int r = jit_eval(
        "fn main() -> int {\n"
        "    vec(int) v\n"
        "    v.reserve(100)\n"
        "    if (v.length != 0) { return -1 }\n"
        "    if (v.capacity < 100) { return -2 }\n"
        "    v.push(42)\n"
        "    if (v.length != 1) { return -3 }\n"
        "    if (v[0] != 42) { return -4 }\n"
        "    return 1\n"
        "}\n"
    );
    ASSERT_EQ(r, 1);
    printf("  PASS: test_vec_reserve\n");
    tests_passed++;
}

/* ===========================================================
   VEC #5 — for-in iteration over vec(int)
   =========================================================== */

static void test_vec_for_in(void) {
    int r = jit_eval(
        "fn main() -> int {\n"
        "    vec(int) v\n"
        "    v.push(1)\n"
        "    v.push(2)\n"
        "    v.push(3)\n"
        "    v.push(4)\n"
        "    int sum = 0\n"
        "    for x in v {\n"
        "        sum = sum + x\n"
        "    }\n"
        "    if (sum == 10) { return 1 }\n"
        "    return 0\n"
        "}\n"
    );
    ASSERT_EQ(r, 1);
    printf("  PASS: test_vec_for_in\n");
    tests_passed++;
}

/* ===========================================================
   VEC #6 — vec index write (v[i] = val)
   =========================================================== */

static void test_vec_index_write(void) {
    int r = jit_eval(
        "fn main() -> int {\n"
        "    vec(int) v\n"
        "    v.push(0)\n"
        "    v.push(0)\n"
        "    v.push(0)\n"
        "    v[0] = 100\n"
        "    v[1] = 200\n"
        "    v[2] = 300\n"
        "    if (v[0] != 100) { return -1 }\n"
        "    if (v[1] != 200) { return -2 }\n"
        "    if (v[2] != 300) { return -3 }\n"
        "    return 1\n"
        "}\n"
    );
    ASSERT_EQ(r, 1);
    printf("  PASS: test_vec_index_write\n");
    tests_passed++;
}

/* ===========================================================
   VEC #7 — vec grows beyond initial capacity (triggers realloc)
   =========================================================== */

static void test_vec_grow(void) {
    int r = jit_eval(
        "fn main() -> int {\n"
        "    vec(int) v\n"
        "    int i = 0\n"
        "    for i in 0..20 {\n"
        "        v.push(i)\n"
        "    }\n"
        "    if (v.length != 20) { return -1 }\n"
        "    int sum = 0\n"
        "    for x in v {\n"
        "        sum = sum + x\n"
        "    }\n"
        "    if (sum != 190) { return -2 }\n"  /* 0+1+...+19 = 190 */
        "    return 1\n"
        "}\n"
    );
    ASSERT_EQ(r, 1);
    printf("  PASS: test_vec_grow\n");
    tests_passed++;
}

/* ===========================================================
   VEC #8 — pop() removes last element, returns correct length
   =========================================================== */

static void test_vec_pop(void) {
    int r = jit_eval(
        "fn main() -> int {\n"
        "    vec(int) v\n"
        "    v.push(10)\n"
        "    v.push(20)\n"
        "    v.push(30)\n"
        "    v.pop()\n"
        "    if (v.length != 2) { return -1 }\n"
        "    if (v[0] != 10) { return -2 }\n"
        "    if (v[1] != 20) { return -3 }\n"
        "    v.pop()\n"
        "    if (v.length != 1) { return -4 }\n"
        "    v.pop()\n"
        "    if (v.length != 0) { return -5 }\n"
        "    return 1\n"
        "}\n"
    );
    ASSERT_EQ(r, 1);
    printf("  PASS: test_vec_pop\n");
    tests_passed++;
}

/* ===========================================================
   VEC #9 — pop() on string elements frees heap strings
   =========================================================== */

static void test_vec_pop_string(void) {
    int r = jit_eval(
        "fn main() -> int {\n"
        "    vec(string) v\n"
        "    v.push(\"hello\".upper())\n"   /* HELLO — heap */
        "    v.push(\"world\".upper())\n"   /* WORLD — heap */
        "    v.push(\"foo\")\n"             /* static */
        "    v.pop()\n"                     /* removes static \"foo\" */
        "    v.pop()\n"                     /* removes heap WORLD, frees it */
        "    if (v.length != 1) { return -1 }\n"
        "    if (v[0] == \"HELLO\") { return 1 }\n"
        "    return 0\n"
        "}\n"   /* scope exit: HELLO heap freed */
    );
    ASSERT_EQ(r, 1);
    printf("  PASS: test_vec_pop_string\n");
    tests_passed++;
}

/* ===========================================================
   VEC #10 — string index write frees old element
   =========================================================== */

static void test_vec_string_index_write(void) {
    int r = jit_eval(
        "fn main() -> int {\n"
        "    vec(string) v\n"
        "    v.push(\"hello\".upper())\n"   /* HELLO — heap */
        "    v.push(\"world\".upper())\n"   /* WORLD — heap */
        "    v[0] = \"replaced\"\n"         /* frees HELLO, stores static */
        "    v[1] = \"new\".upper()\n"      /* frees WORLD, stores NEW */
        "    if (v[0] != \"replaced\") { return -1 }\n"
        "    if (v[1] != \"NEW\") { return -2 }\n"
        "    return 1\n"
        "}\n"
    );
    ASSERT_EQ(r, 1);
    printf("  PASS: test_vec_string_index_write\n");
    tests_passed++;
}

/* ===========================================================
   VEC #11 — for-in over vec(string)
   =========================================================== */

static void test_vec_for_in_string(void) {
    int r = jit_eval(
        "fn main() -> int {\n"
        "    vec(string) v\n"
        "    v.push(\"hello\")\n"
        "    v.push(\"world\")\n"
        "    v.push(\"foo\")\n"
        "    int count = 0\n"
        "    for s in v {\n"
        "        count = count + s.length\n"
        "    }\n"
        /* hello=5, world=5, foo=3 → 13 */
        "    if (count == 13) { return 1 }\n"
        "    return 0\n"
        "}\n"
    );
    ASSERT_EQ(r, 1);
    printf("  PASS: test_vec_for_in_string\n");
    tests_passed++;
}

/* ===========================================================
   VEC #12 — nested vec inside function, multiple calls
   =========================================================== */

static void test_vec_in_function(void) {
    int r = jit_eval(
        "fn sum_vec(vec(int) v) -> int {\n"
        "    int total = 0\n"
        "    for x in v {\n"
        "        total = total + x\n"
        "    }\n"
        "    return total\n"
        "}\n"
        "fn make_vec() -> int {\n"
        "    vec(int) v\n"
        "    v.push(1)\n"
        "    v.push(2)\n"
        "    v.push(3)\n"
        "    v.push(4)\n"
        "    v.push(5)\n"
        "    return sum_vec(v)\n"
        "}\n"
        "fn main() -> int {\n"
        "    int a = make_vec()\n"
        "    int b = make_vec()\n"
        "    if (a == 15 && b == 15) { return 1 }\n"
        "    return 0\n"
        "}\n"
    );
    ASSERT_EQ(r, 1);
    printf("  PASS: test_vec_in_function\n");
    tests_passed++;
}

/* ===========================================================
   VEC #13 — vec(f64) float elements
   =========================================================== */

static void test_vec_float(void) {
    int r = jit_eval(
        "fn main() -> int {\n"
        "    vec(f64) v\n"
        "    v.push(1.5)\n"
        "    v.push(2.5)\n"
        "    v.push(3.0)\n"
        "    f64 sum = 0.0\n"
        "    for x in v {\n"
        "        sum = sum + x\n"
        "    }\n"
        "    if (sum == 7.0) { return 1 }\n"
        "    return 0\n"
        "}\n"
    );
    ASSERT_EQ(r, 1);
    printf("  PASS: test_vec_float\n");
    tests_passed++;
}

/* ===========================================================
   VEC #14 — multiple vecs in same scope, each cleaned up
   =========================================================== */

static void test_vec_multiple_in_scope(void) {
    int r = jit_eval(
        "fn main() -> int {\n"
        "    vec(int) a\n"
        "    vec(int) b\n"
        "    vec(int) c\n"
        "    for i in 0..5 { a.push(i) }\n"
        "    for i in 0..3 { b.push(i * 10) }\n"
        "    for i in 0..2 { c.push(i * 100) }\n"
        "    if (a.length != 5) { return -1 }\n"
        "    if (b.length != 3) { return -2 }\n"
        "    if (c.length != 2) { return -3 }\n"
        "    int sa = 0\n"
        "    int sb = 0\n"
        "    int sc = 0\n"
        "    for x in a { sa = sa + x }\n"
        "    for x in b { sb = sb + x }\n"
        "    for x in c { sc = sc + x }\n"
        /* 0+1+2+3+4=10, 0+10+20=30, 0+100=100 */
        "    if (sa != 10) { return -4 }\n"
        "    if (sb != 30) { return -5 }\n"
        "    if (sc != 100) { return -6 }\n"
        "    return 1\n"
        "}\n"
    );
    ASSERT_EQ(r, 1);
    printf("  PASS: test_vec_multiple_in_scope\n");
    tests_passed++;
}

/* ===========================================================
   VEC #15 — vec clear then repush (reuse buffer, no leak)
   =========================================================== */

static void test_vec_clear_repush(void) {
    int r = jit_eval(
        "fn main() -> int {\n"
        "    vec(string) v\n"
        "    v.push(\"a\".upper())\n"    /* A — heap */
        "    v.push(\"b\".upper())\n"    /* B — heap */
        "    v.clear()\n"                /* frees A and B */
        "    v.push(\"x\")\n"            /* static */
        "    v.push(\"y\".upper())\n"    /* Y — heap */
        "    if (v.length != 2) { return -1 }\n"
        "    if (v[0] != \"x\") { return -2 }\n"
        "    if (v[1] != \"Y\") { return -3 }\n"
        "    return 1\n"
        "}\n"   /* scope exit: frees heap Y, x is static */
    );
    ASSERT_EQ(r, 1);
    printf("  PASS: test_vec_clear_repush\n");
    tests_passed++;
}

/* ===========================================================
   VEC #16 — vec in if/else branches (scoped)
   =========================================================== */

static void test_vec_in_if_branch(void) {
    int r = jit_eval(
        "fn main() -> int {\n"
        "    int result = 0\n"
        "    if (1 == 1) {\n"
        "        vec(int) v\n"
        "        v.push(7)\n"
        "        v.push(8)\n"
        "        v.push(9)\n"
        "        for x in v { result = result + x }\n"
        "    }\n"       /* vec freed on if-block scope exit */
        "    if (result == 24) { return 1 }\n"
        "    return 0\n"
        "}\n"
    );
    ASSERT_EQ(r, 1);
    printf("  PASS: test_vec_in_if_branch\n");
    tests_passed++;
}

/* ===========================================================
   VEC #17 — empty vec never allocated (cap=0), safe to drop
   =========================================================== */

static void test_vec_empty_no_alloc(void) {
    int r = jit_eval(
        "fn main() -> int {\n"
        "    vec(int) v\n"       /* never pushed — cap=0, data=NULL */
        "    if (v.length != 0) { return -1 }\n"
        "    if (v.capacity != 0) { return -2 }\n"
        "    return 1\n"
        "}\n"   /* scope exit: cap==0 → no free */
    );
    ASSERT_EQ(r, 1);
    printf("  PASS: test_vec_empty_no_alloc\n");
    tests_passed++;
}

/* ===========================================================
   VEC #18 — global vec variable
   =========================================================== */

static void test_vec_global(void) {
    int r = jit_eval(
        "vec(int) g_numbers\n"
        "\n"
        "fn fill() {\n"
        "    g_numbers.push(100)\n"
        "    g_numbers.push(200)\n"
        "    g_numbers.push(300)\n"
        "}\n"
        "\n"
        "fn read() -> int {\n"
        "    int s = 0\n"
        "    for x in g_numbers { s = s + x }\n"
        "    return s\n"
        "}\n"
        "\n"
        "fn main() -> int {\n"
        "    fill()\n"
        "    int total = read()\n"
        "    if (total == 600) { return 1 }\n"
        "    return 0\n"
        "}\n"
    );
    ASSERT_EQ(r, 1);
    printf("  PASS: test_vec_global\n");
    tests_passed++;
}

/* ---- Main ---- */

int main(void) {
    printf("=== Memory Management E2E Tests ===\n");
    printf("  skipped: builtin-string memory e2e removed in P5-4 S-1 (Str-side\n");
    printf("  coverage: test_mem_m3/m4/m4_5/overhaul + str_p2/str_p3)\n");


    printf("\n=== vec(T) Tests ===\n");
    printf("  skipped: builtin vec(T) syntax is unreachable after Phase 3 P3-1\n");

    printf("\n=== Results: %d passed ===\n", tests_passed);
    return 0;
}
