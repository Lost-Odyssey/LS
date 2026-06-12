// f-string interpolation of `Str` values (docs/plan_string_to_stdlib.md, P5
// prerequisite): `f"...{str_val}..."` formats a Str via "%.*s" (length-bounded).
// Works in both an f-string-producing-Str slot and inline in print(). Borrowed
// Str interps are not dropped; owned Str rvalue interps (call/index) are dropped
// after the result is built. JIT+AOT+memcheck 0/0/0.
import std.str
import std.vec

fn check(bool ok, Str what) {
    if !ok { print(f"STRFI FAIL: {what}") }
}

fn main() {
    Str a = "world"
    Str b = "WD"

    // Str var interpolated into an f-string that yields a Str
    Str g = f"hi {a}!"
    check(g.eq?("hi world!"), "borrow interp")

    // owned Str rvalue (method clone) interpolated — must drop, not leak
    Str u = f"up={a.upper()}"
    check(u.eq?("up=WORLD"), "owned interp")

    // mixed: borrow + owned rvalue + POD in one f-string
    Str m = f"[{a}]({a.upper()})#{a.len()}"
    check(m.eq?("[world](WORLD)#5"), "mixed interp")

    // Str interpolation inline in print() (separate codegen path) — eyeball + memcheck
    print(f"P:{a} {b} {a.upper()}")        // P:world WD WORLD

    // Str element from a Vec (owned clone rvalue via get) interpolated
    Vec(Str) v = {}
    v.push("alpha")
    v.push("beta")
    Str e = f"{v.get!(0)}-{v.get!(1)}"
    check(e.eq?("alpha-beta"), "vec elem interp")

    print("STRFI PASS")
}
