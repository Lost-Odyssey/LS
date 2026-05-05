/* parser.c — Pratt parser: token stream -> AST */
#include "parser.h"
#include "common.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>

/* ---- Helpers ---- */

/* str_dup_n: portable strndup replacement */
static char *str_dup_n(const char *s, int len) {
    char *r = (char *)malloc_safe((size_t)len + 1);
    memcpy(r, s, (size_t)len);
    r[len] = '\0';
    return r;
}

/* Allocate a new AstNode with zero-init */
static AstNode *new_node(AstNodeType kind, int line, int col) {
    AstNode *n = (AstNode *)malloc_safe(sizeof(AstNode));
    memset(n, 0, sizeof(AstNode));
    n->kind = kind;
    n->line = line;
    n->column = col;
    return n;
}

/* Allocate a new TypeNode with zero-init */
static TypeNode *new_type_node(TypeNodeKind kind, int line, int col) {
    TypeNode *t = (TypeNode *)malloc_safe(sizeof(TypeNode));
    memset(t, 0, sizeof(TypeNode));
    t->kind = kind;
    t->line = line;
    t->column = col;
    return t;
}

/* ---- Error Handling ---- */

static void error_at(Parser *p, Token *tok, const char *msg) {
    if (p->panic_mode) return;
    p->panic_mode = true;
    p->had_error = true;
    fprintf(stderr, "[error] %s:%d:%d: %s\n",
            p->source_path ? p->source_path : "<unknown>",
            tok->line, tok->column, msg);
}

static void error_at_current(Parser *p, const char *msg) {
    error_at(p, &p->current, msg);
}

static void error_at_previous(Parser *p, const char *msg) {
    error_at(p, &p->previous, msg);
}

/* Advance scanner: previous = current, current = next token */
static void advance(Parser *p) {
    p->previous = p->current;
    for (;;) {
        p->current = scanner_next(&p->scanner);
        if (p->current.type != TOKEN_ERROR) break;
        /* Print scanner error and keep going */
        fprintf(stderr, "[error] %s:%d:%d: %.*s\n",
                p->source_path ? p->source_path : "<unknown>",
                p->current.line, p->current.column,
                p->current.length, p->current.start);
        p->had_error = true;
    }
}

/* Consume current if it matches type, else error */
static bool consume(Parser *p, TokenType type, const char *msg) {
    if (p->current.type == type) {
        advance(p);
        return true;
    }
    error_at_current(p, msg);
    return false;
}

/* Check current token type without consuming */
static bool check(Parser *p, TokenType type) {
    return p->current.type == type;
}

/* Consume if matches, return true if matched */
static bool match_tok(Parser *p, TokenType type) {
    if (!check(p, type)) return false;
    advance(p);
    return true;
}

/* Skip any semicolons (optional statement terminator) */
static void skip_semicolons(Parser *p) {
    while (p->current.type == TOKEN_SEMICOLON) {
        advance(p);
    }
}

/* Synchronize after error: advance to next statement boundary */
static void synchronize(Parser *p) {
    p->panic_mode = false;
    while (p->current.type != TOKEN_EOF) {
        if (p->previous.type == TOKEN_SEMICOLON) return;
        switch (p->current.type) {
        case TOKEN_FN:
        case TOKEN_STRUCT:
        case TOKEN_ENUM:
        case TOKEN_IMPL:
        case TOKEN_IF:
        case TOKEN_WHILE:
        case TOKEN_FOR:
        case TOKEN_RETURN:
        case TOKEN_MODULE:
        case TOKEN_IMPORT:
        case TOKEN_EXTERN:
        case TOKEN_RBRACE:
            return;
        default:
            break;
        }
        advance(p);
    }
}

/* ---- String Literal Processing ---- */

/* Process escape sequences in a string token (strips quotes) */
static char *process_string_token(const char *start, int length) {
    /* length includes surrounding quotes */
    /* result buffer: at most (length - 2) chars + NUL */
    int max_len = length - 2;
    if (max_len < 0) max_len = 0;
    char *result = (char *)malloc_safe((size_t)max_len + 1);
    int out = 0;
    int i = 1; /* skip opening quote */
    int end = length - 1; /* stop before closing quote */
    while (i < end) {
        if (start[i] == '\\' && i + 1 < end) {
            i++;
            switch (start[i]) {
            case 'n':  result[out++] = '\n'; break;
            case 't':  result[out++] = '\t'; break;
            case 'r':  result[out++] = '\r'; break;
            case '\\': result[out++] = '\\'; break;
            case '"':  result[out++] = '"';  break;
            case '0':  result[out++] = '\0'; break;
            case 'a':  result[out++] = '\a'; break;
            case 'b':  result[out++] = '\b'; break;
            case 'x':
                if (i + 2 < end) {
                    char hex[3];
                    hex[0] = start[i + 1];
                    hex[1] = start[i + 2];
                    hex[2] = '\0';
                    result[out++] = (char)strtol(hex, NULL, 16);
                    i += 2;
                }
                break;
            default:
                result[out++] = start[i];
                break;
            }
        } else {
            result[out++] = start[i];
        }
        i++;
    }
    result[out] = '\0';
    return result;
}

/* ---- Forward declarations ---- */

typedef AstNode *(*PrefixFn)(Parser *p);
typedef AstNode *(*InfixFn)(Parser *p, AstNode *left);

typedef enum {
    PREC_NONE = 0,
    PREC_ASSIGNMENT,  /* = += -= *= /= (right-assoc) */
    PREC_OR,          /* || */
    PREC_AND,         /* && */
    PREC_EQUALITY,    /* == != */
    PREC_COMPARISON,  /* < > <= >= */
    PREC_BITOR,       /* | */
    PREC_BITXOR,      /* ^ */
    PREC_BITAND,      /* & */
    PREC_SHIFT,       /* << >> */
    PREC_TERM,        /* + - */
    PREC_FACTOR,      /* * / % */
    PREC_UNARY,       /* ! - ~ * & (prefix) */
    PREC_CALL,        /* . () [] as */
    PREC_PRIMARY,
} Precedence;

typedef struct {
    PrefixFn prefix;
    InfixFn  infix;
    Precedence precedence;
} ParseRule;

static AstNode *parse_expr_prec(Parser *p, Precedence min_prec);
static AstNode *parse_statement(Parser *p);
static AstNode *parse_block(Parser *p);
static TypeNode *parse_type(Parser *p);
static bool is_type_keyword(TokenType t);

/* ---- Prefix parse functions ---- */

static AstNode *prefix_int_lit(Parser *p) {
    Token tok = p->previous;
    long long val;
    char buf[64];
    int len = tok.length < 63 ? tok.length : 63;
    memcpy(buf, tok.start, (size_t)len);
    buf[len] = '\0';
    if (len > 2 && buf[0] == '0' && (buf[1] == 'x' || buf[1] == 'X')) {
        val = (long long)strtoll(buf + 2, NULL, 16);
    } else if (len > 2 && buf[0] == '0' && (buf[1] == 'b' || buf[1] == 'B')) {
        val = (long long)strtoll(buf + 2, NULL, 2);
    } else {
        val = (long long)strtoll(buf, NULL, 10);
    }
    AstNode *n = new_node(AST_INT_LIT, tok.line, tok.column);
    n->as.int_lit.value = val;
    return n;
}

static AstNode *prefix_float_lit(Parser *p) {
    Token tok = p->previous;
    char buf[64];
    int len = tok.length < 63 ? tok.length : 63;
    memcpy(buf, tok.start, (size_t)len);
    buf[len] = '\0';
    double val = strtod(buf, NULL);
    AstNode *n = new_node(AST_FLOAT_LIT, tok.line, tok.column);
    n->as.float_lit.value = val;
    return n;
}

static AstNode *prefix_string_lit(Parser *p) {
    Token tok = p->previous;
    char *val = process_string_token(tok.start, tok.length);
    AstNode *n = new_node(AST_STRING_LIT, tok.line, tok.column);
    n->as.string_lit.value = val;
    n->as.string_lit.length = (int)strlen(val);
    return n;
}

/* Process escape sequences in an f-string text segment (no surrounding quotes) */
static char *process_fstring_text(const char *start, int length) {
    char *result = (char *)malloc_safe((size_t)length + 1);
    int out = 0;
    for (int i = 0; i < length; i++) {
        if (start[i] == '\\' && i + 1 < length) {
            i++;
            switch (start[i]) {
            case 'n':  result[out++] = '\n'; break;
            case 't':  result[out++] = '\t'; break;
            case 'r':  result[out++] = '\r'; break;
            case '\\': result[out++] = '\\'; break;
            case '"':  result[out++] = '"';  break;
            case '{':  result[out++] = '{';  break;
            case '}':  result[out++] = '}';  break;
            case '0':  result[out++] = '\0'; break;
            default:   result[out++] = start[i]; break;
            }
        } else {
            result[out++] = start[i];
        }
    }
    result[out] = '\0';
    return result;
}

static AstNode *prefix_fstring(Parser *p) {
    /* TOKEN_FSTRING_START already consumed */
    Token start_tok = p->previous;

    /* Collect text parts and expressions */
    int parts_cap = 4, exprs_cap = 4;
    char **parts = (char **)malloc_safe((size_t)parts_cap * sizeof(char *));
    AstNode **exprs = (AstNode **)malloc_safe((size_t)exprs_cap * sizeof(AstNode *));
    int part_count = 0, expr_count = 0;

    for (;;) {
        if (check(p, TOKEN_FSTRING_TEXT)) {
            advance(p);
            Token text = p->previous;
            char *txt = process_fstring_text(text.start, text.length);
            if (part_count >= parts_cap) {
                parts_cap = GROW_CAPACITY(parts_cap);
                parts = realloc_safe(parts, (size_t)parts_cap * sizeof(char *));
            }
            parts[part_count++] = txt;
        } else if (check(p, TOKEN_LBRACE)) {
            /* Ensure there's a text part before each expression */
            if (part_count == expr_count) {
                if (part_count >= parts_cap) {
                    parts_cap = GROW_CAPACITY(parts_cap);
                    parts = realloc_safe(parts, (size_t)parts_cap * sizeof(char *));
                }
                parts[part_count++] = str_dup_n("", 0);
            }
            advance(p); /* consume '{' */
            AstNode *expr = parse_expr_prec(p, PREC_NONE);
            if (expr == NULL) {
                error_at_current(p, "expected expression inside f-string interpolation");
                break;
            }
            consume(p, TOKEN_RBRACE, "expected '}' after f-string expression");
            if (expr_count >= exprs_cap) {
                exprs_cap = GROW_CAPACITY(exprs_cap);
                exprs = realloc_safe(exprs, (size_t)exprs_cap * sizeof(AstNode *));
            }
            exprs[expr_count++] = expr;
        } else if (check(p, TOKEN_FSTRING_END)) {
            advance(p);
            break;
        } else if (check(p, TOKEN_EOF)) {
            error_at_current(p, "unterminated format string");
            break;
        } else {
            error_at_current(p, "unexpected token in format string");
            break;
        }
    }

    /* Ensure trailing text part (may be empty) */
    if (part_count == expr_count) {
        if (part_count >= parts_cap) {
            parts_cap = GROW_CAPACITY(parts_cap);
            parts = realloc_safe(parts, (size_t)parts_cap * sizeof(char *));
        }
        parts[part_count++] = str_dup_n("", 0);
    }

    AstNode *n = new_node(AST_FORMAT_STRING, start_tok.line, start_tok.column);
    n->as.format_string.parts = parts;
    n->as.format_string.exprs = exprs;
    n->as.format_string.part_count = part_count;
    n->as.format_string.expr_count = expr_count;
    return n;
}

static AstNode *prefix_char_lit(Parser *p) {
    Token tok = p->previous;
    long long val = 0;
    if (tok.length >= 3) {
        if (tok.start[1] == '\\' && tok.length >= 4) {
            switch (tok.start[2]) {
            case 'n':  val = '\n'; break;
            case 't':  val = '\t'; break;
            case 'r':  val = '\r'; break;
            case '\\': val = '\\'; break;
            case '\'': val = '\''; break;
            case '0':  val = '\0'; break;
            default:   val = tok.start[2]; break;
            }
        } else {
            val = (unsigned char)tok.start[1];
        }
    }
    AstNode *n = new_node(AST_INT_LIT, tok.line, tok.column);
    n->as.int_lit.value = val;
    n->as.int_lit.is_char = true;
    return n;
}

static AstNode *prefix_true(Parser *p) {
    Token tok = p->previous;
    AstNode *n = new_node(AST_BOOL_LIT, tok.line, tok.column);
    n->as.bool_lit.value = true;
    return n;
}

static AstNode *prefix_false(Parser *p) {
    Token tok = p->previous;
    AstNode *n = new_node(AST_BOOL_LIT, tok.line, tok.column);
    n->as.bool_lit.value = false;
    return n;
}

static AstNode *prefix_nil(Parser *p) {
    Token tok = p->previous;
    return new_node(AST_NIL_LIT, tok.line, tok.column);
}

/* new StructName or new StructName { field: expr, ... } */
static AstNode *prefix_new_expr(Parser *p) {
    Token tok = p->previous; /* TOKEN_NEW */
    consume(p, TOKEN_IDENTIFIER, "expected struct name after 'new'");
    Token name_tok = p->previous;

    AstNode *n = new_node(AST_NEW_EXPR, tok.line, tok.column);
    n->as.new_expr.struct_name = str_dup_n(name_tok.start, name_tok.length);
    n->as.new_expr.field_inits = NULL;
    n->as.new_expr.field_init_count = 0;

    if (check(p, TOKEN_LBRACE)) {
        advance(p); /* consume '{' */
        int cap = 0;
        int count = 0;
        struct { char *name; AstNode *value; } *inits = NULL;

        while (!check(p, TOKEN_RBRACE) && !check(p, TOKEN_EOF)) {
            consume(p, TOKEN_IDENTIFIER, "expected field name in struct initializer");
            Token fname = p->previous;
            consume(p, TOKEN_COLON, "expected ':' after field name in struct initializer");
            AstNode *val = parse_expr_prec(p, PREC_ASSIGNMENT);

            if (count >= cap) {
                cap = GROW_CAPACITY(cap);
                inits = realloc_safe(inits,
                    (size_t)cap * sizeof(inits[0]));
            }
            inits[count].name = str_dup_n(fname.start, fname.length);
            inits[count].value = val;
            count++;

            if (!match_tok(p, TOKEN_COMMA)) break;
        }
        consume(p, TOKEN_RBRACE, "expected '}' after struct field initializers");
        n->as.new_expr.field_inits = (void *)inits;
        n->as.new_expr.field_init_count = count;
    }

    return n;
}

static AstNode *prefix_ident(Parser *p) {
    Token tok = p->previous;

    /* Detect StructName{field: val, ...} — struct value literal (stack-allocated).
       Use 2-token lookahead: peek past '{' to see if it looks like a struct initializer.
       Heuristic: '{' followed by '}' (empty) or 'IDENT :' is a struct literal.
       Save/restore scanner state to avoid consuming tokens. */
    if (check(p, TOKEN_LBRACE)) {
        Scanner saved_scanner = p->scanner;
        Token saved_current   = p->current;
        advance(p);                         /* consume '{' into p->previous */
        bool is_struct_lit =
            check(p, TOKEN_RBRACE) ||       /* empty: S1{} */
            (check(p, TOKEN_IDENTIFIER) &&
             scanner_peek(&p->scanner).type == TOKEN_COLON);  /* S1{field: ...} */
        /* Restore state */
        p->scanner = saved_scanner;
        p->current = saved_current;

        if (is_struct_lit) {
            AstNode *n = new_node(AST_NEW_EXPR, tok.line, tok.column);
            n->as.new_expr.struct_name = str_dup_n(tok.start, tok.length);
            n->as.new_expr.field_inits = NULL;
            n->as.new_expr.field_init_count = 0;
            n->as.new_expr.on_stack = true;

            advance(p); /* consume '{' */
            int cap = 0, count = 0;
            struct { char *name; AstNode *value; } *inits = NULL;
            while (!check(p, TOKEN_RBRACE) && !check(p, TOKEN_EOF)) {
                consume(p, TOKEN_IDENTIFIER, "expected field name in struct literal");
                Token fname = p->previous;
                consume(p, TOKEN_COLON, "expected ':' after field name in struct literal");
                AstNode *val = parse_expr_prec(p, PREC_ASSIGNMENT);
                if (count >= cap) {
                    cap = GROW_CAPACITY(cap);
                    inits = realloc_safe(inits, (size_t)cap * sizeof(inits[0]));
                }
                inits[count].name  = str_dup_n(fname.start, fname.length);
                inits[count].value = val;
                count++;
                if (!match_tok(p, TOKEN_COMMA)) break;
            }
            consume(p, TOKEN_RBRACE, "expected '}' after struct literal fields");
            n->as.new_expr.field_inits = (void *)inits;
            n->as.new_expr.field_init_count = count;
            return n;
        }
    }

    AstNode *n = new_node(AST_IDENT, tok.line, tok.column);
    n->as.ident.name = str_dup_n(tok.start, tok.length);
    return n;
}

static AstNode *prefix_underscore(Parser *p) {
    /* _ is a wildcard in match patterns — treat as identifier "_" */
    Token tok = p->previous;
    AstNode *n = new_node(AST_IDENT, tok.line, tok.column);
    n->as.ident.name = str_dup_n("_", 1);
    return n;
}

static AstNode *prefix_grouping(Parser *p) {
    AstNode *expr = parse_expr_prec(p, PREC_NONE);
    consume(p, TOKEN_RPAREN, "expected ')' after grouped expression");
    return expr;
}

static AstNode *prefix_unary(Parser *p) {
    Token op = p->previous;
    AstNode *operand = parse_expr_prec(p, PREC_UNARY);
    AstNode *n = new_node(AST_UNARY, op.line, op.column);
    n->as.unary.op = op.type;
    n->as.unary.operand = operand;
    return n;
}

/* Prefix * for pointer dereference */
static AstNode *prefix_deref(Parser *p) {
    Token op = p->previous;
    AstNode *operand = parse_expr_prec(p, PREC_UNARY);
    AstNode *n = new_node(AST_UNARY, op.line, op.column);
    n->as.unary.op = TOKEN_STAR;
    n->as.unary.operand = operand;
    return n;
}

/* Prefix & for address-of, or &! for explicit writable-borrow */
static AstNode *prefix_addr(Parser *p) {
    Token op = p->previous;
    /* &!ident — explicit mutable borrow at call site. Wraps the operand in
       AST_MUT_BORROW; the checker later verifies the operand is an IDENT
       referring to a movable (owned) variable. */
    if (match_tok(p, TOKEN_BANG)) {
        AstNode *operand = parse_expr_prec(p, PREC_UNARY);
        AstNode *n = new_node(AST_MUT_BORROW, op.line, op.column);
        n->as.mut_borrow.operand = operand;
        return n;
    }
    AstNode *operand = parse_expr_prec(p, PREC_UNARY);
    AstNode *n = new_node(AST_UNARY, op.line, op.column);
    n->as.unary.op = TOKEN_AMP;
    n->as.unary.operand = operand;
    return n;
}

/* try expr — Zig-style early return for Result/Option */
static AstNode *prefix_try(Parser *p) {
    Token tok = p->previous; /* TOKEN_TRY */
    AstNode *operand = parse_expr_prec(p, PREC_UNARY);
    AstNode *n = new_node(AST_TRY, tok.line, tok.column);
    n->as.try_expr.expr = operand;
    return n;
}

/* Closure: fn(params) -> ret { body } */
static AstNode *prefix_closure(Parser *p) {
    Token fn_tok = p->previous;
    consume(p, TOKEN_LPAREN, "expected '(' after 'fn' in closure");

    TypeNode **param_types = NULL;
    char **param_names = NULL;
    int param_count = 0;
    int param_cap = 0;

    if (!check(p, TOKEN_RPAREN)) {
        do {
            TypeNode *pt = parse_type(p);
            if (pt == NULL) break;
            char *pname = NULL;
            /* Accept identifier or 'self' as parameter name */
            if (check(p, TOKEN_IDENTIFIER) || check(p, TOKEN_SELF)) {
                advance(p);
                pname = str_dup_n(p->previous.start, p->previous.length);
            } else if (p->current.type >= TOKEN_FN && p->current.type <= TOKEN_TRY) {
                /* Reserved keyword misused as parameter name — emit helpful error */
                char buf[160];
                snprintf(buf, sizeof(buf),
                         "cannot use reserved keyword '%.*s' as parameter name; rename it (e.g. '%.*s_')",
                         p->current.length, p->current.start,
                         p->current.length, p->current.start);
                error_at_current(p, buf);
                advance(p); /* consume the keyword to avoid cascading errors */
                pname = str_dup_n("_", 1);
            } else {
                pname = str_dup_n("_", 1);
            }
            if (param_count >= param_cap) {
                param_cap = GROW_CAPACITY(param_cap);
                param_types = GROW_ARRAY(TypeNode *, param_types, param_cap);
                param_names = GROW_ARRAY(char *, param_names, param_cap);
            }
            param_types[param_count] = pt;
            param_names[param_count] = pname;
            param_count++;
        } while (match_tok(p, TOKEN_COMMA));
    }
    consume(p, TOKEN_RPAREN, "expected ')' after closure parameters");

    TypeNode *return_type = NULL;
    if (match_tok(p, TOKEN_ARROW)) {
        return_type = parse_type(p);
    }

    AstNode *body = parse_block(p);

    AstNode *n = new_node(AST_CLOSURE, fn_tok.line, fn_tok.column);
    n->as.closure.param_types = param_types;
    n->as.closure.param_names = param_names;
    n->as.closure.param_count = param_count;
    n->as.closure.return_type = return_type;
    n->as.closure.body = body;
    return n;
}

/* match expr { pattern => body, ... } */
static AstNode *prefix_match(Parser *p) {
    Token match_start = p->previous;
    AstNode *subject = parse_expr_prec(p, PREC_NONE);
    consume(p, TOKEN_LBRACE, "expected '{' after match subject");

    MatchArm *arms = NULL;
    int arm_count = 0;
    int arm_cap = 0;

    while (!check(p, TOKEN_RBRACE) && !check(p, TOKEN_EOF)) {
        AstNode *pattern = parse_expr_prec(p, PREC_NONE);
        consume(p, TOKEN_FAT_ARROW, "expected '=>' after match pattern");
        AstNode *body;
        if (check(p, TOKEN_LBRACE)) {
            body = parse_block(p);
        } else {
            body = parse_expr_prec(p, PREC_NONE);
        }
        /* optional comma */
        match_tok(p, TOKEN_COMMA);

        if (arm_count >= arm_cap) {
            arm_cap = GROW_CAPACITY(arm_cap);
            arms = GROW_ARRAY(MatchArm, arms, arm_cap);
        }
        arms[arm_count].pattern = pattern;
        arms[arm_count].body = body;
        arm_count++;
    }
    consume(p, TOKEN_RBRACE, "expected '}' after match arms");

    AstNode *n = new_node(AST_MATCH, match_start.line, match_start.column);
    n->as.match.subject = subject;
    n->as.match.arms = arms;
    n->as.match.arm_count = arm_count;
    return n;
}

/* :name -> treat as a string literal containing "name" */
static AstNode *prefix_symbol(Parser *p) {
    Token colon_tok = p->previous;
    if (check(p, TOKEN_IDENTIFIER)) {
        advance(p);
        Token name_tok = p->previous;
        AstNode *n = new_node(AST_STRING_LIT, colon_tok.line, colon_tok.column);
        n->as.string_lit.value = str_dup_n(name_tok.start, name_tok.length);
        n->as.string_lit.length = name_tok.length;
        return n;
    }
    error_at_current(p, "expected identifier after ':'");
    return NULL;
}

/* ---- Infix parse functions ---- */

static const ParseRule *get_rule(TokenType type);

static AstNode *infix_binary_real(Parser *p, AstNode *left) {
    Token op = p->previous;
    const ParseRule *rule = get_rule(op.type);
    /* left-associative: parse right at same precedence so higher-prec operators bind tighter */
    AstNode *right = parse_expr_prec(p, rule->precedence);
    if (right == NULL) {
        ast_free(left);
        return NULL;
    }
    AstNode *n = new_node(AST_BINARY, op.line, op.column);
    n->as.binary.op = op.type;
    n->as.binary.left = left;
    n->as.binary.right = right;
    return n;
}

/* Range expression: a..b — creates AST_RANGE */
static AstNode *infix_range(Parser *p, AstNode *left) {
    Token op = p->previous;
    AstNode *right = parse_expr_prec(p, PREC_COMPARISON);
    if (right == NULL) {
        ast_free(left);
        return NULL;
    }
    AstNode *n = new_node(AST_RANGE, op.line, op.column);
    n->as.range.start = left;
    n->as.range.end = right;
    return n;
}

/* Assignment: right-associative, creates AST_ASSIGN */
static AstNode *infix_assign(Parser *p, AstNode *left) {
    Token op = p->previous;
    /* right-associative: parse right at PREC_NONE */
    AstNode *right = parse_expr_prec(p, PREC_NONE);
    if (right == NULL) {
        ast_free(left);
        return NULL;
    }
    AstNode *n = new_node(AST_ASSIGN, op.line, op.column);
    n->as.assign.target = left;
    n->as.assign.op = op.type;
    n->as.assign.value = right;
    return n;
}

/* Function call: left(args...) */
static AstNode *infix_call(Parser *p, AstNode *left) {
    Token call_tok = p->previous; /* the '(' */
    AstNode **args = NULL;
    int arg_count = 0;
    int arg_cap = 0;

    if (!check(p, TOKEN_RPAREN)) {
        do {
            AstNode *arg = parse_expr_prec(p, PREC_NONE);
            if (arg == NULL) break;
            if (arg_count >= arg_cap) {
                arg_cap = GROW_CAPACITY(arg_cap);
                args = GROW_ARRAY(AstNode *, args, arg_cap);
            }
            args[arg_count++] = arg;
        } while (match_tok(p, TOKEN_COMMA));
    }
    consume(p, TOKEN_RPAREN, "expected ')' after arguments");

    AstNode *n = new_node(AST_CALL, call_tok.line, call_tok.column);
    n->as.call.callee = left;
    n->as.call.args = args;
    n->as.call.arg_count = arg_count;
    return n;
}

/* Map literal: { key -> val, key -> val, ... }
   An empty map literal {} is also allowed (type must be declared). */
static AstNode *prefix_map_lit(Parser *p) {
    Token tok = p->previous; /* the '{' token */
    AstNode **keys = NULL;
    AstNode **vals = NULL;
    int count = 0;
    int cap   = 0;

    if (!check(p, TOKEN_RBRACE)) {
        do {
            /* Skip optional trailing comma before '}' */
            if (check(p, TOKEN_RBRACE)) break;

            AstNode *key = parse_expr_prec(p, PREC_NONE);
            if (key == NULL) break;
            consume(p, TOKEN_ARROW, "expected '->' between key and value in map literal");
            AstNode *val = parse_expr_prec(p, PREC_NONE);
            if (val == NULL) { ast_free(key); break; }

            if (count >= cap) {
                int old_cap = cap;
                cap = GROW_CAPACITY(cap);
                keys = GROW_ARRAY(AstNode *, keys, cap);
                vals = GROW_ARRAY(AstNode *, vals, cap);
                (void)old_cap;
            }
            keys[count] = key;
            vals[count] = val;
            count++;
        } while (match_tok(p, TOKEN_COMMA));
    }
    consume(p, TOKEN_RBRACE, "expected '}' to close map literal");

    AstNode *n = new_node(AST_MAP_LIT, tok.line, tok.column);
    n->as.map_lit.keys       = keys;
    n->as.map_lit.vals       = vals;
    n->as.map_lit.pair_count = count;
    return n;
}

/* Array literal: [expr, expr, ...] */
static AstNode *prefix_array_lit(Parser *p) {
    Token tok = p->previous; /* the '[' token */
    AstNode **elements = NULL;
    int count = 0;
    int cap = 0;

    if (!check(p, TOKEN_RBRACKET)) {
        do {
            AstNode *elem = parse_expr_prec(p, PREC_NONE);
            if (elem == NULL) break;
            if (count >= cap) {
                cap = GROW_CAPACITY(cap);
                elements = GROW_ARRAY(AstNode *, elements, cap);
            }
            elements[count++] = elem;
        } while (match_tok(p, TOKEN_COMMA));
    }
    consume(p, TOKEN_RBRACKET, "expected ']' after array literal");

    AstNode *n = new_node(AST_ARRAY_LIT, tok.line, tok.column);
    n->as.array_lit.elements = elements;
    n->as.array_lit.count = count;
    return n;
}

/* Index: left[index] */
static AstNode *infix_index(Parser *p, AstNode *left) {
    Token tok = p->previous;
    AstNode *index = parse_expr_prec(p, PREC_NONE);
    consume(p, TOKEN_RBRACKET, "expected ']' after index");
    AstNode *n = new_node(AST_INDEX, tok.line, tok.column);
    n->as.index_expr.object = left;
    n->as.index_expr.index = index;
    return n;
}

/* Field access: left.field or lib.call(:fn, ...) */
static AstNode *infix_field(Parser *p, AstNode *left) {
    Token dot_tok = p->previous;
    /* Field names can be identifiers or 'self', or other keywords used as names */
    if (!check(p, TOKEN_IDENTIFIER) && !check(p, TOKEN_SELF)) {
        error_at_current(p, "expected field name after '.'");
        ast_free(left);
        return NULL;
    }
    advance(p);
    Token field_tok = p->previous;
    char *field_name = str_dup_n(field_tok.start, field_tok.length);

    /* Check if this is lib.call(:fn, args...) */
    if (strcmp(field_name, "call") == 0 && check(p, TOKEN_LPAREN)) {
        advance(p); /* consume '(' */
        /* parse :symbol as first arg */
        AstNode **args = NULL;
        int arg_count = 0;
        int arg_cap = 0;

        if (!check(p, TOKEN_RPAREN)) {
            do {
                AstNode *arg = parse_expr_prec(p, PREC_NONE);
                if (arg == NULL) break;
                if (arg_count >= arg_cap) {
                    arg_cap = GROW_CAPACITY(arg_cap);
                    args = GROW_ARRAY(AstNode *, args, arg_cap);
                }
                args[arg_count++] = arg;
            } while (match_tok(p, TOKEN_COMMA));
        }
        consume(p, TOKEN_RPAREN, "expected ')' after ffi call arguments");

        /* first arg should be the symbol (fn name) */
        char *fn_name = NULL;
        if (arg_count > 0 && args[0]->kind == AST_STRING_LIT) {
            fn_name = str_dup_n(args[0]->as.string_lit.value,
                                (int)strlen(args[0]->as.string_lit.value));
            ast_free(args[0]);
            /* shift args down */
            for (int i = 0; i < arg_count - 1; i++) {
                args[i] = args[i + 1];
            }
            arg_count--;
        } else {
            fn_name = str_dup_n("?", 1);
        }

        AstNode *n = new_node(AST_FFI_CALL, dot_tok.line, dot_tok.column);
        n->as.ffi_call.lib_expr = left;
        n->as.ffi_call.fn_name = fn_name;
        n->as.ffi_call.args = args;
        n->as.ffi_call.arg_count = arg_count;
        free(field_name);
        return n;
    }

    AstNode *n = new_node(AST_FIELD, dot_tok.line, dot_tok.column);
    n->as.field_access.object = left;
    n->as.field_access.field = field_name;
    return n;
}

/* Cast: expr as type */
static AstNode *infix_cast(Parser *p, AstNode *left) {
    Token as_tok = p->previous;
    TypeNode *target = parse_type(p);
    AstNode *n = new_node(AST_CAST, as_tok.line, as_tok.column);
    n->as.cast.expr = left;
    n->as.cast.target_type = target;
    return n;
}

/* ---- ParseRule Table ---- */

static ParseRule rules[TOKEN_ERROR + 1];
static bool rules_initialized = false;

static void init_parse_rules(void) {
    if (rules_initialized) return;
    rules_initialized = true;

    /* Default: all NULL, PREC_NONE */
    memset(rules, 0, sizeof(rules));

    /* Literals */
    rules[TOKEN_INT_LIT]    = (ParseRule){ prefix_int_lit,   NULL,              PREC_NONE };
    rules[TOKEN_FLOAT_LIT]  = (ParseRule){ prefix_float_lit, NULL,              PREC_NONE };
    rules[TOKEN_STRING_LIT] = (ParseRule){ prefix_string_lit,NULL,              PREC_NONE };
    rules[TOKEN_FSTRING_START]= (ParseRule){ prefix_fstring,   NULL,              PREC_NONE };
    rules[TOKEN_CHAR_LIT]   = (ParseRule){ prefix_char_lit,  NULL,              PREC_NONE };
    rules[TOKEN_TRUE]       = (ParseRule){ prefix_true,      NULL,              PREC_NONE };
    rules[TOKEN_FALSE]      = (ParseRule){ prefix_false,     NULL,              PREC_NONE };
    rules[TOKEN_NIL]        = (ParseRule){ prefix_nil,       NULL,              PREC_NONE };

    /* Identifier, self, and underscore */
    rules[TOKEN_IDENTIFIER] = (ParseRule){ prefix_ident,     NULL,              PREC_NONE };
    rules[TOKEN_SELF]       = (ParseRule){ prefix_ident,     NULL,              PREC_NONE };
    rules[TOKEN_UNDERSCORE] = (ParseRule){ prefix_underscore,NULL,              PREC_NONE };

    /* Grouping */
    rules[TOKEN_LPAREN]     = (ParseRule){ prefix_grouping,  infix_call,        PREC_CALL };
    rules[TOKEN_LBRACKET]   = (ParseRule){ prefix_array_lit,  infix_index,       PREC_CALL };
    rules[TOKEN_LBRACE]     = (ParseRule){ prefix_map_lit,    NULL,              PREC_NONE };
    rules[TOKEN_DOT]        = (ParseRule){ NULL,             infix_field,       PREC_CALL };
    rules[TOKEN_AS]         = (ParseRule){ NULL,             infix_cast,        PREC_CALL };

    /* Arithmetic */
    rules[TOKEN_PLUS]       = (ParseRule){ NULL,             infix_binary_real, PREC_TERM };
    rules[TOKEN_MINUS]      = (ParseRule){ prefix_unary,     infix_binary_real, PREC_TERM };
    rules[TOKEN_STAR]       = (ParseRule){ prefix_deref,     infix_binary_real, PREC_FACTOR };
    rules[TOKEN_SLASH]      = (ParseRule){ NULL,             infix_binary_real, PREC_FACTOR };
    rules[TOKEN_PERCENT]    = (ParseRule){ NULL,             infix_binary_real, PREC_FACTOR };

    /* Unary-only */
    rules[TOKEN_BANG]       = (ParseRule){ prefix_unary,     NULL,              PREC_NONE };
    rules[TOKEN_TILDE]      = (ParseRule){ prefix_unary,     NULL,              PREC_NONE };
    rules[TOKEN_AMP]        = (ParseRule){ prefix_addr,      infix_binary_real, PREC_BITAND };

    /* Bitwise */
    rules[TOKEN_PIPE]       = (ParseRule){ NULL,             infix_binary_real, PREC_BITOR };
    rules[TOKEN_CARET]      = (ParseRule){ NULL,             infix_binary_real, PREC_BITXOR };
    rules[TOKEN_LSHIFT]     = (ParseRule){ NULL,             infix_binary_real, PREC_SHIFT };
    rules[TOKEN_RSHIFT]     = (ParseRule){ NULL,             infix_binary_real, PREC_SHIFT };

    /* Logical */
    rules[TOKEN_AND]        = (ParseRule){ NULL,             infix_binary_real, PREC_AND };
    rules[TOKEN_OR]         = (ParseRule){ NULL,             infix_binary_real, PREC_OR };

    /* Comparison */
    rules[TOKEN_DOTDOT]     = (ParseRule){ NULL,             infix_range,       PREC_COMPARISON };
    rules[TOKEN_EQ]         = (ParseRule){ NULL,             infix_binary_real, PREC_EQUALITY };
    rules[TOKEN_NEQ]        = (ParseRule){ NULL,             infix_binary_real, PREC_EQUALITY };
    rules[TOKEN_LT]         = (ParseRule){ NULL,             infix_binary_real, PREC_COMPARISON };
    rules[TOKEN_GT]         = (ParseRule){ NULL,             infix_binary_real, PREC_COMPARISON };
    rules[TOKEN_LEQ]        = (ParseRule){ NULL,             infix_binary_real, PREC_COMPARISON };
    rules[TOKEN_GEQ]        = (ParseRule){ NULL,             infix_binary_real, PREC_COMPARISON };

    /* Assignment (right-assoc) */
    rules[TOKEN_ASSIGN]       = (ParseRule){ NULL, infix_assign, PREC_ASSIGNMENT };
    rules[TOKEN_PLUS_ASSIGN]  = (ParseRule){ NULL, infix_assign, PREC_ASSIGNMENT };
    rules[TOKEN_MINUS_ASSIGN] = (ParseRule){ NULL, infix_assign, PREC_ASSIGNMENT };
    rules[TOKEN_STAR_ASSIGN]  = (ParseRule){ NULL, infix_assign, PREC_ASSIGNMENT };
    rules[TOKEN_SLASH_ASSIGN] = (ParseRule){ NULL, infix_assign, PREC_ASSIGNMENT };

    /* Keywords as prefix */
    rules[TOKEN_FN]         = (ParseRule){ prefix_closure,   NULL,              PREC_NONE };
    rules[TOKEN_MATCH]      = (ParseRule){ prefix_match,     NULL,              PREC_NONE };
    rules[TOKEN_COLON]      = (ParseRule){ prefix_symbol,    NULL,              PREC_NONE };
    rules[TOKEN_NEW]        = (ParseRule){ prefix_new_expr,  NULL,              PREC_NONE };
    rules[TOKEN_TRY]        = (ParseRule){ prefix_try,       NULL,              PREC_NONE };
}

static const ParseRule *get_rule(TokenType type) {
    init_parse_rules();
    if ((int)type < 0 || (int)type > (int)TOKEN_ERROR) return &rules[TOKEN_ERROR];
    return &rules[type];
}

/* ---- Core expression parser ---- */

static AstNode *parse_expr_prec(Parser *p, Precedence min_prec) {
    init_parse_rules();
    advance(p);
    PrefixFn prefix_fn = get_rule(p->previous.type)->prefix;
    if (prefix_fn == NULL) {
        error_at_previous(p, "expected expression");
        return NULL;
    }
    AstNode *left = prefix_fn(p);
    if (left == NULL) return NULL;

    while (true) {
        const ParseRule *rule = get_rule(p->current.type);
        if (rule->precedence <= min_prec) break;
        if (rule->infix == NULL) break;
        /* Special case: TOKEN_STAR as infix would be multiplication.
           But if the token after * is a type (built-in or user struct),
           then this * is a pointer-type prefix for the next statement, not multiplication.
           Peek ahead and stop the expression here. */
        if (p->current.type == TOKEN_STAR) {
            Token after_star = scanner_peek(&p->scanner);
            if (is_type_keyword(after_star.type)) break;
            /* Also handle *StructName varName — pointer-to-struct declaration */
            if (after_star.type == TOKEN_IDENTIFIER) {
                /* Peek two tokens ahead: if *Ident Ident, it's a var decl */
                Scanner saved_scan = p->scanner;
                scanner_next(&saved_scan); /* consume the Identifier (struct name) */
                Token after_ident = scanner_next(&saved_scan); /* next token */
                if (after_ident.type == TOKEN_IDENTIFIER) break;
            }
        }
        advance(p);
        AstNode *new_left = rule->infix(p, left);
        if (new_left == NULL) {
            /* left was already freed inside infix or remains orphaned */
            return NULL;
        }
        left = new_left;
    }
    return left;
}

/* ---- Type Parser ---- */

static bool is_type_keyword(TokenType t) {
    switch (t) {
    case TOKEN_TYPE_INT: case TOKEN_TYPE_I8:  case TOKEN_TYPE_I16:
    case TOKEN_TYPE_I32: case TOKEN_TYPE_I64: case TOKEN_TYPE_U8:
    case TOKEN_TYPE_U16: case TOKEN_TYPE_U32: case TOKEN_TYPE_U64:
    case TOKEN_TYPE_F32: case TOKEN_TYPE_F64: case TOKEN_TYPE_BOOL:
    case TOKEN_TYPE_CHAR:
    case TOKEN_TYPE_STRING: case TOKEN_TYPE_VOID:
    case TOKEN_TYPE_OBJECT:
    case TOKEN_ARRAY:
    case TOKEN_VEC:
    case TOKEN_MAP:
        return true;
    default:
        return false;
    }
}

static TypeNode *parse_type(Parser *p) {
    int line = p->current.line;
    int col  = p->current.column;

    /* *T — pointer */
    if (match_tok(p, TOKEN_STAR)) {
        TypeNode *pointee = parse_type(p);
        if (pointee == NULL) return NULL;
        TypeNode *tn = new_type_node(TYPE_NODE_POINTER, line, col);
        tn->as.pointee = pointee;
        return tn;
    }

    /* &T — read-only borrow, &!T — writable borrow. Prefix-only in type
       position, so unambiguous vs. infix bitwise '&' (which never appears
       at the start of a type). */
    if (match_tok(p, TOKEN_AMP)) {
        bool is_mut = match_tok(p, TOKEN_BANG);  /* &! => writable reference */
        TypeNode *pointee = parse_type(p);
        if (pointee == NULL) return NULL;
        TypeNode *tn = new_type_node(TYPE_NODE_REFERENCE, line, col);
        tn->is_mut = is_mut;
        tn->as.pointee = pointee;
        return tn;
    }

    /* vec(T) — dynamic array */
    if (match_tok(p, TOKEN_VEC)) {
        consume(p, TOKEN_LPAREN, "expected '(' after 'vec'");
        TypeNode *elem = parse_type(p);
        if (elem == NULL) return NULL;
        consume(p, TOKEN_RPAREN, "expected ')' after vec element type");
        TypeNode *tn = new_type_node(TYPE_NODE_VECTOR, line, col);
        tn->as.vec.elem = elem;
        return tn;
    }

    /* map(K, V) — chained hash map */
    if (match_tok(p, TOKEN_MAP)) {
        consume(p, TOKEN_LPAREN, "expected '(' after 'map'");
        TypeNode *key_tn = parse_type(p);
        if (key_tn == NULL) return NULL;
        consume(p, TOKEN_COMMA, "expected ',' between map key and value types");
        TypeNode *val_tn = parse_type(p);
        if (val_tn == NULL) { type_node_free(key_tn); return NULL; }
        consume(p, TOKEN_RPAREN, "expected ')' after map value type");
        TypeNode *tn = new_type_node(TYPE_NODE_MAP, line, col);
        tn->as.map.key = key_tn;
        tn->as.map.val = val_tn;
        return tn;
    }

    /* array(T, N) — fixed-size array */
    if (match_tok(p, TOKEN_ARRAY)) {
        consume(p, TOKEN_LPAREN, "expected '(' after 'array'");
        TypeNode *elem = parse_type(p);
        if (elem == NULL) return NULL;
        consume(p, TOKEN_COMMA, "expected ',' after array element type");
        if (!check(p, TOKEN_INT_LIT)) {
            error_at_current(p, "array size must be an integer literal");
            type_node_free(elem);
            return NULL;
        }
        advance(p);
        long long size_val = 0;
        /* Parse integer from token */
        for (int i = 0; i < p->previous.length; i++) {
            size_val = size_val * 10 + (p->previous.start[i] - '0');
        }
        if (size_val <= 0) {
            error_at(p, &p->previous, "array size must be positive");
            type_node_free(elem);
            return NULL;
        }
        consume(p, TOKEN_RPAREN, "expected ')' after array size");
        TypeNode *tn = new_type_node(TYPE_NODE_ARRAY, line, col);
        tn->as.array.elem = elem;
        tn->as.array.size = (int)size_val;
        return tn;
    }

    /* [T] — array (legacy syntax, kept for compatibility) */
    if (match_tok(p, TOKEN_LBRACKET)) {
        TypeNode *elem = parse_type(p);
        if (elem == NULL) return NULL;
        consume(p, TOKEN_RBRACKET, "expected ']' after array element type");
        TypeNode *tn = new_type_node(TYPE_NODE_ARRAY, line, col);
        tn->as.array.elem = elem;
        tn->as.array.size = 0;  /* size 0 = unsized, will be rejected by checker */
        return tn;
    }

    /* fn(types) -> ret */
    if (match_tok(p, TOKEN_FN)) {
        consume(p, TOKEN_LPAREN, "expected '(' after 'fn' in type");
        TypeNode **params = NULL;
        int param_count = 0;
        int param_cap = 0;
        if (!check(p, TOKEN_RPAREN)) {
            do {
                TypeNode *pt = parse_type(p);
                if (pt == NULL) break;
                if (param_count >= param_cap) {
                    param_cap = GROW_CAPACITY(param_cap);
                    params = GROW_ARRAY(TypeNode *, params, param_cap);
                }
                params[param_count++] = pt;
            } while (match_tok(p, TOKEN_COMMA));
        }
        consume(p, TOKEN_RPAREN, "expected ')' in function type");
        TypeNode *ret = NULL;
        if (match_tok(p, TOKEN_ARROW)) {
            ret = parse_type(p);
        }
        TypeNode *tn = new_type_node(TYPE_NODE_FN, line, col);
        tn->as.fn.params = params;
        tn->as.fn.param_count = param_count;
        tn->as.fn.ret = ret;
        return tn;
    }

    /* Primitive types */
    if (is_type_keyword(p->current.type)) {
        TokenType prim = p->current.type;
        advance(p);
        TypeNode *tn = new_type_node(TYPE_NODE_PRIMITIVE, line, col);
        tn->as.primitive = prim;
        return tn;
    }

    /* Named type (user struct or generic instantiation like Option(int)) */
    if (check(p, TOKEN_IDENTIFIER)) {
        advance(p);
        Token name_tok = p->previous;
        TypeNode *tn = new_type_node(TYPE_NODE_NAMED, line, col);
        tn->as.named.name = str_dup_n(name_tok.start, name_tok.length);
        tn->as.named.args = NULL;
        tn->as.named.arg_count = 0;

        /* Optional generic-style args: Name(T1, T2, ...) */
        if (match_tok(p, TOKEN_LPAREN)) {
            int cap = 0;
            if (!check(p, TOKEN_RPAREN)) {
                do {
                    TypeNode *arg = parse_type(p);
                    if (arg == NULL) { synchronize(p); break; }
                    if (tn->as.named.arg_count >= cap) {
                        cap = GROW_CAPACITY(cap);
                        tn->as.named.args = GROW_ARRAY(TypeNode *, tn->as.named.args, cap);
                    }
                    tn->as.named.args[tn->as.named.arg_count++] = arg;
                } while (match_tok(p, TOKEN_COMMA));
            }
            consume(p, TOKEN_RPAREN, "expected ')' after type arguments");
        }
        return tn;
    }

    error_at_current(p, "expected type");
    return NULL;
}

/* ---- starts_var_decl heuristic ---- */

/* Returns true if the current token stream looks like a variable declaration */
static bool starts_var_decl(Parser *p) {
    TokenType cur = p->current.type;

    /* Direct built-in type keyword */
    if (is_type_keyword(cur)) return true;

    /* *something — pointer type declaration */
    if (cur == TOKEN_STAR) {
        Token next = scanner_peek(&p->scanner);
        if (is_type_keyword(next.type)) return true;
        if (next.type == TOKEN_IDENTIFIER) {
            /* Need two peeks: save scanner state, advance, peek again */
            Scanner saved = p->scanner;
            Token saved_cur = p->current;
            Token saved_prev = p->previous;
            /* consume * */
            advance(p);
            /* now current is 'next' (the IDENTIFIER) */
            /* peek what follows the identifier */
            Token after = scanner_peek(&p->scanner);
            /* restore */
            p->scanner = saved;
            p->current = saved_cur;
            p->previous = saved_prev;
            if (after.type == TOKEN_IDENTIFIER) return true;
        }
        return false;
    }

    /* IDENTIFIER followed by another IDENTIFIER → named type + varname.
       IDENTIFIER followed by '(' may be a generic type instantiation
       (Option(int) o = ...) — disambiguate by skipping balanced parens
       and checking whether the token AFTER the matching ')' is an IDENTIFIER
       AND the token after that is '=' or ';' (i.e. a var decl, not adjacent
       expression statements like `print(a1) print(a2)`). */
    if (cur == TOKEN_IDENTIFIER) {
        Token next = scanner_peek(&p->scanner);
        if (next.type == TOKEN_IDENTIFIER) return true;

        if (next.type == TOKEN_LPAREN) {
            Scanner saved = p->scanner;
            Token saved_cur = p->current;
            Token saved_prev = p->previous;
            advance(p);  /* consume IDENT */
            advance(p);  /* consume '(' */
            int depth = 1;
            bool ok = true;
            while (depth > 0) {
                if (p->current.type == TOKEN_EOF) { ok = false; break; }
                if (p->current.type == TOKEN_LPAREN) depth++;
                else if (p->current.type == TOKEN_RPAREN) {
                    depth--;
                    if (depth == 0) break;
                }
                advance(p);
            }
            bool result = false;
            if (ok && p->current.type == TOKEN_RPAREN) {
                /* Need: IDENT then '=' or ';' to qualify as a var decl. */
                advance(p);  /* consume ')' */
                if (p->current.type == TOKEN_IDENTIFIER) {
                    Token after = scanner_peek(&p->scanner);
                    if (after.type == TOKEN_ASSIGN ||
                        after.type == TOKEN_SEMICOLON ||
                        after.type == TOKEN_EOF)
                    {
                        result = true;
                    }
                }
            }
            p->scanner = saved;
            p->current = saved_cur;
            p->previous = saved_prev;
            return result;
        }
        return false;
    }

    return false;
}

/* ---- Statement parsers ---- */

static AstNode *parse_var_decl(Parser *p) {
    int line = p->current.line;
    int col  = p->current.column;
    TypeNode *var_type = parse_type(p);
    if (var_type == NULL) return NULL;

    if (!check(p, TOKEN_IDENTIFIER)) {
        if (p->current.type >= TOKEN_FN && p->current.type <= TOKEN_TRY) {
            char buf[160];
            snprintf(buf, sizeof(buf),
                     "cannot use reserved keyword '%.*s' as variable name; rename it (e.g. '%.*s_')",
                     p->current.length, p->current.start,
                     p->current.length, p->current.start);
            error_at_current(p, buf);
        } else {
            error_at_current(p, "expected variable name");
        }
        type_node_free(var_type);
        return NULL;
    }
    advance(p);
    char *name = str_dup_n(p->previous.start, p->previous.length);

    AstNode *init = NULL;
    if (match_tok(p, TOKEN_ASSIGN)) {
        init = parse_expr_prec(p, PREC_NONE);
    }
    skip_semicolons(p);

    AstNode *n = new_node(AST_VAR_DECL, line, col);
    n->as.var_decl.var_type = var_type;
    n->as.var_decl.name = name;
    n->as.var_decl.init = init;
    return n;
}

/* Parse parameter list into arrays. Returns true on success. */
static bool parse_param_list(Parser *p,
                              TypeNode ***out_types, char ***out_names,
                              int *out_count,
                              bool *out_is_vararg) {
    TypeNode **param_types = NULL;
    char **param_names = NULL;
    int param_count = 0;
    int param_cap = 0;
    bool is_vararg = false;

    if (!check(p, TOKEN_RPAREN)) {
        do {
            if (check(p, TOKEN_ELLIPSIS)) {
                advance(p);
                is_vararg = true;
                break;
            }
            TypeNode *pt = parse_type(p);
            if (pt == NULL) {
                /* free what we have */
                for (int i = 0; i < param_count; i++) {
                    type_node_free(param_types[i]);
                    free(param_names[i]);
                }
                free(param_types);
                free(param_names);
                return false;
            }
            char *pname = NULL;
            /* Accept identifier or 'self' as parameter name */
            if (check(p, TOKEN_IDENTIFIER) || check(p, TOKEN_SELF)) {
                advance(p);
                pname = str_dup_n(p->previous.start, p->previous.length);
            } else if (p->current.type >= TOKEN_FN && p->current.type <= TOKEN_TRY) {
                /* Reserved keyword misused as parameter name — emit helpful error */
                char buf[160];
                snprintf(buf, sizeof(buf),
                         "cannot use reserved keyword '%.*s' as parameter name; rename it (e.g. '%.*s_')",
                         p->current.length, p->current.start,
                         p->current.length, p->current.start);
                error_at_current(p, buf);
                advance(p); /* consume the keyword to avoid cascading errors */
                pname = str_dup_n("_", 1);
            } else {
                pname = str_dup_n("_", 1);
            }
            if (param_count >= param_cap) {
                param_cap = GROW_CAPACITY(param_cap);
                param_types = GROW_ARRAY(TypeNode *, param_types, param_cap);
                param_names = GROW_ARRAY(char *, param_names, param_cap);
            }
            param_types[param_count] = pt;
            param_names[param_count] = pname;
            param_count++;
        } while (match_tok(p, TOKEN_COMMA));
    }

    *out_types = param_types;
    *out_names = param_names;
    *out_count = param_count;
    if (out_is_vararg) *out_is_vararg = is_vararg;
    return true;
}

static AstNode *parse_fn_decl(Parser *p) {
    /* 'fn' already consumed */
    int line = p->previous.line;
    int col  = p->previous.column;

    if (!check(p, TOKEN_IDENTIFIER)) {
        error_at_current(p, "expected function name after 'fn'");
        return NULL;
    }
    advance(p);
    char *name = str_dup_n(p->previous.start, p->previous.length);

    consume(p, TOKEN_LPAREN, "expected '(' after function name");

    /* Phase A1: detect explicit self-borrow at first param position.
       Forms: (&self, ...)  → readonly  (Phase A2 — accepted but reserved)
              (&!self, ...) → writable
       The leading '&[!]self' has no type annotation; the rest of the
       parameter list (after a comma, if any) parses normally. */
    int self_borrow_kind = 0;
    if (check(p, TOKEN_AMP)) {
        /* Peek to see if this is &self or &!self (vs. &string / &vec / &map etc.).
           Only consume '&' if it's truly an explicit self-borrow form. */
        Token next = scanner_peek(&p->scanner);
        bool is_self_borrow = false;
        bool is_mut = false;
        if (next.type == TOKEN_SELF) {
            is_self_borrow = true;
        } else if (next.type == TOKEN_BANG) {
            /* 2-token lookahead: peek past '!' to find 'self' */
            Scanner saved = p->scanner;
            Token saved_cur = p->current;
            Token saved_prev = p->previous;
            advance(p); /* consume & */
            advance(p); /* consume ! */
            if (check(p, TOKEN_SELF)) {
                is_self_borrow = true;
                is_mut = true;
            }
            /* restore */
            p->scanner = saved;
            p->current = saved_cur;
            p->previous = saved_prev;
        }
        if (is_self_borrow) {
            advance(p); /* consume & */
            if (is_mut) advance(p); /* consume ! */
            advance(p); /* consume self */
            self_borrow_kind = is_mut ? 2 : 1;
            if (!check(p, TOKEN_RPAREN)) {
                consume(p, TOKEN_COMMA, "expected ',' or ')' after self parameter");
            }
        }
    }

    TypeNode **param_types = NULL;
    char **param_names = NULL;
    int param_count = 0;
    if (!parse_param_list(p, &param_types, &param_names, &param_count, NULL)) {
        free(name);
        return NULL;
    }
    consume(p, TOKEN_RPAREN, "expected ')' after parameters");

    TypeNode *return_type = NULL;
    if (match_tok(p, TOKEN_ARROW)) {
        return_type = parse_type(p);
    }

    AstNode *body = parse_block(p);

    AstNode *n = new_node(AST_FN_DECL, line, col);
    n->as.fn_decl.name = name;
    n->as.fn_decl.param_types = param_types;
    n->as.fn_decl.param_names = param_names;
    n->as.fn_decl.param_count = param_count;
    n->as.fn_decl.return_type = return_type;
    n->as.fn_decl.body = body;
    n->as.fn_decl.is_static = false;
    n->as.fn_decl.impl_struct_name = NULL;
    n->as.fn_decl.self_borrow_kind = self_borrow_kind;
    return n;
}

static AstNode *parse_struct_decl(Parser *p) {
    /* 'struct' already consumed */
    int line = p->previous.line;
    int col  = p->previous.column;

    if (!check(p, TOKEN_IDENTIFIER)) {
        error_at_current(p, "expected struct name");
        return NULL;
    }
    advance(p);
    char *name = str_dup_n(p->previous.start, p->previous.length);

    consume(p, TOKEN_LBRACE, "expected '{' after struct name");

    TypeNode **field_types = NULL;
    char **field_names = NULL;
    int field_count = 0;
    int field_cap = 0;

    while (!check(p, TOKEN_RBRACE) && !check(p, TOKEN_EOF)) {
        TypeNode *ft = parse_type(p);
        if (ft == NULL) {
            synchronize(p);
            continue;
        }
        if (!check(p, TOKEN_IDENTIFIER)) {
            error_at_current(p, "expected field name");
            type_node_free(ft);
            synchronize(p);
            continue;
        }
        advance(p);
        char *fname = str_dup_n(p->previous.start, p->previous.length);
        skip_semicolons(p);

        if (field_count >= field_cap) {
            field_cap = GROW_CAPACITY(field_cap);
            field_types = GROW_ARRAY(TypeNode *, field_types, field_cap);
            field_names = GROW_ARRAY(char *, field_names, field_cap);
        }
        field_types[field_count] = ft;
        field_names[field_count] = fname;
        field_count++;
    }
    consume(p, TOKEN_RBRACE, "expected '}' after struct fields");

    AstNode *n = new_node(AST_STRUCT_DECL, line, col);
    n->as.struct_decl.name = name;
    n->as.struct_decl.field_types = field_types;
    n->as.struct_decl.field_names = field_names;
    n->as.struct_decl.field_count = field_count;
    return n;
}

/* enum Name {
 *     V1,                  // no payload
 *     V2(Type),            // unnamed payload (Rust-style)
 *     V3(Type name, ...);  // named payload (LS-style)
 * }
 * Variant separators (',' / ';' / nothing) are all accepted. */
static AstNode *parse_enum_decl(Parser *p) {
    /* 'enum' already consumed */
    int line = p->previous.line;
    int col  = p->previous.column;

    if (!check(p, TOKEN_IDENTIFIER)) {
        error_at_current(p, "expected enum name");
        return NULL;
    }
    advance(p);
    char *name = str_dup_n(p->previous.start, p->previous.length);

    consume(p, TOKEN_LBRACE, "expected '{' after enum name");

    AstNode *n = new_node(AST_ENUM_DECL, line, col);
    n->as.enum_decl.name = name;
    n->as.enum_decl.variants = NULL;
    n->as.enum_decl.variant_count = 0;
    int variant_cap = 0;

    skip_semicolons(p);
    while (!check(p, TOKEN_RBRACE) && !check(p, TOKEN_EOF)) {
        if (!check(p, TOKEN_IDENTIFIER)) {
            error_at_current(p, "expected variant name");
            synchronize(p);
            continue;
        }
        advance(p);
        char *vname = str_dup_n(p->previous.start, p->previous.length);

        TypeNode **ptypes = NULL;
        char **pnames = NULL;
        int pcount = 0;
        int pcap = 0;

        if (match_tok(p, TOKEN_LPAREN)) {
            if (!check(p, TOKEN_RPAREN)) {
                do {
                    TypeNode *pt = parse_type(p);
                    if (pt == NULL) {
                        synchronize(p);
                        break;
                    }
                    char *pn = NULL;
                    if (check(p, TOKEN_IDENTIFIER)) {
                        advance(p);
                        pn = str_dup_n(p->previous.start, p->previous.length);
                    }
                    if (pcount >= pcap) {
                        pcap = GROW_CAPACITY(pcap);
                        ptypes = GROW_ARRAY(TypeNode *, ptypes, pcap);
                        pnames = GROW_ARRAY(char *, pnames, pcap);
                    }
                    ptypes[pcount] = pt;
                    pnames[pcount] = pn;
                    pcount++;
                } while (match_tok(p, TOKEN_COMMA));
            }
            consume(p, TOKEN_RPAREN, "expected ')' after variant payload");
        }

        /* Optional separator between variants */
        match_tok(p, TOKEN_COMMA);
        skip_semicolons(p);

        if (n->as.enum_decl.variant_count >= variant_cap) {
            variant_cap = GROW_CAPACITY(variant_cap);
            n->as.enum_decl.variants = realloc_safe(
                n->as.enum_decl.variants,
                sizeof(*n->as.enum_decl.variants) * (size_t)variant_cap);
        }
        int idx = n->as.enum_decl.variant_count++;
        n->as.enum_decl.variants[idx].name = vname;
        n->as.enum_decl.variants[idx].payload_types = ptypes;
        n->as.enum_decl.variants[idx].payload_names = pnames;
        n->as.enum_decl.variants[idx].payload_count = pcount;
    }
    consume(p, TOKEN_RBRACE, "expected '}' after enum variants");

    return n;
}

static AstNode *parse_impl_decl(Parser *p) {
    /* 'impl' already consumed */
    int line = p->previous.line;
    int col  = p->previous.column;

    if (!check(p, TOKEN_IDENTIFIER)) {
        error_at_current(p, "expected struct name after 'impl'");
        return NULL;
    }
    advance(p);
    char *name = str_dup_n(p->previous.start, p->previous.length);

    consume(p, TOKEN_LBRACE, "expected '{' after impl name");

    AstNode **methods = NULL;
    int method_count = 0;
    int method_cap = 0;

    while (!check(p, TOKEN_RBRACE) && !check(p, TOKEN_EOF)) {
        bool is_static = false;
        if (match_tok(p, TOKEN_STATIC)) {
            is_static = true;
        }
        if (!match_tok(p, TOKEN_FN)) {
            error_at_current(p, "expected 'fn' in impl block");
            synchronize(p);
            continue;
        }
        AstNode *method = parse_fn_decl(p);
        if (method == NULL) {
            synchronize(p);
            continue;
        }
        method->as.fn_decl.is_static = is_static;
        method->as.fn_decl.impl_struct_name = name;
        if (method_count >= method_cap) {
            method_cap = GROW_CAPACITY(method_cap);
            methods = GROW_ARRAY(AstNode *, methods, method_cap);
        }
        methods[method_count++] = method;
    }
    consume(p, TOKEN_RBRACE, "expected '}' after impl block");

    AstNode *n = new_node(AST_IMPL_DECL, line, col);
    n->as.impl_decl.name = name;
    n->as.impl_decl.methods = methods;
    n->as.impl_decl.method_count = method_count;
    return n;
}

static AstNode *parse_module_decl(Parser *p) {
    /* 'module' already consumed */
    int line = p->previous.line;
    int col  = p->previous.column;
    if (!check(p, TOKEN_IDENTIFIER)) {
        error_at_current(p, "expected module name");
        return NULL;
    }
    advance(p);
    char *name = str_dup_n(p->previous.start, p->previous.length);
    skip_semicolons(p);
    AstNode *n = new_node(AST_MODULE_DECL, line, col);
    n->as.module_decl.name = name;
    return n;
}

static AstNode *parse_import_decl(Parser *p) {
    /* 'import' already consumed */
    int line = p->previous.line;
    int col  = p->previous.column;

    /* Build path from identifiers separated by dots: std.io -> "std.io" */
    char path_buf[256];
    int path_len = 0;

    if (!check(p, TOKEN_IDENTIFIER)) {
        error_at_current(p, "expected module path after 'import'");
        return NULL;
    }
    advance(p);
    Token first = p->previous;
    if (path_len + first.length < (int)sizeof(path_buf) - 1) {
        memcpy(path_buf + path_len, first.start, (size_t)first.length);
        path_len += first.length;
    }

    while (check(p, TOKEN_DOT)) {
        advance(p); /* consume '.' */
        if (!check(p, TOKEN_IDENTIFIER)) break;
        advance(p);
        Token seg = p->previous;
        if (path_len + 1 + seg.length < (int)sizeof(path_buf) - 1) {
            path_buf[path_len++] = '.';
            memcpy(path_buf + path_len, seg.start, (size_t)seg.length);
            path_len += seg.length;
        }
    }
    path_buf[path_len] = '\0';
    skip_semicolons(p);

    AstNode *n = new_node(AST_IMPORT_DECL, line, col);
    n->as.import_decl.path = str_dup_n(path_buf, path_len);
    return n;
}

static AstNode *parse_load_lib(Parser *p) {
    /* 'lib' (TOKEN_TYPE_LIB) already consumed */
    int line = p->previous.line;
    int col  = p->previous.column;

    if (!check(p, TOKEN_IDENTIFIER)) {
        error_at_current(p, "expected variable name after 'lib'");
        return NULL;
    }
    advance(p);
    char *var_name = str_dup_n(p->previous.start, p->previous.length);

    consume(p, TOKEN_ASSIGN, "expected '=' after lib variable name");
    consume(p, TOKEN_LOAD, "expected 'load' after '='");
    consume(p, TOKEN_LPAREN, "expected '(' after 'load'");

    if (!check(p, TOKEN_STRING_LIT)) {
        error_at_current(p, "expected string literal for library path");
        free(var_name);
        return NULL;
    }
    advance(p);
    Token path_tok = p->previous;
    char *lib_path = process_string_token(path_tok.start, path_tok.length);

    consume(p, TOKEN_RPAREN, "expected ')' after library path");
    skip_semicolons(p);

    AstNode *n = new_node(AST_LOAD_LIB, line, col);
    n->as.load_lib.var_name = var_name;
    n->as.load_lib.lib_path = lib_path;
    return n;
}

/* Parse extern fn body starting after 'fn' keyword is consumed.
   line/col: position of the 'extern' keyword for error reporting.
   'from lib' clause is optional; lib_name = NULL means direct libc call. */
static AstNode *parse_extern_fn_body(Parser *p, int line, int col) {
    if (!check(p, TOKEN_IDENTIFIER)) {
        error_at_current(p, "expected function name after 'extern fn'");
        return NULL;
    }
    advance(p);
    char *name = str_dup_n(p->previous.start, p->previous.length);

    consume(p, TOKEN_LPAREN, "expected '(' after extern function name");

    TypeNode **param_types = NULL;
    char **param_names = NULL;
    int param_count = 0;
    bool is_vararg = false;
    if (!parse_param_list(p, &param_types, &param_names, &param_count, &is_vararg)) {
        free(name);
        return NULL;
    }
    consume(p, TOKEN_RPAREN, "expected ')' after extern function parameters");

    TypeNode *return_type = NULL;
    if (match_tok(p, TOKEN_ARROW)) {
        return_type = parse_type(p);
    }

    /* 'from lib' is now optional — absent = bind to process symbols (libc/CRT) */
    char *lib_name = NULL;
    if (match_tok(p, TOKEN_FROM)) {
        if (check(p, TOKEN_IDENTIFIER)) {
            advance(p);
            lib_name = str_dup_n(p->previous.start, p->previous.length);
        } else {
            error_at_current(p, "expected library name after 'from'");
        }
    }
    skip_semicolons(p);

    AstNode *n = new_node(AST_EXTERN_FN, line, col);
    n->as.extern_fn.name       = name;
    n->as.extern_fn.param_types = param_types;
    n->as.extern_fn.param_names = param_names;
    n->as.extern_fn.param_count = param_count;
    n->as.extern_fn.return_type = return_type;
    n->as.extern_fn.is_vararg   = is_vararg;
    n->as.extern_fn.lib_name    = lib_name;
    return n;
}

/* Parse: extern fn Name(...) [-> T] ['from' lib]  — 'extern' already consumed */
static AstNode *parse_extern_fn(Parser *p) {
    int line = p->previous.line;
    int col  = p->previous.column;
    consume(p, TOKEN_FN, "expected 'fn' after 'extern'");
    return parse_extern_fn_body(p, line, col);
}

/* Parse: extern struct Name { field_type field_name ... }
   'extern' already consumed; current token is 'struct'. */
static AstNode *parse_extern_struct(Parser *p) {
    advance(p); /* consume 'struct' */
    int line = p->previous.line;
    int col  = p->previous.column;

    if (!check(p, TOKEN_IDENTIFIER)) {
        error_at_current(p, "expected struct name after 'extern struct'");
        return NULL;
    }
    advance(p);
    char *name = str_dup_n(p->previous.start, p->previous.length);

    consume(p, TOKEN_LBRACE, "expected '{' after extern struct name");

    TypeNode **field_types = NULL;
    char     **field_names = NULL;
    int        field_count = 0;
    int        capacity    = 0;

    while (!check(p, TOKEN_RBRACE) && !check(p, TOKEN_EOF)) {
        skip_semicolons(p);
        if (check(p, TOKEN_RBRACE)) break;

        TypeNode *ft = parse_type(p);
        if (ft == NULL) { free(name); free(field_types); free(field_names); return NULL; }

        if (!check(p, TOKEN_IDENTIFIER)) {
            error_at_current(p, "expected field name in extern struct");
            type_node_free(ft);
            free(name); free(field_types); free(field_names);
            return NULL;
        }
        advance(p);
        char *fn = str_dup_n(p->previous.start, p->previous.length);

        if (field_count >= capacity) {
            capacity = capacity == 0 ? 4 : capacity * 2;
            field_types = (TypeNode **)realloc(field_types, (size_t)capacity * sizeof(TypeNode *));
            field_names = (char     **)realloc(field_names, (size_t)capacity * sizeof(char *));
        }
        field_types[field_count] = ft;
        field_names[field_count] = fn;
        field_count++;
        skip_semicolons(p);
    }
    consume(p, TOKEN_RBRACE, "expected '}' after extern struct body");
    skip_semicolons(p);

    AstNode *n = new_node(AST_EXTERN_STRUCT_DECL, line, col);
    n->as.extern_struct_decl.name        = name;
    n->as.extern_struct_decl.field_types = field_types;
    n->as.extern_struct_decl.field_names = field_names;
    n->as.extern_struct_decl.field_count = field_count;
    return n;
}

/* Parse: extern { struct/fn decls... }  — 'extern' already consumed */
static AstNode *parse_extern_block(Parser *p) {
    int line = p->previous.line;
    int col  = p->previous.column;

    consume(p, TOKEN_LBRACE, "expected '{' after 'extern'");

    AstNode **decls    = NULL;
    int       decl_count = 0;
    int       capacity   = 0;

    while (!check(p, TOKEN_RBRACE) && !check(p, TOKEN_EOF)) {
        skip_semicolons(p);
        if (check(p, TOKEN_RBRACE)) break;

        AstNode *d = NULL;
        if (check(p, TOKEN_STRUCT)) {
            d = parse_extern_struct(p);
        } else if (match_tok(p, TOKEN_FN)) {
            int fn_line = p->previous.line;
            int fn_col  = p->previous.column;
            d = parse_extern_fn_body(p, fn_line, fn_col);
        } else {
            error_at_current(p, "expected 'struct' or 'fn' inside extern block");
            advance(p);
            continue;
        }

        if (d != NULL) {
            if (decl_count >= capacity) {
                capacity = capacity == 0 ? 4 : capacity * 2;
                decls = (AstNode **)realloc(decls, (size_t)capacity * sizeof(AstNode *));
            }
            decls[decl_count++] = d;
        }
    }
    consume(p, TOKEN_RBRACE, "expected '}' to close extern block");
    skip_semicolons(p);

    AstNode *n = new_node(AST_EXTERN_BLOCK, line, col);
    n->as.extern_block.decls      = decls;
    n->as.extern_block.decl_count = decl_count;
    return n;
}

static AstNode *parse_if_stmt(Parser *p) {
    /* 'if' already consumed */
    int line = p->previous.line;
    int col  = p->previous.column;

    /* Optional parentheses around condition */
    bool has_paren = match_tok(p, TOKEN_LPAREN);
    AstNode *cond = parse_expr_prec(p, PREC_NONE);
    if (has_paren) consume(p, TOKEN_RPAREN, "expected ')' after if condition");

    AstNode *then_block = parse_block(p);
    AstNode *else_block = NULL;
    if (match_tok(p, TOKEN_ELSE)) {
        if (check(p, TOKEN_IF)) {
            advance(p);
            else_block = parse_if_stmt(p);
        } else {
            else_block = parse_block(p);
        }
    }

    AstNode *n = new_node(AST_IF, line, col);
    n->as.if_stmt.cond = cond;
    n->as.if_stmt.then_block = then_block;
    n->as.if_stmt.else_block = else_block;
    return n;
}

static AstNode *parse_while_stmt(Parser *p) {
    /* 'while' already consumed */
    int line = p->previous.line;
    int col  = p->previous.column;

    bool has_paren = match_tok(p, TOKEN_LPAREN);
    AstNode *cond = parse_expr_prec(p, PREC_NONE);
    if (has_paren) consume(p, TOKEN_RPAREN, "expected ')' after while condition");

    AstNode *body = parse_block(p);

    AstNode *n = new_node(AST_WHILE, line, col);
    n->as.while_stmt.cond = cond;
    n->as.while_stmt.body = body;
    return n;
}

/* Parse a single statement suitable for use inside a C-style for clause.
   Handles variable declarations (e.g. int i = 0) and expression/assignment
   statements (e.g. i = i + 1), but does NOT consume trailing semicolons. */
static AstNode *parse_for_clause_stmt(Parser *p) {
    /* Variable declaration: int i = 0 */
    if (starts_var_decl(p)) {
        int line = p->current.line;
        int col  = p->current.column;
        TypeNode *var_type = parse_type(p);
        if (var_type == NULL) return NULL;

        if (!check(p, TOKEN_IDENTIFIER)) {
            error_at_current(p, "expected variable name");
            type_node_free(var_type);
            return NULL;
        }
        advance(p);
        char *name = str_dup_n(p->previous.start, p->previous.length);

        AstNode *init = NULL;
        if (match_tok(p, TOKEN_ASSIGN)) {
            init = parse_expr_prec(p, PREC_NONE);
        }

        AstNode *n = new_node(AST_VAR_DECL, line, col);
        n->as.var_decl.var_type = var_type;
        n->as.var_decl.name = name;
        n->as.var_decl.init = init;
        return n;
    }

    /* Expression or assignment — parse_expr_prec handles assignments via
       infix_assign, so i = i + 1 returns AST_ASSIGN directly */
    AstNode *expr = parse_expr_prec(p, PREC_NONE);
    if (expr == NULL) return NULL;

    /* If parse_expr_prec returned AST_ASSIGN, it's already a statement node */
    if (expr->kind == AST_ASSIGN) {
        return expr;
    }

    /* Otherwise wrap plain expressions as expr_stmt for codegen_stmt */
    AstNode *n = new_node(AST_EXPR_STMT, expr->line, expr->column);
    n->as.expr_stmt.expr = expr;
    return n;
}

/* Detect if the for loop is C-style: for (init; cond; update) { }
   Called right after consuming 'for' and '('.
   Uses scanner lookahead to find a semicolon before ')'. */
static bool is_c_style_for(Parser *p) {
    /* Save parser state */
    Scanner saved_scanner = p->scanner;
    Token saved_current = p->current;
    Token saved_previous = p->previous;

    /* Scan forward looking for a semicolon or closing paren at depth 0 */
    int depth = 0;
    bool found_semi = false;
    for (;;) {
        TokenType t = p->current.type;
        if (t == TOKEN_EOF) break;
        if (t == TOKEN_LPAREN) { depth++; }
        else if (t == TOKEN_RPAREN) {
            if (depth == 0) break;
            depth--;
        }
        else if (t == TOKEN_SEMICOLON && depth == 0) {
            found_semi = true;
            break;
        }
        advance(p);
    }

    /* Restore parser state */
    p->scanner = saved_scanner;
    p->current = saved_current;
    p->previous = saved_previous;

    return found_semi;
}

static AstNode *parse_for_stmt(Parser *p) {
    /* 'for' already consumed */
    int line = p->previous.line;
    int col  = p->previous.column;

    bool has_paren = match_tok(p, TOKEN_LPAREN);

    /* C-style for: for (init; cond; update) { body }
       Requires parentheses and at least one semicolon */
    if (has_paren && is_c_style_for(p)) {
        /* Parse init clause (may be empty) */
        AstNode *init = NULL;
        if (!check(p, TOKEN_SEMICOLON)) {
            init = parse_for_clause_stmt(p);
        }
        consume(p, TOKEN_SEMICOLON, "expected ';' after for init clause");

        /* Parse condition (may be empty → infinite loop) */
        AstNode *cond = NULL;
        if (!check(p, TOKEN_SEMICOLON)) {
            cond = parse_expr_prec(p, PREC_NONE);
        }
        consume(p, TOKEN_SEMICOLON, "expected ';' after for condition");

        /* Parse update clause (may be empty) */
        AstNode *update = NULL;
        if (!check(p, TOKEN_RPAREN)) {
            update = parse_for_clause_stmt(p);
        }
        consume(p, TOKEN_RPAREN, "expected ')' after for clauses");

        AstNode *body = parse_block(p);

        AstNode *n = new_node(AST_FOR_C, line, col);
        n->as.for_c_stmt.init = init;
        n->as.for_c_stmt.cond = cond;
        n->as.for_c_stmt.update = update;
        n->as.for_c_stmt.body = body;
        return n;
    }

    /* for-in loop: for x in iter { } OR for (x in iter) { } */
    if (!check(p, TOKEN_IDENTIFIER)) {
        error_at_current(p, "expected variable name in for loop");
        return NULL;
    }
    advance(p);
    char *var = str_dup_n(p->previous.start, p->previous.length);

    consume(p, TOKEN_IN, "expected 'in' after for variable");

    AstNode *iter = parse_expr_prec(p, PREC_NONE);
    if (has_paren) consume(p, TOKEN_RPAREN, "expected ')' after for expression");

    AstNode *body = parse_block(p);

    AstNode *n = new_node(AST_FOR, line, col);
    n->as.for_stmt.var = var;
    n->as.for_stmt.iter = iter;
    n->as.for_stmt.body = body;
    return n;
}

static AstNode *parse_return_stmt(Parser *p) {
    /* 'return' already consumed */
    int line = p->previous.line;
    int col  = p->previous.column;

    AstNode *value = NULL;
    /* If next token could start an expression, parse it */
    if (!check(p, TOKEN_SEMICOLON) &&
        !check(p, TOKEN_RBRACE) &&
        !check(p, TOKEN_EOF)) {
        value = parse_expr_prec(p, PREC_NONE);
    }
    skip_semicolons(p);

    AstNode *n = new_node(AST_RETURN, line, col);
    n->as.return_stmt.value = value;
    return n;
}

static AstNode *parse_block(Parser *p) {
    int line = p->current.line;
    int col  = p->current.column;

    if (!consume(p, TOKEN_LBRACE, "expected '{'")) {
        /* Return empty block to allow recovery */
        AstNode *n = new_node(AST_BLOCK, line, col);
        n->as.block.stmts = NULL;
        n->as.block.stmt_count = 0;
        return n;
    }
    skip_semicolons(p);

    AstNode **stmts = NULL;
    int stmt_count = 0;
    int stmt_cap = 0;

    while (!check(p, TOKEN_RBRACE) && !check(p, TOKEN_EOF)) {
        AstNode *stmt = parse_statement(p);
        if (stmt == NULL) {
            synchronize(p);
            continue;
        }
        if (stmt_count >= stmt_cap) {
            stmt_cap = GROW_CAPACITY(stmt_cap);
            stmts = GROW_ARRAY(AstNode *, stmts, stmt_cap);
        }
        stmts[stmt_count++] = stmt;
        skip_semicolons(p);
    }
    consume(p, TOKEN_RBRACE, "expected '}' after block");

    AstNode *n = new_node(AST_BLOCK, line, col);
    n->as.block.stmts = stmts;
    n->as.block.stmt_count = stmt_count;
    return n;
}

/* ---- parse_statement ---- */

static AstNode *parse_statement(Parser *p) {
    skip_semicolons(p);
    int line = p->current.line;
    int col  = p->current.column;

    /* fn name(...) {...} — function declaration vs closure expression */
    if (check(p, TOKEN_FN)) {
        /* Peek: if next after 'fn' is an identifier -> function declaration */
        Scanner saved_scanner = p->scanner;
        Token saved_cur = p->current;
        Token saved_prev = p->previous;
        advance(p); /* consume 'fn' */
        bool is_named = check(p, TOKEN_IDENTIFIER);
        /* restore */
        p->scanner = saved_scanner;
        p->current = saved_cur;
        p->previous = saved_prev;

        if (is_named) {
            advance(p); /* consume 'fn' again */
            return parse_fn_decl(p);
        }
        /* else fall through to expression statement (closure) */
    } else if (match_tok(p, TOKEN_STRUCT)) {
        return parse_struct_decl(p);
    } else if (match_tok(p, TOKEN_ENUM)) {
        return parse_enum_decl(p);
    } else if (match_tok(p, TOKEN_IMPL)) {
        return parse_impl_decl(p);
    } else if (match_tok(p, TOKEN_MODULE)) {
        return parse_module_decl(p);
    } else if (match_tok(p, TOKEN_IMPORT)) {
        return parse_import_decl(p);
    } else if (match_tok(p, TOKEN_TYPE_LIB)) {
        return parse_load_lib(p);
    } else if (match_tok(p, TOKEN_EXTERN)) {
        if (check(p, TOKEN_LBRACE))
            return parse_extern_block(p);
        if (check(p, TOKEN_STRUCT))
            return parse_extern_struct(p);
        return parse_extern_fn(p);
    } else if (match_tok(p, TOKEN_IF)) {
        return parse_if_stmt(p);
    } else if (match_tok(p, TOKEN_WHILE)) {
        return parse_while_stmt(p);
    } else if (match_tok(p, TOKEN_FOR)) {
        return parse_for_stmt(p);
    } else if (match_tok(p, TOKEN_RETURN)) {
        return parse_return_stmt(p);
    } else if (match_tok(p, TOKEN_BREAK)) {
        skip_semicolons(p);
        return new_node(AST_BREAK, line, col);
    } else if (match_tok(p, TOKEN_CONTINUE)) {
        skip_semicolons(p);
        return new_node(AST_CONTINUE, line, col);
    } else if (check(p, TOKEN_LBRACE)) {
        return parse_block(p);
    } else if (starts_var_decl(p)) {
        return parse_var_decl(p);
    }

    /* Expression statement (including closures and any other expressions) */
    {
        AstNode *expr = parse_expr_prec(p, PREC_NONE);
        if (expr == NULL) return NULL;
        skip_semicolons(p);

        /* Assignment expressions stand alone as statements */
        if (expr->kind == AST_ASSIGN) return expr;

        AstNode *n = new_node(AST_EXPR_STMT, line, col);
        n->as.expr_stmt.expr = expr;
        return n;
    }
}

/* ---- Top-level parse ---- */

/* Parse source text -> AST_PROGRAM node */
AstNode *parse(const char *source, const char *source_path) {
    init_parse_rules();

    Parser p;
    memset(&p, 0, sizeof(p));
    scanner_init(&p.scanner, source);
    p.source_path = source_path;
    p.had_error = false;
    p.panic_mode = false;

    /* Prime the parser */
    advance(&p);

    AstNode **decls = NULL;
    int decl_count = 0;
    int decl_cap = 0;

    skip_semicolons(&p);
    while (!check(&p, TOKEN_EOF)) {
        AstNode *decl = parse_statement(&p);
        if (decl == NULL) {
            synchronize(&p);
        } else {
            if (decl_count >= decl_cap) {
                decl_cap = GROW_CAPACITY(decl_cap);
                decls = GROW_ARRAY(AstNode *, decls, decl_cap);
            }
            decls[decl_count++] = decl;
        }
        skip_semicolons(&p);
    }

    if (p.had_error) {
        for (int i = 0; i < decl_count; i++) {
            ast_free(decls[i]);
        }
        free(decls);
        return NULL;
    }

    AstNode *program = new_node(AST_PROGRAM, 1, 1);
    program->as.program.decls = decls;
    program->as.program.decl_count = decl_count;
    return program;
}
