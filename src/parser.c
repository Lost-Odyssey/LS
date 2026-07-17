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

/* Shared driver for the "loop until '}' or EOF, parsing one AstNode member
   per iteration into a growable array" shape that recurs across struct/enum/
   interface/methods/block body parsers (Task 4.4,
   docs/plan_arch_round2_backlog.md Batch 4). Converges ONLY the sites whose
   per-member logic reduces to a single `member_fn` call returning either a
   fully-built AstNode* to append, or NULL to trigger `recover_in_body`+retry
   — i.e. sites shaped exactly like:
       while (!check(close) && !check(EOF)) {
           AstNode *m = <parse one member, using ctx for any per-site state>;
           if (m == NULL) { recover_in_body(p); continue; }
           <grow array>; items[count++] = m;
       }
   Sites with extra control flow between "member parsed" and "member
   appended" (e.g. parser_stmt.c's parse_block_inner, which can reject and
   discard a successfully-parsed nested declaration without recovering) do
   NOT fit this contract and are left as hand-written loops on purpose.
   NOTE: *out_items may be NULL when *out_count is 0 (empty body) — callers
   must guard element access with the count, as all five current sites do. */
void parse_body_items(Parser *p, TokenType close,
                       AstNode *(*member_fn)(Parser *p, void *ctx), void *ctx,
                       AstNode ***out_items, int *out_count) {
    AstNode **items = NULL;
    int count = 0;
    int cap = 0;
    while (!check(p, close) && !check(p, TOKEN_EOF)) {
        AstNode *item = member_fn(p, ctx);
        if (item == NULL) {
            recover_in_body(p);
            continue;
        }
        if (count >= cap) {
            cap = GROW_CAPACITY(cap);
            items = GROW_ARRAY(AstNode *, items, cap);
        }
        items[count++] = item;
    }
    *out_items = items;
    *out_count = count;
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
