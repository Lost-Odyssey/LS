/* repl_term.c — platform terminal primitives (Win32 / POSIX).
 * Must NOT include scanner.h/token.h: <windows.h> defines a clashing TokenType. */
#include "repl_term.h"

#ifdef _WIN32
#  include <windows.h>
#  include <conio.h>
#  include <io.h>
#  include <stdio.h>
#  define ISATTY(fd)  _isatty(fd)
#  define FILENO(f)   _fileno(f)
#else
#  include <stdio.h>
#  include <unistd.h>
#  include <termios.h>
#  define ISATTY(fd)  isatty(fd)
#  define FILENO(f)   fileno(f)
#endif

bool repl_term_stdin_is_tty(void)  { return ISATTY(FILENO(stdin))  != 0; }
bool repl_term_stdout_is_tty(void) { return ISATTY(FILENO(stdout)) != 0; }

#ifdef _WIN32

void repl_term_enable_vt(void) {
    HANDLE h = GetStdHandle(STD_OUTPUT_HANDLE);
    DWORD mode = 0;
    if (h != INVALID_HANDLE_VALUE && GetConsoleMode(h, &mode)) {
        SetConsoleMode(h, mode | ENABLE_VIRTUAL_TERMINAL_PROCESSING);
    }
}

void repl_term_raw_enable(void)  { /* _getch reads unbuffered already */ }
void repl_term_raw_disable(void) { }

int repl_term_read_key(void) {
    int c = _getch();
    if (c == 0 || c == 0xE0) {           /* extended key prefix */
        int c2 = _getch();
        switch (c2) {
            case 75: return K_LEFT;
            case 77: return K_RIGHT;
            case 72: return K_UP;
            case 80: return K_DOWN;
            case 71: return K_HOME;
            case 79: return K_END;
            case 83: return K_DELETE;
            default: return -2;
        }
    }
    return c;
}

#else  /* POSIX */

static struct termios g_saved_termios;

void repl_term_enable_vt(void) { /* ANSI assumed on POSIX terminals */ }

void repl_term_raw_enable(void) {
    struct termios raw;
    tcgetattr(STDIN_FILENO, &g_saved_termios);
    raw = g_saved_termios;
    raw.c_lflag &= ~(ICANON | ECHO);
    raw.c_cc[VMIN] = 1;
    raw.c_cc[VTIME] = 0;
    tcsetattr(STDIN_FILENO, TCSAFLUSH, &raw);
}

void repl_term_raw_disable(void) {
    tcsetattr(STDIN_FILENO, TCSAFLUSH, &g_saved_termios);
}

int repl_term_read_key(void) {
    unsigned char c;
    if (read(STDIN_FILENO, &c, 1) != 1) return 4;   /* EOF → treat as Ctrl-D */
    if (c == 27) {                                   /* ESC sequence */
        unsigned char seq[2];
        if (read(STDIN_FILENO, &seq[0], 1) != 1) return 27;
        if (read(STDIN_FILENO, &seq[1], 1) != 1) return 27;
        if (seq[0] == '[') {
            switch (seq[1]) {
                case 'A': return K_UP;
                case 'B': return K_DOWN;
                case 'C': return K_RIGHT;
                case 'D': return K_LEFT;
                case 'H': return K_HOME;
                case 'F': return K_END;
                case '3': {
                    unsigned char tilde;
                    if (read(STDIN_FILENO, &tilde, 1) == 1 && tilde == '~')
                        return K_DELETE;
                    return -2;
                }
                default: return -2;
            }
        }
        return -2;
    }
    return c;
}

#endif
