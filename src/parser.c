/* parser.c — Pratt parser: token stream -> AST. Core TU: token-stream
   primitives, error reporting, AST/TypeNode allocation, the three
   depth-guarded recursive entry points, and the top-level parse() entry
   point. The Pratt expression parser, type parser, declaration parsers and
   statement parsers live in parser_expr.c/parser_type.c/parser_decl.c/
   parser_stmt.c respectively (see parser_internal.h for the roster and the
   cross-TU surface). */
#include "parser_internal.h"
#include "common.h"
#include "diag.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ---- Helpers ---- */

/* str_dup_n: portable strndup replacement */
char *str_dup_n(const char *s, int len) {
    char *r = (char *)malloc_safe((size_t)len + 1);
    memcpy(r, s, (size_t)len);
    r[len] = '\0';
    return r;
}

/* Allocate a new AstNode with zero-init */
AstNode *new_node(AstNodeType kind, int line, int col) {
    AstNode *n = (AstNode *)malloc_safe(sizeof(AstNode));
    memset(n, 0, sizeof(AstNode));
    n->kind = kind;
    n->line = line;
    n->column = col;
    return n;
}

/* Allocate a new TypeNode with zero-init */
TypeNode *new_type_node(TypeNodeKind kind, int line, int col) {
    TypeNode *t = (TypeNode *)malloc_safe(sizeof(TypeNode));
    memset(t, 0, sizeof(TypeNode));
    t->kind = kind;
    t->line = line;
    t->column = col;
    return t;
}

/* ---- Error Handling ---- */

/* Cap on rendered parse diagnostics, mirroring CHECKER_MAX_ERRORS. Panic-mode
   recovery resets per statement, so a pathological input (e.g. thousands of
   stray '}') can otherwise render one full snippet+caret diagnostic per token
   — tens of MB of stderr and minutes of wall clock. Past the cap we keep
   counting (had_error stays authoritative) but stop rendering. */
#define LS_MAX_PARSE_ERRORS 20

void error_at(Parser *p, Token *tok, const char *msg) {
    if (p->panic_mode) return;
    p->panic_mode = true;
    p->had_error = true;
    if (p->error_count >= LS_MAX_PARSE_ERRORS) {
        p->error_count++;
        return;
    }
    p->error_count++;
    diag_emitf(DIAG_PARSE_ERROR, p->source_path, tok->line, tok->column,
               tok->length > 0 ? tok->length : 1, NULL, "%s", msg);
}

void error_at_current(Parser *p, const char *msg) {
    error_at(p, &p->current, msg);
}

void error_at_previous(Parser *p, const char *msg) {
    error_at(p, &p->previous, msg);
}

/* Report an error at `previous` WITHOUT entering panic mode. For value-level
   errors on a syntactically valid token (e.g. an out-of-range numeric
   literal): parsing continues normally afterwards, so there is no cascade to
   suppress — and setting panic_mode here would swallow every later error
   because no synchronize() ever runs when the statement parses fine. Still
   suppressed while already panicking from a real syntax error. */
void error_at_previous_no_panic(Parser *p, const char *msg) {
    if (p->panic_mode) return;
    p->had_error = true;
    if (p->error_count >= LS_MAX_PARSE_ERRORS) {
        p->error_count++;
        return;
    }
    p->error_count++;
    diag_emitf(DIAG_PARSE_ERROR, p->source_path, p->previous.line,
               p->previous.column,
               p->previous.length > 0 ? p->previous.length : 1, NULL,
               "%s", msg);
}

/* Advance scanner: previous = current, current = next token */
void advance(Parser *p) {
    p->previous = p->current;
    for (;;) {
        p->current = scanner_next(&p->scanner);
        if (p->current.type != TOKEN_ERROR) break;
        /* Report scanner error and keep going */
        diag_emitf(DIAG_SCAN_ERROR, p->source_path,
                   p->current.line, p->current.column, 1, NULL,
                   "%.*s", p->current.length, p->current.start);
        p->had_error = true;
    }
}

/* Consume current if it matches type, else error */
bool consume(Parser *p, TokenType type, const char *msg) {
    if (p->current.type == type) {
        advance(p);
        return true;
    }
    error_at_current(p, msg);
    return false;
}

/* Check current token type without consuming */
bool check(Parser *p, TokenType type) {
    return p->current.type == type;
}

/* Consume if matches, return true if matched */
bool match_tok(Parser *p, TokenType type) {
    if (!check(p, type)) return false;
    advance(p);
    return true;
}

/* Skip any semicolons (optional statement terminator) */
void skip_semicolons(Parser *p) {
    while (p->current.type == TOKEN_SEMICOLON) {
        advance(p);
    }
}

/* Synchronize after error: advance to next statement boundary */
void synchronize(Parser *p) {
    p->panic_mode = false;
    while (p->current.type != TOKEN_EOF) {
        if (p->previous.type == TOKEN_SEMICOLON) return;
        switch (p->current.type) {
        case TOKEN_FN:
        case TOKEN_STRUCT:
        case TOKEN_ENUM:
        case TOKEN_IMPL:
        case TOKEN_TRAIT:
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

/* Error recovery inside a brace-delimited body (struct / enum / interface /
   methods / block). Like synchronize(), but GUARANTEES forward progress.
   synchronize() stops *without consuming* when the current token is a statement
   keyword (return / if / struct / ...). A body loop shaped like
       while (!check(RBRACE) && !check(EOF)) { ...; recover; continue; }
   then spins forever on that keyword, because the member/field parse keeps
   failing on it and synchronize keeps stopping on it (fuzz-found infinite
   loops: `struct P return`, `methods C if`). If synchronize made no progress
   and we are not sitting on a body terminator, skip one token so the enclosing
   loop always advances toward `}` / EOF. */
void recover_in_body(Parser *p) {
    const char *before = p->current.start;
    synchronize(p);
    if (p->current.start == before &&
        p->current.type != TOKEN_EOF &&
        p->current.type != TOKEN_RBRACE) {
        advance(p);
    }
}

/* ---- String Literal Processing ---- */

/* Process escape sequences in a string token (strips quotes) */
char *process_string_token(const char *start, int length) {
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

/* ---- Forward declarations ----
   Precedence lives in parser_internal.h now (needed by every TU that calls
   parse_expr_prec). parse_expr_prec/parse_statement/parse_block/parse_type/
   is_type_keyword are declared there too (non-static, cross-TU) — no local
   forward decl needed. PrefixFn/InfixFn/ParseRule are parser_expr.c-only
   and are declared there, next to the rules[] table that uses them. */

/* ---- Recursion depth guard --------------------------------------------
   Hard cap on nested expression/type/block recursion so pathological
   inputs fail with a diagnostic instead of overflowing the C stack (the
   stdlib text parsers cap at 128/256; the compiler parser gets the same
   discipline). One user-visible nesting level costs a few C frames, so
   256 levels stays well inside the default 1 MB Windows stack.

   parse_expr_prec / parse_type return NULL on exhaustion (their callers
   already handle NULL as an ordinary error path). parse_block instead
   reports, skips to a recovery point (so the enclosing statement loop
   always makes progress), and returns an empty block — its callers rely
   on it never returning NULL. */
#define LS_MAX_PARSE_DEPTH 256

/* parse_expr_prec_inner [def: parser_expr.c] / parse_block_inner
   [def: parser_stmt.c] / parse_type_inner [def: parser_type.c] are declared
   in parser_internal.h. */

static bool parse_depth_enter(Parser *p) {
    if (p->depth >= LS_MAX_PARSE_DEPTH) {
        error_at_current(p, "nesting too deep (limit 256)");
        return false;
    }
    p->depth++;
    return true;
}

AstNode *parse_expr_prec(Parser *p, Precedence min_prec) {
    if (!parse_depth_enter(p)) return NULL;
    AstNode *r = parse_expr_prec_inner(p, min_prec);
    p->depth--;
    return r;
}

TypeNode *parse_type(Parser *p) {
    if (!parse_depth_enter(p)) return NULL;
    TypeNode *r = parse_type_inner(p);
    p->depth--;
    return r;
}

AstNode *parse_block(Parser *p) {
    if (!parse_depth_enter(p)) {
        /* Consume tokens up to a sync point so the enclosing statement
           loop cannot spin on the same '{' forever, then yield an empty
           block (this function's callers never expect NULL). */
        recover_in_body(p);
        AstNode *n = new_node(AST_BLOCK, p->current.line, p->current.column);
        n->as.block.stmts = NULL;
        n->as.block.stmt_count = 0;
        return n;
    }
    AstNode *r = parse_block_inner(p);
    p->depth--;
    return r;
}

static AstNode *parse_if_stmt(Parser *p) {
    /* 'if' already consumed */
    int line = p->previous.line;
    int col  = p->previous.column;

    /* Parse the condition as a full expression. A leading '(' is handled as a
       normal grouping expression (so `if (a) {` and `if (a & b) != 0 {` both parse
       correctly) — do NOT special-case it as a whole-condition wrapper, which would
       stop after the first parenthesized sub-term (`(a&b)` then choke on `!= 0`). */
    AstNode *cond = parse_expr_prec(p, PREC_NONE);

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

    /* Full-expression condition; a leading '(' is a normal grouping (see
       parse_if_stmt) so `while (a & b) != 0 {` parses correctly. */
    AstNode *cond = parse_expr_prec(p, PREC_NONE);

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

AstNode *parse_block_inner(Parser *p) {
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
            recover_in_body(p);
            continue;
        }
        /* Nested top-level definitions inside a block are not supported
           (codegen has no place to emit them and would produce a basic
           block with no terminator). Reject cleanly here at parse time. */
        const char *nested_what = NULL;
        switch (stmt->kind) {
            case AST_FN_DECL:         nested_what = "function"; break;
            case AST_STRUCT_DECL:     nested_what = "struct"; break;
            case AST_ENUM_DECL:       nested_what = "enum"; break;
            case AST_IMPL_DECL:
            case AST_IMPL_TRAIT_DECL: nested_what = "methods"; break;
            case AST_TRAIT_DECL:      nested_what = "interface"; break;
            case AST_MODULE_DECL:     nested_what = "module"; break;
            default: break;
        }
        if (nested_what != NULL) {
            char msg[128];
            snprintf(msg, sizeof(msg),
                     "nested %s definition is not allowed inside a function "
                     "body; move it to the top level", nested_what);
            Token at = p->current;
            at.line = stmt->line;
            at.column = stmt->column;
            error_at(p, &at, msg);
            ast_free(stmt);
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

/* comptime field iteration (Stage 3b): the leading `comptime` keyword is already
   consumed. Parses either:
     comptime for <ident> in fields(<Type>) { body }   -> AST_COMPTIME_FOR
     comptime if <cond> { } [else { } | else comptime if ...]  -> AST_COMPTIME_IF
   Both are pure templates here; the checker unrolls them at instantiation time.
   `fields` is a contextual soft keyword (only recognized in this position), so
   ordinary identifiers/functions named `fields`/`_imm_fields` are unaffected. */
static AstNode *parse_comptime_stmt(Parser *p) {
    /* 'comptime' already consumed */
    int line = p->previous.line;
    int col  = p->previous.column;

    if (match_tok(p, TOKEN_FOR)) {
        if (!check(p, TOKEN_IDENTIFIER)) {
            error_at_current(p, "expected loop variable after 'comptime for'");
            return NULL;
        }
        advance(p);
        char *var = str_dup_n(p->previous.start, p->previous.length);

        consume(p, TOKEN_IN, "expected 'in' after comptime for variable");

        /* iterable: fields(Type) [struct fields] or variants(Type) [enum variants].
           Both are contextual soft keywords (only recognized in this position), so
           ordinary identifiers named fields/variants are unaffected. */
        bool over_variants = false;
        if (check(p, TOKEN_IDENTIFIER) && p->current.length == 6 &&
            strncmp(p->current.start, "fields", 6) == 0) {
            over_variants = false;
        } else if (check(p, TOKEN_IDENTIFIER) && p->current.length == 8 &&
                   strncmp(p->current.start, "variants", 8) == 0) {
            over_variants = true;
        } else {
            error_at_current(p,
                "expected 'fields(Type)' or 'variants(Type)' after 'in' in comptime for");
            free(var);
            return NULL;
        }
        advance(p); /* consume 'fields' / 'variants' */
        consume(p, TOKEN_LPAREN,
                over_variants ? "expected '(' after 'variants'"
                              : "expected '(' after 'fields'");
        TypeNode *over = parse_type(p);
        consume(p, TOKEN_RPAREN,
                over_variants ? "expected ')' after variants(Type)"
                              : "expected ')' after fields(Type)");

        AstNode *body = parse_block(p);

        AstNode *n = new_node(AST_COMPTIME_FOR, line, col);
        n->as.comptime_for.var          = var;
        n->as.comptime_for.over_type    = over;
        n->as.comptime_for.body         = body;
        n->as.comptime_for.over_variants = over_variants;
        return n;
    }

    if (match_tok(p, TOKEN_IF)) {
        /* Full-expression condition; a leading '(' is normal grouping (see
           parse_if_stmt). The predicate is required to be comptime-constant —
           the checker enforces that during unroll, not the parser. */
        AstNode *cond = parse_expr_prec(p, PREC_NONE);
        AstNode *then_block = parse_block(p);
        AstNode *else_block = NULL;
        if (match_tok(p, TOKEN_ELSE)) {
            if (match_tok(p, TOKEN_COMPTIME)) {
                /* else comptime if ... — chain (mirrors `else if`) */
                else_block = parse_comptime_stmt(p);
            } else {
                else_block = parse_block(p);
            }
        }
        AstNode *n = new_node(AST_COMPTIME_IF, line, col);
        n->as.comptime_if.cond       = cond;
        n->as.comptime_if.then_block = then_block;
        n->as.comptime_if.else_block = else_block;
        return n;
    }

    if (match_tok(p, TOKEN_MATCH)) {
        /* comptime match <expr> { <handle>(<binder>) => <body> }
           A single generic arm expanded once per enum variant at unroll. The
           handle (e.g. vr) exposes vr.name/index/has_payload/payload_count/
           type_name; the binder (e.g. p) binds the active variant's first
           payload. */
        AstNode *subject = parse_expr_prec(p, PREC_NONE);
        consume(p, TOKEN_LBRACE, "expected '{' after comptime match subject");

        if (!check(p, TOKEN_IDENTIFIER)) {
            error_at_current(p, "expected a variant handle (e.g. 'vr') in comptime match arm");
            ast_free(subject);
            return NULL;
        }
        char *handle = str_dup_n(p->current.start, p->current.length);
        advance(p);

        char *binder = NULL;
        if (match_tok(p, TOKEN_LPAREN)) {
            if (!check(p, TOKEN_IDENTIFIER)) {
                error_at_current(p, "expected a payload binder (e.g. 'p') after '('");
                free(handle); ast_free(subject);
                return NULL;
            }
            binder = str_dup_n(p->current.start, p->current.length);
            advance(p);
            consume(p, TOKEN_RPAREN, "expected ')' after comptime match payload binder");
        }

        consume(p, TOKEN_FAT_ARROW, "expected '=>' after comptime match handle");
        AstNode *body;
        if (check(p, TOKEN_LBRACE)) {
            body = parse_block(p);
        } else {
            body = parse_expr_prec(p, PREC_NONE);
        }
        match_tok(p, TOKEN_COMMA); /* optional trailing comma */
        consume(p, TOKEN_RBRACE, "expected '}' to close comptime match (one arm only)");

        AstNode *n = new_node(AST_COMPTIME_MATCH, line, col);
        n->as.comptime_match.subject = subject;
        n->as.comptime_match.handle  = handle;
        n->as.comptime_match.binder  = binder;
        n->as.comptime_match.body    = body;
        return n;
    }

    /* Otherwise: a comptime constant declaration —
       `comptime <type> <name> = <const-expr | comptime { ... }>`.
       Compile-time const-eval (docs/plan_comptime_consteval.md). Step 1 only
       parses; the checker evaluates and materializes later. global-vs-local is
       decided by the checker from scope depth, so the parser leaves is_global
       false here. */
    {
        TypeNode *cty = parse_type(p);
        if (cty == NULL) {
            /* parse_type already reported a diagnostic; add the comptime hint. */
            error_at_current(p,
                "expected 'for', 'if', 'match', or '<type> <name> = ...' after 'comptime'");
            return NULL;
        }
        if (!check(p, TOKEN_IDENTIFIER)) {
            error_at_current(p, "expected a name after 'comptime <type>'");
            type_node_free(cty);
            return NULL;
        }
        advance(p);
        char *cname = str_dup_n(p->previous.start, p->previous.length);

        consume(p, TOKEN_ASSIGN, "expected '=' in comptime constant declaration");

        AstNode *value = NULL;
        if (check(p, TOKEN_COMPTIME)) {
            /* block form: comptime { ... return v } */
            advance(p); /* consume the inner 'comptime' */
            if (!check(p, TOKEN_LBRACE)) {
                error_at_current(p, "expected '{' after 'comptime' (comptime block)");
                free(cname);
                type_node_free(cty);
                return NULL;
            }
            AstNode *blk = parse_block(p);
            AstNode *cb = new_node(AST_COMPTIME_BLOCK, line, col);
            cb->as.comptime_block.block = blk;
            value = cb;
        } else {
            value = parse_expr_prec(p, PREC_NONE);
        }
        skip_semicolons(p);

        AstNode *n = new_node(AST_COMPTIME_CONST, line, col);
        n->as.comptime_const.decl_type = cty;
        n->as.comptime_const.name      = cname;
        n->as.comptime_const.value     = value;
        n->as.comptime_const.is_global = false; /* checker fills from scope depth */
        return n;
    }
}

AstNode *parse_statement(Parser *p) {
    skip_semicolons(p);
    int line = p->current.line;
    int col  = p->current.column;

    /* Declaration attribute: @derive(Trait, ...) applied to the next struct/enum.
       (@time/@bench are separate expression-level tokens handled in the Pratt
       table; this bare '@' path is the general declaration-attribute mechanism.) */
    if (match_tok(p, TOKEN_AT)) {
        if (!check(p, TOKEN_IDENTIFIER)) {
            error_at_current(p, "expected attribute name after '@'");
            return NULL;
        }
        char *attr = str_dup_n(p->current.start, p->current.length);
        advance(p);

        char **names = NULL;
        int name_count = 0, name_cap = 0;
        if (match_tok(p, TOKEN_LPAREN)) {
            if (!check(p, TOKEN_RPAREN)) {
                do {
                    if (!check(p, TOKEN_IDENTIFIER)) {
                        error_at_current(p, "expected a trait name in @derive(...)");
                        break;
                    }
                    if (name_count >= name_cap) {
                        name_cap = GROW_CAPACITY(name_cap);
                        names = GROW_ARRAY(char *, names, name_cap);
                    }
                    names[name_count++] = str_dup_n(p->current.start, p->current.length);
                    advance(p);
                } while (match_tok(p, TOKEN_COMMA));
            }
            if (!match_tok(p, TOKEN_RPAREN))
                error_at_current(p, "expected ')' after @derive(...) arguments");
        }

        bool is_derive = (strcmp(attr, "derive") == 0);
        free(attr);

        /* Parse the declaration the attribute decorates and attach the list. */
        AstNode *decl = parse_statement(p);
        if (!is_derive) {
            error_at_current(p, "unknown attribute (only @derive is supported)");
        } else if (decl != NULL && decl->kind == AST_STRUCT_DECL) {
            decl->as.struct_decl.derives = names;
            decl->as.struct_decl.derive_count = name_count;
            names = NULL;
        } else if (decl != NULL && decl->kind == AST_ENUM_DECL) {
            decl->as.enum_decl.derives = names;
            decl->as.enum_decl.derive_count = name_count;
            names = NULL;
        } else if (decl != NULL) {
            error_at_current(p, "@derive can only be applied to a struct or enum");
        }
        if (names != NULL) {
            for (int i = 0; i < name_count; i++) free(names[i]);
            free(names);
        }
        return decl;
    }

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
            return parse_fn_decl(p, /*allow_operator_name=*/false);
        }
        /* else fall through to expression statement (closure) */
    } else if (match_tok(p, TOKEN_STRUCT)) {
        return parse_struct_decl(p);
    } else if (match_tok(p, TOKEN_ENUM)) {
        return parse_enum_decl(p);
    } else if (match_tok(p, TOKEN_IMPL)) {
        return parse_impl_decl(p);
    } else if (match_tok(p, TOKEN_TRAIT)) {
        return parse_trait_decl(p);
    } else if (match_tok(p, TOKEN_MODULE)) {
        return parse_module_decl(p);
    } else if (match_tok(p, TOKEN_IMPORT)) {
        return parse_import_decl(p);
    } else if (match_tok(p, TOKEN_TYPE_ALIAS)) {
        return parse_type_alias_decl(p);
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
    } else if (match_tok(p, TOKEN_COMPTIME)) {
        return parse_comptime_stmt(p);
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
        /* Statement boundary: a following statement may legitimately start with
           a pointer declaration, so permit the `*Ident Ident` decl split here. */
        p->stmt_boundary = true;
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
    Parser p;
    memset(&p, 0, sizeof(p));
    scanner_init(&p.scanner, source);
    p.source_path = source_path;
    p.had_error = false;
    p.panic_mode = false;
    p.in_return_type = false;

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
