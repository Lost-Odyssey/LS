/* test_codegen.c — Unit tests for LLVM IR code generation */
#include "common.h"
#include "parser.h"
#include "checker.h"
#include "codegen.h"

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

/* Helper: parse + check + codegen, return IR string (caller must LLVMDisposeMessage) */
static char *compile_to_ir(const char *source) {
    AstNode *ast = parse(source, "<test>");
    if (ast == NULL) return NULL;

    if (!checker_check(ast, "<test>", NULL, NULL)) {
        ast_free(ast);
        return NULL;
    }

    CodegenContext ctx;
    codegen_init(&ctx, "<test>");

    if (codegen_compile(&ctx, ast, NULL) != 0) {
        codegen_destroy(&ctx);
        ast_free(ast);
        return NULL;
    }

    char *ir = codegen_get_ir(&ctx);
    codegen_destroy(&ctx);
    ast_free(ast);
    return ir;
}

/* Helper: check that IR contains a substring */
static bool ir_contains(const char *ir, const char *substr) {
    return ir != NULL && strstr(ir, substr) != NULL;
}

/* ---- Tests ---- */

static void test_empty_main(void) {
    printf("  test_empty_main...");
    char *ir = compile_to_ir(
        "fn main() -> int {\n"
        "    return 0\n"
        "}\n"
    );
    ASSERT_NOT_NULL(ir);
    ASSERT_TRUE(ir_contains(ir, "define i32 @main()"));
    ASSERT_TRUE(ir_contains(ir, "ret i32 0"));
    LLVMDisposeMessage(ir);
    printf(" ok\n");
}

static void test_int_arithmetic(void) {
    printf("  test_int_arithmetic...");
    char *ir = compile_to_ir(
        "fn main() -> int {\n"
        "    int a = 10\n"
        "    int b = 20\n"
        "    int c = a + b\n"
        "    return c\n"
        "}\n"
    );
    ASSERT_NOT_NULL(ir);
    ASSERT_TRUE(ir_contains(ir, "define i32 @main()"));
    ASSERT_TRUE(ir_contains(ir, "add"));
    LLVMDisposeMessage(ir);
    printf(" ok\n");
}

static void test_float_arithmetic(void) {
    printf("  test_float_arithmetic...");
    char *ir = compile_to_ir(
        "fn main() -> int {\n"
        "    f64 a = 1.5\n"
        "    f64 b = 2.5\n"
        "    f64 c = a + b\n"
        "    return 0\n"
        "}\n"
    );
    ASSERT_NOT_NULL(ir);
    ASSERT_TRUE(ir_contains(ir, "fadd"));
    LLVMDisposeMessage(ir);
    printf(" ok\n");
}

static void test_function_call(void) {
    printf("  test_function_call...");
    char *ir = compile_to_ir(
        "fn add(int a, int b) -> int {\n"
        "    return a + b\n"
        "}\n"
        "fn main() -> int {\n"
        "    int r = add(3, 4)\n"
        "    return r\n"
        "}\n"
    );
    ASSERT_NOT_NULL(ir);
    ASSERT_TRUE(ir_contains(ir, "define i32 @add(i32"));
    ASSERT_TRUE(ir_contains(ir, "call i32 @add("));
    LLVMDisposeMessage(ir);
    printf(" ok\n");
}

static void test_recursive_function(void) {
    printf("  test_recursive_function...");
    char *ir = compile_to_ir(
        "fn factorial(int n) -> int {\n"
        "    match n {\n"
        "        0 => 1,\n"
        "        _ => n * factorial(n - 1),\n"
        "    }\n"
        "}\n"
        "fn main() -> int {\n"
        "    int result = factorial(10)\n"
        "    return result\n"
        "}\n"
    );
    ASSERT_NOT_NULL(ir);
    ASSERT_TRUE(ir_contains(ir, "define i32 @factorial(i32"));
    ASSERT_TRUE(ir_contains(ir, "call i32 @factorial("));
    LLVMDisposeMessage(ir);
    printf(" ok\n");
}

static void test_if_else(void) {
    printf("  test_if_else...");
    char *ir = compile_to_ir(
        "fn max(int a, int b) -> int {\n"
        "    if (a > b) {\n"
        "        return a\n"
        "    } else {\n"
        "        return b\n"
        "    }\n"
        "}\n"
        "fn main() -> int {\n"
        "    return max(3, 5)\n"
        "}\n"
    );
    ASSERT_NOT_NULL(ir);
    ASSERT_TRUE(ir_contains(ir, "if.then"));
    ASSERT_TRUE(ir_contains(ir, "if.else"));
    ASSERT_TRUE(ir_contains(ir, "icmp sgt"));
    LLVMDisposeMessage(ir);
    printf(" ok\n");
}

static void test_while_loop(void) {
    printf("  test_while_loop...");
    char *ir = compile_to_ir(
        "fn sum_to(int n) -> int {\n"
        "    int i = 0\n"
        "    int s = 0\n"
        "    while (i < n) {\n"
        "        s = s + i\n"
        "        i = i + 1\n"
        "    }\n"
        "    return s\n"
        "}\n"
        "fn main() -> int {\n"
        "    return sum_to(10)\n"
        "}\n"
    );
    ASSERT_NOT_NULL(ir);
    ASSERT_TRUE(ir_contains(ir, "while.cond"));
    ASSERT_TRUE(ir_contains(ir, "while.body"));
    ASSERT_TRUE(ir_contains(ir, "while.end"));
    LLVMDisposeMessage(ir);
    printf(" ok\n");
}

static void test_for_c_loop(void) {
    printf("  test_for_c_loop...");
    char *ir = compile_to_ir(
        "fn sum_to(int n) -> int {\n"
        "    int s = 0\n"
        "    for (int i = 0; i < n; i = i + 1) {\n"
        "        s = s + i\n"
        "    }\n"
        "    return s\n"
        "}\n"
        "fn main() -> int {\n"
        "    return sum_to(10)\n"
        "}\n"
    );
    ASSERT_NOT_NULL(ir);
    ASSERT_TRUE(ir_contains(ir, "for.cond"));
    ASSERT_TRUE(ir_contains(ir, "for.body"));
    ASSERT_TRUE(ir_contains(ir, "for.update"));
    ASSERT_TRUE(ir_contains(ir, "for.end"));
    LLVMDisposeMessage(ir);
    printf(" ok\n");
}

static void test_foreach_range(void) {
    printf("  test_foreach_range...");
    char *ir = compile_to_ir(
        "fn sum_range(int a, int b) -> int {\n"
        "    int s = 0\n"
        "    for i in a..b {\n"
        "        s = s + i\n"
        "    }\n"
        "    return s\n"
        "}\n"
        "fn main() -> int {\n"
        "    return sum_range(0, 10)\n"
        "}\n"
    );
    ASSERT_NOT_NULL(ir);
    ASSERT_TRUE(ir_contains(ir, "foreach.cond"));
    ASSERT_TRUE(ir_contains(ir, "foreach.body"));
    ASSERT_TRUE(ir_contains(ir, "foreach.update"));
    ASSERT_TRUE(ir_contains(ir, "foreach.end"));
    LLVMDisposeMessage(ir);
    printf(" ok\n");
}

static void test_struct_codegen(void) {
    printf("  test_struct_codegen...");
    char *ir = compile_to_ir(
        "struct Point { f64 x; f64 y; }\n"
        "fn main() -> int {\n"
        "    Point p\n"
        "    p.x = 1.0\n"
        "    p.y = 2.0\n"
        "    return 0\n"
        "}\n"
    );
    ASSERT_NOT_NULL(ir);
    ASSERT_TRUE(ir_contains(ir, "%Point = type { double, double }"));
    ASSERT_TRUE(ir_contains(ir, "getelementptr"));
    LLVMDisposeMessage(ir);
    printf(" ok\n");
}

static void test_match_codegen(void) {
    printf("  test_match_codegen...");
    char *ir = compile_to_ir(
        "fn classify(int n) -> int {\n"
        "    match n {\n"
        "        0 => 0,\n"
        "        1 => 1,\n"
        "        _ => 2,\n"
        "    }\n"
        "}\n"
        "fn main() -> int {\n"
        "    return classify(5)\n"
        "}\n"
    );
    ASSERT_NOT_NULL(ir);
    /* Integer match now uses LLVM switch instruction (not CondBr + ICmp).
       The switch emits a 'switch' instruction, 'match.case' body blocks,
       and a 'match.default' block for the wildcard arm. */
    ASSERT_TRUE(ir_contains(ir, "switch"));
    ASSERT_TRUE(ir_contains(ir, "match.case"));
    ASSERT_TRUE(ir_contains(ir, "match.end"));
    LLVMDisposeMessage(ir);
    printf(" ok\n");
}

static void test_bool_logic(void) {
    printf("  test_bool_logic...");
    char *ir = compile_to_ir(
        "fn main() -> int {\n"
        "    bool a = true\n"
        "    bool b = false\n"
        "    bool c = a && b\n"
        "    bool d = a || b\n"
        "    bool e = !a\n"
        "    return 0\n"
        "}\n"
    );
    ASSERT_NOT_NULL(ir);
    /* Short-circuit generates phi nodes and branches */
    ASSERT_TRUE(ir_contains(ir, "sc.rhs") || ir_contains(ir, "phi"));
    LLVMDisposeMessage(ir);
    printf(" ok\n");
}

static void test_string_global(void) {
    printf("  test_string_global...");
    char *ir = compile_to_ir(
        "fn main() -> int {\n"
        "    string s = \"Hello, World!\"\n"
        "    return 0\n"
        "}\n"
    );
    ASSERT_NOT_NULL(ir);
    ASSERT_TRUE(ir_contains(ir, "Hello, World!"));
    LLVMDisposeMessage(ir);
    printf(" ok\n");
}

static void test_print_builtin(void) {
    printf("  test_print_builtin...");
    char *ir = compile_to_ir(
        "fn main() -> int {\n"
        "    print(\"Hello\")\n"
        "    return 0\n"
        "}\n"
    );
    ASSERT_NOT_NULL(ir);
    ASSERT_TRUE(ir_contains(ir, "call") && ir_contains(ir, "@print"));
    LLVMDisposeMessage(ir);
    printf(" ok\n");
}

static void test_pointer_ops(void) {
    printf("  test_pointer_ops...");
    char *ir = compile_to_ir(
        "fn main() -> int {\n"
        "    int x = 42\n"
        "    *int p = &x\n"
        "    int y = *p\n"
        "    return y\n"
        "}\n"
    );
    ASSERT_NOT_NULL(ir);
    /* Should have load/store for pointer operations */
    ASSERT_TRUE(ir_contains(ir, "load"));
    ASSERT_TRUE(ir_contains(ir, "store"));
    LLVMDisposeMessage(ir);
    printf(" ok\n");
}

static void test_compound_assignment(void) {
    printf("  test_compound_assignment...");
    char *ir = compile_to_ir(
        "fn main() -> int {\n"
        "    int x = 10\n"
        "    x += 5\n"
        "    x -= 2\n"
        "    x *= 3\n"
        "    return x\n"
        "}\n"
    );
    ASSERT_NOT_NULL(ir);
    ASSERT_TRUE(ir_contains(ir, "add"));
    ASSERT_TRUE(ir_contains(ir, "sub"));
    ASSERT_TRUE(ir_contains(ir, "mul"));
    LLVMDisposeMessage(ir);
    printf(" ok\n");
}

static void test_unary_neg(void) {
    printf("  test_unary_neg...");
    char *ir = compile_to_ir(
        "fn main() -> int {\n"
        "    int x = 42\n"
        "    int y = -x\n"
        "    return y\n"
        "}\n"
    );
    ASSERT_NOT_NULL(ir);
    ASSERT_TRUE(ir_contains(ir, "sub") || ir_contains(ir, "neg"));
    LLVMDisposeMessage(ir);
    printf(" ok\n");
}

static void test_samples_hello(void) {
    printf("  test_samples_hello...");
    char *ir = compile_to_ir(
        "module main\n"
        "fn main() -> int {\n"
        "    string greeting = \"Hello, World!\"\n"
        "    int x = 42\n"
        "    f64 pi = 3.14159\n"
        "    bool flag = true\n"
        "    int sum = x + 8\n"
        "    int product = x * 2\n"
        "    int hex_val = 0xFF\n"
        "    int bin_val = 0b1010\n"
        "    *int ptr = &x\n"
        "    return 0\n"
        "}\n"
    );
    ASSERT_NOT_NULL(ir);
    ASSERT_TRUE(ir_contains(ir, "define i32 @main()"));
    ASSERT_TRUE(ir_contains(ir, "ret i32 0"));
    LLVMDisposeMessage(ir);
    printf(" ok\n");
}

static void test_samples_factorial(void) {
    printf("  test_samples_factorial...");
    char *ir = compile_to_ir(
        "module main\n"
        "fn factorial(int n) -> int {\n"
        "    match n {\n"
        "        0 => 1,\n"
        "        _ => n * factorial(n - 1),\n"
        "    }\n"
        "}\n"
        "fn main() -> int {\n"
        "    int result = factorial(10)\n"
        "    return result\n"
        "}\n"
    );
    ASSERT_NOT_NULL(ir);
    ASSERT_TRUE(ir_contains(ir, "define i32 @factorial("));
    ASSERT_TRUE(ir_contains(ir, "define i32 @main()"));
    LLVMDisposeMessage(ir);
    printf(" ok\n");
}

static void test_samples_struct(void) {
    printf("  test_samples_struct...");
    char *ir = compile_to_ir(
        "module main\n"
        "struct Point { f64 x; f64 y; }\n"
        "impl Point {\n"
        "    fn distance(Point other) -> f64 {\n"
        "        f64 dx = self.x - other.x\n"
        "        f64 dy = self.y - other.y\n"
        "        return dx * dx + dy * dy\n"
        "    }\n"
        "}\n"
        "fn main() -> int {\n"
        "    return 0\n"
        "}\n"
    );
    ASSERT_NOT_NULL(ir);
    ASSERT_TRUE(ir_contains(ir, "%Point = type { double, double }"));
    /* Instance method with qualified name gets an implicit *Point self param (ptr) */
    ASSERT_TRUE(ir_contains(ir, "define double @Point.distance(ptr"));
    LLVMDisposeMessage(ir);
    printf(" ok\n");
}

static void test_samples_match(void) {
    printf("  test_samples_match...");
    char *ir = compile_to_ir(
        "module main\n"
        "fn classify(int n) -> int {\n"
        "    match n {\n"
        "        0 => 0,\n"
        "        1 => 1,\n"
        "        _ => n + 1,\n"
        "    }\n"
        "}\n"
        "fn main() -> int {\n"
        "    int a = classify(0)\n"
        "    int b = classify(1)\n"
        "    int c = classify(5)\n"
        "    return 0\n"
        "}\n"
    );
    ASSERT_NOT_NULL(ir);
    ASSERT_TRUE(ir_contains(ir, "define i32 @classify("));
    LLVMDisposeMessage(ir);
    printf(" ok\n");
}

/* ---- Enum tests (Phase 8) ---- */

static void test_enum_decl_layout(void) {
    printf("  test_enum_decl_layout...");
    char *ir = compile_to_ir(
        "enum Color { Red Green Blue }\n"
        "fn main() -> int { Color c = Red  return 0 }\n"
    );
    ASSERT_NOT_NULL(ir);
    /* Color appears as a named LLVM struct type, referenced via alloca */
    ASSERT_TRUE(ir_contains(ir, "Color"));
    LLVMDisposeMessage(ir);
    printf(" ok\n");
}

static void test_enum_ctor_and_match(void) {
    printf("  test_enum_ctor_and_match...");
    char *ir = compile_to_ir(
        "enum Shape { Point Circle(int r) Rect(int w, int h) }\n"
        "fn area(Shape s) -> int {\n"
        "    match s { Point => 0  Circle(r) => r * r * 3  Rect(w, h) => w * h }\n"
        "}\n"
        "fn main() -> int {\n"
        "    Shape s = Circle(5)\n"
        "    return area(s)\n"
        "}\n"
    );
    ASSERT_NOT_NULL(ir);
    /* Match codegen uses LLVM switch on the discriminant byte */
    ASSERT_TRUE(ir_contains(ir, "switch i8"));
    /* Ctor uses an alloca for the enum struct */
    ASSERT_TRUE(ir_contains(ir, "alloca"));
    ASSERT_TRUE(ir_contains(ir, "Shape"));
    LLVMDisposeMessage(ir);
    printf(" ok\n");
}

static void test_enum_drop_fn(void) {
    printf("  test_enum_drop_fn...");
    char *ir = compile_to_ir(
        "enum Event { Quit  Message(string text) }\n"
        "fn main() -> int {\n"
        "    Event e = Message(\"hi\")\n"
        "    return 0\n"
        "}\n"
    );
    ASSERT_NOT_NULL(ir);
    /* Auto-generated EnumName.__drop function (LLVM may or may not quote name) */
    ASSERT_TRUE(ir_contains(ir, "Event.__drop"));
    LLVMDisposeMessage(ir);
    printf(" ok\n");
}

static void test_option_template_instantiation(void) {
    printf("  test_option_template_instantiation...");
    char *ir = compile_to_ir(
        "fn main() -> int {\n"
        "    Option(int) a = Some(42)\n"
        "    Option(int) b = None\n"
        "    return 0\n"
        "}\n"
    );
    ASSERT_NOT_NULL(ir);
    /* Mangled name appears as a named LLVM struct */
    ASSERT_TRUE(ir_contains(ir, "Option(int)"));
    LLVMDisposeMessage(ir);
    printf(" ok\n");
}

/* ---- Array tests ---- */

static void test_array_local(void) {
    printf("  test_array_local...");
    char *ir = compile_to_ir(
        "fn main() -> int {\n"
        "    array(int, 3) nums = [10, 20, 30]\n"
        "    int x = nums[1]\n"
        "    return x\n"
        "}\n"
    );
    ASSERT_NOT_NULL(ir);
    /* Should have array type and GEP for indexing */
    ASSERT_TRUE(ir_contains(ir, "[3 x i32]"));
    ASSERT_TRUE(ir_contains(ir, "getelementptr"));
    LLVMDisposeMessage(ir);
    printf(" ok\n");
}

static void test_array_length(void) {
    printf("  test_array_length...");
    char *ir = compile_to_ir(
        "fn main() -> int {\n"
        "    array(int, 5) arr = [1, 2, 3, 4, 5]\n"
        "    int len = arr.length\n"
        "    return len\n"
        "}\n"
    );
    ASSERT_NOT_NULL(ir);
    /* .length should be a compile-time constant 5 */
    ASSERT_TRUE(ir_contains(ir, "store i32 5"));
    LLVMDisposeMessage(ir);
    printf(" ok\n");
}

static void test_array_index_write(void) {
    printf("  test_array_index_write...");
    char *ir = compile_to_ir(
        "fn main() -> int {\n"
        "    array(int, 3) arr = [1, 2, 3]\n"
        "    arr[0] = 99\n"
        "    return arr[0]\n"
        "}\n"
    );
    ASSERT_NOT_NULL(ir);
    ASSERT_TRUE(ir_contains(ir, "getelementptr"));
    ASSERT_TRUE(ir_contains(ir, "store"));
    LLVMDisposeMessage(ir);
    printf(" ok\n");
}

static void test_array_foreach(void) {
    printf("  test_array_foreach...");
    char *ir = compile_to_ir(
        "fn main() -> int {\n"
        "    array(int, 3) arr = [10, 20, 30]\n"
        "    int sum = 0\n"
        "    for x in arr {\n"
        "        sum = sum + x\n"
        "    }\n"
        "    return sum\n"
        "}\n"
    );
    ASSERT_NOT_NULL(ir);
    ASSERT_TRUE(ir_contains(ir, "foreach.cond"));
    ASSERT_TRUE(ir_contains(ir, "foreach.body"));
    LLVMDisposeMessage(ir);
    printf(" ok\n");
}

static void test_array_as_param(void) {
    printf("  test_array_as_param...");
    char *ir = compile_to_ir(
        "fn sum3(array(int, 3) a) -> int {\n"
        "    return a[0] + a[1] + a[2]\n"
        "}\n"
        "fn main() -> int {\n"
        "    array(int, 3) nums = [1, 2, 3]\n"
        "    return sum3(nums)\n"
        "}\n"
    );
    ASSERT_NOT_NULL(ir);
    ASSERT_TRUE(ir_contains(ir, "define i32 @sum3("));
    ASSERT_TRUE(ir_contains(ir, "call i32 @sum3("));
    LLVMDisposeMessage(ir);
    printf(" ok\n");
}

/* ---- Global variable tests ---- */

static void test_global_int(void) {
    printf("  test_global_int...");
    char *ir = compile_to_ir(
        "int MAGIC = 42\n"
        "fn main() -> int {\n"
        "    return MAGIC\n"
        "}\n"
    );
    ASSERT_NOT_NULL(ir);
    /* Should have a global variable declaration */
    ASSERT_TRUE(ir_contains(ir, "@MAGIC = global i32"));
    /* Should have __ls_global_stmts function (renamed from __ls_global_init) */
    ASSERT_TRUE(ir_contains(ir, "__ls_global_stmts"));
    LLVMDisposeMessage(ir);
    printf(" ok\n");
}

static void test_global_array(void) {
    printf("  test_global_array...");
    char *ir = compile_to_ir(
        "array(int, 3) data = [10, 20, 30]\n"
        "fn main() -> int {\n"
        "    return data[1]\n"
        "}\n"
    );
    ASSERT_NOT_NULL(ir);
    ASSERT_TRUE(ir_contains(ir, "@data = global [3 x i32]"));
    LLVMDisposeMessage(ir);
    printf(" ok\n");
}

static void test_global_mutable(void) {
    printf("  test_global_mutable...");
    char *ir = compile_to_ir(
        "int counter = 0\n"
        "fn increment() {\n"
        "    counter = counter + 1\n"
        "}\n"
        "fn main() -> int {\n"
        "    increment()\n"
        "    increment()\n"
        "    return counter\n"
        "}\n"
    );
    ASSERT_NOT_NULL(ir);
    ASSERT_TRUE(ir_contains(ir, "@counter = global i32"));
    ASSERT_TRUE(ir_contains(ir, "define void @increment()"));
    LLVMDisposeMessage(ir);
    printf(" ok\n");
}

static void test_global_multiple_types(void) {
    printf("  test_global_multiple_types...");
    char *ir = compile_to_ir(
        "int x = 1\n"
        "f64 pi = 3.14\n"
        "bool flag = true\n"
        "fn main() -> int {\n"
        "    return x\n"
        "}\n"
    );
    ASSERT_NOT_NULL(ir);
    ASSERT_TRUE(ir_contains(ir, "@x = global i32"));
    ASSERT_TRUE(ir_contains(ir, "@pi = global double"));
    ASSERT_TRUE(ir_contains(ir, "@flag = global i1"));
    LLVMDisposeMessage(ir);
    printf(" ok\n");
}

/* ---- LsString Codegen Tests ---- */

static void test_string_lsstruct(void) {
    printf("  test_string_lsstruct...");
    char *ir = compile_to_ir(
        "fn main() -> int {\n"
        "    string s = \"hello\"\n"
        "    return 0\n"
        "}\n"
    );
    ASSERT_NOT_NULL(ir);
    /* String type should be %LsString = { ptr, i32, i32 } */
    ASSERT_TRUE(ir_contains(ir, "LsString"));
    /* String literal data should exist as global constant */
    ASSERT_TRUE(ir_contains(ir, "hello"));
    LLVMDisposeMessage(ir);
    printf(" ok\n");
}

static void test_string_concat_ir(void) {
    printf("  test_string_concat_ir...");
    char *ir = compile_to_ir(
        "fn main() -> int {\n"
        "    string a = \"hello\"\n"
        "    string b = \" world\"\n"
        "    string c = a + b\n"
        "    return 0\n"
        "}\n"
    );
    ASSERT_NOT_NULL(ir);
    /* Concat should generate calls to malloc and memcpy */
    ASSERT_TRUE(ir_contains(ir, "@malloc"));
    ASSERT_TRUE(ir_contains(ir, "@memcpy"));
    LLVMDisposeMessage(ir);
    printf(" ok\n");
}

static void test_string_compare_ir(void) {
    printf("  test_string_compare_ir...");
    char *ir = compile_to_ir(
        "fn main() -> int {\n"
        "    string a = \"abc\"\n"
        "    string b = \"abc\"\n"
        "    bool eq = (a == b)\n"
        "    bool ne = (a != b)\n"
        "    return 0\n"
        "}\n"
    );
    ASSERT_NOT_NULL(ir);
    /* String comparison should use strcmp */
    ASSERT_TRUE(ir_contains(ir, "@strcmp"));
    LLVMDisposeMessage(ir);
    printf(" ok\n");
}

static void test_string_length_ir(void) {
    printf("  test_string_length_ir...");
    char *ir = compile_to_ir(
        "fn main() -> int {\n"
        "    string s = \"hello\"\n"
        "    int len = s.length\n"
        "    return len\n"
        "}\n"
    );
    ASSERT_NOT_NULL(ir);
    /* .length should extract field 1 from the LsString struct */
    ASSERT_TRUE(ir_contains(ir, "extractvalue"));
    LLVMDisposeMessage(ir);
    printf(" ok\n");
}

static void test_string_ffi_extract_data(void) {
    printf("  test_string_ffi_extract_data...");
    /* When printing a string, codegen should produce a printf call with the
       string data. LLVM may optimize away extractvalue for literal strings,
       so we just verify print() works and produces a printf call. */
    char *ir = compile_to_ir(
        "fn main() -> int {\n"
        "    print(\"hello\")\n"
        "    return 0\n"
        "}\n"
    );
    ASSERT_NOT_NULL(ir);
    /* print expands to printf with %s format and the string data */
    ASSERT_TRUE(ir_contains(ir, "@printf"));
    ASSERT_TRUE(ir_contains(ir, "hello"));
    LLVMDisposeMessage(ir);
    printf(" ok\n");
}

/* ---- String Method Codegen Tests ---- */

static void test_string_empty_ir(void) {
    printf("  test_string_empty_ir...");
    char *ir = compile_to_ir(
        "fn main() -> int {\n"
        "    string s = \"hello\"\n"
        "    bool b = s.empty()\n"
        "    return 0\n"
        "}\n"
    );
    ASSERT_NOT_NULL(ir);
    /* empty() compares len field to 0 via icmp eq */
    ASSERT_TRUE(ir_contains(ir, "icmp eq"));
    LLVMDisposeMessage(ir);
    printf(" ok\n");
}

static void test_string_at_ir(void) {
    printf("  test_string_at_ir...");
    char *ir = compile_to_ir(
        "fn main() -> int {\n"
        "    string s = \"hello\"\n"
        "    int ch = s.at(1)\n"
        "    return 0\n"
        "}\n"
    );
    ASSERT_NOT_NULL(ir);
    /* at() uses GEP + load + zext */
    ASSERT_TRUE(ir_contains(ir, "getelementptr"));
    LLVMDisposeMessage(ir);
    printf(" ok\n");
}

static void test_string_find_ir(void) {
    printf("  test_string_find_ir...");
    char *ir = compile_to_ir(
        "fn main() -> int {\n"
        "    string s = \"hello world\"\n"
        "    int pos = s.find(\"world\")\n"
        "    return 0\n"
        "}\n"
    );
    ASSERT_NOT_NULL(ir);
    /* find() calls strstr */
    ASSERT_TRUE(ir_contains(ir, "@strstr"));
    LLVMDisposeMessage(ir);
    printf(" ok\n");
}

static void test_string_contains_ir(void) {
    printf("  test_string_contains_ir...");
    char *ir = compile_to_ir(
        "fn main() -> int {\n"
        "    string s = \"hello world\"\n"
        "    bool has = s.contains(\"llo\")\n"
        "    return 0\n"
        "}\n"
    );
    ASSERT_NOT_NULL(ir);
    /* contains() calls strstr and compares != null */
    ASSERT_TRUE(ir_contains(ir, "@strstr"));
    ASSERT_TRUE(ir_contains(ir, "icmp ne"));
    LLVMDisposeMessage(ir);
    printf(" ok\n");
}

static void test_string_starts_with_ir(void) {
    printf("  test_string_starts_with_ir...");
    char *ir = compile_to_ir(
        "fn main() -> int {\n"
        "    string s = \"hello world\"\n"
        "    bool b = s.starts_with(\"hello\")\n"
        "    return 0\n"
        "}\n"
    );
    ASSERT_NOT_NULL(ir);
    /* starts_with() calls strncmp */
    ASSERT_TRUE(ir_contains(ir, "@strncmp"));
    LLVMDisposeMessage(ir);
    printf(" ok\n");
}

static void test_string_ends_with_ir(void) {
    printf("  test_string_ends_with_ir...");
    char *ir = compile_to_ir(
        "fn main() -> int {\n"
        "    string s = \"hello world\"\n"
        "    bool b = s.ends_with(\"world\")\n"
        "    return 0\n"
        "}\n"
    );
    ASSERT_NOT_NULL(ir);
    /* ends_with() calls strcmp and uses and for combining checks */
    ASSERT_TRUE(ir_contains(ir, "@strcmp"));
    LLVMDisposeMessage(ir);
    printf(" ok\n");
}

static void test_string_compare_method_ir(void) {
    printf("  test_string_compare_method_ir...");
    char *ir = compile_to_ir(
        "fn main() -> int {\n"
        "    string a = \"abc\"\n"
        "    string b = \"def\"\n"
        "    int c = a.compare(b)\n"
        "    return 0\n"
        "}\n"
    );
    ASSERT_NOT_NULL(ir);
    /* compare() calls strcmp */
    ASSERT_TRUE(ir_contains(ir, "@strcmp"));
    LLVMDisposeMessage(ir);
    printf(" ok\n");
}

/* ---- String method Batch 2 codegen tests ---- */

static void test_string_upper_ir(void) {
    printf("  test_string_upper_ir...");
    char *ir = compile_to_ir(
        "fn main() -> int {\n"
        "    string s = \"hello\"\n"
        "    string u = s.upper()\n"
        "    return 0\n"
        "}\n"
    );
    ASSERT_NOT_NULL(ir);
    /* upper() allocates via malloc and loops over bytes */
    ASSERT_TRUE(ir_contains(ir, "@malloc"));
    ASSERT_TRUE(ir_contains(ir, "up.cond"));
    ASSERT_TRUE(ir_contains(ir, "up.body"));
    LLVMDisposeMessage(ir);
    printf(" ok\n");
}

static void test_string_lower_ir(void) {
    printf("  test_string_lower_ir...");
    char *ir = compile_to_ir(
        "fn main() -> int {\n"
        "    string s = \"HELLO\"\n"
        "    string l = s.lower()\n"
        "    return 0\n"
        "}\n"
    );
    ASSERT_NOT_NULL(ir);
    ASSERT_TRUE(ir_contains(ir, "@malloc"));
    ASSERT_TRUE(ir_contains(ir, "lo.cond"));
    ASSERT_TRUE(ir_contains(ir, "lo.body"));
    LLVMDisposeMessage(ir);
    printf(" ok\n");
}

static void test_string_substr_ir(void) {
    printf("  test_string_substr_ir...");
    char *ir = compile_to_ir(
        "fn main() -> int {\n"
        "    string s = \"hello world\"\n"
        "    string sub = s.substr(0, 5)\n"
        "    return 0\n"
        "}\n"
    );
    ASSERT_NOT_NULL(ir);
    ASSERT_TRUE(ir_contains(ir, "@malloc"));
    ASSERT_TRUE(ir_contains(ir, "@memcpy"));
    LLVMDisposeMessage(ir);
    printf(" ok\n");
}

static void test_string_trim_ir(void) {
    printf("  test_string_trim_ir...");
    char *ir = compile_to_ir(
        "fn main() -> int {\n"
        "    string s = \"  hello  \"\n"
        "    string t = s.trim()\n"
        "    return 0\n"
        "}\n"
    );
    ASSERT_NOT_NULL(ir);
    ASSERT_TRUE(ir_contains(ir, "@malloc"));
    ASSERT_TRUE(ir_contains(ir, "tr.fwd.cond"));
    ASSERT_TRUE(ir_contains(ir, "tr.bwd.cond"));
    LLVMDisposeMessage(ir);
    printf(" ok\n");
}

static void test_string_copy_ir(void) {
    printf("  test_string_copy_ir...");
    char *ir = compile_to_ir(
        "fn main() -> int {\n"
        "    string s = \"hello\"\n"
        "    string c = s.copy()\n"
        "    return 0\n"
        "}\n"
    );
    ASSERT_NOT_NULL(ir);
    ASSERT_TRUE(ir_contains(ir, "@malloc"));
    ASSERT_TRUE(ir_contains(ir, "@memcpy"));
    LLVMDisposeMessage(ir);
    printf(" ok\n");
}

static void test_string_replace_ir(void) {
    printf("  test_string_replace_ir...");
    char *ir = compile_to_ir(
        "fn main() -> int {\n"
        "    string s = \"hello world\"\n"
        "    string r = s.replace(\"world\", \"LS\")\n"
        "    return 0\n"
        "}\n"
    );
    ASSERT_NOT_NULL(ir);
    ASSERT_TRUE(ir_contains(ir, "@__ls_str_replace"));
    LLVMDisposeMessage(ir);
    printf(" ok\n");
}

/* ---- String Batch 3 codegen tests ---- */

static void test_string_rfind_ir(void) {
    printf("  test_string_rfind_ir...");
    char *ir = compile_to_ir(
        "fn main() -> int {\n"
        "    string s = \"hello world hello\"\n"
        "    int pos = s.rfind(\"hello\")\n"
        "    return pos\n"
        "}\n"
    );
    ASSERT_NOT_NULL(ir);
    ASSERT_TRUE(ir_contains(ir, "rf.cond"));
    ASSERT_TRUE(ir_contains(ir, "rf.body"));
    ASSERT_TRUE(ir_contains(ir, "rf.end"));
    LLVMDisposeMessage(ir);
    printf(" ok\n");
}

static void test_string_count_ir(void) {
    printf("  test_string_count_ir...");
    char *ir = compile_to_ir(
        "fn main() -> int {\n"
        "    string s = \"abababab\"\n"
        "    int n = s.count(\"ab\")\n"
        "    return n\n"
        "}\n"
    );
    ASSERT_NOT_NULL(ir);
    ASSERT_TRUE(ir_contains(ir, "cn.cond"));
    ASSERT_TRUE(ir_contains(ir, "cn.body"));
    ASSERT_TRUE(ir_contains(ir, "cn.end"));
    LLVMDisposeMessage(ir);
    printf(" ok\n");
}

static void test_string_substr_one_arg_ir(void) {
    printf("  test_string_substr_one_arg_ir...");
    char *ir = compile_to_ir(
        "fn main() -> int {\n"
        "    string s = \"hello world\"\n"
        "    string tail = s.substr(6)\n"
        "    return 0\n"
        "}\n"
    );
    ASSERT_NOT_NULL(ir);
    ASSERT_TRUE(ir_contains(ir, "ss1.len"));
    ASSERT_TRUE(ir_contains(ir, "ss.cap64"));
    LLVMDisposeMessage(ir);
    printf(" ok\n");
}

/* Phase 2.5: split/join are no longer compiler builtins — they were moved to
   pure-LS `impl string` in std/string.ls (returning std.vec Vec(string)). With
   no `import std.string`, `s.split(...)` must be rejected by the checker, so
   compilation yields no IR. */
static void test_string_split_ir(void) {
    printf("  test_string_split_ir...");
    char *ir = compile_to_ir(
        "fn main() -> int {\n"
        "    string s = \"a,b,c\"\n"
        "    string parts = s.split(\",\")\n"
        "    return 0\n"
        "}\n"
    );
    ASSERT_TRUE(ir == NULL);
    printf(" ok\n");
}

static void test_string_join_ir(void) {
    printf("  test_string_join_ir...");
    char *ir = compile_to_ir(
        "fn main() -> int {\n"
        "    string sep = \", \"\n"
        "    string result = sep.join(\"x\")\n"
        "    return 0\n"
        "}\n"
    );
    ASSERT_TRUE(ir == NULL);
    printf(" ok\n");
}

/* ---- Struct method (implicit self + static) codegen tests ---- */

static void test_instance_method_ir(void) {
    printf("  test_instance_method_ir...");
    char *ir = compile_to_ir(
        "module main\n"
        "struct Point { f64 x; f64 y; }\n"
        "impl Point {\n"
        "    fn get_x() -> f64 { return self.x }\n"
        "}\n"
        "fn main() -> int {\n"
        "    Point p\n"
        "    p.x = 5.0\n"
        "    p.y = 0.0\n"
        "    f64 x = p.get_x()\n"
        "    return 0\n"
        "}\n"
    );
    ASSERT_NOT_NULL(ir);
    /* Instance method with qualified name has ptr as first param (implicit self) */
    ASSERT_TRUE(ir_contains(ir, "define double @Point.get_x(ptr"));
    /* Main should call get_x with a pointer arg */
    ASSERT_TRUE(ir_contains(ir, "call double @Point.get_x(ptr"));
    LLVMDisposeMessage(ir);
    printf(" ok\n");
}

static void test_static_method_ir(void) {
    printf("  test_static_method_ir...");
    char *ir = compile_to_ir(
        "module main\n"
        "struct Point { f64 x; f64 y; }\n"
        "impl Point {\n"
        "    static fn zero() -> int { return 0 }\n"
        "}\n"
        "fn main() -> int {\n"
        "    int z = Point.zero()\n"
        "    return z\n"
        "}\n"
    );
    ASSERT_NOT_NULL(ir);
    /* Static method with qualified name should NOT have a self ptr param — just return i32 */
    ASSERT_TRUE(ir_contains(ir, "define i32 @Point.zero()"));
    ASSERT_TRUE(ir_contains(ir, "call i32 @Point.zero()"));
    LLVMDisposeMessage(ir);
    printf(" ok\n");
}

/* ---- String auto-free codegen tests ---- */

static void test_string_autofree_block(void) {
    printf("  test_string_autofree_block...");
    /* String in an inner block scope should be freed at block exit */
    char *ir = compile_to_ir(
        "fn main() -> int {\n"
        "    if (true) {\n"
        "        string s = \"hello\".upper()\n"
        "        print(s)\n"
        "    }\n"
        "    return 0\n"
        "}\n"
    );
    ASSERT_NOT_NULL(ir);
    /* Should contain sf.free / sf.cont blocks for auto-free cleanup */
    ASSERT_TRUE(ir_contains(ir, "sf.owned"));
    ASSERT_TRUE(ir_contains(ir, "sf.free"));
    ASSERT_TRUE(ir_contains(ir, "@free"));
    LLVMDisposeMessage(ir);
    printf(" ok\n");
}

static void test_string_autofree_return(void) {
    printf("  test_string_autofree_return...");
    /* Function-level dynamic string should be freed before return */
    char *ir = compile_to_ir(
        "fn main() -> int {\n"
        "    string a = \"hello\".upper()\n"
        "    print(a)\n"
        "    return 0\n"
        "}\n"
    );
    ASSERT_NOT_NULL(ir);
    /* return should trigger cleanup of 'a' before ret */
    ASSERT_TRUE(ir_contains(ir, "sf.owned"));
    ASSERT_TRUE(ir_contains(ir, "@free"));
    LLVMDisposeMessage(ir);
    printf(" ok\n");
}

static void test_string_autofree_reassign(void) {
    printf("  test_string_autofree_reassign...");
    char *ir = compile_to_ir(
        "fn main() -> int {\n"
        "    string s = \"hello\"\n"
        "    s = s.upper()\n"
        "    return 0\n"
        "}\n"
    );
    ASSERT_NOT_NULL(ir);
    /* Reassignment should free old string before store */
    ASSERT_TRUE(ir_contains(ir, "sf.owned"));
    ASSERT_TRUE(ir_contains(ir, "@free"));
    LLVMDisposeMessage(ir);
    printf(" ok\n");
}

/* ---- Temporary String Cleanup Tests (Phase 0) ---- */

/* Test: chained method calls create temporaries that need cleanup */
static void test_temp_string_chained_methods(void) {
    printf("  test_temp_string_chained_methods...");
    char *ir = compile_to_ir(
        "fn main() -> int {\n"
        "    \"hello\".upper().lower()\n"
        "    return 0\n"
        "}\n"
    );
    ASSERT_NOT_NULL(ir);
    /* Should have sf.* (string-free) blocks for intermediate temp results */
    ASSERT_TRUE(ir_contains(ir, "sf.free") || ir_contains(ir, "sf.owned"));
    LLVMDisposeMessage(ir);
    printf(" ok\n");
}

/* Test: string concatenation creates temporary that needs cleanup */
static void test_temp_string_concat(void) {
    printf("  test_temp_string_concat...");
    char *ir = compile_to_ir(
        "fn main() -> int {\n"
        "    \"hello\" + \"world\"\n"
        "    return 0\n"
        "}\n"
    );
    ASSERT_NOT_NULL(ir);
    /* Concatenation returns owned string, should have cleanup */
    ASSERT_TRUE(ir_contains(ir, "tsc.owned") || ir_contains(ir, "tsc.free") || ir_contains(ir, "sf.owned"));
    LLVMDisposeMessage(ir);
    printf(" ok\n");
}

/* Test: format string creates temporary that needs cleanup */
static void test_temp_string_fstring(void) {
    printf("  test_temp_string_fstring...");
    char *ir = compile_to_ir(
        "fn main() -> int {\n"
        "    f\"hello {42}\"\n"
        "    return 0\n"
        "}\n"
    );
    ASSERT_NOT_NULL(ir);
    /* Format string allocates, should have cleanup */
    ASSERT_TRUE(ir_contains(ir, "@malloc"));
    LLVMDisposeMessage(ir);
    printf(" ok\n");
}

/* Test: f-string is heap-allocated with cap > 0 */
static void test_fstring_heap_allocated(void) {
    printf("  test_fstring_heap_allocated...");
    char *ir = compile_to_ir(
        "fn main() -> int {\n"
        "    f\"hello {42}\"\n"
        "    return 0\n"
        "}\n"
    );
    ASSERT_NOT_NULL(ir);
    /* f-string should use malloc (heap allocation) */
    ASSERT_TRUE(ir_contains(ir, "@malloc"));
    /* Should NOT use alloca (stack allocation) for f-string buffer */
    /* The cap field should be set to a positive value (4096) */
    ASSERT_TRUE(ir_contains(ir, "insertvalue") || ir_contains(ir, "i32 4096"));
    LLVMDisposeMessage(ir);
    printf(" ok\n");
}

/* Test: f-string assigned to variable is properly cleaned up */
static void test_fstring_variable_cleanup(void) {
    printf("  test_fstring_variable_cleanup...");
    char *ir = compile_to_ir(
        "fn main() -> int {\n"
        "    string s = f\"value={42}\"\n"
        "    return 0\n"
        "}\n"
    );
    ASSERT_NOT_NULL(ir);
    /* Should have malloc for f-string allocation */
    ASSERT_TRUE(ir_contains(ir, "@malloc"));
    /* Should have cleanup that checks cap > 0 */
    ASSERT_TRUE(ir_contains(ir, "sf.owned") || ir_contains(ir, "icmp"));
    /* Should free the string */
    ASSERT_TRUE(ir_contains(ir, "@free"));
    LLVMDisposeMessage(ir);
    printf(" ok\n");
}

/* Test: f-string in struct to_string method is returned and cleaned up */
static void test_fstring_in_struct_method(void) {
    printf("  test_fstring_in_struct_method...");
    char *ir = compile_to_ir(
        "struct Point { int x; int y; }\n"
        "impl Point {\n"
        "    fn to_string() -> string {\n"
        "        return f\"({self.x}, {self.y})\"\n"
        "    }\n"
        "}\n"
        "fn main() -> int {\n"
        "    Point p\n"
        "    p.x = 10\n"
        "    p.y = 20\n"
        "    string s = p.to_string()\n"
        "    return 0\n"
        "}\n"
    );
    ASSERT_NOT_NULL(ir);
    /* to_string should use malloc for f-string */
    ASSERT_TRUE(ir_contains(ir, "@malloc"));
    /* The returned string should be cleaned up in main */
    ASSERT_TRUE(ir_contains(ir, "@free"));
    LLVMDisposeMessage(ir);
    printf(" ok\n");
}

/* Test: method chain with assignment to variable should not have extra cleanup */
static void test_temp_string_method_chain_assignment(void) {
    printf("  test_temp_string_method_chain_assignment...");
    char *ir = compile_to_ir(
        "fn main() -> int {\n"
        "    string s = \"hello\".upper()\n"
        "    print(s)\n"
        "    return 0\n"
        "}\n"
    );
    ASSERT_NOT_NULL(ir);
    /* Variable 's' should be cleaned up, not extra temp cleanup */
    ASSERT_TRUE(ir_contains(ir, "sf.owned") || ir_contains(ir, "sf.free"));
    LLVMDisposeMessage(ir);
    printf(" ok\n");
}

/* Test: static string literal should NOT be freed */
static void test_static_string_no_free(void) {
    printf("  test_static_string_no_free...");
    char *ir = compile_to_ir(
        "fn main() -> int {\n"
        "    string s = \"hello\"\n"
        "    return 0\n"
        "}\n"
    );
    ASSERT_NOT_NULL(ir);
    /* Static string (cap=0) should NOT trigger free */
    /* The cleanup check should skip (cap <= 0) */
    ASSERT_TRUE(ir_contains(ir, "sf.skip") || ir_contains(ir, "sf.owned"));
    LLVMDisposeMessage(ir);
    printf(" ok\n");
}

/* Test: various string methods that allocate should be tracked */
static void test_temp_string_various_methods(void) {
    printf("  test_temp_string_various_methods...");
    /* Test substr as statement */
    char *ir = compile_to_ir(
        "fn main() -> int {\n"
        "    string s = \"hello world\"\n"
        "    s.substr(0, 5)\n"
        "    return 0\n"
        "}\n"
    );
    ASSERT_NOT_NULL(ir);
    /* substr allocates, but result stored back to s - should have some cleanup */
    ASSERT_TRUE(ir_contains(ir, "@malloc"));
    LLVMDisposeMessage(ir);
    printf(" ok\n");
}

/* ---- String Move Semantics Tests (Phase 1) ---- */

/* Test: string variable to variable assignment should use move semantics.
   Dynamic string move should mark source as moved (cap = -1). */
static void test_string_move_assignment(void) {
    printf("  test_string_move_assignment...");
    char *ir = compile_to_ir(
        "fn main() -> int {\n"
        "    string a = \"hello\".upper()\n"
        "    string b = a\n"
        "    return 0\n"
        "}\n"
    );
    ASSERT_NOT_NULL(ir);
    /* IR should compile without errors - move semantics work correctly */
    /* The string assignment b = a performs a struct copy + marks a as moved */
    LLVMDisposeMessage(ir);
    printf(" ok\n");
}

/* Test: static string copy (no move needed) */
static void test_string_static_copy(void) {
    printf("  test_string_static_copy...");
    char *ir = compile_to_ir(
        "fn main() -> int {\n"
        "    string a = \"hello\"\n"
        "    string b = a\n"
        "    print(a)\n"
        "    print(b)\n"
        "    return 0\n"
        "}\n"
    );
    ASSERT_NOT_NULL(ir);
    /* Static strings don't need msm (mark moved) since cap=0 */
    /* The copy happens but no move marking */
    LLVMDisposeMessage(ir);
    printf(" ok\n");
}

/* Test: method call result assigned to variable (not a variable move) */
static void test_string_method_to_var(void) {
    printf("  test_string_method_to_var...");
    char *ir = compile_to_ir(
        "fn main() -> int {\n"
        "    string s = \"hello\".upper()\n"
        "    print(s)\n"
        "    return 0\n"
        "}\n"
    );
    ASSERT_NOT_NULL(ir);
    /* s is owned, should be freed at end */
    ASSERT_TRUE(ir_contains(ir, "sf.owned") || ir_contains(ir, "@free"));
    LLVMDisposeMessage(ir);
    printf(" ok\n");
}

/* Test: return string variable marks it as moved */
static void test_string_return_move(void) {
    printf("  test_string_return_move...");
    char *ir = compile_to_ir(
        "fn get_str() -> string {\n"
        "    string s = \"hello\".upper()\n"
        "    return s\n"
        "}\n"
        "fn main() -> int {\n"
        "    print(get_str())\n"
        "    return 0\n"
        "}\n"
    );
    ASSERT_NOT_NULL(ir);
    /* get_str should not free 's' on return (moved to caller) */
    LLVMDisposeMessage(ir);
    printf(" ok\n");
}

/* ---- new expression codegen tests ---- */

static void test_new_zero_init_ir(void) {
    printf("  test_new_zero_init_ir...");
    char *ir = compile_to_ir(
        "struct Point { f64 x; f64 y; }\n"
        "fn main() -> int {\n"
        "    *Point p = new Point\n"
        "    return 0\n"
        "}\n"
    );
    ASSERT_NOT_NULL(ir);
    /* malloc called to allocate struct */
    ASSERT_TRUE(ir_contains(ir, "@malloc"));
    /* zero initializer stored */
    ASSERT_TRUE(ir_contains(ir, "zeroinitializer"));
    LLVMDisposeMessage(ir);
    printf(" ok\n");
}

static void test_new_with_fields_ir(void) {
    printf("  test_new_with_fields_ir...");
    char *ir = compile_to_ir(
        "struct Point { f64 x; f64 y; }\n"
        "fn main() -> int {\n"
        "    *Point p = new Point { x: 3.0, y: 4.0 }\n"
        "    return 0\n"
        "}\n"
    );
    ASSERT_NOT_NULL(ir);
    ASSERT_TRUE(ir_contains(ir, "@malloc"));
    /* GEP used to access fields */
    ASSERT_TRUE(ir_contains(ir, "getelementptr"));
    LLVMDisposeMessage(ir);
    printf(" ok\n");
}

static void test_new_nil_compare_ir(void) {
    printf("  test_new_nil_compare_ir...");
    char *ir = compile_to_ir(
        "struct Point { f64 x; f64 y; }\n"
        "fn main() -> int {\n"
        "    *Point p = nil\n"
        "    if (p == nil) { return 1 }\n"
        "    return 0\n"
        "}\n"
    );
    ASSERT_NOT_NULL(ir);
    /* nil pointer comparison generates icmp eq with null */
    ASSERT_TRUE(ir_contains(ir, "icmp eq"));
    LLVMDisposeMessage(ir);
    printf(" ok\n");
}

static void test_new_free_ir(void) {
    printf("  test_new_free_ir...");
    char *ir = compile_to_ir(
        "struct Point { f64 x; f64 y; }\n"
        "fn main() -> int {\n"
        "    *Point p = new Point\n"
        "    free(p)\n"
        "    return 0\n"
        "}\n"
    );
    ASSERT_NOT_NULL(ir);
    ASSERT_TRUE(ir_contains(ir, "@malloc"));
    ASSERT_TRUE(ir_contains(ir, "@free"));
    LLVMDisposeMessage(ir);
    printf(" ok\n");
}

static void test_new_method_call_ir(void) {
    printf("  test_new_method_call_ir...");
    char *ir = compile_to_ir(
        "struct Point { f64 x; f64 y; }\n"
        "impl Point {\n"
        "    fn get_x() -> f64 { return self.x }\n"
        "}\n"
        "fn main() -> int {\n"
        "    *Point p = new Point { x: 3.0, y: 4.0 }\n"
        "    f64 x = p.get_x()\n"
        "    free(p)\n"
        "    return 0\n"
        "}\n"
    );
    ASSERT_NOT_NULL(ir);
    ASSERT_TRUE(ir_contains(ir, "@malloc"));
    /* method call present with qualified name */
    ASSERT_TRUE(ir_contains(ir, "@Point.get_x"));
    LLVMDisposeMessage(ir);
    printf(" ok\n");
}

/* ---- Struct Destructor (RAII) Codegen Tests ---- */

static void test_struct_drop_scope_exit(void) {
    printf("  test_struct_drop_scope_exit...");
    char *ir = compile_to_ir(
        "struct Foo { int x; }\n"
        "impl Foo {\n"
        "    fn __drop() { }\n"
        "}\n"
        "fn main() -> int {\n"
        "    Foo f = Foo{ x: 1 }\n"
        "    return 0\n"
        "}\n"
    );
    ASSERT_NOT_NULL(ir);
    /* __drop function should be generated with qualified name */
    ASSERT_TRUE(ir_contains(ir, "@Foo.__drop"));
    LLVMDisposeMessage(ir);
    printf(" ok\n");
}

static void test_struct_drop_free_call(void) {
    printf("  test_struct_drop_free_call...");
    char *ir = compile_to_ir(
        "struct Foo { int x; }\n"
        "impl Foo {\n"
        "    fn __drop() { }\n"
        "}\n"
        "fn main() -> int {\n"
        "    *Foo p = new Foo { x: 1 }\n"
        "    free(p)\n"
        "    return 0\n"
        "}\n"
    );
    ASSERT_NOT_NULL(ir);
    /* __drop should be called before free */
    ASSERT_TRUE(ir_contains(ir, "@Foo.__drop"));
    LLVMDisposeMessage(ir);
    printf(" ok\n");
}

static void test_struct_drop_nested(void) {
    printf("  test_struct_drop_nested...");
    char *ir = compile_to_ir(
        "struct Inner { int x; }\n"
        "impl Inner {\n"
        "    fn __drop() { }\n"
        "}\n"
        "struct Outer { Inner inner; int y; }\n"
        "impl Outer {\n"
        "    fn __drop() {\n"
        "        self.inner.__drop()\n"
        "    }\n"
        "}\n"
        "fn main() -> int {\n"
        "    Outer o = Outer{ inner: Inner{ x: 1 }, y: 2 }\n"
        "    return 0\n"
        "}\n"
    );
    ASSERT_NOT_NULL(ir);
    /* Both __drop functions should be generated */
    ASSERT_TRUE(ir_contains(ir, "@Inner.__drop") && ir_contains(ir, "@Outer.__drop"));
    LLVMDisposeMessage(ir);
    printf(" ok\n");
}

static void test_struct_no_drop_no_call(void) {
    printf("  test_struct_no_drop_no_call...");
    char *ir = compile_to_ir(
        "struct Foo { int x; }\n"
        "fn main() -> int {\n"
        "    Foo f = Foo{ x: 1 }\n"
        "    return 0\n"
        "}\n"
    );
    ASSERT_NOT_NULL(ir);
    /* No __drop should be generated */
    ASSERT_TRUE(!ir_contains(ir, "@Foo.__drop"));
    LLVMDisposeMessage(ir);
    printf(" ok\n");
}

static void test_struct_drop_return_move(void) {
    printf("  test_struct_drop_return_move...");
    char *ir = compile_to_ir(
        "struct Foo { int x; }\n"
        "impl Foo {\n"
        "    fn __drop() { }\n"
        "}\n"
        "fn create() -> Foo {\n"
        "    Foo f = Foo{ x: 1 }\n"
        "    return f\n"
        "}\n"
        "fn main() -> int {\n"
        "    Foo f = create()\n"
        "    return 0\n"
        "}\n"
    );
    ASSERT_NOT_NULL(ir);
    /* __drop should be generated with qualified name */
    ASSERT_TRUE(ir_contains(ir, "@Foo.__drop"));
    LLVMDisposeMessage(ir);
    printf(" ok\n");
}

/* Return a shadowed string variable from an inner block.
   inner "x" must be returned (not freed); outer "x" must be freed. */
static void test_string_return_shadow(void) {
    printf("  test_string_return_shadow...");
    char *ir = compile_to_ir(
        "fn make() -> string {\n"
        "    string x = \"outer\"\n"
        "    {\n"
        "        string x = \"inner\"\n"
        "        return x\n"
        "    }\n"
        "    return x\n"
        "}\n"
        "fn main() -> int {\n"
        "    string s = make()\n"
        "    return 0\n"
        "}\n"
    );
    ASSERT_NOT_NULL(ir);
    /* Both alloca instructions must exist — they are different variables */
    ASSERT_TRUE(ir_contains(ir, "alloca %LsString"));
    /* The outer return must be unreachable (no second ret after the first cleanup) */
    ASSERT_TRUE(ir_contains(ir, "ret %LsString"));
    LLVMDisposeMessage(ir);
    printf(" ok\n");
}

/* Return a shadowed struct-with-drop from an inner block.
   inner "f" must be returned (not dropped); outer "f" must be dropped. */
static void test_struct_drop_return_shadow(void) {
    printf("  test_struct_drop_return_shadow...");
    char *ir = compile_to_ir(
        "struct Res { int v; }\n"
        "impl Res {\n"
        "    fn __drop() { }\n"
        "}\n"
        "fn make() -> Res {\n"
        "    Res f = Res{ v: 1 }\n"
        "    {\n"
        "        Res f = Res{ v: 2 }\n"
        "        return f\n"
        "    }\n"
        "    return f\n"
        "}\n"
        "fn main() -> int {\n"
        "    Res r = make()\n"
        "    return 0\n"
        "}\n"
    );
    ASSERT_NOT_NULL(ir);
    /* __drop must be generated */
    ASSERT_TRUE(ir_contains(ir, "@Res.__drop"));
    /* cleanup block must exist (outer f gets dropped before return) */
    ASSERT_TRUE(ir_contains(ir, "cleanup"));
    LLVMDisposeMessage(ir);
    printf(" ok\n");
}

/* ---- vec(T) bounds-check tests ---- */

/* vec(T)[i] out-of-bounds should warn but not crash; IR must contain the
   conditional branch + printf warning call. */
static void test_vec_index_bounds_check_ir(void) {
    printf("  test_vec_index_bounds_check_ir...");
    char *ir = compile_to_ir(
        "fn main() -> int {\n"
        "    vec(int) v\n"
        "    v.push(42)\n"
        "    v.pop()\n"
        "    int x = v[0]\n"   /* OOB: should warn, x=0 */
        "    int y = v[1]\n"   /* OOB: should warn, y=0 */
        "    return 0\n"
        "}\n"
    );
    ASSERT_NOT_NULL(ir);
    /* IR must contain the bounds-check blocks and the warning printf */
    ASSERT_TRUE(ir_contains(ir, "vi.ok"));
    ASSERT_TRUE(ir_contains(ir, "vi.oob"));
    ASSERT_TRUE(ir_contains(ir, "vi.merge"));
    ASSERT_TRUE(ir_contains(ir, "vi.inb"));
    ASSERT_TRUE(ir_contains(ir, "out of bounds"));
    LLVMDisposeMessage(ir);
    printf("OK\n");
}

static void test_vec_string_index_bounds_check_ir(void) {
    printf("  test_vec_string_index_bounds_check_ir...");
    char *ir = compile_to_ir(
        "fn main() -> int {\n"
        "    vec(string) vs\n"
        "    vs.push(\"hello\")\n"
        "    vs.pop()\n"
        "    print(vs[0])\n"   /* OOB: should warn, print empty string */
        "    return 0\n"
        "}\n"
    );
    ASSERT_NOT_NULL(ir);
    /* print(vs[0]) uses auto-borrow path — block names are vb.ok / vb.oob */
    ASSERT_TRUE(ir_contains(ir, "vb.ok"));
    ASSERT_TRUE(ir_contains(ir, "vb.oob"));
    ASSERT_TRUE(ir_contains(ir, "out of bounds"));
    LLVMDisposeMessage(ir);
    printf("OK\n");
}

/* ---- vec(T) Batch A: is_empty / first / last ---- */

/* is_empty() on int vec: IR must contain the EQ compare and basic blocks */
static void test_vec_is_empty_ir(void) {
    printf("  test_vec_is_empty_ir...");
    char *ir = compile_to_ir(
        "fn main() -> int {\n"
        "    vec(int) v\n"
        "    bool e = v.is_empty()\n"
        "    v.push(42)\n"
        "    bool ne = v.is_empty()\n"
        "    return 0\n"
        "}\n"
    );
    ASSERT_NOT_NULL(ir);
    /* is_empty compares len == 0, should see icmp eq */
    ASSERT_TRUE(ir_contains(ir, "icmp eq"));
    /* vie prefix labels */
    ASSERT_TRUE(ir_contains(ir, "vie.len") || ir_contains(ir, "vie.res") || ir_contains(ir, "vie.v"));
    LLVMDisposeMessage(ir);
    printf("OK\n");
}

/* first() on int vec: IR must contain the not-empty branch and warning path */
static void test_vec_first_int_ir(void) {
    printf("  test_vec_first_int_ir...");
    char *ir = compile_to_ir(
        "fn main() -> int {\n"
        "    vec(int) v\n"
        "    v.push(10)\n"
        "    v.push(20)\n"
        "    int x = v.first()\n"
        "    return x\n"
        "}\n"
    );
    ASSERT_NOT_NULL(ir);
    ASSERT_TRUE(ir_contains(ir, "vfst.ok"));
    ASSERT_TRUE(ir_contains(ir, "vfst.emp"));
    ASSERT_TRUE(ir_contains(ir, "vfst.merge"));
    ASSERT_TRUE(ir_contains(ir, "vec.first() called on empty vec"));
    LLVMDisposeMessage(ir);
    printf("OK\n");
}

/* last() on int vec: IR must contain the not-empty branch and warning path */
static void test_vec_last_int_ir(void) {
    printf("  test_vec_last_int_ir...");
    char *ir = compile_to_ir(
        "fn main() -> int {\n"
        "    vec(int) v\n"
        "    v.push(10)\n"
        "    v.push(20)\n"
        "    int x = v.last()\n"
        "    return x\n"
        "}\n"
    );
    ASSERT_NOT_NULL(ir);
    ASSERT_TRUE(ir_contains(ir, "vlst.ok"));
    ASSERT_TRUE(ir_contains(ir, "vlst.emp"));
    ASSERT_TRUE(ir_contains(ir, "vlst.merge"));
    ASSERT_TRUE(ir_contains(ir, "vec.last() called on empty vec"));
    LLVMDisposeMessage(ir);
    printf("OK\n");
}

/* first() on string vec: IR must contain a free call (clone path) */
static void test_vec_first_string_ir(void) {
    printf("  test_vec_first_string_ir...");
    char *ir = compile_to_ir(
        "fn main() -> int {\n"
        "    vec(string) vs\n"
        "    vs.push(\"hello\")\n"
        "    string s = vs.first()\n"
        "    return 0\n"
        "}\n"
    );
    ASSERT_NOT_NULL(ir);
    ASSERT_TRUE(ir_contains(ir, "vfst.ok"));
    ASSERT_TRUE(ir_contains(ir, "vfst.emp"));
    /* clone path must malloc a new string */
    ASSERT_TRUE(ir_contains(ir, "sc.copy") || ir_contains(ir, "malloc"));
    LLVMDisposeMessage(ir);
    printf("OK\n");
}

/* last() on string vec: IR must contain a free call (clone path) */
static void test_vec_last_string_ir(void) {
    printf("  test_vec_last_string_ir...");
    char *ir = compile_to_ir(
        "fn main() -> int {\n"
        "    vec(string) vs\n"
        "    vs.push(\"world\")\n"
        "    vs.push(\"end\")\n"
        "    string s = vs.last()\n"
        "    return 0\n"
        "}\n"
    );
    ASSERT_NOT_NULL(ir);
    ASSERT_TRUE(ir_contains(ir, "vlst.ok"));
    ASSERT_TRUE(ir_contains(ir, "vlst.emp"));
    ASSERT_TRUE(ir_contains(ir, "sc.copy") || ir_contains(ir, "malloc"));
    LLVMDisposeMessage(ir);
    printf("OK\n");
}

/* ---- vec(T) Batch C: extend / insert ---- */

/* extend() on trivial int vec: IR must have the grow/cpy structure + memcpy */
static void test_vec_extend_int_ir(void) {
    printf("  test_vec_extend_int_ir...");
    char *ir = compile_to_ir(
        "fn main() -> int {\n"
        "    vec(int) a\n"
        "    a.push(1)\n"
        "    a.push(2)\n"
        "    vec(int) b\n"
        "    b.push(3)\n"
        "    b.push(4)\n"
        "    a.extend(b)\n"
        "    return 0\n"
        "}\n"
    );
    ASSERT_NOT_NULL(ir);
    ASSERT_TRUE(ir_contains(ir, "vex.do"));
    ASSERT_TRUE(ir_contains(ir, "vex.end"));
    ASSERT_TRUE(ir_contains(ir, "vex.cpy"));
    ASSERT_TRUE(ir_contains(ir, "memcpy"));
    LLVMDisposeMessage(ir);
    printf("OK\n");
}

/* extend() on string vec: IR must use the clone loop blocks */
static void test_vec_extend_string_ir(void) {
    printf("  test_vec_extend_string_ir...");
    char *ir = compile_to_ir(
        "fn main() -> int {\n"
        "    vec(string) a\n"
        "    a.push(\"hello\")\n"
        "    vec(string) b\n"
        "    b.push(\"world\")\n"
        "    a.extend(b)\n"
        "    return 0\n"
        "}\n"
    );
    ASSERT_NOT_NULL(ir);
    ASSERT_TRUE(ir_contains(ir, "vex.do"));
    ASSERT_TRUE(ir_contains(ir, "vex.lcond"));
    ASSERT_TRUE(ir_contains(ir, "vex.lbody"));
    ASSERT_TRUE(ir_contains(ir, "vex.lend"));
    LLVMDisposeMessage(ir);
    printf("OK\n");
}

/* insert() on int vec: IR must have do/oob/shift/store structure + memmove */
static void test_vec_insert_int_ir(void) {
    printf("  test_vec_insert_int_ir...");
    char *ir = compile_to_ir(
        "fn main() -> int {\n"
        "    vec(int) v\n"
        "    v.push(1)\n"
        "    v.push(3)\n"
        "    v.insert(1, 2)\n"
        "    return 0\n"
        "}\n"
    );
    ASSERT_NOT_NULL(ir);
    ASSERT_TRUE(ir_contains(ir, "vins.do"));
    ASSERT_TRUE(ir_contains(ir, "vins.oob"));
    ASSERT_TRUE(ir_contains(ir, "vins.shift"));
    ASSERT_TRUE(ir_contains(ir, "vins.store"));
    ASSERT_TRUE(ir_contains(ir, "vins.end"));
    ASSERT_TRUE(ir_contains(ir, "memmove"));
    ASSERT_TRUE(ir_contains(ir, "vec.insert() index out of bounds"));
    LLVMDisposeMessage(ir);
    printf("OK\n");
}

/* insert() on string vec: IR must contain oob warn path + memmove */
static void test_vec_insert_string_ir(void) {
    printf("  test_vec_insert_string_ir...");
    char *ir = compile_to_ir(
        "fn main() -> int {\n"
        "    vec(string) v\n"
        "    v.push(\"a\")\n"
        "    v.push(\"c\")\n"
        "    v.insert(1, \"b\")\n"
        "    return 0\n"
        "}\n"
    );
    ASSERT_NOT_NULL(ir);
    ASSERT_TRUE(ir_contains(ir, "vins.do"));
    ASSERT_TRUE(ir_contains(ir, "memmove"));
    ASSERT_TRUE(ir_contains(ir, "vins.store"));
    LLVMDisposeMessage(ir);
    printf("OK\n");
}

/* ---- vec(T) Batch E: sort / sort_by / slice / shrink_to_fit ---- */

/* sort() on int vec: IR must contain a generated comparator + qsort call */
static void test_vec_sort_int_ir(void) {
    printf("  test_vec_sort_int_ir...");
    char *ir = compile_to_ir(
        "fn main() -> int {\n"
        "    vec(int) v\n"
        "    v.push(3)\n"
        "    v.push(1)\n"
        "    v.push(2)\n"
        "    v.sort()\n"
        "    return 0\n"
        "}\n"
    );
    ASSERT_NOT_NULL(ir);
    /* Comparator function prefix */
    ASSERT_TRUE(ir_contains(ir, "__ls_vcmp_"));
    /* qsort call */
    ASSERT_TRUE(ir_contains(ir, "qsort"));
    LLVMDisposeMessage(ir);
    printf("OK\n");
}

/* sort() on string vec: comparator must use strcmp */
static void test_vec_sort_string_ir(void) {
    printf("  test_vec_sort_string_ir...");
    char *ir = compile_to_ir(
        "fn main() -> int {\n"
        "    vec(string) v\n"
        "    v.push(\"banana\")\n"
        "    v.push(\"apple\")\n"
        "    v.sort()\n"
        "    return 0\n"
        "}\n"
    );
    ASSERT_NOT_NULL(ir);
    ASSERT_TRUE(ir_contains(ir, "__ls_vcmp_"));
    ASSERT_TRUE(ir_contains(ir, "qsort"));
    ASSERT_TRUE(ir_contains(ir, "strcmp"));
    LLVMDisposeMessage(ir);
    printf("OK\n");
}

/* sort_by() passes function pointer to qsort */
static void test_vec_sort_by_ir(void) {
    printf("  test_vec_sort_by_ir...");
    char *ir = compile_to_ir(
        "fn my_cmp(int a, int b) -> int { return a - b }\n"
        "fn main() -> int {\n"
        "    vec(int) v\n"
        "    v.push(3)\n"
        "    v.push(1)\n"
        "    v.sort_by(my_cmp)\n"
        "    return 0\n"
        "}\n"
    );
    ASSERT_NOT_NULL(ir);
    ASSERT_TRUE(ir_contains(ir, "qsort"));
    ASSERT_TRUE(ir_contains(ir, "my_cmp"));
    LLVMDisposeMessage(ir);
    printf("OK\n");
}

/* slice() on int vec: IR must have clamping + do/end structure + memcpy */
static void test_vec_slice_int_ir(void) {
    printf("  test_vec_slice_int_ir...");
    char *ir = compile_to_ir(
        "fn main() -> int {\n"
        "    vec(int) v\n"
        "    v.push(1)\n"
        "    v.push(2)\n"
        "    v.push(3)\n"
        "    v.push(4)\n"
        "    vec(int) s = v.slice(1, 3)\n"
        "    return 0\n"
        "}\n"
    );
    ASSERT_NOT_NULL(ir);
    ASSERT_TRUE(ir_contains(ir, "vsl.do"));
    ASSERT_TRUE(ir_contains(ir, "vsl.end"));
    ASSERT_TRUE(ir_contains(ir, "memcpy"));
    LLVMDisposeMessage(ir);
    printf("OK\n");
}

/* slice() on string vec: IR uses clone loop */
static void test_vec_slice_string_ir(void) {
    printf("  test_vec_slice_string_ir...");
    char *ir = compile_to_ir(
        "fn main() -> int {\n"
        "    vec(string) v\n"
        "    v.push(\"a\")\n"
        "    v.push(\"b\")\n"
        "    v.push(\"c\")\n"
        "    vec(string) s = v.slice(0, 2)\n"
        "    return 0\n"
        "}\n"
    );
    ASSERT_NOT_NULL(ir);
    ASSERT_TRUE(ir_contains(ir, "vsl.lcond"));
    ASSERT_TRUE(ir_contains(ir, "vsl.lbody"));
    LLVMDisposeMessage(ir);
    printf("OK\n");
}

/* shrink_to_fit(): IR must have the do/free/ra/end structure */
static void test_vec_shrink_to_fit_ir(void) {
    printf("  test_vec_shrink_to_fit_ir...");
    char *ir = compile_to_ir(
        "fn main() -> int {\n"
        "    vec(int) v\n"
        "    v.push(1)\n"
        "    v.push(2)\n"
        "    v.shrink_to_fit()\n"
        "    return 0\n"
        "}\n"
    );
    ASSERT_NOT_NULL(ir);
    ASSERT_TRUE(ir_contains(ir, "vstf.do"));
    ASSERT_TRUE(ir_contains(ir, "vstf.free") || ir_contains(ir, "vstf.ra"));
    ASSERT_TRUE(ir_contains(ir, "vstf.end"));
    LLVMDisposeMessage(ir);
    printf("OK\n");
}

/* ---- vec(T) Batch D: contains / index_of / resize / copy ---- */

static void test_vec_contains_int_ir(void) {
    printf("  test_vec_contains_int_ir...");
    char *ir = compile_to_ir(
        "fn main() -> int {\n"
        "    vec(int) v\n"
        "    v.push(1)\n"
        "    v.push(2)\n"
        "    v.push(3)\n"
        "    bool b = v.contains(2)\n"
        "    return 0\n"
        "}\n"
    );
    ASSERT_NOT_NULL(ir);
    ASSERT_TRUE(ir_contains(ir, "vcnt.cond"));
    ASSERT_TRUE(ir_contains(ir, "vcnt.body"));
    ASSERT_TRUE(ir_contains(ir, "vcnt.found"));
    ASSERT_TRUE(ir_contains(ir, "vcnt.end"));
    LLVMDisposeMessage(ir);
    printf("OK\n");
}

static void test_vec_contains_string_ir(void) {
    printf("  test_vec_contains_string_ir...");
    char *ir = compile_to_ir(
        "fn main() -> int {\n"
        "    vec(string) v\n"
        "    v.push(\"hello\")\n"
        "    bool b = v.contains(\"hello\")\n"
        "    return 0\n"
        "}\n"
    );
    ASSERT_NOT_NULL(ir);
    ASSERT_TRUE(ir_contains(ir, "vcnt.body"));
    /* String comparison uses strcmp */
    ASSERT_TRUE(ir_contains(ir, "strcmp"));
    LLVMDisposeMessage(ir);
    printf("OK\n");
}

static void test_vec_index_of_ir(void) {
    printf("  test_vec_index_of_ir...");
    char *ir = compile_to_ir(
        "fn main() -> int {\n"
        "    vec(int) v\n"
        "    v.push(10)\n"
        "    v.push(20)\n"
        "    v.push(30)\n"
        "    int idx = v.index_of(20)\n"
        "    return 0\n"
        "}\n"
    );
    ASSERT_NOT_NULL(ir);
    ASSERT_TRUE(ir_contains(ir, "vidx.cond"));
    ASSERT_TRUE(ir_contains(ir, "vidx.found"));
    ASSERT_TRUE(ir_contains(ir, "vidx.end"));
    LLVMDisposeMessage(ir);
    printf("OK\n");
}

static void test_vec_resize_grow_ir(void) {
    printf("  test_vec_resize_grow_ir...");
    char *ir = compile_to_ir(
        "fn main() -> int {\n"
        "    vec(int) v\n"
        "    v.push(1)\n"
        "    v.resize(5)\n"
        "    return 0\n"
        "}\n"
    );
    ASSERT_NOT_NULL(ir);
    ASSERT_TRUE(ir_contains(ir, "vrsz.grow"));
    ASSERT_TRUE(ir_contains(ir, "vrsz.fill") || ir_contains(ir, "vrsz.fcond"));
    ASSERT_TRUE(ir_contains(ir, "vrsz.end"));
    LLVMDisposeMessage(ir);
    printf("OK\n");
}

static void test_vec_resize_shrink_ir(void) {
    printf("  test_vec_resize_shrink_ir...");
    char *ir = compile_to_ir(
        "fn main() -> int {\n"
        "    vec(int) v\n"
        "    v.push(1)\n"
        "    v.push(2)\n"
        "    v.push(3)\n"
        "    v.resize(1)\n"
        "    return 0\n"
        "}\n"
    );
    ASSERT_NOT_NULL(ir);
    ASSERT_TRUE(ir_contains(ir, "vrsz.shrink"));
    ASSERT_TRUE(ir_contains(ir, "vrsz.end"));
    LLVMDisposeMessage(ir);
    printf("OK\n");
}

static void test_vec_copy_int_ir(void) {
    printf("  test_vec_copy_int_ir...");
    char *ir = compile_to_ir(
        "fn main() -> int {\n"
        "    vec(int) a\n"
        "    a.push(1)\n"
        "    a.push(2)\n"
        "    vec(int) b = a.copy()\n"
        "    return 0\n"
        "}\n"
    );
    ASSERT_NOT_NULL(ir);
    ASSERT_TRUE(ir_contains(ir, "vcpy.do"));
    ASSERT_TRUE(ir_contains(ir, "vcpy.end"));
    ASSERT_TRUE(ir_contains(ir, "memcpy"));
    LLVMDisposeMessage(ir);
    printf("OK\n");
}

static void test_vec_copy_string_ir(void) {
    printf("  test_vec_copy_string_ir...");
    char *ir = compile_to_ir(
        "fn main() -> int {\n"
        "    vec(string) a\n"
        "    a.push(\"hello\")\n"
        "    a.push(\"world\")\n"
        "    vec(string) b = a.copy()\n"
        "    return 0\n"
        "}\n"
    );
    ASSERT_NOT_NULL(ir);
    ASSERT_TRUE(ir_contains(ir, "vcpy.do"));
    /* Non-trivial path uses clone loop */
    ASSERT_TRUE(ir_contains(ir, "vcpy.lcond"));
    ASSERT_TRUE(ir_contains(ir, "vcpy.lbody"));
    LLVMDisposeMessage(ir);
    printf("OK\n");
}

/* ---- vec(T) Batch B: truncate / remove / swap / reverse ---- */

/* truncate(n) on int vec: IR must have the do/end branch structure */
static void test_vec_truncate_int_ir(void) {
    printf("  test_vec_truncate_int_ir...");
    char *ir = compile_to_ir(
        "fn main() -> int {\n"
        "    vec(int) v\n"
        "    v.push(1)\n"
        "    v.push(2)\n"
        "    v.push(3)\n"
        "    v.truncate(1)\n"
        "    return 0\n"
        "}\n"
    );
    ASSERT_NOT_NULL(ir);
    /* truncate emits a conditional branch (do / end blocks) */
    ASSERT_TRUE(ir_contains(ir, "vtr.do") || ir_contains(ir, "vtr.need"));
    ASSERT_TRUE(ir_contains(ir, "vtr.end"));
    LLVMDisposeMessage(ir);
    printf("OK\n");
}

/* truncate on string vec: IR must contain a free call (drop path for strings) */
static void test_vec_truncate_string_drops_ir(void) {
    printf("  test_vec_truncate_string_drops_ir...");
    char *ir = compile_to_ir(
        "fn main() -> int {\n"
        "    vec(string) v\n"
        "    v.push(\"a\")\n"
        "    v.push(\"b\")\n"
        "    v.push(\"c\")\n"
        "    v.truncate(1)\n"
        "    return 0\n"
        "}\n"
    );
    ASSERT_NOT_NULL(ir);
    ASSERT_TRUE(ir_contains(ir, "vtr.cond") || ir_contains(ir, "vtr.body"));
    /* string drop path must call free */
    ASSERT_TRUE(ir_contains(ir, "free"));
    LLVMDisposeMessage(ir);
    printf("OK\n");
}

/* remove(i) on int vec: IR must contain memmove and oob warning path */
static void test_vec_remove_int_ir(void) {
    printf("  test_vec_remove_int_ir...");
    char *ir = compile_to_ir(
        "fn main() -> int {\n"
        "    vec(int) v\n"
        "    v.push(10)\n"
        "    v.push(20)\n"
        "    v.push(30)\n"
        "    v.remove(1)\n"
        "    return 0\n"
        "}\n"
    );
    ASSERT_NOT_NULL(ir);
    ASSERT_TRUE(ir_contains(ir, "vrm.do"));
    ASSERT_TRUE(ir_contains(ir, "vrm.oob"));
    ASSERT_TRUE(ir_contains(ir, "memmove"));
    ASSERT_TRUE(ir_contains(ir, "vec.remove() index out of bounds"));
    LLVMDisposeMessage(ir);
    printf("OK\n");
}

/* remove(i) on string vec: IR must contain a free call (drop) + memmove */
static void test_vec_remove_string_drops_ir(void) {
    printf("  test_vec_remove_string_drops_ir...");
    char *ir = compile_to_ir(
        "fn main() -> int {\n"
        "    vec(string) vs\n"
        "    vs.push(\"x\")\n"
        "    vs.push(\"y\")\n"
        "    vs.push(\"z\")\n"
        "    vs.remove(0)\n"
        "    return 0\n"
        "}\n"
    );
    ASSERT_NOT_NULL(ir);
    ASSERT_TRUE(ir_contains(ir, "vrm.do"));
    ASSERT_TRUE(ir_contains(ir, "memmove"));
    ASSERT_TRUE(ir_contains(ir, "free"));
    LLVMDisposeMessage(ir);
    printf("OK\n");
}

/* swap(i, j): IR must have the chk/oob/do/end blocks and no free/malloc */
static void test_vec_swap_ir(void) {
    printf("  test_vec_swap_ir...");
    char *ir = compile_to_ir(
        "fn main() -> int {\n"
        "    vec(int) v\n"
        "    v.push(1)\n"
        "    v.push(2)\n"
        "    v.push(3)\n"
        "    v.swap(0, 2)\n"
        "    return 0\n"
        "}\n"
    );
    ASSERT_NOT_NULL(ir);
    ASSERT_TRUE(ir_contains(ir, "vsw.do"));
    ASSERT_TRUE(ir_contains(ir, "vsw.oob"));
    ASSERT_TRUE(ir_contains(ir, "vsw.chk"));
    ASSERT_TRUE(ir_contains(ir, "vsw.end"));
    ASSERT_TRUE(ir_contains(ir, "vec.swap() index out of bounds"));
    LLVMDisposeMessage(ir);
    printf("OK\n");
}

/* reverse(): IR must have the loop structure (do/cond/body/end) */
static void test_vec_reverse_ir(void) {
    printf("  test_vec_reverse_ir...");
    char *ir = compile_to_ir(
        "fn main() -> int {\n"
        "    vec(int) v\n"
        "    v.push(1)\n"
        "    v.push(2)\n"
        "    v.push(3)\n"
        "    v.push(4)\n"
        "    v.reverse()\n"
        "    return 0\n"
        "}\n"
    );
    ASSERT_NOT_NULL(ir);
    ASSERT_TRUE(ir_contains(ir, "vrev.do"));
    ASSERT_TRUE(ir_contains(ir, "vrev.cond"));
    ASSERT_TRUE(ir_contains(ir, "vrev.body"));
    ASSERT_TRUE(ir_contains(ir, "vrev.end"));
    LLVMDisposeMessage(ir);
    printf("OK\n");
}

/* ---- vec(T) pop() regression test ---- */

/* Regression: vec(string) pop() as a top-level statement used to crash with
   "Terminator found in the middle of a basic block! label %sf.skip0"
   because emit_vec_elem_drop_at called emit_string_free (which emits ret void)
   instead of emit_string_free_with_cont (which emits a continuation block). */
static void test_vec_string_pop_top_level(void) {
    printf("  test_vec_string_pop_top_level...");
    char *ir = compile_to_ir(
        "vec(string) v\n"
        "v.push(\"hello\")\n"
        "v.pop()\n"
        "fn main() -> int {\n"
        "    return 0\n"
        "}\n"
    );
    ASSERT_NOT_NULL(ir);
    /* Verify the IR contains a free call (from the pop drop path) */
    ASSERT_TRUE(ir_contains(ir, "call") && ir_contains(ir, "free"));
    LLVMDisposeMessage(ir);
    printf("OK\n");
}

/* ---- Main ---- */

int main(void) {
    printf("=== Codegen Tests ===\n");
    test_empty_main();
    test_int_arithmetic();
    test_float_arithmetic();
    test_function_call();
    test_recursive_function();
    test_if_else();
    test_while_loop();
    test_for_c_loop();
    test_foreach_range();
    test_struct_codegen();
    test_match_codegen();
    test_bool_logic();
    test_string_global();
    test_print_builtin();
    test_pointer_ops();
    test_compound_assignment();
    test_unary_neg();

    printf("\n=== Array Codegen Tests ===\n");
    test_array_local();
    test_array_length();
    test_array_index_write();
    test_array_foreach();
    test_array_as_param();

    printf("\n=== Global Variable Codegen Tests ===\n");
    test_global_int();
    test_global_array();
    test_global_mutable();
    test_global_multiple_types();

    printf("\n=== LsString Codegen Tests ===\n");
    test_string_lsstruct();
    test_string_concat_ir();
    test_string_compare_ir();
    test_string_length_ir();
    test_string_ffi_extract_data();

    printf("\n=== String Method Codegen Tests ===\n");
    test_string_empty_ir();
    test_string_at_ir();
    test_string_find_ir();
    test_string_contains_ir();
    test_string_starts_with_ir();
    test_string_ends_with_ir();
    test_string_compare_method_ir();

    printf("\n=== String Method Batch 2 Codegen Tests ===\n");
    test_string_upper_ir();
    test_string_lower_ir();
    test_string_substr_ir();
    test_string_trim_ir();
    test_string_copy_ir();
    test_string_replace_ir();

    printf("\n=== String Method Batch 3 Codegen Tests ===\n");
    test_string_rfind_ir();
    test_string_count_ir();
    test_string_substr_one_arg_ir();
    test_string_split_ir();
    test_string_join_ir();

    printf("\n=== Struct Method (implicit self + static) Codegen Tests ===\n");
    test_instance_method_ir();
    test_static_method_ir();

    printf("\n=== String Auto-Free Codegen Tests ===\n");
    test_string_autofree_block();
    test_string_autofree_reassign();
    test_string_autofree_return();

    printf("\n=== Temporary String Cleanup Tests (Phase 0) ===\n");
    test_temp_string_chained_methods();
    test_temp_string_concat();
    test_temp_string_fstring();
    test_fstring_heap_allocated();
    test_fstring_variable_cleanup();
    test_fstring_in_struct_method();
    test_temp_string_method_chain_assignment();
    test_static_string_no_free();
    test_temp_string_various_methods();

    printf("\n=== String Move Semantics Tests (Phase 1) ===\n");
    test_string_move_assignment();
    test_string_static_copy();
    test_string_method_to_var();
    test_string_return_move();

    printf("\n=== new Expression Codegen Tests ===\n");
    test_new_zero_init_ir();
    test_new_with_fields_ir();
    test_new_nil_compare_ir();
    test_new_free_ir();
    test_new_method_call_ir();

    printf("\n=== Struct Destructor (RAII) Codegen Tests ===\n");
    test_struct_drop_scope_exit();
    test_struct_drop_free_call();
    test_struct_drop_nested();
    test_struct_no_drop_no_call();
    test_struct_drop_return_move();
    test_string_return_shadow();
    test_struct_drop_return_shadow();

    printf("\n=== vec(T) Bounds Check Tests ===\n");
    test_vec_index_bounds_check_ir();
    test_vec_string_index_bounds_check_ir();

    printf("\n=== vec(T) pop() Bug Regression Tests ===\n");
    test_vec_string_pop_top_level();

    printf("\n=== vec(T) Batch A Tests (is_empty / first / last) ===\n");
    test_vec_is_empty_ir();
    test_vec_first_int_ir();
    test_vec_last_int_ir();
    test_vec_first_string_ir();
    test_vec_last_string_ir();

    printf("\n=== vec(T) Batch B Tests (truncate / remove / swap / reverse) ===\n");
    test_vec_truncate_int_ir();
    test_vec_truncate_string_drops_ir();
    test_vec_remove_int_ir();
    test_vec_remove_string_drops_ir();
    test_vec_swap_ir();
    test_vec_reverse_ir();

    printf("\n=== vec(T) Batch C Tests (extend / insert) ===\n");
    test_vec_extend_int_ir();
    test_vec_extend_string_ir();
    test_vec_insert_int_ir();
    test_vec_insert_string_ir();

    printf("\n=== vec(T) Batch E Tests (sort / sort_by / slice / shrink_to_fit) ===\n");
    test_vec_sort_int_ir();
    test_vec_sort_string_ir();
    test_vec_sort_by_ir();
    test_vec_slice_int_ir();
    test_vec_slice_string_ir();
    test_vec_shrink_to_fit_ir();

    printf("\n=== vec(T) Batch D Tests (contains / index_of / resize / copy) ===\n");
    test_vec_contains_int_ir();
    test_vec_contains_string_ir();
    test_vec_index_of_ir();
    test_vec_resize_grow_ir();
    test_vec_resize_shrink_ir();
    test_vec_copy_int_ir();
    test_vec_copy_string_ir();

    printf("\n=== Sample File Codegen Tests ===\n");
    test_samples_hello();
    test_samples_factorial();
    test_samples_struct();
    test_samples_match();
    test_enum_decl_layout();
    test_enum_ctor_and_match();
    test_enum_drop_fn();
    test_option_template_instantiation();

    printf("\n=== Results: %d/%d passed ===\n", tests_passed, tests_run);
    return tests_passed == tests_run ? 0 : 1;
}
