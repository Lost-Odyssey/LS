/* test_types.c — Unit tests for type system, symbol table, and type checker */
#include "common.h"
#include "types.h"
#include "symtable.h"
#include "checker.h"
#include "parser.h"
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

#define ASSERT_EQ(a, b) ASSERT((a) == (b), #a " != " #b)
#define ASSERT_TRUE(cond) ASSERT((cond), #cond " is false")
#define ASSERT_FALSE(cond) ASSERT(!(cond), #cond " is true")
#define ASSERT_NULL(p) ASSERT((p) == NULL, #p " is not NULL")
#define ASSERT_NOT_NULL(p) ASSERT((p) != NULL, #p " is NULL")
#define ASSERT_STR_EQ(a, b) ASSERT(strcmp(a, b) == 0, #a " != " #b)

/* ---- Type system tests ---- */

static void test_primitive_singletons(void) {
    printf("  test_primitive_singletons...");
    ASSERT_TRUE(type_int() == type_int());
    ASSERT_TRUE(type_f64() == type_f64());
    ASSERT_TRUE(type_bool() == type_bool());
    ASSERT_TRUE(type_string() == type_string());
    ASSERT_TRUE(type_void() == type_void());
    ASSERT_TRUE(type_nil() == type_nil());
    ASSERT_TRUE(type_int() != type_f64());
    printf(" ok\n");
}

static void test_type_equality(void) {
    printf("  test_type_equality...");
    ASSERT_TRUE(type_equals(type_int(), type_int()));
    ASSERT_FALSE(type_equals(type_int(), type_f64()));
    ASSERT_FALSE(type_equals(type_int(), type_bool()));

    /* Pointer equality */
    Type *p1 = type_pointer(type_int());
    Type *p2 = type_pointer(type_int());
    Type *p3 = type_pointer(type_f64());
    ASSERT_TRUE(type_equals(p1, p2));
    ASSERT_FALSE(type_equals(p1, p3));
    type_free(p1);
    type_free(p2);
    type_free(p3);

    /* Array equality (includes size) */
    Type *a1 = type_array(type_int(), 10);
    Type *a2 = type_array(type_int(), 10);
    Type *a3 = type_array(type_f64(), 10);
    Type *a4 = type_array(type_int(), 5);
    ASSERT_TRUE(type_equals(a1, a2));
    ASSERT_FALSE(type_equals(a1, a3));
    ASSERT_FALSE(type_equals(a1, a4));   /* same elem, different size */
    type_free(a1);
    type_free(a2);
    type_free(a3);
    type_free(a4);

    printf(" ok\n");
}

static void test_type_queries(void) {
    printf("  test_type_queries...");
    ASSERT_TRUE(type_is_integer(type_int()));
    ASSERT_TRUE(type_is_integer(type_i8()));
    ASSERT_TRUE(type_is_integer(type_i64()));
    ASSERT_TRUE(type_is_integer(type_u8()));
    ASSERT_TRUE(type_is_integer(type_u64()));
    ASSERT_FALSE(type_is_integer(type_f32()));
    ASSERT_FALSE(type_is_integer(type_bool()));

    ASSERT_TRUE(type_is_float(type_f32()));
    ASSERT_TRUE(type_is_float(type_f64()));
    ASSERT_FALSE(type_is_float(type_int()));

    ASSERT_TRUE(type_is_numeric(type_int()));
    ASSERT_TRUE(type_is_numeric(type_f64()));
    ASSERT_FALSE(type_is_numeric(type_bool()));
    ASSERT_FALSE(type_is_numeric(type_string()));

    ASSERT_TRUE(type_is_signed(type_int()));
    ASSERT_TRUE(type_is_signed(type_i32()));
    ASSERT_FALSE(type_is_signed(type_u32()));

    ASSERT_TRUE(type_is_unsigned(type_u8()));
    ASSERT_TRUE(type_is_unsigned(type_u64()));
    ASSERT_FALSE(type_is_unsigned(type_int()));

    printf(" ok\n");
}

static void test_type_name(void) {
    printf("  test_type_name...");
    ASSERT_STR_EQ(type_name(type_int()), "int");
    ASSERT_STR_EQ(type_name(type_f64()), "f64");
    ASSERT_STR_EQ(type_name(type_bool()), "bool");
    ASSERT_STR_EQ(type_name(type_string()), "string");
    ASSERT_STR_EQ(type_name(type_void()), "void");
    ASSERT_STR_EQ(type_name(NULL), "void");
    printf(" ok\n");
}

static void test_type_clone(void) {
    printf("  test_type_clone...");
    /* Primitive clone returns same singleton */
    ASSERT_TRUE(type_clone(type_int()) == type_int());

    /* Pointer clone */
    Type *p = type_pointer(type_int());
    Type *pc = type_clone(p);
    ASSERT_TRUE(type_equals(p, pc));
    ASSERT_TRUE(p != pc);
    type_free(p);
    type_free(pc);

    printf(" ok\n");
}

static void test_type_enum(void) {
    printf("  test_type_enum...");
    /* Build enum Result(int, string) { Ok(int); Err(string) } */
    Type *t = type_enum("Result(int,string)", 2);
    ASSERT_EQ(t->kind, TYPE_ENUM);
    ASSERT_STR_EQ(t->as.enom.name, "Result(int,string)");
    ASSERT_EQ(t->as.enom.variant_count, 2);

    /* Variant 0: Ok(int) */
    char *vn0 = (char *)malloc_safe(3); memcpy(vn0, "Ok", 3);
    t->as.enom.variants[0].name = vn0;
    t->as.enom.variants[0].payload_count = 1;
    t->as.enom.variants[0].payload_types =
        (Type **)malloc_safe(sizeof(Type *));
    t->as.enom.variants[0].payload_types[0] = type_int();

    /* Variant 1: Err(string) */
    char *vn1 = (char *)malloc_safe(4); memcpy(vn1, "Err", 4);
    t->as.enom.variants[1].name = vn1;
    t->as.enom.variants[1].payload_count = 1;
    t->as.enom.variants[1].payload_types =
        (Type **)malloc_safe(sizeof(Type *));
    t->as.enom.variants[1].payload_types[0] = type_string();
    t->as.enom.has_drop = true;  /* contains string */

    /* Equality by mangled name */
    Type *t2 = type_enum("Result(int,string)", 2);
    ASSERT_TRUE(type_equals(t, t2));

    Type *t3 = type_enum("Option(int)", 2);
    ASSERT_TRUE(!type_equals(t, t3));

    /* type_name returns the mangled name */
    ASSERT_STR_EQ(type_name(t), "Result(int,string)");

    /* Clone preserves structure */
    Type *clone = type_clone(t);
    ASSERT_TRUE(type_equals(t, clone));
    ASSERT_TRUE(t != clone);
    ASSERT_EQ(clone->as.enom.has_drop, true);

    type_free(t);
    type_free(t2);
    type_free(t3);
    type_free(clone);

    printf(" ok\n");
}

/* ---- Symbol table tests ---- */

static void test_scope_basic(void) {
    printf("  test_scope_basic...");
    Scope *s = scope_new(NULL);
    ASSERT_EQ(s->depth, 0);

    Symbol *sym = scope_define(s, "x", type_int());
    ASSERT_NOT_NULL(sym);
    ASSERT_STR_EQ(sym->name, "x");
    ASSERT_TRUE(type_equals(sym->type, type_int()));

    Symbol *found = scope_resolve(s, "x");
    ASSERT_TRUE(found == sym);

    ASSERT_NULL(scope_resolve(s, "y"));

    scope_free(s);
    printf(" ok\n");
}

static void test_scope_nesting(void) {
    printf("  test_scope_nesting...");
    Scope *outer = scope_new(NULL);
    scope_define(outer, "x", type_int());
    scope_define(outer, "y", type_f64());

    Scope *inner = scope_new(outer);
    ASSERT_EQ(inner->depth, 1);

    /* Inner can see outer */
    ASSERT_NOT_NULL(scope_resolve(inner, "x"));
    ASSERT_NOT_NULL(scope_resolve(inner, "y"));

    /* Shadowing */
    scope_define(inner, "x", type_bool());
    Symbol *sx = scope_resolve(inner, "x");
    ASSERT_TRUE(type_equals(sx->type, type_bool()));

    /* Outer x unchanged */
    Symbol *ox = scope_resolve(outer, "x");
    ASSERT_TRUE(type_equals(ox->type, type_int()));

    scope_free(inner);
    scope_free(outer);
    printf(" ok\n");
}

static void test_scope_duplicate_define(void) {
    printf("  test_scope_duplicate_define...");
    Scope *s = scope_new(NULL);
    ASSERT_NOT_NULL(scope_define(s, "x", type_int()));
    ASSERT_NULL(scope_define(s, "x", type_f64())); /* duplicate */
    scope_free(s);
    printf(" ok\n");
}

static void test_scope_resolve_local(void) {
    printf("  test_scope_resolve_local...");
    Scope *outer = scope_new(NULL);
    scope_define(outer, "x", type_int());

    Scope *inner = scope_new(outer);
    ASSERT_NULL(scope_resolve_local(inner, "x")); /* not in inner */
    scope_define(inner, "y", type_f64());
    ASSERT_NOT_NULL(scope_resolve_local(inner, "y"));

    scope_free(inner);
    scope_free(outer);
    printf(" ok\n");
}

/* ---- Type checker tests (using parser) ---- */

/* Helper: parse + check, return true if no errors */
static bool check_source(const char *source) {
    AstNode *ast = parse(source, "<test>");
    if (ast == NULL) return false;
    bool ok = checker_check(ast, "<test>", NULL, NULL);
    ast_free(ast);
    return ok;
}

static void test_check_var_decl_ok(void) {
    printf("  test_check_var_decl_ok...");
    ASSERT_TRUE(check_source(
        "fn main() -> int {\n"
        "    int x = 42\n"
        "    f64 y = 3.14\n"
        "    bool b = true\n"
        "    string s = \"hello\"\n"
        "    return 0\n"
        "}\n"
    ));
    printf(" ok\n");
}

static void test_check_var_decl_type_mismatch(void) {
    printf("  test_check_var_decl_type_mismatch...");
    ASSERT_FALSE(check_source(
        "fn main() -> int {\n"
        "    int x = 3.14\n"
        "    return 0\n"
        "}\n"
    ));
    printf(" ok\n");
}

static void test_check_undefined_variable(void) {
    printf("  test_check_undefined_variable...");
    ASSERT_FALSE(check_source(
        "fn main() -> int {\n"
        "    int x = y\n"
        "    return 0\n"
        "}\n"
    ));
    printf(" ok\n");
}

static void test_check_arithmetic_ok(void) {
    printf("  test_check_arithmetic_ok...");
    ASSERT_TRUE(check_source(
        "fn main() -> int {\n"
        "    int a = 1\n"
        "    int b = 2\n"
        "    int c = a + b\n"
        "    int d = a * b - c\n"
        "    return 0\n"
        "}\n"
    ));
    printf(" ok\n");
}

static void test_check_arithmetic_type_mismatch(void) {
    printf("  test_check_arithmetic_type_mismatch...");
    ASSERT_FALSE(check_source(
        "fn main() -> int {\n"
        "    int a = 1\n"
        "    f64 b = 2.0\n"
        "    int c = a + b\n"
        "    return 0\n"
        "}\n"
    ));
    printf(" ok\n");
}

static void test_check_bool_in_arithmetic(void) {
    printf("  test_check_bool_in_arithmetic...");
    ASSERT_FALSE(check_source(
        "fn main() -> int {\n"
        "    bool a = true\n"
        "    int b = a + 1\n"
        "    return 0\n"
        "}\n"
    ));
    printf(" ok\n");
}

static void test_check_comparison(void) {
    printf("  test_check_comparison...");
    ASSERT_TRUE(check_source(
        "fn main() -> int {\n"
        "    int a = 1\n"
        "    int b = 2\n"
        "    bool c = a < b\n"
        "    bool d = a == b\n"
        "    bool e = a >= b\n"
        "    return 0\n"
        "}\n"
    ));
    printf(" ok\n");
}

static void test_check_logical(void) {
    printf("  test_check_logical...");
    ASSERT_TRUE(check_source(
        "fn main() -> int {\n"
        "    bool a = true\n"
        "    bool b = false\n"
        "    bool c = a && b\n"
        "    bool d = a || !b\n"
        "    return 0\n"
        "}\n"
    ));
    printf(" ok\n");
}

static void test_check_logical_non_bool(void) {
    printf("  test_check_logical_non_bool...");
    ASSERT_FALSE(check_source(
        "fn main() -> int {\n"
        "    int a = 1\n"
        "    bool b = a && true\n"
        "    return 0\n"
        "}\n"
    ));
    printf(" ok\n");
}

static void test_check_function_call_ok(void) {
    printf("  test_check_function_call_ok...");
    ASSERT_TRUE(check_source(
        "fn add(int a, int b) -> int {\n"
        "    return a + b\n"
        "}\n"
        "fn main() -> int {\n"
        "    int r = add(1, 2)\n"
        "    return r\n"
        "}\n"
    ));
    printf(" ok\n");
}

static void test_check_function_call_wrong_arg_count(void) {
    printf("  test_check_function_call_wrong_arg_count...");
    ASSERT_FALSE(check_source(
        "fn add(int a, int b) -> int {\n"
        "    return a + b\n"
        "}\n"
        "fn main() -> int {\n"
        "    int r = add(1)\n"
        "    return r\n"
        "}\n"
    ));
    printf(" ok\n");
}

static void test_check_function_call_wrong_arg_type(void) {
    printf("  test_check_function_call_wrong_arg_type...");
    ASSERT_FALSE(check_source(
        "fn add(int a, int b) -> int {\n"
        "    return a + b\n"
        "}\n"
        "fn main() -> int {\n"
        "    int r = add(1, 2.5)\n"
        "    return r\n"
        "}\n"
    ));
    printf(" ok\n");
}

static void test_check_return_type_mismatch(void) {
    printf("  test_check_return_type_mismatch...");
    ASSERT_FALSE(check_source(
        "fn foo() -> int {\n"
        "    return true\n"
        "}\n"
    ));
    printf(" ok\n");
}

static void test_check_if_condition_must_be_bool(void) {
    printf("  test_check_if_condition_must_be_bool...");
    ASSERT_FALSE(check_source(
        "fn main() -> int {\n"
        "    if (42) { return 0 }\n"
        "    return 1\n"
        "}\n"
    ));
    printf(" ok\n");
}

static void test_check_while_condition_must_be_bool(void) {
    printf("  test_check_while_condition_must_be_bool...");
    ASSERT_FALSE(check_source(
        "fn main() -> int {\n"
        "    while (1) { return 0 }\n"
        "    return 1\n"
        "}\n"
    ));
    printf(" ok\n");
}

static void test_check_struct_field_access(void) {
    printf("  test_check_struct_field_access...");
    ASSERT_TRUE(check_source(
        "struct Point { f64 x; f64 y; }\n"
        "fn main() -> int {\n"
        "    Point p\n"
        "    f64 a = p.x\n"
        "    f64 b = p.y\n"
        "    return 0\n"
        "}\n"
    ));
    printf(" ok\n");
}

static void test_check_struct_field_nonexistent(void) {
    printf("  test_check_struct_field_nonexistent...");
    ASSERT_FALSE(check_source(
        "struct Point { f64 x; f64 y; }\n"
        "fn main() -> int {\n"
        "    Point p\n"
        "    f64 a = p.z\n"
        "    return 0\n"
        "}\n"
    ));
    printf(" ok\n");
}

static void test_check_match_arms_consistent(void) {
    printf("  test_check_match_arms_consistent...");
    ASSERT_TRUE(check_source(
        "fn classify(int n) -> int {\n"
        "    match n {\n"
        "        0 => 0,\n"
        "        1 => 1,\n"
        "        _ => n + 1,\n"
        "    }\n"
        "}\n"
    ));
    printf(" ok\n");
}

static void test_check_scope_shadowing(void) {
    printf("  test_check_scope_shadowing...");
    ASSERT_TRUE(check_source(
        "fn main() -> int {\n"
        "    int x = 1\n"
        "    {\n"
        "        f64 x = 2.0\n"
        "        f64 y = x + 1.0\n"
        "    }\n"
        "    int z = x + 1\n"
        "    return 0\n"
        "}\n"
    ));
    printf(" ok\n");
}

static void test_check_forward_function_ref(void) {
    printf("  test_check_forward_function_ref...");
    /* Functions can be called before they are defined (two-pass) */
    ASSERT_TRUE(check_source(
        "fn main() -> int {\n"
        "    int r = helper(10)\n"
        "    return r\n"
        "}\n"
        "fn helper(int x) -> int {\n"
        "    return x * 2\n"
        "}\n"
    ));
    printf(" ok\n");
}

static void test_check_string_concat(void) {
    printf("  test_check_string_concat...");
    ASSERT_TRUE(check_source(
        "fn main() -> int {\n"
        "    string a = \"hello\"\n"
        "    string b = \" world\"\n"
        "    string c = a + b\n"
        "    return 0\n"
        "}\n"
    ));
    printf(" ok\n");
}

static void test_check_unary_ops(void) {
    printf("  test_check_unary_ops...");
    ASSERT_TRUE(check_source(
        "fn main() -> int {\n"
        "    int x = 42\n"
        "    int y = -x\n"
        "    bool a = true\n"
        "    bool b = !a\n"
        "    return 0\n"
        "}\n"
    ));
    printf(" ok\n");
}

static void test_check_unary_neg_bool(void) {
    printf("  test_check_unary_neg_bool...");
    ASSERT_FALSE(check_source(
        "fn main() -> int {\n"
        "    bool a = true\n"
        "    int b = -a\n"
        "    return 0\n"
        "}\n"
    ));
    printf(" ok\n");
}

static void test_check_pointer_deref(void) {
    printf("  test_check_pointer_deref...");
    ASSERT_TRUE(check_source(
        "fn main() -> int {\n"
        "    int x = 42\n"
        "    *int p = &x\n"
        "    int y = *p\n"
        "    return 0\n"
        "}\n"
    ));
    printf(" ok\n");
}

static void test_check_deref_non_pointer(void) {
    printf("  test_check_deref_non_pointer...");
    ASSERT_FALSE(check_source(
        "fn main() -> int {\n"
        "    int x = 42\n"
        "    int y = *x\n"
        "    return 0\n"
        "}\n"
    ));
    printf(" ok\n");
}

static void test_check_impl_method(void) {
    printf("  test_check_impl_method...");
    ASSERT_TRUE(check_source(
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
    ));
    printf(" ok\n");
}

static void test_check_samples_hello(void) {
    printf("  test_check_samples_hello...");
    ASSERT_TRUE(check_source(
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
    ));
    printf(" ok\n");
}

static void test_check_samples_factorial(void) {
    printf("  test_check_samples_factorial...");
    ASSERT_TRUE(check_source(
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
    ));
    printf(" ok\n");
}

static void test_check_samples_struct(void) {
    printf("  test_check_samples_struct...");
    ASSERT_TRUE(check_source(
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
    ));
    printf(" ok\n");
}

static void test_check_samples_match(void) {
    printf("  test_check_samples_match...");
    ASSERT_TRUE(check_source(
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
    ));
    printf(" ok\n");
}

static void test_check_assign_type_mismatch(void) {
    printf("  test_check_assign_type_mismatch...");
    ASSERT_FALSE(check_source(
        "fn main() -> int {\n"
        "    int x = 1\n"
        "    x = 3.14\n"
        "    return 0\n"
        "}\n"
    ));
    printf(" ok\n");
}

static void test_check_duplicate_variable(void) {
    printf("  test_check_duplicate_variable...");
    ASSERT_FALSE(check_source(
        "fn main() -> int {\n"
        "    int x = 1\n"
        "    int x = 2\n"
        "    return 0\n"
        "}\n"
    ));
    printf(" ok\n");
}

/* ---- Array type checker tests ---- */

static void test_check_array_ok(void) {
    printf("  test_check_array_ok...");
    ASSERT_TRUE(check_source(
        "fn main() -> int {\n"
        "    array(int, 3) nums = [1, 2, 3]\n"
        "    int x = nums[0]\n"
        "    int len = nums.length\n"
        "    return 0\n"
        "}\n"
    ));
    printf(" ok\n");
}

static void test_check_array_size_mismatch(void) {
    printf("  test_check_array_size_mismatch...");
    /* Declared array(int, 3) but literal has 2 elements */
    ASSERT_FALSE(check_source(
        "fn main() -> int {\n"
        "    array(int, 3) x = [1, 2]\n"
        "    return 0\n"
        "}\n"
    ));
    printf(" ok\n");
}

static void test_check_array_element_type_mismatch(void) {
    printf("  test_check_array_element_type_mismatch...");
    /* Array elements must all be the same type */
    ASSERT_FALSE(check_source(
        "fn main() -> int {\n"
        "    array(int, 3) x = [1, 2.0, 3]\n"
        "    return 0\n"
        "}\n"
    ));
    printf(" ok\n");
}

static void test_check_array_index_non_integer(void) {
    printf("  test_check_array_index_non_integer...");
    ASSERT_FALSE(check_source(
        "fn main() -> int {\n"
        "    array(int, 3) x = [1, 2, 3]\n"
        "    int v = x[true]\n"
        "    return 0\n"
        "}\n"
    ));
    printf(" ok\n");
}

static void test_check_array_no_field(void) {
    printf("  test_check_array_no_field...");
    /* Arrays only support .length, not arbitrary fields */
    ASSERT_FALSE(check_source(
        "fn main() -> int {\n"
        "    array(int, 3) x = [1, 2, 3]\n"
        "    int v = x.size\n"
        "    return 0\n"
        "}\n"
    ));
    printf(" ok\n");
}

static void test_check_array_foreach_ok(void) {
    printf("  test_check_array_foreach_ok...");
    ASSERT_TRUE(check_source(
        "fn main() -> int {\n"
        "    array(int, 3) arr = [10, 20, 30]\n"
        "    int sum = 0\n"
        "    for x in arr {\n"
        "        sum = sum + x\n"
        "    }\n"
        "    return sum\n"
        "}\n"
    ));
    printf(" ok\n");
}

static void test_check_array_param_ok(void) {
    printf("  test_check_array_param_ok...");
    ASSERT_TRUE(check_source(
        "fn sum3(array(int, 3) a) -> int {\n"
        "    return a[0] + a[1] + a[2]\n"
        "}\n"
        "fn main() -> int {\n"
        "    array(int, 3) nums = [1, 2, 3]\n"
        "    int r = sum3(nums)\n"
        "    return r\n"
        "}\n"
    ));
    printf(" ok\n");
}

static void test_check_array_param_size_mismatch(void) {
    printf("  test_check_array_param_size_mismatch...");
    /* Passing array(int, 2) to a function expecting array(int, 3) */
    ASSERT_FALSE(check_source(
        "fn sum3(array(int, 3) a) -> int {\n"
        "    return a[0] + a[1] + a[2]\n"
        "}\n"
        "fn main() -> int {\n"
        "    array(int, 2) nums = [1, 2]\n"
        "    int r = sum3(nums)\n"
        "    return r\n"
        "}\n"
    ));
    printf(" ok\n");
}

/* ---- Global variable type checker tests ---- */

static void test_check_global_var_ok(void) {
    printf("  test_check_global_var_ok...");
    ASSERT_TRUE(check_source(
        "int MAGIC = 42\n"
        "f64 PI = 3.14\n"
        "fn main() -> int {\n"
        "    return MAGIC\n"
        "}\n"
    ));
    printf(" ok\n");
}

static void test_check_global_var_type_mismatch(void) {
    printf("  test_check_global_var_type_mismatch...");
    ASSERT_FALSE(check_source(
        "int X = 3.14\n"
        "fn main() -> int { return 0 }\n"
    ));
    printf(" ok\n");
}

static void test_check_global_var_used_in_function(void) {
    printf("  test_check_global_var_used_in_function...");
    ASSERT_TRUE(check_source(
        "int counter = 0\n"
        "fn increment() {\n"
        "    counter = counter + 1\n"
        "}\n"
        "fn main() -> int {\n"
        "    increment()\n"
        "    return counter\n"
        "}\n"
    ));
    printf(" ok\n");
}

static void test_check_global_array_ok(void) {
    printf("  test_check_global_array_ok...");
    ASSERT_TRUE(check_source(
        "array(int, 3) data = [10, 20, 30]\n"
        "fn main() -> int {\n"
        "    return data[0]\n"
        "}\n"
    ));
    printf(" ok\n");
}

/* ---- String LsString type checker tests ---- */

static void test_check_string_length_ok(void) {
    printf("  test_check_string_length_ok...");
    ASSERT_TRUE(check_source(
        "fn main() -> int {\n"
        "    string s = \"hello\"\n"
        "    int len = s.length\n"
        "    return len\n"
        "}\n"
    ));
    printf(" ok\n");
}

static void test_check_string_no_field(void) {
    printf("  test_check_string_no_field...");
    /* Accessing a non-existent field on string should fail */
    ASSERT_FALSE(check_source(
        "fn main() -> int {\n"
        "    string s = \"hello\"\n"
        "    int x = s.foobar\n"
        "    return 0\n"
        "}\n"
    ));
    printf(" ok\n");
}

static void test_check_string_compare_ok(void) {
    printf("  test_check_string_compare_ok...");
    ASSERT_TRUE(check_source(
        "fn main() -> int {\n"
        "    string a = \"abc\"\n"
        "    string b = \"def\"\n"
        "    bool eq = (a == b)\n"
        "    bool ne = (a != b)\n"
        "    return 0\n"
        "}\n"
    ));
    printf(" ok\n");
}

static void test_check_string_concat_to_var(void) {
    printf("  test_check_string_concat_to_var...");
    ASSERT_TRUE(check_source(
        "fn main() -> int {\n"
        "    string a = \"hello\"\n"
        "    string b = \" world\"\n"
        "    string c = a + b\n"
        "    int len = c.length\n"
        "    return 0\n"
        "}\n"
    ));
    printf(" ok\n");
}

/* ---- String method type checker tests ---- */

static void test_check_string_empty_ok(void) {
    printf("  test_check_string_empty_ok...");
    ASSERT_TRUE(check_source(
        "fn main() -> int {\n"
        "    string s = \"hello\"\n"
        "    bool b = s.empty()\n"
        "    return 0\n"
        "}\n"
    ));
    printf(" ok\n");
}

static void test_check_string_at_ok(void) {
    printf("  test_check_string_at_ok...");
    ASSERT_TRUE(check_source(
        "fn main() -> int {\n"
        "    string s = \"hello\"\n"
        "    int ch = s.at(0)\n"
        "    return 0\n"
        "}\n"
    ));
    printf(" ok\n");
}

static void test_check_string_find_ok(void) {
    printf("  test_check_string_find_ok...");
    ASSERT_TRUE(check_source(
        "fn main() -> int {\n"
        "    string s = \"hello world\"\n"
        "    int pos = s.find(\"world\")\n"
        "    return 0\n"
        "}\n"
    ));
    printf(" ok\n");
}

static void test_check_string_contains_ok(void) {
    printf("  test_check_string_contains_ok...");
    ASSERT_TRUE(check_source(
        "fn main() -> int {\n"
        "    string s = \"hello world\"\n"
        "    bool has = s.contains(\"llo\")\n"
        "    return 0\n"
        "}\n"
    ));
    printf(" ok\n");
}

static void test_check_string_starts_ends_with_ok(void) {
    printf("  test_check_string_starts_ends_with_ok...");
    ASSERT_TRUE(check_source(
        "fn main() -> int {\n"
        "    string s = \"hello world\"\n"
        "    bool a = s.starts_with(\"hello\")\n"
        "    bool b = s.ends_with(\"world\")\n"
        "    return 0\n"
        "}\n"
    ));
    printf(" ok\n");
}

static void test_check_string_compare_method_ok(void) {
    printf("  test_check_string_compare_method_ok...");
    ASSERT_TRUE(check_source(
        "fn main() -> int {\n"
        "    string a = \"abc\"\n"
        "    string b = \"def\"\n"
        "    int c = a.compare(b)\n"
        "    return 0\n"
        "}\n"
    ));
    printf(" ok\n");
}

static void test_check_string_method_wrong_args(void) {
    printf("  test_check_string_method_wrong_args...");
    /* empty() with args should fail */
    ASSERT_TRUE(!check_source(
        "fn main() -> int {\n"
        "    string s = \"hello\"\n"
        "    bool b = s.empty(1)\n"
        "    return 0\n"
        "}\n"
    ));
    /* at() with string arg should fail */
    ASSERT_TRUE(!check_source(
        "fn main() -> int {\n"
        "    string s = \"hello\"\n"
        "    int ch = s.at(\"x\")\n"
        "    return 0\n"
        "}\n"
    ));
    /* find() with int arg should fail */
    ASSERT_TRUE(!check_source(
        "fn main() -> int {\n"
        "    string s = \"hello\"\n"
        "    int pos = s.find(42)\n"
        "    return 0\n"
        "}\n"
    ));
    printf(" ok\n");
}

static void test_check_string_unknown_method(void) {
    printf("  test_check_string_unknown_method...");
    ASSERT_TRUE(!check_source(
        "fn main() -> int {\n"
        "    string s = \"hello\"\n"
        "    int x = s.foobar()\n"
        "    return 0\n"
        "}\n"
    ));
    printf(" ok\n");
}

/* ---- String Batch 2 type checker tests ---- */

static void test_check_string_upper_ok(void) {
    printf("  test_check_string_upper_ok...");
    ASSERT_TRUE(check_source(
        "fn main() -> int {\n"
        "    string s = \"hello\"\n"
        "    string u = s.upper()\n"
        "    return 0\n"
        "}\n"
    ));
    printf(" ok\n");
}

static void test_check_string_lower_ok(void) {
    printf("  test_check_string_lower_ok...");
    ASSERT_TRUE(check_source(
        "fn main() -> int {\n"
        "    string s = \"HELLO\"\n"
        "    string l = s.lower()\n"
        "    return 0\n"
        "}\n"
    ));
    printf(" ok\n");
}

static void test_check_string_substr_ok(void) {
    printf("  test_check_string_substr_ok...");
    ASSERT_TRUE(check_source(
        "fn main() -> int {\n"
        "    string s = \"hello world\"\n"
        "    string sub = s.substr(0, 5)\n"
        "    return 0\n"
        "}\n"
    ));
    printf(" ok\n");
}

static void test_check_string_trim_ok(void) {
    printf("  test_check_string_trim_ok...");
    ASSERT_TRUE(check_source(
        "fn main() -> int {\n"
        "    string s = \"  hello  \"\n"
        "    string t = s.trim()\n"
        "    return 0\n"
        "}\n"
    ));
    printf(" ok\n");
}

static void test_check_string_replace_ok(void) {
    printf("  test_check_string_replace_ok...");
    ASSERT_TRUE(check_source(
        "fn main() -> int {\n"
        "    string s = \"hello world\"\n"
        "    string r = s.replace(\"world\", \"LS\")\n"
        "    return 0\n"
        "}\n"
    ));
    printf(" ok\n");
}

static void test_check_string_batch2_wrong_args(void) {
    printf("  test_check_string_batch2_wrong_args...");
    /* upper() with args should fail */
    ASSERT_TRUE(!check_source(
        "fn main() -> int {\n"
        "    string s = \"hello\"\n"
        "    string u = s.upper(1)\n"
        "    return 0\n"
        "}\n"
    ));
    /* substr() with zero args should fail */
    ASSERT_TRUE(!check_source(
        "fn main() -> int {\n"
        "    string s = \"hello\"\n"
        "    string sub = s.substr()\n"
        "    return 0\n"
        "}\n"
    ));
    /* substr() with string arg should fail */
    ASSERT_TRUE(!check_source(
        "fn main() -> int {\n"
        "    string s = \"hello\"\n"
        "    string sub = s.substr(\"a\", 3)\n"
        "    return 0\n"
        "}\n"
    ));
    /* replace() with int args should fail */
    ASSERT_TRUE(!check_source(
        "fn main() -> int {\n"
        "    string s = \"hello\"\n"
        "    string r = s.replace(42, \"x\")\n"
        "    return 0\n"
        "}\n"
    ));
    printf(" ok\n");
}

/* ---- String Batch 3 type checker tests ---- */

static void test_check_string_rfind_ok(void) {
    printf("  test_check_string_rfind_ok...");
    ASSERT_TRUE(check_source(
        "fn main() -> int {\n"
        "    string s = \"hello world hello\"\n"
        "    int pos = s.rfind(\"hello\")\n"
        "    return pos\n"
        "}\n"
    ));
    printf(" ok\n");
}

static void test_check_string_count_ok(void) {
    printf("  test_check_string_count_ok...");
    ASSERT_TRUE(check_source(
        "fn main() -> int {\n"
        "    string s = \"aababab\"\n"
        "    int n = s.count(\"ab\")\n"
        "    return n\n"
        "}\n"
    ));
    printf(" ok\n");
}

static void test_check_string_substr_one_arg_ok(void) {
    printf("  test_check_string_substr_one_arg_ok...");
    ASSERT_TRUE(check_source(
        "fn main() -> int {\n"
        "    string s = \"hello world\"\n"
        "    string tail = s.substr(6)\n"
        "    return 0\n"
        "}\n"
    ));
    printf(" ok\n");
}

static void test_check_string_split_ok(void) {
    printf("  test_check_string_split_ok...");
    ASSERT_TRUE(check_source(
        "fn main() -> int {\n"
        "    string s = \"a,b,c\"\n"
        "    vec(string) parts = s.split(\",\")\n"
        "    return 0\n"
        "}\n"
    ));
    printf(" ok\n");
}

static void test_check_string_join_ok(void) {
    printf("  test_check_string_join_ok...");
    ASSERT_TRUE(check_source(
        "fn main() -> int {\n"
        "    vec(string) words\n"
        "    words.push(\"hello\")\n"
        "    words.push(\"world\")\n"
        "    string result = \", \".join(words)\n"
        "    return 0\n"
        "}\n"
    ));
    printf(" ok\n");
}

static void test_check_string_batch3_wrong_args(void) {
    printf("  test_check_string_batch3_wrong_args...");
    /* rfind with no args should fail */
    ASSERT_TRUE(!check_source(
        "fn main() -> int {\n"
        "    string s = \"hello\"\n"
        "    int p = s.rfind()\n"
        "    return 0\n"
        "}\n"
    ));
    /* count with non-string arg should fail */
    ASSERT_TRUE(!check_source(
        "fn main() -> int {\n"
        "    string s = \"hello\"\n"
        "    int n = s.count(42)\n"
        "    return 0\n"
        "}\n"
    ));
    /* split with int arg should fail */
    ASSERT_TRUE(!check_source(
        "fn main() -> int {\n"
        "    string s = \"hello\"\n"
        "    vec(string) v = s.split(1)\n"
        "    return 0\n"
        "}\n"
    ));
    /* join with vec(int) should fail */
    ASSERT_TRUE(!check_source(
        "fn main() -> int {\n"
        "    vec(int) nums\n"
        "    nums.push(1)\n"
        "    string r = \",\".join(nums)\n"
        "    return 0\n"
        "}\n"
    ));
    printf(" ok\n");
}

/* ---- String conversion builtin type checker tests ---- */

static void test_check_to_string_ok(void) {
    printf("  test_check_to_string_ok...");
    ASSERT_TRUE(check_source(
        "fn main() -> int {\n"
        "    string s = to_string(42)\n"
        "    return 0\n"
        "}\n"
    ));
    ASSERT_TRUE(check_source(
        "fn main() -> int {\n"
        "    string s = to_string(3.14)\n"
        "    return 0\n"
        "}\n"
    ));
    ASSERT_TRUE(check_source(
        "fn main() -> int {\n"
        "    string s = to_string(true)\n"
        "    return 0\n"
        "}\n"
    ));
    printf(" ok\n");
}

static void test_check_to_string_wrong_type(void) {
    printf("  test_check_to_string_wrong_type...");
    ASSERT_TRUE(!check_source(
        "fn main() -> int {\n"
        "    string s = to_string(\"hello\")\n"
        "    return 0\n"
        "}\n"
    ));
    printf(" ok\n");
}

static void test_check_to_string_wrong_arg_count(void) {
    printf("  test_check_to_string_wrong_arg_count...");
    ASSERT_TRUE(!check_source(
        "fn main() -> int {\n"
        "    string s = to_string()\n"
        "    return 0\n"
        "}\n"
    ));
    ASSERT_TRUE(!check_source(
        "fn main() -> int {\n"
        "    string s = to_string(1, 2)\n"
        "    return 0\n"
        "}\n"
    ));
    printf(" ok\n");
}

static void test_check_from_int_ok(void) {
    printf("  test_check_from_int_ok...");
    ASSERT_TRUE(check_source(
        "fn main() -> int {\n"
        "    int n = from_int(\"123\")\n"
        "    return 0\n"
        "}\n"
    ));
    ASSERT_TRUE(check_source(
        "fn main() -> int {\n"
        "    int n = from_int(\"-456\")\n"
        "    return 0\n"
        "}\n"
    ));
    printf(" ok\n");
}

static void test_check_from_int_wrong_type(void) {
    printf("  test_check_from_int_wrong_type...");
    ASSERT_TRUE(!check_source(
        "fn main() -> int {\n"
        "    int n = from_int(42)\n"
        "    return 0\n"
        "}\n"
    ));
    printf(" ok\n");
}

static void test_check_from_float_ok(void) {
    printf("  test_check_from_float_ok...");
    ASSERT_TRUE(check_source(
        "fn main() -> int {\n"
        "    f64 f = from_float(\"3.14\")\n"
        "    return 0\n"
        "}\n"
    ));
    printf(" ok\n");
}

static void test_check_from_float_wrong_type(void) {
    printf("  test_check_from_float_wrong_type...");
    ASSERT_TRUE(!check_source(
        "fn main() -> int {\n"
        "    f64 f = from_float(42)\n"
        "    return 0\n"
        "}\n"
    ));
    printf(" ok\n");
}

/* ---- Struct method (implicit self + static) type checker tests ---- */

static void test_check_instance_method_ok(void) {
    printf("  test_check_instance_method_ok...");
    ASSERT_TRUE(check_source(
        "struct Point { f64 x; f64 y; }\n"
        "impl Point {\n"
        "    fn get_x() -> f64 { return self.x }\n"
        "    fn distance(Point other) -> f64 {\n"
        "        f64 dx = self.x - other.x\n"
        "        return dx\n"
        "    }\n"
        "}\n"
        "fn main() -> int {\n"
        "    Point p\n"
        "    p.x = 1.0\n"
        "    p.y = 2.0\n"
        "    f64 x = p.get_x()\n"
        "    Point p2\n"
        "    p2.x = 3.0\n"
        "    p2.y = 4.0\n"
        "    f64 d = p.distance(p2)\n"
        "    return 0\n"
        "}\n"
    ));
    printf(" ok\n");
}

static void test_check_static_method_ok(void) {
    printf("  test_check_static_method_ok...");
    ASSERT_TRUE(check_source(
        "struct Point { f64 x; f64 y; }\n"
        "impl Point {\n"
        "    static fn origin() -> Point {\n"
        "        Point p\n"
        "        p.x = 0.0\n"
        "        p.y = 0.0\n"
        "        return p\n"
        "    }\n"
        "}\n"
        "fn main() -> int {\n"
        "    Point p = Point.origin()\n"
        "    return 0\n"
        "}\n"
    ));
    printf(" ok\n");
}

static void test_check_instance_method_wrong_args(void) {
    printf("  test_check_instance_method_wrong_args...");
    /* Too many args */
    ASSERT_TRUE(!check_source(
        "struct Point { f64 x; f64 y; }\n"
        "impl Point {\n"
        "    fn get_x() -> f64 { return self.x }\n"
        "}\n"
        "fn main() -> int {\n"
        "    Point p\n"
        "    p.x = 1.0\n"
        "    f64 x = p.get_x(1.0)\n"
        "    return 0\n"
        "}\n"
    ));
    printf(" ok\n");
}

static void test_check_instance_method_via_typename_fails(void) {
    printf("  test_check_instance_method_via_typename_fails...");
    /* Cannot call instance method via type name */
    ASSERT_TRUE(!check_source(
        "struct Point { f64 x; f64 y; }\n"
        "impl Point {\n"
        "    fn get_x() -> f64 { return self.x }\n"
        "}\n"
        "fn main() -> int {\n"
        "    f64 x = Point.get_x()\n"
        "    return 0\n"
        "}\n"
    ));
    printf(" ok\n");
}

/* ---- new expression type checker tests ---- */

static void test_check_new_ok(void) {
    printf("  test_check_new_ok...");
    ASSERT_TRUE(check_source(
        "struct Point { f64 x; f64 y; }\n"
        "fn main() -> int {\n"
        "    *Point p = new Point { x: 1.0, y: 2.0 }\n"
        "    return 0\n"
        "}\n"
    ));
    printf(" ok\n");
}

static void test_check_new_partial_fields_ok(void) {
    printf("  test_check_new_partial_fields_ok...");
    ASSERT_TRUE(check_source(
        "struct Point { f64 x; f64 y; }\n"
        "fn main() -> int {\n"
        "    *Point p = new Point { x: 1.0 }\n"
        "    return 0\n"
        "}\n"
    ));
    printf(" ok\n");
}

static void test_check_new_unknown_struct(void) {
    printf("  test_check_new_unknown_struct...");
    ASSERT_FALSE(check_source(
        "fn main() -> int {\n"
        "    *Foo p = new Foo\n"
        "    return 0\n"
        "}\n"
    ));
    printf(" ok\n");
}

static void test_check_new_unknown_field(void) {
    printf("  test_check_new_unknown_field...");
    ASSERT_FALSE(check_source(
        "struct Point { f64 x; f64 y; }\n"
        "fn main() -> int {\n"
        "    *Point p = new Point { z: 1.0 }\n"
        "    return 0\n"
        "}\n"
    ));
    printf(" ok\n");
}

static void test_check_new_field_type_mismatch(void) {
    printf("  test_check_new_field_type_mismatch...");
    ASSERT_FALSE(check_source(
        "struct Point { f64 x; f64 y; }\n"
        "fn main() -> int {\n"
        "    *Point p = new Point { x: \"hello\" }\n"
        "    return 0\n"
        "}\n"
    ));
    printf(" ok\n");
}

static void test_check_new_duplicate_field(void) {
    printf("  test_check_new_duplicate_field...");
    ASSERT_FALSE(check_source(
        "struct Point { f64 x; f64 y; }\n"
        "fn main() -> int {\n"
        "    *Point p = new Point { x: 1.0, x: 2.0 }\n"
        "    return 0\n"
        "}\n"
    ));
    printf(" ok\n");
}

/* ---- Move Semantics Tests ---- */

static void test_check_move_struct_assignment(void) {
    /* Clone semantics: Foo b = a deep-copies a, so a remains valid afterwards */
    printf("  test_check_move_struct_assignment...");
    ASSERT_TRUE(check_source(
        "struct Foo { int x; }\n"
        "fn main() -> int {\n"
        "    Foo a = Foo{}\n"
        "    Foo b = a\n"
        "    print(a.x)\n"
        "    return 0\n"
        "}\n"
    ));
    printf(" ok\n");
}

static void test_check_move_struct_call(void) {
    /* Clone semantics: take(a) deep-copies a, so a remains valid afterwards */
    printf("  test_check_move_struct_call...");
    ASSERT_TRUE(check_source(
        "struct Foo { int x; }\n"
        "fn take(Foo f) { }\n"
        "fn main() -> int {\n"
        "    Foo a = Foo{}\n"
        "    take(a)\n"
        "    print(a.x)\n"
        "    return 0\n"
        "}\n"
    ));
    printf(" ok\n");
}

static void test_check_move_duplicate_in_call(void) {
    /* Clone semantics: take2(x, x) clones x twice, both copies are independent */
    printf("  test_check_move_duplicate_in_call...");
    ASSERT_TRUE(check_source(
        "struct Foo { int x; }\n"
        "fn take2(Foo a, Foo b) { }\n"
        "fn main() -> int {\n"
        "    Foo x = Foo{}\n"
        "    take2(x, x)\n"
        "    return 0\n"
        "}\n"
    ));
    printf(" ok\n");
}

static void test_check_move_temporary_literal(void) {
    printf("  test_check_move_temporary_literal...");
    ASSERT_TRUE(check_source(
        "struct Foo { int x; }\n"
        "fn take(Foo f) { }\n"
        "fn main() -> int {\n"
        "    take(Foo{ x: 1 })\n"
        "    return 0\n"
        "}\n"
    ));
    printf(" ok\n");
}

static void test_check_move_struct_field_allowed(void) {
    /* Clone semantics: take(o.foo) clones the field value, o.foo remains valid */
    printf("  test_check_move_struct_field_allowed...");
    ASSERT_TRUE(check_source(
        "struct Foo { int x; }\n"
        "struct Outer { Foo foo; }\n"
        "fn take(Foo f) { }\n"
        "fn main() -> int {\n"
        "    Outer o\n"
        "    take(o.foo)\n"
        "    return 0\n"
        "}\n"
    ));
    printf(" ok\n");
}

static void test_check_clone_multiple_times(void) {
    /* Clone semantics: a can be cloned multiple times, each copy is independent */
    printf("  test_check_clone_multiple_times...");
    ASSERT_TRUE(check_source(
        "struct Foo { int x; }\n"
        "fn main() -> int {\n"
        "    Foo a = Foo{}\n"
        "    Foo b = a\n"
        "    Foo c = a\n"
        "    return 0\n"
        "}\n"
    ));
    printf(" ok\n");
}

static void test_check_move_non_struct_not_affected(void) {
    printf("  test_check_move_non_struct_not_affected...");
    ASSERT_TRUE(check_source(
        "fn take(int x) { }\n"
        "fn main() -> int {\n"
        "    int a = 42\n"
        "    int b = a\n"
        "    take(a)\n"
        "    take(a)\n"
        "    return 0\n"
        "}\n"
    ));
    printf(" ok\n");
}

static void test_check_move_valid_after_assignment(void) {
    printf("  test_check_move_valid_after_assignment...");
    ASSERT_TRUE(check_source(
        "struct Foo { int x; }\n"
        "fn main() -> int {\n"
        "    Foo a = Foo{}\n"
        "    Foo b = Foo{}\n"
        "    b = a\n"
        "    print(b.x)\n"
        "    return 0\n"
        "}\n"
    ));
    printf(" ok\n");
}

/* ---- Return Move Semantics Tests ---- */

static void test_check_return_move_struct(void) {
    printf("  test_check_return_move_struct...");
    ASSERT_TRUE(check_source(
        "struct Foo { int x; }\n"
        "fn create() -> Foo {\n"
        "    Foo a = Foo{ x: 1 }\n"
        "    return a\n"
        "}\n"
        "fn main() -> int {\n"
        "    Foo f = create()\n"
        "    return 0\n"
        "}\n"
    ));
    printf(" ok\n");
}

static void test_check_return_move_nested_scope(void) {
    printf("  test_check_return_move_nested_scope...");
    ASSERT_TRUE(check_source(
        "struct Foo { int x; }\n"
        "fn create() -> Foo {\n"
        "    Foo a = Foo{ x: 1 }\n"
        "    {\n"
        "        Foo b = Foo{ x: 2 }\n"
        "        return b\n"
        "    }\n"
        "}\n"
        "fn main() -> int {\n"
        "    Foo f = create()\n"
        "    return 0\n"
        "}\n"
    ));
    printf(" ok\n");
}

static void test_check_return_move_multiple_vars(void) {
    printf("  test_check_return_move_multiple_vars...");
    ASSERT_TRUE(check_source(
        "struct Foo { int x; }\n"
        "fn create(int which) -> Foo {\n"
        "    Foo a = Foo{ x: 1 }\n"
        "    Foo b = Foo{ x: 2 }\n"
        "    if (which == 1) {\n"
        "        return a\n"
        "    } else {\n"
        "        return b\n"
        "    }\n"
        "}\n"
        "fn main() -> int {\n"
        "    Foo f = create(1)\n"
        "    return 0\n"
        "}\n"
    ));
    printf(" ok\n");
}

static void test_check_return_move_with_other_vars(void) {
    printf("  test_check_return_move_with_other_vars...");
    ASSERT_TRUE(check_source(
        "struct Foo { int x; }\n"
        "fn create() -> Foo {\n"
        "    Foo a = Foo{ x: 1 }\n"
        "    Foo b = Foo{ x: 2 }\n"
        "    Foo c = Foo{ x: 3 }\n"
        "    return a\n"
        "}\n"
        "fn main() -> int {\n"
        "    Foo f = create()\n"
        "    return 0\n"
        "}\n"
    ));
    printf(" ok\n");
}

static void test_check_return_struct_in_call(void) {
    printf("  test_check_return_struct_in_call...");
    ASSERT_TRUE(check_source(
        "struct Foo { int x; }\n"
        "fn take(Foo f) { }\n"
        "fn create() -> Foo {\n"
        "    Foo a = Foo{ x: 1 }\n"
        "    return a\n"
        "}\n"
        "fn main() -> int {\n"
        "    take(create())\n"
        "    return 0\n"
        "}\n"
    ));
    printf(" ok\n");
}

static void test_check_return_temporary_literal(void) {
    printf("  test_check_return_temporary_literal...");
    ASSERT_TRUE(check_source(
        "struct Foo { int x; }\n"
        "fn create() -> Foo {\n"
        "    return Foo{ x: 42 }\n"
        "}\n"
        "fn main() -> int {\n"
        "    Foo f = create()\n"
        "    return 0\n"
        "}\n"
    ));
    printf(" ok\n");
}

/* ---- vec Batch A: is_empty / first / last ---- */

static void test_check_vec_is_empty_ok(void) {
    printf("  test_check_vec_is_empty_ok...");
    ASSERT_TRUE(check_source(
        "fn main() -> int {\n"
        "    vec(int) v\n"
        "    bool e = v.is_empty()\n"
        "    v.push(1)\n"
        "    bool ne = v.is_empty()\n"
        "    return 0\n"
        "}\n"
    ));
    printf(" ok\n");
}

static void test_check_vec_is_empty_no_args(void) {
    printf("  test_check_vec_is_empty_no_args...");
    ASSERT_FALSE(check_source(
        "fn main() -> int {\n"
        "    vec(int) v\n"
        "    bool e = v.is_empty(99)\n"
        "    return 0\n"
        "}\n"
    ));
    printf(" ok\n");
}

static void test_check_vec_first_ok(void) {
    printf("  test_check_vec_first_ok...");
    ASSERT_TRUE(check_source(
        "fn main() -> int {\n"
        "    vec(int) v\n"
        "    v.push(10)\n"
        "    int x = v.first()\n"
        "    return x\n"
        "}\n"
    ));
    printf(" ok\n");
}

static void test_check_vec_last_ok(void) {
    printf("  test_check_vec_last_ok...");
    ASSERT_TRUE(check_source(
        "fn main() -> int {\n"
        "    vec(int) v\n"
        "    v.push(10)\n"
        "    v.push(20)\n"
        "    int x = v.last()\n"
        "    return x\n"
        "}\n"
    ));
    printf(" ok\n");
}

static void test_check_vec_first_string_ok(void) {
    printf("  test_check_vec_first_string_ok...");
    ASSERT_TRUE(check_source(
        "fn main() -> int {\n"
        "    vec(string) v\n"
        "    v.push(\"hello\")\n"
        "    string s = v.first()\n"
        "    return 0\n"
        "}\n"
    ));
    printf(" ok\n");
}

static void test_check_vec_last_string_ok(void) {
    printf("  test_check_vec_last_string_ok...");
    ASSERT_TRUE(check_source(
        "fn main() -> int {\n"
        "    vec(string) v\n"
        "    v.push(\"a\")\n"
        "    v.push(\"b\")\n"
        "    string s = v.last()\n"
        "    return 0\n"
        "}\n"
    ));
    printf(" ok\n");
}

static void test_check_vec_first_no_args(void) {
    printf("  test_check_vec_first_no_args...");
    ASSERT_FALSE(check_source(
        "fn main() -> int {\n"
        "    vec(int) v\n"
        "    int x = v.first(1)\n"
        "    return 0\n"
        "}\n"
    ));
    printf(" ok\n");
}

static void test_check_vec_last_no_args(void) {
    printf("  test_check_vec_last_no_args...");
    ASSERT_FALSE(check_source(
        "fn main() -> int {\n"
        "    vec(int) v\n"
        "    int x = v.last(1)\n"
        "    return 0\n"
        "}\n"
    ));
    printf(" ok\n");
}

/* ---- vec Batch B: truncate / remove / swap / reverse ---- */

static void test_check_vec_truncate_ok(void) {
    printf("  test_check_vec_truncate_ok...");
    ASSERT_TRUE(check_source(
        "fn main() -> int {\n"
        "    vec(int) v\n"
        "    v.push(1)\n"
        "    v.push(2)\n"
        "    v.push(3)\n"
        "    v.truncate(1)\n"
        "    return 0\n"
        "}\n"
    ));
    printf(" ok\n");
}

static void test_check_vec_truncate_wrong_type(void) {
    printf("  test_check_vec_truncate_wrong_type...");
    ASSERT_FALSE(check_source(
        "fn main() -> int {\n"
        "    vec(int) v\n"
        "    v.truncate(true)\n"
        "    return 0\n"
        "}\n"
    ));
    printf(" ok\n");
}

static void test_check_vec_truncate_wrong_argc(void) {
    printf("  test_check_vec_truncate_wrong_argc...");
    ASSERT_FALSE(check_source(
        "fn main() -> int {\n"
        "    vec(int) v\n"
        "    v.truncate(1, 2)\n"
        "    return 0\n"
        "}\n"
    ));
    printf(" ok\n");
}

static void test_check_vec_remove_ok(void) {
    printf("  test_check_vec_remove_ok...");
    ASSERT_TRUE(check_source(
        "fn main() -> int {\n"
        "    vec(int) v\n"
        "    v.push(10)\n"
        "    v.push(20)\n"
        "    v.remove(0)\n"
        "    return 0\n"
        "}\n"
    ));
    printf(" ok\n");
}

static void test_check_vec_remove_wrong_type(void) {
    printf("  test_check_vec_remove_wrong_type...");
    ASSERT_FALSE(check_source(
        "fn main() -> int {\n"
        "    vec(int) v\n"
        "    v.remove(3.14)\n"
        "    return 0\n"
        "}\n"
    ));
    printf(" ok\n");
}

static void test_check_vec_swap_ok(void) {
    printf("  test_check_vec_swap_ok...");
    ASSERT_TRUE(check_source(
        "fn main() -> int {\n"
        "    vec(int) v\n"
        "    v.push(1)\n"
        "    v.push(2)\n"
        "    v.swap(0, 1)\n"
        "    return 0\n"
        "}\n"
    ));
    printf(" ok\n");
}

static void test_check_vec_swap_wrong_argc(void) {
    printf("  test_check_vec_swap_wrong_argc...");
    ASSERT_FALSE(check_source(
        "fn main() -> int {\n"
        "    vec(int) v\n"
        "    v.swap(0)\n"
        "    return 0\n"
        "}\n"
    ));
    printf(" ok\n");
}

static void test_check_vec_swap_wrong_type(void) {
    printf("  test_check_vec_swap_wrong_type...");
    ASSERT_FALSE(check_source(
        "fn main() -> int {\n"
        "    vec(int) v\n"
        "    v.swap(0, true)\n"
        "    return 0\n"
        "}\n"
    ));
    printf(" ok\n");
}

static void test_check_vec_reverse_ok(void) {
    printf("  test_check_vec_reverse_ok...");
    ASSERT_TRUE(check_source(
        "fn main() -> int {\n"
        "    vec(int) v\n"
        "    v.push(1)\n"
        "    v.push(2)\n"
        "    v.push(3)\n"
        "    v.reverse()\n"
        "    return 0\n"
        "}\n"
    ));
    printf(" ok\n");
}

static void test_check_vec_reverse_no_args(void) {
    printf("  test_check_vec_reverse_no_args...");
    ASSERT_FALSE(check_source(
        "fn main() -> int {\n"
        "    vec(int) v\n"
        "    v.reverse(1)\n"
        "    return 0\n"
        "}\n"
    ));
    printf(" ok\n");
}

/* ---- vec Batch C: extend / insert ---- */

static void test_check_vec_extend_ok(void) {
    printf("  test_check_vec_extend_ok...");
    ASSERT_TRUE(check_source(
        "fn main() -> int {\n"
        "    vec(int) a\n"
        "    a.push(1)\n"
        "    vec(int) b\n"
        "    b.push(2)\n"
        "    a.extend(b)\n"
        "    return 0\n"
        "}\n"
    ));
    printf(" ok\n");
}

static void test_check_vec_extend_string_ok(void) {
    printf("  test_check_vec_extend_string_ok...");
    ASSERT_TRUE(check_source(
        "fn main() -> int {\n"
        "    vec(string) a\n"
        "    a.push(\"x\")\n"
        "    vec(string) b\n"
        "    b.push(\"y\")\n"
        "    a.extend(b)\n"
        "    return 0\n"
        "}\n"
    ));
    printf(" ok\n");
}

static void test_check_vec_extend_wrong_argc(void) {
    printf("  test_check_vec_extend_wrong_argc...");
    ASSERT_FALSE(check_source(
        "fn main() -> int {\n"
        "    vec(int) a\n"
        "    a.extend()\n"
        "    return 0\n"
        "}\n"
    ));
    printf(" ok\n");
}

static void test_check_vec_extend_wrong_elem_type(void) {
    printf("  test_check_vec_extend_wrong_elem_type...");
    ASSERT_FALSE(check_source(
        "fn main() -> int {\n"
        "    vec(int) a\n"
        "    vec(string) b\n"
        "    a.extend(b)\n"
        "    return 0\n"
        "}\n"
    ));
    printf(" ok\n");
}

static void test_check_vec_extend_not_vec(void) {
    printf("  test_check_vec_extend_not_vec...");
    ASSERT_FALSE(check_source(
        "fn main() -> int {\n"
        "    vec(int) a\n"
        "    a.extend(42)\n"
        "    return 0\n"
        "}\n"
    ));
    printf(" ok\n");
}

static void test_check_vec_insert_ok(void) {
    printf("  test_check_vec_insert_ok...");
    ASSERT_TRUE(check_source(
        "fn main() -> int {\n"
        "    vec(int) v\n"
        "    v.push(1)\n"
        "    v.push(3)\n"
        "    v.insert(1, 2)\n"
        "    return 0\n"
        "}\n"
    ));
    printf(" ok\n");
}

static void test_check_vec_insert_string_ok(void) {
    printf("  test_check_vec_insert_string_ok...");
    ASSERT_TRUE(check_source(
        "fn main() -> int {\n"
        "    vec(string) v\n"
        "    v.push(\"a\")\n"
        "    v.push(\"c\")\n"
        "    v.insert(1, \"b\")\n"
        "    return 0\n"
        "}\n"
    ));
    printf(" ok\n");
}

static void test_check_vec_insert_wrong_argc(void) {
    printf("  test_check_vec_insert_wrong_argc...");
    ASSERT_FALSE(check_source(
        "fn main() -> int {\n"
        "    vec(int) v\n"
        "    v.insert(0)\n"
        "    return 0\n"
        "}\n"
    ));
    printf(" ok\n");
}

static void test_check_vec_insert_non_integer_idx(void) {
    printf("  test_check_vec_insert_non_integer_idx...");
    ASSERT_FALSE(check_source(
        "fn main() -> int {\n"
        "    vec(int) v\n"
        "    v.insert(1.5, 42)\n"
        "    return 0\n"
        "}\n"
    ));
    printf(" ok\n");
}

static void test_check_vec_insert_wrong_elem_type(void) {
    printf("  test_check_vec_insert_wrong_elem_type...");
    ASSERT_FALSE(check_source(
        "fn main() -> int {\n"
        "    vec(int) v\n"
        "    v.insert(0, \"not an int\")\n"
        "    return 0\n"
        "}\n"
    ));
    printf(" ok\n");
}

/* ---- vec Batch D: contains / index_of / resize / copy ---- */

static void test_check_vec_contains_ok(void) {
    printf("  test_check_vec_contains_ok...");
    ASSERT_TRUE(check_source(
        "fn main() -> int {\n"
        "    vec(int) v\n"
        "    v.push(1)\n"
        "    bool b = v.contains(1)\n"
        "    return 0\n"
        "}\n"
    ));
    printf(" ok\n");
}

static void test_check_vec_contains_string_ok(void) {
    printf("  test_check_vec_contains_string_ok...");
    ASSERT_TRUE(check_source(
        "fn main() -> int {\n"
        "    vec(string) v\n"
        "    v.push(\"hello\")\n"
        "    bool b = v.contains(\"hello\")\n"
        "    return 0\n"
        "}\n"
    ));
    printf(" ok\n");
}

static void test_check_vec_contains_wrong_type(void) {
    printf("  test_check_vec_contains_wrong_type...");
    ASSERT_FALSE(check_source(
        "fn main() -> int {\n"
        "    vec(int) v\n"
        "    bool b = v.contains(\"not an int\")\n"
        "    return 0\n"
        "}\n"
    ));
    printf(" ok\n");
}

static void test_check_vec_index_of_ok(void) {
    printf("  test_check_vec_index_of_ok...");
    ASSERT_TRUE(check_source(
        "fn main() -> int {\n"
        "    vec(int) v\n"
        "    v.push(42)\n"
        "    int idx = v.index_of(42)\n"
        "    return 0\n"
        "}\n"
    ));
    printf(" ok\n");
}

static void test_check_vec_index_of_wrong_type(void) {
    printf("  test_check_vec_index_of_wrong_type...");
    ASSERT_FALSE(check_source(
        "fn main() -> int {\n"
        "    vec(int) v\n"
        "    int idx = v.index_of(3.14)\n"
        "    return 0\n"
        "}\n"
    ));
    printf(" ok\n");
}

static void test_check_vec_resize_ok(void) {
    printf("  test_check_vec_resize_ok...");
    ASSERT_TRUE(check_source(
        "fn main() -> int {\n"
        "    vec(int) v\n"
        "    v.resize(10)\n"
        "    return 0\n"
        "}\n"
    ));
    printf(" ok\n");
}

static void test_check_vec_resize_wrong_type(void) {
    printf("  test_check_vec_resize_wrong_type...");
    ASSERT_FALSE(check_source(
        "fn main() -> int {\n"
        "    vec(int) v\n"
        "    v.resize(3.14)\n"
        "    return 0\n"
        "}\n"
    ));
    printf(" ok\n");
}

static void test_check_vec_copy_ok(void) {
    printf("  test_check_vec_copy_ok...");
    ASSERT_TRUE(check_source(
        "fn main() -> int {\n"
        "    vec(int) a\n"
        "    a.push(1)\n"
        "    vec(int) b = a.copy()\n"
        "    return 0\n"
        "}\n"
    ));
    printf(" ok\n");
}

static void test_check_vec_copy_no_args(void) {
    printf("  test_check_vec_copy_no_args...");
    ASSERT_FALSE(check_source(
        "fn main() -> int {\n"
        "    vec(int) v\n"
        "    v.copy(1)\n"
        "    return 0\n"
        "}\n"
    ));
    printf(" ok\n");
}

/* ---- vec Batch E: sort / sort_by / slice / shrink_to_fit ---- */

static void test_check_vec_sort_ok(void) {
    printf("  test_check_vec_sort_ok...");
    ASSERT_TRUE(check_source(
        "fn main() -> int {\n"
        "    vec(int) v\n"
        "    v.push(3)\n"
        "    v.push(1)\n"
        "    v.push(2)\n"
        "    v.sort()\n"
        "    return 0\n"
        "}\n"
    ));
    printf(" ok\n");
}

static void test_check_vec_sort_string_ok(void) {
    printf("  test_check_vec_sort_string_ok...");
    ASSERT_TRUE(check_source(
        "fn main() -> int {\n"
        "    vec(string) v\n"
        "    v.push(\"b\")\n"
        "    v.push(\"a\")\n"
        "    v.sort()\n"
        "    return 0\n"
        "}\n"
    ));
    printf(" ok\n");
}

static void test_check_vec_sort_no_args(void) {
    printf("  test_check_vec_sort_no_args...");
    ASSERT_FALSE(check_source(
        "fn main() -> int {\n"
        "    vec(int) v\n"
        "    v.sort(42)\n"
        "    return 0\n"
        "}\n"
    ));
    printf(" ok\n");
}

static void test_check_vec_sort_by_ok(void) {
    printf("  test_check_vec_sort_by_ok...");
    ASSERT_TRUE(check_source(
        "fn cmp(int a, int b) -> int { return a - b }\n"
        "fn main() -> int {\n"
        "    vec(int) v\n"
        "    v.push(3)\n"
        "    v.push(1)\n"
        "    v.sort_by(cmp)\n"
        "    return 0\n"
        "}\n"
    ));
    printf(" ok\n");
}

static void test_check_vec_sort_by_wrong_argc(void) {
    printf("  test_check_vec_sort_by_wrong_argc...");
    ASSERT_FALSE(check_source(
        "fn main() -> int {\n"
        "    vec(int) v\n"
        "    v.sort_by()\n"
        "    return 0\n"
        "}\n"
    ));
    printf(" ok\n");
}

static void test_check_vec_slice_ok(void) {
    printf("  test_check_vec_slice_ok...");
    ASSERT_TRUE(check_source(
        "fn main() -> int {\n"
        "    vec(int) v\n"
        "    v.push(1)\n"
        "    v.push(2)\n"
        "    v.push(3)\n"
        "    vec(int) s = v.slice(0, 2)\n"
        "    return 0\n"
        "}\n"
    ));
    printf(" ok\n");
}

static void test_check_vec_slice_wrong_argc(void) {
    printf("  test_check_vec_slice_wrong_argc...");
    ASSERT_FALSE(check_source(
        "fn main() -> int {\n"
        "    vec(int) v\n"
        "    vec(int) s = v.slice(0)\n"
        "    return 0\n"
        "}\n"
    ));
    printf(" ok\n");
}

static void test_check_vec_slice_wrong_type(void) {
    printf("  test_check_vec_slice_wrong_type...");
    ASSERT_FALSE(check_source(
        "fn main() -> int {\n"
        "    vec(int) v\n"
        "    vec(int) s = v.slice(0.5, 2)\n"
        "    return 0\n"
        "}\n"
    ));
    printf(" ok\n");
}

static void test_check_vec_shrink_to_fit_ok(void) {
    printf("  test_check_vec_shrink_to_fit_ok...");
    ASSERT_TRUE(check_source(
        "fn main() -> int {\n"
        "    vec(int) v\n"
        "    v.push(1)\n"
        "    v.push(2)\n"
        "    v.shrink_to_fit()\n"
        "    return 0\n"
        "}\n"
    ));
    printf(" ok\n");
}

static void test_check_vec_shrink_to_fit_no_args(void) {
    printf("  test_check_vec_shrink_to_fit_no_args...");
    ASSERT_FALSE(check_source(
        "fn main() -> int {\n"
        "    vec(int) v\n"
        "    v.shrink_to_fit(1)\n"
        "    return 0\n"
        "}\n"
    ));
    printf(" ok\n");
}

/* ======================================================================
   Move Semantics Phase A: String move tracking (linear, no control flow)
   ====================================================================== */

/* --- Static string: vec.push does NOT move, var stays live --- */
static void test_move_static_string_push_no_move(void) {
    printf("  test_move_static_string_push_no_move...");
    ASSERT_TRUE(check_source(
        "fn main() -> int {\n"
        "    vec(string) v\n"
        "    string a = \"hello\"\n"       /* static string literal */
        "    v.push(a)\n"                   /* should NOT move a */
        "    print(a)\n"                    /* a still LIVE — must not error */
        "    return 0\n"
        "}\n"
    ));
    printf(" ok\n");
}

/* --- Dynamic string: vec.push moves the source --- */
static void test_move_dynamic_string_push_marks_moved(void) {
    printf("  test_move_dynamic_string_push_marks_moved...");
    ASSERT_FALSE(check_source(
        "fn main() -> int {\n"
        "    vec(string) v\n"
        "    string b = \"world\".upper()\n"  /* dynamic string */
        "    v.push(b)\n"                      /* moves b */
        "    print(b)\n"                       /* ERROR: use of moved variable 'b' */
        "    return 0\n"
        "}\n"
    ));
    printf(" ok\n");
}

/* --- Use of moved variable in second vec.push --- */
static void test_move_double_push_error(void) {
    printf("  test_move_double_push_error...");
    ASSERT_FALSE(check_source(
        "fn main() -> int {\n"
        "    vec(string) v\n"
        "    string s = \"hello\".upper()\n"
        "    v.push(s)\n"   /* s: MOVED */
        "    v.push(s)\n"   /* ERROR: use of moved variable 's' */
        "    return 0\n"
        "}\n"
    ));
    printf(" ok\n");
}

/* --- Dynamic string: map.set CLONES the key, source stays live (BF-039) ---
   map.set deep-copies its key/value (codegen __ls_map_XX_set), so the source
   variable is NOT moved and remains usable. Pre-BF-039 the checker wrongly
   marked it moved (false positive); the move-mark was removed in BF-039. */
static void test_move_map_set_key_clones_no_move(void) {
    printf("  test_move_map_set_key_clones_no_move...");
    ASSERT_TRUE(check_source(
        "fn main() -> int {\n"
        "    map(string, int) m\n"
        "    string k = \"key\".upper()\n"  /* dynamic */
        "    m.set(k, 1)\n"                 /* clones k (no move) */
        "    print(k)\n"                    /* k still LIVE — must not error */
        "    return 0\n"
        "}\n"
    ));
    printf(" ok\n");
}

/* --- Static string key in map.set: NOT moved --- */
static void test_move_map_set_static_key_no_move(void) {
    printf("  test_move_map_set_static_key_no_move...");
    ASSERT_TRUE(check_source(
        "fn main() -> int {\n"
        "    map(string, int) m\n"
        "    string k = \"key\"\n"  /* static */
        "    m.set(k, 1)\n"
        "    print(k)\n"            /* k still LIVE — must not error */
        "    return 0\n"
        "}\n"
    ));
    printf(" ok\n");
}

/* --- Dynamic string: map.set CLONES the value, source stays live (BF-039) --- */
static void test_move_map_set_value_clones_no_move(void) {
    printf("  test_move_map_set_value_clones_no_move...");
    ASSERT_TRUE(check_source(
        "fn main() -> int {\n"
        "    map(string, string) m\n"
        "    string v = \"val\".upper()\n"  /* dynamic */
        "    m.set(\"key\", v)\n"           /* clones v (no move) */
        "    print(v)\n"                    /* v still LIVE — must not error */
        "    return 0\n"
        "}\n"
    ));
    printf(" ok\n");
}

/* --- Direct string-to-string assignment moves source (dynamic) --- */
static void test_move_direct_assignment_moves(void) {
    printf("  test_move_direct_assignment_moves...");
    ASSERT_FALSE(check_source(
        "fn main() -> int {\n"
        "    string s = \"hello\".upper()\n"  /* dynamic */
        "    string t = s\n"                   /* moves s */
        "    print(s)\n"                       /* ERROR */
        "    return 0\n"
        "}\n"
    ));
    printf(" ok\n");
}

/* --- Direct string-to-string assignment from static: NOT moved --- */
static void test_move_static_assignment_no_move(void) {
    printf("  test_move_static_assignment_no_move...");
    ASSERT_TRUE(check_source(
        "fn main() -> int {\n"
        "    string s = \"hello\"\n"  /* static */
        "    string t = s\n"           /* s is static → NOT moved */
        "    print(s)\n"               /* s still LIVE */
        "    print(t)\n"
        "    return 0\n"
        "}\n"
    ));
    printf(" ok\n");
}

/* --- Re-assign to moved variable is an error --- */
static void test_move_reassign_to_moved_error(void) {
    printf("  test_move_reassign_to_moved_error...");
    ASSERT_FALSE(check_source(
        "fn main() -> int {\n"
        "    vec(string) v\n"
        "    string s = \"hello\".upper()\n"
        "    v.push(s)\n"              /* s: MOVED */
        "    s = \"new\".upper()\n"   /* ERROR: re-assign to moved variable */
        "    return 0\n"
        "}\n"
    ));
    printf(" ok\n");
}

/* --- __move explicit move on dynamic string --- */
static void test_move_explicit_move_builtin(void) {
    printf("  test_move_explicit_move_builtin...");
    ASSERT_FALSE(check_source(
        "fn main() -> int {\n"
        "    vec(string) v\n"
        "    string s = \"hello\".upper()\n"
        "    v.push(__move(s))\n"  /* explicit move */
        "    print(s)\n"           /* ERROR: use of moved variable */
        "    return 0\n"
        "}\n"
    ));
    printf(" ok\n");
}

/* --- __move on static string forces move --- */
static void test_move_explicit_move_static_forced(void) {
    printf("  test_move_explicit_move_static_forced...");
    ASSERT_FALSE(check_source(
        "fn main() -> int {\n"
        "    vec(string) v\n"
        "    string s = \"hello\"\n"   /* static */
        "    v.push(__move(s))\n"      /* __move forces move even on static */
        "    print(s)\n"               /* ERROR: use of moved variable */
        "    return 0\n"
        "}\n"
    ));
    printf(" ok\n");
}

/* --- __move on non-movable type is an error --- */
static void test_move_explicit_move_non_movable_error(void) {
    printf("  test_move_explicit_move_non_movable_error...");
    ASSERT_FALSE(check_source(
        "fn main() -> int {\n"
        "    int x = 42\n"
        "    int y = __move(x)\n"  /* ERROR: int is not movable */
        "    return 0\n"
        "}\n"
    ));
    printf(" ok\n");
}

/* --- Function call does NOT move caller's string (deep copy) --- */
static void test_move_fn_param_no_move_caller(void) {
    printf("  test_move_fn_param_no_move_caller...");
    ASSERT_TRUE(check_source(
        "fn greet(string name) {\n"
        "    print(name)\n"
        "}\n"
        "fn main() -> int {\n"
        "    string s = \"Alice\".upper()\n"  /* dynamic */
        "    greet(s)\n"                        /* deep copy — s stays LIVE */
        "    print(s)\n"                        /* OK */
        "    greet(s)\n"                        /* can call again */
        "    return 0\n"
        "}\n"
    ));
    printf(" ok\n");
}

/* --- vec.insert also moves the element --- */
static void test_move_vec_insert_moves(void) {
    printf("  test_move_vec_insert_moves...");
    ASSERT_FALSE(check_source(
        "fn main() -> int {\n"
        "    vec(string) v\n"
        "    v.push(\"a\")\n"
        "    string s = \"b\".upper()\n"  /* dynamic */
        "    v.insert(0, s)\n"             /* moves s */
        "    print(s)\n"                   /* ERROR */
        "    return 0\n"
        "}\n"
    ));
    printf(" ok\n");
}

/* --- is_static_string propagates through assignment --- */
static void test_move_static_propagates_through_assign(void) {
    printf("  test_move_static_propagates_through_assign...");
    ASSERT_TRUE(check_source(
        /* t = s where s is static → t is also static → vec.push(t) does NOT move t */
        "fn main() -> int {\n"
        "    vec(string) v\n"
        "    string s = \"hello\"\n"   /* static */
        "    string t = s\n"            /* s is static, not moved; t is also static */
        "    v.push(t)\n"               /* t is static → not moved */
        "    print(t)\n"                /* t still LIVE */
        "    return 0\n"
        "}\n"
    ));
    printf(" ok\n");
}

/* ======================================================================
   Move Semantics Phase 3: Struct move tracking (has_drop structs)
   ======================================================================
   A struct is "movable" iff type_is_movable(T) is true, which (for structs)
   requires has_drop == true (struct contains a string field, a nested
   has_drop struct, or a user-defined __drop method). Structs without any
   drop requirement (plain POD) keep the prior clone semantics and are NOT
   affected by these tests. */

/* --- Assign with-drop struct: source is marked moved --- */
static void test_move_struct_with_drop_assignment(void) {
    printf("  test_move_struct_with_drop_assignment...");
    ASSERT_FALSE(check_source(
        "struct Person { string name; int age; }\n"
        "fn main() -> int {\n"
        "    Person p\n"
        "    p.name = \"Alice\".upper()\n"
        "    Person q = p\n"        /* q = p moves p */
        "    print(p.age)\n"          /* ERROR: use of moved variable 'p' */
        "    return 0\n"
        "}\n"
    ));
    printf(" ok\n");
}

/* --- POD struct (no drop) retains clone semantics --- */
static void test_move_struct_no_drop_clone_ok(void) {
    printf("  test_move_struct_no_drop_clone_ok...");
    ASSERT_TRUE(check_source(
        "struct Pod { int x; int y; }\n"
        "fn main() -> int {\n"
        "    Pod a = Pod{}\n"
        "    Pod b = a\n"
        "    Pod c = a\n"             /* plain struct → no move tracking */
        "    print(a.x)\n"
        "    return 0\n"
        "}\n"
    ));
    printf(" ok\n");
}

/* --- Field-level assignment does NOT move the struct --- */
static void test_move_struct_field_assign_no_move(void) {
    printf("  test_move_struct_field_assign_no_move...");
    ASSERT_TRUE(check_source(
        "struct Person { string name; int age; }\n"
        "fn main() -> int {\n"
        "    Person p\n"
        "    p.name = \"Alice\".upper()\n"  /* field write — does not move p */
        "    p.name = \"Bob\".upper()\n"    /* still OK */
        "    print(p.name)\n"                /* p still LIVE */
        "    return 0\n"
        "}\n"
    ));
    printf(" ok\n");
}

/* --- vec.push on with-drop struct marks it moved --- */
static void test_move_struct_vec_push_moves(void) {
    printf("  test_move_struct_vec_push_moves...");
    ASSERT_FALSE(check_source(
        "struct Person { string name; int age; }\n"
        "fn main() -> int {\n"
        "    vec(Person) people\n"
        "    Person p\n"
        "    p.name = \"Alice\".upper()\n"
        "    people.push(p)\n"          /* moves p */
        "    print(p.age)\n"             /* ERROR */
        "    return 0\n"
        "}\n"
    ));
    printf(" ok\n");
}

/* --- Re-assignment to moved with-drop struct is an error --- */
static void test_move_struct_reassign_to_moved_error(void) {
    printf("  test_move_struct_reassign_to_moved_error...");
    ASSERT_FALSE(check_source(
        "struct Person { string name; int age; }\n"
        "fn main() -> int {\n"
        "    Person p\n"
        "    p.name = \"Alice\".upper()\n"
        "    Person q = p\n"             /* p: MOVED */
        "    p = Person{}\n"              /* ERROR: use of moved variable 'p' on lhs */
        "    return 0\n"
        "}\n"
    ));
    printf(" ok\n");
}

/* --- Nested has_drop struct is tracked (via transitive has_drop) --- */
static void test_move_struct_nested_drop_moves(void) {
    printf("  test_move_struct_nested_drop_moves...");
    ASSERT_FALSE(check_source(
        "struct Inner { string s; }\n"
        "struct Outer { Inner inner; int tag; }\n"
        "fn main() -> int {\n"
        "    Outer o\n"
        "    o.inner.s = \"hi\".upper()\n"
        "    Outer q = o\n"            /* transitive has_drop → moves o */
        "    print(o.tag)\n"             /* ERROR */
        "    return 0\n"
        "}\n"
    ));
    printf(" ok\n");
}

/* --- Phase B: if/else with both branches moving struct → MOVED afterwards --- */
static void test_move_struct_if_else_both_move(void) {
    printf("  test_move_struct_if_else_both_move...");
    ASSERT_FALSE(check_source(
        "struct Person { string name; int age; }\n"
        "fn main() -> int {\n"
        "    vec(Person) ps\n"
        "    Person p\n"
        "    p.name = \"A\".upper()\n"
        "    bool c = true\n"
        "    if c { ps.push(p) } else { ps.push(p) }\n"
        "    print(p.age)\n"             /* ERROR: MOVED on every path */
        "    return 0\n"
        "}\n"
    ));
    printf(" ok\n");
}

/* --- Phase B: if-only (no else) moving struct → MAYBE_MOVED afterwards --- */
static void test_move_struct_if_only_maybe_moved(void) {
    printf("  test_move_struct_if_only_maybe_moved...");
    ASSERT_FALSE(check_source(
        "struct Person { string name; int age; }\n"
        "fn main() -> int {\n"
        "    vec(Person) ps\n"
        "    Person p\n"
        "    p.name = \"A\".upper()\n"
        "    bool c = true\n"
        "    if c { ps.push(p) }\n"
        "    print(p.age)\n"             /* ERROR: MAYBE_MOVED = death */
        "    return 0\n"
        "}\n"
    ));
    printf(" ok\n");
}

/* ---- Main ---- */

int main(void) {
    printf("=== Type System Tests ===\n");
    test_primitive_singletons();
    test_type_equality();
    test_type_queries();
    test_type_name();
    test_type_clone();
    test_type_enum();

    printf("\n=== Symbol Table Tests ===\n");
    test_scope_basic();
    test_scope_nesting();
    test_scope_duplicate_define();
    test_scope_resolve_local();

    printf("\n=== Type Checker Tests (correct programs) ===\n");
    test_check_var_decl_ok();
    test_check_arithmetic_ok();
    test_check_comparison();
    test_check_logical();
    test_check_function_call_ok();
    test_check_struct_field_access();
    test_check_match_arms_consistent();
    test_check_scope_shadowing();
    test_check_forward_function_ref();
    test_check_string_concat();
    test_check_unary_ops();
    test_check_pointer_deref();
    test_check_impl_method();
    test_check_samples_hello();
    test_check_samples_factorial();
    test_check_samples_struct();
    test_check_samples_match();

    printf("\n=== Type Checker Tests (type errors correctly rejected) ===\n");
    test_check_var_decl_type_mismatch();
    test_check_undefined_variable();
    test_check_arithmetic_type_mismatch();
    test_check_bool_in_arithmetic();
    test_check_logical_non_bool();
    test_check_function_call_wrong_arg_count();
    test_check_function_call_wrong_arg_type();
    test_check_return_type_mismatch();
    test_check_if_condition_must_be_bool();
    test_check_while_condition_must_be_bool();
    test_check_struct_field_nonexistent();
    test_check_unary_neg_bool();
    test_check_deref_non_pointer();
    test_check_assign_type_mismatch();
    test_check_duplicate_variable();

    printf("\n=== Array Type Checker Tests ===\n");
    test_check_array_ok();
    test_check_array_size_mismatch();
    test_check_array_element_type_mismatch();
    test_check_array_index_non_integer();
    test_check_array_no_field();
    test_check_array_foreach_ok();
    test_check_array_param_ok();
    test_check_array_param_size_mismatch();

    printf("\n=== Global Variable Type Checker Tests ===\n");
    test_check_global_var_ok();
    test_check_global_var_type_mismatch();
    test_check_global_var_used_in_function();
    test_check_global_array_ok();

    printf("\n=== String LsString Type Checker Tests ===\n");
    test_check_string_length_ok();
    test_check_string_no_field();
    test_check_string_compare_ok();
    test_check_string_concat_to_var();

    printf("\n=== String Method Type Checker Tests ===\n");
    test_check_string_empty_ok();
    test_check_string_at_ok();
    test_check_string_find_ok();
    test_check_string_contains_ok();
    test_check_string_starts_ends_with_ok();
    test_check_string_compare_method_ok();
    test_check_string_method_wrong_args();
    test_check_string_unknown_method();

    printf("\n=== String Method Batch 2 Type Checker Tests ===\n");
    test_check_string_upper_ok();
    test_check_string_lower_ok();
    test_check_string_substr_ok();
    test_check_string_trim_ok();
    test_check_string_replace_ok();
    test_check_string_batch2_wrong_args();

    printf("\n=== String Method Batch 3 Type Checker Tests ===\n");
    test_check_string_rfind_ok();
    test_check_string_count_ok();
    test_check_string_substr_one_arg_ok();
    test_check_string_split_ok();
    test_check_string_join_ok();
    test_check_string_batch3_wrong_args();

    printf("\n=== String Conversion Builtin Type Checker Tests ===\n");
    test_check_to_string_ok();
    test_check_to_string_wrong_type();
    test_check_to_string_wrong_arg_count();
    test_check_from_int_ok();
    test_check_from_int_wrong_type();
    test_check_from_float_ok();
    test_check_from_float_wrong_type();

    printf("\n=== Struct Method (implicit self + static) Tests ===\n");
    test_check_instance_method_ok();
    test_check_static_method_ok();
    test_check_instance_method_wrong_args();
    test_check_instance_method_via_typename_fails();

    printf("\n=== new Expression Type Checker Tests ===\n");
    test_check_new_ok();
    test_check_new_partial_fields_ok();
    test_check_new_unknown_struct();
    test_check_new_unknown_field();
    test_check_new_field_type_mismatch();
    test_check_new_duplicate_field();

    printf("\n=== Clone Semantics Type Checker Tests ===\n");
    test_check_move_struct_assignment();
    test_check_move_struct_call();
    test_check_move_duplicate_in_call();
    test_check_move_temporary_literal();
    test_check_move_struct_field_allowed();
    test_check_clone_multiple_times();
    test_check_move_non_struct_not_affected();
    test_check_move_valid_after_assignment();

    printf("\n=== Return Move Semantics Type Checker Tests ===\n");
    test_check_return_move_struct();
    test_check_return_move_nested_scope();
    test_check_return_move_multiple_vars();
    test_check_return_move_with_other_vars();
    test_check_return_struct_in_call();
    test_check_return_temporary_literal();

    printf("\n=== vec Batch A Type Checker Tests ===\n");
    test_check_vec_is_empty_ok();
    test_check_vec_is_empty_no_args();
    test_check_vec_first_ok();
    test_check_vec_last_ok();
    test_check_vec_first_string_ok();
    test_check_vec_last_string_ok();
    test_check_vec_first_no_args();
    test_check_vec_last_no_args();

    printf("\n=== vec Batch B Type Checker Tests ===\n");
    test_check_vec_truncate_ok();
    test_check_vec_truncate_wrong_type();
    test_check_vec_truncate_wrong_argc();
    test_check_vec_remove_ok();
    test_check_vec_remove_wrong_type();
    test_check_vec_swap_ok();
    test_check_vec_swap_wrong_argc();
    test_check_vec_swap_wrong_type();
    test_check_vec_reverse_ok();
    test_check_vec_reverse_no_args();

    printf("\n=== vec Batch C Type Checker Tests ===\n");
    test_check_vec_extend_ok();
    test_check_vec_extend_string_ok();
    test_check_vec_extend_wrong_argc();
    test_check_vec_extend_wrong_elem_type();
    test_check_vec_extend_not_vec();
    test_check_vec_insert_ok();
    test_check_vec_insert_string_ok();
    test_check_vec_insert_wrong_argc();
    test_check_vec_insert_non_integer_idx();
    test_check_vec_insert_wrong_elem_type();

    printf("\n=== vec Batch D Type Checker Tests ===\n");
    test_check_vec_contains_ok();
    test_check_vec_contains_string_ok();
    test_check_vec_contains_wrong_type();
    test_check_vec_index_of_ok();
    test_check_vec_index_of_wrong_type();
    test_check_vec_resize_ok();
    test_check_vec_resize_wrong_type();
    test_check_vec_copy_ok();
    test_check_vec_copy_no_args();

    printf("\n=== vec Batch E Type Checker Tests ===\n");
    test_check_vec_sort_ok();
    test_check_vec_sort_string_ok();
    test_check_vec_sort_no_args();
    test_check_vec_sort_by_ok();
    test_check_vec_sort_by_wrong_argc();
    test_check_vec_slice_ok();
    test_check_vec_slice_wrong_argc();
    test_check_vec_slice_wrong_type();
    test_check_vec_shrink_to_fit_ok();
    test_check_vec_shrink_to_fit_no_args();

    printf("\n=== String Move Semantics Phase A Tests ===\n");
    test_move_static_string_push_no_move();
    test_move_dynamic_string_push_marks_moved();
    test_move_double_push_error();
    test_move_map_set_key_clones_no_move();
    test_move_map_set_static_key_no_move();
    test_move_map_set_value_clones_no_move();
    test_move_direct_assignment_moves();
    test_move_static_assignment_no_move();
    test_move_reassign_to_moved_error();
    test_move_explicit_move_builtin();
    test_move_explicit_move_static_forced();
    test_move_explicit_move_non_movable_error();
    test_move_fn_param_no_move_caller();
    test_move_vec_insert_moves();
    test_move_static_propagates_through_assign();

    printf("\n=== Struct Move Semantics Phase 3 Tests ===\n");
    test_move_struct_with_drop_assignment();
    test_move_struct_no_drop_clone_ok();
    test_move_struct_field_assign_no_move();
    test_move_struct_vec_push_moves();
    test_move_struct_reassign_to_moved_error();
    test_move_struct_nested_drop_moves();
    test_move_struct_if_else_both_move();
    test_move_struct_if_only_maybe_moved();

    printf("\n=== Results: %d/%d passed ===\n", tests_passed, tests_run);
    return tests_passed == tests_run ? 0 : 1;
}
