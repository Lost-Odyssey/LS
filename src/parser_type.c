/* parser_type.c -- type-node parser: is_type_keyword and parse_type_inner.
   See parser_internal.h for the roster and the cross-TU surface (Task 4.3,
   docs/plan_arch_round2_backlog.md Batch 4). */
#include "parser_internal.h"
#include "common.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

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

