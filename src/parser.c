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
    /* Use strtoull (not strtoll): an int literal is always a non-negative
       magnitude (a leading '-' is a separate unary-minus token), so unsigned
       parsing covers the full u64 range without saturating bit63-set values
       like 0xAABBCCDDEE112233. The bits are stored verbatim into long long. */
    if (len > 2 && buf[0] == '0' && (buf[1] == 'x' || buf[1] == 'X')) {
        val = (long long)strtoull(buf + 2, NULL, 16);
    } else if (len > 2 && buf[0] == '0' && (buf[1] == 'b' || buf[1] == 'B')) {
        val = (long long)strtoull(buf + 2, NULL, 2);
    } else {
        val = (long long)strtoull(buf, NULL, 10);
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
    char **specs = (char **)malloc_safe((size_t)exprs_cap * sizeof(char *));
    int part_count = 0, expr_count = 0;
    bool any_spec = false;

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
            /* Optional format specifier: {expr:spec}. The scanner emits a
               TOKEN_FSTRING_SPEC that has already consumed the trailing '}',
               so don't expect a separate '}' in that case. */
            char *spec = NULL;
            if (check(p, TOKEN_FSTRING_SPEC)) {
                advance(p);
                spec = str_dup_n(p->previous.start, p->previous.length);
                any_spec = true;
            } else {
                consume(p, TOKEN_RBRACE, "expected '}' after f-string expression");
            }
            if (expr_count >= exprs_cap) {
                exprs_cap = GROW_CAPACITY(exprs_cap);
                exprs = realloc_safe(exprs, (size_t)exprs_cap * sizeof(AstNode *));
                specs = realloc_safe(specs, (size_t)exprs_cap * sizeof(char *));
            }
            specs[expr_count] = spec;
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
    if (any_spec) {
        n->as.format_string.specs = specs;
    } else {
        free(specs);
        n->as.format_string.specs = NULL;
    }
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
    n->as.new_expr.type_args = NULL;
    n->as.new_expr.type_arg_count = 0;

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

static bool ident_has_auto_call_suffix(const char *name) {
    size_t len = strlen(name);
    if (len == 0) return false;
    return name[len - 1] == '?' || name[len - 1] == '!';
}

static AstNode *wrap_zero_arg_call(AstNode *callee, int line, int column) {
    AstNode *call = new_node(AST_CALL, line, column);
    call->as.call.callee = callee;
    call->as.call.args = NULL;
    call->as.call.arg_count = 0;
    call->as.call.type_args = NULL;
    call->as.call.type_arg_count = 0;
    return call;
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
            n->as.new_expr.type_args = NULL;
            n->as.new_expr.type_arg_count = 0;

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

    /* B-4: qualified struct literal  mod.Type{...} / a.b.Type{...}.
       Lookahead: IDENT (.IDENT)+ '{' ('}' | IDENT ':'). Otherwise leave the '.'
       to the normal infix field-access path (e.g. mod.func(), mod.CONST). */
    if (check(p, TOKEN_DOT)) {
        Scanner saved_scanner = p->scanner;
        Token saved_current   = p->current;
        Token saved_previous  = p->previous;
        bool ok = true;
        int segs = 0;
        while (check(p, TOKEN_DOT)) {
            advance(p);                       /* '.' */
            if (!check(p, TOKEN_IDENTIFIER)) { ok = false; break; }
            advance(p);                       /* IDENT */
            segs++;
        }
        bool is_qual_lit = false;
        if (ok && segs >= 1 && check(p, TOKEN_LBRACE)) {
            advance(p);                       /* '{' */
            is_qual_lit = check(p, TOKEN_RBRACE) ||
                (check(p, TOKEN_IDENTIFIER) &&
                 scanner_peek(&p->scanner).type == TOKEN_COLON);
        }
        p->scanner  = saved_scanner;
        p->current  = saved_current;
        p->previous = saved_previous;

        if (is_qual_lit) {
            /* Consume the dotted path: module = tok + all-but-last seg, name = last. */
            char *module = str_dup_n(tok.start, tok.length);
            char *sname  = NULL;
            for (int i = 0; i < segs; i++) {
                advance(p);                   /* '.' */
                advance(p);                   /* IDENT */
                Token seg = p->previous;
                if (i == segs - 1) {
                    sname = str_dup_n(seg.start, seg.length);
                } else {
                    size_t ml = strlen(module), sl = (size_t)seg.length;
                    char *nm = (char *)malloc_safe(ml + 1 + sl + 1);
                    memcpy(nm, module, ml);
                    nm[ml] = '.';
                    memcpy(nm + ml + 1, seg.start, sl);
                    nm[ml + 1 + sl] = '\0';
                    free(module);
                    module = nm;
                }
            }

            AstNode *n = new_node(AST_NEW_EXPR, tok.line, tok.column);
            n->as.new_expr.struct_name = sname;
            n->as.new_expr.module = module;
            n->as.new_expr.field_inits = NULL;
            n->as.new_expr.field_init_count = 0;
            n->as.new_expr.on_stack = true;
            n->as.new_expr.type_args = NULL;
            n->as.new_expr.type_arg_count = 0;

            consume(p, TOKEN_LBRACE, "expected '{' in struct literal");
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

    /* G1: Detect GenericStruct(type_args) { field: val, ... } — generic struct literal.
       Heuristic: IDENT '(' ... ')' '{' ('}' | IDENT ':')
       Save/restore scanner state to peek past balanced parens. */
    if (check(p, TOKEN_LPAREN)) {
        Scanner saved_scanner = p->scanner;
        Token saved_current   = p->current;
        Token saved_previous  = p->previous;
        advance(p); /* consume '(' */
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
        bool is_generic_struct_lit = false;
        if (ok && p->current.type == TOKEN_RPAREN) {
            advance(p); /* consume ')' */
            if (p->current.type == TOKEN_LBRACE) {
                /* Peek inside '{' to check for struct literal pattern */
                Scanner saved2 = p->scanner;
                Token saved2_cur = p->current;
                advance(p); /* consume '{' */
                is_generic_struct_lit =
                    check(p, TOKEN_RBRACE) ||
                    (check(p, TOKEN_IDENTIFIER) &&
                     scanner_peek(&p->scanner).type == TOKEN_COLON);
                (void)saved2; (void)saved2_cur; /* will restore fully below */
            }
        }
        /* Restore state */
        p->scanner  = saved_scanner;
        p->current  = saved_current;
        p->previous = saved_previous;

        if (is_generic_struct_lit) {
            /* Parse: IDENT '(' type_arg, ... ')' '{' field: val, ... '}' */
            AstNode *ne = new_node(AST_NEW_EXPR, tok.line, tok.column);
            ne->as.new_expr.struct_name = str_dup_n(tok.start, tok.length);
            ne->as.new_expr.field_inits = NULL;
            ne->as.new_expr.field_init_count = 0;
            ne->as.new_expr.on_stack = true;
            ne->as.new_expr.type_args = NULL;
            ne->as.new_expr.type_arg_count = 0;

            /* Parse type arguments */
            advance(p); /* consume '(' */
            int ta_cap = 0;
            if (!check(p, TOKEN_RPAREN)) {
                do {
                    TypeNode *ta = parse_type(p);
                    if (ta == NULL) { synchronize(p); break; }
                    if (ne->as.new_expr.type_arg_count >= ta_cap) {
                        ta_cap = GROW_CAPACITY(ta_cap);
                        ne->as.new_expr.type_args = GROW_ARRAY(TypeNode *,
                            ne->as.new_expr.type_args, ta_cap);
                    }
                    ne->as.new_expr.type_args[ne->as.new_expr.type_arg_count++] = ta;
                } while (match_tok(p, TOKEN_COMMA));
            }
            consume(p, TOKEN_RPAREN, "expected ')' after generic type arguments");

            /* Parse struct literal body */
            consume(p, TOKEN_LBRACE, "expected '{' after generic struct type");
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
            ne->as.new_expr.field_inits = (void *)inits;
            ne->as.new_expr.field_init_count = count;
            return ne;
        }
    }

    AstNode *n = new_node(AST_IDENT, tok.line, tok.column);
    n->as.ident.name = str_dup_n(tok.start, tok.length);
    if (!check(p, TOKEN_LPAREN) && ident_has_auto_call_suffix(n->as.ident.name)) {
        return wrap_zero_arg_call(n, tok.line, tok.column);
    }
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

static AstNode *prefix_at_time(Parser *p) {
    Token tok = p->previous; /* TOKEN_AT_TIME */
    AstNode *operand = parse_expr_prec(p, PREC_UNARY);
    AstNode *n = new_node(AST_AT_TIME, tok.line, tok.column);
    n->as.at_time.expr = operand;
    return n;
}

static AstNode *prefix_at_bench(Parser *p) {
    Token tok = p->previous; /* TOKEN_AT_BENCH */
    consume(p, TOKEN_LPAREN, "expected '(' after @bench");
    if (!check(p, TOKEN_INT_LIT)) {
        error_at_current(p, "expected integer iteration count in @bench(N)");
        return new_node(AST_AT_BENCH, tok.line, tok.column);
    }
    advance(p);
    int iterations = (int)strtoll(p->previous.start, NULL, 10);
    if (iterations <= 0) {
        error_at_previous(p, "@bench iteration count must be > 0");
        iterations = 1;
    }
    consume(p, TOKEN_RPAREN, "expected ')' after iteration count");
    AstNode *operand = parse_expr_prec(p, PREC_UNARY);
    AstNode *n = new_node(AST_AT_BENCH, tok.line, tok.column);
    n->as.at_bench.expr = operand;
    n->as.at_bench.iterations = iterations;
    return n;
}

/* Phase A.5: Ruby-style closure literal. Param list already past the leading
   '|' (or, for the no-arg ||, past the '||' itself). When `parse_params` is
   true, we still need to consume identifiers and the trailing '|'.

   Body grammar: either `{ block }` or a single expression (auto-returned).
   Param types are NULL — to be inferred from the call site (Phase B/C).
   The resulting AST_CLOSURE has param_types[i] == NULL for every i; the
   checker treats this as the "Ruby-style, needs context" case. */
static AstNode *parse_ruby_closure_after_bar(Parser *p, Token start_tok,
                                             bool parse_params) {
    TypeNode **param_types = NULL;
    char **param_names = NULL;
    int param_count = 0;
    int param_cap = 0;

    if (parse_params) {
        if (!check(p, TOKEN_PIPE)) {
            do {
                if (!check(p, TOKEN_IDENTIFIER)) {
                    error_at_current(p, "expected parameter name in `|...|`");
                    break;
                }
                advance(p);
                char *pname = str_dup_n(p->previous.start, p->previous.length);
                if (param_count >= param_cap) {
                    param_cap = GROW_CAPACITY(param_cap);
                    param_types = GROW_ARRAY(TypeNode *, param_types, param_cap);
                    param_names = GROW_ARRAY(char *, param_names, param_cap);
                }
                /* No type annotation in v1 — inferred from call site. */
                param_types[param_count] = NULL;
                param_names[param_count] = pname;
                param_count++;
            } while (match_tok(p, TOKEN_COMMA));
        }
        consume(p, TOKEN_PIPE, "expected closing '|' after closure parameters");
    }

    AstNode *body = NULL;
    if (check(p, TOKEN_LBRACE)) {
        body = parse_block(p);
    } else {
        /* Single expression body: auto-return. Wrap as { return expr }. */
        AstNode *expr = parse_expr_prec(p, PREC_NONE);
        if (expr == NULL) {
            for (int i = 0; i < param_count; i++) free(param_names[i]);
            free(param_types);
            free(param_names);
            return NULL;
        }
        AstNode *ret = new_node(AST_RETURN, expr->line, expr->column);
        ret->as.return_stmt.value = expr;
        AstNode *blk = new_node(AST_BLOCK, expr->line, expr->column);
        blk->as.block.stmts = (AstNode **)malloc_safe(sizeof(AstNode *));
        blk->as.block.stmts[0] = ret;
        blk->as.block.stmt_count = 1;
        body = blk;
    }

    AstNode *n = new_node(AST_CLOSURE, start_tok.line, start_tok.column);
    n->as.closure.param_types = param_types;
    n->as.closure.param_names = param_names;
    n->as.closure.param_count = param_count;
    n->as.closure.return_type = NULL;
    n->as.closure.body = body;
    n->as.closure.is_ruby_form = true;
    n->as.closure.captures = NULL;
    n->as.closure.capture_count = 0;
    n->as.closure.move_names = NULL;
    n->as.closure.move_count = 0;
    return n;
}

/* Prefix `|x, y| body` — Ruby-style closure. */
static AstNode *prefix_ruby_closure(Parser *p) {
    /* `|` already consumed (we are the prefix handler) */
    Token bar_tok = p->previous;
    return parse_ruby_closure_after_bar(p, bar_tok, true);
}

/* Prefix `|| body` — zero-argument Ruby closure. The lexer fuses `||` into a
   single TOKEN_OR; both '|' bars are gone by the time this fires. */
static AstNode *prefix_no_arg_closure(Parser *p) {
    Token bar_tok = p->previous;
    return parse_ruby_closure_after_bar(p, bar_tok, false);
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
    n->as.closure.is_ruby_form = false;
    n->as.closure.captures = NULL;
    n->as.closure.capture_count = 0;
    n->as.closure.move_names = NULL;
    n->as.closure.move_count = 0;
    return n;
}

/* V1 bit-pattern match: `bits` is a soft keyword recognized only at a match-arm
   pattern position — current token is the identifier `bits` immediately followed
   by `[`. Anywhere else `bits` remains an ordinary identifier. */
static bool parser_at_bits_keyword(Parser *p) {
    return p->current.type == TOKEN_IDENTIFIER &&
           p->current.length == 4 &&
           memcmp(p->current.start, "bits", 4) == 0 &&
           scanner_peek(&p->scanner).type == TOKEN_LBRACKET;
}

/* Consume the current TOKEN_INT_LIT and return its value (10 / 0x / 0b). */
static long long parse_int_lit_value(Parser *p) {
    Token tok = p->current;
    char buf[64];
    int len = tok.length < 63 ? tok.length : 63;
    memcpy(buf, tok.start, (size_t)len);
    buf[len] = '\0';
    long long val;
    /* strtoull: int literals are non-negative magnitudes — full u64 range, no
       bit63 saturation (see prefix_int_lit). */
    if (len > 2 && buf[0] == '0' && (buf[1] == 'x' || buf[1] == 'X'))
        val = (long long)strtoull(buf + 2, NULL, 16);
    else if (len > 2 && buf[0] == '0' && (buf[1] == 'b' || buf[1] == 'B'))
        val = (long long)strtoull(buf + 2, NULL, 2);
    else
        val = (long long)strtoull(buf, NULL, 10);
    advance(p);
    return val;
}

/* bits[width:name] [width:0xVAL] [width:_] ...  (V1, MSB-first field extraction).
   Returns an AST_MATCH_BIT_PATTERN_SEQ. 'bits' must be the current token. */
static AstNode *parse_bit_pattern_seq(Parser *p) {
    Token start = p->current;   /* 'bits' */
    advance(p);                 /* consume 'bits' */

    AstNode **items = NULL;
    int count = 0, cap = 0;

    while (check(p, TOKEN_LBRACKET)) {
        Token lb = p->current;
        advance(p);             /* consume '[' */

        if (!check(p, TOKEN_INT_LIT)) {
            error_at_current(p, "expected bit width (integer) after '['");
            break;
        }
        long long w = parse_int_lit_value(p);
        consume(p, TOKEN_COLON, "expected ':' after bit width");

        AstNode *item = new_node(AST_MATCH_BIT_PATTERN, lb.line, lb.column);
        item->as.bit_pattern.width = (int)w;
        item->as.bit_pattern.name = NULL;
        item->as.bit_pattern.match_value_set = false;
        item->as.bit_pattern.match_val = 0;
        item->as.bit_pattern.lsb_shift = 0;

        if (check(p, TOKEN_INT_LIT)) {                 /* bits[4:0xA] — match value */
            item->as.bit_pattern.match_value_set = true;
            item->as.bit_pattern.match_val = parse_int_lit_value(p);
        } else if (check(p, TOKEN_UNDERSCORE)) {       /* bits[4:_] — skip/wildcard */
            advance(p);
        } else if (check(p, TOKEN_IDENTIFIER)) {       /* bits[4:name] — bind */
            item->as.bit_pattern.name = str_dup_n(p->current.start, p->current.length);
            advance(p);
        } else {
            error_at_current(p, "expected name, integer literal, or '_' after ':'");
        }

        consume(p, TOKEN_RBRACKET, "expected ']' after bit field");

        if (count >= cap) {
            cap = GROW_CAPACITY(cap);
            items = GROW_ARRAY(AstNode *, items, cap);
        }
        items[count++] = item;
    }

    AstNode *seq = new_node(AST_MATCH_BIT_PATTERN_SEQ, start.line, start.column);
    seq->as.bit_pattern_seq.items = items;
    seq->as.bit_pattern_seq.count = count;
    seq->as.bit_pattern_seq.total_width = 0;
    return seq;
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
        /* Parse the first pattern at PREC_BITXOR (one level above PREC_BITOR) so
           that a bare `|` token is NOT consumed as a bitwise-OR expression.
           Then collect additional `|`-separated alternatives to build an
           AST_MATCH_OR_PATTERN tree: `98 | 102 | 110 => body`
           If the user needs bitwise-OR in a pattern value, they can parenthesise:
           `(FLAG_A | FLAG_B) => body`. */
        AstNode *pattern = parser_at_bits_keyword(p)
            ? parse_bit_pattern_seq(p)
            : parse_expr_prec(p, PREC_BITXOR);
        while (check(p, TOKEN_PIPE)) {
            Token pipe_tok = p->current;
            advance(p); /* consume '|' */
            AstNode *rhs = parser_at_bits_keyword(p)
                ? parse_bit_pattern_seq(p)
                : parse_expr_prec(p, PREC_BITXOR);
            AstNode *or_node = new_node(AST_MATCH_OR_PATTERN, pipe_tok.line, pipe_tok.column);
            or_node->as.or_pattern.left  = pattern;
            or_node->as.or_pattern.right = rhs;
            pattern = or_node;
        }
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

    /* sizeof(Type): the operand is a TYPE, not an expression. Intercept before
       the generic-call heuristic (which would try to parse `int` as an expr).
       Produces an AST_SIZEOF node carrying the parsed TypeNode. */
    if (left->kind == AST_IDENT && strcmp(left->as.ident.name, "sizeof") == 0) {
        TypeNode *t = parse_type(p);
        consume(p, TOKEN_RPAREN, "expected ')' after sizeof type");
        AstNode *n = new_node(AST_SIZEOF, call_tok.line, call_tok.column);
        n->as.sizeof_expr.type_node  = t;
        n->as.sizeof_expr.sized_type = NULL;
        ast_free(left); /* the bare `sizeof` ident node is no longer needed */
        return n;
    }

    /* G2: detect generic function call — ident(TypeArgs)(args).
       If left is AST_IDENT and the first token after '(' is a type keyword
       or uppercase identifier followed by ',' or ')', this is a type arg list. */
    TypeNode **call_type_args = NULL;
    int call_type_arg_count = 0;
    if (left->kind == AST_IDENT || left->kind == AST_FIELD) {
        /* G2: detect generic function call — ident(TypeArgs)(realArgs)
           or method call — obj.method(TypeArgs)(realArgs).
           Heuristic: first token is a type keyword, or uppercase ident, or *&.
           We save parser state and try parsing types; if the list ends with ')'
           followed by '(' it's a genuine generic call. Otherwise restore. */
        bool might_be_type_arg = false;
        Token cur_tok = p->current;
        if (is_type_keyword(cur_tok.type)) {
            might_be_type_arg = true;
        } else if (cur_tok.type == TOKEN_IDENTIFIER
                   && cur_tok.length > 0
                   && cur_tok.start[0] >= 'A' && cur_tok.start[0] <= 'Z') {
            might_be_type_arg = true;
        } else if (cur_tok.type == TOKEN_STAR || cur_tok.type == TOKEN_AMP) {
            might_be_type_arg = true;
        }
        if (might_be_type_arg) {
            /* Lightweight lookahead: scan tokens to check if the pattern is
               type_list ')' '(' — i.e., a valid generic function type arg list
               followed by a real argument list. We scan balanced parens without
               consuming tokens from the parser. Only if the pattern matches do
               we actually parse the type args. */
            Scanner scan_save = p->scanner;
            Token   tok_save  = p->current;
            bool    confirmed = false;
            int     depth     = 1; /* we're inside the first '(' already */

            /* Scan forward through the token stream, tracking paren depth.
               When depth reaches 0 (matching ')'), the very next token must
               be '(' for this to be a generic call. */
            while (depth > 0) {
                /* Use scanner_peek-style: manually advance a copy */
                Token t = p->current;
                if (t.type == TOKEN_EOF) break;
                advance(p);
                if (t.type == TOKEN_LPAREN) depth++;
                else if (t.type == TOKEN_RPAREN) {
                    depth--;
                    if (depth == 0) {
                        /* Check the token immediately after the closing ')' */
                        if (p->current.type == TOKEN_LPAREN) {
                            confirmed = true;
                        }
                        break;
                    }
                }
            }

            /* Restore scanner state */
            p->scanner = scan_save;
            p->current = tok_save;

            if (confirmed) {
                /* Now actually parse the type argument list */
                int ta_cap = 0;
                do {
                    TypeNode *ta = parse_type(p);
                    if (ta == NULL) break;
                    if (call_type_arg_count >= ta_cap) {
                        ta_cap = ta_cap < 4 ? 4 : ta_cap * 2;
                        call_type_args = realloc(call_type_args,
                            (size_t)ta_cap * sizeof(TypeNode *));
                    }
                    call_type_args[call_type_arg_count++] = ta;
                } while (match_tok(p, TOKEN_COMMA));
                consume(p, TOKEN_RPAREN, "expected ')' after type arguments");
                consume(p, TOKEN_LPAREN, "expected '(' after generic type arguments");
            }
        }
    }

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

    /* Phase A.5: trailing closure sugar. If the next token is `{` AND the
       *first* token inside the brace is `|` or `||` (Ruby pipe pair), parse
       it as a closure literal and append as the last argument. Plain `{`
       blocks (struct literals, map literals, if-bodies) are NOT consumed —
       the `|` lookahead is the unambiguous gate per closures_plan §11. */
    if (check(p, TOKEN_LBRACE)) {
        Token next = scanner_peek(&p->scanner);
        if (next.type == TOKEN_PIPE || next.type == TOKEN_OR) {
            advance(p); /* consume '{' */
            /* Now current is '|' or '||'; treat the '{' as the body opener
               of a desugared `f(args, |x| { ... })` form. We construct the
               closure inline: parse `|x|` (or `||`), then statements until
               '}'. */
            Token bar_tok = p->current;
            advance(p); /* consume '|' or '||' */
            bool parse_params = (bar_tok.type == TOKEN_PIPE);
            /* Build the closure: params + body-as-block-of-statements */
            TypeNode **cp_types = NULL;
            char **cp_names = NULL;
            int cp_count = 0;
            int cp_cap = 0;
            if (parse_params) {
                if (!check(p, TOKEN_PIPE)) {
                    do {
                        if (!check(p, TOKEN_IDENTIFIER)) {
                            error_at_current(p, "expected parameter name in `|...|`");
                            break;
                        }
                        advance(p);
                        char *pname = str_dup_n(p->previous.start, p->previous.length);
                        if (cp_count >= cp_cap) {
                            cp_cap = GROW_CAPACITY(cp_cap);
                            cp_types = GROW_ARRAY(TypeNode *, cp_types, cp_cap);
                            cp_names = GROW_ARRAY(char *, cp_names, cp_cap);
                        }
                        cp_types[cp_count] = NULL;
                        cp_names[cp_count] = pname;
                        cp_count++;
                    } while (match_tok(p, TOKEN_COMMA));
                }
                consume(p, TOKEN_PIPE, "expected closing '|' after closure parameters");
            }
            /* Body: read statements until '}'. */
            AstNode **stmts = NULL;
            int stmt_count = 0;
            int stmt_cap = 0;
            skip_semicolons(p);
            while (!check(p, TOKEN_RBRACE) && !check(p, TOKEN_EOF)) {
                AstNode *s = parse_statement(p);
                if (s == NULL) { synchronize(p); continue; }
                if (stmt_count >= stmt_cap) {
                    stmt_cap = GROW_CAPACITY(stmt_cap);
                    stmts = GROW_ARRAY(AstNode *, stmts, stmt_cap);
                }
                stmts[stmt_count++] = s;
                skip_semicolons(p);
            }
            consume(p, TOKEN_RBRACE, "expected '}' after trailing closure body");

            /* Implicit return for single-expression trailing-closure bodies:
               `f(x) { |y| y * 2 }` is the trailing form of `f(x, |y| y*2)`,
               which the prefix path already auto-returns. Match that here so
               both forms behave identically when the body is one expression. */
            if (stmt_count == 1 && stmts[0]->kind == AST_EXPR_STMT) {
                AstNode *expr = stmts[0]->as.expr_stmt.expr;
                stmts[0]->as.expr_stmt.expr = NULL; /* detach so ast_free skips */
                ast_free(stmts[0]);
                AstNode *ret = new_node(AST_RETURN, expr->line, expr->column);
                ret->as.return_stmt.value = expr;
                stmts[0] = ret;
            }

            AstNode *body = new_node(AST_BLOCK, bar_tok.line, bar_tok.column);
            body->as.block.stmts = stmts;
            body->as.block.stmt_count = stmt_count;

            AstNode *closure = new_node(AST_CLOSURE, bar_tok.line, bar_tok.column);
            closure->as.closure.param_types = cp_types;
            closure->as.closure.param_names = cp_names;
            closure->as.closure.param_count = cp_count;
            closure->as.closure.return_type = NULL;
            closure->as.closure.body = body;
            closure->as.closure.is_ruby_form = true;
            closure->as.closure.captures = NULL;
            closure->as.closure.capture_count = 0;
            closure->as.closure.move_names = NULL;
            closure->as.closure.move_count = 0;

            if (arg_count >= arg_cap) {
                arg_cap = GROW_CAPACITY(arg_cap);
                args = GROW_ARRAY(AstNode *, args, arg_cap);
            }
            args[arg_count++] = closure;
        }
    }

    AstNode *n = new_node(AST_CALL, call_tok.line, call_tok.column);
    n->as.call.callee = left;
    n->as.call.args = args;
    n->as.call.arg_count = arg_count;
    n->as.call.type_args = call_type_args;
    n->as.call.type_arg_count = call_type_arg_count;
    return n;
}

/* Key-value literal: { key: val, key: val, ... }
   An empty literal {} is also allowed (type must be declared). */
static AstNode *prefix_map_lit(Parser *p) {
    Token tok = p->previous; /* the '{' token */

    /* Anonymous struct literal `{ field: val, ... }` — the struct type is inferred
       from the expected type at the use site (checker uses c->expected_type).
       A bare IDENT followed by `:` is a struct field. Non-identifier keys below
       route to the key-value literal path. Mirrors the prefixed
       StructName{field: val} parser. */
    if (check(p, TOKEN_IDENTIFIER) &&
        scanner_peek(&p->scanner).type == TOKEN_COLON) {
        AstNode *n = new_node(AST_NEW_EXPR, tok.line, tok.column);
        n->as.new_expr.struct_name = NULL;   /* anonymous: infer from expected type */
        n->as.new_expr.module = NULL;
        n->as.new_expr.on_stack = true;
        n->as.new_expr.type_args = NULL;
        n->as.new_expr.type_arg_count = 0;
        int sc_cap = 0, sc_count = 0;
        struct { char *name; AstNode *value; } *inits = NULL;
        while (!check(p, TOKEN_RBRACE) && !check(p, TOKEN_EOF)) {
            consume(p, TOKEN_IDENTIFIER, "expected field name in struct literal");
            Token fname = p->previous;
            consume(p, TOKEN_COLON, "expected ':' after field name in struct literal");
            AstNode *val = parse_expr_prec(p, PREC_ASSIGNMENT);
            if (sc_count >= sc_cap) {
                sc_cap = GROW_CAPACITY(sc_cap);
                inits = realloc_safe(inits, (size_t)sc_cap * sizeof(inits[0]));
            }
            inits[sc_count].name  = str_dup_n(fname.start, fname.length);
            inits[sc_count].value = val;
            sc_count++;
            if (!match_tok(p, TOKEN_COMMA)) break;
        }
        consume(p, TOKEN_RBRACE, "expected '}' after struct literal fields");
        n->as.new_expr.field_inits = (void *)inits;
        n->as.new_expr.field_init_count = sc_count;
        return n;
    }

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
            /* M-LIT key-value literal, e.g. std.map `Map(K,V) m = {"a": 1}`.
               The checker routes TYPE_STRUCT with __from_pairs to user containers.
               A bare-IDENT key with `:` was already consumed above as an anonymous
               struct literal, so keys here are non-identifier exprs
               (string/int/...). */
            if (!match_tok(p, TOKEN_COLON)) {
                error_at_current(p, "expected ':' between key and value in key-value literal");
                ast_free(key);
                break;
            }
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
    consume(p, TOKEN_RBRACE, "expected '}' to close key-value literal");

    AstNode *n = new_node(AST_MAP_LIT, tok.line, tok.column);
    n->as.map_lit.keys       = keys;
    n->as.map_lit.vals       = vals;
    n->as.map_lit.pair_count = count;
    return n;
}

/* F.1: [move v1, v2] capture spec + closure literal.
   Called from prefix_array_lit when 'move' keyword is detected.
   `bracket_tok` is the '[' that was already consumed. */
static AstNode *prefix_capture_spec(Parser *p, Token bracket_tok) {
    advance(p); /* consume the 'move' identifier */

    char **move_names = NULL;
    int move_count = 0;
    int move_cap = 0;

    do {
        if (!check(p, TOKEN_IDENTIFIER)) {
            error_at_current(p, "expected variable name in '[move ...]' capture list");
            for (int i = 0; i < move_count; i++) free(move_names[i]);
            free(move_names);
            return NULL;
        }
        advance(p);
        char *nm = str_dup_n(p->previous.start, p->previous.length);
        if (move_count >= move_cap) {
            move_cap = GROW_CAPACITY(move_cap);
            move_names = GROW_ARRAY(char *, move_names, move_cap);
        }
        move_names[move_count++] = nm;
    } while (match_tok(p, TOKEN_COMMA));

    consume(p, TOKEN_RBRACKET, "expected ']' after '[move ...]' capture list");

    /* Immediately following must be a Ruby-style closure literal. */
    AstNode *cls = NULL;
    if (check(p, TOKEN_PIPE)) {
        advance(p); /* consume '|' */
        cls = parse_ruby_closure_after_bar(p, p->previous, true);
    } else if (check(p, TOKEN_OR)) {
        advance(p); /* consume '||' */
        cls = parse_ruby_closure_after_bar(p, p->previous, false);
    } else {
        error_at_current(p,
            "expected closure literal '|...|' or '||' after '[move ...]'");
        for (int i = 0; i < move_count; i++) free(move_names[i]);
        free(move_names);
        return NULL;
    }

    if (cls) {
        cls->as.closure.move_names = move_names;
        cls->as.closure.move_count = move_count;
        /* Update location to the '[' for better error messages. */
        cls->line   = bracket_tok.line;
        cls->column = bracket_tok.column;
    } else {
        for (int i = 0; i < move_count; i++) free(move_names[i]);
        free(move_names);
    }
    return cls;
}

/* Array literal: [expr, expr, ...] */
static AstNode *prefix_array_lit(Parser *p) {
    Token tok = p->previous; /* the '[' token */

    /* F.1: [move v1, v2] capture spec — detect 'move' as the first token.
       'move' is not a reserved keyword; check by lexeme. */
    if (check(p, TOKEN_IDENTIFIER) &&
        p->current.length == 4 &&
        strncmp(p->current.start, "move", 4) == 0) {
        return prefix_capture_spec(p, tok);
    }

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
    AstNode *first = parse_expr_prec(p, PREC_NONE);
    AstNode *n = new_node(AST_INDEX, tok.line, tok.column);
    n->as.index_expr.object = left;
    /* Multi-subscript t[i, j, k] -> collect all indices; the checker lowers it to
       the arity-specific reserved protocol method __index{N} (a generalization of
       single-subscript v[i] -> __index). Single-subscript keeps the legacy shape
       (index set, indices == NULL) so all existing consumers are byte-unchanged. */
    if (match_tok(p, TOKEN_COMMA)) {
        int cap = 4, cnt = 1;
        AstNode **arr = (AstNode **)malloc_safe((size_t)cap * sizeof(AstNode *));
        arr[0] = first;
        do {
            if (cnt == cap) {
                cap *= 2;
                arr = (AstNode **)realloc_safe(arr, (size_t)cap * sizeof(AstNode *));
            }
            arr[cnt++] = parse_expr_prec(p, PREC_NONE);
        } while (match_tok(p, TOKEN_COMMA));
        n->as.index_expr.index = NULL;
        n->as.index_expr.indices = arr;
        n->as.index_expr.index_count = cnt;
    } else {
        n->as.index_expr.index = first;
        n->as.index_expr.indices = NULL;
        n->as.index_expr.index_count = 1;
    }
    consume(p, TOKEN_RBRACKET, "expected ']' after index");
    return n;
}

/* Postfix ! force-unwrap: expr! — panic on None/Err */
static AstNode *infix_force_unwrap(Parser *p, AstNode *left) {
    Token tok = p->previous;
    AstNode *n = new_node(AST_FORCE_UNWRAP, tok.line, tok.column);
    n->as.force_unwrap.expr = left;
    return n;
}

/* Field access: left.field or lib.call(:fn, ...) */
static AstNode *infix_field(Parser *p, AstNode *left) {
    Token dot_tok = p->previous;
    /* Field names can be identifiers, 'self', or keywords used as method names
       (e.g. `array` is a type keyword but a valid method name; `new` is reserved
       for allocation but `Task.new`/`X.new` reads as a Ruby-style constructor —
       unambiguous in member position). */
    if (!check(p, TOKEN_IDENTIFIER) && !check(p, TOKEN_SELF) &&
        !check(p, TOKEN_ARRAY) && !check(p, TOKEN_NEW))
    {
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
    if (!check(p, TOKEN_LPAREN) && ident_has_auto_call_suffix(field_name)) {
        return wrap_zero_arg_call(n, dot_tok.line, dot_tok.column);
    }
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
    rules[TOKEN_BANG]       = (ParseRule){ prefix_unary,     infix_force_unwrap, PREC_CALL };
    rules[TOKEN_TILDE]      = (ParseRule){ prefix_unary,     NULL,              PREC_NONE };
    rules[TOKEN_AMP]        = (ParseRule){ prefix_addr,      infix_binary_real, PREC_BITAND };

    /* Bitwise */
    rules[TOKEN_PIPE]       = (ParseRule){ prefix_ruby_closure, infix_binary_real, PREC_BITOR };
    rules[TOKEN_CARET]      = (ParseRule){ NULL,             infix_binary_real, PREC_BITXOR };
    rules[TOKEN_LSHIFT]     = (ParseRule){ NULL,             infix_binary_real, PREC_SHIFT };
    rules[TOKEN_RSHIFT]     = (ParseRule){ NULL,             infix_binary_real, PREC_SHIFT };

    /* Logical */
    rules[TOKEN_AND]        = (ParseRule){ NULL,             infix_binary_real, PREC_AND };
    rules[TOKEN_OR]         = (ParseRule){ prefix_no_arg_closure, infix_binary_real, PREC_OR };

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
    rules[TOKEN_AT_TIME]    = (ParseRule){ prefix_at_time,   NULL,              PREC_NONE };
    rules[TOKEN_AT_BENCH]   = (ParseRule){ prefix_at_bench,  NULL,              PREC_NONE };
}

static const ParseRule *get_rule(TokenType type) {
    init_parse_rules();
    if ((int)type < 0 || (int)type > (int)TOKEN_ERROR) return &rules[TOKEN_ERROR];
    return &rules[type];
}

/* ---- Core expression parser ---- */

static AstNode *parse_expr_prec(Parser *p, Precedence min_prec) {
    init_parse_rules();
    /* Capture and clear the statement-boundary flag: only this top-level call
       may split a trailing `*Ident Ident` into a pointer declaration. Nested
       sub-expressions (initializers, args, parens) must read it as multiplication. */
    bool allow_decl_break = p->stmt_boundary;
    p->stmt_boundary = false;
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
            /* Also handle *StructName varName — pointer-to-struct declaration.
               Only at a statement boundary: inside an initializer / args / parens
               `ident * ident` is always multiplication (e.g. `int e = a * b`). */
            if (allow_decl_break && after_star.type == TOKEN_IDENTIFIER) {
                /* Peek two tokens ahead: if *Ident Ident, it's a var decl */
                Scanner saved_scan = p->scanner;
                scanner_next(&saved_scan); /* consume the Identifier (struct name) */
                Token after_ident = scanner_next(&saved_scan); /* next token */
                if (after_ident.type == TOKEN_IDENTIFIER) break;
            }
        }
        /* Phase 1 (borrow extension): TOKEN_AMP as infix would be bitwise-and.
           But a borrow var decl `&T name` / `&!T name` starting the NEXT
           statement also begins with `&`. Disambiguate by two signals that
           never both hold for genuine infix `a & b` (operands always share the
           operator's line): (1) the `&` opens a new line (newline since the
           previous token), and (2) the tokens after it form a borrow decl
           shape `& [!] (TypeKw|Ident) Ident`. If so, stop the expression so
           parse_statement re-parses the `&...` as a declaration. */
        if (p->current.type == TOKEN_AMP &&
            p->current.line > p->previous.line) {
            Scanner sc = p->scanner;
            Token t1 = scanner_next(&sc);   /* token after & */
            bool decl_shape = false;
            if (t1.type == TOKEN_BANG) {                 /* &! Type name */
                Token t2 = scanner_next(&sc);
                Token t3 = scanner_next(&sc);
                if ((is_type_keyword(t2.type) || t2.type == TOKEN_IDENTIFIER) &&
                    t3.type == TOKEN_IDENTIFIER)
                    decl_shape = true;
            } else if (is_type_keyword(t1.type) || t1.type == TOKEN_IDENTIFIER) {
                Token t2 = scanner_next(&sc);            /* & Type name */
                if (t2.type == TOKEN_IDENTIFIER)
                    decl_shape = true;
            }
            if (decl_shape) break;
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
    case TOKEN_TYPE_VOID:
    case TOKEN_TYPE_OBJECT:
    case TOKEN_ARRAY:
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
        bool is_mut = match_tok(p, TOKEN_BANG);  /* &! => writable reference/slice */
        /* &[T] / &![T] — borrowed slice (a {ptr,len} view over a Vec(T) range). */
        if (match_tok(p, TOKEN_LBRACKET)) {
            TypeNode *elem = parse_type(p);
            if (elem == NULL) return NULL;
            if (!consume(p, TOKEN_RBRACKET, "expected ']' to close slice type '&[T]'")) {
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
static bool starts_var_decl(Parser *p) {
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
                    while (gdepth > 0 && p->current.type != TOKEN_EOF) {
                        if (p->current.type == TOKEN_LPAREN) gdepth++;
                        else if (p->current.type == TOKEN_RPAREN) {
                            gdepth--;
                            if (gdepth == 0) break;
                        }
                        advance(p);
                    }
                    if (p->current.type == TOKEN_RPAREN) {
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
        error_at_current(p, "expected method name after 'fn' in trait");
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
                error_at_current(p, "expected trait name in where clause");
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
static AstNode *parse_fn_decl(Parser *p, bool allow_operator_name) {
    /* 'fn' already consumed */
    int line = p->previous.line;
    int col  = p->previous.column;

    char *name = NULL;
    if (check(p, TOKEN_IDENTIFIER)) {
        advance(p);
        name = str_dup_n(p->previous.start, p->previous.length);
    } else if (check(p, TOKEN_NEW)) {
        /* `new` is reserved for the allocation prefix, but is a valid method
           name (Ruby-style `Task.new` constructor); unambiguous after `fn`. */
        advance(p);
        name = str_dup_n(p->previous.start, p->previous.length); /* "new" */
    } else if (allow_operator_name &&
               (name = operator_method_name(p->current.type)) != NULL) {
        advance(p);  /* consume the operator token used as the method name */
    } else {
        error_at_current(p, "expected function name after 'fn'");
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
                                error_at_current(p, "expected trait name after ':'");
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
                            error_at_current(p, "expected trait name after ':'");
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
    int field_count = 0;
    int field_cap = 0;

    while (!check(p, TOKEN_RBRACE) && !check(p, TOKEN_EOF)) {
        bool save_rt = p->in_return_type;
        p->in_return_type = true;  /* struct fields require Block alias */
        TypeNode *ft = parse_type(p);
        p->in_return_type = save_rt;
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
        }
        field_types[field_count] = ft;
        field_names[field_count] = fname;
        field_defaults[field_count] = fdefault;
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

/* ---- parse_trait_decl ---- */
/* Parse: trait Name { fn sig(); fn sig(); ... }
   'trait' already consumed. */
static AstNode *parse_trait_decl(Parser *p) {
    int line = p->previous.line;
    int col  = p->previous.column;

    if (!check(p, TOKEN_IDENTIFIER)) {
        error_at_current(p, "expected trait name after 'trait'");
        return NULL;
    }
    advance(p);
    char *name = str_dup_n(p->previous.start, p->previous.length);

    consume(p, TOKEN_LBRACE, "expected '{' after trait name");

    AstNode **sigs = NULL;
    int sig_count = 0;
    int sig_cap = 0;

    while (!check(p, TOKEN_RBRACE) && !check(p, TOKEN_EOF)) {
        bool sig_static = false;
        if (match_tok(p, TOKEN_STATIC)) {
            sig_static = true;
        }
        if (!match_tok(p, TOKEN_FN)) {
            error_at_current(p, "expected 'fn' in trait body");
            synchronize(p);
            continue;
        }
        AstNode *sig = parse_fn_signature(p);
        if (sig == NULL) {
            synchronize(p);
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
    consume(p, TOKEN_RBRACE, "expected '}' after trait body");

    AstNode *n = new_node(AST_TRAIT_DECL, line, col);
    n->as.trait_decl.name = name;
    n->as.trait_decl.method_sigs = sigs;
    n->as.trait_decl.method_sig_count = sig_count;
    return n;
}

static AstNode *parse_impl_decl(Parser *p) {
    /* 'impl' already consumed */
    int line = p->previous.line;
    int col  = p->previous.column;

    /* G1.5: optional type param list — impl(T, U) ... { ... }. Parsed FIRST so it
       applies to both inherent impls (impl(T) Stack(T)) and generic trait impls
       (impl(T) Add for Complex(T)). */
    char **type_params = NULL;
    int type_param_count = 0;
    int type_param_cap = 0;

    if (check(p, TOKEN_LPAREN)) {
        advance(p); /* consume '(' */
        if (!check(p, TOKEN_RPAREN)) {
            do {
                if (!check(p, TOKEN_IDENTIFIER)) {
                    error_at_current(p, "expected type parameter name in impl(...)");
                    break;
                }
                advance(p);
                char *tpname = str_dup_n(p->previous.start, p->previous.length);
                if (type_param_count >= type_param_cap) {
                    type_param_cap = GROW_CAPACITY(type_param_cap);
                    type_params = GROW_ARRAY(char *, type_params, type_param_cap);
                }
                type_params[type_param_count++] = tpname;
            } while (match_tok(p, TOKEN_COMMA));
        }
        consume(p, TOKEN_RPAREN, "expected ')' after impl type params");
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
                error_at_current(p, "expected type name after 'for' in impl");
                free(trait_name);
                for (int i = 0; i < type_param_count; i++) free(type_params[i]);
                free(type_params);
                return NULL;
            }
            advance(p);
            char *struct_name = str_dup_n(p->previous.start, p->previous.length);

            /* generic target args — impl(T) Trait for Complex(T): consume the (T) */
            if (check(p, TOKEN_LPAREN)) {
                advance(p); /* consume '(' */
                while (!check(p, TOKEN_RPAREN) && !check(p, TOKEN_EOF))
                    advance(p);
                consume(p, TOKEN_RPAREN, "expected ')' after impl target type params");
            }

            consume(p, TOKEN_LBRACE, "expected '{' after struct name in trait impl");

            AstNode **methods = NULL;
            int method_count = 0;
            int method_cap = 0;

            while (!check(p, TOKEN_RBRACE) && !check(p, TOKEN_EOF)) {
                bool is_static = false;
                if (match_tok(p, TOKEN_STATIC)) {
                    is_static = true;
                }
                if (!match_tok(p, TOKEN_FN)) {
                    error_at_current(p, "expected 'fn' in trait impl block");
                    synchronize(p);
                    continue;
                }
                /* trait-impl methods may use operator symbol names (fn +, fn ==, ...) */
                AstNode *method = parse_fn_decl(p, /*allow_operator_name=*/true);
                if (method == NULL) {
                    synchronize(p);
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
            consume(p, TOKEN_RBRACE, "expected '}' after trait impl block");

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

    if (!check(p, TOKEN_IDENTIFIER)) {
        error_at_current(p, "expected struct name after 'impl'");
        for (int i = 0; i < type_param_count; i++) free(type_params[i]);
        free(type_params);
        return NULL;
    }
    advance(p);
    char *name = str_dup_n(p->previous.start, p->previous.length);

    /* G1.5: skip target type params — impl(T) Stack(T) — the second (T) */
    if (check(p, TOKEN_LPAREN)) {
        advance(p); /* consume '(' */
        while (!check(p, TOKEN_RPAREN) && !check(p, TOKEN_EOF))
            advance(p);
        consume(p, TOKEN_RPAREN, "expected ')' after impl target type params");
    }

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
        /* plain impl methods: operator symbol names not allowed here */
        AstNode *method = parse_fn_decl(p, /*allow_operator_name=*/false);
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
    n->as.impl_decl.type_params = type_params;
    n->as.impl_decl.type_param_count = type_param_count;
    n->as.impl_decl.methods = methods;
    n->as.impl_decl.method_count = method_count;
    return n;
}

/* type Name = T — type alias (Phase A closure prerequisite). The target
   type is parsed without the return-type guard, so `type X = Block(...) -> Y`
   is the canonical site where Block appears. */
static AstNode *parse_type_alias_decl(Parser *p) {
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

/* A module-path segment is normally an identifier, but a few type keywords are
   valid std module file names. */
static bool is_import_path_segment(TokenType t) {
    return t == TOKEN_IDENTIFIER ||
           t == TOKEN_ARRAY;
}

static AstNode *parse_import_decl(Parser *p) {
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
        /* Nested top-level definitions inside a block are not supported
           (codegen has no place to emit them and would produce a basic
           block with no terminator). Reject cleanly here at parse time. */
        const char *nested_what = NULL;
        switch (stmt->kind) {
            case AST_FN_DECL:         nested_what = "function"; break;
            case AST_STRUCT_DECL:     nested_what = "struct"; break;
            case AST_ENUM_DECL:       nested_what = "enum"; break;
            case AST_IMPL_DECL:
            case AST_IMPL_TRAIT_DECL: nested_what = "impl"; break;
            case AST_TRAIT_DECL:      nested_what = "trait"; break;
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
    init_parse_rules();

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
