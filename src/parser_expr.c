/* parser_expr.c — Pratt expression parser: `prefix_*`/`infix_*` handlers,
   the rules[] table, get_rule, and the recursive-descent core
   (parse_expr_prec_inner). See parser_internal.h for the roster and the
   cross-TU surface (Task 4.3, docs/plan_arch_round2_backlog.md Batch 4). */
#include "parser_internal.h"
#include "common.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <math.h>

/* ---- Prefix parse functions ---- */

static AstNode *prefix_int_lit(Parser *p) {
    Token tok = p->previous;
    long long val = 0;
    char buf[256];
    if (tok.length >= (int)sizeof(buf)) {
        /* Refuse rather than truncate: a silently shortened token parses to a
           wrong constant (e.g. a 70-digit binary literal cut to 61 digits
           fits u64 and never trips ERANGE below). */
        error_at_previous_no_panic(p, "numeric literal too long");
    } else {
        memcpy(buf, tok.start, (size_t)tok.length);
        buf[tok.length] = '\0';
        /* Use strtoull (not strtoll): an int literal is always a non-negative
           magnitude (a leading '-' is a separate unary-minus token), so unsigned
           parsing covers the full u64 range without saturating bit63-set values
           like 0xAABBCCDDEE112233. The bits are stored verbatim into long long. */
        unsigned long long uv;
        errno = 0;
        if (tok.length > 2 && buf[0] == '0' && (buf[1] == 'x' || buf[1] == 'X')) {
            uv = strtoull(buf + 2, NULL, 16);
        } else if (tok.length > 2 && buf[0] == '0' && (buf[1] == 'b' || buf[1] == 'B')) {
            uv = strtoull(buf + 2, NULL, 2);
        } else {
            uv = strtoull(buf, NULL, 10);
        }
        if (errno == ERANGE) {
            /* strtoull saturates to ULLONG_MAX on overflow — a silently
               miscompiled constant. Reject instead. */
            error_at_previous_no_panic(p,
                "integer literal out of range (does not fit in 64 bits)");
            uv = 0;
        }
        val = (long long)uv;
    }
    AstNode *n = new_node(AST_INT_LIT, tok.line, tok.column);
    n->as.int_lit.value = val;
    return n;
}

static AstNode *prefix_float_lit(Parser *p) {
    Token tok = p->previous;
    double val = 0.0;
    char buf[256];
    if (tok.length >= (int)sizeof(buf)) {
        error_at_previous_no_panic(p, "numeric literal too long");
    } else {
        memcpy(buf, tok.start, (size_t)tok.length);
        buf[tok.length] = '\0';
        errno = 0;
        val = strtod(buf, NULL);
        /* ERANGE covers both overflow (±HUGE_VAL) and underflow (denormal or
           zero result). Only overflow is an error — denormal literals like
           5e-324 are legal and must keep parsing. */
        if (errno == ERANGE && (val >= HUGE_VAL || val <= -HUGE_VAL)) {
            error_at_previous_no_panic(p, "float literal magnitude too large for f64");
            val = 0.0;
        }
    }
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
        bool saw_type_kw = false;   /* any depth-1 arg is a type keyword (int/f64/..) */
        while (depth > 0) {
            if (p->current.type == TOKEN_EOF) { ok = false; break; }
            if (p->current.type == TOKEN_LPAREN) depth++;
            else if (p->current.type == TOKEN_RPAREN) {
                depth--;
                if (depth == 0) break;
            }
            else if (depth == 1 && is_type_keyword(p->current.type)) saw_type_kw = true;
            advance(p);
        }
        bool is_generic_struct_lit = false;
        bool is_param_type_static = false;  /* ③: Box(int).method() — parameterized
                                               type, static call. Only when an arg is a
                                               type KEYWORD (unambiguously a type — a
                                               value-arg call never has `int` as an arg),
                                               so `foo(x).bar()` chains are untouched.
                                               User-type args (`Box(Str)`) parse as a
                                               normal call and the checker reinterprets. */
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
            } else if (p->current.type == TOKEN_DOT && saw_type_kw) {
                is_param_type_static = true;
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

        if (is_param_type_static) {
            /* IDENT '(' type_arg, ... ')' followed by '.' — a parameterized-type
               name used as an expression, for a static call (`Box(int).reflect()`).
               Build an AST_IDENT carrying the type args; the infix '.' + call parse
               normally, and the checker instantiates name(type_args) for static
               dispatch. (Only reached when an arg is a type keyword — see the peek.) */
            AstNode *idn = new_node(AST_IDENT, tok.line, tok.column);
            idn->as.ident.name = str_dup_n(tok.start, tok.length);
            idn->as.ident.type_args = NULL;
            idn->as.ident.type_arg_count = 0;
            advance(p); /* consume '(' */
            int ta_cap = 0;
            if (!check(p, TOKEN_RPAREN)) {
                do {
                    TypeNode *ta = parse_type(p);
                    if (ta == NULL) { synchronize(p); break; }
                    if (idn->as.ident.type_arg_count >= ta_cap) {
                        ta_cap = GROW_CAPACITY(ta_cap);
                        idn->as.ident.type_args = GROW_ARRAY(TypeNode *,
                            idn->as.ident.type_args, ta_cap);
                    }
                    idn->as.ident.type_args[idn->as.ident.type_arg_count++] = ta;
                } while (match_tok(p, TOKEN_COMMA));
            }
            consume(p, TOKEN_RPAREN, "expected ')' after parameterized type arguments");
            return idn;
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

/* @print — the print intrinsic (a dedicated token, like @time/@bench). Produces
   the IDENT "@print"; the Pratt call-infix then applies the (args) -> AST_CALL
   callee "@print", which the checker/codegen recognize. Bare "print" is no longer
   a registered builtin, so print(...) is a clear "undefined" error. Stage F. */
static AstNode *prefix_at_print(Parser *p) {
    Token tok = p->previous; /* TOKEN_AT_PRINT */
    AstNode *n = new_node(AST_IDENT, tok.line, tok.column);
    n->as.ident.name = str_dup_n("@print", 6);
    return n;
}

/* @take/@dispose/@dup/@move — place/ownership intrinsics. Mirrors prefix_at_print:
   produce the IDENT whose name IS the lexeme ("@take"), so the Pratt call-infix
   forms AST_CALL callee "@take", which the checker/codegen recognize via
   intrinsic_lookup. */
static AstNode *prefix_at_intrinsic(Parser *p) {
    Token tok = p->previous; /* TOKEN_AT_INTRINSIC, lexeme spans "@name" */
    AstNode *n = new_node(AST_IDENT, tok.line, tok.column);
    n->as.ident.name = str_dup_n(tok.start, tok.length);
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
    consume(p, TOKEN_LPAREN, "expected '(' after 'def' in closure");

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

/* PrefixFn/InfixFn/ParseRule: local to the Pratt rule table below (and the
   `prefix_*`/`infix_*` handlers above it) — not part of the cross-TU
   surface, so they live here rather than in parser_internal.h. */
typedef AstNode *(*PrefixFn)(Parser *p);
typedef AstNode *(*InfixFn)(Parser *p, AstNode *left);

typedef struct {
    PrefixFn prefix;
    InfixFn  infix;
    Precedence precedence;
} ParseRule;

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

/* True when no expression follows `..` (an open-ended range like `a..` or `..`),
   detected by an immediate closer. Enables slice open ranges `v[a..]`/`v[..b]`. */
static bool range_bound_absent(TokenType t) {
    return t == TOKEN_RBRACKET || t == TOKEN_RPAREN || t == TOKEN_COMMA ||
           t == TOKEN_SEMICOLON || t == TOKEN_EOF ||
           t == TOKEN_LBRACE || t == TOKEN_RBRACE;
}

/* Range expression: a..b — creates AST_RANGE. Open forms `a..` (end=NULL) are
   produced when no expression follows the `..` (e.g. `v[a..]`). */
static AstNode *infix_range(Parser *p, AstNode *left) {
    Token op = p->previous;
    AstNode *right = NULL;
    if (!range_bound_absent(p->current.type)) {
        right = parse_expr_prec(p, PREC_COMPARISON);
        if (right == NULL) {
            ast_free(left);
            return NULL;
        }
    }
    AstNode *n = new_node(AST_RANGE, op.line, op.column);
    n->as.range.start = left;
    n->as.range.end = right;
    return n;
}

/* Prefix range `..b` (start=NULL) / `..` (both NULL) — for slice open ranges
   `v[..b]` / `v[..]`. */
static AstNode *prefix_range(Parser *p) {
    Token op = p->previous;
    AstNode *end = NULL;
    if (!range_bound_absent(p->current.type)) {
        end = parse_expr_prec(p, PREC_COMPARISON);
        if (end == NULL)
            return NULL;
    }
    AstNode *n = new_node(AST_RANGE, op.line, op.column);
    n->as.range.start = NULL;
    n->as.range.end = end;
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

    /* __type_name(Type): like sizeof, the operand is a TYPE, not an expression.
       Produces a compile-time type-name Str (a type-param `T` resolves to the
       concrete type per monomorphization). Used by @derive(Reflect) to report a
       generic field's concrete instantiated type name. */
    if (left->kind == AST_IDENT && strcmp(left->as.ident.name, "__type_name") == 0) {
        TypeNode *t = parse_type(p);
        consume(p, TOKEN_RPAREN, "expected ')' after __type_name type");
        AstNode *n = new_node(AST_TYPENAME, call_tok.line, call_tok.column);
        n->as.typename_expr.type_node  = t;
        n->as.typename_expr.named_type = NULL;
        ast_free(left);
        return n;
    }

    /* G2: detect generic function call — ident(TypeArgs)(args).
       If left is AST_IDENT and the first token after '(' is a type keyword
       or uppercase identifier followed by ',' or ')', this is a type arg list. */
    TypeNode **call_type_args = NULL;
    int call_type_arg_count = 0;
    bool ta_skip_valargs = false;  /* type args followed directly by a trailing closure */
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
                        /* Check the token immediately after the closing ')'.
                           A real generic call is `(types)(args)`, but a trailing
                           closure may follow the type args directly:
                           `v.map(int) { |x| ... }` — confirm that too so it
                           desugars to `v.map(int)(|x| ...)`. */
                        if (p->current.type == TOKEN_LPAREN) {
                            confirmed = true;
                        } else if (p->current.type == TOKEN_LBRACE) {
                            Token tnext = scanner_peek(&p->scanner);
                            if (tnext.type == TOKEN_PIPE || tnext.type == TOKEN_OR)
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
                /* Trailing-closure value arg directly after the type args:
                   `v.map(int) { |x| ... }` == `v.map(int)(|x| ...)`. When a
                   `{ |` follows, skip the `(value args)` list — the trailing-
                   closure block below parses the closure as the sole arg. */
                if (check(p, TOKEN_LBRACE)) {
                    Token tnext = scanner_peek(&p->scanner);
                    if (tnext.type == TOKEN_PIPE || tnext.type == TOKEN_OR)
                        ta_skip_valargs = true;
                }
                if (!ta_skip_valargs)
                    consume(p, TOKEN_LPAREN, "expected '(' after generic type arguments");
            }
        }
    }

    AstNode **args = NULL;
    int arg_count = 0;
    int arg_cap = 0;

    if (!ta_skip_valargs) {
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
    }

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
                if (s == NULL) { recover_in_body(p); continue; }
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
    /* comptime field-by-handle access: `v.(f)` — a '.' immediately followed by '('.
       The handle is a comptime loop variable bound by `comptime for f in fields(T)`;
       it lowers to a concrete field access `v.<name>` during comptime unroll (checker).
       Unambiguous: range `..` is a single token (TOKEN_DOTDOT), so `.` + '(' only
       arises here. */
    if (check(p, TOKEN_LPAREN)) {
        advance(p); /* consume '(' */
        if (!check(p, TOKEN_IDENTIFIER)) {
            error_at_current(p, "expected comptime field handle after '.('");
            ast_free(left);
            return NULL;
        }
        char *handle = str_dup_n(p->current.start, p->current.length);
        advance(p);
        consume(p, TOKEN_RPAREN, "expected ')' after comptime field handle");
        AstNode *n = new_node(AST_COMPTIME_FIELD, dot_tok.line, dot_tok.column);
        n->as.comptime_field.object = left;
        n->as.comptime_field.handle = handle;
        return n;
    }
    /* Field names can be identifiers, 'self', or keywords used as method names
       (e.g. `array` is a type keyword but a valid method name; `new` is reserved
       for allocation but `Task.new`/`X.new` reads as a Ruby-style constructor —
       unambiguous in member position). `type` (the type-alias keyword) is accepted
       in member position for the comptime `f.type` handle (lowered to the field's
       concrete type during unroll); unambiguous here since a `.` precedes it. */
    if (!check(p, TOKEN_IDENTIFIER) && !check(p, TOKEN_SELF) &&
        !check(p, TOKEN_ARRAY) && !check(p, TOKEN_NEW) &&
        !check(p, TOKEN_TYPE_ALIAS))
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

/* Compile-time constant table. Every entry not listed here is
   zero-initialized (NULL prefix, NULL infix, PREC_NONE), matching the
   previous runtime-filled table's memset(rules, 0, sizeof(rules)) default. */
static const ParseRule rules[TOKEN_ERROR + 1] = {
    /* Literals */
    [TOKEN_INT_LIT]    = { prefix_int_lit,   NULL,              PREC_NONE },
    [TOKEN_FLOAT_LIT]  = { prefix_float_lit, NULL,              PREC_NONE },
    [TOKEN_STRING_LIT] = { prefix_string_lit,NULL,              PREC_NONE },
    [TOKEN_FSTRING_START]= { prefix_fstring,   NULL,              PREC_NONE },
    [TOKEN_CHAR_LIT]   = { prefix_char_lit,  NULL,              PREC_NONE },
    [TOKEN_TRUE]       = { prefix_true,      NULL,              PREC_NONE },
    [TOKEN_FALSE]      = { prefix_false,     NULL,              PREC_NONE },
    [TOKEN_NIL]        = { prefix_nil,       NULL,              PREC_NONE },

    /* Identifier, self, and underscore */
    [TOKEN_IDENTIFIER] = { prefix_ident,     NULL,              PREC_NONE },
    [TOKEN_SELF]       = { prefix_ident,     NULL,              PREC_NONE },
    [TOKEN_UNDERSCORE] = { prefix_underscore,NULL,              PREC_NONE },

    /* Grouping */
    [TOKEN_LPAREN]     = { prefix_grouping,  infix_call,        PREC_CALL },
    [TOKEN_LBRACKET]   = { prefix_array_lit,  infix_index,       PREC_CALL },
    [TOKEN_LBRACE]     = { prefix_map_lit,    NULL,              PREC_NONE },
    [TOKEN_DOT]        = { NULL,             infix_field,       PREC_CALL },
    [TOKEN_AS]         = { NULL,             infix_cast,        PREC_CALL },

    /* Arithmetic */
    [TOKEN_PLUS]       = { NULL,             infix_binary_real, PREC_TERM },
    [TOKEN_MINUS]      = { prefix_unary,     infix_binary_real, PREC_TERM },
    [TOKEN_STAR]       = { prefix_deref,     infix_binary_real, PREC_FACTOR },
    [TOKEN_SLASH]      = { NULL,             infix_binary_real, PREC_FACTOR },
    [TOKEN_PERCENT]    = { NULL,             infix_binary_real, PREC_FACTOR },

    /* Unary-only */
    [TOKEN_BANG]       = { prefix_unary,     infix_force_unwrap, PREC_CALL },
    [TOKEN_TILDE]      = { prefix_unary,     NULL,              PREC_NONE },
    [TOKEN_AMP]        = { prefix_addr,      infix_binary_real, PREC_BITAND },

    /* Bitwise */
    [TOKEN_PIPE]       = { prefix_ruby_closure, infix_binary_real, PREC_BITOR },
    [TOKEN_CARET]      = { NULL,             infix_binary_real, PREC_BITXOR },
    [TOKEN_LSHIFT]     = { NULL,             infix_binary_real, PREC_SHIFT },
    [TOKEN_RSHIFT]     = { NULL,             infix_binary_real, PREC_SHIFT },

    /* Logical */
    [TOKEN_AND]        = { NULL,             infix_binary_real, PREC_AND },
    [TOKEN_OR]         = { prefix_no_arg_closure, infix_binary_real, PREC_OR },

    /* Comparison */
    [TOKEN_DOTDOT]     = { prefix_range,     infix_range,       PREC_COMPARISON },
    [TOKEN_EQ]         = { NULL,             infix_binary_real, PREC_EQUALITY },
    [TOKEN_NEQ]        = { NULL,             infix_binary_real, PREC_EQUALITY },
    [TOKEN_LT]         = { NULL,             infix_binary_real, PREC_COMPARISON },
    [TOKEN_GT]         = { NULL,             infix_binary_real, PREC_COMPARISON },
    [TOKEN_LEQ]        = { NULL,             infix_binary_real, PREC_COMPARISON },
    [TOKEN_GEQ]        = { NULL,             infix_binary_real, PREC_COMPARISON },

    /* Assignment (right-assoc) */
    [TOKEN_ASSIGN]       = { NULL, infix_assign, PREC_ASSIGNMENT },
    [TOKEN_PLUS_ASSIGN]  = { NULL, infix_assign, PREC_ASSIGNMENT },
    [TOKEN_MINUS_ASSIGN] = { NULL, infix_assign, PREC_ASSIGNMENT },
    [TOKEN_STAR_ASSIGN]  = { NULL, infix_assign, PREC_ASSIGNMENT },
    [TOKEN_SLASH_ASSIGN] = { NULL, infix_assign, PREC_ASSIGNMENT },

    /* Keywords as prefix */
    [TOKEN_FN]         = { prefix_closure,   NULL,              PREC_NONE },
    [TOKEN_MATCH]      = { prefix_match,     NULL,              PREC_NONE },
    [TOKEN_COLON]      = { prefix_symbol,    NULL,              PREC_NONE },
    [TOKEN_NEW]        = { prefix_new_expr,  NULL,              PREC_NONE },
    [TOKEN_TRY]        = { prefix_try,       NULL,              PREC_NONE },
    [TOKEN_AT_TIME]    = { prefix_at_time,   NULL,              PREC_NONE },
    [TOKEN_AT_BENCH]   = { prefix_at_bench,  NULL,              PREC_NONE },
    [TOKEN_AT_PRINT]   = { prefix_at_print,  NULL,              PREC_NONE },
    [TOKEN_AT_INTRINSIC] = { prefix_at_intrinsic, NULL,          PREC_NONE },
};

static const ParseRule *get_rule(TokenType type) {
    if ((int)type < 0 || (int)type > (int)TOKEN_ERROR) return &rules[TOKEN_ERROR];
    return &rules[type];
}

/* ---- Core expression parser ---- */

AstNode *parse_expr_prec_inner(Parser *p, Precedence min_prec) {
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
            /* Also handle *StructName varName / *TypeParam varName — a pointer
               var decl starting the next statement. Allowed when EITHER:
               (a) we're at a statement boundary (top-level expr stmt), OR
               (b) the `*` opens a NEW LINE — genuine infix `a * b` always has its
                   operands on the operator's line, so a line-leading `* Ident
                   Ident` can only be a pointer decl. (b) is what catches the
                   RHS-of-assignment case `self.n = 8` ⏎ `*K p = ...`, where the
                   RHS `8` is parsed in a nested call (allow_decl_break == false)
                   — mirrors the `&` borrow-decl disambiguation just below. (L-003)
               Inside an initializer / args / parens on the SAME line, `ident *
               ident` stays multiplication (e.g. `int e = a * b`). */
            if (after_star.type == TOKEN_IDENTIFIER &&
                (allow_decl_break || p->current.line > p->previous.line)) {
                /* Peek two tokens ahead: if `*Ident Ident`, it's a var decl —
                   BUT only when the var-name Ident is on the SAME line as the
                   `*`. A real decl `*K p` is all on one line; this rejects the
                   false positive `a` ⏎ `* b` ⏎ `print` (cross-line multiplication
                   `a*b` where the following statement's leading Ident `print`
                   would otherwise look like the var name). */
                Scanner saved_scan = p->scanner;
                scanner_next(&saved_scan); /* consume the Identifier (type name) */
                Token after_ident = scanner_next(&saved_scan); /* var-name token */
                if (after_ident.type == TOKEN_IDENTIFIER &&
                    after_ident.line == p->current.line) break;
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
            if (t1.type == TOKEN_BANG) {                 /* &! ... */
                Token t2 = scanner_next(&sc);
                if (t2.type == TOKEN_ARRAY) {
                    decl_shape = true;                   /* &!array(T) name — slice decl */
                } else {
                    Token t3 = scanner_next(&sc);
                    if ((is_type_keyword(t2.type) || t2.type == TOKEN_IDENTIFIER) &&
                        t3.type == TOKEN_IDENTIFIER)
                        decl_shape = true;               /* &!Type name */
                }
            } else if (t1.type == TOKEN_ARRAY) {
                decl_shape = true;                       /* &array(T) name — slice decl */
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

