/* test_ffi.c — Unit tests for FFI (dynamic library loading) */
#include "common.h"
#include "ffi.h"
#include "parser.h"
#include "checker.h"
#include "codegen.h"
#include "jit.h"

#include <llvm-c/Core.h>
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

#define ASSERT_TRUE(cond) ASSERT((cond), #cond " is false")
#define ASSERT_NOT_NULL(p) ASSERT((p) != NULL, #p " is NULL")
#define ASSERT_NULL(p) ASSERT((p) == NULL, #p " is not NULL")

/* ---- Test: ffi_load / ffi_symbol / ffi_unload ---- */

static void test_ffi_load_valid(void) {
    printf("  test_ffi_load_valid...");
#ifdef _WIN32
    FfiLibrary *lib = ffi_load("kernel32.dll");
    ASSERT_NOT_NULL(lib);

    /* Look up a known symbol */
    void *sym = ffi_symbol(lib, "GetCurrentProcessId");
    ASSERT_NOT_NULL(sym);

    ffi_unload(lib);
#else
    /* Linux/macOS: load libc */
    FfiLibrary *lib = ffi_load("libc.so.6");
    if (lib == NULL) lib = ffi_load("libc.dylib");
    if (lib == NULL) lib = ffi_load("libc.so");
    ASSERT_NOT_NULL(lib);

    void *sym = ffi_symbol(lib, "puts");
    ASSERT_NOT_NULL(sym);

    ffi_unload(lib);
#endif
    printf(" ok\n");
}

static void test_ffi_load_invalid(void) {
    printf("  test_ffi_load_invalid...");
    FfiLibrary *lib = ffi_load("nonexistent_library_12345.dll");
    ASSERT_NULL(lib);
    printf(" ok\n");
}

static void test_ffi_symbol_invalid(void) {
    printf("  test_ffi_symbol_invalid...");
#ifdef _WIN32
    FfiLibrary *lib = ffi_load("kernel32.dll");
    ASSERT_NOT_NULL(lib);

    void *sym = ffi_symbol(lib, "ThisFunctionDoesNotExist_XYZ");
    ASSERT_NULL(sym);

    ffi_unload(lib);
#else
    FfiLibrary *lib = ffi_load("libc.so.6");
    if (lib == NULL) lib = ffi_load("libc.so");
    ASSERT_NOT_NULL(lib);

    void *sym = ffi_symbol(lib, "ThisFunctionDoesNotExist_XYZ");
    ASSERT_NULL(sym);

    ffi_unload(lib);
#endif
    printf(" ok\n");
}

/* ---- Test: ls_ffi_load runtime helper ---- */

static void test_runtime_ffi_load(void) {
    printf("  test_runtime_ffi_load...");
#ifdef _WIN32
    void *handle = ls_ffi_load("kernel32.dll");
    ASSERT_NOT_NULL(handle);

    void *sym = ls_ffi_symbol(handle, "GetCurrentProcessId");
    ASSERT_NOT_NULL(sym);

    ls_ffi_unload(handle);
#else
    void *handle = ls_ffi_load("libc.so.6");
    if (handle == NULL) handle = ls_ffi_load("libc.so");
    ASSERT_NOT_NULL(handle);

    void *sym = ls_ffi_symbol(handle, "puts");
    ASSERT_NOT_NULL(sym);

    ls_ffi_unload(handle);
#endif
    printf(" ok\n");
}

/* ---- Test: auto-append extension ---- */

static void test_ffi_auto_extension(void) {
    printf("  test_ffi_auto_extension...");
#ifdef _WIN32
    /* "kernel32" without .dll should also work */
    FfiLibrary *lib = ffi_load("kernel32");
    ASSERT_NOT_NULL(lib);
    ffi_unload(lib);
#endif
    /* On non-Windows, skip (system libs need version suffix) */
    printf(" ok\n");
}

/* ---- Test: Parse and typecheck FFI syntax ---- */

static void test_ffi_parse_extern(void) {
    printf("  test_ffi_parse_extern...");
    const char *src =
        "lib msvcrt = load(\"msvcrt.dll\")\n"
        "extern fn puts(string s) -> int from msvcrt\n"
        "fn main() -> int {\n"
        "    puts(\"hello\")\n"
        "    return 0\n"
        "}\n";

    AstNode *ast = parse(src, "<test>");
    ASSERT_NOT_NULL(ast);
    ASSERT_TRUE(ast->kind == AST_PROGRAM);
    ASSERT_TRUE(ast->as.program.decl_count >= 3);

    /* First decl is load_lib */
    ASSERT_TRUE(ast->as.program.decls[0]->kind == AST_LOAD_LIB);

    /* Second decl is extern_fn */
    ASSERT_TRUE(ast->as.program.decls[1]->kind == AST_EXTERN_FN);

    /* Type check should pass */
    bool ok = checker_check(ast, "<test>", NULL, NULL);
    ASSERT_TRUE(ok);

    ast_free(ast);
    printf(" ok\n");
}

static void test_ffi_parse_dynamic_call(void) {
    printf("  test_ffi_parse_dynamic_call...");
    const char *src =
        "lib msvcrt = load(\"msvcrt.dll\")\n"
        "fn main() -> int {\n"
        "    msvcrt.call(\"puts\", \"hello\")\n"
        "    return 0\n"
        "}\n";

    AstNode *ast = parse(src, "<test>");
    ASSERT_NOT_NULL(ast);

    bool ok = checker_check(ast, "<test>", NULL, NULL);
    ASSERT_TRUE(ok);

    ast_free(ast);
    printf(" ok\n");
}

/* ---- Test: Codegen for FFI ---- */

static void test_ffi_codegen_extern(void) {
    printf("  test_ffi_codegen_extern...");
    const char *src =
        "lib msvcrt = load(\"msvcrt.dll\")\n"
        "extern fn puts(string s) -> int from msvcrt\n"
        "fn main() -> int {\n"
        "    puts(\"hello from codegen\")\n"
        "    return 0\n"
        "}\n";

    AstNode *ast = parse(src, "<test>");
    ASSERT_NOT_NULL(ast);
    ASSERT_TRUE(checker_check(ast, "<test>", NULL, NULL));

    CodegenContext ctx;
    codegen_init(&ctx, "<test>");
    int result = codegen_compile(&ctx, ast, NULL);
    ASSERT_TRUE(result == 0);

    /* Check IR contains expected elements */
    char *ir = codegen_get_ir(&ctx);
    ASSERT_NOT_NULL(ir);
    ASSERT_TRUE(strstr(ir, "@msvcrt") != NULL);  /* global lib handle */
    ASSERT_TRUE(strstr(ir, "LoadLibraryA") != NULL);  /* platform load call */
    ASSERT_TRUE(strstr(ir, "define i32 @main") != NULL);

    LLVMDisposeMessage(ir);
    codegen_destroy(&ctx);
    ast_free(ast);
    printf(" ok\n");
}

static void test_ffi_codegen_dynamic_call(void) {
    printf("  test_ffi_codegen_dynamic_call...");
    const char *src =
        "lib msvcrt = load(\"msvcrt.dll\")\n"
        "fn main() -> int {\n"
        "    msvcrt.call(\"puts\", \"dynamic hello\")\n"
        "    return 0\n"
        "}\n";

    AstNode *ast = parse(src, "<test>");
    ASSERT_NOT_NULL(ast);
    ASSERT_TRUE(checker_check(ast, "<test>", NULL, NULL));

    CodegenContext ctx;
    codegen_init(&ctx, "<test>");
    int result = codegen_compile(&ctx, ast, NULL);
    ASSERT_TRUE(result == 0);

    char *ir = codegen_get_ir(&ctx);
    ASSERT_NOT_NULL(ir);
    ASSERT_TRUE(strstr(ir, "GetProcAddress") != NULL);  /* symbol lookup */

    LLVMDisposeMessage(ir);
    codegen_destroy(&ctx);
    ast_free(ast);
    printf(" ok\n");
}

/* ---- Test: Varargs extern fn ---- */

static void test_ffi_varargs_extern(void) {
    printf("  test_ffi_varargs_extern...");
    const char *src =
        "lib msvcrt = load(\"msvcrt.dll\")\n"
        "extern fn printf(*u8 fmt, ...) -> int from msvcrt\n"
        "fn main() -> int {\n"
        "    return 0\n"
        "}\n";

    AstNode *ast = parse(src, "<test>");
    ASSERT_NOT_NULL(ast);
    ASSERT_TRUE(checker_check(ast, "<test>", NULL, NULL));

    CodegenContext ctx;
    codegen_init(&ctx, "<test>");
    int result = codegen_compile(&ctx, ast, NULL);
    ASSERT_TRUE(result == 0);

    codegen_destroy(&ctx);
    ast_free(ast);
    printf(" ok\n");
}

/* ---- Test: Error on missing library ---- */

static void test_ffi_error_missing_lib(void) {
    printf("  test_ffi_error_missing_lib...");
    /* Loading a nonexistent library should return NULL */
    void *handle = ls_ffi_load("nonexistent_lib_xyz_123.dll");
    ASSERT_NULL(handle);
    printf(" ok\n");
}

/* ---- Main ---- */

int main(void) {
    printf("Running FFI tests...\n");

    /* Runtime FFI tests */
    test_ffi_load_valid();
    test_ffi_load_invalid();
    test_ffi_symbol_invalid();
    test_runtime_ffi_load();
    test_ffi_auto_extension();

    /* Parse/check tests */
    test_ffi_parse_extern();
    test_ffi_parse_dynamic_call();

    /* Codegen tests */
    test_ffi_codegen_extern();
    test_ffi_codegen_dynamic_call();
    test_ffi_varargs_extern();

    /* Error handling */
    test_ffi_error_missing_lib();

    printf("\nFFI tests: %d/%d passed\n", tests_passed, tests_run);
    return (tests_passed == tests_run) ? 0 : 1;
}
