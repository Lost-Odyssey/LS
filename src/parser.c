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

static void error_at(Parser *p, Token *tok, const char *msg) {
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

/* ---- Type Parser ---- */

bool is_type_keyword(TokenType t) {
    switch (t) {
    case TOKEN_TYPE_INT: case TOKEN_TYPE_I8:  case TOKEN_TYPE_I16:
    case TOKEN_TYPE_I32: case TOKEN_TYPE_I64: case TOKEN_TYPE_U8:
    case TOKEN_TYPE_U16: case TOKEN_TYPE_U32: case TOKEN_TYPE_U64:
    case TOKEN_TYPE_F32: case TOKEN_TYPE_F64:
    case TOKEN_TYPE_F16: case TOKEN_TYPE_BF16: case TOKEN_TYPE_BOOL:
    case TOKEN_TYPE_CHAR:
    case TOKEN_TYPE_VOID:
    case TOKEN_TYPE_OBJECT:
    case TOKEN_ARRAY:
    case TOKEN_SIMD:
        return true;
    default:
        return false;
    }
}

TypeNode *parse_type_inner(Parser *p) {
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
        bool is_mut = match_tok(p, TOKEN_BANG);  /* &! => writable reference/slice */
        /* &array(T) / &!array(T) — borrowed slice: a {ptr,len} view over a
           contiguous range. Mirrors LS's `array(T, N)` (owned, fixed) minus the
           size: a slice is a "borrowed unsized array". Distinguished from
           `array(T, N)` by the `&` and the absence of `, N`. */
        if (check(p, TOKEN_ARRAY)) {
            advance(p);  /* consume 'array' */
            if (!consume(p, TOKEN_LPAREN, "expected '(' after 'array' in slice type"))
                return NULL;
            TypeNode *elem = parse_type(p);
            if (elem == NULL) return NULL;
            if (check(p, TOKEN_COMMA)) {
                error_at_current(p, "a slice `&array(T)` has no size; write "
                                    "`array(T, N)` for an owned fixed array "
                                    "(borrowing a fixed array is not supported)");
                type_node_free(elem);
                return NULL;
            }
            if (!consume(p, TOKEN_RPAREN, "expected ')' to close slice type '&array(T)'")) {
                type_node_free(elem);
                return NULL;
            }
            TypeNode *tn = new_type_node(TYPE_NODE_SLICE, line, col);
            tn->is_mut = is_mut;
            tn->as.array.elem = elem;
            return tn;
        }
        TypeNode *pointee = parse_type(p);
        if (pointee == NULL) return NULL;
        TypeNode *tn = new_type_node(TYPE_NODE_REFERENCE, line, col);
        tn->is_mut = is_mut;
        tn->as.pointee = pointee;
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

    /* Simd(T, N) — portable SIMD vector (mirrors array(T, N); N = lane count) */
    if (match_tok(p, TOKEN_SIMD)) {
        consume(p, TOKEN_LPAREN, "expected '(' after 'Simd'");
        TypeNode *elem = parse_type(p);
        if (elem == NULL) return NULL;
        consume(p, TOKEN_COMMA, "expected ',' after Simd element type");
        if (!check(p, TOKEN_INT_LIT)) {
            error_at_current(p, "Simd lane count must be an integer literal");
            type_node_free(elem);
            return NULL;
        }
        advance(p);
        long long lanes = 0;
        for (int i = 0; i < p->previous.length; i++) {
            lanes = lanes * 10 + (p->previous.start[i] - '0');
        }
        if (lanes <= 0) {
            error_at(p, &p->previous, "Simd lane count must be positive");
            type_node_free(elem);
            return NULL;
        }
        consume(p, TOKEN_RPAREN, "expected ')' after Simd lane count");
        TypeNode *tn = new_type_node(TYPE_NODE_SIMD, line, col);
        tn->as.array.elem = elem;
        tn->as.array.size = (int)lanes;
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

    /* Block(types) -> ret — closure type (Phase A). Forbidden in return
       position; the user must define a type alias instead. */
    if (check(p, TOKEN_BLOCK)) {
        if (p->in_return_type) {
            error_at_current(p,
                "Block type cannot appear directly here (return type / struct "
                "field / nested closure type); define a type alias first "
                "(e.g. `type Adder = Block(int) -> int`)");
            advance(p); /* consume 'Block' to avoid cascading errors */
            /* Best-effort: skip through balanced parens then optional -> T */
            if (check(p, TOKEN_LPAREN)) {
                advance(p);
                int depth = 1;
                while (depth > 0 && !check(p, TOKEN_EOF)) {
                    if (check(p, TOKEN_LPAREN)) depth++;
                    else if (check(p, TOKEN_RPAREN)) { depth--; if (depth == 0) { advance(p); break; } }
                    advance(p);
                }
            }
            if (check(p, TOKEN_ARROW)) { advance(p); type_node_free(parse_type(p)); }
            return NULL;
        }
        advance(p); /* consume 'Block' */
        consume(p, TOKEN_LPAREN, "expected '(' after 'Block'");
        TypeNode **params = NULL;
        int param_count = 0;
        int param_cap = 0;
        if (!check(p, TOKEN_RPAREN)) {
            do {
                /* Inside Block(...) parameter list, return-type guard does
                   not apply — Block params can themselves be Block. */
                bool save = p->in_return_type;
                p->in_return_type = false;
                TypeNode *pt = parse_type(p);
                p->in_return_type = save;
                if (pt == NULL) break;
                if (param_count >= param_cap) {
                    param_cap = GROW_CAPACITY(param_cap);
                    params = GROW_ARRAY(TypeNode *, params, param_cap);
                }
                params[param_count++] = pt;
            } while (match_tok(p, TOKEN_COMMA));
        }
        consume(p, TOKEN_RPAREN, "expected ')' in Block type");
        TypeNode *ret = NULL;
        if (match_tok(p, TOKEN_ARROW)) {
            /* Disallow chained `Block(...) -> Block(...)` in any position —
               require a type alias for the inner Block too. */
            bool save = p->in_return_type;
            p->in_return_type = true;
            ret = parse_type(p);
            p->in_return_type = save;
        }
        TypeNode *tn = new_type_node(TYPE_NODE_BLOCK, line, col);
        tn->as.fn.params = params;
        tn->as.fn.param_count = param_count;
        tn->as.fn.ret = ret;
        return tn;
    }

    /* fn(types) -> ret */
    if (match_tok(p, TOKEN_FN)) {
        consume(p, TOKEN_LPAREN, "expected '(' after 'def' in type");
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

    /* Named type (user struct or generic instantiation like Option(int)),
       optionally module-qualified: `mod.Type`, `A.Type`, `std.json.Value`.
       B-4: an IDENT followed by `.IDENT` (one or more) is a qualified type — the
       trailing segment is the type name, the leading dotted path is the module
       qualifier (module path or import alias). */
    if (check(p, TOKEN_IDENTIFIER)) {
        advance(p);
        Token name_tok = p->previous;
        TypeNode *tn = new_type_node(TYPE_NODE_NAMED, line, col);
        tn->as.named.name = str_dup_n(name_tok.start, name_tok.length);
        tn->as.named.module = NULL;
        tn->as.named.args = NULL;
        tn->as.named.arg_count = 0;

        /* B-4: consume `.IDENT` chain → module-qualified type. Accumulate all but
           the last segment into `module` (dot-separated), keep the last as `name`. */
        while (check(p, TOKEN_DOT)) {
            advance(p); /* consume '.' */
            if (!check(p, TOKEN_IDENTIFIER)) {
                error_at_current(p, "expected identifier after '.' in qualified type");
                type_node_free(tn);
                return NULL;
            }
            advance(p);
            Token seg = p->previous;
            /* current `name` becomes part of the module path */
            size_t mlen = tn->as.named.module ? strlen(tn->as.named.module) : 0;
            size_t nlen = strlen(tn->as.named.name);
            char *newmod = (char *)malloc_safe(mlen + (mlen ? 1 : 0) + nlen + 1);
            newmod[0] = '\0';
            if (mlen) { memcpy(newmod, tn->as.named.module, mlen); newmod[mlen] = '.'; mlen++; }
            memcpy(newmod + mlen, tn->as.named.name, nlen);
            newmod[mlen + nlen] = '\0';
            free(tn->as.named.module);
            free(tn->as.named.name);
            tn->as.named.module = newmod;
            tn->as.named.name = str_dup_n(seg.start, seg.length);
        }

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
bool starts_var_decl(Parser *p) {
    TokenType cur = p->current.type;

    /* Direct built-in type keyword */
    if (is_type_keyword(cur)) return true;

    /* Phase 1 (borrow extension): `&T name` / `&!T name` — a named local borrow
       declaration. Disambiguate from the expression forms `&x` / `&!x`: strip
       the borrow prefix and check whether the remainder looks like a typed var
       decl (`T name <term>`). `&x` (address-of, IDENT NOT followed by another
       type+name) stays an expression. */
    if (cur == TOKEN_AMP) {
        Scanner saved = p->scanner;
        Token saved_cur = p->current;
        Token saved_prev = p->previous;
        advance(p);                              /* consume '&' */
        if (p->current.type == TOKEN_BANG)       /* '&!' writable borrow */
            advance(p);
        /* Reuse the full type+name disambiguation on the remaining tokens. */
        bool result = starts_var_decl(p);
        p->scanner = saved;
        p->current = saved_cur;
        p->previous = saved_prev;
        return result;
    }

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

        /* B-4: module-qualified type var decl — `mod.Type x`, `std.json.Value v`.
           Scan a `.IDENT` chain; if it ends with an IDENTIFIER (the var name) on
           the SAME LINE, it's a var decl. `A.foo()` / `A.foo` / `A.b = x` (next
           token after the dotted path is '(' / '=' / nothing) stay expressions. */
        if (next.type == TOKEN_DOT) {
            Scanner saved = p->scanner;
            Token saved_cur = p->current;
            Token saved_prev = p->previous;
            advance(p);  /* consume leading IDENT; current = '.' */
            bool result = false;
            while (p->current.type == TOKEN_DOT) {
                advance(p);  /* consume '.' */
                if (p->current.type != TOKEN_IDENTIFIER) { result = false; break; }
                advance(p);  /* consume IDENT segment; current = next token */
                if (p->current.type == TOKEN_IDENTIFIER &&
                    p->current.line == saved_cur.line) {
                    /* `... .Seg varname` → qualified type + var name */
                    result = true;
                    break;
                }
                if (p->current.type == TOKEN_LPAREN) {
                    /* `... .Seg(args) varname` → qualified GENERIC type + var name,
                       e.g. `st.Stack(int) s`. Skip the balanced type-arg parens,
                       then require an IDENT var name on the same line. */
                    advance(p);  /* consume '(' */
                    int gdepth = 1;
                    /* L-005: a generic type-arg list contains only types (keywords /
                       identifiers / `,` / nested `(...)` / `*` / `&`), never value
                       literals. If a literal appears inside the parens, this is a
                       method call `recv.method(1, 2)` (possibly followed on the same
                       line by another statement `… a = …`), NOT a qualified generic
                       type decl — so do not treat it as a var decl. */
                    bool gen_has_literal = false;
                    bool gen_saw_token = false;  /* any token inside the parens */
                    while (gdepth > 0 && p->current.type != TOKEN_EOF) {
                        if (p->current.type == TOKEN_LPAREN) gdepth++;
                        else if (p->current.type == TOKEN_RPAREN) {
                            gdepth--;
                            if (gdepth == 0) break;
                        }
                        else {
                            gen_saw_token = true;
                            if (p->current.type == TOKEN_INT_LIT ||
                                p->current.type == TOKEN_FLOAT_LIT ||
                                p->current.type == TOKEN_STRING_LIT ||
                                p->current.type == TOKEN_CHAR_LIT ||
                                p->current.type == TOKEN_TRUE ||
                                p->current.type == TOKEN_FALSE)
                                gen_has_literal = true;
                        }
                        advance(p);
                    }
                    /* A generic type-arg list is non-empty and contains only types
                       (no value literals). Empty parens `recv.method()` or literal
                       args `recv.method(1,2)` mean a method call, not a type decl. */
                    if (p->current.type == TOKEN_RPAREN && gen_saw_token &&
                        !gen_has_literal) {
                        advance(p);  /* consume ')' */
                        /* Require IDENT var name on the same line AND the token
                           after it to be '=' / ';' / EOF — otherwise this is a
                           qualified method call like `r.append(x)` (possibly
                           followed on the same line by another statement
                           `r.append(y)`), NOT a var decl. Mirrors the safeguard
                           on the non-qualified `Foo(args) var` branch below. */
                        if (p->current.type == TOKEN_IDENTIFIER &&
                            p->current.line == saved_cur.line) {
                            Token after = scanner_peek(&p->scanner);
                            /* M-DEF: a no-init decl `mod.Type(args) v` is terminated
                               by a newline (after-token on a different line), a '}'
                               (last stmt in block) or EOF — accept those alongside
                               the explicit '='/';' forms. The same-line guard above
                               keeps adjacent statements (`a.f(x) b.g(y)`) as exprs. */
                            if (after.type == TOKEN_ASSIGN ||
                                after.type == TOKEN_SEMICOLON ||
                                after.type == TOKEN_EOF ||
                                after.type == TOKEN_RBRACE ||
                                after.line != p->current.line)
                                result = true;
                        }
                    }
                    break;
                }
                /* otherwise keep scanning the dotted chain (e.g. std.json.Value) */
            }
            p->scanner = saved;
            p->current = saved_cur;
            p->previous = saved_prev;
            return result;
        }

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
                /* Need: IDENT then '=' or ';' to qualify as a var decl.
                   Also require the variable name is on the SAME LINE as the
                   type — this prevents mistaking `fn(cur(10))` followed by
                   `varname =` on the NEXT line (a separate statement) from
                   being mis-parsed as a generic type declaration. */
                advance(p);  /* consume ')' */
                if (p->current.type == TOKEN_IDENTIFIER &&
                    p->current.line == saved_cur.line)
                {
                    Token after = scanner_peek(&p->scanner);
                    /* M-DEF: `Foo(args) v` with no initializer is a valid decl
                       (`T v` ≡ `T v = {}`). It is terminated by a newline
                       (after-token on a different line than the var name), a '}'
                       (last stmt of a block) or EOF, in addition to the explicit
                       '='/';' forms. The same-line var-name guard above keeps
                       adjacent expr statements like `print(a1) print(a2)` (the
                       second name is followed by '(' on the SAME line) as exprs. */
                    if (after.type == TOKEN_ASSIGN ||
                        after.type == TOKEN_SEMICOLON ||
                        after.type == TOKEN_EOF ||
                        after.type == TOKEN_RBRACE ||
                        after.line != p->current.line)
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

AstNode *parse_var_decl(Parser *p) {
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

static bool is_literal_default(const AstNode *e);  /* fwd decl */

/* Parse parameter list into arrays. Returns true on success. */
static bool parse_param_list(Parser *p,
                              TypeNode ***out_types, char ***out_names,
                              int *out_count,
                              bool *out_is_vararg,
                              AstNode ***out_defaults) {
    TypeNode **param_types = NULL;
    char **param_names = NULL;
    AstNode **param_defaults = NULL;
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
                    if (param_defaults) ast_free(param_defaults[i]);
                }
                free(param_types);
                free(param_names);
                free(param_defaults);
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
            /* Optional default value: `= <literal | [..] | Foo{..}>`. */
            AstNode *pdefault = NULL;
            if (match_tok(p, TOKEN_ASSIGN)) {
                pdefault = parse_expr_prec(p, PREC_ASSIGNMENT);
                if (pdefault != NULL && !is_literal_default(pdefault)) {
                    error_at_current(p, "function parameter default must be a literal, "
                                        "vec literal ([..]), or struct literal (Foo{..})");
                    ast_free(pdefault);
                    pdefault = NULL;
                }
            }
            if (param_count >= param_cap) {
                param_cap = GROW_CAPACITY(param_cap);
                param_types = GROW_ARRAY(TypeNode *, param_types, param_cap);
                param_names = GROW_ARRAY(char *, param_names, param_cap);
                param_defaults = GROW_ARRAY(AstNode *, param_defaults, param_cap);
            }
            param_types[param_count] = pt;
            param_names[param_count] = pname;
            param_defaults[param_count] = pdefault;
            param_count++;
        } while (match_tok(p, TOKEN_COMMA));
    }

    *out_types = param_types;
    *out_names = param_names;
    *out_count = param_count;
    if (out_is_vararg) *out_is_vararg = is_vararg;
    if (out_defaults) *out_defaults = param_defaults;
    else free(param_defaults);
    return true;
}

/* ---- parse_fn_signature ---- */
/* Parse a function signature without body, used in trait declarations.
   Expects 'fn' already consumed. Returns AST_FN_DECL with body=NULL. */
static AstNode *parse_fn_signature(Parser *p) {
    int line = p->previous.line;
    int col  = p->previous.column;

    if (!check(p, TOKEN_IDENTIFIER)) {
        error_at_current(p, "expected method name after 'def' in interface");
        return NULL;
    }
    advance(p);
    char *name = str_dup_n(p->previous.start, p->previous.length);

    consume(p, TOKEN_LPAREN, "expected '(' after method name");

    /* Parse &self / &!self (same logic as parse_fn_decl) */
    int self_borrow_kind = 0;
    if (check(p, TOKEN_AMP)) {
        Token next = scanner_peek(&p->scanner);
        bool is_self_borrow = false;
        bool is_mut = false;
        if (next.type == TOKEN_SELF) {
            is_self_borrow = true;
        } else if (next.type == TOKEN_BANG) {
            Scanner saved = p->scanner;
            Token saved_cur = p->current;
            Token saved_prev = p->previous;
            advance(p); /* consume & */
            advance(p); /* consume ! */
            if (check(p, TOKEN_SELF)) {
                is_self_borrow = true;
                is_mut = true;
            }
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
    AstNode **param_defaults = NULL;
    int param_count = 0;
    if (!parse_param_list(p, &param_types, &param_names, &param_count, NULL, &param_defaults)) {
        free(name);
        return NULL;
    }
    consume(p, TOKEN_RPAREN, "expected ')' after parameters");

    TypeNode *return_type = NULL;
    if (match_tok(p, TOKEN_ARROW)) {
        bool save = p->in_return_type;
        p->in_return_type = true;
        return_type = parse_type(p);
        p->in_return_type = save;
    }

    /* No body — signature only */
    AstNode *n = new_node(AST_FN_DECL, line, col);
    n->as.fn_decl.name = name;
    n->as.fn_decl.type_params = NULL;
    n->as.fn_decl.type_param_count = 0;
    n->as.fn_decl.param_types = param_types;
    n->as.fn_decl.param_names = param_names;
    n->as.fn_decl.param_defaults = param_defaults;
    n->as.fn_decl.param_count = param_count;
    n->as.fn_decl.return_type = return_type;
    n->as.fn_decl.body = NULL;
    n->as.fn_decl.is_static = false;
    n->as.fn_decl.impl_struct_name = NULL;
    n->as.fn_decl.self_borrow_kind = self_borrow_kind;
    return n;
}

/* Operator overloading: map an operator token to its internal method name
   ($op_*). Returns a malloc'd string, or NULL if the token is not an
   overloadable operator. The '$' sigil cannot appear in a user identifier
   (scanner only accepts isalnum/_), so these names never collide with
   user-written methods. */
static char *operator_method_name(TokenType t) {
    const char *s = NULL;
    switch (t) {
    case TOKEN_PLUS:    s = "$op_add"; break;
    case TOKEN_MINUS:   s = "$op_sub"; break;
    case TOKEN_STAR:    s = "$op_mul"; break;
    case TOKEN_SLASH:   s = "$op_div"; break;
    case TOKEN_PERCENT: s = "$op_rem"; break;
    case TOKEN_EQ:      s = "$op_eq";  break;
    case TOKEN_NEQ:     s = "$op_ne";  break;
    case TOKEN_LT:      s = "$op_lt";  break;
    case TOKEN_GT:      s = "$op_gt";  break;
    case TOKEN_LEQ:     s = "$op_le";  break;
    case TOKEN_GEQ:     s = "$op_ge";  break;
    default: return NULL;
    }
    return str_dup_n(s, (int)strlen(s));
}

static bool check_ident_text(Parser *p, const char *text) {
    size_t n = strlen(text);
    return check(p, TOKEN_IDENTIFIER) &&
           (size_t)p->current.length == n &&
           strncmp(p->current.start, text, n) == 0;
}

static WhereBound *parse_where_bounds(Parser *p, int *out_count) {
    *out_count = 0;
    if (!check_ident_text(p, "where"))
        return NULL;
    advance(p); /* consume where */

    WhereBound *bounds = NULL;
    int count = 0;
    int cap = 0;
    do {
        if (!check(p, TOKEN_IDENTIFIER)) {
            error_at_current(p, "expected type parameter name after 'where'");
            break;
        }
        advance(p);
        char *tpname = str_dup_n(p->previous.start, p->previous.length);
        consume(p, TOKEN_COLON, "expected ':' in where clause");

        if (count >= cap) {
            cap = cap < 4 ? 4 : cap * 2;
            bounds = realloc_safe(bounds, (size_t)cap * sizeof(bounds[0]));
        }
        bounds[count].type_param_name = tpname;
        bounds[count].bounds.trait_names = NULL;
        bounds[count].bounds.count = 0;

        int bcap = 0;
        do {
            if (!check(p, TOKEN_IDENTIFIER)) {
                error_at_current(p, "expected interface name in where clause");
                break;
            }
            advance(p);
            char *tname = str_dup_n(p->previous.start, p->previous.length);
            int bc = bounds[count].bounds.count;
            if (bc >= bcap) {
                bcap = bcap < 4 ? 4 : bcap * 2;
                bounds[count].bounds.trait_names =
                    realloc_safe(bounds[count].bounds.trait_names,
                                 (size_t)bcap * sizeof(char *));
            }
            bounds[count].bounds.trait_names[bc] = tname;
            bounds[count].bounds.count++;
        } while (match_tok(p, TOKEN_PLUS));

        count++;
    } while (match_tok(p, TOKEN_COMMA));

    *out_count = count;
    return bounds;
}

/* allow_operator_name: when true (only inside `impl Trait for Type` blocks),
   an operator token is accepted as the method name and canonicalized to its
   $op_* internal name. Elsewhere only TOKEN_IDENTIFIER is a valid name, so
   top-level `fn +` and symbol names in plain `impl`/`trait` bodies are rejected. */
AstNode *parse_fn_decl(Parser *p, bool allow_operator_name) {
    /* 'fn' already consumed */
    int line = p->previous.line;
    int col  = p->previous.column;

    char *name = NULL;
    if (check(p, TOKEN_IDENTIFIER)) {
        advance(p);
        name = str_dup_n(p->previous.start, p->previous.length);
        /* Retired: `__drop`/`__clone` are no longer user-writable method names.
           Define the destructor as `~` in a `methods X: Destroy {}` block, and the
           deep-copy hook as `clone` in a `methods X: Clone {}` block. */
        if (strcmp(name, "__drop") == 0 || strcmp(name, "__clone") == 0) {
            error_at_current(p,
                strcmp(name, "__drop") == 0
                    ? "`__drop` is retired; use the `~` destructor in a `methods X: Destroy {}` block"
                    : "`__clone` is retired; use `def clone` in a `methods X: Clone {}` block");
            free(name);
            return NULL;
        }
    } else if (check(p, TOKEN_NEW)) {
        /* `new` is reserved for the allocation prefix, but is a valid method
           name (Ruby-style `Task.new` constructor); unambiguous after `fn`. */
        advance(p);
        name = str_dup_n(p->previous.start, p->previous.length); /* "new" */
    } else if (allow_operator_name && check(p, TOKEN_TILDE)) {
        /* C++-style `~` destructor method (only inside `methods X: Destroy {}`).
           Lowers to the internal `__drop` name so every existing RAII mechanism
           (auto field-drop, scope cleanup, manual-call rejection) applies as-is. */
        advance(p);
        name = str_dup_n("__drop", 6);
    } else if (allow_operator_name &&
               (name = operator_method_name(p->current.type)) != NULL) {
        advance(p);  /* consume the operator token used as the method name */
    } else {
        error_at_current(p, "expected function name after 'def'");
        return NULL;
    }

    /* G2: optional type parameter list — fn name(T, U)(params...) -> R { ... }
       Disambiguate: if '(' followed by uppercase identifier then ',' or ')' or ':' → type params. */
    char **fn_type_params = NULL;
    int    fn_type_param_count = 0;
    /* Trait bounds per type param (parallel array); NULL if no bounds anywhere. */
    TypeParamBound *fn_type_param_bounds = NULL;
    if (check(p, TOKEN_LPAREN)) {
        /* Peek at what follows '(' */
        Scanner saved_sc = p->scanner;
        Token   saved_cur = p->current;
        Token   saved_prev = p->previous;
        advance(p); /* consume '(' */
        if (check(p, TOKEN_IDENTIFIER)
            && p->current.length > 0
            && p->current.start[0] >= 'A' && p->current.start[0] <= 'Z') {
            /* Looks like type params. Peek further: after ident must be ',' or ')' or ':'. */
            advance(p); /* consume ident */
            if (check(p, TOKEN_COMMA) || check(p, TOKEN_RPAREN) || check(p, TOKEN_COLON)) {
                /* Confirmed: this is a type parameter list. Restore and parse. */
                p->scanner = saved_sc;
                p->current = saved_cur;
                p->previous = saved_prev;
                advance(p); /* consume '(' */
                int cap = 0;
                bool has_any_bounds = false;
                do {
                    if (!check(p, TOKEN_IDENTIFIER)) {
                        error_at_current(p, "expected type parameter name");
                        break;
                    }
                    advance(p);
                    char *tp = str_dup_n(p->previous.start, p->previous.length);
                    if (fn_type_param_count >= cap) {
                        cap = cap < 4 ? 4 : cap * 2;
                        fn_type_params = realloc(fn_type_params, (size_t)cap * sizeof(char *));
                        fn_type_param_bounds = realloc(fn_type_param_bounds,
                            (size_t)cap * sizeof(fn_type_param_bounds[0]));
                    }
                    fn_type_params[fn_type_param_count] = tp;
                    fn_type_param_bounds[fn_type_param_count].trait_names = NULL;
                    fn_type_param_bounds[fn_type_param_count].count = 0;

                    /* Parse optional trait bounds: T: Trait1 + Trait2 */
                    if (match_tok(p, TOKEN_COLON)) {
                        has_any_bounds = true;
                        int bcap = 0;
                        do {
                            if (!check(p, TOKEN_IDENTIFIER)) {
                                error_at_current(p, "expected interface name after ':'");
                                break;
                            }
                            advance(p);
                            char *tname = str_dup_n(p->previous.start, p->previous.length);
                            int bc = fn_type_param_bounds[fn_type_param_count].count;
                            if (bc >= bcap) {
                                bcap = bcap < 4 ? 4 : bcap * 2;
                                fn_type_param_bounds[fn_type_param_count].trait_names =
                                    realloc(fn_type_param_bounds[fn_type_param_count].trait_names,
                                            (size_t)bcap * sizeof(char *));
                            }
                            fn_type_param_bounds[fn_type_param_count].trait_names[bc] = tname;
                            fn_type_param_bounds[fn_type_param_count].count++;
                        } while (match_tok(p, TOKEN_PLUS));
                    }
                    fn_type_param_count++;
                } while (match_tok(p, TOKEN_COMMA));
                /* If no bounds were used at all, free the array to keep it NULL */
                if (!has_any_bounds) {
                    free(fn_type_param_bounds);
                    fn_type_param_bounds = NULL;
                }
                consume(p, TOKEN_RPAREN, "expected ')' after type parameters");
                /* Now the next '(' is the regular parameter list — fall through */
            } else {
                /* Not type params — restore everything */
                p->scanner = saved_sc;
                p->current = saved_cur;
                p->previous = saved_prev;
            }
        } else {
            /* Not uppercase ident — restore */
            p->scanner = saved_sc;
            p->current = saved_cur;
            p->previous = saved_prev;
        }
    }

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
    AstNode **param_defaults = NULL;
    int param_count = 0;
    if (!parse_param_list(p, &param_types, &param_names, &param_count, NULL, &param_defaults)) {
        free(name);
        return NULL;
    }
    consume(p, TOKEN_RPAREN, "expected ')' after parameters");

    TypeNode *return_type = NULL;
    if (match_tok(p, TOKEN_ARROW)) {
        bool save = p->in_return_type;
        p->in_return_type = true;
        return_type = parse_type(p);
        p->in_return_type = save;
    }

    int where_bound_count = 0;
    WhereBound *where_bounds = parse_where_bounds(p, &where_bound_count);

    AstNode *body = parse_block(p);

    AstNode *n = new_node(AST_FN_DECL, line, col);
    n->as.fn_decl.name = name;
    n->as.fn_decl.type_params = fn_type_params;
    n->as.fn_decl.type_param_count = fn_type_param_count;
    n->as.fn_decl.type_param_bounds = fn_type_param_bounds;
    n->as.fn_decl.where_bounds = where_bounds;
    n->as.fn_decl.where_bound_count = where_bound_count;
    n->as.fn_decl.param_types = param_types;
    n->as.fn_decl.param_names = param_names;
    n->as.fn_decl.param_defaults = param_defaults;
    n->as.fn_decl.param_count = param_count;
    n->as.fn_decl.return_type = return_type;
    n->as.fn_decl.body = body;
    n->as.fn_decl.is_static = false;
    n->as.fn_decl.impl_struct_name = NULL;
    n->as.fn_decl.self_borrow_kind = self_borrow_kind;
    return n;
}

/* struct field defaults: compile-time literals (v1) plus, for v2, expressions
   constructible at the construction site — array/map literals (incl. empty
   []/{}) and struct literals Foo{...}. */
static bool is_literal_default(const AstNode *e) {
    if (e == NULL) return false;
    switch (e->kind) {
    case AST_INT_LIT:
    case AST_FLOAT_LIT:
    case AST_STRING_LIT:
    case AST_BOOL_LIT:
    case AST_NIL_LIT:
        return true;
    case AST_UNARY:
        return e->as.unary.operand != NULL &&
               (e->as.unary.operand->kind == AST_INT_LIT ||
                e->as.unary.operand->kind == AST_FLOAT_LIT);
    /* v2: empty/literal vec and struct literals (constructed at the site).
       (map defaults deferred — map literal codegen is var-decl-special.) */
    case AST_ARRAY_LIT:
    case AST_NEW_EXPR:
        return true;
    default:
        return false;
    }
}

AstNode *parse_struct_decl(Parser *p) {
    /* 'struct' already consumed */
    int line = p->previous.line;
    int col  = p->previous.column;

    if (!check(p, TOKEN_IDENTIFIER)) {
        error_at_current(p, "expected struct name");
        return NULL;
    }
    advance(p);
    char *name = str_dup_n(p->previous.start, p->previous.length);

    /* G1: optional type parameter list — struct Name(T, U) { ... }
       Supports trait bounds: struct Name(T: Trait1 + Trait2, U) { ... } */
    char **type_params = NULL;
    int   type_param_count = 0;
    int   type_param_cap = 0;
    TypeParamBound *struct_type_param_bounds = NULL;

    if (check(p, TOKEN_LPAREN)) {
        advance(p); /* consume '(' */
        if (check(p, TOKEN_IDENTIFIER)
            && p->current.length > 0
            && p->current.start[0] >= 'A' && p->current.start[0] <= 'Z') {
            /* It IS a type parameter list */
            bool has_any_bounds = false;
            do {
                if (!check(p, TOKEN_IDENTIFIER)
                    || p->current.length == 0
                    || p->current.start[0] < 'A' || p->current.start[0] > 'Z') {
                    error_at_current(p, "type parameter names must start with an uppercase letter");
                    break;
                }
                advance(p);
                char *tpname = str_dup_n(p->previous.start, p->previous.length);
                if (type_param_count >= type_param_cap) {
                    type_param_cap = GROW_CAPACITY(type_param_cap);
                    type_params = GROW_ARRAY(char *, type_params, type_param_cap);
                    struct_type_param_bounds = realloc(struct_type_param_bounds,
                        (size_t)type_param_cap * sizeof(struct_type_param_bounds[0]));
                }
                type_params[type_param_count] = tpname;
                struct_type_param_bounds[type_param_count].trait_names = NULL;
                struct_type_param_bounds[type_param_count].count = 0;

                /* Parse optional trait bounds: T: Trait1 + Trait2 */
                if (match_tok(p, TOKEN_COLON)) {
                    has_any_bounds = true;
                    int bcap = 0;
                    do {
                        if (!check(p, TOKEN_IDENTIFIER)) {
                            error_at_current(p, "expected interface name after ':'");
                            break;
                        }
                        advance(p);
                        char *tname = str_dup_n(p->previous.start, p->previous.length);
                        int bc = struct_type_param_bounds[type_param_count].count;
                        if (bc >= bcap) {
                            bcap = bcap < 4 ? 4 : bcap * 2;
                            struct_type_param_bounds[type_param_count].trait_names =
                                realloc(struct_type_param_bounds[type_param_count].trait_names,
                                        (size_t)bcap * sizeof(char *));
                        }
                        struct_type_param_bounds[type_param_count].trait_names[bc] = tname;
                        struct_type_param_bounds[type_param_count].count++;
                    } while (match_tok(p, TOKEN_PLUS));
                }
                type_param_count++;
            } while (match_tok(p, TOKEN_COMMA));
            if (!has_any_bounds) {
                free(struct_type_param_bounds);
                struct_type_param_bounds = NULL;
            }
            consume(p, TOKEN_RPAREN, "expected ')' after type parameters");
        } else {
            error_at_current(p, "expected uppercase type parameter name after '(' in struct declaration");
        }
    }

    consume(p, TOKEN_LBRACE, "expected '{' after struct name");

    TypeNode **field_types = NULL;
    char **field_names = NULL;
    AstNode **field_defaults = NULL;
    bool *field_private = NULL;
    int field_count = 0;
    int field_cap = 0;

    while (!check(p, TOKEN_RBRACE) && !check(p, TOKEN_EOF)) {
        /* Optional `priv` modifier (contextual keyword, field position only —
           `priv` stays a usable identifier elsewhere). Marks the field
           inaccessible outside its own impl methods / defining module. */
        bool fpriv = false;
        if (check(p, TOKEN_IDENTIFIER) && p->current.length == 7 &&
            strncmp(p->current.start, "private", 7) == 0) {
            advance(p);
            fpriv = true;
        }
        bool save_rt = p->in_return_type;
        p->in_return_type = true;  /* struct fields require Block alias */
        TypeNode *ft = parse_type(p);
        p->in_return_type = save_rt;
        if (ft == NULL) {
            recover_in_body(p);
            continue;
        }
        if (!check(p, TOKEN_IDENTIFIER)) {
            error_at_current(p, "expected field name");
            type_node_free(ft);
            recover_in_body(p);
            continue;
        }
        advance(p);
        char *fname = str_dup_n(p->previous.start, p->previous.length);

        /* Optional default value: `= <literal | [..] | Foo{..}>`. */
        AstNode *fdefault = NULL;
        if (match_tok(p, TOKEN_ASSIGN)) {
            fdefault = parse_expr_prec(p, PREC_ASSIGNMENT);
            if (fdefault != NULL && !is_literal_default(fdefault)) {
                error_at_current(p, "struct field default must be a literal, "
                                    "vec literal ([..]), or struct literal (Foo{..})");
                ast_free(fdefault);
                fdefault = NULL;
            }
        }
        int field_end_line = p->previous.line;  /* last token of this field */

        if (field_count >= field_cap) {
            field_cap = GROW_CAPACITY(field_cap);
            field_types = GROW_ARRAY(TypeNode *, field_types, field_cap);
            field_names = GROW_ARRAY(char *, field_names, field_cap);
            field_defaults = GROW_ARRAY(AstNode *, field_defaults, field_cap);
            field_private = GROW_ARRAY(bool, field_private, field_cap);
        }
        field_types[field_count] = ft;
        field_names[field_count] = fname;
        field_defaults[field_count] = fdefault;
        field_private[field_count] = fpriv;
        field_count++;

        /* Field separator: ',' / ';' (any number, trailing allowed) OR a
           newline. Newline-separated fields need no separator (LS "semicolons
           optional" style); but two fields on the SAME line must be separated
           by ',' or ';' — same-line space-only separation is rejected. */
        bool had_sep = false;
        while (check(p, TOKEN_COMMA) || check(p, TOKEN_SEMICOLON)) {
            advance(p);
            had_sep = true;
        }
        if (!check(p, TOKEN_RBRACE) && !check(p, TOKEN_EOF) && !had_sep &&
            p->current.line <= field_end_line) {
            error_at_current(p,
                "struct fields on the same line must be separated by ',' or ';'");
        }
    }
    consume(p, TOKEN_RBRACE, "expected '}' after struct fields");

    AstNode *n = new_node(AST_STRUCT_DECL, line, col);
    n->as.struct_decl.name = name;
    n->as.struct_decl.type_params = type_params;
    n->as.struct_decl.type_param_count = type_param_count;
    n->as.struct_decl.type_param_bounds = struct_type_param_bounds;
    n->as.struct_decl.field_types = field_types;
    n->as.struct_decl.field_names = field_names;
    n->as.struct_decl.field_defaults = field_defaults;
    n->as.struct_decl.field_private = field_private;
    n->as.struct_decl.field_count = field_count;
    return n;
}

/* enum Name {
 *     V1,                  // no payload
 *     V2(Type),            // unnamed payload (Rust-style)
 *     V3(Type name, ...);  // named payload (LS-style)
 * }
 * Variant separators (',' / ';' / nothing) are all accepted. */
AstNode *parse_enum_decl(Parser *p) {
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
            recover_in_body(p);
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

/* ---- parse_trait_decl ---- */
/* Parse: trait Name { fn sig(); fn sig(); ... }
   'trait' already consumed. */
AstNode *parse_trait_decl(Parser *p) {
    int line = p->previous.line;
    int col  = p->previous.column;

    if (!check(p, TOKEN_IDENTIFIER)) {
        error_at_current(p, "expected interface name after 'interface'");
        return NULL;
    }
    advance(p);
    char *name = str_dup_n(p->previous.start, p->previous.length);

    consume(p, TOKEN_LBRACE, "expected '{' after interface name");

    AstNode **sigs = NULL;
    int sig_count = 0;
    int sig_cap = 0;

    while (!check(p, TOKEN_RBRACE) && !check(p, TOKEN_EOF)) {
        bool sig_static = false;
        if (match_tok(p, TOKEN_STATIC)) {
            sig_static = true;
        }
        if (!match_tok(p, TOKEN_FN)) {
            error_at_current(p, "expected 'def' in interface body");
            recover_in_body(p);
            continue;
        }
        AstNode *sig = parse_fn_signature(p);
        if (sig == NULL) {
            recover_in_body(p);
            continue;
        }
        sig->as.fn_decl.is_static = sig_static;
        if (sig_count >= sig_cap) {
            sig_cap = GROW_CAPACITY(sig_cap);
            sigs = GROW_ARRAY(AstNode *, sigs, sig_cap);
        }
        sigs[sig_count++] = sig;
        /* optional semicolon / newline separator */
        match_tok(p, TOKEN_SEMICOLON);
    }
    consume(p, TOKEN_RBRACE, "expected '}' after interface body");

    AstNode *n = new_node(AST_TRAIT_DECL, line, col);
    n->as.trait_decl.name = name;
    n->as.trait_decl.method_sigs = sigs;
    n->as.trait_decl.method_sig_count = sig_count;
    return n;
}

/* Collect bare type-parameter names from a `(T, U, ...)` receiver/target list
   into *params (growing *cap). Each entry must be an identifier — a concrete type
   keyword (`int`, ...) is rejected, since LS has no concrete-type impl
   specialization. Assumes the current token is '('. This is where a generic
   `methods X(T) { ... }` block declares its type parameters: there is no separate
   leading `methods(T)` list — the receiver IS the declaration site. */
static void parse_receiver_type_params(Parser *p, char ***params,
                                       int *count, int *cap) {
    advance(p); /* consume '(' */
    if (!check(p, TOKEN_RPAREN)) {
        do {
            if (!check(p, TOKEN_IDENTIFIER)) {
                error_at_current(p, "expected a type-parameter name (e.g. T) in "
                                    "the methods receiver type");
                break;
            }
            advance(p);
            char *tpname = str_dup_n(p->previous.start, p->previous.length);
            if (*count >= *cap) {
                *cap = GROW_CAPACITY(*cap);
                *params = GROW_ARRAY(char *, *params, *cap);
            }
            (*params)[(*count)++] = tpname;
        } while (match_tok(p, TOKEN_COMMA));
    }
    consume(p, TOKEN_RPAREN, "expected ')' after methods receiver type params");
}

AstNode *parse_impl_decl(Parser *p) {
    /* 'impl' already consumed */
    int line = p->previous.line;
    int col  = p->previous.column;

    /* Type params come from the RECEIVER type — `methods X(T) { ... }` — collected
       at the receiver/target sites below. The old leading list `methods(T) X(T)`
       is retired (it duplicated the receiver and could silently disagree with it). */
    char **type_params = NULL;
    int type_param_count = 0;
    int type_param_cap = 0;

    if (check(p, TOKEN_LPAREN)) {
        error_at_current(p, "type parameters now go on the receiver type: write "
                            "`methods X(T) { ... }`, not `methods(T) X(T)`");
        return NULL;
    }

    /* Trait impl detection: impl[(T...)] TraitName for StructName[(T...)] { ... }.
       Disambiguate: current is IDENTIFIER and next is TOKEN_FOR. The optional
       leading (T...) was already consumed above (generic trait impl). */
    if (check(p, TOKEN_IDENTIFIER)) {
        Token peek = scanner_peek(&p->scanner);
        if (peek.type == TOKEN_FOR) {
            advance(p); /* consume trait name */
            char *trait_name = str_dup_n(p->previous.start, p->previous.length);
            advance(p); /* consume 'for' */

            /* Step 11: accept both struct identifiers and builtin type keywords */
            if (!check(p, TOKEN_IDENTIFIER) &&
                !check(p, TOKEN_TYPE_INT) && !check(p, TOKEN_TYPE_I64) &&
                !check(p, TOKEN_TYPE_F64) && !check(p, TOKEN_TYPE_BOOL) &&
                !check(p, TOKEN_TYPE_CHAR)) {
                error_at_current(p, "expected type name after 'for' in methods");
                free(trait_name);
                for (int i = 0; i < type_param_count; i++) free(type_params[i]);
                free(type_params);
                return NULL;
            }
            advance(p);
            char *struct_name = str_dup_n(p->previous.start, p->previous.length);

            /* generic target args — `methods Trait for Complex(T)`: collect the (T)
               as this impl's type parameters (the receiver is the declaration site). */
            if (check(p, TOKEN_LPAREN)) {
                parse_receiver_type_params(p, &type_params,
                                           &type_param_count, &type_param_cap);
            }

            consume(p, TOKEN_LBRACE, "expected '{' after struct name in interface methods");

            AstNode **methods = NULL;
            int method_count = 0;
            int method_cap = 0;

            while (!check(p, TOKEN_RBRACE) && !check(p, TOKEN_EOF)) {
                bool is_static = false;
                if (match_tok(p, TOKEN_STATIC)) {
                    is_static = true;
                }
                if (!match_tok(p, TOKEN_FN)) {
                    error_at_current(p, "expected 'def' in interface methods block");
                    recover_in_body(p);
                    continue;
                }
                /* trait-impl methods may use operator symbol names (fn +, fn ==, ...) */
                AstNode *method = parse_fn_decl(p, /*allow_operator_name=*/true);
                if (method == NULL) {
                    recover_in_body(p);
                    continue;
                }
                method->as.fn_decl.is_static = is_static;
                method->as.fn_decl.impl_struct_name = struct_name;
                if (method_count >= method_cap) {
                    method_cap = GROW_CAPACITY(method_cap);
                    methods = GROW_ARRAY(AstNode *, methods, method_cap);
                }
                methods[method_count++] = method;
            }
            consume(p, TOKEN_RBRACE, "expected '}' after interface methods block");

            AstNode *n = new_node(AST_IMPL_TRAIT_DECL, line, col);
            n->as.impl_trait_decl.trait_name = trait_name;
            n->as.impl_trait_decl.struct_name = struct_name;
            n->as.impl_trait_decl.type_params = type_params;
            n->as.impl_trait_decl.type_param_count = type_param_count;
            n->as.impl_trait_decl.methods = methods;
            n->as.impl_trait_decl.method_count = method_count;
            return n;
        }
    }

    /* Accept a struct identifier OR a builtin type keyword as the target — the
       latter enables `methods int { ... }` extension blocks and the merged
       trait-impl form `methods int: Hash { ... }` (mirrors the `for` branch). */
    if (!check(p, TOKEN_IDENTIFIER) &&
        !check(p, TOKEN_TYPE_INT) && !check(p, TOKEN_TYPE_I8) &&
        !check(p, TOKEN_TYPE_I16) && !check(p, TOKEN_TYPE_I32) &&
        !check(p, TOKEN_TYPE_I64) && !check(p, TOKEN_TYPE_U8) &&
        !check(p, TOKEN_TYPE_U16) && !check(p, TOKEN_TYPE_U32) &&
        !check(p, TOKEN_TYPE_U64) && !check(p, TOKEN_TYPE_F32) &&
        !check(p, TOKEN_TYPE_F64) && !check(p, TOKEN_TYPE_BOOL) &&
        !check(p, TOKEN_TYPE_CHAR)) {
        error_at_current(p, "expected struct name after 'methods'");
        for (int i = 0; i < type_param_count; i++) free(type_params[i]);
        free(type_params);
        return NULL;
    }
    advance(p);
    char *name = str_dup_n(p->previous.start, p->previous.length);

    /* Collect receiver type params — `methods Stack(T)` / `methods Map(K, V)`.
       This is the sole declaration site (the leading `methods(T)` form is retired). */
    if (check(p, TOKEN_LPAREN)) {
        parse_receiver_type_params(p, &type_params,
                                   &type_param_count, &type_param_cap);
    }

    /* de-Rust merged trait-impl form: `methods Type[(T)]: Interface { ... }`.
       The type is named FIRST (vs the legacy `impl Interface for Type`); a ':'
       here names the interface being implemented, and we build the very same
       AST_IMPL_TRAIT_DECL. The legacy `Interface for Type` order is still handled
       by the peek==TOKEN_FOR branch above, so both spellings coexist. */
    if (match_tok(p, TOKEN_COLON)) {
        if (!check(p, TOKEN_IDENTIFIER)) {
            error_at_current(p, "expected interface name after ':' in methods block");
            free(name);
            for (int i = 0; i < type_param_count; i++) free(type_params[i]);
            free(type_params);
            return NULL;
        }
        advance(p);
        char *mt_trait_name = str_dup_n(p->previous.start, p->previous.length);

        consume(p, TOKEN_LBRACE, "expected '{' after interface name in methods block");

        AstNode **mt_methods = NULL;
        int mt_method_count = 0;
        int mt_method_cap = 0;
        while (!check(p, TOKEN_RBRACE) && !check(p, TOKEN_EOF)) {
            bool mt_is_static = false;
            if (match_tok(p, TOKEN_STATIC)) mt_is_static = true;
            if (!match_tok(p, TOKEN_FN)) {
                error_at_current(p, "expected 'def' in methods block");
                recover_in_body(p);
                continue;
            }
            /* interface methods may use operator symbol names (fn +, fn ==, ...) */
            AstNode *method = parse_fn_decl(p, /*allow_operator_name=*/true);
            if (method == NULL) { recover_in_body(p); continue; }
            method->as.fn_decl.is_static = mt_is_static;
            method->as.fn_decl.impl_struct_name = name;  /* shared, mirrors legacy branch */
            if (mt_method_count >= mt_method_cap) {
                mt_method_cap = GROW_CAPACITY(mt_method_cap);
                mt_methods = GROW_ARRAY(AstNode *, mt_methods, mt_method_cap);
            }
            mt_methods[mt_method_count++] = method;
        }
        consume(p, TOKEN_RBRACE, "expected '}' after methods block");

        AstNode *n = new_node(AST_IMPL_TRAIT_DECL, line, col);
        n->as.impl_trait_decl.trait_name = mt_trait_name;
        n->as.impl_trait_decl.struct_name = name;
        n->as.impl_trait_decl.type_params = type_params;
        n->as.impl_trait_decl.type_param_count = type_param_count;
        n->as.impl_trait_decl.methods = mt_methods;
        n->as.impl_trait_decl.method_count = mt_method_count;
        return n;
    }

    consume(p, TOKEN_LBRACE, "expected '{' after methods name");

    AstNode **methods = NULL;
    int method_count = 0;
    int method_cap = 0;

    while (!check(p, TOKEN_RBRACE) && !check(p, TOKEN_EOF)) {
        bool is_static = false;
        if (match_tok(p, TOKEN_STATIC)) {
            is_static = true;
        }
        if (!match_tok(p, TOKEN_FN)) {
            error_at_current(p, "expected 'def' in methods block");
            recover_in_body(p);
            continue;
        }
        /* plain impl methods: operator symbol names not allowed here */
        AstNode *method = parse_fn_decl(p, /*allow_operator_name=*/false);
        if (method == NULL) {
            recover_in_body(p);
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
    consume(p, TOKEN_RBRACE, "expected '}' after methods block");

    AstNode *n = new_node(AST_IMPL_DECL, line, col);
    n->as.impl_decl.name = name;
    n->as.impl_decl.type_params = type_params;
    n->as.impl_decl.type_param_count = type_param_count;
    n->as.impl_decl.methods = methods;
    n->as.impl_decl.method_count = method_count;
    return n;
}

/* type Name = T — type alias (Phase A closure prerequisite). The target
   type is parsed without the return-type guard, so `type X = Block(...) -> Y`
   is the canonical site where Block appears. */
AstNode *parse_type_alias_decl(Parser *p) {
    /* 'type' already consumed */
    int line = p->previous.line;
    int col  = p->previous.column;
    if (!check(p, TOKEN_IDENTIFIER)) {
        error_at_current(p, "expected name after 'type'");
        return NULL;
    }
    advance(p);
    char *name = str_dup_n(p->previous.start, p->previous.length);
    consume(p, TOKEN_ASSIGN, "expected '=' after type alias name");
    bool save = p->in_return_type;
    p->in_return_type = false;
    TypeNode *target = parse_type(p);
    p->in_return_type = save;
    skip_semicolons(p);
    if (target == NULL) {
        free(name);
        return NULL;
    }
    AstNode *n = new_node(AST_TYPE_ALIAS_DECL, line, col);
    n->as.type_alias_decl.name = name;
    n->as.type_alias_decl.target = target;
    return n;
}

AstNode *parse_module_decl(Parser *p) {
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

/* A module-path segment is normally an identifier, but a few type keywords are
   valid std module file names. */
static bool is_import_path_segment(TokenType t) {
    return t == TOKEN_IDENTIFIER ||
           t == TOKEN_ARRAY;
}

AstNode *parse_import_decl(Parser *p) {
    /* 'import' already consumed */
    int line = p->previous.line;
    int col  = p->previous.column;

    /* Build path from identifiers separated by dots: std.io -> "std.io" */
    char path_buf[256];
    int path_len = 0;

    if (!is_import_path_segment(p->current.type)) {
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
        if (!is_import_path_segment(p->current.type)) break;
        advance(p);
        Token seg = p->previous;
        if (path_len + 1 + seg.length < (int)sizeof(path_buf) - 1) {
            path_buf[path_len++] = '.';
            memcpy(path_buf + path_len, seg.start, (size_t)seg.length);
            path_len += seg.length;
        }
    }
    path_buf[path_len] = '\0';

    /* Optional: `as alias_name` */
    char *alias = NULL;
    if (check(p, TOKEN_AS)) {
        advance(p); /* consume 'as' */
        if (check(p, TOKEN_IDENTIFIER)) {
            advance(p);
            Token at = p->previous;
            alias = str_dup_n(at.start, at.length);
        }
    }
    skip_semicolons(p);

    AstNode *n = new_node(AST_IMPORT_DECL, line, col);
    n->as.import_decl.path = str_dup_n(path_buf, path_len);
    n->as.import_decl.alias = alias;
    return n;
}

AstNode *parse_load_lib(Parser *p) {
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
        error_at_current(p, "expected function name after 'extern def'");
        return NULL;
    }
    advance(p);
    char *name = str_dup_n(p->previous.start, p->previous.length);

    consume(p, TOKEN_LPAREN, "expected '(' after extern function name");

    TypeNode **param_types = NULL;
    char **param_names = NULL;
    int param_count = 0;
    bool is_vararg = false;
    if (!parse_param_list(p, &param_types, &param_names, &param_count, &is_vararg, NULL)) {
        free(name);
        return NULL;
    }
    consume(p, TOKEN_RPAREN, "expected ')' after extern function parameters");

    TypeNode *return_type = NULL;
    if (match_tok(p, TOKEN_ARROW)) {
        bool save = p->in_return_type;
        p->in_return_type = true;
        return_type = parse_type(p);
        p->in_return_type = save;
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
AstNode *parse_extern_fn(Parser *p) {
    int line = p->previous.line;
    int col  = p->previous.column;
    consume(p, TOKEN_FN, "expected 'def' after 'extern'");
    return parse_extern_fn_body(p, line, col);
}

/* Parse: extern struct Name { field_type field_name ... }
   'extern' already consumed; current token is 'struct'. */
AstNode *parse_extern_struct(Parser *p) {
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
AstNode *parse_extern_block(Parser *p) {
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
            error_at_current(p, "expected 'struct' or 'def' inside extern block");
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
