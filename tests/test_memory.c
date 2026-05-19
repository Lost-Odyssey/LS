/* test_memory.c — End-to-end memory management tests (JIT mode)
 *
 * Covers:
 *   BUG #1  — Global string variables freed at program exit
 *   BUG #2  — String / struct-field overwrite frees old value
 *   BUG #3  — array(string,N) and array(struct,N) cleaned up at scope exit
 *   BUG #4  — arr[i] = new_str frees old element
 *   TEMP    — Discarded string expressions (AST_EXPR_STMT) freed immediately
 *   CHAIN   — Chained method calls free intermediate temporaries
 *   BLOCK   — Block-scoped strings don't corrupt outer variables (double-free fix)
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
    free(cg.temp_string_slots);

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
   BUG #2 — String variable overwrite frees old value
   Strategy: reassign multiple times, verify final value is correct.
   If the old value were double-freed or use-after-freed, the heap
   would be corrupted and strcmp inside == would crash or mismatch.
   =========================================================== */

static void test_string_reassign(void) {
    int r = jit_eval(
        "fn main() -> int {\n"
        "    string s = \"hello\"\n"
        "    s = \"world\"\n"            /* static → static, no free needed */
        "    string dyn = s + \"!\"\n"  /* creates heap string */
        "    s = dyn\n"                 /* move dyn into s */
        "    s = s.upper()\n"           /* free old (\"world!\"), store \"WORLD!\" */
        "    if (s == \"WORLD!\") { return 1 }\n"
        "    return 0\n"
        "}\n"
    );
    ASSERT_EQ(r, 1);
    printf("  PASS: test_string_reassign\n");
    tests_passed++;
}

/* ===========================================================
   BUG #2 — Struct field string overwrite frees old value
   =========================================================== */

static void test_struct_field_string_overwrite(void) {
    int r = jit_eval(
        "struct Box { string val; }\n"
        "fn main() -> int {\n"
        "    Box b\n"
        "    b.val = \"hello\"\n"          /* static */
        "    b.val = b.val + \" world\"\n" /* dynamic; old static freed (no-op) */
        "    b.val = b.val.upper()\n"       /* free old dynamic, store new */
        "    if (b.val == \"HELLO WORLD\") { return 1 }\n"
        "    return 0\n"
        "}\n"
    );
    ASSERT_EQ(r, 1);
    printf("  PASS: test_struct_field_string_overwrite\n");
    tests_passed++;
}

/* ===========================================================
   BUG #4 — arr[i] = new_str frees old element
   =========================================================== */

static void test_array_string_elem_overwrite(void) {
    int r = jit_eval(
        "fn main() -> int {\n"
        "    array(string, 3) arr\n"
        "    arr[0] = \"hello\"\n"
        "    arr[1] = \"world\"\n"
        "    arr[2] = \"!\"\n"
        "    arr[0] = arr[0].upper()\n"   /* BUG #4: free old \"hello\", store \"HELLO\" */
        "    arr[1] = arr[1] + arr[2]\n"  /* BUG #4: free old \"world\", store \"world!\" */
        "    if (arr[0] == \"HELLO\" && arr[1] == \"world!\") { return 1 }\n"
        "    return 0\n"
        "}\n"
    );
    ASSERT_EQ(r, 1);
    printf("  PASS: test_array_string_elem_overwrite\n");
    tests_passed++;
}

/* ===========================================================
   BUG #3 — array(string,N) scope cleanup
   Verify that dynamic strings stored in an array survive until
   scope exit (not freed too early), and the values are correct.
   We use a helper function so its stack frame is fully torn down
   before we continue, exercising the scope-exit cleanup path.
   =========================================================== */

static void test_array_string_scope_cleanup(void) {
    int r = jit_eval(
        "fn sum_lengths() -> int {\n"
        "    array(string, 4) strs\n"
        "    strs[0] = \"hello\".upper()\n"   /* dynamic: \"HELLO\", len=5 */
        "    strs[1] = \"world\".lower()\n"   /* dynamic: \"world\", len=5 */
        "    strs[2] = strs[0] + strs[1]\n"  /* dynamic: \"HELLOworld\", len=10 */
        "    strs[3] = strs[2].substr(0, 5)\n" /* dynamic: \"HELLO\", len=5 */
        "    return strs[0].length + strs[1].length + strs[2].length + strs[3].length\n"
        "}\n"
        "fn main() -> int {\n"
        "    int total = sum_lengths()\n"
        "    return total\n"  /* expect 5+5+10+5 = 25 */
        "}\n"
    );
    ASSERT_EQ(r, 25);
    printf("  PASS: test_array_string_scope_cleanup\n");
    tests_passed++;
}

/* ===========================================================
   BUG #3 — array(struct,N): __drop called for each element
   Global counter incremented in __drop; verify count = array size.
   =========================================================== */

static void test_array_struct_drop(void) {
    int r = jit_eval(
        "int drop_count = 0\n"
        "\n"
        "struct Widget {\n"
        "    string label;\n"
        "    int id;\n"
        "}\n"
        "\n"
        "impl Widget {\n"
        "    fn __drop() {\n"
        "        drop_count = drop_count + 1\n"
        "    }\n"
        "}\n"
        "\n"
        "fn create_and_destroy() {\n"
        "    array(Widget, 4) ws\n"
        "    ws[0].label = \"alpha\"\n"
        "    ws[1].label = \"beta\"\n"
        "    ws[2].label = \"gamma\"\n"
        "    ws[3].label = \"delta\"\n"
        "    ws[0].id = 1\n"
        "    ws[1].id = 2\n"
        "    ws[2].id = 3\n"
        "    ws[3].id = 4\n"
        "}\n"  /* scope exit: 4 drops expected */
        "\n"
        "fn main() -> int {\n"
        "    create_and_destroy()\n"
        "    return drop_count\n"  /* expect 4 */
        "}\n"
    );
    ASSERT_EQ(r, 4);
    printf("  PASS: test_array_struct_drop\n");
    tests_passed++;
}

/* ===========================================================
   BUG #3 — array(struct,N): nested struct with string fields
   Each element has a string field; verify both the struct drop
   and the string field cleanup fire without corruption.
   =========================================================== */

static void test_array_struct_with_strings(void) {
    int r = jit_eval(
        "int drop_count = 0\n"
        "\n"
        "struct Person {\n"
        "    string name;\n"
        "    int age;\n"
        "}\n"
        "\n"
        "impl Person {\n"
        "    fn __drop() {\n"
        "        drop_count = drop_count + 1\n"
        "    }\n"
        "}\n"
        "\n"
        "fn run() -> int {\n"
        "    array(Person, 3) people\n"
        "    people[0].name = \"Alice\"\n"
        "    people[0].age = 30\n"
        "    people[1].name = \"Bob\"\n"
        "    people[1].age = 25\n"
        "    people[2].name = \"Carol\"\n"
        "    people[2].age = 28\n"
        "    int sum = people[0].age + people[1].age + people[2].age\n"
        "    return sum\n"  /* expect 83; drops happen on scope exit */
        "}\n"
        "\n"
        "fn main() -> int {\n"
        "    int age_sum = run()\n"
        "    if (drop_count != 3) { return -1 }\n"
        "    return age_sum\n"  /* expect 83 */
        "}\n"
    );
    ASSERT_EQ(r, 83);
    printf("  PASS: test_array_struct_with_strings\n");
    tests_passed++;
}

/* ===========================================================
   TEMP — Discarded dynamic strings freed immediately (AST_EXPR_STMT)
   If temps weren't freed the heap would eventually corrupt;
   we stress this by calling many discarded expressions and then
   verify a subsequent allocation still works correctly.
   =========================================================== */

static void test_temp_expr_cleanup(void) {
    int r = jit_eval(
        "fn main() -> int {\n"
        "    \"hello\".upper()\n"             /* discard dynamic string */
        "    \"world\".lower()\n"             /* discard dynamic string */
        "    \"  hi  \".trim()\n"             /* discard dynamic string */
        "    \"abc\".replace(\"b\", \"B\")\n" /* discard dynamic string */
        "    \"hello\" + \" world\"\n"        /* discard concatenation */
        "    string s = \"alive\".upper()\n"  /* this one must survive */
        "    if (s == \"ALIVE\") { return 1 }\n"
        "    return 0\n"
        "}\n"
    );
    ASSERT_EQ(r, 1);
    printf("  PASS: test_temp_expr_cleanup\n");
    tests_passed++;
}

/* ===========================================================
   CHAIN — Intermediate temps in chained method calls are freed
   =========================================================== */

static void test_chained_methods(void) {
    int r = jit_eval(
        "fn main() -> int {\n"
        "    string s = \"  HELLO WORLD  \".trim().lower()\n"
        "    if (s == \"hello world\") { return 1 }\n"
        "    return 0\n"
        "}\n"
    );
    ASSERT_EQ(r, 1);
    printf("  PASS: test_chained_methods\n");
    tests_passed++;
}

/* ===========================================================
   BLOCK — Block-scoped strings don't corrupt outer variables
   (pre-existing double-free bug: AST_BLOCK emit_cleanup_to
    was traversing the full scope chain instead of stopping at
    the block's own scope)
   =========================================================== */

static void test_block_scope_no_double_free(void) {
    /* Outer string + if block that creates its own inner string;
       both should survive until their respective scopes exit. */
    int r = jit_eval(
        "fn main() -> int {\n"
        "    string src = \"hello\"\n"
        "    string dup = src.copy()\n"
        "    int ok = 0\n"
        "    if (src == dup) {\n"       /* if block: inner scope, no outer free */
        "        string inner = dup.upper()\n"
        "        if (inner == \"HELLO\") { ok = 1 }\n"
        "    }\n"
        "    if (dup == \"hello\") { ok = ok + 1 }\n"  /* dup still valid */
        "    return ok\n"  /* expect 2 */
        "}\n"
    );
    ASSERT_EQ(r, 2);
    printf("  PASS: test_block_scope_no_double_free\n");
    tests_passed++;
}

/* ===========================================================
   BUG #1 — Global string variables cleaned up at program exit
   Verify global strings produce correct values (program would
   crash or corrupt if globals were double-freed or leaked badly).
   =========================================================== */

static void test_global_string_cleanup(void) {
    int r = jit_eval(
        "string prefix = \"Hello\"\n"
        "string suffix = \" World\"\n"
        "\n"
        "fn greet() -> string {\n"
        "    return prefix + suffix\n"
        "}\n"
        "\n"
        "fn main() -> int {\n"
        "    string g = greet()\n"
        "    if (g == \"Hello World\") { return 1 }\n"
        "    return 0\n"
        "}\n"
    );
    ASSERT_EQ(r, 1);
    printf("  PASS: test_global_string_cleanup\n");
    tests_passed++;
}

/* ===========================================================
   COMBINED — Stress test mixing all features
   =========================================================== */

static void test_combined_memory_stress(void) {
    int r = jit_eval(
        "int total_drops = 0\n"
        "\n"
        "struct Node {\n"
        "    string data;\n"
        "    int value;\n"
        "}\n"
        "\n"
        "impl Node {\n"
        "    fn __drop() {\n"
        "        total_drops = total_drops + 1\n"
        "    }\n"
        "    static fn make(string d, int v) -> Node {\n"
        "        Node n\n"
        "        n.data = d\n"
        "        n.value = v\n"
        "        return n\n"
        "    }\n"
        "}\n"
        "\n"
        "fn process() -> int {\n"
        "    array(Node, 3) nodes\n"
        "    nodes[0].data = \"first\".upper()\n"   /* dynamic */
        "    nodes[1].data = \"second\".upper()\n"  /* dynamic */
        "    nodes[2].data = \"third\".upper()\n"   /* dynamic */
        "    nodes[0].value = 10\n"
        "    nodes[1].value = 20\n"
        "    nodes[2].value = 30\n"
        "    nodes[1].data = nodes[0].data + nodes[2].data\n"  /* BUG#4+#2: old freed */
        "    int sum = nodes[0].value + nodes[1].value + nodes[2].value\n"
        "    return sum\n"   /* expect 60; 3 drops on scope exit */
        "}\n"
        "\n"
        "fn main() -> int {\n"
        "    int s = process()\n"
        "    if (s != 60) { return -1 }\n"
        "    if (total_drops != 3) { return -2 }\n"
        "    return 1\n"
        "}\n"
    );
    ASSERT_EQ(r, 1);
    printf("  PASS: test_combined_memory_stress\n");
    tests_passed++;
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

    test_string_reassign();
    test_struct_field_string_overwrite();
    test_array_string_elem_overwrite();
    test_array_string_scope_cleanup();
    test_array_struct_drop();
    test_array_struct_with_strings();
    test_temp_expr_cleanup();
    test_chained_methods();
    test_block_scope_no_double_free();
    test_global_string_cleanup();
    test_combined_memory_stress();

    printf("\n=== vec(T) Tests ===\n");
    test_vec_basic();
    test_vec_string_elements();
    test_vec_clear();
    test_vec_reserve();
    test_vec_for_in();
    test_vec_index_write();
    test_vec_grow();
    test_vec_pop();
    test_vec_pop_string();
    test_vec_string_index_write();
    test_vec_for_in_string();
    test_vec_in_function();
    test_vec_float();
    test_vec_multiple_in_scope();
    test_vec_clear_repush();
    test_vec_in_if_branch();
    test_vec_empty_no_alloc();
    test_vec_global();

    printf("\n=== Results: %d/29 passed ===\n", tests_passed);
    return 0;
}
