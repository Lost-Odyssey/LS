/* parser_internal.h — the genuine cross-TU surface shared across the
   parser translation units (Task 4.3, docs/plan_arch_round2_backlog.md
   Batch 4).

   These are exactly the parser helpers/types that are *called or referenced
   from a TU other than the one that defines them* — nothing more. Every
   helper used only within its own TU (all `prefix_*`/`infix_*` Pratt
   handlers, the `rules[]` table, `get_rule`, every internal declaration-body
   sub-helper, etc.) stays `static` there and does NOT appear here. If a
   helper is ever needed from a second TU, move its definition's `static`
   off and add its prototype below under the group owned by its defining TU.

   Defining TUs of the declarations below:
     - parser.c      — token-stream primitives (advance/consume/check/...),
                       error reporting, AST/TypeNode allocation, the three
                       depth-guarded recursive entry points
                       (parse_expr_prec/parse_type/parse_block — each a thin
                       wrapper around a same-named `*_inner` defined in
                       another TU), and the top-level `parse()` entry point.
     - parser_expr.c — Pratt expression parser (`prefix_*`/`infix_*` handlers,
                       the `rules[]` table, `get_rule`, parse_expr_prec_inner).
     - parser_type.c — type-node parser (is_type_keyword, parse_type_inner).
     - parser_decl.c — declaration parsers (var/fn/struct/enum/trait/impl/
                       type-alias/module/import/load-lib/extern).
     - parser_stmt.c — statement parsers (if/while/for/return/comptime/block,
                       parse_statement, parse_block_inner).
   Each prototype's owning TU is noted inline in the block below.

   The public Parser struct / parser API lives in parser.h (included
   below). */
#ifndef LS_PARSER_INTERNAL_H
#define LS_PARSER_INTERNAL_H

#include "parser.h"

/* Pratt-parser precedence ladder. Shared across TUs because parse_expr_prec
   (parser.c) takes a Precedence argument and every TU that parses a
   sub-expression (parser_expr.c/parser_decl.c/parser_stmt.c) calls it with
   one of these levels. PrefixFn/InfixFn/ParseRule stay local `static` types
   in parser_expr.c — nothing outside that TU touches the rule table. */
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

/* ---- [def: parser.c] token-stream primitives, error reporting, AST/
   TypeNode allocation. Used from every other parser TU. */
char *str_dup_n(const char *s, int len);
AstNode *new_node(AstNodeType kind, int line, int col);
TypeNode *new_type_node(TypeNodeKind kind, int line, int col);
void error_at(Parser *p, Token *tok, const char *msg);
void error_at_current(Parser *p, const char *msg);
void error_at_previous(Parser *p, const char *msg);
void error_at_previous_no_panic(Parser *p, const char *msg);
void advance(Parser *p);
bool consume(Parser *p, TokenType type, const char *msg);
bool check(Parser *p, TokenType type);
bool match_tok(Parser *p, TokenType type);
void skip_semicolons(Parser *p);
void synchronize(Parser *p);
void recover_in_body(Parser *p);
char *process_string_token(const char *start, int length);

/* ---- [def: parser.c] depth-guarded recursive entry points. Each wraps a
   same-named `*_inner` defined in another TU (see that TU's group below);
   the wrapper itself stays in parser.c so all three share one
   `p->depth` guard next to `parse_depth_enter`. */
AstNode *parse_expr_prec(Parser *p, Precedence min_prec);
TypeNode *parse_type(Parser *p);
AstNode *parse_block(Parser *p);

/* ---- [def: parser_expr.c] Pratt expression parser body, called only by
   the parse_expr_prec wrapper above. */
AstNode *parse_expr_prec_inner(Parser *p, Precedence min_prec);

/* ---- [def: parser_type.c] type-node parser. is_type_keyword is also used
   by parser_expr.c (disambiguating `*T`/generic-arg lookahead) and
   parser_decl.c (starts_var_decl's built-in-type fast path);
   parse_type_inner is called only by the parse_type wrapper above. */
bool is_type_keyword(TokenType t);
TypeNode *parse_type_inner(Parser *p);

/* ---- [def: parser_decl.c] declaration parsers. starts_var_decl is also
   used by parser_stmt.c (parse_for_clause_stmt / parse_statement) to
   decide whether a leading token begins a local variable declaration; the
   rest are called from parser_stmt.c's parse_statement dispatch. */
bool starts_var_decl(Parser *p);
AstNode *parse_var_decl(Parser *p);
AstNode *parse_fn_decl(Parser *p, bool allow_operator_name);
AstNode *parse_struct_decl(Parser *p);
AstNode *parse_enum_decl(Parser *p);
AstNode *parse_trait_decl(Parser *p);
AstNode *parse_impl_decl(Parser *p);
AstNode *parse_type_alias_decl(Parser *p);
AstNode *parse_module_decl(Parser *p);
AstNode *parse_import_decl(Parser *p);
AstNode *parse_load_lib(Parser *p);
AstNode *parse_extern_fn(Parser *p);
AstNode *parse_extern_struct(Parser *p);
AstNode *parse_extern_block(Parser *p);

/* ---- [def: parser_stmt.c] statement parsers. parse_block_inner is called
   only by the parse_block wrapper above; parse_statement is also called
   from parser_expr.c's infix_call (`.call { ... }` trailing-closure sugar
   parses a full statement) and from parser.c's top-level parse() loop. */
AstNode *parse_block_inner(Parser *p);
AstNode *parse_statement(Parser *p);

#endif /* LS_PARSER_INTERNAL_H */
