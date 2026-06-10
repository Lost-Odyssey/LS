// opt_combinator_panic.ls — `.unwrap()` on None must abort the process (panic),
// like force-unwrap `!`. The line after the bad unwrap must NOT run. Driven by
// test_opt_combinator.cmake (expects non-zero exit + an "[unwrap]" diagnostic).
fn main() -> int {
    Option(int) a = None
    int x = a.unwrap()             // None -> print "[unwrap] ..." + abort()
    print(f"AFTER {x}")            // must never run
    return 0
}
