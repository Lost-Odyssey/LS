// P5-0: Str.c_str() — NUL-terminated view for FFI consumers
// (docs/plan_p5_remove_builtin_string.md §4). Verified by reading the
// pointer back through CRT strlen: the reported C length must equal
// Str.len() in every state (static literal / owned / grown / empty /
// zero-init), and len() itself must never change.
import std.str

extern fn strlen(*u8 p) -> i64

fn check(bool ok, Str what) {
    if !ok { print(f"CSTR FAIL: {what}") }
}

fn main() {
    // static literal: zero-cost direct pointer (.rodata is NUL-terminated)
    Str a = "hello"
    check(strlen(a.c_str()) == (5 as i64), "static literal")
    check(a.len() == 5, "static len unchanged")
    check(a.cap() == 0, "static stays static (no copy)")

    // owned buffer (substr allocates; no NUL invariant before c_str)
    Str b = a.substr(1, 3)
    check(strlen(b.c_str()) == (3 as i64), "owned substr")
    check(b.len() == 3, "owned len unchanged")

    // grown via push_str (copy-on-grow off a static source)
    Str c = "ab"
    Str d = "cd"
    c.push_str(d)
    check(strlen(c.c_str()) == (4 as i64), "grown buffer")
    check(strlen(c.c_str()) == (4 as i64), "repeat call stable")

    // mutate after c_str, then c_str again
    c.push_byte(33)
    check(strlen(c.c_str()) == (5 as i64), "append after c_str")

    // empty literal (cap 0, len 0 -> materialises a 1-byte NUL buffer)
    Str e = ""
    check(strlen(e.c_str()) == (0 as i64), "empty literal")

    // zero-init Str (data nil)
    Str z
    check(strlen(z.c_str()) == (0 as i64), "zero-init nil data")

    print("CSTR PASS")
}
