/* repl_term.h — Low-level terminal primitives for the REPL line editor.
 *
 * Isolated from scanner.h/token.h on purpose: the Win32 <windows.h> header
 * defines an identifier `TokenType` that collides with the project's own
 * TokenType enum, so all platform console code lives in its own translation
 * unit (repl_term.c) that never includes the scanner headers.
 */
#ifndef LS_REPL_TERM_H
#define LS_REPL_TERM_H

#include <stdbool.h>

/* Virtual key codes returned by repl_term_read_key for navigation keys.
   Printable input and control chars (Enter, Backspace=8/127, Ctrl-C=3,
   Ctrl-D=4) are returned as their raw byte value. -2 means "ignore". */
enum {
    K_LEFT = 1000, K_RIGHT, K_UP, K_DOWN, K_HOME, K_END, K_DELETE
};

bool repl_term_stdin_is_tty(void);
bool repl_term_stdout_is_tty(void);

/* Enable ANSI/VT processing on the output console (Windows); no-op elsewhere. */
void repl_term_enable_vt(void);

/* Raw mode toggle for char-by-char input (POSIX termios); no-op on Windows
   since _getch already reads unbuffered. */
void repl_term_raw_enable(void);
void repl_term_raw_disable(void);

/* Read one keystroke, decoding arrow/nav escape sequences into K_* codes. */
int repl_term_read_key(void);

#endif /* LS_REPL_TERM_H */
