// B-3 (reverse bridge): a `Str` value flowing into a builtin-string slot.
//   var-decl : IDENT source -> clone (Str stays live); rvalue -> raw transfer
//   call-arg : IDENT only -> reinterpret + cap=-2 borrow (zero-copy)
//   return   : IDENT -> clone; rvalue -> raw transfer
// JIT+AOT+memcheck 0/0/0.
import std.str
import std.vec

fn check(bool ok, string what) {
    if !ok { print(f"STRBV FAIL: {what}") }
}

// Str rvalue producers
fn make_str() -> Str { return "made" }
fn make_owned_str() -> Str {
    Str s = "dyn"
    s.push_str("amic")
    return s
}

// builtin-string consumers
fn slen(string s) -> int { return s.length }
fn echo2(string a, string b) -> string { return a + b }

// return position: `-> string` fn returning Str
fn ret_ident() -> string {
    Str t = "from-ident"
    return t
}
fn ret_rvalue() -> string {
    return make_owned_str()
}

// builtin-string equality helper: routes a (possibly Str-flipped) literal
// back through a `string` param (expected==string) so the bridge's
// builtin-string source/result comparisons stay string==string both states.
fn seq(string a, string b) -> bool { return a == b }

fn main() {
    // var_decl, IDENT source (static Str): clone, source stays live
    Str sv = "hello"
    string a = sv
    check(seq(a, "hello"), "var IDENT static")
    check(sv.len() == 5, "Str src alive")

    // var_decl, IDENT source (owned Str): deep clone, both independent
    Str ov = "dyn"
    ov.push_str("amic")
    string b = ov
    check(seq(b, "dynamic"), "var IDENT owned")
    check(ov.eq?("dynamic"), "owned Str src alive")
    string bang = "!"        // pin to `string` (builtin append arg, not Str)
    b.append(bang)
    check(seq(b, "dynamic!"), "clone independent (string)")
    check(ov.eq?("dynamic"), "clone independent (Str)")

    // var_decl, rvalue source (call result): raw transfer
    string c = make_str()
    check(seq(c, "made"), "var rvalue static")
    string d = make_owned_str()
    check(seq(d, "dynamic"), "var rvalue owned")

    // call-arg: Str IDENT -> by-value string param (borrowed, zero-copy)
    check(slen(sv) == 5, "arg static Str")
    check(slen(ov) == 7, "arg owned Str")
    check(ov.eq?("dynamic"), "Str src alive after arg")
    string e = echo2(sv, ov)
    check(seq(e, "hellodynamic"), "two coerced args")

    // return position
    check(seq(ret_ident(), "from-ident"), "return IDENT")
    check(seq(ret_rvalue(), "dynamic"), "return rvalue")

    // coerced string remains independently mutable
    string f = sv
    string bang2 = "!"       // pin to `string` (builtin + operand, not Str)
    f = f + bang2
    check(seq(f, "hello!"), "coerced string mutates freely")
    check(sv.len() == 5, "Str src alive at end")

    print("STRBV PASS")
}
