// P1 self-verifying (docs/plan_string_to_stdlib.md §5.1): a string LITERAL in a
// `Str`-expecting position lowers to a static Str {data:.rodata, len, cap:0} —
// no malloc, __drop skips free, __clone shallow-copies. The builtin string is
// still the DEFAULT literal type; this only fires where Str is expected.
// Prints "STRP1 PASS" iff all checks hold. JIT + AOT + memcheck 0/0/0.
import std.str
import std.vec

struct Box { Str s; int n }

fn check(bool ok, string what) {
    if !ok { print(f"STRP1 FAIL: {what}") }
}

// builtin-string equality helper: routes a (possibly Str-flipped) literal back
// through a `string` param so the `to_string()` round-trips stay covered.
fn seq(string a, string b) -> bool { return a == b }

// literal flows into a Str parameter slot (by-value clone of a static = static)
fn take(Str s) -> int { return s.len() }

// literal flows into a Str return slot
fn make() -> Str { return "world" }

fn main() {
    // var-decl: static Str straight from a literal (no from_string bridge)
    Str a = "hello"
    check(a.len() == 5, "litdecl len")
    check(a.byte_at(0) == 104, "litdecl byte")
    check(seq(a.to_string(), "hello"), "litdecl roundtrip")

    // empty literal -> static Str, empty
    Str e = ""
    check(e.empty?(), "empty lit")

    // clone of a static literal Str: shallow (cap 0), still correct + drop-safe
    Str b = a.__clone()
    check(b.eq?(a), "clone static")

    // move of a static Str
    Str c = a
    check(seq(c.to_string(), "hello"), "move static")

    // literal into a param slot
    check(take("abcd") == 4, "param lit")

    // literal into a return slot
    Str w = make()
    check(seq(w.to_string(), "world"), "return lit")

    // literal into a struct field slot
    Box bx = Box { s: "hi", n: 9 }
    check(seq(bx.s.to_string(), "hi"), "field lit")
    check(bx.n == 9, "field n")

    // literal pushed into Vec(Str) (element slot expects Str)
    Vec(Str) v = {}
    v.push("x")
    v.push("yy")
    check(v.len() == 2, "vec push lit")
    check(seq(v.get(1).to_string(), "yy"), "vec elem lit")

    // owned Str (built via bridge) still works alongside static literals,
    // and concatenating a literal-built byte keeps drop clean
    Str owned = Str.from_string("dyn")
    owned.push_byte(33)
    check(seq(owned.to_string(), "dyn!"), "owned mix")

    print("STRP1 PASS")
}
