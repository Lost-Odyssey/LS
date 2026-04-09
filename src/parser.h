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
} Parser;

/* Parse source text -> AST_PROGRAM node.
   Returns NULL if parse errors occurred.
   Errors are printed to stderr: [error] path:line:col: message */
AstNode *parse(const char *source, const char *source_path);

#endif /* LS_PARSER_H */
