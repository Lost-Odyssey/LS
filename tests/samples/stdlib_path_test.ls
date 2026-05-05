// Phase E.3.4 — stdlib path resolution
// Imports `hello` which has no user-relative file but is provided by
// <ls.exe-dir>/stdlib/hello.ls. Verifies the three-level lookup
// (user dir → LS_HOME/stdlib → builtin) works.

import hello

fn main() {
    int a = hello.answer()
    if a != 42 {
        print("FAIL: stdlib hello.answer expected 42")
        return
    }
    print("PASS: stdlib hello.answer() = 42")

    string g = hello.greet("LS")
    if g != "Hello from stdlib, LS!" {
        print("FAIL: stdlib hello.greet wrong text")
        print(g)
        return
    }
    print("PASS: stdlib hello.greet")

    print("ALL PASS")
}
