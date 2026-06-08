// stack_test.ls — std.stack correctness + ownership/memcheck probe.
//
// Prints "ok <label>" per passing check and "FAIL <label>" on mismatch, then
// "STACK PASS" at the end. The driver (test_stack.cmake) asserts STACK PASS is
// present, FAIL is absent, and (under --memcheck) 0 leak / 0 double-free.

import std.stack

fn check(bool cond, string label) {
    if cond {
        print(f"ok {label}")
    } else {
        print(f"FAIL {label}")
    }
}

fn main() {
    // ---- Stack(int): POD element ----
    Stack(int) si = new_stack(int)()
    check(si.is_empty(), "int empty init")
    check(si.len() == 0, "int len 0")

    si.push(1)
    si.push(2)
    si.push(3)
    check(si.len() == 3, "int len 3")
    check(si.peek() == 3, "int peek 3")
    check(si.len() == 3, "int peek non-destructive")

    check(si.pop() == 3, "int pop 3")
    check(si.pop() == 2, "int pop 2")
    check(si.len() == 1, "int len 1 after pops")
    check(si.is_empty() == false, "int not empty")

    si.clear()
    check(si.is_empty(), "int empty after clear")

    // ---- Stack(string): has_drop element ----
    Stack(string) ss = new_stack(string)()
    ss.push("alpha")
    ss.push("beta")
    ss.push("gamma")
    check(ss.len() == 3, "str len 3")

    // peek clones the top; the original stays in the stack.
    string top = ss.peek()
    check(top == "gamma", "str peek gamma")
    check(ss.len() == 3, "str peek non-destructive")

    // pop moves the top out; `g` owns it, the stack no longer does.
    string g = ss.pop()
    check(g == "gamma", "str pop gamma")
    check(ss.len() == 2, "str len 2 after pop")

    // ss still owns "alpha" and "beta": when it goes out of scope the stack
    // drops its Vec(string), which must free both remaining strings exactly
    // once (the memcheck probe for generic struct + Vec(T) field drop).
    print("STACK PASS")
}
