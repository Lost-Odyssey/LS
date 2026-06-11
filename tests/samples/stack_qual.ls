// stack_qual.ls — module-qualified generic type `st.Stack(int)` (single-owner).
//
// Exercises the qualified-type form: `import std.stack as st` then use the
// generic as `st.Stack(int)` / `st.Stack(string)` in type position. The
// qualifier is validated against the module that owns the generic. Construction
// uses the (unambiguous) bare constructor; prints "STACK PASS" so it reuses
// test_stack.cmake.

import std.stack as st
import std.str

fn check(bool c, string l) {
    if c { print(f"ok {l}") } else { print(f"FAIL {l}") }
}

fn main() {
    st.Stack(int) si = new_stack(int)()
    si.push(1)
    si.push(2)
    si.push(3)
    check(si.len() == 3, "qual int len 3")
    check(si.peek() == 3, "qual int peek 3")
    check(si.pop() == 3, "qual int pop 3")
    check(si.len() == 2, "qual int len 2")

    st.Stack(Str) ss = new_stack(Str)()
    ss.push("alpha")
    ss.push("beta")
    check(ss.len() == 2, "qual str len 2")
    Str t = ss.peek()
    check(t.eq?("beta"), "qual str peek beta")

    print("STACK PASS")
}
