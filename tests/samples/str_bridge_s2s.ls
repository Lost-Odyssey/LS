// B-step migration bridge (docs/plan_string_to_stdlib.md): a builtin-string VALUE
// initializing a `Str` var is deep-copied into an owned Str (var-decl site).
// Reuses emit_string_clone_val + reinterpret — static stays shared, owned/borrowed
// deep-copy. Lets `Str x = <string expr>` work during migration. JIT+AOT+memcheck.
import std.str
import std.vec

fn check(bool ok, string what) {
    if !ok { print(f"STRB FAIL: {what}") }
}

fn gimme() -> string { string s = "dynamic"  return s }

// builtin-string equality helper: routes a (possibly Str-flipped) literal
// back through a `string` param (expected==string) so the bridge's
// builtin-string source/result comparisons stay string==string both states.
fn seq(string a, string b) -> bool { return a == b }

fn main() {
    // string variable -> Str (deep copy; source stays alive)
    string sv = "hello"
    Str a = sv
    check(a.eq?("hello"), "var s2s")
    check(seq(sv, "hello"), "source alive")

    // mutate the Str copy; source string unaffected (independent buffers)
    a.push_byte(33)
    check(a.eq?("hello!"), "copy independent")
    check(seq(sv, "hello"), "source unmutated")

    // string-returning call -> Str
    Str b = gimme()
    check(b.eq?("dynamic"), "call s2s")

    // string method result -> Str (owned temp; cloned then temp freed, no dbl-free)
    Str c = sv.upper()
    check(c.eq?("HELLO"), "method s2s")

    // f-string (builtin) -> Str
    int n = 7
    Str d = f"n={n}"
    check(d.eq?("n=7"), "fstr s2s")

    // empty string -> Str
    string es = ""
    Str e = es
    check(e.empty?(), "empty s2s")

    // the coerced Str works with ported methods + goes into a Vec
    Vec(Str) v = {}
    string w = "world"
    Str ws = w
    v.push(ws)
    check(v.get(0).eq?("world"), "s2s into vec")

    // literal path (zero-copy static) still works alongside
    Str lit = "static"
    check(lit.eq?("static"), "lit still ok")

    print("STRB PASS")
}
