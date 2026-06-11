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

// builtin-string equality helper: routes a (possibly Str-flipped) literal
// back through a `string` param (expected==string) so the bridge's
// builtin-string source/result comparisons stay string==string both states.
fn seq(string a, string b) -> bool { return a == b }

fn main() {
    // static string variable -> Str param (cap 0, shared; callee borrows)
    string sv = "hello"
    check(takelen(sv) == 5, "static var arg")
    check(seq(sv, "hello"), "static src alive")

    // owned string variable -> Str param (cap>0; deep copy, then freed)
    string ov = "dyn"
    string amic = "amic"     // pin to `string` (builtin append arg, not Str)
    ov.append(amic)
    check(takelen(ov) == 7, "owned var arg")
    check(seq(ov, "dynamic"), "owned src alive")

    // returns a Str built from a coerced arg
    Str r = shout(sv)
    check(r.eq?("HELLO"), "coerced arg -> Str return")
    check(seq(sv, "hello"), "src alive after shout")

    // two coerced string args in one call
    string p = "ab"
    string q = "cd"
    Str j = joiner(p, q)
    check(j.eq?("abcd"), "two coerced args")
    check(seq(p, "ab"), "p alive")
    check(seq(q, "cd"), "q alive")

    // coerced arg result flows into a Vec(Str) (owned chain) — memcheck guards
    Vec(Str) v = {}
    string w = "world"
    v.push(shout(w))
    check(v.get(0).eq?("WORLD"), "coerced -> vec")

    // a string literal still coerces (zero-copy static), unaffected
    check(takelen("lit") == 3, "literal arg still works")

    print("STRBA PASS")
}
