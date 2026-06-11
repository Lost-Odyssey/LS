/* test_types.c — Unit tests for type system, symbol table, and type checker */
#include "common.h"
#include "types.h"
#include "symtable.h"
#include "checker.h"
#include "parser.h"
#include "module.h"
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

/* Helper: parse + check, return true if no errors.
   P5-4 S-2: literals are Str (std.str) — mirror the real pipeline: inject the
   prelude import and check with a module registry (resolution falls back to
   the executable's directory, build/<cfg>/std). */
static bool check_source(const char *source) {
    AstNode *ast = parse(source, "<test>");
    if (ast == NULL) return false;
    ast_inject_std_str_import(ast);
    ModuleRegistry *reg = module_registry_new();
    bool ok = checker_check(ast, "<test>", reg, NULL);
    module_registry_free(reg);
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

    printf("\n=== String Method Type Checker Tests ===\n");

    printf("\n=== String Method Batch 2 Type Checker Tests ===\n");

    printf("\n=== String Method Batch 3 Type Checker Tests ===\n");

    printf("\n=== String Conversion Builtin Type Checker Tests ===\n");

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

    printf("\n=== vec Batch Type Checker Tests ===\n");
    printf("  skipped: builtin vec(T) syntax is unreachable after Phase 3 P3-1\n");

    printf("\n=== String Move Semantics Phase A Tests ===\n");
    printf("  skipped: builtin vec(T) static-string push tests are unreachable after Phase 3 P3-1\n");
    test_move_explicit_move_non_movable_error();
    printf("  skipped: builtin vec(T) static propagation test is unreachable after Phase 3 P3-1\n");

    printf("\n=== Struct Move Semantics Phase 3 Tests ===\n");
    test_move_struct_no_drop_clone_ok();

    printf("\n=== Results: %d/%d passed ===\n", tests_passed, tests_run);
    return tests_passed == tests_run ? 0 : 1;
}
