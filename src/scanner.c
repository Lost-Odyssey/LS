/* scanner.c — Lexical analyzer: source text -> token stream */
#include "scanner.h"
#include "common.h"
#include <ctype.h>

/* ---- Helpers ---- */

static bool is_at_end(Scanner *s) {
    return *s->current == '\0';
}

static char advance(Scanner *s) {
    char c = *s->current++;
    s->column++;
    return c;
}

static char peek(Scanner *s) {
    return *s->current;
}

static char peek_next(Scanner *s) {
    if (is_at_end(s)) return '\0';
    return s->current[1];
}

static bool match(Scanner *s, char expected) {
    if (is_at_end(s)) return false;
    if (*s->current != expected) return false;
    s->current++;
    s->column++;
    return true;
}

static Token make_token(Scanner *s, TokenType type) {
    Token t;
    t.type = type;
    t.start = s->start;
    t.length = (int)(s->current - s->start);
    t.line = s->line;
    t.column = s->start_column;
    return t;
}

static Token error_token(Scanner *s, const char *message) {
    Token t;
    t.type = TOKEN_ERROR;
    t.start = message;
    t.length = (int)strlen(message);
    t.line = s->line;
    t.column = s->start_column;
    return t;
}

/* ---- Whitespace & Comments ---- */

static void skip_whitespace(Scanner *s) {
    for (;;) {
        char c = peek(s);
        switch (c) {
        case ' ':
        case '\t':
        case '\r':
            advance(s);
            break;
        case '\n':
            s->line++;
            s->current++;
            s->column = 1;
            break;
        case '/':
            if (peek_next(s) == '/') {
                /* Single-line comment */
                while (!is_at_end(s) && peek(s) != '\n') advance(s);
            } else if (peek_next(s) == '*') {
                /* Multi-line comment */
                advance(s); /* / */
                advance(s); /* * */
                int depth = 1;
                while (!is_at_end(s) && depth > 0) {
                    if (peek(s) == '/' && peek_next(s) == '*') {
                        advance(s); advance(s);
                        depth++;
                    } else if (peek(s) == '*' && peek_next(s) == '/') {
                        advance(s); advance(s);
                        depth--;
                    } else {
                        if (peek(s) == '\n') {
                            s->line++;
                            s->current++;
                            s->column = 1;
                        } else {
                            advance(s);
                        }
                    }
                }
            } else {
                return;
            }
            break;
        default:
            return;
        }
    }
}

/* ---- Keywords (sorted table + binary search) ---- */

typedef struct {
    const char *name;
    int length;
    TokenType type;
} Keyword;

static const Keyword keywords[] = {
    /* Capital letters sort first in ASCII (B < a). Keep this list strictly
       sorted by the byte sequence — the lookup uses binary search. */
    {"Block",    5, TOKEN_BLOCK},
    {"array",    5, TOKEN_ARRAY},
    {"as",       2, TOKEN_AS},
    {"bool",     4, TOKEN_TYPE_BOOL},
    {"break",    5, TOKEN_BREAK},
    {"char",     4, TOKEN_TYPE_CHAR},
    {"continue", 8, TOKEN_CONTINUE},
    {"do",       2, TOKEN_DO},
    {"else",     4, TOKEN_ELSE},
    {"end",      3, TOKEN_END},
    {"enum",     4, TOKEN_ENUM},
    {"extern",   6, TOKEN_EXTERN},
    {"f32",      3, TOKEN_TYPE_F32},
    {"f64",      3, TOKEN_TYPE_F64},
    {"false",    5, TOKEN_FALSE},
    {"fn",       2, TOKEN_FN},
    {"for",      3, TOKEN_FOR},
    {"from",     4, TOKEN_FROM},
    {"i16",      3, TOKEN_TYPE_I16},
    {"i32",      3, TOKEN_TYPE_I32},
    {"i64",      3, TOKEN_TYPE_I64},
    {"i8",       2, TOKEN_TYPE_I8},
    {"if",       2, TOKEN_IF},
    {"impl",     4, TOKEN_IMPL},
    {"import",   6, TOKEN_IMPORT},
    {"in",       2, TOKEN_IN},
    {"int",      3, TOKEN_TYPE_INT},
    {"lib",      3, TOKEN_TYPE_LIB},
    {"load",     4, TOKEN_LOAD},
    {"map",      3, TOKEN_MAP},
    {"match",    5, TOKEN_MATCH},
    {"module",   6, TOKEN_MODULE},
    {"new",      3, TOKEN_NEW},
    {"nil",      3, TOKEN_NIL},
    {"object",   6, TOKEN_TYPE_OBJECT},
    {"pub",      3, TOKEN_PUB},
    {"return",   6, TOKEN_RETURN},
    {"self",     4, TOKEN_SELF},
    {"static",   6, TOKEN_STATIC},
    {"string",   6, TOKEN_TYPE_STRING},
    {"struct",   6, TOKEN_STRUCT},
    {"trait",    5, TOKEN_TRAIT},
    {"true",     4, TOKEN_TRUE},
    {"try",      3, TOKEN_TRY},
    {"type",     4, TOKEN_TYPE_ALIAS},
    {"u16",      3, TOKEN_TYPE_U16},
    {"u32",      3, TOKEN_TYPE_U32},
    {"u64",      3, TOKEN_TYPE_U64},
    {"u8",       2, TOKEN_TYPE_U8},
    {"vec",      3, TOKEN_VEC},
    {"void",     4, TOKEN_TYPE_VOID},
    {"while",    5, TOKEN_WHILE},
};

#define KEYWORD_COUNT (sizeof(keywords) / sizeof(keywords[0]))

static TokenType check_keyword(const char *start, int length) {
    /* Binary search on sorted keyword table */
    int lo = 0, hi = (int)KEYWORD_COUNT - 1;
    while (lo <= hi) {
        int mid = (lo + hi) / 2;
        int cmp = strncmp(start, keywords[mid].name, (size_t)length);
        if (cmp == 0) {
            /* Prefix matches — check exact length */
            if (length < keywords[mid].length) {
                hi = mid - 1;
            } else if (length > keywords[mid].length) {
                lo = mid + 1;
            } else {
                return keywords[mid].type;
            }
        } else if (cmp < 0) {
            hi = mid - 1;
        } else {
            lo = mid + 1;
        }
    }
    return TOKEN_IDENTIFIER;
}

/* ---- Number Literals ---- */

static Token scan_number(Scanner *s) {
    bool is_float = false;

    /* Check for hex/binary prefix */
    if (s->start[0] == '0' && (int)(s->current - s->start) == 1) {
        if (peek(s) == 'x' || peek(s) == 'X') {
            advance(s); /* skip x */
            if (!isxdigit((unsigned char)peek(s))) {
                return error_token(s, "invalid hex literal");
            }
            while (isxdigit((unsigned char)peek(s))) advance(s);
            return make_token(s, TOKEN_INT_LIT);
        }
        if (peek(s) == 'b' || peek(s) == 'B') {
            advance(s); /* skip b */
            if (peek(s) != '0' && peek(s) != '1') {
                return error_token(s, "invalid binary literal");
            }
            while (peek(s) == '0' || peek(s) == '1') advance(s);
            return make_token(s, TOKEN_INT_LIT);
        }
    }

    /* Decimal digits */
    while (isdigit((unsigned char)peek(s))) advance(s);

    /* Fractional part */
    if (peek(s) == '.' && isdigit((unsigned char)peek_next(s))) {
        is_float = true;
        advance(s); /* consume '.' */
        while (isdigit((unsigned char)peek(s))) advance(s);
    }

    /* Exponent part */
    if (peek(s) == 'e' || peek(s) == 'E') {
        is_float = true;
        advance(s);
        if (peek(s) == '+' || peek(s) == '-') advance(s);
        if (!isdigit((unsigned char)peek(s))) {
            return error_token(s, "invalid number: expected digit after exponent");
        }
        while (isdigit((unsigned char)peek(s))) advance(s);
    }

    return make_token(s, is_float ? TOKEN_FLOAT_LIT : TOKEN_INT_LIT);
}

/* ---- String Literals ---- */

static Token scan_string(Scanner *s) {
    while (!is_at_end(s) && peek(s) != '"') {
        if (peek(s) == '\n') {
            s->line++;
            s->current++;
            s->column = 1;
            continue;
        }
        if (peek(s) == '\\') {
            advance(s); /* skip backslash */
            if (is_at_end(s)) {
                return error_token(s, "unterminated string escape");
            }
            char esc = peek(s);
            switch (esc) {
            case 'n': case 't': case '\\': case '"':
            case '0': case 'r': case 'a': case 'b':
                advance(s);
                break;
            case 'x':
                advance(s); /* skip 'x' */
                if (!isxdigit((unsigned char)peek(s)) ||
                    !isxdigit((unsigned char)peek_next(s))) {
                    return error_token(s, "invalid \\xHH escape");
                }
                advance(s);
                advance(s);
                break;
            default:
                return error_token(s, "unknown escape sequence");
            }
        } else {
            advance(s);
        }
    }
    if (is_at_end(s)) {
        return error_token(s, "unterminated string");
    }
    advance(s); /* closing '"' */
    return make_token(s, TOKEN_STRING_LIT);
}

/* ---- Char Literals ---- */

static Token scan_char(Scanner *s) {
    if (is_at_end(s)) {
        return error_token(s, "unterminated char literal");
    }
    if (peek(s) == '\\') {
        advance(s); /* backslash */
        if (is_at_end(s)) return error_token(s, "unterminated char escape");
        advance(s); /* escaped char */
    } else {
        advance(s); /* the character */
    }
    if (peek(s) != '\'') {
        return error_token(s, "unterminated char literal");
    }
    advance(s); /* closing '\'' */
    return make_token(s, TOKEN_CHAR_LIT);
}

/* ---- Identifier / Keyword ---- */

static Token scan_identifier(Scanner *s) {
    while (isalnum((unsigned char)peek(s)) || peek(s) == '_') advance(s);

    int length = (int)(s->current - s->start);

    /* Single underscore is TOKEN_UNDERSCORE (match wildcard) */
    if (length == 1 && s->start[0] == '_') {
        return make_token(s, TOKEN_UNDERSCORE);
    }

    /* f"..." format string: single 'f' followed by '"' */
    if (length == 1 && s->start[0] == 'f' && peek(s) == '"') {
        advance(s); /* consume the opening '"' */
        s->in_fstring = true;
        s->fstring_brace_depth = 0;
        return make_token(s, TOKEN_FSTRING_START);
    }

    TokenType type = check_keyword(s->start, length);
    return make_token(s, type);
}

/* ---- Format string text segment ---- */

static Token scan_fstring_text(Scanner *s) {
    /* Scan text inside f"..." up to '{' or closing '"' */
    s->start = s->current;
    s->start_column = s->column;

    while (!is_at_end(s) && peek(s) != '{' && peek(s) != '"') {
        if (peek(s) == '\\') {
            advance(s); /* skip backslash */
            if (!is_at_end(s)) advance(s); /* skip escaped char */
        } else if (peek(s) == '\n') {
            s->line++;
            s->current++;
            s->column = 1;
        } else {
            advance(s);
        }
    }

    if (s->current > s->start) {
        return make_token(s, TOKEN_FSTRING_TEXT);
    }

    /* No text — we're at '{' or '"', handle below */
    if (peek(s) == '"') {
        advance(s); /* consume closing '"' */
        s->in_fstring = false;
        return make_token(s, TOKEN_FSTRING_END);
    }

    /* Must be '{' — start expression */
    advance(s); /* consume '{' */
    s->fstring_brace_depth = 1;
    return make_token(s, TOKEN_LBRACE);
}

/* ---- Public API ---- */

void scanner_init(Scanner *s, const char *source) {
    s->source = source;
    s->start = source;
    s->current = source;
    s->line = 1;
    s->column = 1;
    s->start_column = 1;
    s->in_fstring = false;
    s->fstring_brace_depth = 0;
    s->cond_depth = 0;
}

/* ---- Phase E.3.2: Conditional compilation (#if / #else / #end) ---- */

/* Compile-time platform name. Source compares against this case-sensitively. */
static const char *cond_platform_name(void) {
#ifdef _WIN32
    return "WINDOWS";
#elif defined(__APPLE__)
    return "MACOS";
#else
    return "LINUX";
#endif
}

/* True if the scanner is currently emitting tokens (i.e. all enclosing
   conditional frames have active=true). */
static bool cond_is_emitting(const Scanner *s) {
    if (s->cond_depth == 0) return true;
    return s->cond_stack[s->cond_depth - 1].active;
}

/* Read an identifier-like word starting at s->current into a small buffer.
   Stops at whitespace, newline, or EOF. Returns length written (clamped). */
static int read_directive_word(Scanner *s, char *out, int max_len) {
    int n = 0;
    while (!is_at_end(s) && (isalnum((unsigned char)*s->current) || *s->current == '_')) {
        if (n + 1 < max_len) out[n++] = *s->current;
        advance(s);
    }
    out[n] = '\0';
    return n;
}

/* Skip whitespace within a directive line (does NOT advance over newlines). */
static void skip_inline_ws(Scanner *s) {
    while (!is_at_end(s) && (peek(s) == ' ' || peek(s) == '\t')) advance(s);
}

/* Process a `#name [arg]` directive starting at the `#`. The `#` itself
   should not have been consumed yet. Returns true on success; sets
   *had_error if the directive was malformed (caller still continues). */
static bool process_directive(Scanner *s, bool *had_error) {
    *had_error = false;
    /* consume `#` */
    advance(s);

    char word[32];
    int n = read_directive_word(s, word, (int)sizeof(word));
    if (n == 0) {
        *had_error = true;
        return false;
    }

    if (strcmp(word, "if") == 0) {
        skip_inline_ws(s);
        char arg[32];
        int an = read_directive_word(s, arg, (int)sizeof(arg));
        if (an == 0) { *had_error = true; return false; }

        if (s->cond_depth >= (int)(sizeof(s->cond_stack)/sizeof(s->cond_stack[0]))) {
            *had_error = true;
            return false;
        }
        bool parent_emit = cond_is_emitting(s);
        bool match_plat = (strcmp(arg, cond_platform_name()) == 0);
        bool active = parent_emit && match_plat;
        s->cond_stack[s->cond_depth].parent_active = parent_emit;
        s->cond_stack[s->cond_depth].active = active;
        s->cond_stack[s->cond_depth].branch_taken = active;
        s->cond_depth++;
        return true;
    }
    if (strcmp(word, "else") == 0) {
        if (s->cond_depth == 0) { *had_error = true; return false; }
        int top = s->cond_depth - 1;
        bool already_taken = s->cond_stack[top].branch_taken;
        bool parent_emit = s->cond_stack[top].parent_active;
        s->cond_stack[top].active = parent_emit && !already_taken;
        if (s->cond_stack[top].active) s->cond_stack[top].branch_taken = true;
        return true;
    }
    if (strcmp(word, "end") == 0 || strcmp(word, "endif") == 0) {
        if (s->cond_depth == 0) { *had_error = true; return false; }
        s->cond_depth--;
        return true;
    }

    /* Unknown directive — consume to end-of-line and report error. */
    *had_error = true;
    while (!is_at_end(s) && peek(s) != '\n') advance(s);
    return false;
}

/* Forward declaration so scanner_next_inner can recurse via wrapper. */
static Token scanner_next_inner(Scanner *s);

Token scanner_next(Scanner *s) {
    /* Phase E.3.2: loop until we get a token from an active branch.
       Handles `#if`/`#else`/`#end` directives in either branch and
       silently discards lexed tokens whose conditional frame is inactive. */
    for (;;) {
        /* Skip whitespace only when NOT in f-string text mode; inside
           f-string literal segments (depth==0) spaces are content. */
        if (!(s->in_fstring && s->fstring_brace_depth == 0))
            skip_whitespace(s);
        /* Process directives at the start of a line/inline. The `#` must
           appear with only whitespace between it and the start of file or
           a newline; we accept it anywhere whitespace-skipped, simpler. */
        if (!s->in_fstring && peek(s) == '#') {
            bool err = false;
            process_directive(s, &err);
            if (err) {
                /* Surface a clean error token so parser can synchronise. */
                return error_token(s, "malformed preprocessor directive");
            }
            continue;
        }
        if (cond_is_emitting(s)) {
            return scanner_next_inner(s);
        }
        /* Inactive branch: lex and discard one token, retry. */
        Token discarded = scanner_next_inner(s);
        if (discarded.type == TOKEN_EOF) return discarded;
        if (discarded.type == TOKEN_ERROR) return discarded;
        /* loop continues, discarding silently */
    }
}

static Token scanner_next_inner(Scanner *s) {
    /* If inside f-string and NOT inside an expression, scan text/end */
    if (s->in_fstring && s->fstring_brace_depth == 0) {
        return scan_fstring_text(s);
    }

    skip_whitespace(s);

    s->start = s->current;
    s->start_column = s->column;

    if (is_at_end(s)) return make_token(s, TOKEN_EOF);

    /* Track brace depth inside f-string expressions */
    if (s->in_fstring && s->fstring_brace_depth > 0) {
        if (*s->current == '{') {
            s->fstring_brace_depth++;
        } else if (*s->current == '}') {
            s->fstring_brace_depth--;
            if (s->fstring_brace_depth == 0) {
                /* End of f-string expression — return to text scanning */
                advance(s); /* consume '}' */
                return make_token(s, TOKEN_RBRACE);
            }
        }
    }

    char c = advance(s);

    /* Identifiers & keywords */
    if (isalpha((unsigned char)c) || c == '_') return scan_identifier(s);

    /* Number literals */
    if (isdigit((unsigned char)c)) return scan_number(s);

    switch (c) {
    /* String literal */
    case '"': return scan_string(s);
    case '\'': return scan_char(s);

    /* Single-char tokens */
    case '(': return make_token(s, TOKEN_LPAREN);
    case ')': return make_token(s, TOKEN_RPAREN);
    case '{': return make_token(s, TOKEN_LBRACE);
    case '}': return make_token(s, TOKEN_RBRACE);
    case '[': return make_token(s, TOKEN_LBRACKET);
    case ']': return make_token(s, TOKEN_RBRACKET);
    case ';': return make_token(s, TOKEN_SEMICOLON);
    case ',': return make_token(s, TOKEN_COMMA);
    case '~': return make_token(s, TOKEN_TILDE);
    case ':': return make_token(s, TOKEN_COLON);

    /* Dot, dotdot, ellipsis */
    case '.':
        if (match(s, '.')) {
            if (match(s, '.')) return make_token(s, TOKEN_ELLIPSIS);
            return make_token(s, TOKEN_DOTDOT);
        }
        return make_token(s, TOKEN_DOT);

    /* Operators with optional second char */
    case '+':
        if (match(s, '=')) return make_token(s, TOKEN_PLUS_ASSIGN);
        return make_token(s, TOKEN_PLUS);
    case '-':
        if (match(s, '>')) return make_token(s, TOKEN_ARROW);
        if (match(s, '=')) return make_token(s, TOKEN_MINUS_ASSIGN);
        return make_token(s, TOKEN_MINUS);
    case '*':
        if (match(s, '=')) return make_token(s, TOKEN_STAR_ASSIGN);
        return make_token(s, TOKEN_STAR);
    case '/':
        if (match(s, '=')) return make_token(s, TOKEN_SLASH_ASSIGN);
        return make_token(s, TOKEN_SLASH);
    case '%':
        return make_token(s, TOKEN_PERCENT);

    case '&':
        if (match(s, '&')) return make_token(s, TOKEN_AND);
        return make_token(s, TOKEN_AMP);
    case '|':
        if (match(s, '|')) return make_token(s, TOKEN_OR);
        return make_token(s, TOKEN_PIPE);
    case '^':
        return make_token(s, TOKEN_CARET);

    case '!':
        if (match(s, '=')) return make_token(s, TOKEN_NEQ);
        return make_token(s, TOKEN_BANG);
    case '=':
        if (match(s, '=')) return make_token(s, TOKEN_EQ);
        if (match(s, '>')) return make_token(s, TOKEN_FAT_ARROW);
        return make_token(s, TOKEN_ASSIGN);
    case '<':
        if (match(s, '<')) return make_token(s, TOKEN_LSHIFT);
        if (match(s, '=')) return make_token(s, TOKEN_LEQ);
        return make_token(s, TOKEN_LT);
    case '>':
        if (match(s, '>')) return make_token(s, TOKEN_RSHIFT);
        if (match(s, '=')) return make_token(s, TOKEN_GEQ);
        return make_token(s, TOKEN_GT);

    /* Annotations: @time, @bench */
    case '@':
        if (strncmp(s->current, "time", 4) == 0 &&
            !isalnum((unsigned char)s->current[4]) && s->current[4] != '_') {
            s->current += 4; s->column += 4;
            return make_token(s, TOKEN_AT_TIME);
        }
        if (strncmp(s->current, "bench", 5) == 0 &&
            !isalnum((unsigned char)s->current[5]) && s->current[5] != '_') {
            s->current += 5; s->column += 5;
            return make_token(s, TOKEN_AT_BENCH);
        }
        return error_token(s, "unknown annotation (expected @time or @bench)");
    }

    return error_token(s, "unexpected character");
}

Token scanner_peek(Scanner *s) {
    /* Save state */
    Scanner saved = *s;
    Token t = scanner_next(s);
    /* Restore state */
    *s = saved;
    return t;
}

/* ---- token_type_name ---- */

const char *token_type_name(TokenType type) {
    switch (type) {
    case TOKEN_INT_LIT:       return "INT_LIT";
    case TOKEN_FLOAT_LIT:     return "FLOAT_LIT";
    case TOKEN_STRING_LIT:    return "STRING_LIT";
    case TOKEN_CHAR_LIT:      return "CHAR_LIT";
    case TOKEN_TRUE:          return "TRUE";
    case TOKEN_FALSE:         return "FALSE";
    case TOKEN_NIL:           return "NIL";
    case TOKEN_FN:            return "FN";
    case TOKEN_RETURN:        return "RETURN";
    case TOKEN_IF:            return "IF";
    case TOKEN_ELSE:          return "ELSE";
    case TOKEN_WHILE:         return "WHILE";
    case TOKEN_FOR:           return "FOR";
    case TOKEN_IN:            return "IN";
    case TOKEN_MATCH:         return "MATCH";
    case TOKEN_STRUCT:        return "STRUCT";
    case TOKEN_ENUM:          return "ENUM";
    case TOKEN_IMPL:          return "IMPL";
    case TOKEN_MODULE:        return "MODULE";
    case TOKEN_IMPORT:        return "IMPORT";
    case TOKEN_LOAD:          return "LOAD";
    case TOKEN_SELF:          return "SELF";
    case TOKEN_DO:            return "DO";
    case TOKEN_END:           return "END";
    case TOKEN_BREAK:         return "BREAK";
    case TOKEN_CONTINUE:      return "CONTINUE";
    case TOKEN_EXTERN:        return "EXTERN";
    case TOKEN_AS:            return "AS";
    case TOKEN_FROM:          return "FROM";
    case TOKEN_PUB:           return "PUB";
    case TOKEN_NEW:           return "NEW";
    case TOKEN_TRY:           return "TRY";
    case TOKEN_TYPE_ALIAS:    return "TYPE_ALIAS";
    case TOKEN_BLOCK:         return "BLOCK";
    case TOKEN_TRAIT:         return "TRAIT";
    case TOKEN_AT_TIME:       return "AT_TIME";
    case TOKEN_AT_BENCH:      return "AT_BENCH";
    case TOKEN_TYPE_INT:      return "TYPE_INT";
    case TOKEN_TYPE_I8:       return "TYPE_I8";
    case TOKEN_TYPE_I16:      return "TYPE_I16";
    case TOKEN_TYPE_I32:      return "TYPE_I32";
    case TOKEN_TYPE_I64:      return "TYPE_I64";
    case TOKEN_TYPE_U8:       return "TYPE_U8";
    case TOKEN_TYPE_U16:      return "TYPE_U16";
    case TOKEN_TYPE_U32:      return "TYPE_U32";
    case TOKEN_TYPE_U64:      return "TYPE_U64";
    case TOKEN_TYPE_F32:      return "TYPE_F32";
    case TOKEN_TYPE_F64:      return "TYPE_F64";
    case TOKEN_TYPE_BOOL:     return "TYPE_BOOL";
    case TOKEN_TYPE_CHAR:     return "TYPE_CHAR";
    case TOKEN_TYPE_STRING:   return "TYPE_STRING";
    case TOKEN_TYPE_VOID:     return "TYPE_VOID";
    case TOKEN_TYPE_LIB:      return "TYPE_LIB";
    case TOKEN_TYPE_OBJECT:   return "TYPE_OBJECT";
    case TOKEN_ARRAY:         return "ARRAY";
    case TOKEN_VEC:           return "VEC";
    case TOKEN_MAP:           return "MAP";
    case TOKEN_IDENTIFIER:    return "IDENTIFIER";
    case TOKEN_PLUS:          return "PLUS";
    case TOKEN_MINUS:         return "MINUS";
    case TOKEN_STAR:          return "STAR";
    case TOKEN_SLASH:         return "SLASH";
    case TOKEN_PERCENT:       return "PERCENT";
    case TOKEN_AMP:           return "AMP";
    case TOKEN_PIPE:          return "PIPE";
    case TOKEN_CARET:         return "CARET";
    case TOKEN_TILDE:         return "TILDE";
    case TOKEN_LSHIFT:        return "LSHIFT";
    case TOKEN_RSHIFT:        return "RSHIFT";
    case TOKEN_AND:           return "AND";
    case TOKEN_OR:            return "OR";
    case TOKEN_BANG:           return "BANG";
    case TOKEN_EQ:            return "EQ";
    case TOKEN_NEQ:           return "NEQ";
    case TOKEN_LT:            return "LT";
    case TOKEN_GT:            return "GT";
    case TOKEN_LEQ:           return "LEQ";
    case TOKEN_GEQ:           return "GEQ";
    case TOKEN_ASSIGN:        return "ASSIGN";
    case TOKEN_PLUS_ASSIGN:   return "PLUS_ASSIGN";
    case TOKEN_MINUS_ASSIGN:  return "MINUS_ASSIGN";
    case TOKEN_STAR_ASSIGN:   return "STAR_ASSIGN";
    case TOKEN_SLASH_ASSIGN:  return "SLASH_ASSIGN";
    case TOKEN_LPAREN:        return "LPAREN";
    case TOKEN_RPAREN:        return "RPAREN";
    case TOKEN_LBRACE:        return "LBRACE";
    case TOKEN_RBRACE:        return "RBRACE";
    case TOKEN_LBRACKET:      return "LBRACKET";
    case TOKEN_RBRACKET:      return "RBRACKET";
    case TOKEN_SEMICOLON:     return "SEMICOLON";
    case TOKEN_COLON:         return "COLON";
    case TOKEN_COMMA:         return "COMMA";
    case TOKEN_DOT:           return "DOT";
    case TOKEN_ARROW:         return "ARROW";
    case TOKEN_FAT_ARROW:     return "FAT_ARROW";
    case TOKEN_UNDERSCORE:    return "UNDERSCORE";
    case TOKEN_DOTDOT:        return "DOTDOT";
    case TOKEN_ELLIPSIS:      return "ELLIPSIS";
    case TOKEN_FSTRING_START: return "FSTRING_START";
    case TOKEN_FSTRING_TEXT:  return "FSTRING_TEXT";
    case TOKEN_FSTRING_END:   return "FSTRING_END";
    case TOKEN_EOF:           return "EOF";
    case TOKEN_ERROR:         return "ERROR";
    }
    return "UNKNOWN";
}
