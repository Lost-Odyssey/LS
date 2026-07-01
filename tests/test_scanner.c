/* test_scanner.c — Scanner unit tests */
#include "scanner.h"
#include "common.h"

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
#define ASSERT_STR_EQ(a, b) ASSERT(strcmp(a, b) == 0, #a " != " #b)

#define ASSERT_TOKEN(scanner, expected_type) do { \
    Token _t = scanner_next(&scanner); \
    tests_run++; \
    if (_t.type != (expected_type)) { \
        fprintf(stderr, "FAIL: %s:%d: expected %s, got %s (lexeme: '%.*s')\n", \
                __FILE__, __LINE__, \
                token_type_name(expected_type), \
                token_type_name(_t.type), \
                _t.length, _t.start); \
        return; \
    } \
    tests_passed++; \
} while(0)

#define ASSERT_TOKEN_VAL(scanner, expected_type, expected_lexeme) do { \
    Token _t = scanner_next(&scanner); \
    tests_run++; \
    if (_t.type != (expected_type)) { \
        fprintf(stderr, "FAIL: %s:%d: expected %s, got %s (lexeme: '%.*s')\n", \
                __FILE__, __LINE__, \
                token_type_name(expected_type), \
                token_type_name(_t.type), \
                _t.length, _t.start); \
        return; \
    } \
    tests_passed++; \
    tests_run++; \
    if (_t.length != (int)strlen(expected_lexeme) || \
        strncmp(_t.start, expected_lexeme, (size_t)_t.length) != 0) { \
        fprintf(stderr, "FAIL: %s:%d: expected lexeme '%s', got '%.*s'\n", \
                __FILE__, __LINE__, expected_lexeme, _t.length, _t.start); \
        return; \
    } \
    tests_passed++; \
} while(0)

/* ---- Test functions ---- */

static void test_empty_source(void) {
    Scanner s;
    scanner_init(&s, "");
    ASSERT_TOKEN(s, TOKEN_EOF);
    printf("  PASS: test_empty_source\n");
}

static void test_whitespace_only(void) {
    Scanner s;
    scanner_init(&s, "   \t\n\r  \n  ");
    ASSERT_TOKEN(s, TOKEN_EOF);
    printf("  PASS: test_whitespace_only\n");
}

static void test_single_line_comment(void) {
    Scanner s;
    scanner_init(&s, "// this is a comment\n42");
    ASSERT_TOKEN_VAL(s, TOKEN_INT_LIT, "42");
    ASSERT_TOKEN(s, TOKEN_EOF);
    printf("  PASS: test_single_line_comment\n");
}

static void test_multi_line_comment(void) {
    Scanner s;
    scanner_init(&s, "/* comment */ 42");
    ASSERT_TOKEN_VAL(s, TOKEN_INT_LIT, "42");
    ASSERT_TOKEN(s, TOKEN_EOF);
    printf("  PASS: test_multi_line_comment\n");
}

static void test_nested_comment(void) {
    Scanner s;
    scanner_init(&s, "/* outer /* inner */ still comment */ 99");
    ASSERT_TOKEN_VAL(s, TOKEN_INT_LIT, "99");
    ASSERT_TOKEN(s, TOKEN_EOF);
    printf("  PASS: test_nested_comment\n");
}

/* ---- Keywords ---- */

static void test_all_keywords(void) {
    Scanner s;
    scanner_init(&s, "def return if else while for in match struct enum methods "
                      "module import load self do end break continue extern as from public");

    ASSERT_TOKEN(s, TOKEN_FN);
    ASSERT_TOKEN(s, TOKEN_RETURN);
    ASSERT_TOKEN(s, TOKEN_IF);
    ASSERT_TOKEN(s, TOKEN_ELSE);
    ASSERT_TOKEN(s, TOKEN_WHILE);
    ASSERT_TOKEN(s, TOKEN_FOR);
    ASSERT_TOKEN(s, TOKEN_IN);
    ASSERT_TOKEN(s, TOKEN_MATCH);
    ASSERT_TOKEN(s, TOKEN_STRUCT);
    ASSERT_TOKEN(s, TOKEN_ENUM);
    ASSERT_TOKEN(s, TOKEN_IMPL);
    ASSERT_TOKEN(s, TOKEN_MODULE);
    ASSERT_TOKEN(s, TOKEN_IMPORT);
    ASSERT_TOKEN(s, TOKEN_LOAD);
    ASSERT_TOKEN(s, TOKEN_SELF);
    ASSERT_TOKEN(s, TOKEN_DO);
    ASSERT_TOKEN(s, TOKEN_END);
    ASSERT_TOKEN(s, TOKEN_BREAK);
    ASSERT_TOKEN(s, TOKEN_CONTINUE);
    ASSERT_TOKEN(s, TOKEN_EXTERN);
    ASSERT_TOKEN(s, TOKEN_AS);
    ASSERT_TOKEN(s, TOKEN_FROM);
    ASSERT_TOKEN(s, TOKEN_PUB);
    ASSERT_TOKEN(s, TOKEN_EOF);
    printf("  PASS: test_all_keywords\n");
}

static void test_type_keywords(void) {
    Scanner s;
    scanner_init(&s, "int i8 i16 i32 i64 u8 u16 u32 u64 f32 f64 bool string void lib"  /* string is an IDENTIFIER since P5-4 */);

    ASSERT_TOKEN(s, TOKEN_TYPE_INT);
    ASSERT_TOKEN(s, TOKEN_TYPE_I8);
    ASSERT_TOKEN(s, TOKEN_TYPE_I16);
    ASSERT_TOKEN(s, TOKEN_TYPE_I32);
    ASSERT_TOKEN(s, TOKEN_TYPE_I64);
    ASSERT_TOKEN(s, TOKEN_TYPE_U8);
    ASSERT_TOKEN(s, TOKEN_TYPE_U16);
    ASSERT_TOKEN(s, TOKEN_TYPE_U32);
    ASSERT_TOKEN(s, TOKEN_TYPE_U64);
    ASSERT_TOKEN(s, TOKEN_TYPE_F32);
    ASSERT_TOKEN(s, TOKEN_TYPE_F64);
    ASSERT_TOKEN(s, TOKEN_TYPE_BOOL);
    ASSERT_TOKEN(s, TOKEN_IDENTIFIER);  /* "string" demoted to identifier (P5-4 S-1) */
    ASSERT_TOKEN(s, TOKEN_TYPE_VOID);
    ASSERT_TOKEN(s, TOKEN_TYPE_LIB);
    ASSERT_TOKEN(s, TOKEN_EOF);
    printf("  PASS: test_type_keywords\n");
}

/* ---- Identifiers ---- */

static void test_identifiers(void) {
    Scanner s;
    scanner_init(&s, "foo bar_baz _hidden x123 myVar");

    ASSERT_TOKEN_VAL(s, TOKEN_IDENTIFIER, "foo");
    ASSERT_TOKEN_VAL(s, TOKEN_IDENTIFIER, "bar_baz");
    ASSERT_TOKEN_VAL(s, TOKEN_IDENTIFIER, "_hidden");
    ASSERT_TOKEN_VAL(s, TOKEN_IDENTIFIER, "x123");
    ASSERT_TOKEN_VAL(s, TOKEN_IDENTIFIER, "myVar");
    ASSERT_TOKEN(s, TOKEN_EOF);
    printf("  PASS: test_identifiers\n");
}

static void test_underscore_wildcard(void) {
    Scanner s;
    scanner_init(&s, "_ _a");
    ASSERT_TOKEN(s, TOKEN_UNDERSCORE);
    ASSERT_TOKEN_VAL(s, TOKEN_IDENTIFIER, "_a");
    ASSERT_TOKEN(s, TOKEN_EOF);
    printf("  PASS: test_underscore_wildcard\n");
}

/* ---- Number Literals ---- */

static void test_integer_literals(void) {
    Scanner s;
    scanner_init(&s, "0 42 12345");
    ASSERT_TOKEN_VAL(s, TOKEN_INT_LIT, "0");
    ASSERT_TOKEN_VAL(s, TOKEN_INT_LIT, "42");
    ASSERT_TOKEN_VAL(s, TOKEN_INT_LIT, "12345");
    ASSERT_TOKEN(s, TOKEN_EOF);
    printf("  PASS: test_integer_literals\n");
}

static void test_hex_literals(void) {
    Scanner s;
    scanner_init(&s, "0xFF 0x1A 0XBE");
    ASSERT_TOKEN_VAL(s, TOKEN_INT_LIT, "0xFF");
    ASSERT_TOKEN_VAL(s, TOKEN_INT_LIT, "0x1A");
    ASSERT_TOKEN_VAL(s, TOKEN_INT_LIT, "0XBE");
    ASSERT_TOKEN(s, TOKEN_EOF);
    printf("  PASS: test_hex_literals\n");
}

static void test_binary_literals(void) {
    Scanner s;
    scanner_init(&s, "0b1010 0B110");
    ASSERT_TOKEN_VAL(s, TOKEN_INT_LIT, "0b1010");
    ASSERT_TOKEN_VAL(s, TOKEN_INT_LIT, "0B110");
    ASSERT_TOKEN(s, TOKEN_EOF);
    printf("  PASS: test_binary_literals\n");
}

static void test_float_literals(void) {
    Scanner s;
    scanner_init(&s, "3.14 1.0 0.5 1.5e3 2.0e-5 3E+10");
    ASSERT_TOKEN_VAL(s, TOKEN_FLOAT_LIT, "3.14");
    ASSERT_TOKEN_VAL(s, TOKEN_FLOAT_LIT, "1.0");
    ASSERT_TOKEN_VAL(s, TOKEN_FLOAT_LIT, "0.5");
    ASSERT_TOKEN_VAL(s, TOKEN_FLOAT_LIT, "1.5e3");
    ASSERT_TOKEN_VAL(s, TOKEN_FLOAT_LIT, "2.0e-5");
    ASSERT_TOKEN_VAL(s, TOKEN_FLOAT_LIT, "3E+10");
    ASSERT_TOKEN(s, TOKEN_EOF);
    printf("  PASS: test_float_literals\n");
}

static void test_invalid_hex(void) {
    Scanner s;
    scanner_init(&s, "0xGG");
    ASSERT_TOKEN(s, TOKEN_ERROR);
    printf("  PASS: test_invalid_hex\n");
}

static void test_invalid_binary(void) {
    Scanner s;
    scanner_init(&s, "0b2");
    ASSERT_TOKEN(s, TOKEN_ERROR);
    printf("  PASS: test_invalid_binary\n");
}

/* ---- String Literals ---- */

static void test_string_literal(void) {
    Scanner s;
    scanner_init(&s, "\"hello world\"");
    ASSERT_TOKEN_VAL(s, TOKEN_STRING_LIT, "\"hello world\"");
    ASSERT_TOKEN(s, TOKEN_EOF);
    printf("  PASS: test_string_literal\n");
}

static void test_string_escapes(void) {
    Scanner s;
    scanner_init(&s, "\"\\n\\t\\\\\\\"\\0\\r\\a\\b\\x41\"");
    ASSERT_TOKEN(s, TOKEN_STRING_LIT);
    ASSERT_TOKEN(s, TOKEN_EOF);
    printf("  PASS: test_string_escapes\n");
}

static void test_unterminated_string(void) {
    Scanner s;
    scanner_init(&s, "\"hello");
    ASSERT_TOKEN(s, TOKEN_ERROR);
    printf("  PASS: test_unterminated_string\n");
}

static void test_unknown_escape(void) {
    Scanner s;
    scanner_init(&s, "\"\\q\"");
    ASSERT_TOKEN(s, TOKEN_ERROR);
    printf("  PASS: test_unknown_escape\n");
}

/* ---- Char Literals ---- */

static void test_char_literal(void) {
    Scanner s;
    scanner_init(&s, "'a' 'Z'");
    ASSERT_TOKEN_VAL(s, TOKEN_CHAR_LIT, "'a'");
    ASSERT_TOKEN_VAL(s, TOKEN_CHAR_LIT, "'Z'");
    ASSERT_TOKEN(s, TOKEN_EOF);
    printf("  PASS: test_char_literal\n");
}

static void test_char_escape(void) {
    Scanner s;
    scanner_init(&s, "'\\n'");
    ASSERT_TOKEN_VAL(s, TOKEN_CHAR_LIT, "'\\n'");
    ASSERT_TOKEN(s, TOKEN_EOF);
    printf("  PASS: test_char_escape\n");
}

/* ---- Operators ---- */

static void test_all_operators(void) {
    Scanner s;
    scanner_init(&s, "+ - * / % & | ^ ~ << >> && || ! == != < > <= >=");

    ASSERT_TOKEN(s, TOKEN_PLUS);
    ASSERT_TOKEN(s, TOKEN_MINUS);
    ASSERT_TOKEN(s, TOKEN_STAR);
    ASSERT_TOKEN(s, TOKEN_SLASH);
    ASSERT_TOKEN(s, TOKEN_PERCENT);
    ASSERT_TOKEN(s, TOKEN_AMP);
    ASSERT_TOKEN(s, TOKEN_PIPE);
    ASSERT_TOKEN(s, TOKEN_CARET);
    ASSERT_TOKEN(s, TOKEN_TILDE);
    ASSERT_TOKEN(s, TOKEN_LSHIFT);
    ASSERT_TOKEN(s, TOKEN_RSHIFT);
    ASSERT_TOKEN(s, TOKEN_AND);
    ASSERT_TOKEN(s, TOKEN_OR);
    ASSERT_TOKEN(s, TOKEN_BANG);
    ASSERT_TOKEN(s, TOKEN_EQ);
    ASSERT_TOKEN(s, TOKEN_NEQ);
    ASSERT_TOKEN(s, TOKEN_LT);
    ASSERT_TOKEN(s, TOKEN_GT);
    ASSERT_TOKEN(s, TOKEN_LEQ);
    ASSERT_TOKEN(s, TOKEN_GEQ);
    ASSERT_TOKEN(s, TOKEN_EOF);
    printf("  PASS: test_all_operators\n");
}

static void test_assignment_operators(void) {
    Scanner s;
    scanner_init(&s, "= += -= *= /=");

    ASSERT_TOKEN(s, TOKEN_ASSIGN);
    ASSERT_TOKEN(s, TOKEN_PLUS_ASSIGN);
    ASSERT_TOKEN(s, TOKEN_MINUS_ASSIGN);
    ASSERT_TOKEN(s, TOKEN_STAR_ASSIGN);
    ASSERT_TOKEN(s, TOKEN_SLASH_ASSIGN);
    ASSERT_TOKEN(s, TOKEN_EOF);
    printf("  PASS: test_assignment_operators\n");
}

/* ---- Delimiters ---- */

static void test_delimiters(void) {
    Scanner s;
    scanner_init(&s, "( ) { } [ ] ; : , . -> => _ .. ...");

    ASSERT_TOKEN(s, TOKEN_LPAREN);
    ASSERT_TOKEN(s, TOKEN_RPAREN);
    ASSERT_TOKEN(s, TOKEN_LBRACE);
    ASSERT_TOKEN(s, TOKEN_RBRACE);
    ASSERT_TOKEN(s, TOKEN_LBRACKET);
    ASSERT_TOKEN(s, TOKEN_RBRACKET);
    ASSERT_TOKEN(s, TOKEN_SEMICOLON);
    ASSERT_TOKEN(s, TOKEN_COLON);
    ASSERT_TOKEN(s, TOKEN_COMMA);
    ASSERT_TOKEN(s, TOKEN_DOT);
    ASSERT_TOKEN(s, TOKEN_ARROW);
    ASSERT_TOKEN(s, TOKEN_FAT_ARROW);
    ASSERT_TOKEN(s, TOKEN_UNDERSCORE);
    ASSERT_TOKEN(s, TOKEN_DOTDOT);
    ASSERT_TOKEN(s, TOKEN_ELLIPSIS);
    ASSERT_TOKEN(s, TOKEN_EOF);
    printf("  PASS: test_delimiters\n");
}

/* ---- Line/Column tracking ---- */

static void test_line_column(void) {
    Scanner s;
    scanner_init(&s, "int x\n  42");

    Token t1 = scanner_next(&s);
    tests_run++; if (t1.line == 1 && t1.column == 1) tests_passed++; else {
        fprintf(stderr, "FAIL: %s:%d: int at line=%d col=%d, expected 1:1\n",
                __FILE__, __LINE__, t1.line, t1.column); return;
    }

    Token t2 = scanner_next(&s);
    tests_run++; if (t2.line == 1 && t2.column == 5) tests_passed++; else {
        fprintf(stderr, "FAIL: %s:%d: x at line=%d col=%d, expected 1:5\n",
                __FILE__, __LINE__, t2.line, t2.column); return;
    }

    Token t3 = scanner_next(&s);
    tests_run++; if (t3.line == 2 && t3.column == 3) tests_passed++; else {
        fprintf(stderr, "FAIL: %s:%d: 42 at line=%d col=%d, expected 2:3\n",
                __FILE__, __LINE__, t3.line, t3.column); return;
    }

    printf("  PASS: test_line_column\n");
}

/* ---- Error token ---- */

static void test_unexpected_char(void) {
    Scanner s;
    scanner_init(&s, "@");
    ASSERT_TOKEN(s, TOKEN_ERROR);
    printf("  PASS: test_unexpected_char\n");
}

/* ---- Peek ---- */

static void test_peek(void) {
    Scanner s;
    scanner_init(&s, "let x");
    Token peeked = scanner_peek(&s);
    Token next = scanner_next(&s);
    tests_run++;
    if (peeked.type == next.type && peeked.line == next.line) tests_passed++;
    else {
        fprintf(stderr, "FAIL: %s:%d: peek and next mismatch\n", __FILE__, __LINE__);
        return;
    }
    printf("  PASS: test_peek\n");
}

/* ---- Keyword vs identifier boundary ---- */

static void test_keyword_prefix_not_keyword(void) {
    Scanner s;
    /* "letter" should be IDENTIFIER (not a keyword) */
    scanner_init(&s, "letter");
    ASSERT_TOKEN_VAL(s, TOKEN_IDENTIFIER, "letter");
    ASSERT_TOKEN(s, TOKEN_EOF);

    /* "format" starts with "for" but should be IDENTIFIER */
    scanner_init(&s, "format");
    ASSERT_TOKEN_VAL(s, TOKEN_IDENTIFIER, "format");
    ASSERT_TOKEN(s, TOKEN_EOF);

    /* "integer" starts with "int" */
    scanner_init(&s, "integer");
    ASSERT_TOKEN_VAL(s, TOKEN_IDENTIFIER, "integer");
    ASSERT_TOKEN(s, TOKEN_EOF);

    printf("  PASS: test_keyword_prefix_not_keyword\n");
}

/* ---- Compound token sequences ---- */

static void test_var_decl(void) {
    Scanner s;
    /* C-style type-first variable declaration: int x = 42; */
    scanner_init(&s, "int x = 42;");

    ASSERT_TOKEN(s, TOKEN_TYPE_INT);
    ASSERT_TOKEN_VAL(s, TOKEN_IDENTIFIER, "x");
    ASSERT_TOKEN(s, TOKEN_ASSIGN);
    ASSERT_TOKEN_VAL(s, TOKEN_INT_LIT, "42");
    ASSERT_TOKEN(s, TOKEN_SEMICOLON);
    ASSERT_TOKEN(s, TOKEN_EOF);
    printf("  PASS: test_var_decl\n");
}

static void test_function_decl(void) {
    Scanner s;
    /* C-style params: fn add(int a, int b) -> int { return a + b; } */
    scanner_init(&s, "def add(int a, int b) -> int { return a + b; }");

    ASSERT_TOKEN(s, TOKEN_FN);
    ASSERT_TOKEN_VAL(s, TOKEN_IDENTIFIER, "add");
    ASSERT_TOKEN(s, TOKEN_LPAREN);
    ASSERT_TOKEN(s, TOKEN_TYPE_INT);
    ASSERT_TOKEN_VAL(s, TOKEN_IDENTIFIER, "a");
    ASSERT_TOKEN(s, TOKEN_COMMA);
    ASSERT_TOKEN(s, TOKEN_TYPE_INT);
    ASSERT_TOKEN_VAL(s, TOKEN_IDENTIFIER, "b");
    ASSERT_TOKEN(s, TOKEN_RPAREN);
    ASSERT_TOKEN(s, TOKEN_ARROW);
    ASSERT_TOKEN(s, TOKEN_TYPE_INT);
    ASSERT_TOKEN(s, TOKEN_LBRACE);
    ASSERT_TOKEN(s, TOKEN_RETURN);
    ASSERT_TOKEN_VAL(s, TOKEN_IDENTIFIER, "a");
    ASSERT_TOKEN(s, TOKEN_PLUS);
    ASSERT_TOKEN_VAL(s, TOKEN_IDENTIFIER, "b");
    ASSERT_TOKEN(s, TOKEN_SEMICOLON);
    ASSERT_TOKEN(s, TOKEN_RBRACE);
    ASSERT_TOKEN(s, TOKEN_EOF);
    printf("  PASS: test_function_decl\n");
}

static void test_match_expression(void) {
    Scanner s;
    scanner_init(&s, "match n { 0 => 1, _ => n * 2, }");

    ASSERT_TOKEN(s, TOKEN_MATCH);
    ASSERT_TOKEN_VAL(s, TOKEN_IDENTIFIER, "n");
    ASSERT_TOKEN(s, TOKEN_LBRACE);
    ASSERT_TOKEN_VAL(s, TOKEN_INT_LIT, "0");
    ASSERT_TOKEN(s, TOKEN_FAT_ARROW);
    ASSERT_TOKEN_VAL(s, TOKEN_INT_LIT, "1");
    ASSERT_TOKEN(s, TOKEN_COMMA);
    ASSERT_TOKEN(s, TOKEN_UNDERSCORE);
    ASSERT_TOKEN(s, TOKEN_FAT_ARROW);
    ASSERT_TOKEN_VAL(s, TOKEN_IDENTIFIER, "n");
    ASSERT_TOKEN(s, TOKEN_STAR);
    ASSERT_TOKEN_VAL(s, TOKEN_INT_LIT, "2");
    ASSERT_TOKEN(s, TOKEN_COMMA);
    ASSERT_TOKEN(s, TOKEN_RBRACE);
    ASSERT_TOKEN(s, TOKEN_EOF);
    printf("  PASS: test_match_expression\n");
}

static void test_boolean_literals(void) {
    Scanner s;
    scanner_init(&s, "true false nil");
    ASSERT_TOKEN(s, TOKEN_TRUE);
    ASSERT_TOKEN(s, TOKEN_FALSE);
    ASSERT_TOKEN(s, TOKEN_NIL);
    ASSERT_TOKEN(s, TOKEN_EOF);
    printf("  PASS: test_boolean_literals\n");
}

/* ---- Main ---- */

static void test_at_sigil_intrinsics(void) {
    Scanner s;
    scanner_init(&s, "@take @dispose @dup @move");
    ASSERT_TOKEN_VAL(s, TOKEN_AT_INTRINSIC, "@take");
    ASSERT_TOKEN_VAL(s, TOKEN_AT_INTRINSIC, "@dispose");
    ASSERT_TOKEN_VAL(s, TOKEN_AT_INTRINSIC, "@dup");
    ASSERT_TOKEN_VAL(s, TOKEN_AT_INTRINSIC, "@move");
}

int main(void) {
    printf("=== Scanner Tests ===\n");

    test_empty_source();
    test_whitespace_only();
    test_single_line_comment();
    test_multi_line_comment();
    test_nested_comment();

    test_all_keywords();
    test_type_keywords();

    test_identifiers();
    test_underscore_wildcard();

    test_integer_literals();
    test_hex_literals();
    test_binary_literals();
    test_float_literals();
    test_invalid_hex();
    test_invalid_binary();

    test_string_literal();
    test_string_escapes();
    test_unterminated_string();
    test_unknown_escape();

    test_char_literal();
    test_char_escape();

    test_all_operators();
    test_assignment_operators();

    test_delimiters();

    test_line_column();
    test_unexpected_char();
    test_peek();

    test_keyword_prefix_not_keyword();
    test_var_decl();
    test_function_decl();
    test_match_expression();
    test_boolean_literals();
    test_at_sigil_intrinsics();

    printf("\n=== Results: %d/%d passed ===\n", tests_passed, tests_run);
    return (tests_passed == tests_run) ? 0 : 1;
}
