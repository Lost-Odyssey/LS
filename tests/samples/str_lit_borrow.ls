// gap① fix (docs/plan_string_to_stdlib.md §5.1): a string literal passed where a
// read-only `&Str` is expected coerces to a (static) Str and auto-borrows — in
// both method-arg and free-function-arg positions. The literal-coerced Str rvalue
// is spilled and borrowed by existing codegen; static (cap 0) so drop is a no-op.
// (gap② — a bare literal *receiver* like "x".trim() — is still pre-P5.)
// JIT+AOT+memcheck 0/0/0.
import std.str
import std.vec

fn check(bool ok, string what) {
    if !ok { print(f"STRLB FAIL: {what}") }
}

fn cnt(&Str s, &Str sub) -> int { return s.count(sub) }

fn main() {
    Str s = "a.b.c.b"

    // literal -> &Str in METHOD-arg position
    check(s.find("b") == 2, "method find lit")
    check(s.contains?("c"), "method contains lit")
    check(!s.contains?("zzz"), "method contains miss lit")
    check(s.starts_with?("a."), "method starts lit")
    check(s.ends_with?(".b"), "method ends lit")
    check(s.count(".") == 3, "method count lit")
    check(s.rfind("b") == 6, "method rfind lit")

    // literal -> &Str in FREE-FUNCTION-arg position
    check(cnt(s, ".") == 3, "freefn lit")

    // multiple literal args + chaining
    check(s.replace(".", "-").eq?("a-b-c-b"), "replace lits")
    check(s.split(".").len() == 4, "split lit")

    // borrowing a literal does not disturb the receiver
    check(s.find("c") == 4, "find c")
    check(s.eq?("a.b.c.b"), "receiver intact")

    print("STRLB PASS")
}
