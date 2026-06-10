// B-2b: a builtin-string value returned from a function declared `-> Str` is
// deep-copied into an owned Str (the source string keeps its normal cleanup;
// no transfer/skip-cleanup, no mark-temp-moved). Covers static/owned locals,
// owned string temps (method result, concat), borrowed &string params and a
// global. JIT+AOT+memcheck 0/0/0.
import std.str
import std.vec

fn check(bool ok, string what) {
    if !ok { print(f"STRBR FAIL: {what}") }
}

string GREET = "gday"

// static string local -> Str return (clone shares static, cap 0)
fn from_static() -> Str {
    string sv = "hello"
    return sv
}

// owned string local -> Str return (deep copy; local freed at cleanup)
fn from_owned() -> Str {
    string ov = "dyn"
    ov.append("amic")
    return ov
}

// owned string TEMP (builtin method result) -> Str return
fn from_temp(string base) -> Str {
    return base.upper()
}

// owned string TEMP (concat) -> Str return
fn from_concat(string a, string b) -> Str {
    return a + b
}

// borrowed &string param -> Str return (cap -2 source: must deep copy)
fn from_borrow(&string s) -> Str {
    return s
}

// global string -> Str return (global keeps ownership; deep copy)
fn from_global() -> Str {
    return GREET
}

// f-string in a `-> Str` fn resolves to Str natively (P2), no bridge needed
fn from_fstring(int n) -> Str {
    return f"n={n}"
}

fn main() {
    Str a = from_static()
    check(a.to_string() == "hello", "static local")

    Str b = from_owned()
    check(b.to_string() == "dynamic", "owned local")

    string base = "shout"
    Str c = from_temp(base)
    check(c.to_string() == "SHOUT", "owned temp (method result)")
    check(base == "shout", "src alive after temp return")

    Str d = from_concat("ab", "cd")
    check(d.to_string() == "abcd", "owned temp (concat)")

    string borrowed = "borrowme"
    Str e = from_borrow(borrowed)
    check(e.to_string() == "borrowme", "borrowed param")
    check(borrowed == "borrowme", "borrow src alive")

    Str g = from_global()
    check(g.to_string() == "gday", "global string")
    check(GREET == "gday", "global alive")

    Str h = from_fstring(42)
    check(h.to_string() == "n=42", "f-string native Str")

    // returned Str flows into owned containers — memcheck guards the chain
    Vec(Str) v = {}
    v.push(from_owned())
    v.push(from_temp("mix"))
    check(v.get(0).to_string() == "dynamic", "ret -> vec 0")
    check(v.get(1).to_string() == "MIX", "ret -> vec 1")

    print("STRBR PASS")
}
