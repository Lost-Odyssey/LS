/* scanner.h — Lexical analyzer interface */
#ifndef LS_SCANNER_H
#define LS_SCANNER_H

#include "token.h"
#include <stdbool.h>

typedef struct {
    const char *source;    /* Full source text */
    const char *start;     /* Start of current token */
    const char *current;   /* Current scan position */
    int line;
    int column;
    int start_column;      /* Column at token start */

    /* Format string state */
    bool in_fstring;       /* Inside f"..." scanning */
    int fstring_brace_depth; /* Nesting depth of {} inside f-string expression */

    /* Phase E.3.2: conditional compilation state.
       Stack of #if frames; each entry tracks whether its branch is currently
       active (emitting tokens), whether the parent block was emitting (so
       nested #if inside an inactive parent stays inactive), and whether
       any branch in this if/else chain has yet been taken (controls #else). */
    struct {
        bool active;
        bool parent_active;
        bool branch_taken;
    } cond_stack[16];
    int cond_depth;
} Scanner;

/* Initialize scanner with source text */
void scanner_init(Scanner *s, const char *source);

/* Return the next token, advancing the scanner */
Token scanner_next(Scanner *s);

/* Peek at the next token without consuming it */
Token scanner_peek(Scanner *s);

#endif /* LS_SCANNER_H */
