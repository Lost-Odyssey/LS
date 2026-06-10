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

fn main() {
    // var_decl, IDENT source (static Str): clone, source stays live
    Str sv = "hello"
    string a = sv
    check(a == "hello", "var IDENT static")
    check(sv.len() == 5, "Str src alive")

    // var_decl, IDENT source (owned Str): deep clone, both independent
    Str ov = "dyn"
    ov.push_str("amic")
    string b = ov
    check(b == "dynamic", "var IDENT owned")
    check(ov.to_string() == "dynamic", "owned Str src alive")
    b.append("!")
    check(b == "dynamic!", "clone independent (string)")
    check(ov.to_string() == "dynamic", "clone independent (Str)")

    // var_decl, rvalue source (call result): raw transfer
    string c = make_str()
    check(c == "made", "var rvalue static")
    string d = make_owned_str()
    check(d == "dynamic", "var rvalue owned")

    // call-arg: Str IDENT -> by-value string param (borrowed, zero-copy)
    check(slen(sv) == 5, "arg static Str")
    check(slen(ov) == 7, "arg owned Str")
    check(ov.to_string() == "dynamic", "Str src alive after arg")
    string e = echo2(sv, ov)
    check(e == "hellodynamic", "two coerced args")

    // return position
    check(ret_ident() == "from-ident", "return IDENT")
    check(ret_rvalue() == "dynamic", "return rvalue")

    // coerced string remains independently mutable
    string f = sv
    f = f + "!"
    check(f == "hello!", "coerced string mutates freely")
    check(sv.len() == 5, "Str src alive at end")

    print("STRBV PASS")
}
