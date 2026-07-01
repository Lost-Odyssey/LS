/* test_module.c — Module system unit tests */
#include "module.h"
#include "parser.h"
#include "checker.h"
#include "codegen.h"
#include <stdio.h>
#include <string.h>

#define ASSERT(cond, msg) do { \
    if (!(cond)) { \
        fprintf(stderr, "FAIL: %s:%d: %s\n", __FILE__, __LINE__, msg); \
        exit(1); \
    } \
} while(0)

#define ASSERT_TRUE(cond)  ASSERT((cond), #cond " is false")
#define ASSERT_FALSE(cond) ASSERT(!(cond), #cond " is true")
#define ASSERT_EQ(a, b)    ASSERT((a) == (b), #a " != " #b)
#define ASSERT_NOT_NULL(p) ASSERT((p) != NULL, #p " is NULL")
#define ASSERT_NULL(p)     ASSERT((p) == NULL, #p " is not NULL")

static int tests_passed = 0;

/* ---- Module resolution tests ---- */

static void test_resolve_simple_path(void) {
    printf("  test_resolve_simple_path...");
    char *path = module_resolve_path("math", "tests/samples/module_test/main.lls");
    ASSERT_NOT_NULL(path);
    /* Should end with math.lls */
    const char *suffix = "math.lls";
    size_t slen = strlen(suffix);
    ASSERT_TRUE(strlen(path) > slen);
    const char *end = path + strlen(path) - slen;
    ASSERT_TRUE(strcmp(end, suffix) == 0);
    free(path);
    tests_passed++;
    printf(" ok\n");
}

static void test_resolve_dotted_path(void) {
    printf("  test_resolve_dotted_path...");
    char *path = module_resolve_path("std.io", "/some/project/main.lls");
    ASSERT_NOT_NULL(path);
    /* Should contain path separator between std and io */
    ASSERT_TRUE(strstr(path, "io.lls") != NULL);
    free(path);
    tests_passed++;
    printf(" ok\n");
}

/* ---- Module registry tests ---- */

static void test_registry_create_free(void) {
    printf("  test_registry_create_free...");
    ModuleRegistry *reg = module_registry_new();
    ASSERT_NOT_NULL(reg);
    ASSERT_EQ(reg->count, 0);
    module_registry_free(reg);
    tests_passed++;
    printf(" ok\n");
}

static void test_registry_find_empty(void) {
    printf("  test_registry_find_empty...");
    ModuleRegistry *reg = module_registry_new();
    ASSERT_NULL(module_find(reg, "nonexistent"));
    module_registry_free(reg);
    tests_passed++;
    printf(" ok\n");
}

/* ---- Circular import detection ---- */

static void test_circular_import_detection(void) {
    printf("  test_circular_import_detection...");
    ModuleRegistry *reg = module_registry_new();

    ASSERT_FALSE(module_is_importing(reg, "A"));

    ASSERT_TRUE(module_push_import(reg, "A"));
    ASSERT_TRUE(module_is_importing(reg, "A"));
    ASSERT_FALSE(module_is_importing(reg, "B"));

    ASSERT_TRUE(module_push_import(reg, "B"));
    ASSERT_TRUE(module_is_importing(reg, "A"));
    ASSERT_TRUE(module_is_importing(reg, "B"));

    /* Trying to push A again should fail (circular) */
    ASSERT_FALSE(module_push_import(reg, "A"));

    module_pop_import(reg);
    ASSERT_TRUE(module_is_importing(reg, "A"));
    ASSERT_FALSE(module_is_importing(reg, "B"));

    module_pop_import(reg);
    ASSERT_FALSE(module_is_importing(reg, "A"));

    module_registry_free(reg);
    tests_passed++;
    printf(" ok\n");
}

/* ---- Module loading tests ---- */

static void test_load_module(void) {
    printf("  test_load_module...");
    ModuleRegistry *reg = module_registry_new();

    /* Load math module relative to the test sample */
    ModuleInfo *mod = module_load(reg, "math",
        "tests/samples/module_test/main.lls");
    ASSERT_NOT_NULL(mod);
    ASSERT_TRUE(strcmp(mod->name, "math") == 0);
    ASSERT_NOT_NULL(mod->source);
    ASSERT_NOT_NULL(mod->ast);
    ASSERT_FALSE(mod->checked);

    /* Loading again should return same entry */
    ModuleInfo *mod2 = module_load(reg, "math",
        "tests/samples/module_test/main.lls");
    ASSERT_TRUE(mod == mod2);
    ASSERT_EQ(reg->count, 1);

    module_registry_free(reg);
    tests_passed++;
    printf(" ok\n");
}

static void test_load_nonexistent_module(void) {
    printf("  test_load_nonexistent_module...");
    ModuleRegistry *reg = module_registry_new();

    ModuleInfo *mod = module_load(reg, "nonexistent",
        "tests/samples/module_test/main.lls");
    ASSERT_NULL(mod);
    ASSERT_EQ(reg->count, 0);

    module_registry_free(reg);
    tests_passed++;
    printf(" ok\n");
}

/* ---- Type module tests ---- */

static void test_type_module_exports(void) {
    printf("  test_type_module_exports...");

    Type *mod = type_module_new("test_mod");
    ASSERT_NOT_NULL(mod);
    ASSERT_EQ(mod->kind, TYPE_MODULE);
    ASSERT_TRUE(strcmp(mod->as.module.name, "test_mod") == 0);
    ASSERT_EQ(mod->as.module.export_count, 0);

    /* Add an export */
    Type *fn_type = type_function(NULL, 0, type_void(), false);
    type_module_add_export(mod, "greet", fn_type);
    ASSERT_EQ(mod->as.module.export_count, 1);
    ASSERT_TRUE(strcmp(mod->as.module.exports[0].name, "greet") == 0);

    /* type_name should work */
    const char *name = type_name(mod);
    ASSERT_NOT_NULL(name);
    ASSERT_TRUE(strstr(name, "test_mod") != NULL);

    type_free(mod);
    tests_passed++;
    printf(" ok\n");
}

/* ---- Checker with imports ---- */

static void test_checker_import_math(void) {
    printf("  test_checker_import_math...");

    const char *src =
        "module main\n"
        "import math\n"
        "def main() -> int {\n"
        "    int result = math.add(1, 2)\n"
        "    return result\n"
        "}\n";

    AstNode *ast = parse(src, "tests/samples/module_test/main.lls");
    ASSERT_NOT_NULL(ast);

    ModuleRegistry *reg = module_registry_new();
    bool ok = checker_check(ast, "tests/samples/module_test/main.lls", reg, NULL);
    ASSERT_TRUE(ok);

    /* Verify module was loaded */
    ASSERT_EQ(reg->count, 1);
    ModuleInfo *mod = module_find(reg, "math");
    ASSERT_NOT_NULL(mod);
    ASSERT_TRUE(mod->checked);

    module_registry_free(reg);
    ast_free(ast);
    tests_passed++;
    printf(" ok\n");
}

static void test_checker_import_nonexistent(void) {
    printf("  test_checker_import_nonexistent...");

    const char *src =
        "import nonexistent\n"
        "def main() -> int { return 0 }\n";

    AstNode *ast = parse(src, "tests/samples/module_test/main.lls");
    ASSERT_NOT_NULL(ast);

    ModuleRegistry *reg = module_registry_new();
    bool ok = checker_check(ast, "tests/samples/module_test/main.lls", reg, NULL);
    ASSERT_FALSE(ok); /* Should fail: module not found */

    module_registry_free(reg);
    ast_free(ast);
    tests_passed++;
    printf(" ok\n");
}

static void test_checker_module_no_export(void) {
    printf("  test_checker_module_no_export...");

    const char *src =
        "import math\n"
        "def main() -> int {\n"
        "    int x = math.nonexistent()\n"
        "    return x\n"
        "}\n";

    AstNode *ast = parse(src, "tests/samples/module_test/main.lls");
    ASSERT_NOT_NULL(ast);

    ModuleRegistry *reg = module_registry_new();
    bool ok = checker_check(ast, "tests/samples/module_test/main.lls", reg, NULL);
    ASSERT_FALSE(ok); /* Should fail: no such export */

    module_registry_free(reg);
    ast_free(ast);
    tests_passed++;
    printf(" ok\n");
}

/* ---- Codegen with imports ---- */

static void test_codegen_import_math(void) {
    printf("  test_codegen_import_math...");

    const char *src =
        "module main\n"
        "import math\n"
        "def main() -> int {\n"
        "    int result = math.add(1, 2)\n"
        "    return result\n"
        "}\n";

    AstNode *ast = parse(src, "tests/samples/module_test/main.lls");
    ASSERT_NOT_NULL(ast);

    ModuleRegistry *reg = module_registry_new();
    ASSERT_TRUE(checker_check(ast, "tests/samples/module_test/main.lls", reg, NULL));

    CodegenContext ctx;
    codegen_init(&ctx, "test_module");

    int result = codegen_compile(&ctx, ast, reg);
    ASSERT_EQ(result, 0);

    /* Verify IR contains the imported function */
    char *ir = codegen_get_ir(&ctx);
    ASSERT_NOT_NULL(ir);
    ASSERT_TRUE(strstr(ir, "define") != NULL);
    ASSERT_TRUE(strstr(ir, "add") != NULL);
    LLVMDisposeMessage(ir);

    codegen_destroy(&ctx);
    module_registry_free(reg);
    ast_free(ast);
    tests_passed++;
    printf(" ok\n");
}

static void test_codegen_import_call_return(void) {
    printf("  test_codegen_import_call_return...");

    /* Use the actual sample files */
    const char *src =
        "module main\n"
        "import math\n"
        "def main() -> int {\n"
        "    int result = math.add(10, 20)\n"
        "    f64 p = math.pi()\n"
        "    return result\n"
        "}\n";

    AstNode *ast = parse(src, "tests/samples/module_test/main.lls");
    ASSERT_NOT_NULL(ast);

    ModuleRegistry *reg = module_registry_new();
    ASSERT_TRUE(checker_check(ast, "tests/samples/module_test/main.lls", reg, NULL));

    CodegenContext ctx;
    codegen_init(&ctx, "test_module");

    int result = codegen_compile(&ctx, ast, reg);
    ASSERT_EQ(result, 0);

    /* Verify IR contains both imported functions */
    char *ir = codegen_get_ir(&ctx);
    ASSERT_NOT_NULL(ir);
    ASSERT_TRUE(strstr(ir, "add") != NULL);
    ASSERT_TRUE(strstr(ir, "pi") != NULL);
    LLVMDisposeMessage(ir);

    codegen_destroy(&ctx);
    module_registry_free(reg);
    ast_free(ast);
    tests_passed++;
    printf(" ok\n");
}

/* ---- Parse module/import syntax ---- */

static void test_parse_module_decl(void) {
    printf("  test_parse_module_decl...");
    AstNode *ast = parse("module math\n", "<test>");
    ASSERT_NOT_NULL(ast);
    ASSERT_EQ(ast->as.program.decl_count, 1);
    ASSERT_EQ(ast->as.program.decls[0]->kind, AST_MODULE_DECL);
    ASSERT_TRUE(strcmp(ast->as.program.decls[0]->as.module_decl.name, "math") == 0);
    ast_free(ast);
    tests_passed++;
    printf(" ok\n");
}

static void test_parse_import_decl(void) {
    printf("  test_parse_import_decl...");
    AstNode *ast = parse("import std.sys.io\n", "<test>");
    ASSERT_NOT_NULL(ast);
    ASSERT_EQ(ast->as.program.decl_count, 1);
    ASSERT_EQ(ast->as.program.decls[0]->kind, AST_IMPORT_DECL);
    ASSERT_TRUE(strcmp(ast->as.program.decls[0]->as.import_decl.path, "std.sys.io") == 0);
    ast_free(ast);
    tests_passed++;
    printf(" ok\n");
}

static void test_parse_import_simple(void) {
    printf("  test_parse_import_simple...");
    AstNode *ast = parse("import math\n", "<test>");
    ASSERT_NOT_NULL(ast);
    ASSERT_EQ(ast->as.program.decl_count, 1);
    ASSERT_EQ(ast->as.program.decls[0]->kind, AST_IMPORT_DECL);
    ASSERT_TRUE(strcmp(ast->as.program.decls[0]->as.import_decl.path, "math") == 0);
    ast_free(ast);
    tests_passed++;
    printf(" ok\n");
}

/* ---- Cross-module variable tests ---- */

static void test_checker_import_module_var(void) {
    printf("  test_checker_import_module_var...");

    const char *src =
        "module main\n"
        "import constants\n"
        "def main() -> int {\n"
        "    int a = constants.ANSWER\n"
        "    int b = constants.MAGIC\n"
        "    return a + b\n"
        "}\n";

    AstNode *ast = parse(src, "tests/samples/module_test/main.lls");
    ASSERT_NOT_NULL(ast);

    ModuleRegistry *reg = module_registry_new();
    bool ok = checker_check(ast, "tests/samples/module_test/main.lls", reg, NULL);
    ASSERT_TRUE(ok);

    /* Verify module was loaded and has variable exports */
    ModuleInfo *mod = module_find(reg, "constants");
    ASSERT_NOT_NULL(mod);
    ASSERT_TRUE(mod->checked);

    module_registry_free(reg);
    ast_free(ast);
    tests_passed++;
    printf(" ok\n");
}

static void test_checker_import_module_var_nonexistent(void) {
    printf("  test_checker_import_module_var_nonexistent...");

    const char *src =
        "module main\n"
        "import constants\n"
        "def main() -> int {\n"
        "    int a = constants.NONEXIST\n"
        "    return a\n"
        "}\n";

    AstNode *ast = parse(src, "tests/samples/module_test/main.lls");
    ASSERT_NOT_NULL(ast);

    ModuleRegistry *reg = module_registry_new();
    bool ok = checker_check(ast, "tests/samples/module_test/main.lls", reg, NULL);
    ASSERT_FALSE(ok); /* Should fail: no such export */

    module_registry_free(reg);
    ast_free(ast);
    tests_passed++;
    printf(" ok\n");
}

static void test_checker_import_module_var_type(void) {
    printf("  test_checker_import_module_var_type...");

    /* Accessing int variable from module and using it in int context */
    const char *src =
        "module main\n"
        "import constants\n"
        "def main() -> int {\n"
        "    int x = constants.double_it(constants.ANSWER)\n"
        "    return x\n"
        "}\n";

    AstNode *ast = parse(src, "tests/samples/module_test/main.lls");
    ASSERT_NOT_NULL(ast);

    ModuleRegistry *reg = module_registry_new();
    bool ok = checker_check(ast, "tests/samples/module_test/main.lls", reg, NULL);
    ASSERT_TRUE(ok);

    module_registry_free(reg);
    ast_free(ast);
    tests_passed++;
    printf(" ok\n");
}

static void test_codegen_import_module_var(void) {
    printf("  test_codegen_import_module_var...");

    const char *src =
        "module main\n"
        "import constants\n"
        "def main() -> int {\n"
        "    int a = constants.ANSWER\n"
        "    return a\n"
        "}\n";

    AstNode *ast = parse(src, "tests/samples/module_test/main.lls");
    ASSERT_NOT_NULL(ast);

    ModuleRegistry *reg = module_registry_new();
    ASSERT_TRUE(checker_check(ast, "tests/samples/module_test/main.lls", reg, NULL));

    CodegenContext ctx;
    codegen_init(&ctx, "test_module_var");

    int result = codegen_compile(&ctx, ast, reg);
    ASSERT_EQ(result, 0);

    /* Verify IR contains the global variable from the imported module */
    char *ir = codegen_get_ir(&ctx);
    ASSERT_NOT_NULL(ir);
    ASSERT_TRUE(strstr(ir, "ANSWER") != NULL);
    LLVMDisposeMessage(ir);

    codegen_destroy(&ctx);
    module_registry_free(reg);
    ast_free(ast);
    tests_passed++;
    printf(" ok\n");
}

/* ---- Type module variable export ---- */

static void test_type_module_var_exports(void) {
    printf("  test_type_module_var_exports...");

    Type *mod = type_module_new("test_mod");
    ASSERT_NOT_NULL(mod);

    /* Add function export */
    Type *fn_type = type_function(NULL, 0, type_void(), false);
    type_module_add_export(mod, "greet", fn_type);

    /* Add variable export */
    type_module_add_export(mod, "MAGIC", type_int());
    type_module_add_export(mod, "PI", type_f64());

    ASSERT_EQ(mod->as.module.export_count, 3);
    ASSERT_TRUE(strcmp(mod->as.module.exports[1].name, "MAGIC") == 0);
    ASSERT_TRUE(mod->as.module.exports[1].type->kind == TYPE_INT);
    ASSERT_TRUE(strcmp(mod->as.module.exports[2].name, "PI") == 0);
    ASSERT_TRUE(mod->as.module.exports[2].type->kind == TYPE_F64);

    type_free(mod);
    tests_passed++;
    printf(" ok\n");
}

/* ---- Main ---- */

int main(void) {
    printf("=== test_module ===\n");

    printf("Module resolution:\n");
    test_resolve_simple_path();
    test_resolve_dotted_path();

    printf("Module registry:\n");
    test_registry_create_free();
    test_registry_find_empty();

    printf("Circular import detection:\n");
    test_circular_import_detection();

    printf("Module loading:\n");
    test_load_module();
    test_load_nonexistent_module();

    printf("Type module:\n");
    test_type_module_exports();
    test_type_module_var_exports();

    printf("Parse module/import:\n");
    test_parse_module_decl();
    test_parse_import_decl();
    test_parse_import_simple();

    printf("Checker with imports:\n");
    test_checker_import_math();
    test_checker_import_nonexistent();
    test_checker_module_no_export();

    printf("Cross-module variables:\n");
    test_checker_import_module_var();
    test_checker_import_module_var_nonexistent();
    test_checker_import_module_var_type();

    printf("Codegen with imports:\n");
    test_codegen_import_math();
    test_codegen_import_call_return();
    test_codegen_import_module_var();

    printf("\nAll %d tests passed!\n", tests_passed);
    return 0;
}
