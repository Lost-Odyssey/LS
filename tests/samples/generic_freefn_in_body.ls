// generic_freefn_in_body.ls — regression for the generic-free-function call-site
// mangling gap (docs/plan_generic_freefn_mangle.md).
//
// A generic callable (free function OR method-level generic method) invoked with
// an ABSTRACT type param `T` from inside another generic body used to emit the
// abstract symbol at codegen (no alias context) → "undefined function 'make(T)'"
// / "Symbols not found: Box(int).conv(T)". The fix stashes the checker-resolved
// concrete type names on the call node and makes codegen prefer them. Covers:
// abstract T to a free fn / a method-level generic / a container-returning free
// fn from a generic method body; abstract T to a free fn from a generic free-fn
// body; concrete-type control cases; int + Str (has_drop) elements.
//
// Prints "ok <label>" / "FAIL <label>", then "GFN PASS".

import std.core.str
import std.core.vec

def identity(T)(T x) -> T { return x }

// A generic free function that builds a generic container parameterized by the
// caller's abstract T — the call-with-abstract-T shape that first surfaced the gap.
def singleton(T)(T x) -> Vec(T) {
    Vec(T) v = {}
    v.push(x)
    return v
}

// Generic free function whose body calls another generic free function with its
// OWN abstract T (generic-in-generic, free-function flavor).
def twice(T)(T x) -> T { return identity(T)(x) }

struct Box(T) { T v }

methods Box(T) {
    // abstract T passed to a generic free function from a generic method body
    def echo(&self) -> T { return identity(T)(self.v) }
    // concrete type arg from a generic method body (the path that already worked)
    def seven(&self) -> int { return identity(int)(7) }
    // generic free fn returning a generic container, by abstract T
    def wrap(&self) -> Vec(T) { return singleton(T)(self.v) }

    // ---- method-level generic (the twin gap) ----
    // a method that carries its OWN type param U, independent of the struct's T
    def conv(U)(&self, U y) -> U { return y }
    // call the method-level generic with the outer ABSTRACT T from a generic body
    def via_method_abstract(&self) -> T { return self.conv(T)(self.v) }
    // and with a concrete type (the path that already worked)
    def via_method_concrete(&self) -> int { return self.conv(int)(9) }
}

def check(bool cond, Str label) {
    if cond { @print(f"ok {label}") } else { @print(f"FAIL {label}") }
}

def main() {
    Box(int) bi = Box(int){ v: 42 }
    check(bi.seven() == 7, "concrete arg in method body")
    check(bi.echo() == 42, "abstract T in method body (int)")
    check(twice(int)(5) == 5, "abstract T in free-fn body (int)")

    Vec(int) wi = bi.wrap()
    check(wi.len() == 1, "container-returning free fn (int) len")
    check(wi[0] == 42, "container-returning free fn (int) value")

    check(bi.via_method_concrete() == 9, "method-level generic, concrete arg")
    check(bi.via_method_abstract() == 42, "method-level generic, abstract T (int)")

    // has_drop element: same paths must keep ownership correct.
    Box(Str) bs = Box(Str){ v: "alpha" }
    Str e = bs.echo()
    check(e.eq?("alpha"), "abstract T in method body (Str)")
    Vec(Str) ws = bs.wrap()
    check(ws.len() == 1, "container-returning free fn (Str) len")
    check(ws[0].eq?("alpha"), "container-returning free fn (Str) value")
    Str me = bs.via_method_abstract()
    check(me.eq?("alpha"), "method-level generic, abstract T (Str)")

    @print("GFN PASS")
}
