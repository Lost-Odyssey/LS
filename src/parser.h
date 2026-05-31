/* parser.h — Pratt parser interface */
#ifndef LS_PARSER_H
#define LS_PARSER_H

#include "scanner.h"
#include "ast.h"

typedef struct {
    Scanner scanner;
    Token current;
    Token previous;
    bool had_error;
    bool panic_mode;
    const char *source_path;
    /* Phase A (closures): when parsing a `-> RET` return type, this is set so
       parse_type can reject a bare `Block(...)` and force a type alias. */
    bool in_return_type;
    /* True only for the top-level expression of an expression statement, where a
       following statement could legitimately begin with a pointer declaration
       (`*Foo bar`). Outside that boundary (e.g. a `= init` RHS, call args, parens)
       a bare `ident * ident` must always be multiplication, never a decl split.
       Set in parse_statement; cleared on entry to parse_expr_prec so nested
       sub-expressions never inherit it. */
    bool stmt_boundary;
} Parser;

/* Parse source text -> AST_PROGRAM node.
   Returns NULL if parse errors occurred.
   Errors are printed to stderr: [error] path:line:col: message */
AstNode *parse(const char *source, const char *source_path);

#endif /* LS_PARSER_H */
