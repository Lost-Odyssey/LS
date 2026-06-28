/* test_parser.c — Unit tests for the LS Pratt parser */
#ifdef _MSC_VER
#define _CRT_SECURE_NO_WARNINGS
#endif
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "parser.h"

/* ---- Test helpers ---- */

#define ASSERT(cond, msg) do { \
    if (!(cond)) { \
        fprintf(stderr, "FAIL: %s:%d: %s\n", __FILE__, __LINE__, (msg)); \
        exit(1); \
    } \
} while(0)

#define ASSERT_EQ(a, b) ASSERT((a) == (b), #a " != " #b)
#define ASSERT_STR_EQ(a, b) ASSERT(strcmp((a), (b)) == 0, #a " != " #b)

/* Parse helper: suppress stderr during error tests */
static AstNode *parse_source(const char *src) {
    return parse(src, "<test>");
}

/* ---- Test functions ---- */

static void test_empty_program(void) {
    AstNode *ast = parse_source("");
    ASSERT(ast != NULL, "empty program should parse");
    ASSERT_EQ(ast->kind, AST_PROGRAM);
    ASSERT_EQ(ast->as.program.decl_count, 0);
    ast_free(ast);
    printf("  PASS: test_empty_program\n");
}

static void test_var_decl_int(void) {
    AstNode *ast = parse_source("int x = 42");
    ASSERT(ast != NULL, "parse failed");
    ASSERT_EQ(ast->kind, AST_PROGRAM);
    ASSERT_EQ(ast->as.program.decl_count, 1);
    AstNode *decl = ast->as.program.decls[0];
    ASSERT_EQ(decl->kind, AST_VAR_DECL);
    ASSERT_STR_EQ(decl->as.var_decl.name, "x");
    ASSERT(decl->as.var_decl.var_type != NULL, "type not null");
    ASSERT_EQ(decl->as.var_decl.var_type->kind, TYPE_NODE_PRIMITIVE);
    ASSERT_EQ(decl->as.var_decl.var_type->as.primitive, TOKEN_TYPE_INT);
    ASSERT(decl->as.var_decl.init != NULL, "init not null");
    ASSERT_EQ(decl->as.var_decl.init->kind, AST_INT_LIT);
    ASSERT_EQ(decl->as.var_decl.init->as.int_lit.value, 42LL);
    ast_free(ast);
    printf("  PASS: test_var_decl_int\n");
}

static void test_fn_decl_with_return(void) {
    AstNode *ast = parse_source(
        "def add(int a, int b) -> int { return a + b }"
    );
    ASSERT(ast != NULL, "parse failed");
    ASSERT_EQ(ast->as.program.decl_count, 1);
    AstNode *fn = ast->as.program.decls[0];
    ASSERT_EQ(fn->kind, AST_FN_DECL);
    ASSERT_STR_EQ(fn->as.fn_decl.name, "add");
    ASSERT_EQ(fn->as.fn_decl.param_count, 2);
    ASSERT_STR_EQ(fn->as.fn_decl.param_names[0], "a");
    ASSERT_STR_EQ(fn->as.fn_decl.param_names[1], "b");
    ASSERT(fn->as.fn_decl.return_type != NULL, "return type not null");
    ASSERT_EQ(fn->as.fn_decl.return_type->kind, TYPE_NODE_PRIMITIVE);
    ASSERT_EQ(fn->as.fn_decl.return_type->as.primitive, TOKEN_TYPE_INT);
    ASSERT(fn->as.fn_decl.body != NULL, "body not null");
    ASSERT_EQ(fn->as.fn_decl.body->kind, AST_BLOCK);
    ASSERT_EQ(fn->as.fn_decl.body->as.block.stmt_count, 1);
    AstNode *ret = fn->as.fn_decl.body->as.block.stmts[0];
    ASSERT_EQ(ret->kind, AST_RETURN);
    ASSERT(ret->as.return_stmt.value != NULL, "return value not null");
    ASSERT_EQ(ret->as.return_stmt.value->kind, AST_BINARY);
    ASSERT_EQ(ret->as.return_stmt.value->as.binary.op, TOKEN_PLUS);
    ast_free(ast);
    printf("  PASS: test_fn_decl_with_return\n");
}

static void test_fn_decl_void_return(void) {
    AstNode *ast = parse_source("def greet(Str name) { }");
    ASSERT(ast != NULL, "parse failed");
    ASSERT_EQ(ast->as.program.decl_count, 1);
    AstNode *fn = ast->as.program.decls[0];
    ASSERT_EQ(fn->kind, AST_FN_DECL);
    ASSERT_STR_EQ(fn->as.fn_decl.name, "greet");
    ASSERT_EQ(fn->as.fn_decl.param_count, 1);
    ASSERT(fn->as.fn_decl.return_type == NULL, "void return type is NULL");
    ast_free(ast);
    printf("  PASS: test_fn_decl_void_return\n");
}

static void test_binary_precedence(void) {
    /* 2 + 3 * 4 should parse as 2 + (3 * 4) */
    AstNode *ast = parse_source("2 + 3 * 4");
    ASSERT(ast != NULL, "parse failed");
    ASSERT_EQ(ast->as.program.decl_count, 1);
    AstNode *stmt = ast->as.program.decls[0];
    ASSERT_EQ(stmt->kind, AST_EXPR_STMT);
    AstNode *expr = stmt->as.expr_stmt.expr;
    ASSERT_EQ(expr->kind, AST_BINARY);
    ASSERT_EQ(expr->as.binary.op, TOKEN_PLUS);
    /* left should be INT_LIT(2) */
    ASSERT_EQ(expr->as.binary.left->kind, AST_INT_LIT);
    ASSERT_EQ(expr->as.binary.left->as.int_lit.value, 2LL);
    /* right should be BINARY(*, 3, 4) */
    AstNode *right = expr->as.binary.right;
    ASSERT_EQ(right->kind, AST_BINARY);
    ASSERT_EQ(right->as.binary.op, TOKEN_STAR);
    ASSERT_EQ(right->as.binary.left->as.int_lit.value, 3LL);
    ASSERT_EQ(right->as.binary.right->as.int_lit.value, 4LL);
    ast_free(ast);
    printf("  PASS: test_binary_precedence\n");
}

static void test_if_else(void) {
    AstNode *ast = parse_source(
        "if (x > 0) { return x } else { return 0 }"
    );
    ASSERT(ast != NULL, "parse failed");
    ASSERT_EQ(ast->as.program.decl_count, 1);
    AstNode *stmt = ast->as.program.decls[0];
    ASSERT_EQ(stmt->kind, AST_IF);
    ASSERT(stmt->as.if_stmt.cond != NULL, "cond not null");
    ASSERT(stmt->as.if_stmt.then_block != NULL, "then not null");
    ASSERT(stmt->as.if_stmt.else_block != NULL, "else not null");
    ASSERT_EQ(stmt->as.if_stmt.cond->kind, AST_BINARY);
    ASSERT_EQ(stmt->as.if_stmt.cond->as.binary.op, TOKEN_GT);
    ast_free(ast);
    printf("  PASS: test_if_else\n");
}

static void test_while_loop(void) {
    AstNode *ast = parse_source("while (i < 10) { i = i + 1 }");
    ASSERT(ast != NULL, "parse failed");
    ASSERT_EQ(ast->as.program.decl_count, 1);
    AstNode *stmt = ast->as.program.decls[0];
    ASSERT_EQ(stmt->kind, AST_WHILE);
    ASSERT(stmt->as.while_stmt.cond != NULL, "cond not null");
    ASSERT(stmt->as.while_stmt.body != NULL, "body not null");
    ASSERT_EQ(stmt->as.while_stmt.cond->kind, AST_BINARY);
    ASSERT_EQ(stmt->as.while_stmt.cond->as.binary.op, TOKEN_LT);
    ast_free(ast);
    printf("  PASS: test_while_loop\n");
}

/* comptime field iteration (Stage 3b), step 1: parser/AST round-trip only —
   the checker unroll lands in step 2, so these parse but are not checked/run. */
static void test_comptime_for_field(void) {
    AstNode *ast = parse_source(
        "def dump(Config v) {\n"
        "    comptime for f in fields(Config) {\n"
        "        @print(v.(f))\n"
        "    }\n"
        "}\n"
    );
    ASSERT(ast != NULL, "parse failed");
    ASSERT_EQ(ast->as.program.decl_count, 1);
    AstNode *fn = ast->as.program.decls[0];
    ASSERT_EQ(fn->kind, AST_FN_DECL);
    AstNode *body = fn->as.fn_decl.body;
    ASSERT_EQ(body->kind, AST_BLOCK);
    ASSERT_EQ(body->as.block.stmt_count, 1);
    AstNode *cf = body->as.block.stmts[0];
    ASSERT_EQ(cf->kind, AST_COMPTIME_FOR);
    ASSERT_STR_EQ(cf->as.comptime_for.var, "f");
    ASSERT(cf->as.comptime_for.over_type != NULL, "over_type not null");
    ASSERT_EQ(cf->as.comptime_for.over_type->kind, TYPE_NODE_NAMED);
    ASSERT_STR_EQ(cf->as.comptime_for.over_type->as.named.name, "Config");
    AstNode *cbody = cf->as.comptime_for.body;
    ASSERT_EQ(cbody->kind, AST_BLOCK);
    ASSERT_EQ(cbody->as.block.stmt_count, 1);
    AstNode *es = cbody->as.block.stmts[0];
    ASSERT_EQ(es->kind, AST_EXPR_STMT);
    AstNode *call = es->as.expr_stmt.expr;
    ASSERT_EQ(call->kind, AST_CALL);
    ASSERT_EQ(call->as.call.arg_count, 1);
    AstNode *arg = call->as.call.args[0];
    ASSERT_EQ(arg->kind, AST_COMPTIME_FIELD);
    ASSERT_STR_EQ(arg->as.comptime_field.handle, "f");
    ASSERT(arg->as.comptime_field.object != NULL, "v.(f) object not null");
    ASSERT_EQ(arg->as.comptime_field.object->kind, AST_IDENT);
    ASSERT_STR_EQ(arg->as.comptime_field.object->as.ident.name, "v");
    ast_free(ast);
    printf("  PASS: test_comptime_for_field\n");
}

static void test_comptime_if_else(void) {
    AstNode *ast = parse_source(
        "def f2(Config v) {\n"
        "    comptime if f.index > 0 {\n"
        "        @print(1)\n"
        "    } else {\n"
        "        @print(2)\n"
        "    }\n"
        "}\n"
    );
    ASSERT(ast != NULL, "parse failed");
    AstNode *ci = ast->as.program.decls[0]->as.fn_decl.body->as.block.stmts[0];
    ASSERT_EQ(ci->kind, AST_COMPTIME_IF);
    ASSERT(ci->as.comptime_if.cond != NULL, "cond not null");
    ASSERT_EQ(ci->as.comptime_if.cond->kind, AST_BINARY);
    ASSERT_EQ(ci->as.comptime_if.cond->as.binary.op, TOKEN_GT);
    ASSERT(ci->as.comptime_if.then_block != NULL, "then not null");
    ASSERT_EQ(ci->as.comptime_if.then_block->kind, AST_BLOCK);
    ASSERT(ci->as.comptime_if.else_block != NULL, "else not null");
    ASSERT_EQ(ci->as.comptime_if.else_block->kind, AST_BLOCK);
    ast_free(ast);

    /* else-chain: `else comptime if` nests a COMPTIME_IF in the else slot */
    AstNode *ast2 = parse_source(
        "def f3(Config v) {\n"
        "    comptime if f.index > 0 { @print(1) }\n"
        "    else comptime if f.index > 1 { @print(2) }\n"
        "}\n"
    );
    ASSERT(ast2 != NULL, "parse failed (chain)");
    AstNode *ci2 = ast2->as.program.decls[0]->as.fn_decl.body->as.block.stmts[0];
    ASSERT_EQ(ci2->kind, AST_COMPTIME_IF);
    ASSERT(ci2->as.comptime_if.else_block != NULL, "chain else not null");
    ASSERT_EQ(ci2->as.comptime_if.else_block->kind, AST_COMPTIME_IF);
    ast_free(ast2);
    printf("  PASS: test_comptime_if_else\n");
}

static void test_comptime_parse_errors(void) {
    /* `comptime` must be followed by `for` or `if` */
    ASSERT(parse("def g() { comptime @print(1) }", "<err>") == NULL,
           "comptime without for/if should fail");
    /* the iterable must be fields(Type) */
    ASSERT(parse("def g(Config v) { comptime for f in v { } }", "<err>") == NULL,
           "comptime for over non-fields() should fail");
    printf("  PASS: test_comptime_parse_errors\n");
}

static void test_comptime_const(void) {
    /* top-level scalar comptime constant: comptime <type> <name> = <expr> */
    AstNode *ast = parse_source("comptime int MASK = (1 << 9) - 1\n");
    ASSERT(ast != NULL, "parse failed (scalar)");
    ASSERT_EQ(ast->as.program.decl_count, 1);
    AstNode *cc = ast->as.program.decls[0];
    ASSERT_EQ(cc->kind, AST_COMPTIME_CONST);
    ASSERT_STR_EQ(cc->as.comptime_const.name, "MASK");
    ASSERT(cc->as.comptime_const.decl_type != NULL, "decl_type not null");
    ASSERT(cc->as.comptime_const.value != NULL, "value not null");
    ASSERT_EQ(cc->as.comptime_const.value->kind, AST_BINARY);
    ast_free(ast);

    /* block-form: comptime <type> <name> = comptime { ... return v } */
    AstNode *ast2 = parse_source(
        "comptime int SUM = comptime {\n"
        "    int s = 0\n"
        "    for i in 0..4 { s = s + i }\n"
        "    return s\n"
        "}\n");
    ASSERT(ast2 != NULL, "parse failed (block)");
    AstNode *cb = ast2->as.program.decls[0];
    ASSERT_EQ(cb->kind, AST_COMPTIME_CONST);
    ASSERT_STR_EQ(cb->as.comptime_const.name, "SUM");
    ASSERT(cb->as.comptime_const.value != NULL, "block value not null");
    ASSERT_EQ(cb->as.comptime_const.value->kind, AST_COMPTIME_BLOCK);
    AstNode *blk = cb->as.comptime_const.value->as.comptime_block.block;
    ASSERT(blk != NULL, "comptime block body not null");
    ASSERT_EQ(blk->kind, AST_BLOCK);
    ast_free(ast2);

    /* local position inside a function body */
    AstNode *ast3 = parse_source(
        "def f() {\n"
        "    comptime f64 INV = 1.0 / 2.0\n"
        "    @print(1)\n"
        "}\n");
    ASSERT(ast3 != NULL, "parse failed (local)");
    AstNode *body = ast3->as.program.decls[0]->as.fn_decl.body;
    ASSERT_EQ(body->as.block.stmts[0]->kind, AST_COMPTIME_CONST);
    ASSERT_STR_EQ(body->as.block.stmts[0]->as.comptime_const.name, "INV");
    ast_free(ast3);

    printf("  PASS: test_comptime_const\n");
}

static void test_for_c_loop(void) {
    /* C-style for loop: for (int i = 0; i < 10; i = i + 1) { } */
    AstNode *ast = parse_source("for (int i = 0; i < 10; i = i + 1) { @print(i) }");
    ASSERT(ast != NULL, "parse failed");
    ASSERT_EQ(ast->as.program.decl_count, 1);
    AstNode *stmt = ast->as.program.decls[0];
    ASSERT_EQ(stmt->kind, AST_FOR_C);
    ASSERT(stmt->as.for_c_stmt.init != NULL, "init not null");
    ASSERT(stmt->as.for_c_stmt.cond != NULL, "cond not null");
    ASSERT(stmt->as.for_c_stmt.update != NULL, "update not null");
    ASSERT(stmt->as.for_c_stmt.body != NULL, "body not null");
    ASSERT_EQ(stmt->as.for_c_stmt.init->kind, AST_VAR_DECL);
    ASSERT_EQ(stmt->as.for_c_stmt.cond->kind, AST_BINARY);
    ASSERT_EQ(stmt->as.for_c_stmt.update->kind, AST_ASSIGN);
    ast_free(ast);
    printf("  PASS: test_for_c_loop\n");
}

static void test_foreach_range(void) {
    /* foreach with range: for i in 0..10 { } */
    AstNode *ast = parse_source("for i in 0..10 { @print(i) }");
    ASSERT(ast != NULL, "parse failed");
    ASSERT_EQ(ast->as.program.decl_count, 1);
    AstNode *stmt = ast->as.program.decls[0];
    ASSERT_EQ(stmt->kind, AST_FOR);
    ASSERT_STR_EQ(stmt->as.for_stmt.var, "i");
    ASSERT(stmt->as.for_stmt.iter != NULL, "iter not null");
    ASSERT_EQ(stmt->as.for_stmt.iter->kind, AST_RANGE);
    ASSERT_EQ(stmt->as.for_stmt.iter->as.range.start->kind, AST_INT_LIT);
    ASSERT_EQ(stmt->as.for_stmt.iter->as.range.end->kind, AST_INT_LIT);
    ASSERT(stmt->as.for_stmt.body != NULL, "body not null");
    ast_free(ast);
    printf("  PASS: test_foreach_range\n");
}

static void test_foreach_int(void) {
    /* foreach with integer: for i in n { } */
    AstNode *ast = parse_source("for x in 5 { @print(x) }");
    ASSERT(ast != NULL, "parse failed");
    AstNode *stmt = ast->as.program.decls[0];
    ASSERT_EQ(stmt->kind, AST_FOR);
    ASSERT_STR_EQ(stmt->as.for_stmt.var, "x");
    ASSERT_EQ(stmt->as.for_stmt.iter->kind, AST_INT_LIT);
    ast_free(ast);
    printf("  PASS: test_foreach_int\n");
}

static void test_for_c_empty_clauses(void) {
    /* Infinite loop: for (;;) { break } */
    AstNode *ast = parse_source("for (;;) { break }");
    ASSERT(ast != NULL, "parse failed");
    ASSERT_EQ(ast->as.program.decl_count, 1);
    AstNode *stmt = ast->as.program.decls[0];
    ASSERT_EQ(stmt->kind, AST_FOR_C);
    ASSERT(stmt->as.for_c_stmt.init == NULL, "init should be null");
    ASSERT(stmt->as.for_c_stmt.cond == NULL, "cond should be null");
    ASSERT(stmt->as.for_c_stmt.update == NULL, "update should be null");
    ASSERT(stmt->as.for_c_stmt.body != NULL, "body not null");
    ast_free(ast);
    printf("  PASS: test_for_c_empty_clauses\n");
}

static void test_match_expr(void) {
    AstNode *ast = parse_source(
        "match n {\n"
        "    0 => 1,\n"
        "    _ => n,\n"
        "}"
    );
    ASSERT(ast != NULL, "parse failed");
    ASSERT_EQ(ast->as.program.decl_count, 1);
    AstNode *stmt = ast->as.program.decls[0];
    ASSERT_EQ(stmt->kind, AST_EXPR_STMT);
    AstNode *m = stmt->as.expr_stmt.expr;
    ASSERT_EQ(m->kind, AST_MATCH);
    ASSERT(m->as.match.subject != NULL, "subject not null");
    ASSERT_EQ(m->as.match.arm_count, 2);
    /* arm 0: pattern=0, body=1 */
    ASSERT_EQ(m->as.match.arms[0].pattern->kind, AST_INT_LIT);
    ASSERT_EQ(m->as.match.arms[0].pattern->as.int_lit.value, 0LL);
    ASSERT_EQ(m->as.match.arms[0].body->kind, AST_INT_LIT);
    ASSERT_EQ(m->as.match.arms[0].body->as.int_lit.value, 1LL);
    /* arm 1: pattern=_ (IDENT "_"), body=n */
    ASSERT_EQ(m->as.match.arms[1].pattern->kind, AST_IDENT);
    ASSERT_STR_EQ(m->as.match.arms[1].pattern->as.ident.name, "_");
    ASSERT_EQ(m->as.match.arms[1].body->kind, AST_IDENT);
    ast_free(ast);
    printf("  PASS: test_match_expr\n");
}

static void test_struct_decl(void) {
    AstNode *ast = parse_source(
        "struct Point {\n"
        "    f64 x;\n"
        "    f64 y;\n"
        "}"
    );
    ASSERT(ast != NULL, "parse failed");
    ASSERT_EQ(ast->as.program.decl_count, 1);
    AstNode *s = ast->as.program.decls[0];
    ASSERT_EQ(s->kind, AST_STRUCT_DECL);
    ASSERT_STR_EQ(s->as.struct_decl.name, "Point");
    ASSERT_EQ(s->as.struct_decl.field_count, 2);
    ASSERT_STR_EQ(s->as.struct_decl.field_names[0], "x");
    ASSERT_STR_EQ(s->as.struct_decl.field_names[1], "y");
    ASSERT_EQ(s->as.struct_decl.field_types[0]->kind, TYPE_NODE_PRIMITIVE);
    ASSERT_EQ(s->as.struct_decl.field_types[0]->as.primitive, TOKEN_TYPE_F64);
    ast_free(ast);
    printf("  PASS: test_struct_decl\n");
}

static void test_enum_decl(void) {
    /* Mix of: no-payload variant, named-payload variant, unnamed-payload variant.
       Mix of separators: ';', ',', and none. */
    AstNode *ast = parse_source(
        "enum Shape {\n"
        "    Point\n"
        "    Circle(f64 radius);\n"
        "    Rect(f64, f64),\n"
        "}"
    );
    ASSERT(ast != NULL, "parse failed");
    ASSERT_EQ(ast->as.program.decl_count, 1);
    AstNode *e = ast->as.program.decls[0];
    ASSERT_EQ(e->kind, AST_ENUM_DECL);
    ASSERT_STR_EQ(e->as.enum_decl.name, "Shape");
    ASSERT_EQ(e->as.enum_decl.variant_count, 3);

    /* Variant 0: Point — no payload */
    ASSERT_STR_EQ(e->as.enum_decl.variants[0].name, "Point");
    ASSERT_EQ(e->as.enum_decl.variants[0].payload_count, 0);

    /* Variant 1: Circle(f64 radius) — named */
    ASSERT_STR_EQ(e->as.enum_decl.variants[1].name, "Circle");
    ASSERT_EQ(e->as.enum_decl.variants[1].payload_count, 1);
    ASSERT_EQ(e->as.enum_decl.variants[1].payload_types[0]->kind, TYPE_NODE_PRIMITIVE);
    ASSERT_EQ(e->as.enum_decl.variants[1].payload_types[0]->as.primitive, TOKEN_TYPE_F64);
    ASSERT_STR_EQ(e->as.enum_decl.variants[1].payload_names[0], "radius");

    /* Variant 2: Rect(f64, f64) — unnamed payload */
    ASSERT_STR_EQ(e->as.enum_decl.variants[2].name, "Rect");
    ASSERT_EQ(e->as.enum_decl.variants[2].payload_count, 2);
    ASSERT_EQ(e->as.enum_decl.variants[2].payload_types[0]->as.primitive, TOKEN_TYPE_F64);
    ASSERT(e->as.enum_decl.variants[2].payload_names[0] == NULL, "expected unnamed payload");
    ASSERT(e->as.enum_decl.variants[2].payload_names[1] == NULL, "expected unnamed payload");

    ast_free(ast);
    printf("  PASS: test_enum_decl\n");
}

static void test_load_lib(void) {
    AstNode *ast = parse_source("lib libc = load(\"libc.so\")");
    ASSERT(ast != NULL, "parse failed");
    ASSERT_EQ(ast->as.program.decl_count, 1);
    AstNode *n = ast->as.program.decls[0];
    ASSERT_EQ(n->kind, AST_LOAD_LIB);
    ASSERT_STR_EQ(n->as.load_lib.var_name, "libc");
    ASSERT_STR_EQ(n->as.load_lib.lib_path, "libc.so");
    ast_free(ast);
    printf("  PASS: test_load_lib\n");
}

static void test_module_decl(void) {
    AstNode *ast = parse_source("module math");
    ASSERT(ast != NULL, "parse failed");
    ASSERT_EQ(ast->as.program.decl_count, 1);
    AstNode *n = ast->as.program.decls[0];
    ASSERT_EQ(n->kind, AST_MODULE_DECL);
    ASSERT_STR_EQ(n->as.module_decl.name, "math");
    ast_free(ast);
    printf("  PASS: test_module_decl\n");
}

static void test_import_decl(void) {
    AstNode *ast = parse_source("import std.sys.io");
    ASSERT(ast != NULL, "parse failed");
    ASSERT_EQ(ast->as.program.decl_count, 1);
    AstNode *n = ast->as.program.decls[0];
    ASSERT_EQ(n->kind, AST_IMPORT_DECL);
    ASSERT_STR_EQ(n->as.import_decl.path, "std.sys.io");
    ast_free(ast);
    printf("  PASS: test_import_decl\n");
}

static void test_closure(void) {
    AstNode *ast = parse_source("def(int x) -> int { x * 2 }");
    ASSERT(ast != NULL, "parse failed");
    ASSERT_EQ(ast->as.program.decl_count, 1);
    AstNode *stmt = ast->as.program.decls[0];
    ASSERT_EQ(stmt->kind, AST_EXPR_STMT);
    AstNode *cl = stmt->as.expr_stmt.expr;
    ASSERT_EQ(cl->kind, AST_CLOSURE);
    ASSERT_EQ(cl->as.closure.param_count, 1);
    ASSERT_STR_EQ(cl->as.closure.param_names[0], "x");
    ASSERT_EQ(cl->as.closure.param_types[0]->kind, TYPE_NODE_PRIMITIVE);
    ASSERT_EQ(cl->as.closure.param_types[0]->as.primitive, TOKEN_TYPE_INT);
    ASSERT(cl->as.closure.return_type != NULL, "return type not null");
    ASSERT_EQ(cl->as.closure.return_type->as.primitive, TOKEN_TYPE_INT);
    ast_free(ast);
    printf("  PASS: test_closure\n");
}

static void test_assignment(void) {
    AstNode *ast = parse_source("x = 42");
    ASSERT(ast != NULL, "parse failed");
    ASSERT_EQ(ast->as.program.decl_count, 1);
    AstNode *n = ast->as.program.decls[0];
    ASSERT_EQ(n->kind, AST_ASSIGN);
    ASSERT_EQ(n->as.assign.op, TOKEN_ASSIGN);
    ASSERT_EQ(n->as.assign.target->kind, AST_IDENT);
    ASSERT_STR_EQ(n->as.assign.target->as.ident.name, "x");
    ASSERT_EQ(n->as.assign.value->kind, AST_INT_LIT);
    ASSERT_EQ(n->as.assign.value->as.int_lit.value, 42LL);
    ast_free(ast);
    printf("  PASS: test_assignment\n");
}

static void test_compound_assignment(void) {
    AstNode *ast = parse_source("x += 1");
    ASSERT(ast != NULL, "parse failed");
    ASSERT_EQ(ast->as.program.decl_count, 1);
    AstNode *n = ast->as.program.decls[0];
    ASSERT_EQ(n->kind, AST_ASSIGN);
    ASSERT_EQ(n->as.assign.op, TOKEN_PLUS_ASSIGN);
    ast_free(ast);
    printf("  PASS: test_compound_assignment\n");
}

static void test_pointer_type(void) {
    AstNode *ast = parse_source("*int ptr = &x");
    ASSERT(ast != NULL, "parse failed");
    ASSERT_EQ(ast->as.program.decl_count, 1);
    AstNode *decl = ast->as.program.decls[0];
    ASSERT_EQ(decl->kind, AST_VAR_DECL);
    ASSERT_STR_EQ(decl->as.var_decl.name, "ptr");
    ASSERT_EQ(decl->as.var_decl.var_type->kind, TYPE_NODE_POINTER);
    ASSERT_EQ(decl->as.var_decl.var_type->as.pointee->kind, TYPE_NODE_PRIMITIVE);
    ASSERT_EQ(decl->as.var_decl.var_type->as.pointee->as.primitive, TOKEN_TYPE_INT);
    /* init: &x — unary AMP */
    ASSERT(decl->as.var_decl.init != NULL, "init not null");
    ASSERT_EQ(decl->as.var_decl.init->kind, AST_UNARY);
    ASSERT_EQ(decl->as.var_decl.init->as.unary.op, TOKEN_AMP);
    ast_free(ast);
    printf("  PASS: test_pointer_type\n");
}

static void test_field_access(void) {
    AstNode *ast = parse_source("obj.field");
    ASSERT(ast != NULL, "parse failed");
    ASSERT_EQ(ast->as.program.decl_count, 1);
    AstNode *stmt = ast->as.program.decls[0];
    ASSERT_EQ(stmt->kind, AST_EXPR_STMT);
    AstNode *fa = stmt->as.expr_stmt.expr;
    ASSERT_EQ(fa->kind, AST_FIELD);
    ASSERT_STR_EQ(fa->as.field_access.field, "field");
    ASSERT_EQ(fa->as.field_access.object->kind, AST_IDENT);
    ast_free(ast);
    printf("  PASS: test_field_access\n");
}

static void test_method_call(void) {
    AstNode *ast = parse_source("obj.method(a, b)");
    ASSERT(ast != NULL, "parse failed");
    ASSERT_EQ(ast->as.program.decl_count, 1);
    AstNode *stmt = ast->as.program.decls[0];
    ASSERT_EQ(stmt->kind, AST_EXPR_STMT);
    AstNode *call = stmt->as.expr_stmt.expr;
    ASSERT_EQ(call->kind, AST_CALL);
    ASSERT_EQ(call->as.call.callee->kind, AST_FIELD);
    ASSERT_STR_EQ(call->as.call.callee->as.field_access.field, "method");
    ASSERT_EQ(call->as.call.arg_count, 2);
    ast_free(ast);
    printf("  PASS: test_method_call\n");
}

static void test_cast(void) {
    AstNode *ast = parse_source("x as f64");
    ASSERT(ast != NULL, "parse failed");
    ASSERT_EQ(ast->as.program.decl_count, 1);
    AstNode *stmt = ast->as.program.decls[0];
    ASSERT_EQ(stmt->kind, AST_EXPR_STMT);
    AstNode *cast = stmt->as.expr_stmt.expr;
    ASSERT_EQ(cast->kind, AST_CAST);
    ASSERT(cast->as.cast.target_type != NULL, "target type not null");
    ASSERT_EQ(cast->as.cast.target_type->kind, TYPE_NODE_PRIMITIVE);
    ASSERT_EQ(cast->as.cast.target_type->as.primitive, TOKEN_TYPE_F64);
    ast_free(ast);
    printf("  PASS: test_cast\n");
}

static void test_impl_decl(void) {
    AstNode *ast = parse_source(
        "methods Point {\n"
        "    def dist() -> f64 { return 0.0 }\n"
        "    static def origin() -> f64 { return 0.0 }\n"
        "}"
    );
    ASSERT(ast != NULL, "parse failed");
    ASSERT_EQ(ast->as.program.decl_count, 1);
    AstNode *impl = ast->as.program.decls[0];
    ASSERT_EQ(impl->kind, AST_IMPL_DECL);
    ASSERT_STR_EQ(impl->as.impl_decl.name, "Point");
    ASSERT_EQ(impl->as.impl_decl.method_count, 2);
    AstNode *method = impl->as.impl_decl.methods[0];
    ASSERT_EQ(method->kind, AST_FN_DECL);
    ASSERT_STR_EQ(method->as.fn_decl.name, "dist");
    ASSERT_EQ(method->as.fn_decl.is_static, false);
    ASSERT_STR_EQ(method->as.fn_decl.impl_struct_name, "Point");
    AstNode *static_method = impl->as.impl_decl.methods[1];
    ASSERT_EQ(static_method->as.fn_decl.is_static, true);
    ASSERT_STR_EQ(static_method->as.fn_decl.impl_struct_name, "Point");
    ast_free(ast);
    printf("  PASS: test_impl_decl\n");
}

static void test_syntax_error(void) {
    /* Suppress error output: redirect stderr */
    /* Instead just check that NULL is returned */
    /* 'int x = ' is incomplete — should return NULL */
    /* We can't easily suppress stderr in C without platform-specific tricks */
    /* So just run it and check result */
    AstNode *ast = parse("int x = ", "<test_err>");
    ASSERT(ast == NULL, "incomplete program should fail");
    printf("  PASS: test_syntax_error\n");
}

static void test_hello_sample(void) {
    /* Test the equivalent of hello.ls — all constructs in a function body */
    const char *src =
        "module main\n"
        "\n"
        "def main() -> int {\n"
        "    Str greeting = \"Hello, World!\"\n"
        "    int x = 42\n"
        "    f64 pi = 3.14159\n"
        "    bool flag = true\n"
        "    int sum = x + 8\n"
        "    int product = x * 2\n"
        "    int hex_val = 0xFF\n"
        "    int bin_val = 0b1010\n"
        "    *int ptr = &x\n"
        "    return 0\n"
        "}\n";

    AstNode *ast = parse(src, "hello.ls");
    ASSERT(ast != NULL, "hello.ls equivalent should parse without errors");
    ast_free(ast);

    /* Also try reading the actual file if available */
    FILE *f = fopen("tests/samples/hello.ls", "rb");
    if (f == NULL) f = fopen("../tests/samples/hello.ls", "rb");
    if (f != NULL) {
        fseek(f, 0, SEEK_END);
        long size = ftell(f);
        fseek(f, 0, SEEK_SET);
        char *source = (char *)malloc((size_t)size + 1);
        if (source != NULL) {
            size_t rd = fread(source, 1, (size_t)size, f);
            source[rd] = '\0';
            fclose(f);
            AstNode *ast2 = parse(source, "hello.ls");
            free(source);
            ASSERT(ast2 != NULL, "hello.ls file should parse without errors");
            ast_free(ast2);
        } else {
            fclose(f);
        }
    }
    printf("  PASS: test_hello_sample\n");
}

static void test_pointer_in_fn(void) {
    /* *int ptr = &x inside a function body */
    AstNode *ast = parse_source(
        "def main() -> int {\n"
        "    *int ptr = &x\n"
        "    return 0\n"
        "}"
    );
    ASSERT(ast != NULL, "pointer decl in def body should parse");
    ast_free(ast);
    printf("  PASS: test_pointer_in_fn\n");
}

static void test_pointer_after_other_decl(void) {
    /* *int ptr after other variable declarations */
    AstNode *ast = parse_source(
        "def main() -> int {\n"
        "    int x = 42\n"
        "    *int ptr = &x\n"
        "    return 0\n"
        "}"
    );
    ASSERT(ast != NULL, "pointer after decl should parse");
    ast_free(ast);
    printf("  PASS: test_pointer_after_other_decl\n");
}

static void test_multiple_stmts(void) {
    AstNode *ast = parse_source(
        "int a = 1\n"
        "int b = 2\n"
        "int c = a + b\n"
    );
    ASSERT(ast != NULL, "parse failed");
    ASSERT_EQ(ast->as.program.decl_count, 3);
    ast_free(ast);
    printf("  PASS: test_multiple_stmts\n");
}

static void test_nested_calls(void) {
    AstNode *ast = parse_source("foo(bar(1), baz(2, 3))");
    ASSERT(ast != NULL, "parse failed");
    ASSERT_EQ(ast->as.program.decl_count, 1);
    AstNode *stmt = ast->as.program.decls[0];
    ASSERT_EQ(stmt->kind, AST_EXPR_STMT);
    AstNode *call = stmt->as.expr_stmt.expr;
    ASSERT_EQ(call->kind, AST_CALL);
    ASSERT_EQ(call->as.call.arg_count, 2);
    ASSERT_EQ(call->as.call.args[0]->kind, AST_CALL);
    ASSERT_EQ(call->as.call.args[1]->kind, AST_CALL);
    ast_free(ast);
    printf("  PASS: test_nested_calls\n");
}

static void test_extern_fn(void) {
    AstNode *ast = parse_source("extern def puts(*u8 s) -> int from libc");
    ASSERT(ast != NULL, "parse failed");
    ASSERT_EQ(ast->as.program.decl_count, 1);
    AstNode *n = ast->as.program.decls[0];
    ASSERT_EQ(n->kind, AST_EXTERN_FN);
    ASSERT_STR_EQ(n->as.extern_fn.name, "puts");
    ASSERT_EQ(n->as.extern_fn.param_count, 1);
    ASSERT_STR_EQ(n->as.extern_fn.lib_name, "libc");
    ASSERT(n->as.extern_fn.return_type != NULL, "return type not null");
    ast_free(ast);
    printf("  PASS: test_extern_fn\n");
}

static void test_string_literals(void) {
    AstNode *ast = parse_source("Str s = \"hello\\nworld\"");
    ASSERT(ast != NULL, "parse failed");
    ASSERT_EQ(ast->as.program.decl_count, 1);
    AstNode *decl = ast->as.program.decls[0];
    ASSERT_EQ(decl->kind, AST_VAR_DECL);
    ASSERT(decl->as.var_decl.init != NULL, "init not null");
    ASSERT_EQ(decl->as.var_decl.init->kind, AST_STRING_LIT);
    /* The string should have \n processed */
    const char *val = decl->as.var_decl.init->as.string_lit.value;
    ASSERT(val[5] == '\n', "escape sequence processed");
    ast_free(ast);
    printf("  PASS: test_string_literals\n");
}

static void test_new_expr_zero_init(void) {
    /* new StructName — zero-initialized heap allocation */
    AstNode *ast = parse_source("*Point p = new Point");
    ASSERT(ast != NULL, "parse failed");
    ASSERT_EQ(ast->as.program.decl_count, 1);
    AstNode *decl = ast->as.program.decls[0];
    ASSERT_EQ(decl->kind, AST_VAR_DECL);
    ASSERT(decl->as.var_decl.init != NULL, "init not null");
    AstNode *ne = decl->as.var_decl.init;
    ASSERT_EQ(ne->kind, AST_NEW_EXPR);
    ASSERT_STR_EQ(ne->as.new_expr.struct_name, "Point");
    ASSERT_EQ(ne->as.new_expr.field_init_count, 0);
    ast_free(ast);
    printf("  PASS: test_new_expr_zero_init\n");
}

static void test_new_expr_with_fields(void) {
    /* new StructName { field: val, ... } */
    AstNode *ast = parse_source("*Point p = new Point { x: 1.0, y: 2.0 }");
    ASSERT(ast != NULL, "parse failed");
    AstNode *ne = ast->as.program.decls[0]->as.var_decl.init;
    ASSERT_EQ(ne->kind, AST_NEW_EXPR);
    ASSERT_STR_EQ(ne->as.new_expr.struct_name, "Point");
    ASSERT_EQ(ne->as.new_expr.field_init_count, 2);
    ASSERT_STR_EQ(ne->as.new_expr.field_inits[0].name, "x");
    ASSERT_STR_EQ(ne->as.new_expr.field_inits[1].name, "y");
    ASSERT_EQ(ne->as.new_expr.field_inits[0].value->kind, AST_FLOAT_LIT);
    ast_free(ast);
    printf("  PASS: test_new_expr_with_fields\n");
}

static void test_new_expr_trailing_comma(void) {
    /* Trailing comma inside field initializer list should be allowed */
    AstNode *ast = parse_source("*Point p = new Point { x: 1.0, }");
    ASSERT(ast != NULL, "parse failed");
    AstNode *ne = ast->as.program.decls[0]->as.var_decl.init;
    ASSERT_EQ(ne->kind, AST_NEW_EXPR);
    ASSERT_EQ(ne->as.new_expr.field_init_count, 1);
    ASSERT_STR_EQ(ne->as.new_expr.field_inits[0].name, "x");
    ast_free(ast);
    printf("  PASS: test_new_expr_trailing_comma\n");
}

static void test_fn_no_params(void) {
    AstNode *ast = parse_source("def main() -> int { return 0 }");
    ASSERT(ast != NULL, "parse failed");
    ASSERT_EQ(ast->as.program.decl_count, 1);
    AstNode *fn = ast->as.program.decls[0];
    ASSERT_EQ(fn->kind, AST_FN_DECL);
    ASSERT_EQ(fn->as.fn_decl.param_count, 0);
    ast_free(ast);
    printf("  PASS: test_fn_no_params\n");
}

static void test_semicolons_optional(void) {
    /* Semicolons should be OK and should also be omittable */
    AstNode *ast1 = parse_source("int x = 1; int y = 2;");
    AstNode *ast2 = parse_source("int x = 1\n int y = 2\n");
    ASSERT(ast1 != NULL, "with semicolons should parse");
    ASSERT(ast2 != NULL, "without semicolons should parse");
    ASSERT_EQ(ast1->as.program.decl_count, 2);
    ASSERT_EQ(ast2->as.program.decl_count, 2);
    ast_free(ast1);
    ast_free(ast2);
    printf("  PASS: test_semicolons_optional\n");
}

int main(void) {
    printf("=== test_parser ===\n");
    test_empty_program();
    test_var_decl_int();
    test_fn_decl_with_return();
    test_fn_decl_void_return();
    test_binary_precedence();
    test_if_else();
    test_while_loop();
    test_comptime_for_field();
    test_comptime_if_else();
    test_comptime_parse_errors();
    test_comptime_const();
    test_for_c_loop();
    test_for_c_empty_clauses();
    test_foreach_range();
    test_foreach_int();
    test_match_expr();
    test_struct_decl();
    test_enum_decl();
    test_load_lib();
    test_module_decl();
    test_import_decl();
    test_closure();
    test_assignment();
    test_compound_assignment();
    test_pointer_type();
    test_field_access();
    test_method_call();
    test_cast();
    test_impl_decl();
    test_syntax_error();
    test_hello_sample();
    test_pointer_in_fn();
    test_pointer_after_other_decl();
    test_multiple_stmts();
    test_nested_calls();
    test_extern_fn();
    test_string_literals();
    test_fn_no_params();
    test_semicolons_optional();
    test_new_expr_zero_init();
    test_new_expr_with_fields();
    test_new_expr_trailing_comma();
    printf("All tests passed.\n");
    return 0;
}
