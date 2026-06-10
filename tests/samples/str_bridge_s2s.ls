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

fn main() {
    // string variable -> Str (deep copy; source stays alive)
    string sv = "hello"
    Str a = sv
    check(a.to_string() == "hello", "var s2s")
    check(sv == "hello", "source alive")

    // mutate the Str copy; source string unaffected (independent buffers)
    a.push_byte(33)
    check(a.to_string() == "hello!", "copy independent")
    check(sv == "hello", "source unmutated")

    // string-returning call -> Str
    Str b = gimme()
    check(b.to_string() == "dynamic", "call s2s")

    // string method result -> Str (owned temp; cloned then temp freed, no dbl-free)
    Str c = sv.upper()
    check(c.to_string() == "HELLO", "method s2s")

    // f-string (builtin) -> Str
    int n = 7
    Str d = f"n={n}"
    check(d.to_string() == "n=7", "fstr s2s")

    // empty string -> Str
    string es = ""
    Str e = es
    check(e.empty?(), "empty s2s")

    // the coerced Str works with ported methods + goes into a Vec
    Vec(Str) v = {}
    string w = "world"
    Str ws = w
    v.push(ws)
    check(v.get(0).to_string() == "world", "s2s into vec")

    // literal path (zero-copy static) still works alongside
    Str lit = "static"
    check(lit.to_string() == "static", "lit still ok")

    print("STRB PASS")
}
