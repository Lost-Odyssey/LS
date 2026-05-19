/* test_jit.c — JIT engine integration tests */
#include "jit.h"
#include "parser.h"
#include "checker.h"
#include "codegen.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define ASSERT(cond, msg) do { \
    if (!(cond)) { \
        fprintf(stderr, "FAIL: %s:%d: %s\n", __FILE__, __LINE__, msg); \
        exit(1); \
    } \
} while(0)

#define ASSERT_EQ(a, b) ASSERT((a) == (b), #a " != " #b)

static int tests_passed = 0;

/* Helper: compile source, add to JIT, look up and call main(), return its result */
static int jit_eval_main(const char *source) {
    AstNode *ast = parse(source, "<test>");
    ASSERT(ast != NULL, "parse failed");
    ASSERT(checker_check(ast, "<test>", NULL, NULL), "type check failed");

    JitEngine engine;
    ASSERT_EQ(jit_init(&engine), 0);

    /* Build module using JIT context */
    LLVMContextRef ctx = LLVMOrcThreadSafeContextGetContext(engine.ts_context);
    CodegenContext cg;
    memset(&cg, 0, sizeof(CodegenContext));
    cg.context = ctx;
    cg.module = LLVMModuleCreateWithNameInContext("test", ctx);
    cg.builder = LLVMCreateBuilderInContext(ctx);
    cg.extern_builtins = true; /* builtins already defined by jit_init */

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
    MainFn fn = (MainFn)(uintptr_t)addr;
    int result = fn();

    jit_destroy(&engine);
    ast_free(ast);
    return result;
}

/* ---- Tests ---- */

static void test_jit_init_destroy(void) {
    JitEngine engine;
    ASSERT_EQ(jit_init(&engine), 0);
    ASSERT(engine.initialized, "engine not initialized");
    ASSERT(engine.jit != NULL, "jit handle is NULL");
    ASSERT(engine.main_dylib != NULL, "main_dylib is NULL");
    ASSERT(engine.ts_context != NULL, "ts_context is NULL");
    jit_destroy(&engine);
    ASSERT(!engine.initialized, "engine should be destroyed");
    printf("  PASS: test_jit_init_destroy\n");
    tests_passed++;
}

static void test_jit_simple_main(void) {
    int result = jit_eval_main(
        "fn main() -> int {\n"
        "    return 42\n"
        "}\n"
    );
    ASSERT_EQ(result, 42);
    printf("  PASS: test_jit_simple_main\n");
    tests_passed++;
}

static void test_jit_arithmetic(void) {
    int result = jit_eval_main(
        "fn main() -> int {\n"
        "    int a = 10\n"
        "    int b = 20\n"
        "    return a + b\n"
        "}\n"
    );
    ASSERT_EQ(result, 30);
    printf("  PASS: test_jit_arithmetic\n");
    tests_passed++;
}

static void test_jit_function_call(void) {
    int result = jit_eval_main(
        "fn add(int a, int b) -> int {\n"
        "    return a + b\n"
        "}\n"
        "fn main() -> int {\n"
        "    return add(17, 25)\n"
        "}\n"
    );
    ASSERT_EQ(result, 42);
    printf("  PASS: test_jit_function_call\n");
    tests_passed++;
}

static void test_jit_recursion(void) {
    int result = jit_eval_main(
        "fn factorial(int n) -> int {\n"
        "    if (n < 2) { return 1 }\n"
        "    return n * factorial(n - 1)\n"
        "}\n"
        "fn main() -> int {\n"
        "    return factorial(5)\n"
        "}\n"
    );
    ASSERT_EQ(result, 120);
    printf("  PASS: test_jit_recursion\n");
    tests_passed++;
}

static void test_jit_if_else(void) {
    int result = jit_eval_main(
        "fn max(int a, int b) -> int {\n"
        "    if (a > b) { return a }\n"
        "    else { return b }\n"
        "}\n"
        "fn main() -> int {\n"
        "    return max(42, 17)\n"
        "}\n"
    );
    ASSERT_EQ(result, 42);
    printf("  PASS: test_jit_if_else\n");
    tests_passed++;
}

static void test_jit_while_loop(void) {
    int result = jit_eval_main(
        "fn main() -> int {\n"
        "    int sum = 0\n"
        "    int i = 0\n"
        "    while (i < 10) {\n"
        "        sum = sum + i\n"
        "        i = i + 1\n"
        "    }\n"
        "    return sum\n"
        "}\n"
    );
    ASSERT_EQ(result, 45);
    printf("  PASS: test_jit_while_loop\n");
    tests_passed++;
}

static void test_jit_match(void) {
    int result = jit_eval_main(
        "fn classify(int n) -> int {\n"
        "    match n {\n"
        "        0 => 100,\n"
        "        1 => 200,\n"
        "        _ => 300,\n"
        "    }\n"
        "}\n"
        "fn main() -> int {\n"
        "    return classify(0) + classify(1) + classify(99)\n"
        "}\n"
    );
    ASSERT_EQ(result, 600);
    printf("  PASS: test_jit_match\n");
    tests_passed++;
}

static void test_jit_fibonacci(void) {
    int result = jit_eval_main(
        "fn fibonacci(int n) -> int {\n"
        "    if (n < 2) { return n }\n"
        "    return fibonacci(n - 1) + fibonacci(n - 2)\n"
        "}\n"
        "fn main() -> int {\n"
        "    return fibonacci(10)\n"
        "}\n"
    );
    ASSERT_EQ(result, 55);
    printf("  PASS: test_jit_fibonacci\n");
    tests_passed++;
}

static void test_jit_hash_fn(void) {
    const char *source =
        "fn foo(int x) -> int { return x * 2 }\n"
        "fn main() -> int { return 0 }\n";
    AstNode *ast = parse(source, "<test>");
    ASSERT(ast != NULL, "parse failed");

    /* Find foo declaration */
    AstNode *foo = NULL;
    for (int i = 0; i < ast->as.program.decl_count; i++) {
        if (ast->as.program.decls[i]->kind == AST_FN_DECL &&
            strcmp(ast->as.program.decls[i]->as.fn_decl.name, "foo") == 0) {
            foo = ast->as.program.decls[i];
            break;
        }
    }
    ASSERT(foo != NULL, "foo not found");

    uint64_t h1 = jit_hash_fn(foo);
    uint64_t h2 = jit_hash_fn(foo);
    ASSERT(h1 == h2, "same AST should produce same hash");
    ASSERT(h1 != 0, "hash should be non-zero");

    ast_free(ast);

    /* Different source should give different hash */
    const char *source2 =
        "fn foo(int x) -> int { return x * 3 }\n"
        "fn main() -> int { return 0 }\n";
    AstNode *ast2 = parse(source2, "<test>");
    ASSERT(ast2 != NULL, "parse failed");

    AstNode *foo2 = NULL;
    for (int i = 0; i < ast2->as.program.decl_count; i++) {
        if (ast2->as.program.decls[i]->kind == AST_FN_DECL &&
            strcmp(ast2->as.program.decls[i]->as.fn_decl.name, "foo") == 0) {
            foo2 = ast2->as.program.decls[i];
            break;
        }
    }
    ASSERT(foo2 != NULL, "foo2 not found");

    uint64_t h3 = jit_hash_fn(foo2);
    ASSERT(h1 != h3, "different body should produce different hash");

    ast_free(ast2);
    printf("  PASS: test_jit_hash_fn\n");
    tests_passed++;
}

static void test_jit_registry(void) {
    JitEngine engine;
    ASSERT_EQ(jit_init(&engine), 0);

    /* First time — needs compile */
    ASSERT(jit_needs_recompile(&engine, "foo", 12345), "new fn should need recompile");

    /* Register it */
    jit_update_registry(&engine, "foo", 12345);
    ASSERT(!jit_needs_recompile(&engine, "foo", 12345), "same hash should not need recompile");

    /* Change hash */
    ASSERT(jit_needs_recompile(&engine, "foo", 99999), "different hash should need recompile");

    /* Update */
    jit_update_registry(&engine, "foo", 99999);
    ASSERT(!jit_needs_recompile(&engine, "foo", 99999), "updated hash should not need recompile");

    /* Multiple functions */
    jit_update_registry(&engine, "bar", 11111);
    jit_update_registry(&engine, "baz", 22222);
    ASSERT_EQ(engine.fn_count, 3);
    ASSERT(!jit_needs_recompile(&engine, "bar", 11111), "bar should match");
    ASSERT(!jit_needs_recompile(&engine, "baz", 22222), "baz should match");

    jit_destroy(&engine);
    printf("  PASS: test_jit_registry\n");
    tests_passed++;
}

static void test_jit_fact5_sample(void) {
    int result = jit_eval_main(
        "fn factorial(int n) -> int {\n"
        "    match n {\n"
        "        0 => 1,\n"
        "        _ => n * factorial(n - 1),\n"
        "    }\n"
        "}\n"
        "fn main() -> int {\n"
        "    int result = factorial(5)\n"
        "    return result\n"
        "}\n"
    );
    ASSERT_EQ(result, 120);
    printf("  PASS: test_jit_fact5_sample\n");
    tests_passed++;
}

/* ---- Main ---- */

int main(void) {
    printf("=== JIT Integration Tests ===\n");

    test_jit_init_destroy();
    test_jit_simple_main();
    test_jit_arithmetic();
    test_jit_function_call();
    test_jit_recursion();
    test_jit_if_else();
    test_jit_while_loop();
    test_jit_match();
    test_jit_fibonacci();
    test_jit_hash_fn();
    test_jit_registry();
    test_jit_fact5_sample();

    printf("\nAll %d JIT tests passed!\n", tests_passed);
    return 0;
}
