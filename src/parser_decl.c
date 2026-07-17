/* parser_decl.c — declaration parsers: var/fn/struct/enum/trait/impl/
   type-alias/module/import/load-lib/extern, plus the starts_var_decl
   lookahead heuristic. See parser_internal.h for the roster and the
   cross-TU surface (Task 4.3, docs/plan_arch_round2_backlog.md Batch 4). */
#include "parser_internal.h"
#include "common.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

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

/* One interface-body member: `[static] fn sig(...) [-> T];`. Returns NULL
   (member parse failed) to signal the parse_body_items driver to recover. */
static AstNode *parse_one_trait_sig(Parser *p, void *ctx) {
    (void)ctx;
    bool sig_static = false;
    if (match_tok(p, TOKEN_STATIC)) {
        sig_static = true;
    }
    if (!match_tok(p, TOKEN_FN)) {
        error_at_current(p, "expected 'def' in interface body");
        return NULL;
    }
    AstNode *sig = parse_fn_signature(p);
    if (sig == NULL) return NULL;
    sig->as.fn_decl.is_static = sig_static;
    /* optional semicolon / newline separator */
    match_tok(p, TOKEN_SEMICOLON);
    return sig;
}

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
    parse_body_items(p, TOKEN_RBRACE, parse_one_trait_sig, NULL,
                      &sigs, &sig_count);
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

/* One `methods` (impl) body member: `[static] fn name(...) { ... }`. Shared
   by the three near-identical methods-block loops below (trait-impl `for`
   form / merged `Type: Interface` form / plain `methods Type { ... }` form)
   via the parse_body_items driver. */
typedef struct {
    char *owner_name;         /* stamped onto fn_decl.impl_struct_name */
    bool allow_operator_name; /* trait-impl forms allow `fn +`/`fn ==`/... */
    const char *missing_fn_msg;
} ImplMethodCtx;

static AstNode *parse_one_impl_method(Parser *p, void *ctx_) {
    ImplMethodCtx *ctx = (ImplMethodCtx *)ctx_;
    bool is_static = false;
    if (match_tok(p, TOKEN_STATIC)) {
        is_static = true;
    }
    if (!match_tok(p, TOKEN_FN)) {
        error_at_current(p, ctx->missing_fn_msg);
        return NULL;
    }
    AstNode *method = parse_fn_decl(p, ctx->allow_operator_name);
    if (method == NULL) return NULL;
    method->as.fn_decl.is_static = is_static;
    method->as.fn_decl.impl_struct_name = ctx->owner_name;
    return method;
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
            /* trait-impl methods may use operator symbol names (fn +, fn ==, ...) */
            ImplMethodCtx mctx = { struct_name, /*allow_operator_name=*/true,
                                    "expected 'def' in interface methods block" };
            parse_body_items(p, TOKEN_RBRACE, parse_one_impl_method, &mctx,
                              &methods, &method_count);
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
        /* interface methods may use operator symbol names (fn +, fn ==, ...);
           impl_struct_name = name (shared, mirrors the legacy `for` branch) */
        ImplMethodCtx mt_ctx = { name, /*allow_operator_name=*/true,
                                  "expected 'def' in methods block" };
        parse_body_items(p, TOKEN_RBRACE, parse_one_impl_method, &mt_ctx,
                          &mt_methods, &mt_method_count);
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
    /* plain impl methods: operator symbol names not allowed here */
    ImplMethodCtx ctx = { name, /*allow_operator_name=*/false,
                           "expected 'def' in methods block" };
    parse_body_items(p, TOKEN_RBRACE, parse_one_impl_method, &ctx,
                      &methods, &method_count);
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

