// stack_xmod/main.ls — cross-module / transitive generic container use.
//
// main imports std.core.stack directly (Stack(Str)) AND imports `helper`, which
// itself imports std.core.stack and uses Stack(int) internally. Exercises:
//   - the same generic template (Stack) registered in two checkers (helper +
//     main) without a duplicate-registration crash (idempotent registration);
//   - a generic struct method (Stack(int).pop/push) referenced from a module
//     function body (helper.sum_pushed), emitted before the pending-gm block.
// Prints "STACK PASS" so it reuses test_stack.cmake.

module main

import std.core.stack
import helper

def main() {
    // Direct use in the root module: a different instantiation (Str).
    Stack(Str) ss = {}
    ss.push("x")
    ss.push("y")

    int n = ss.len()                 // 2
    int t = helper.sum_pushed()      // 60

    @print(f"n={n} t={t}")
    if n == 2 && t == 60 {
        @print("STACK PASS")
    } else {
        @print("FAIL xmod")
    }
}
