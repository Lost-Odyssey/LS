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
} Parser;

/* Parse source text -> AST_PROGRAM node.
   Returns NULL if parse errors occurred.
   Errors are printed to stderr: [error] path:line:col: message */
AstNode *parse(const char *source, const char *source_path);

#endif /* LS_PARSER_H */
