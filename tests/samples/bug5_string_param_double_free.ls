// Bug #5 regression test: string parameter double-free
// When a dynamic string is passed to a function, the callee must not free
// the caller's data. After this fix, cap is zeroed on entry so only the
// caller's scope cleanup frees it.

fn print_str(string s) {
    print(s)
}

fn modify_param(string s) {
    s = s.upper()
    print(s)
}

fn main() -> int {
    string s = "abc".upper()
    print_str(s)
    modify_param(s)
    print(s)
    return 0
}
