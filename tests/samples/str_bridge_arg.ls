// B-2: a builtin-string VARIABLE passed to a by-value `Str` parameter is
// deep-copied into an owned Str (caller owns the temp, callee borrows for the
// call). Restricted to IDENT args — owned string temps (sv.upper()) still need
// an explicit `Str t = ...` first. JIT+AOT+memcheck 0/0/0.
import std.str
import std.vec

fn check(bool ok, string what) {
    if !ok { print(f"STRBA FAIL: {what}") }
}

fn takelen(Str s) -> int { return s.len() }
fn shout(Str s) -> Str { return s.upper() }
fn joiner(Str a, Str b) -> Str { return a.concat(b) }

fn main() {
    // static string variable -> Str param (cap 0, shared; callee borrows)
    string sv = "hello"
    check(takelen(sv) == 5, "static var arg")
    check(sv == "hello", "static src alive")

    // owned string variable -> Str param (cap>0; deep copy, then freed)
    string ov = "dyn"
    ov.append("amic")
    check(takelen(ov) == 7, "owned var arg")
    check(ov == "dynamic", "owned src alive")

    // returns a Str built from a coerced arg
    Str r = shout(sv)
    check(r.to_string() == "HELLO", "coerced arg -> Str return")
    check(sv == "hello", "src alive after shout")

    // two coerced string args in one call
    string p = "ab"
    string q = "cd"
    Str j = joiner(p, q)
    check(j.to_string() == "abcd", "two coerced args")
    check(p == "ab", "p alive")
    check(q == "cd", "q alive")

    // coerced arg result flows into a Vec(Str) (owned chain) — memcheck guards
    Vec(Str) v = {}
    string w = "world"
    v.push(shout(w))
    check(v.get(0).to_string() == "WORLD", "coerced -> vec")

    // a string literal still coerces (zero-copy static), unaffected
    check(takelen("lit") == 3, "literal arg still works")

    print("STRBA PASS")
}
