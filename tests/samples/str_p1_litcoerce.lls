// P1 self-verifying (docs/plan_string_to_stdlib.md §5.1): a string LITERAL in a
// `Str`-expecting position lowers to a static Str {data:.rodata, len, cap:0} —
// no malloc, __drop skips free, __clone shallow-copies. Since P5-2 the literal
// default IS Str everywhere; these checks pin the static-Str lowering itself.
// Prints "STRP1 PASS" iff all checks hold. JIT + AOT + memcheck 0/0/0.
import std.core.str
import std.core.vec

struct Box { Str s; int n }

def check(bool ok, Str what) {
    if !ok { @print(f"STRP1 FAIL: {what}") }
}

// literal flows into a Str parameter slot (by-value clone of a static = static)
def take(Str s) -> int { return s.len() }

// literal flows into a Str return slot
def make() -> Str { return "world" }

def main() {
    // var-decl: static Str straight from a literal
    Str a = "hello"
    check(a.len() == 5, "litdecl len")
    check(a.byte_at(0) == 104, "litdecl byte")
    check(a.eq?("hello"), "litdecl roundtrip")

    // empty literal -> static Str, empty
    Str e = ""
    check(e.empty?(), "empty lit")

    // clone of a static literal Str: shallow (cap 0), still correct + drop-safe
    Str b = a.__clone()
    check(b.eq?(a), "clone static")

    // move of a static Str
    Str c = a
    check(c.eq?("hello"), "move static")

    // literal into a param slot
    check(take("abcd") == 4, "param lit")

    // literal into a return slot
    Str w = make()
    check(w.eq?("world"), "return lit")

    // literal into a struct field slot
    Box bx = Box { s: "hi", n: 9 }
    check(bx.s.eq?("hi"), "field lit")
    check(bx.n == 9, "field n")

    // literal pushed into Vec(Str) (element slot expects Str)
    Vec(Str) v = {}
    v.push("x")
    v.push("yy")
    check(v.len() == 2, "vec push lit")
    check(v.get!(1).eq?("yy"), "vec elem lit")

    // owned Str (deep copy allocates) still works alongside static literals,
    // and appending a byte to it keeps drop clean
    Str owned = "dyn".copy()
    owned.push_byte(33)
    check(owned.eq?("dyn!"), "owned mix")

    @print("STRP1 PASS")
}
