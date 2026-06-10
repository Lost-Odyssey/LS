// opt_combinator_reject.ls — `.unwrap()` consumes a has_drop Option/Result
// (takes self by value, like force-unwrap `!`). Using the receiver again must be
// a compile-time move error. Driven by test_opt_combinator.cmake (expects a
// non-zero exit and a "moved" diagnostic; this file must NOT compile/run).
fn main() -> int {
    Option(string) s = Some("x")
    string a = s.unwrap()          // moves the owned payload out of s
    string b = s.unwrap()          // ERROR: use of moved variable 's'
    print(a)
    print(b)
    return 0
}
