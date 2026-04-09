/* builtins.c — Runtime built-in function implementations for AOT-compiled programs.
   These are linked into the final executable produced by AOT compilation.
   Note: For the compiler itself (ls.exe), builtins are declared as LLVM IR
   in codegen.c. This file is only needed if we compile a separate runtime library. */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

/* print(string) — print a string followed by newline */
void ls_print(const char *s) {
    if (s) {
        puts(s);
    } else {
        puts("(nil)");
    }
}

/* print_int(int) — print an integer */
void ls_print_int(int value) {
    printf("%d\n", value);
}

/* print_f64(double) — print a float */
void ls_print_f64(double value) {
    printf("%g\n", value);
}

/* print_bool(int) — print a boolean */
void ls_print_bool(int value) {
    printf("%s\n", value ? "true" : "false");
}
